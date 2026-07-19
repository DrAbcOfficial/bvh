#include "runtime/projectile_gate.h"

#include <cstring>
#include <vector>

#include <extdll.h>
#include <enginecallback.h>
#include <util.h>

namespace {

constexpr const char* kProjectileClassname = "bdsc_bullet_proj";

}  // namespace

namespace Bvh {

CProjectileGate::CProjectileGate(CCollisionWorld& collisionWorld)
    : m_collisionWorld(collisionWorld)
{
}

void CProjectileGate::Update()
{
    m_collisionWorld.Synchronize();
    if (!m_collisionWorld.IsReady() || gpGlobals == nullptr) {
        RestoreSuppressedProjectiles();
        return;
    }

    std::vector<int> staleProjectiles;
    for (const auto& [entityIndex, projectile] : m_suppressedProjectiles) {
        if (projectile == nullptr || projectile->free || !IsProjectile(projectile)) {
            staleProjectiles.push_back(entityIndex);
        }
    }
    for (const int entityIndex : staleProjectiles) {
        m_suppressedProjectiles.erase(entityIndex);
    }

    for (int entityIndex = 1; entityIndex < gpGlobals->maxEntities; ++entityIndex) {
        edict_t* projectile = INDEXENT(entityIndex);
        if (projectile == nullptr || projectile->free || !IsProjectile(projectile)) {
            continue;
        }

        const auto suppressed = m_suppressedProjectiles.find(entityIndex);
        const bool managedByBvh = projectile->v.solid == SOLID_TRIGGER ||
                                  (suppressed != m_suppressedProjectiles.end() &&
                                   suppressed->second == projectile);
        if (!managedByBvh) {
            continue;
        }

        // Think callbacks can retarget or accelerate BDSC projectiles after
        // StartFrame. Preserve engine collision for that frame instead of
        // sweeping an already stale trajectory.
        if (!CanUseLinearSweep(projectile) ||
            (projectile->v.nextthink > 0.0f && projectile->v.nextthink <= gpGlobals->time)) {
            projectile->v.solid = SOLID_TRIGGER;
            m_suppressedProjectiles.erase(entityIndex);
            continue;
        }

        if (m_collisionWorld.WouldProjectileHit(projectile, gpGlobals->frametime)) {
            projectile->v.solid = SOLID_TRIGGER;
            m_suppressedProjectiles.erase(entityIndex);
        } else {
            projectile->v.solid = SOLID_NOT;
            m_suppressedProjectiles[entityIndex] = projectile;
        }
    }
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
    const auto found = m_suppressedProjectiles.find(entityIndex);
    if (found != m_suppressedProjectiles.end() && found->second == entity) {
        m_suppressedProjectiles.erase(found);
    }
}

bool CProjectileGate::IsProjectile(const edict_t* entity) const
{
    return entity != nullptr &&
           std::strcmp(STRING(entity->v.classname), kProjectileClassname) == 0;
}

bool CProjectileGate::CanUseLinearSweep(const edict_t* projectile) const
{
    return projectile->v.movetype == MOVETYPE_FLY ||
           projectile->v.movetype == MOVETYPE_FLYMISSILE;
}

void CProjectileGate::RestoreSuppressedProjectiles()
{
    for (const auto& [_, projectile] : m_suppressedProjectiles) {
        if (projectile != nullptr && !projectile->free && IsProjectile(projectile) &&
            projectile->v.solid == SOLID_NOT) {
            projectile->v.solid = SOLID_TRIGGER;
        }
    }
    m_suppressedProjectiles.clear();
}

}  // namespace Bvh
