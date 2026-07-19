#include <cstring>

#include <extdll.h>
#include <meta_api.h>

#include "plugin/game_hooks.h"

static META_FUNCTIONS g_metaFunctionTable = {
    nullptr,
    nullptr,
    GetEntityAPI2,
    nullptr,
    GetNewDLLFunctions,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

plugin_info_t Plugin_info = {
    META_INTERFACE_VERSION,
    "BVH Projectile Optimizer",
    "1.0.0",
    "20260719",
    "DrAbc",
    "https://24helikopter.ws",
    "BVH",
    PT_ANYTIME,
    PT_STARTUP,
};

meta_globals_t* gpMetaGlobals = nullptr;
gamedll_funcs_t* gpGamedllFuncs = nullptr;
mutil_funcs_t* gpMetaUtilFuncs = nullptr;

C_DLLEXPORT int Meta_Query(const char* interfaceVersion,
                           plugin_info_t** pluginInfo,
                           mutil_funcs_t* metaUtilFunctions)
{
    if (interfaceVersion == nullptr || pluginInfo == nullptr ||
        metaUtilFunctions == nullptr ||
        std::strcmp(interfaceVersion, META_INTERFACE_VERSION) != 0) {
        return FALSE;
    }

    *pluginInfo = &Plugin_info;
    gpMetaUtilFuncs = metaUtilFunctions;
    return TRUE;
}

C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME,
                            META_FUNCTIONS* functionTable,
                            meta_globals_t* metaGlobals,
                            gamedll_funcs_t* gameDllFunctions)
{
    if (functionTable == nullptr || metaGlobals == nullptr ||
        gpMetaUtilFuncs == nullptr) {
        return FALSE;
    }

    std::memcpy(functionTable, &g_metaFunctionTable, sizeof(g_metaFunctionTable));
    gpMetaGlobals = metaGlobals;
    gpGamedllFuncs = gameDllFunctions;
    return TRUE;
}

C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME, PL_UNLOAD_REASON)
{
    ShutdownBvhPlugin();
    return TRUE;
}
