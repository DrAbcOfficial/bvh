#include "runtime/projectile_gate.h"

#include <cstring>
#include <vector>

#include <extdll.h>
#include <enginecallback.h>
#include <util.h>

#include "config/projectile_class_config.h"
#include "runtime/debug_log.h"

namespace Bvh {

CProjectileGate::CProjectileGate(CCollisionWorld& collisionWorld,
                                 const CProjectileClassConfig& projectileClassConfig)
    : m_collisionWorld(collisionWorld),
      m_projectileClassConfig(projectileClassConfig)
{
}

void CProjectileGate::Update()
{
    m_collisionWorld.Synchronize();
    const bool worldReady = m_collisionWorld.IsReady() && gpGlobals != nullptr;
    if (!m_hasWorldReadyState || m_lastWorldReady != worldReady) {
        DebugLog(1, "Projectile gate %s.", worldReady ? "enabled" : "disabled");
        m_hasWorldReadyState = true;
        m_lastWorldReady = worldReady;
    }

    if (!worldReady) {
        RestoreSuppressedProjectiles();
        return;
    }

    m_staleScratch.clear();
    for (const auto& [entityIndex, tracked] : m_trackedProjectiles) {
        if (tracked.entity == nullptr || tracked.entity->free || !IsProjectile(tracked.entity)) {
            m_staleScratch.push_back(entityIndex);
        }
    }
    for (const int entityIndex : m_staleScratch) {
        const auto found = m_trackedProjectiles.find(entityIndex);
        ForgetTrackedProjectile(entityIndex, found == m_trackedProjectiles.end() ? nullptr : found->second.entity,
                                "entity expired");
        m_suppressedProjectiles.erase(entityIndex);
    }

    int projectileCount = 0;
    int managedCount = 0;
    int culledCount = 0;
    int collisionCandidateCount = 0;
    int engineFallbackCount = 0;
    for (int entityIndex = 1; entityIndex < gpGlobals->maxEntities; ++entityIndex) {
        edict_t* projectile = INDEXENT(entityIndex);
        if (projectile == nullptr || projectile->free || !IsProjectile(projectile)) {
            continue;
        }
        ++projectileCount;

        auto suppressed = m_suppressedProjectiles.find(entityIndex);
        if (suppressed != m_suppressedProjectiles.end() && suppressed->second != projectile) {
            ForgetTrackedProjectile(entityIndex, nullptr, "entity index reused");
            m_suppressedProjectiles.erase(suppressed);
            suppressed = m_suppressedProjectiles.end();
        }

        if (suppressed != m_suppressedProjectiles.end() && projectile->v.solid != SOLID_NOT) {
            // Script code has ended BVH suppression. Keep the original solid
            // state so a later collision still restores the spawn behavior.
            m_suppressedProjectiles.erase(suppressed);
            const auto tracked = m_trackedProjectiles.find(entityIndex);
            if (tracked != m_trackedProjectiles.end()) {
                tracked->second.culled = false;
            }
        }

        ++managedCount;
        TrackProjectile(entityIndex, projectile);

        // Think callbacks can retarget or accelerate BDSC projectiles after
        // StartFrame. Preserve engine collision for that frame instead of
        // sweeping an already stale trajectory.
        if (!CanUseLinearSweep(projectile) ||
            (projectile->v.nextthink > 0.0f && projectile->v.nextthink <= gpGlobals->time)) {
            const bool wasCulled = m_suppressedProjectiles.erase(entityIndex) != 0;
            projectile->v.solid = m_trackedProjectiles[entityIndex].initialSolid;
            m_trackedProjectiles[entityIndex].culled = false;
            ++engineFallbackCount;
            if (wasCulled) {
                DebugLog(1, "Projectile %d restored initial solidity %d (%s).", entityIndex,
                         projectile->v.solid,
                         CanUseLinearSweep(projectile) ? "due Think" : "nonlinear movement");
            }
            continue;
        }

        if (m_collisionWorld.WouldProjectileHit(projectile, gpGlobals->frametime)) {
            const bool wasCulled = m_suppressedProjectiles.erase(entityIndex) != 0;
            projectile->v.solid = m_trackedProjectiles[entityIndex].initialSolid;
            m_trackedProjectiles[entityIndex].culled = false;
            ++collisionCandidateCount;
            if (wasCulled) {
                DebugLog(1, "Projectile %d predicted collision; restored initial solidity %d.",
                         entityIndex, projectile->v.solid);
            }
        } else {
            const bool wasCulled = m_suppressedProjectiles.find(entityIndex) !=
                                   m_suppressedProjectiles.end();
            projectile->v.solid = SOLID_NOT;
            m_suppressedProjectiles[entityIndex] = projectile;
            m_trackedProjectiles[entityIndex].culled = true;
            ++culledCount;
            if (!wasCulled) {
                DebugLog(1, "Projectile %d BVH-culled; set SOLID_NOT.", entityIndex);
            }
        }
    }

    DebugLog(2, "Projectile frame: scanned=%d managed=%d culled=%d collision=%d fallback=%d.",
             projectileCount, managedCount, culledCount, collisionCandidateCount, engineFallbackCount);
}

void CProjectileGate::Reset()
{
    RestoreSuppressedProjectiles();
}

void CProjectileGate::Forget(edict_t* entity)
{
    if (entity == nullptr) {
        return;
    }

    const int entityIndex = ENTINDEX(entity);
    ForgetTrackedProjectile(entityIndex, entity, "entity deleted");
    const auto suppressed = m_suppressedProjectiles.find(entityIndex);
    if (suppressed != m_suppressedProjectiles.end() && suppressed->second == entity) {
        m_suppressedProjectiles.erase(suppressed);
    }
}

bool CProjectileGate::IsProjectile(const edict_t* entity) const
{
    return entity != nullptr && m_projectileClassConfig.IsManaged(STRING(entity->v.classname));
}

bool CProjectileGate::CanUseLinearSweep(const edict_t* projectile) const
{
    return projectile->v.movetype == MOVETYPE_FLY ||
           projectile->v.movetype == MOVETYPE_FLYMISSILE;
}

void CProjectileGate::TrackProjectile(int entityIndex, edict_t* projectile)
{
    const auto [found, inserted] = m_trackedProjectiles.emplace(
        entityIndex, CTrackedProjectile{projectile, projectile->v.solid, false});
    if (inserted) {
        DebugLog(1,
                 "Projectile %d added to BVH: solid=%d movetype=%d velocity=(%.1f %.1f %.1f).",
                 entityIndex, projectile->v.solid, projectile->v.movetype,
                  projectile->v.velocity.x, projectile->v.velocity.y, projectile->v.velocity.z);
    } else if (found->second.entity != projectile) {
        found->second = {projectile, projectile->v.solid, false};
        DebugLog(1, "Projectile %d reused; BVH tracking reset.", entityIndex);
    }
}

void CProjectileGate::ForgetTrackedProjectile(int entityIndex, edict_t* projectile,
                                               const char* reason)
{
    const auto found = m_trackedProjectiles.find(entityIndex);
    if (found == m_trackedProjectiles.end() ||
        (projectile != nullptr && found->second.entity != projectile)) {
        return;
    }

    DebugLog(1, "Projectile %d removed from BVH (%s, state=%s).", entityIndex, reason,
             found->second.culled ? "culled" : "collision-enabled");
    m_trackedProjectiles.erase(found);
}

void CProjectileGate::RestoreSuppressedProjectiles()
{
    for (const auto& [entityIndex, projectile] : m_suppressedProjectiles) {
        if (projectile != nullptr && !projectile->free && IsProjectile(projectile) &&
            projectile->v.solid == SOLID_NOT) {
            const auto tracked = m_trackedProjectiles.find(entityIndex);
            if (tracked == m_trackedProjectiles.end()) {
                DebugLog(1, "Projectile %d suppression state had no initial solidity.", entityIndex);
                continue;
            }

            projectile->v.solid = tracked->second.initialSolid;
            tracked->second.culled = false;
        }
    }
    m_suppressedProjectiles.clear();
    m_trackedProjectiles.clear();
}

}  // namespace Bvh
