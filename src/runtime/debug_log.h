#pragma once

namespace Bvh {

// Registers every plugin cvar (bvh_debug, bvh_enabled).
void RegisterRuntimeCvars();
[[nodiscard]] int GetDebugLevel();
[[nodiscard]] bool IsPluginEnabled();
void PrintDebugStatus();
void DebugLog(int minimumLevel, const char* format, ...);

}  // namespace Bvh
