#pragma once

#include <cstdint>

namespace Bvh {

struct CFrameCounters {
    int projectiles = 0;
    int managed = 0;
    int culled = 0;
    int collision = 0;
    int fallback = 0;
    // Fallback causes: a due Think inside the frame versus a non-linear
    // movetype. The two always sum to `fallback`.
    int fallbackThink = 0;
    int fallbackMovetype = 0;
    // Trusted-projectile frames where a due Think was swept instead of
    // falling back: via the per-entity trajectory-static marker and via the
    // classname `trust` flag respectively.
    int trustEntity = 0;
    int trustThink = 0;
    int lookahead = 0;
    int sweeps = 0;
    // Query split: ray tests for zero-hull projectiles versus convex box
    // sweeps for the rest.
    int rayQueries = 0;
    int boxQueries = 0;
    int ownerFiltered = 0;
    int colliders = 0;
    double updateMilliseconds = 0.0;
};

// Rolling per-frame aggregates backing the bvh_status command. Cheap enough
// to feed unconditionally every frame; printing is on demand only.
class CPerfStats {
public:
    void AddFrame(const CFrameCounters& counters);
    void Reset();
    void Print() const;

private:
    static constexpr int kWindowFrames = 128;

    std::uint64_t m_frames = 0;
    std::uint64_t m_projectileTotal = 0;
    std::uint64_t m_culledTotal = 0;
    std::uint64_t m_collisionTotal = 0;
    std::uint64_t m_fallbackTotal = 0;
    std::uint64_t m_fallbackThinkTotal = 0;
    std::uint64_t m_fallbackMovetypeTotal = 0;
    std::uint64_t m_trustEntityTotal = 0;
    std::uint64_t m_trustThinkTotal = 0;
    std::uint64_t m_ownerFilteredTotal = 0;
    std::uint64_t m_lookaheadTotal = 0;
    std::uint64_t m_sweepTotal = 0;
    std::uint64_t m_rayQueryTotal = 0;
    std::uint64_t m_boxQueryTotal = 0;
    double m_updateTotalMilliseconds = 0.0;
    double m_updatePeakMilliseconds = 0.0;
    CFrameCounters m_window[kWindowFrames] = {};
    int m_windowIndex = 0;
    int m_windowFill = 0;
};

}  // namespace Bvh
