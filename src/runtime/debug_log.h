#pragma once

namespace Bvh {

constexpr int kMaximumLookaheadFrames = 8;

// Registers every plugin cvar (bvh_debug, bvh_enabled, bvh_lookahead_frames).
void RegisterRuntimeCvars();
[[nodiscard]] int GetDebugLevel();
[[nodiscard]] bool IsPluginEnabled();
[[nodiscard]] int GetLookaheadFrames();
void PrintDebugStatus();
void DebugLog(int minimumLevel, const char* format, ...);

}  // namespace Bvh
