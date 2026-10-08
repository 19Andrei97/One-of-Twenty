#pragma once

#include "Goods.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// A coarse record of a run over time, and a summary of it. The point is to turn
// "the settlement feels wrong" into numbers: sample the population and stores at
// a fixed in-game interval, then read off the peak, the trough and the totals so
// a balance change can be compared against a baseline. Kept a plain value type
// (no clock, map, registry or renderer) so a test can build one by hand and a
// headless run can print it.
namespace Observability
{

// One sample of the world at an instant.
struct RunPoint
{
    std::int64_t timestamp_min{ 0 };
    int          population{ 0 };
    int          buildings{ 0 };
    Goods::Stock goods{};
};

// An aggregate over a run's samples: the shape a balance change moves.
struct RunSummary
{
    std::int64_t start_min{ 0 };
    std::int64_t end_min{ 0 };
    int          min_population{ 0 };
    int          peak_population{ 0 };
    int          end_population{ 0 };
    int          births{ 0 };
    int          deaths{ 0 };
    int          gathers{ 0 };

    // Whole in-game days the samples span (a partial day does not count).
    [[nodiscard]] int days() const noexcept
    {
        return static_cast<int>((end_min - start_min) / (24 * 60));
    }

    // A one-line, greppable digest for a headless log.
    [[nodiscard]] std::string format() const
    {
        return "run: days=" + std::to_string(days())
             + " pop=" + std::to_string(min_population) + ".." + std::to_string(peak_population)
             + " (end " + std::to_string(end_population) + ")"
             + " births=" + std::to_string(births)
             + " deaths=" + std::to_string(deaths)
             + " gathers=" + std::to_string(gathers);
    }
};

// Samples a run at a fixed in-game interval, so the history has a bounded size
// regardless of the clock speed or how long the run lasts. The first sample is
// always kept; later ones are kept only once the interval has passed.
class RunHistory
{
public:
    explicit RunHistory(const std::int64_t interval_min = 24 * 60)
        : m_interval_min(interval_min > 0 ? interval_min : 1)
    {
    }

    void sample(const RunPoint& point)
    {
        if (m_started && point.timestamp_min < m_next_sample)
            return;

        m_started = true;
        m_next_sample = point.timestamp_min + m_interval_min;
        m_points.push_back(point);
    }

    void clear()
    {
        m_points.clear();
        m_started = false;
        m_next_sample = 0;
    }

    [[nodiscard]] bool empty() const { return m_points.empty(); }
    [[nodiscard]] std::size_t size() const { return m_points.size(); }
    [[nodiscard]] const std::vector<RunPoint>& points() const { return m_points; }
    [[nodiscard]] std::int64_t interval() const { return m_interval_min; }

    [[nodiscard]] RunSummary summarize() const
    {
        RunSummary summary;
        if (m_points.empty())
            return summary;

        summary.start_min = m_points.front().timestamp_min;
        summary.end_min = m_points.back().timestamp_min;
        summary.min_population = m_points.front().population;
        summary.peak_population = m_points.front().population;
        summary.end_population = m_points.back().population;

        for (const auto& point : m_points)
        {
            summary.min_population = std::min(summary.min_population, point.population);
            summary.peak_population = std::max(summary.peak_population, point.population);
        }
        return summary;
    }

private:
    std::int64_t          m_interval_min;
    std::int64_t          m_next_sample{ 0 };
    bool                  m_started{ false };
    std::vector<RunPoint> m_points;
};

} // namespace Observability
