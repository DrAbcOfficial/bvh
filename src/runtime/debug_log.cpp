#include "runtime/debug_log.h"

#include <cstdarg>
#include <cstdio>

#include <extdll.h>
#include <enginecallback.h>
#include <meta_api.h>

namespace {

char g_debugName[] = "bvh_debug";
char g_debugDefault[] = "0";
cvar_t g_debugCvar = {g_debugName, g_debugDefault, FCVAR_SERVER, 0.0f, nullptr};
bool g_debugRegistered = false;

}  // namespace

namespace Bvh {

void RegisterDebugCvar()
{
    if (g_debugRegistered) {
        return;
    }

    CVAR_REGISTER(&g_debugCvar);
    g_debugRegistered = true;
    LOG_CONSOLE(PLID, "[BVH] Registered bvh_debug. Use bvh_debug_status to inspect its effective value.");
}

int GetDebugLevel()
{
    if (!g_debugRegistered) {
        return 0;
    }

    // The engine keeps registered cvars current in-place; reading the struct
    // avoids a per-call engine string lookup in hot paths.
    const float level = g_debugCvar.value;
    return level > 0.0f ? static_cast<int>(level) : 0;
}

void PrintDebugStatus()
{
    const float rawValue = g_debugRegistered ? g_debugCvar.value : 0.0f;
    LOG_CONSOLE(PLID, "[BVH] bvh_debug registered=%d raw=%.3f effective=%d.",
                g_debugRegistered ? 1 : 0, rawValue, GetDebugLevel());
}

void DebugLog(int minimumLevel, const char* format, ...)
{
    if (GetDebugLevel() < minimumLevel || gpMetaUtilFuncs == nullptr) {
        return;
    }

    char message[1024];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    LOG_CONSOLE(PLID, "[BVH] %s", message);
}

}  // namespace Bvh
