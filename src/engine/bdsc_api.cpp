#include "engine/bdsc_api.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Bvh::BdscApi {

namespace {

constexpr char kExportName[] = "bdsc_bvh_get_trajectory_marker";
GetTrajectoryMarkerFn g_resolved = nullptr;

#if defined(_WIN32)
GetTrajectoryMarkerFn LoadTrajectoryMarker()
{
    if (HMODULE module = GetModuleHandleA("bdsc.dll")) {
        return reinterpret_cast<GetTrajectoryMarkerFn>(
            GetProcAddress(module, kExportName));
    }
    return nullptr;
}
#else
GetTrajectoryMarkerFn LoadTrajectoryMarker()
{
    // metamod loads plugin shared objects with RTLD_GLOBAL, so bdsc's
    // export is reachable from the global symbol namespace.
    return reinterpret_cast<GetTrajectoryMarkerFn>(
        dlsym(RTLD_DEFAULT, kExportName));
}
#endif

}  // namespace

GetTrajectoryMarkerFn ResolveTrajectoryMarker()
{
    if (g_resolved == nullptr) {
        g_resolved = LoadTrajectoryMarker();
    }
    return g_resolved;
}

int GetTrajectoryMarker(int entityIndex)
{
    const GetTrajectoryMarkerFn resolve = ResolveTrajectoryMarker();
    if (resolve == nullptr) {
        return kUnmarked;
    }
    return resolve(entityIndex);
}

void InvalidateCache()
{
    g_resolved = nullptr;
}

}  // namespace Bvh::BdscApi
