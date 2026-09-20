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

char g_enabledName[] = "bvh_enabled";
char g_enabledDefault[] = "1";
cvar_t g_enabledCvar = {g_enabledName, g_enabledDefault, FCVAR_SERVER, 1.0f, nullptr};
bool g_enabledRegistered = false;

char g_lookaheadName[] = "bvh_lookahead_frames";
char g_lookaheadDefault[] = "1";
cvar_t g_lookaheadCvar = {g_lookaheadName, g_lookaheadDefault, FCVAR_SERVER, 1.0f, nullptr};
bool g_lookaheadRegistered = false;

}  // namespace

namespace Bvh {

void RegisterRuntimeCvars()
{
    if (!g_debugRegistered) {
        CVAR_REGISTER(&g_debugCvar);
        g_debugRegistered = true;
    }

    if (!g_enabledRegistered) {
        CVAR_REGISTER(&g_enabledCvar);
        g_enabledRegistered = true;
    }

    if (!g_lookaheadRegistered) {
        CVAR_REGISTER(&g_lookaheadCvar);
        g_lookaheadRegistered = true;
    }

    LOG_CONSOLE(PLID, "[BVH] Registered cvars: bvh_debug (0/1/2), bvh_enabled, bvh_lookahead_frames. Commands: bvh_status, bvh_reload, bvh_debug_status.");
}

bool IsPluginEnabled()
{
    return !g_enabledRegistered || g_enabledCvar.value > 0.5f;
}

int GetLookaheadFrames()
{
    if (!g_lookaheadRegistered) {
        return 1;
    }

    int frames = static_cast<int>(g_lookaheadCvar.value);
    if (frames < 1) {
        frames = 1;
    }
    if (frames > kMaximumLookaheadFrames) {
        frames = kMaximumLookaheadFrames;
    }
    return frames;
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
    const float rawDebugValue = g_debugRegistered ? g_debugCvar.value : 0.0f;
    const float rawEnabledValue = g_enabledRegistered ? g_enabledCvar.value : 0.0f;
    LOG_CONSOLE(PLID, "[BVH] bvh_debug registered=%d raw=%.3f effective=%d; bvh_enabled raw=%.3f effective=%d.",
                g_debugRegistered ? 1 : 0, rawDebugValue, GetDebugLevel(),
                rawEnabledValue, IsPluginEnabled() ? 1 : 0);
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
