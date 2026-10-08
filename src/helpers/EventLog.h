#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

// A lightweight, fixed-capacity event log for the simulation. Events are the
// story of a run - a birth, a death and its cause, a completed gather, a
// resource first seen - stamped with the in-game minute they happened. Keeping
// this a plain value type (no clock, map, registry or renderer) means the log
// can be recorded from EntityManager and asserted on in tests without any of
// those. The capacity is a ring: once full, the oldest event is dropped, so a
// long fast-forwarded run cannot grow the log without bound.
namespace Observability
{

enum class EventKind
{
    Birth,       // a new entity was added
    Death,       // an entity was removed (cause says why)
    Gather,      // a gather completed and banked a good
    Discovery,   // an entity first remembered a resource
    Production,  // a farm/workshop produced this hour

    Count
};

inline constexpr std::size_t kEventKindCount = static_cast<std::size_t>(EventKind::Count);

[[nodiscard]] inline const char* kindName(const EventKind kind) noexcept
{
    switch (kind)
    {
        case EventKind::Birth:      return "birth";
        case EventKind::Death:      return "death";
        case EventKind::Gather:     return "gather";
        case EventKind::Discovery:  return "discovery";
        case EventKind::Production: return "production";
        default:                    return "?";
    }
}

// Why an event happened, where that is meaningful. Deaths carry the interesting
// one (old age vs. a need that ran out); everything else is `None`.
enum class EventCause
{
    None,
    Aged,
    Starved,
    Dehydrated,
    Natural
};

[[nodiscard]] inline const char* causeName(const EventCause cause) noexcept
{
    switch (cause)
    {
        case EventCause::Aged:       return "aged";
        case EventCause::Starved:    return "starved";
        case EventCause::Dehydrated: return "dehydrated";
        case EventCause::Natural:    return "natural";
        default:                     return "none";
    }
}

struct Event
{
    EventKind    kind{ EventKind::Gather };
    EventCause   cause{ EventCause::None };
    std::int64_t timestamp_min{ 0 };   // in-game minutes since the run began
    int          value{ 0 };           // quantity (gathers, produced units) or population
    std::string  detail;               // short label: good/element name, or ""

    // One-line, greppable rendering (also useful for a headless summary).
    [[nodiscard]] std::string format() const
    {
        std::string text = std::string(kindName(kind));
        if (cause != EventCause::None)
        {
            text += "(";
            text += causeName(cause);
            text += ")";
        }
        text += " @" + std::to_string(timestamp_min) + "m";
        if (!detail.empty())
            text += " " + detail;
        if (value != 0)
            text += " x" + std::to_string(value);
        return text;
    }
};

class EventLog
{
public:
    explicit EventLog(const std::size_t capacity = 512)
        : m_capacity(capacity > 0 ? capacity : 1)
    {
    }

    void record(const EventKind kind, const EventCause cause,
                const std::int64_t timestamp_min, const int value = 0,
                std::string detail = {})
    {
        if (m_events.size() >= m_capacity)
            m_events.pop_front();
        m_events.push_back(Event{ kind, cause, timestamp_min, value, std::move(detail) });
    }

    void record(const EventKind kind, const std::int64_t timestamp_min,
                const int value = 0, std::string detail = {})
    {
        record(kind, EventCause::None, timestamp_min, value, std::move(detail));
    }

    void clear() { m_events.clear(); }

    [[nodiscard]] std::size_t size() const { return m_events.size(); }
    [[nodiscard]] std::size_t capacity() const { return m_capacity; }
    [[nodiscard]] bool empty() const { return m_events.empty(); }

    [[nodiscard]] const std::deque<Event>& events() const { return m_events; }

    [[nodiscard]] std::size_t countOf(const EventKind kind) const
    {
        std::size_t total = 0;
        for (const auto& event : m_events)
            if (event.kind == kind)
                ++total;
        return total;
    }

    [[nodiscard]] std::size_t countOf(const EventKind kind, const EventCause cause) const
    {
        std::size_t total = 0;
        for (const auto& event : m_events)
            if (event.kind == kind && event.cause == cause)
                ++total;
        return total;
    }

    [[nodiscard]] std::array<std::size_t, kEventKindCount> counts() const
    {
        std::array<std::size_t, kEventKindCount> totals{};
        for (const auto& event : m_events)
            totals[static_cast<std::size_t>(event.kind)] += 1;
        return totals;
    }

    // Every recorded event at or after `timestamp_min`, in order.
    [[nodiscard]] std::vector<Event> since(const std::int64_t timestamp_min) const
    {
        std::vector<Event> result;
        for (const auto& event : m_events)
            if (event.timestamp_min >= timestamp_min)
                result.push_back(event);
        return result;
    }

    // The most recent `n` events of a kind (newest last), for a compact HUD ticker.
    [[nodiscard]] std::vector<Event> recent(const std::size_t n, const EventKind kind) const
    {
        std::vector<Event> result;
        for (auto it = m_events.rbegin(); it != m_events.rend() && result.size() < n; ++it)
            if (it->kind == kind)
                result.push_back(*it);
        std::reverse(result.begin(), result.end());
        return result;
    }

    [[nodiscard]] std::vector<Event> recent(const std::size_t n) const
    {
        std::vector<Event> result;
        for (auto it = m_events.rbegin(); it != m_events.rend() && result.size() < n; ++it)
            result.push_back(*it);
        std::reverse(result.begin(), result.end());
        return result;
    }

private:
    std::size_t       m_capacity;
    std::deque<Event> m_events;
};

} // namespace Observability
