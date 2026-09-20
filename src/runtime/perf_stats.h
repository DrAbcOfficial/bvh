#pragma once

#include <cstdint>

namespace Bvh {

struct CFrameCounters {
    int projectiles = 0;
    int managed = 0;
    int culled = 0;
    int collision = 0;
    int fallback = 0;
    int lookahead = 0;
    int sweeps = 0;
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
    std::uint64_t m_lookaheadTotal = 0;
    std::uint64_t m_sweepTotal = 0;
    double m_updateTotalMilliseconds = 0.0;
    double m_updatePeakMilliseconds = 0.0;
    CFrameCounters m_window[kWindowFrames] = {};
    int m_windowIndex = 0;
    int m_windowFill = 0;
};

}  // namespace Bvh
