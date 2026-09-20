#pragma once

#include <unordered_map>
#include <vector>

#include "physics/collision_world.h"

struct edict_s;
typedef struct edict_s edict_t;

namespace Bvh {

class CProjectileClassConfig;

class CProjectileGate {
public:
    CProjectileGate(CCollisionWorld& collisionWorld,
                    const CProjectileClassConfig& projectileClassConfig);

    void Update();
    void Reset();
    void Forget(edict_t* entity);

private:
    struct CTrackedProjectile {
        edict_t* entity = nullptr;
        int initialSolid = 0;
        bool culled = false;
    };

    [[nodiscard]] bool IsProjectile(const edict_t* entity) const;
    [[nodiscard]] bool CanUseLinearSweep(const edict_t* projectile) const;
    void TrackProjectile(int entityIndex, edict_t* projectile);
    void ForgetTrackedProjectile(int entityIndex, edict_t* projectile, const char* reason);
    void RestoreSuppressedProjectiles();

    CCollisionWorld& m_collisionWorld;
    const CProjectileClassConfig& m_projectileClassConfig;
    std::unordered_map<int, edict_t*> m_suppressedProjectiles;
    std::unordered_map<int, CTrackedProjectile> m_trackedProjectiles;
    std::vector<int> m_staleScratch;
    bool m_hasWorldReadyState = false;
    bool m_lastWorldReady = false;
};

}  // namespace Bvh
