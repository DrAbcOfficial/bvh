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

    const bool pluginEnabled = IsPluginEnabled();
    if (!m_hasEnabledState || m_lastEnabled != pluginEnabled) {
        DebugLog(1, "Projectile management %s (bvh_enabled).",
                 pluginEnabled ? "enabled" : "disabled");
        m_hasEnabledState = true;
        m_lastEnabled = pluginEnabled;
    }

    if (!worldReady || !pluginEnabled) {
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
                    ++m_throttledRestoredThink;
                }
                ++engineFallbackCount;
                continue;
            }

            if (m_collisionWorld.WouldProjectileHit(projectile, gpGlobals->frametime)) {
                if (tracked->second.suppressed) {
                    projectile->v.solid = tracked->second.initialSolid;
                    tracked->second.suppressed = false;
                    ++m_throttledRestoredHit;
                }
                ++collisionCandidateCount;
            } else {
                const bool wasCulled = tracked->second.suppressed;
                projectile->v.solid = SOLID_NOT;
                tracked->second.suppressed = true;
                ++culledCount;
                if (!wasCulled) {
                    ++m_throttledCulled;
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
    FlushTransitionLogs();
}

void CProjectileGate::Reset()
{
    RestoreSuppressedProjectiles();
}

void CProjectileGate::FlushTransitionLogs()
{
    if (m_throttledCulled == 0 && m_throttledRestoredHit == 0 && m_throttledRestoredThink == 0) {
        return;
    }

    const double now = gpGlobals != nullptr ? static_cast<double>(gpGlobals->time) : 0.0;
    if (now < m_nextTransitionFlush) {
        return;
    }

    DebugLog(1, "Recent transitions: %d culled, %d restored on predicted hit, %d restored on Think.",
             m_throttledCulled, m_throttledRestoredHit, m_throttledRestoredThink);
    m_throttledCulled = 0;
    m_throttledRestoredHit = 0;
    m_throttledRestoredThink = 0;
    m_nextTransitionFlush = now + 0.5;
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
