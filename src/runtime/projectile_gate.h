#pragma once

#include <unordered_map>

#include "physics/collision_world.h"

struct edict_s;
typedef struct edict_s edict_t;

namespace Bvh {

class CProjectileGate {
public:
    explicit CProjectileGate(CCollisionWorld& collisionWorld);

    void Update();
    void Reset();
    void Forget(edict_t* entity);

private:
    [[nodiscard]] bool IsProjectile(const edict_t* entity) const;
    [[nodiscard]] bool CanUseLinearSweep(const edict_t* projectile) const;
    void RestoreSuppressedProjectiles();

    CCollisionWorld& m_collisionWorld;
    std::unordered_map<int, edict_t*> m_suppressedProjectiles;
};

}  // namespace Bvh
