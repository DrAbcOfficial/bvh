#include "runtime/projectile_gate.h"

#include <chrono>
#include <cstring>
#include <vector>

#include <extdll.h>
#include <enginecallback.h>
#include <meta_api.h>

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
    const auto start = std::chrono::steady_clock::now();
    CFrameCounters counters;

    const bool worldReady = m_collisionWorld.IsReady() && gpGlobals != nullptr;
    if (!m_hasWorldReadyState || m_lastWorldReady != worldReady) {
        DebugLog(1, "Projectile gate %s.", worldReady ? "enabled" : "disabled");
        m_hasWorldReadyState = true;
        m_lastWorldReady = worldReady;
    }

    if (!worldReady) {
        RestoreSuppressedProjectiles();
    } else {
        ForgetExpiredProjectiles();
        m_collisionWorld.BeginFrame();

        int projectileCount = 0;
        int managedCount = 0;
        int culledCount = 0;
        int collisionCandidateCount = 0;
        int engineFallbackCount = 0;

        // Single pass over the entity list: collider synchronization and
        // projectile management share one scan.
        for (int entityIndex = 1; entityIndex < gpGlobals->maxEntities; ++entityIndex) {
            edict_t* projectile = INDEXENT(entityIndex);
            if (projectile == nullptr || projectile->free) {
                continue;
            }
            m_collisionWorld.SynchronizeEntity(entityIndex, projectile);

            // Discovery pre-filter: untracked entities only become manageable
            // while moving linearly; tracked ones always fall through so a
            // mid-flight movetype change still reaches the restore path.
            if (!CanUseLinearSweep(projectile) &&
                m_trackedProjectiles.find(entityIndex) == m_trackedProjectiles.end()) {
                continue;
            }
            if (!IsProjectile(projectile)) {
                continue;
            }
            ++projectileCount;

            auto tracked = m_trackedProjectiles.find(entityIndex);
            if (tracked == m_trackedProjectiles.end()) {
                tracked = TrackProjectile(entityIndex, projectile);
            } else if (tracked->second.entity != projectile) {
                tracked->second = CTrackedProjectile{projectile, projectile->v.solid, false};
                DebugLog(1, "Projectile %d reused; BVH tracking reset.", entityIndex);
            }

            if (tracked->second.suppressed && projectile->v.solid != SOLID_NOT) {
                // Script code has ended BVH suppression. Keep the original
                // solid state so a later collision still restores the spawn
                // behavior.
                tracked->second.suppressed = false;
            }

            ++managedCount;

            // Think callbacks can retarget or accelerate BDSC projectiles after
            // StartFrame. Preserve engine collision for that frame instead of
            // sweeping an already stale trajectory.
            if (!CanUseLinearSweep(projectile) ||
                (projectile->v.nextthink > 0.0f && projectile->v.nextthink <= gpGlobals->time)) {
                if (tracked->second.suppressed) {
                    projectile->v.solid = tracked->second.initialSolid;
                    tracked->second.suppressed = false;
                    DebugLog(1, "Projectile %d restored initial solidity %d (%s).", entityIndex,
                             projectile->v.solid,
                             CanUseLinearSweep(projectile) ? "due Think" : "nonlinear movement");
                }
                ++engineFallbackCount;
                continue;
            }

            if (m_collisionWorld.WouldProjectileHit(projectile, gpGlobals->frametime)) {
                if (tracked->second.suppressed) {
                    projectile->v.solid = tracked->second.initialSolid;
                    tracked->second.suppressed = false;
                    DebugLog(1, "Projectile %d predicted collision; restored initial solidity %d.",
                             entityIndex, projectile->v.solid);
                }
                ++collisionCandidateCount;
            } else {
                const bool wasCulled = tracked->second.suppressed;
                projectile->v.solid = SOLID_NOT;
                tracked->second.suppressed = true;
                ++culledCount;
                if (!wasCulled) {
                    DebugLog(1, "Projectile %d BVH-culled; set SOLID_NOT.", entityIndex);
                }
            }
        }

        counters.projectiles = projectileCount;
        counters.managed = managedCount;
        counters.culled = culledCount;
        counters.collision = collisionCandidateCount;
        counters.fallback = engineFallbackCount;

        DebugLog(2, "Projectile frame: scanned=%d managed=%d culled=%d collision=%d fallback=%d.",
                 projectileCount, managedCount, culledCount, collisionCandidateCount,
                 engineFallbackCount);
    }

    counters.colliders = m_collisionWorld.GetColliderCount();
    counters.sweeps = m_collisionWorld.GetSweepCount();
    counters.updateMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    m_perfStats.AddFrame(counters);
}

void CProjectileGate::Reset()
{
    RestoreSuppressedProjectiles();
}

void CProjectileGate::PrintStats() const
{
    LOG_CONSOLE(PLID, "[BVH] world: ready=%d colliders=%d triangles=%d.",
                m_collisionWorld.IsReady() ? 1 : 0,
                m_collisionWorld.GetColliderCount(),
                m_collisionWorld.GetWorldTriangleCount());
    m_perfStats.Print();
}

void CProjectileGate::Forget(edict_t* entity)
{
    if (entity == nullptr) {
        return;
    }

    const int entityIndex = ENTINDEX(entity);
    ForgetTrackedProjectile(entityIndex, entity, "entity deleted");
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

std::unordered_map<int, CProjectileGate::CTrackedProjectile>::iterator
CProjectileGate::TrackProjectile(int entityIndex, edict_t* projectile)
{
    const auto [iterator, inserted] = m_trackedProjectiles.emplace(
        entityIndex, CTrackedProjectile{projectile, projectile->v.solid, false});
    if (inserted) {
        DebugLog(1,
                 "Projectile %d added to BVH: solid=%d movetype=%d velocity=(%.1f %.1f %.1f).",
                 entityIndex, projectile->v.solid, projectile->v.movetype,
                 projectile->v.velocity.x, projectile->v.velocity.y, projectile->v.velocity.z);
    }
    return iterator;
}

void CProjectileGate::ForgetExpiredProjectiles()
{
    m_staleScratch.clear();
    for (const auto& [entityIndex, tracked] : m_trackedProjectiles) {
        if (tracked.entity == nullptr || tracked.entity->free || !IsProjectile(tracked.entity)) {
            m_staleScratch.push_back(entityIndex);
        }
    }
    for (const int entityIndex : m_staleScratch) {
        ForgetTrackedProjectile(entityIndex, nullptr, "entity expired");
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
             found->second.suppressed ? "culled" : "collision-enabled");
    m_trackedProjectiles.erase(found);
}

void CProjectileGate::RestoreSuppressedProjectiles()
{
    for (auto& [entityIndex, tracked] : m_trackedProjectiles) {
        if (tracked.entity != nullptr && !tracked.entity->free && tracked.suppressed &&
            IsProjectile(tracked.entity) && tracked.entity->v.solid == SOLID_NOT) {
            tracked.entity->v.solid = tracked.initialSolid;
            tracked.suppressed = false;
        }
    }
    m_trackedProjectiles.clear();
}

}  // namespace Bvh
