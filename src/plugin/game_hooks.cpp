#include <cstring>

#include <extdll.h>
#include <dllapi.h>
#include <meta_api.h>

#include "physics/collision_world.h"
#include "plugin/game_hooks.h"
#include "runtime/projectile_gate.h"

namespace {

Bvh::CCollisionWorld g_collisionWorld;
Bvh::CProjectileGate g_projectileGate(g_collisionWorld);

void OnServerActivate(edict_t* entityList, int, int)
{
    if (entityList == nullptr || !g_collisionWorld.Activate(entityList)) {
        LOG_ERROR(PLID, "BVH collision world could not be built; projectile optimization is disabled.");
    }

    SET_META_RESULT(MRES_HANDLED);
}

void OnServerDeactivate()
{
    g_projectileGate.Reset();
    g_collisionWorld.Deactivate();
    SET_META_RESULT(MRES_HANDLED);
}

void OnStartFrame()
{
    g_projectileGate.Update();
    SET_META_RESULT(MRES_HANDLED);
}

void OnFreeEntPrivateData(edict_t* entity)
{
    g_projectileGate.Forget(entity);
    g_collisionWorld.RemoveEntity(entity);
    SET_META_RESULT(MRES_HANDLED);
}

void OnGameShutdown()
{
    g_projectileGate.Reset();
    g_collisionWorld.Deactivate();
    SET_META_RESULT(MRES_HANDLED);
}

DLL_FUNCTIONS g_functionTable = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    OnServerActivate,
    OnServerDeactivate,
    nullptr,
    nullptr,
    OnStartFrame,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

NEW_DLL_FUNCTIONS g_newFunctionTable = {
    OnFreeEntPrivateData,
    OnGameShutdown,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

int GetEntityAPI2(DLL_FUNCTIONS* functionTable, int* interfaceVersion)
{
    if (functionTable == nullptr || interfaceVersion == nullptr) {
        return FALSE;
    }
    if (*interfaceVersion != INTERFACE_VERSION) {
        *interfaceVersion = INTERFACE_VERSION;
        return FALSE;
    }

    std::memcpy(functionTable, &g_functionTable, sizeof(g_functionTable));
    return TRUE;
}

int GetNewDLLFunctions(NEW_DLL_FUNCTIONS* functionTable, int* interfaceVersion)
{
    if (functionTable == nullptr || interfaceVersion == nullptr) {
        return FALSE;
    }
    if (*interfaceVersion != NEW_DLL_FUNCTIONS_VERSION) {
        *interfaceVersion = NEW_DLL_FUNCTIONS_VERSION;
        return FALSE;
    }

    std::memcpy(functionTable, &g_newFunctionTable, sizeof(g_newFunctionTable));
    return TRUE;
}

void ShutdownBvhPlugin()
{
    g_projectileGate.Reset();
    g_collisionWorld.Deactivate();
}
