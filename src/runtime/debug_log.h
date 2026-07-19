#pragma once

namespace Bvh {

void RegisterDebugCvar();
[[nodiscard]] int GetDebugLevel();
void PrintDebugStatus();
void DebugLog(int minimumLevel, const char* format, ...);

}  // namespace Bvh
