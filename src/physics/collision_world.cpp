#include "physics/collision_world.h"

#include <algorithm>
#include <cmath>
#include <memory>
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
// Hulls below this are zero-size point entities (BDSC bullets set
// mins = maxs = 0) and predict via a ray instead of a convex cast.
constexpr float kPointHullHalfExtent = 0.001f;
constexpr float kExtentTolerance = 0.01f;
constexpr std::size_t kMaximumCachedSweepShapes = 256;

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

// Sweep boxes quantize to 0.5-unit cells, rounded up so the predicted box is
// never smaller than the entity bounds. Cache hits avoid rebuilding the 15
// vertex arrays of btBoxShape for every projectile on every frame.
int QuantizeExtent(btScalar extent)
{
    return std::max(1, static_cast<int>(std::ceil(extent * btScalar(2.0) - btScalar(0.001))));
}

btTransform MakeTransform(const btVector3& origin)
{
    btTransform transform;
    transform.setIdentity();
    transform.setOrigin(origin);
    return transform;
}

// Brush models rotate around vertical in practice (doors, turntables); pitch
// or roll falls back to the fitted box, which stays conservative.
btTransform MakeYawTransform(const btVector3& origin, float yawDegrees)
{
    btTransform transform;
    transform.setIdentity();
    if (yawDegrees != 0.0f) {
        constexpr btScalar kDegreesToRadians = btScalar(3.14159265358979323846) / btScalar(180.0);
        transform.setRotation(btQuaternion(btVector3(0, 0, 1),
                                           yawDegrees * kDegreesToRadians));
    }
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
    CProjectileSweepCallback(const btVector3& from, const btVector3& to,
                             int ownerIndex, int* ownerFilteredCounter)
        : btCollisionWorld::ClosestConvexResultCallback(from, to),
          m_ownerIndex(ownerIndex),
          m_ownerFilteredCounter(ownerFilteredCounter)
    {
    }

    bool needsCollision(btBroadphaseProxy* proxy) const override
    {
        // Boolean query: once any candidate hit, skip all remaining candidates.
        if (hasHit()) {
            return false;
        }

        if (!btCollisionWorld::ClosestConvexResultCallback::needsCollision(proxy)) {
            return false;
        }

        const auto* object = static_cast<const btCollisionObject*>(proxy->m_clientObject);
        if (object == nullptr) {
            return false;
        }

        const int userIndex = object->getUserIndex();
        if (m_ownerIndex > 0 && userIndex == m_ownerIndex) {
            // GoldSrc projectile movement never collides with the shooter, so
            // a muzzle-frame overlap must not count as a predicted hit.
            if (m_ownerFilteredCounter != nullptr) {
                ++*m_ownerFilteredCounter;
            }
            return false;
        }
        return userIndex != kWorldBoundsUserIndex;
    }

private:
    int m_ownerIndex = -1;
    int* m_ownerFilteredCounter = nullptr;
};

// Boolean ray callback for zero-hull projectiles: identical candidate rules
// to the sweep callback, closest-fraction bookkeeping for the lookahead.
class CProjectileRayCallback final : public btCollisionWorld::RayResultCallback {
public:
    CProjectileRayCallback(int ownerIndex, int* ownerFilteredCounter)
        : m_ownerIndex(ownerIndex),
          m_ownerFilteredCounter(ownerFilteredCounter)
    {
    }

    bool needsCollision(btBroadphaseProxy* proxy) const override
    {
        // Boolean query: once any candidate hit, skip all remaining candidates.
        if (hasHit()) {
            return false;
        }

        if (!btCollisionWorld::RayResultCallback::needsCollision(proxy)) {
            return false;
        }

        const auto* object = static_cast<const btCollisionObject*>(proxy->m_clientObject);
        if (object == nullptr) {
            return false;
        }

        const int userIndex = object->getUserIndex();
        if (m_ownerIndex > 0 && userIndex == m_ownerIndex) {
            if (m_ownerFilteredCounter != nullptr) {
                ++*m_ownerFilteredCounter;
            }
            return false;
        }
        return userIndex != kWorldBoundsUserIndex;
    }

    btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult,
                             bool normalInWorldSpace) override
    {
        (void)normalInWorldSpace;
        m_closestHitFraction = rayResult.m_hitFraction;
        m_collisionObject = rayResult.m_collisionObject;
        return rayResult.m_hitFraction;
    }

private:
    int m_ownerIndex = -1;
    int* m_ownerFilteredCounter = nullptr;
};

bool IsIgnoredWorldSurface(const msurface_t& surface)
{
    // Only water boundary faces stay out of the collision mesh: GoldSrc
    // treats them as passable. Sky and underwater solid faces were once
    // skipped too, which let projectiles fly through sky and submerged
    // walls while culled; they must stay in the mesh.
    constexpr int kSurfaceDrawTurb = 0x10;
    return (surface.flags & kSurfaceDrawTurb) != 0;
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

bool HasCompleteBrushData(const model_t& model)
{
    return model.type == mod_brush &&
           model.surfaces != nullptr && model.vertexes != nullptr &&
           model.edges != nullptr && model.surfedges != nullptr;
}

// Shared by the world and by per-brush-model colliders.
std::unique_ptr<btTriangleMesh> BuildModelTriangleMesh(const model_t& model, int* triangleCountOut)
{
    auto triangleMesh = std::make_unique<btTriangleMesh>(true, false);
    int triangleCount = 0;

    std::vector<const msurface_t*> modelSurfaces;
    if (model.nodes != nullptr && model.numnodes > 0) {
        CollectVisibleWorldSurfaces(model, model.nodes, modelSurfaces);
    } else {
        modelSurfaces.reserve(static_cast<size_t>(model.numsurfaces));
        for (int surfaceIndex = 0; surfaceIndex < model.numsurfaces; ++surfaceIndex) {
            modelSurfaces.push_back(&model.surfaces[surfaceIndex]);
        }
    }

    for (const msurface_t* surfacePtr : modelSurfaces) {
        const msurface_t& surface = *surfacePtr;
        if (IsIgnoredWorldSurface(surface) || surface.numedges < 3) {
            continue;
        }
        if (surface.firstedge < 0 ||
            surface.firstedge + surface.numedges > model.numsurfedges) {
            continue;
        }

        std::vector<btVector3> vertices;
        vertices.reserve(static_cast<size_t>(surface.numedges));

        for (int edgeOffset = 0; edgeOffset < surface.numedges; ++edgeOffset) {
            const int signedEdge = model.surfedges[surface.firstedge + edgeOffset];
            const int edgeIndex = signedEdge >= 0 ? signedEdge : -signedEdge;
            if (edgeIndex < 0 || edgeIndex >= model.numedges) {
                vertices.clear();
                break;
            }

            const medge_t& edge = model.edges[edgeIndex];
            const int vertexIndex = signedEdge >= 0 ? edge.v[0] : edge.v[1];
            if (vertexIndex < 0 || vertexIndex >= model.numvertexes) {
                vertices.clear();
                break;
            }

            const Vector& vertex = model.vertexes[vertexIndex].position;
            vertices.emplace_back(vertex.x, vertex.y, vertex.z);
        }

        for (size_t vertexIndex = 2; vertexIndex < vertices.size(); ++vertexIndex) {
            // Duplicate vertices are harmless for collision queries; skipping
            // the dedup hash keeps large-map builds fast and small.
            triangleMesh->addTriangle(vertices[0], vertices[vertexIndex - 1],
                                      vertices[vertexIndex], false);
            ++triangleCount;
        }
    }

    if (triangleCountOut != nullptr) {
        *triangleCountOut = triangleCount;
    }
    return triangleCount > 0 ? std::move(triangleMesh) : nullptr;
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
    return true;
}

void CCollisionWorld::Deactivate()
{
    m_active = false;
    m_worldReady = false;
    ClearColliders();
    ClearWorldGeometry();
}

void CCollisionWorld::BeginFrame()
{
    if (!m_active || !m_worldReady) {
        return;
    }

    ++m_syncGeneration;
    m_sweepCount = 0;
    m_ownerFilteredCount = 0;
    m_rayQueryCount = 0;
    m_boxQueryCount = 0;

    for (auto iterator = m_colliders.begin(); iterator != m_colliders.end();) {
        if (iterator->second.lastSyncGeneration != m_syncGeneration) {
            const int entityIndex = iterator->first;
            ++iterator;
            RemoveCollider(entityIndex);
        } else {
            ++iterator;
        }
    }
}

void CCollisionWorld::SynchronizeEntity(int entityIndex, edict_t* entity)
{
    if (!m_active || !m_worldReady || !IsTargetEntity(entityIndex, entity)) {
        return;
    }

    UpdateCollider(entityIndex, entity);
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

int CCollisionWorld::GetColliderCount() const
{
    return static_cast<int>(m_colliders.size());
}

int CCollisionWorld::GetSweepCount() const
{
    return m_sweepCount;
}

int CCollisionWorld::GetWorldTriangleCount() const
{
    return m_worldTriangleCount;
}

int CCollisionWorld::GetOwnerFilteredCount() const
{
    return m_ownerFilteredCount;
}

int CCollisionWorld::GetRayQueryCount() const
{
    return m_rayQueryCount;
}

int CCollisionWorld::GetBoxQueryCount() const
{
    return m_boxQueryCount;
}

float CCollisionWorld::SweepProjectile(const edict_t* projectile, float frameTime) const
{
    if (!IsReady() || projectile == nullptr || !std::isfinite(frameTime) || frameTime <= 0.0f) {
        // A missing or invalid query must leave GoldSrc collision authoritative.
        return 0.0f;
    }

    btVector3 center;
    btVector3 halfExtents;
    const bool hasExtent = GetEntityBounds(projectile, center, halfExtents, false);
    if (!hasExtent) {
        // GoldSrc moves MOVETYPE_FLY projectiles as (velocity + basevelocity)
        // * frametime (SV_Physics_Bounce); predict the same displacement.
        const btVector3 velocity(projectile->v.velocity.x + projectile->v.basevelocity.x,
                                 projectile->v.velocity.y + projectile->v.basevelocity.y,
                                 projectile->v.velocity.z + projectile->v.basevelocity.z);
        if (velocity.length2() <= SIMD_EPSILON) {
            return 1.0f;
        }

        const int ownerIndex = projectile->v.owner != nullptr ? ENTINDEX(projectile->v.owner) : 0;
        int ownerFiltered = 0;
        ++m_sweepCount;

        if (std::fabs(halfExtents.x()) < kPointHullHalfExtent &&
            std::fabs(halfExtents.y()) < kPointHullHalfExtent &&
            std::fabs(halfExtents.z()) < kPointHullHalfExtent) {
            // Zero-hull projectile: GoldSrc traces a line segment (point vs
            // volume), so a ray matches the engine semantics exactly and is
            // far cheaper than a polyhedral convex cast.
            const btVector3 origin(projectile->v.origin.x,
                                   projectile->v.origin.y,
                                   projectile->v.origin.z);
            CProjectileRayCallback callback(ownerIndex,
                                            ownerIndex > 0 ? &ownerFiltered : nullptr);
            callback.m_collisionFilterGroup = btBroadphaseProxy::DefaultFilter;
            callback.m_collisionFilterMask = btBroadphaseProxy::DefaultFilter |
                                             btBroadphaseProxy::StaticFilter;
            ++m_rayQueryCount;
            m_collisionWorld->rayTest(origin, origin + velocity * frameTime, callback);
            m_ownerFilteredCount += ownerFiltered;
            return callback.m_closestHitFraction;
        }

        // Degenerate but non-zero hull: keep the old conservative minimum box.
        halfExtents = btVector3(kMinimumHalfExtent, kMinimumHalfExtent, kMinimumHalfExtent);
        center = btVector3(projectile->v.origin.x,
                           projectile->v.origin.y,
                           projectile->v.origin.z);
        const btTransform from = MakeTransform(center);
        const btTransform to = MakeTransform(center + velocity * frameTime);
        btBoxShape* projectileShape = ResolveProjectileShape(halfExtents);
        CProjectileSweepCallback callback(from.getOrigin(), to.getOrigin(),
                                          ownerIndex, ownerIndex > 0 ? &ownerFiltered : nullptr);
        callback.m_collisionFilterGroup = btBroadphaseProxy::DefaultFilter;
        callback.m_collisionFilterMask = btBroadphaseProxy::DefaultFilter |
                                         btBroadphaseProxy::StaticFilter;
        ++m_boxQueryCount;
        m_collisionWorld->convexSweepTest(projectileShape, from, to, callback);
        m_ownerFilteredCount += ownerFiltered;
        return callback.m_closestHitFraction;
    }

    const btVector3 velocity(projectile->v.velocity.x + projectile->v.basevelocity.x,
                             projectile->v.velocity.y + projectile->v.basevelocity.y,
                             projectile->v.velocity.z + projectile->v.basevelocity.z);
    if (velocity.length2() <= SIMD_EPSILON) {
        return 1.0f;
    }

    const btTransform from = MakeTransform(center);
    const btTransform to = MakeTransform(center + velocity * frameTime);
    btBoxShape* projectileShape = ResolveProjectileShape(halfExtents);

    const int ownerIndex = projectile->v.owner != nullptr ? ENTINDEX(projectile->v.owner) : 0;
    int ownerFiltered = 0;
    CProjectileSweepCallback callback(from.getOrigin(), to.getOrigin(),
                                      ownerIndex, ownerIndex > 0 ? &ownerFiltered : nullptr);
    callback.m_collisionFilterGroup = btBroadphaseProxy::DefaultFilter;
    callback.m_collisionFilterMask = btBroadphaseProxy::DefaultFilter |
                                     btBroadphaseProxy::StaticFilter;

    ++m_sweepCount;
    ++m_boxQueryCount;
    m_collisionWorld->convexSweepTest(projectileShape, from, to, callback);
    m_ownerFilteredCount += ownerFiltered;
    return callback.m_closestHitFraction;
}

btBoxShape* CCollisionWorld::ResolveProjectileShape(const btVector3& halfExtents) const
{
    const auto key = std::make_tuple(QuantizeExtent(halfExtents.x()),
                                     QuantizeExtent(halfExtents.y()),
                                     QuantizeExtent(halfExtents.z()));
    const auto found = m_shapeCache.find(key);
    if (found != m_shapeCache.end()) {
        return found->second.get();
    }

    if (m_shapeCache.size() >= kMaximumCachedSweepShapes) {
        // Distinct projectile sizes are few; a pathological config restarts
        // the cache instead of growing without bound.
        m_shapeCache.clear();
    }

    btVector3 quantizedHalfExtents(static_cast<btScalar>(std::get<0>(key)) * btScalar(0.5),
                                   static_cast<btScalar>(std::get<1>(key)) * btScalar(0.5),
                                   static_cast<btScalar>(std::get<2>(key)) * btScalar(0.5));
    auto shape = std::make_unique<btBoxShape>(quantizedHalfExtents);
    btBoxShape* shapePtr = shape.get();
    m_shapeCache.emplace(key, std::move(shape));
    return shapePtr;
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
    m_worldTriangleCount = 0;
    m_shapeCache.clear();
    m_brushModelShapes.clear();
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
    if (!HasCompleteBrushData(*worldModel)) {
        LOG_ERROR(PLID, "World model %d is not a complete BSP collision model.",
                  worldEntity->v.modelindex);
        return false;
    }

    int triangleCount = 0;
    auto triangleMesh = BuildModelTriangleMesh(*worldModel, &triangleCount);
    if (triangleMesh == nullptr) {
        LOG_ERROR(PLID, "World model %d produced no collision triangles.",
                  worldEntity->v.modelindex);
        return false;
    }

    m_worldTriangleMesh = std::move(triangleMesh);
    m_worldTriangleCount = triangleCount;
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

btBvhTriangleMeshShape* CCollisionWorld::ResolveBrushModelShape(edict_t* entity)
{
    const int modelIndex = entity->v.modelindex;
    if (modelIndex <= 0 || modelIndex >= 8192) {
        return nullptr;
    }

    auto found = m_brushModelShapes.find(modelIndex);
    if (found != m_brushModelShapes.end()) {
        return found->second.shape.get();
    }

    // Negative results are cached too so a non-brush or broken model never
    // repeats the model resolution on every frame.
    CBrushModelShape entry;
    model_t* model = m_modelProvider.GetModel(entity);
    if (model != nullptr && HasCompleteBrushData(*model)) {
        int triangleCount = 0;
        entry.mesh = BuildModelTriangleMesh(*model, &triangleCount);
        if (entry.mesh != nullptr) {
            entry.shape = std::make_unique<btBvhTriangleMeshShape>(entry.mesh.get(), true, true);
            DebugLog(1, "Built brush model %d BVH with %d collision triangles.",
                     modelIndex, triangleCount);
        }
    }
    auto [iterator, inserted] = m_brushModelShapes.emplace(modelIndex, std::move(entry));
    (void)inserted;
    return iterator->second.shape.get();
}

void CCollisionWorld::UpdateCollider(int entityIndex, edict_t* entity)
{
    // SOLID_BSP entities move as rigid brush models: predict with the real
    // model mesh when it is available, and only when they stay yaw-rotated.
    btBvhTriangleMeshShape* meshShape = nullptr;
    if (entity->v.solid == SOLID_BSP && entity->v.angles.x == 0.0f &&
        entity->v.angles.z == 0.0f) {
        meshShape = ResolveBrushModelShape(entity);
    }

    const btVector3 origin(entity->v.origin.x, entity->v.origin.y, entity->v.origin.z);
    btVector3 center;
    btVector3 halfExtents;
    if (meshShape == nullptr && !GetEntityBounds(entity, center, halfExtents, true)) {
        RemoveCollider(entityIndex);
        return;
    }

    auto existing = m_colliders.find(entityIndex);
    if (existing != m_colliders.end()) {
        CBoxCollider& collider = existing->second;
        const bool sameKind = (collider.meshShape != nullptr) == (meshShape != nullptr);
        const bool sameMesh = meshShape == nullptr || collider.meshShape == meshShape;
        const bool sameSize = meshShape != nullptr ||
                              NearlyEqual(collider.halfExtents, halfExtents);
        if (collider.entity != entity || !sameKind || !sameMesh || !sameSize) {
            RemoveCollider(entityIndex);
            existing = m_colliders.end();
        }
    }

    if (existing == m_colliders.end()) {
        CBoxCollider collider;
        collider.entity = entity;
        collider.lastCenter = meshShape != nullptr ? origin : center;
        collider.lastYaw = entity->v.angles.y;
        collider.hasLastCenter = true;
        collider.lastSyncGeneration = m_syncGeneration;
        collider.meshShape = meshShape;
        collider.object = std::make_unique<btCollisionObject>();
        if (meshShape != nullptr) {
            collider.object->setCollisionShape(meshShape);
            collider.object->setWorldTransform(MakeYawTransform(origin, entity->v.angles.y));
        } else {
            collider.halfExtents = halfExtents;
            collider.shape = std::make_unique<btBoxShape>(halfExtents);
            collider.object->setCollisionShape(collider.shape.get());
            collider.object->setWorldTransform(MakeTransform(center));
        }
        collider.object->setUserIndex(entityIndex);
        m_collisionWorld->addCollisionObject(collider.object.get(),
                                             btBroadphaseProxy::DefaultFilter,
                                             btBroadphaseProxy::AllFilter);
        m_colliders.emplace(entityIndex, std::move(collider));
        return;
    }

    CBoxCollider& collider = existing->second;
    collider.lastSyncGeneration = m_syncGeneration;

    // Static brush entities and resting clients rarely move; skip the
    // transform write and the broadphase leaf update until they do.
    const bool unchanged = collider.hasLastCenter &&
                           NearlyEqual(collider.lastCenter,
                                       meshShape != nullptr ? origin : center) &&
                           (meshShape == nullptr || collider.lastYaw == entity->v.angles.y);
    if (unchanged) {
        return;
    }

    if (meshShape != nullptr) {
        collider.object->setWorldTransform(MakeYawTransform(origin, entity->v.angles.y));
    } else {
        collider.object->setWorldTransform(MakeTransform(center));
    }
    m_collisionWorld->updateSingleAabb(collider.object.get());
    collider.lastCenter = meshShape != nullptr ? origin : center;
    collider.lastYaw = entity->v.angles.y;
    collider.hasLastCenter = true;
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
