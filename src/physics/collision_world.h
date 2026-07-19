#pragma once

#include <memory>
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
    void Synchronize();
    void RemoveEntity(edict_t* entity);

    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] bool WouldProjectileHit(const edict_t* projectile,
                                          float frameTime) const;

private:
    struct CBoxCollider {
        edict_t* entity = nullptr;
        btVector3 halfExtents;
        std::unique_ptr<btBoxShape> shape;
        std::unique_ptr<btCollisionObject> object;
    };

    void Initialize();
    void Shutdown();
    void ClearColliders();
    void ClearWorldGeometry();
    bool BuildWorldGeometry(edict_t* worldEntity);
    void UpdateCollider(int entityIndex, edict_t* entity);
    void RemoveCollider(int entityIndex);

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
    bool m_active = false;
    bool m_worldReady = false;
};

}  // namespace Bvh
