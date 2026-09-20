#pragma once

namespace Bvh::BdscApi {

// Trajectory marker carried on the bdsc-side CGameObject for BDSC
// projectiles (protocol: docs/OPTIMIZATION_ROUND3.md 4.3). The iuser
// entvars slots have engine-assigned meanings in Sven Co-op, so the
// marker travels out-of-band through the bdsc plugin instead.
enum TrajectoryMarker {
    kUnmarked = 0,  // no marker written; fall back to classname config
    kStatic = 1,    // Think never rewrites velocity/trajectory; trust sweeps
    kDynamic = 2,   // Think may rewrite the trajectory; always fall back
};

// Signature of bdsc's exported query: bdsc_bvh_get_trajectory_marker(int).
using GetTrajectoryMarkerFn = int (*)(int entindex);

// Resolve bdsc's exported query from bdsc.dll / bdsc.so (loaded by metamod;
// BDSC is expected to load before bvh per plugins.ini). Returns nullptr
// until bdsc is present; retries are cheap and cached once resolved.
GetTrajectoryMarkerFn ResolveTrajectoryMarker();

// Marker for an entity index: one of TrajectoryMarker, or kUnmarked when
// bdsc is absent or the entity has no marker.
int GetTrajectoryMarker(int entityIndex);

// Drop the cached function pointer so the next query re-resolves; call on
// ServerActivate so an unloaded/reloaded bdsc cannot leave a stale pointer.
void InvalidateCache();

}  // namespace Bvh::BdscApi
