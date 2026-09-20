#include "runtime/perf_stats.h"

#include <algorithm>

#include <extdll.h>
#include <meta_api.h>

namespace Bvh {

void CPerfStats::AddFrame(const CFrameCounters& counters)
{
    ++m_frames;
    m_projectileTotal += static_cast<std::uint64_t>(std::max(0, counters.projectiles));
    m_culledTotal += static_cast<std::uint64_t>(std::max(0, counters.culled));
    m_collisionTotal += static_cast<std::uint64_t>(std::max(0, counters.collision));
    m_fallbackTotal += static_cast<std::uint64_t>(std::max(0, counters.fallback));
    m_sweepTotal += static_cast<std::uint64_t>(std::max(0, counters.sweeps));
    m_updateTotalMilliseconds += std::max(0.0, counters.updateMilliseconds);
    m_updatePeakMilliseconds = std::max(m_updatePeakMilliseconds, counters.updateMilliseconds);

    m_window[m_windowIndex] = counters;
    m_windowIndex = (m_windowIndex + 1) % kWindowFrames;
    m_windowFill = std::min(m_windowFill + 1, kWindowFrames);
}

void CPerfStats::Reset()
{
    m_frames = 0;
    m_projectileTotal = 0;
    m_culledTotal = 0;
    m_collisionTotal = 0;
    m_fallbackTotal = 0;
    m_sweepTotal = 0;
    m_updateTotalMilliseconds = 0.0;
    m_updatePeakMilliseconds = 0.0;
    m_windowIndex = 0;
    m_windowFill = 0;
}

void CPerfStats::Print() const
{
    const int windowFrames = std::max(1, m_windowFill);
    double projectiles = 0.0;
    double culled = 0.0;
    double collision = 0.0;
    double fallback = 0.0;
    double sweeps = 0.0;
    double update = 0.0;
    for (int index = 0; index < m_windowFill; ++index) {
        const CFrameCounters& frame = m_window[index];
        projectiles += frame.projectiles;
        culled += frame.culled;
        collision += frame.collision;
        fallback += frame.fallback;
        sweeps += frame.sweeps;
        update += frame.updateMilliseconds;
    }
    projectiles /= windowFrames;
    culled /= windowFrames;
    collision /= windowFrames;
    fallback /= windowFrames;
    sweeps /= windowFrames;
    update /= windowFrames;

    LOG_CONSOLE(PLID, "[BVH] status: frames=%llu window=%d",
                static_cast<unsigned long long>(m_frames), m_windowFill);
    LOG_CONSOLE(PLID, "[BVH] frame avg: projectiles=%.2f culled=%.2f collision=%.2f fallback=%.2f sweeps=%.2f update=%.3fms",
                projectiles, culled, collision, fallback, sweeps, update);
    LOG_CONSOLE(PLID, "[BVH] update: avg=%.3fms peak=%.3fms",
                m_frames > 0 ? m_updateTotalMilliseconds / static_cast<double>(m_frames) : 0.0,
                m_updatePeakMilliseconds);
    LOG_CONSOLE(PLID, "[BVH] totals: projectiles=%llu culled=%llu collision=%llu fallback=%llu sweeps=%llu",
                static_cast<unsigned long long>(m_projectileTotal),
                static_cast<unsigned long long>(m_culledTotal),
                static_cast<unsigned long long>(m_collisionTotal),
                static_cast<unsigned long long>(m_fallbackTotal),
                static_cast<unsigned long long>(m_sweepTotal));
}

}  // namespace Bvh
