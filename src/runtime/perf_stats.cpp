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
    m_fallbackThinkTotal += static_cast<std::uint64_t>(std::max(0, counters.fallbackThink));
    m_fallbackMovetypeTotal += static_cast<std::uint64_t>(std::max(0, counters.fallbackMovetype));
    m_trustEntityTotal += static_cast<std::uint64_t>(std::max(0, counters.trustEntity));
    m_trustThinkTotal += static_cast<std::uint64_t>(std::max(0, counters.trustThink));
    m_ownerFilteredTotal += static_cast<std::uint64_t>(std::max(0, counters.ownerFiltered));
    m_lookaheadTotal += static_cast<std::uint64_t>(std::max(0, counters.lookahead));
    m_sweepTotal += static_cast<std::uint64_t>(std::max(0, counters.sweeps));
    m_rayQueryTotal += static_cast<std::uint64_t>(std::max(0, counters.rayQueries));
    m_boxQueryTotal += static_cast<std::uint64_t>(std::max(0, counters.boxQueries));
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
    m_fallbackThinkTotal = 0;
    m_fallbackMovetypeTotal = 0;
    m_trustEntityTotal = 0;
    m_trustThinkTotal = 0;
    m_ownerFilteredTotal = 0;
    m_lookaheadTotal = 0;
    m_sweepTotal = 0;
    m_rayQueryTotal = 0;
    m_boxQueryTotal = 0;
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
    double fallbackThink = 0.0;
    double fallbackMovetype = 0.0;
    double trustEntity = 0.0;
    double trustThink = 0.0;
    double ownerFiltered = 0.0;
    double lookahead = 0.0;
    double sweeps = 0.0;
    double rayQueries = 0.0;
    double boxQueries = 0.0;
    double update = 0.0;
    for (int index = 0; index < m_windowFill; ++index) {
        const CFrameCounters& frame = m_window[index];
        projectiles += frame.projectiles;
        culled += frame.culled;
        collision += frame.collision;
        fallback += frame.fallback;
        fallbackThink += frame.fallbackThink;
        fallbackMovetype += frame.fallbackMovetype;
        trustEntity += frame.trustEntity;
        trustThink += frame.trustThink;
        ownerFiltered += frame.ownerFiltered;
        lookahead += frame.lookahead;
        sweeps += frame.sweeps;
        rayQueries += frame.rayQueries;
        boxQueries += frame.boxQueries;
        update += frame.updateMilliseconds;
    }
    projectiles /= windowFrames;
    culled /= windowFrames;
    collision /= windowFrames;
    fallback /= windowFrames;
    fallbackThink /= windowFrames;
    fallbackMovetype /= windowFrames;
    trustEntity /= windowFrames;
    trustThink /= windowFrames;
    ownerFiltered /= windowFrames;
    lookahead /= windowFrames;
    sweeps /= windowFrames;
    rayQueries /= windowFrames;
    boxQueries /= windowFrames;
    update /= windowFrames;

    LOG_CONSOLE(PLID, "[BVH] status: frames=%llu window=%d",
                static_cast<unsigned long long>(m_frames), m_windowFill);
    LOG_CONSOLE(PLID, "[BVH] frame avg: projectiles=%.2f culled=%.2f collision=%.2f fallback=%.2f (think=%.2f move=%.2f trust=%.2f/%.2f ownerFilter=%.2f) lookahead=%.2f sweeps=%.2f (ray=%.2f box=%.2f) update=%.3fms",
                projectiles, culled, collision, fallback, fallbackThink,
                fallbackMovetype, trustEntity, trustThink, ownerFiltered,
                lookahead, sweeps, rayQueries, boxQueries, update);
    LOG_CONSOLE(PLID, "[BVH] update: avg=%.3fms peak=%.3fms",
                m_frames > 0 ? m_updateTotalMilliseconds / static_cast<double>(m_frames) : 0.0,
                m_updatePeakMilliseconds);
    LOG_CONSOLE(PLID, "[BVH] totals: projectiles=%llu culled=%llu collision=%llu fallback=%llu (think=%llu move=%llu trust=%llu/%llu ownerFilter=%llu) lookahead=%llu sweeps=%llu (ray=%llu box=%llu)",
                static_cast<unsigned long long>(m_projectileTotal),
                static_cast<unsigned long long>(m_culledTotal),
                static_cast<unsigned long long>(m_collisionTotal),
                static_cast<unsigned long long>(m_fallbackTotal),
                static_cast<unsigned long long>(m_fallbackThinkTotal),
                static_cast<unsigned long long>(m_fallbackMovetypeTotal),
                static_cast<unsigned long long>(m_trustEntityTotal),
                static_cast<unsigned long long>(m_trustThinkTotal),
                static_cast<unsigned long long>(m_ownerFilteredTotal),
                static_cast<unsigned long long>(m_lookaheadTotal),
                static_cast<unsigned long long>(m_sweepTotal),
                static_cast<unsigned long long>(m_rayQueryTotal),
                static_cast<unsigned long long>(m_boxQueryTotal));
}

}  // namespace Bvh
