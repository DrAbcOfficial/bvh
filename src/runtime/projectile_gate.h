#pragma once

#include <unordered_map>
#include <vector>

#include "physics/collision_world.h"
#include "runtime/perf_stats.h"

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
    void PrintStats() const;

private:
    struct CTrackedProjectile {
        edict_t* entity = nullptr;
        int initialSolid = 0;
        bool suppressed = false;
    };

    [[nodiscard]] bool IsProjectile(const edict_t* entity) const;
    [[nodiscard]] bool CanUseLinearSweep(const edict_t* projectile) const;
    std::unordered_map<int, CTrackedProjectile>::iterator TrackProjectile(
        int entityIndex, edict_t* projectile);
    void ForgetExpiredProjectiles();
    void ForgetTrackedProjectile(int entityIndex, edict_t* projectile, const char* reason);
    void RestoreSuppressedProjectiles();

    CCollisionWorld& m_collisionWorld;
    const CProjectileClassConfig& m_projectileClassConfig;
    std::unordered_map<int, CTrackedProjectile> m_trackedProjectiles;
    std::vector<int> m_staleScratch;
    CPerfStats m_perfStats;
    bool m_hasWorldReadyState = false;
    bool m_lastWorldReady = false;
};

}  // namespace Bvh
