#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>

#include <LinearMath/btVector3.h>

#include "engine/model_provider.h"

struct edict_s;
typedef struct edict_s edict_t;

class btBvhTriangleMeshShape;
class btBoxShape;
class btCollisionDispatcher;
class btCollisionObject;
class btCollisionWorld;
struct btDbvtBroadphase;
class btDefaultCollisionConfiguration;
class btTriangleMesh;

namespace Bvh {

class CCollisionWorld {
public:
    CCollisionWorld();
    ~CCollisionWorld();

    CCollisionWorld(const CCollisionWorld&) = delete;
    CCollisionWorld& operator=(const CCollisionWorld&) = delete;

    bool Activate(edict_t* worldEntity);
    void Deactivate();

    // Single-pass synchronization, driven by the gate's own entity scan:
    // BeginFrame stamps a new generation and prunes colliders not re-marked
    // by SynchronizeEntity during that frame.
    void BeginFrame();
    void SynchronizeEntity(int entityIndex, edict_t* entity);
    void RemoveEntity(edict_t* entity);

    [[nodiscard]] bool IsReady() const;

    // Sweep the projectile over `frameTime` seconds of linear motion and
    // return the closest hit fraction in [0, 1]: anything below 1.0 is a
    // predicted collision, 1.0 means clear. Invalid queries return 0.0 so a
    // missing prediction fails open and leaves GoldSrc authoritative.
    [[nodiscard]] float SweepProjectile(const edict_t* projectile,
                                        float frameTime) const;

    [[nodiscard]] int GetColliderCount() const;
    [[nodiscard]] int GetSweepCount() const;
    // Sweeps in the current frame whose broadphase walk encountered the
    // projectile's owner collider and skipped it (muzzle-frame false hits).
    [[nodiscard]] int GetOwnerFilteredCount() const;
    [[nodiscard]] int GetRayQueryCount() const;
    [[nodiscard]] int GetBoxQueryCount() const;
    [[nodiscard]] int GetWorldTriangleCount() const;

private:
    struct CBoxCollider {
        edict_t* entity = nullptr;
        btVector3 halfExtents;
        btVector3 lastCenter;
        float lastYaw = 0.0f;
        bool hasLastCenter = false;
        std::uint32_t lastSyncGeneration = 0;
        // Non-null when the collider follows a per-brush-model mesh instead
        // of a fitted box; the shape is shared via m_brushModelShapes.
        btBvhTriangleMeshShape* meshShape = nullptr;
        std::unique_ptr<btBoxShape> shape;
        std::unique_ptr<btCollisionObject> object;
    };

    struct CBrushModelShape {
        std::unique_ptr<btTriangleMesh> mesh;
        std::unique_ptr<btBvhTriangleMeshShape> shape;
    };

    void Initialize();
    void Shutdown();
    void ClearColliders();
    void ClearWorldGeometry();
    bool BuildWorldGeometry(edict_t* worldEntity);
    void UpdateCollider(int entityIndex, edict_t* entity);
    void RemoveCollider(int entityIndex);

    [[nodiscard]] btBoxShape* ResolveProjectileShape(const btVector3& halfExtents) const;
    [[nodiscard]] btBvhTriangleMeshShape* ResolveBrushModelShape(edict_t* entity);
    [[nodiscard]] bool IsTargetEntity(int entityIndex, const edict_t* entity) const;

    std::unique_ptr<btDefaultCollisionConfiguration> m_collisionConfiguration;
    std::unique_ptr<btCollisionDispatcher> m_dispatcher;
    std::unique_ptr<btDbvtBroadphase> m_broadphase;
    std::unique_ptr<btCollisionWorld> m_collisionWorld;

    std::unique_ptr<btTriangleMesh> m_worldTriangleMesh;
    std::unique_ptr<btBvhTriangleMeshShape> m_worldMeshShape;
    std::unique_ptr<btCollisionObject> m_worldMeshObject;
    std::unique_ptr<btBoxShape> m_worldBoundsShape;
    std::unique_ptr<btCollisionObject> m_worldBoundsObject;

    CModelProvider m_modelProvider;
    std::unordered_map<int, CBoxCollider> m_colliders;
    std::unordered_map<int, CBrushModelShape> m_brushModelShapes;
    std::uint32_t m_syncGeneration = 0;
    mutable int m_sweepCount = 0;
    mutable int m_ownerFilteredCount = 0;
    mutable int m_rayQueryCount = 0;
    mutable int m_boxQueryCount = 0;
    mutable std::map<std::tuple<int, int, int>, std::unique_ptr<btBoxShape>> m_shapeCache;
    int m_worldTriangleCount = 0;
    bool m_active = false;
    bool m_worldReady = false;
};

}  // namespace Bvh
