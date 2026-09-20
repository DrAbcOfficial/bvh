#include <cstring>

#include <extdll.h>
#include <dllapi.h>
#include <meta_api.h>

#include "config/projectile_class_config.h"
#include "physics/collision_world.h"
#include "plugin/game_hooks.h"
#include "runtime/debug_log.h"
#include "runtime/projectile_gate.h"

namespace {

Bvh::CProjectileClassConfig g_projectileClassConfig;
Bvh::CCollisionWorld g_collisionWorld;
Bvh::CProjectileGate g_projectileGate(g_collisionWorld, g_projectileClassConfig);

bool IsProjectile(const edict_t* entity)
{
    return entity != nullptr && g_projectileClassConfig.IsManaged(STRING(entity->v.classname));
}

void OnGameInit()
{
    Bvh::RegisterDebugCvar();
    if (!g_projectileClassConfig.Load()) {
        LOG_ERROR(PLID, "Could not load BVH projectile class config %s; projectile management is disabled.",
                  Bvh::CProjectileClassConfig::GetPath());
    } else {
        if (g_projectileClassConfig.WasDefaultCreated()) {
            LOG_CONSOLE(PLID, "[BVH] Created default projectile class config %s.",
                        Bvh::CProjectileClassConfig::GetPath());
        }
        Bvh::DebugLog(1, "Loaded %zu projectile classnames from %s.",
                      g_projectileClassConfig.GetClassnameCount(),
                      Bvh::CProjectileClassConfig::GetPath());
    }
    if (g_engfuncs.pfnAddServerCommand != nullptr) {
        g_engfuncs.pfnAddServerCommand(const_cast<char*>("bvh_debug_status"), []() {
            Bvh::PrintDebugStatus();
        });
        g_engfuncs.pfnAddServerCommand(const_cast<char*>("bvh_status"), []() {
            g_projectileGate.PrintStats();
        });
    }
    SET_META_RESULT(MRES_HANDLED);
}

void OnServerActivate(edict_t* entityList, int, int)
{
    Bvh::DebugLog(1, "Activating world collision.");
    if (entityList == nullptr || !g_collisionWorld.Activate(entityList)) {
        LOG_ERROR(PLID, "BVH collision world could not be built; projectile optimization is disabled.");
    } else {
        Bvh::DebugLog(1, "World collision is ready.");
    }

    SET_META_RESULT(MRES_HANDLED);
}

void OnServerDeactivate()
{
    Bvh::DebugLog(1, "Deactivating world collision.");
    g_projectileGate.Reset();
    g_collisionWorld.Deactivate();
    SET_META_RESULT(MRES_HANDLED);
}

void OnStartFrame()
{
    g_projectileGate.Update();
    SET_META_RESULT(MRES_HANDLED);
}

int OnSpawnPost(edict_t* entity)
{
    if (IsProjectile(entity)) {
        Bvh::DebugLog(2,
                      "Observed projectile spawn %d: solid=%d movetype=%d velocity=(%.1f %.1f %.1f).",
                      ENTINDEX(entity), entity->v.solid, entity->v.movetype,
                      entity->v.velocity.x, entity->v.velocity.y, entity->v.velocity.z);
    }

    SET_META_RESULT(MRES_HANDLED);
    return 0;
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
    OnGameInit,
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

int GetEntityAPI2_Post(DLL_FUNCTIONS* functionTable, int* interfaceVersion)
{
    if (functionTable == nullptr || interfaceVersion == nullptr) {
        return FALSE;
    }
    if (*interfaceVersion != INTERFACE_VERSION) {
        *interfaceVersion = INTERFACE_VERSION;
        return FALSE;
    }

    std::memset(functionTable, 0, sizeof(*functionTable));
    functionTable->pfnSpawn = OnSpawnPost;
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
