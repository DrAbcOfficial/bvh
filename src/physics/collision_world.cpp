#include "physics/collision_world.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>
#include <vector>

#include <extdll.h>
#include <enginecallback.h>
#include <meta_api.h>
#include <com_model.h>

#include "runtime/debug_log.h"

#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>
#include <BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h>
#include <BulletCollision/CollisionDispatch/btCollisionDispatcher.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>

namespace {

constexpr int kWorldBoundsUserIndex = -2;
constexpr float kMinimumHalfExtent = 0.5f;
constexpr float kExtentTolerance = 0.01f;

bool IsUsableExtent(const btVector3& halfExtents)
{
    return halfExtents.x() >= kMinimumHalfExtent &&
           halfExtents.y() >= kMinimumHalfExtent &&
           halfExtents.z() >= kMinimumHalfExtent;
}

bool NearlyEqual(const btVector3& left, const btVector3& right)
{
    return (left - right).length2() <= kExtentTolerance * kExtentTolerance;
}

btTransform MakeTransform(const btVector3& origin)
{
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(origin);
    return transform;
}

bool GetEntityBounds(const edict_t* entity, btVector3& center, btVector3& halfExtents,
                     bool includePredictedMotion)
{
    Vector minimum = entity->v.absmin;
    Vector maximum = entity->v.absmax;

    if (maximum.x <= minimum.x || maximum.y <= minimum.y || maximum.z <= minimum.z) {
        minimum = entity->v.origin + entity->v.mins;
        maximum = entity->v.origin + entity->v.maxs;
    }

    if (includePredictedMotion && gpGlobals != nullptr &&
        std::isfinite(gpGlobals->frametime) && gpGlobals->frametime > 0.0f) {
        const Vector movement = entity->v.velocity * gpGlobals->frametime;
        if (std::isfinite(movement.x) && std::isfinite(movement.y) &&
            std::isfinite(movement.z)) {
            minimum.x = std::min(minimum.x, minimum.x + movement.x);
            minimum.y = std::min(minimum.y, minimum.y + movement.y);
            minimum.z = std::min(minimum.z, minimum.z + movement.z);
            maximum.x = std::max(maximum.x, maximum.x + movement.x);
            maximum.y = std::max(maximum.y, maximum.y + movement.y);
            maximum.z = std::max(maximum.z, maximum.z + movement.z);
        }
    }

    center = btVector3((minimum.x + maximum.x) * 0.5f,
                       (minimum.y + maximum.y) * 0.5f,
                       (minimum.z + maximum.z) * 0.5f);
    halfExtents = btVector3((maximum.x - minimum.x) * 0.5f,
                            (maximum.y - minimum.y) * 0.5f,
                            (maximum.z - minimum.z) * 0.5f);
    return IsUsableExtent(halfExtents);
}

class CProjectileSweepCallback final : public btCollisionWorld::ClosestConvexResultCallback {
public:
    CProjectileSweepCallback(const btVector3& from, const btVector3& to)
        : btCollisionWorld::ClosestConvexResultCallback(from, to)
    {
    }

    bool needsCollision(btBroadphaseProxy* proxy) const override
    {
        if (!btCollisionWorld::ClosestConvexResultCallback::needsCollision(proxy)) {
            return false;
        }

        const auto* object = static_cast<const btCollisionObject*>(proxy->m_clientObject);
        return object != nullptr && object->getUserIndex() != kWorldBoundsUserIndex;
    }
};

bool IsIgnoredWorldSurface(const msurface_t& surface)
{
    constexpr int kSurfaceDrawSky = 0x04;
    constexpr int kSurfaceDrawTurb = 0x10;
    constexpr int kSurfaceUnderwater = 0x80;
    return (surface.flags & (kSurfaceDrawSky | kSurfaceDrawTurb | kSurfaceUnderwater)) != 0;
}

void CollectVisibleWorldSurfaces(const model_t& worldModel, const mnode_t* node,
                                 std::vector<const msurface_t*>& surfaces)
{
    if (node == nullptr || node->contents < 0) {
        return;
    }

    CollectVisibleWorldSurfaces(worldModel, node->children[0], surfaces);

    const int firstSurface = node->firstsurface;
    const int lastSurface = firstSurface + node->numsurfaces;
    if (firstSurface >= 0 && lastSurface <= worldModel.numsurfaces) {
        for (int index = firstSurface; index < lastSurface; ++index) {
            surfaces.push_back(&worldModel.surfaces[index]);
        }
    }

    CollectVisibleWorldSurfaces(worldModel, node->children[1], surfaces);
}

}  // namespace

namespace Bvh {

CCollisionWorld::CCollisionWorld() = default;

CCollisionWorld::~CCollisionWorld()
{
    Shutdown();
}

bool CCollisionWorld::Activate(edict_t* worldEntity)
{
    Deactivate();
    Initialize();

    if (!BuildWorldGeometry(worldEntity)) {
        return false;
    }

    m_active = true;
    Synchronize();
    return true;
}

void CCollisionWorld::Deactivate()
{
    m_active = false;
    m_worldReady = false;
    ClearColliders();
    ClearWorldGeometry();
}

void CCollisionWorld::Synchronize()
{
    if (!m_active || !m_worldReady || gpGlobals == nullptr) {
        return;
    }

    std::unordered_set<int> activeColliders;
    for (int index = 1; index < gpGlobals->maxEntities; ++index) {
        edict_t* entity = INDEXENT(index);
        if (!IsTargetEntity(index, entity)) {
            continue;
        }

        activeColliders.insert(index);
        UpdateCollider(index, entity);
    }

    for (auto iterator = m_colliders.begin(); iterator != m_colliders.end();) {
        if (activeColliders.find(iterator->first) == activeColliders.end()) {
            const int entityIndex = iterator->first;
            ++iterator;
            RemoveCollider(entityIndex);
        } else {
            ++iterator;
        }
    }

}

void CCollisionWorld::RemoveEntity(edict_t* entity)
{
    if (entity == nullptr) {
        return;
    }

    RemoveCollider(ENTINDEX(entity));
}

bool CCollisionWorld::IsReady() const
{
    return m_active && m_worldReady && m_collisionWorld != nullptr;
}

bool CCollisionWorld::WouldProjectileHit(const edict_t* projectile, float frameTime) const
{
    if (!IsReady() || projectile == nullptr || !std::isfinite(frameTime) || frameTime <= 0.0f) {
        // A missing or invalid query must leave GoldSrc collision authoritative.
        return true;
    }

    btVector3 center;
    btVector3 halfExtents;
    if (!GetEntityBounds(projectile, center, halfExtents, false)) {
        halfExtents = btVector3(kMinimumHalfExtent, kMinimumHalfExtent, kMinimumHalfExtent);
        center = btVector3(projectile->v.origin.x, projectile->v.origin.y, projectile->v.origin.z);
    }

    const btVector3 velocity(projectile->v.velocity.x,
                             projectile->v.velocity.y,
                             projectile->v.velocity.z);
    if (velocity.length2() <= SIMD_EPSILON) {
        return false;
    }

    const btTransform from = MakeTransform(center);
    const btTransform to = MakeTransform(center + velocity * frameTime);
    btBoxShape projectileShape(halfExtents);
    CProjectileSweepCallback callback(from.getOrigin(), to.getOrigin());
    callback.m_collisionFilterGroup = btBroadphaseProxy::DefaultFilter;
    callback.m_collisionFilterMask = btBroadphaseProxy::DefaultFilter |
                                     btBroadphaseProxy::StaticFilter;

    m_collisionWorld->convexSweepTest(&projectileShape, from, to, callback);
    return callback.hasHit();
}

void CCollisionWorld::Initialize()
{
    if (m_collisionWorld != nullptr) {
        return;
    }

    m_collisionConfiguration = std::make_unique<btDefaultCollisionConfiguration>();
    m_dispatcher = std::make_unique<btCollisionDispatcher>(m_collisionConfiguration.get());
    m_broadphase = std::make_unique<btDbvtBroadphase>();
    m_collisionWorld = std::make_unique<btCollisionWorld>(m_dispatcher.get(),
                                                           m_broadphase.get(),
                                                           m_collisionConfiguration.get());
}

void CCollisionWorld::Shutdown()
{
    Deactivate();
    m_collisionWorld.reset();
    m_broadphase.reset();
    m_dispatcher.reset();
    m_collisionConfiguration.reset();
}

void CCollisionWorld::ClearColliders()
{
    if (m_collisionWorld != nullptr) {
        for (auto& [_, collider] : m_colliders) {
            m_collisionWorld->removeCollisionObject(collider.object.get());
        }
    }
    m_colliders.clear();
}

void CCollisionWorld::ClearWorldGeometry()
{
    if (m_collisionWorld != nullptr) {
        if (m_worldMeshObject != nullptr) {
            m_collisionWorld->removeCollisionObject(m_worldMeshObject.get());
        }
        if (m_worldBoundsObject != nullptr) {
            m_collisionWorld->removeCollisionObject(m_worldBoundsObject.get());
        }
    }

    m_worldBoundsObject.reset();
    m_worldBoundsShape.reset();
    m_worldMeshObject.reset();
    m_worldMeshShape.reset();
    m_worldTriangleMesh.reset();
}

bool CCollisionWorld::BuildWorldGeometry(edict_t* worldEntity)
{
    if (m_collisionWorld == nullptr || worldEntity == nullptr) {
        return false;
    }

    model_t* worldModel = m_modelProvider.GetModel(worldEntity);
    if (worldModel == nullptr) {
        return false;
    }
    if (worldModel->type != mod_brush ||
        worldModel->surfaces == nullptr || worldModel->vertexes == nullptr ||
        worldModel->edges == nullptr || worldModel->surfedges == nullptr) {
        LOG_ERROR(PLID, "World model %d is not a complete BSP collision model.",
                  worldEntity->v.modelindex);
        return false;
    }

    auto triangleMesh = std::make_unique<btTriangleMesh>(true, false);
    int triangleCount = 0;

    std::vector<const msurface_t*> worldSurfaces;
    if (worldModel->nodes != nullptr && worldModel->numnodes > 0) {
        CollectVisibleWorldSurfaces(*worldModel, worldModel->nodes, worldSurfaces);
    } else {
        worldSurfaces.reserve(static_cast<size_t>(worldModel->numsurfaces));
        for (int surfaceIndex = 0; surfaceIndex < worldModel->numsurfaces; ++surfaceIndex) {
            worldSurfaces.push_back(&worldModel->surfaces[surfaceIndex]);
        }
    }

    for (const msurface_t* surfacePtr : worldSurfaces) {
        const msurface_t& surface = *surfacePtr;
        if (IsIgnoredWorldSurface(surface) || surface.numedges < 3) {
            continue;
        }
        if (surface.firstedge < 0 ||
            surface.firstedge + surface.numedges > worldModel->numsurfedges) {
            continue;
        }

        std::vector<btVector3> vertices;
        vertices.reserve(static_cast<size_t>(surface.numedges));

        for (int edgeOffset = 0; edgeOffset < surface.numedges; ++edgeOffset) {
            const int signedEdge = worldModel->surfedges[surface.firstedge + edgeOffset];
            const int edgeIndex = signedEdge >= 0 ? signedEdge : -signedEdge;
            if (edgeIndex < 0 || edgeIndex >= worldModel->numedges) {
                vertices.clear();
                break;
            }

            const medge_t& edge = worldModel->edges[edgeIndex];
            const int vertexIndex = signedEdge >= 0 ? edge.v[0] : edge.v[1];
            if (vertexIndex < 0 || vertexIndex >= worldModel->numvertexes) {
                vertices.clear();
                break;
            }

            const Vector& vertex = worldModel->vertexes[vertexIndex].position;
            vertices.emplace_back(vertex.x, vertex.y, vertex.z);
        }

        for (size_t vertexIndex = 2; vertexIndex < vertices.size(); ++vertexIndex) {
            triangleMesh->addTriangle(vertices[0], vertices[vertexIndex - 1],
                                      vertices[vertexIndex], true);
            ++triangleCount;
        }
    }

    if (triangleCount == 0) {
        LOG_ERROR(PLID, "World model %d produced no collision triangles.",
                  worldEntity->v.modelindex);
        return false;
    }

    m_worldTriangleMesh = std::move(triangleMesh);
    m_worldMeshShape = std::make_unique<btBvhTriangleMeshShape>(m_worldTriangleMesh.get(),
                                                                  true, true);
    m_worldMeshObject = std::make_unique<btCollisionObject>();
    m_worldMeshObject->setCollisionShape(m_worldMeshShape.get());
    m_worldMeshObject->setWorldTransform(MakeTransform(btVector3(0, 0, 0)));
    m_collisionWorld->addCollisionObject(m_worldMeshObject.get(),
                                         btBroadphaseProxy::StaticFilter,
                                         btBroadphaseProxy::AllFilter);

    const btVector3 minimum(worldModel->mins.x, worldModel->mins.y, worldModel->mins.z);
    const btVector3 maximum(worldModel->maxs.x, worldModel->maxs.y, worldModel->maxs.z);
    const btVector3 halfExtents = (maximum - minimum) * btScalar(0.5);
    if (IsUsableExtent(halfExtents)) {
        // This AABB is retained as the world bounds collider; surface tests use
        // the BVH mesh below so map interiors are never treated as solid boxes.
        m_worldBoundsShape = std::make_unique<btBoxShape>(halfExtents);
        m_worldBoundsObject = std::make_unique<btCollisionObject>();
        m_worldBoundsObject->setCollisionShape(m_worldBoundsShape.get());
        m_worldBoundsObject->setWorldTransform(MakeTransform((minimum + maximum) * btScalar(0.5)));
        m_worldBoundsObject->setUserIndex(kWorldBoundsUserIndex);
        m_collisionWorld->addCollisionObject(m_worldBoundsObject.get(),
                                             btBroadphaseProxy::StaticFilter,
                                             btBroadphaseProxy::AllFilter);
    }

    m_worldReady = true;
    DebugLog(1, "Built world BVH with %d collision triangles.", triangleCount);
    return true;
}

void CCollisionWorld::UpdateCollider(int entityIndex, edict_t* entity)
{
    btVector3 center;
    btVector3 halfExtents;
    if (!GetEntityBounds(entity, center, halfExtents, true)) {
        RemoveCollider(entityIndex);
        return;
    }

    auto existing = m_colliders.find(entityIndex);
    if (existing != m_colliders.end() &&
        (existing->second.entity != entity ||
         !NearlyEqual(existing->second.halfExtents, halfExtents))) {
        RemoveCollider(entityIndex);
        existing = m_colliders.end();
    }

    if (existing == m_colliders.end()) {
        CBoxCollider collider;
        collider.entity = entity;
        collider.halfExtents = halfExtents;
        collider.shape = std::make_unique<btBoxShape>(halfExtents);
        collider.object = std::make_unique<btCollisionObject>();
        collider.object->setCollisionShape(collider.shape.get());
        collider.object->setWorldTransform(MakeTransform(center));
        collider.object->setUserIndex(entityIndex);
        m_collisionWorld->addCollisionObject(collider.object.get(),
                                             btBroadphaseProxy::DefaultFilter,
                                             btBroadphaseProxy::AllFilter);
        m_colliders.emplace(entityIndex, std::move(collider));
        return;
    }

    existing->second.object->setWorldTransform(MakeTransform(center));
    m_collisionWorld->updateSingleAabb(existing->second.object.get());
}

void CCollisionWorld::RemoveCollider(int entityIndex)
{
    const auto found = m_colliders.find(entityIndex);
    if (found == m_colliders.end()) {
        return;
    }

    if (m_collisionWorld != nullptr) {
        m_collisionWorld->removeCollisionObject(found->second.object.get());
    }
    m_colliders.erase(found);
}

bool CCollisionWorld::IsTargetEntity(int entityIndex, const edict_t* entity) const
{
    if (entityIndex <= 0 || entity == nullptr || entity->free ||
        (entity->v.flags & (FL_KILLME | FL_DORMANT | FL_PROXY | FL_SPECTATOR)) != 0 ||
        entity->v.solid == SOLID_NOT || entity->v.movetype == MOVETYPE_NOCLIP) {
        return false;
    }

    return (entity->v.flags & (FL_CLIENT | FL_MONSTER)) != 0 ||
           entity->v.solid == SOLID_BSP;
}

}  // namespace Bvh
