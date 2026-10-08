#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// Calendar constants for the in-game clock. The simulation uses a tidy 12 x
// 30-day year (360 days) so dates are easy to reason about and display. Hours
// remain the finest unit the survival systems care about; minutes exist so
// actions (eating, sleeping) have a duration to run for.
namespace GameTime
{
constexpr int kMinutesPerHour = 60;
constexpr int kHoursPerDay    = 24;
constexpr int kDaysPerMonth   = 30;
constexpr int kMonthsPerYear  = 12;
constexpr int kDaysPerYear    = kDaysPerMonth * kMonthsPerYear;   // 360
constexpr int kHoursPerYear   = kDaysPerYear * kHoursPerDay;      // 8640
constexpr int kMinutesPerDay  = kHoursPerDay * kMinutesPerHour;   // 1440

[[nodiscard]] constexpr int yearOf(const std::int64_t day) noexcept
{
    return static_cast<int>(day / kDaysPerYear) + 1;
}

[[nodiscard]] constexpr int monthOf(const std::int64_t day) noexcept
{
    return static_cast<int>((day % kDaysPerYear) / kDaysPerMonth) + 1;
}

[[nodiscard]] constexpr int dayOfMonthOf(const std::int64_t day) noexcept
{
    return static_cast<int>(day % kDaysPerMonth) + 1;
}
} // namespace GameTime

// The in-game clock. Time is tracked to the minute, but `update` is fed a real
// frame delta and a time scale measured in *in-game minutes per real second*.
// The same clock can therefore run at "one hour per second" or "one month per
// second" without the survival systems that read it having to change.
//
// The speed presets give the HUD a small, labelled set of rates to cycle
// through; `faster`/`slower` step to the next preset above/below the current
// rate, so a custom scale set through `setTimeScale` still steps sensibly.
class GameClock
{
public:
    struct Speed
    {
        float       minutesPerSecond;
        std::string label;
    };

    explicit GameClock(float timeScale = 60.0f)
        : m_timeScale(timeScale)
    {
    }

    void update(float deltaTime)
    {
        if (m_paused)
            return;

        m_accumulator += deltaTime * m_timeScale;

        while (m_accumulator >= 1.0f)
        {
            m_accumulator -= 1.0f;
            tick();
        }
    }

    // SETTERS
    void setTimeScale(float scale) { m_timeScale = std::max(0.f, scale); }
    void pause(bool p) { m_paused = p; }
    void resume() { m_paused = false; }
    void togglePause() { m_paused = !m_paused; }

    // SPEED PRESETS
    void faster()
    {
        for (const auto& speed : presets())
        {
            if (speed.minutesPerSecond > m_timeScale)
            {
                m_timeScale = speed.minutesPerSecond;
                return;
            }
        }
    }

    void slower()
    {
        float chosen = -1.f;
        for (const auto& speed : presets())
        {
            if (speed.minutesPerSecond < m_timeScale)
                chosen = speed.minutesPerSecond;
        }
        if (chosen > 0.f)
            m_timeScale = chosen;
    }

    void setSpeedIndex(int index)
    {
        const auto& all = presets();
        if (index < 0 || index >= static_cast<int>(all.size()))
            return;
        m_timeScale = all[static_cast<std::size_t>(index)].minutesPerSecond;
    }

    // Jump to a given time of day, used to start a run in daylight rather than
    // at midnight (when a settlement would otherwise spend its first hours
    // asleep).
    void setTime(int hour, int minute = 0)
    {
        m_hour = ((hour % GameTime::kHoursPerDay) + GameTime::kHoursPerDay) % GameTime::kHoursPerDay;
        m_minute = ((minute % GameTime::kMinutesPerHour) + GameTime::kMinutesPerHour) % GameTime::kMinutesPerHour;
    }

    // GETTERS
    float getTimeScale() const { return m_timeScale; }
    bool  isPaused() const { return m_paused; }

    // -1 when the current scale is not exactly one of the presets.
    int getSpeedIndex() const
    {
        const auto& all = presets();
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            if (all[i].minutesPerSecond == m_timeScale)
                return static_cast<int>(i);
        }
        return -1;
    }

    int getSpeedCount() const { return static_cast<int>(presets().size()); }

    const Speed& getSpeed(int index) const
    {
        const auto& all = presets();
        const int clamped = std::clamp(index, 0, static_cast<int>(all.size()) - 1);
        return all[static_cast<std::size_t>(clamped)];
    }

    // A short human label for the current rate, e.g. "1 day/s". Falls back to a
    // formatted value for a custom scale.
    std::string getSpeedLabel() const
    {
        const int index = getSpeedIndex();
        if (index >= 0)
            return presets()[static_cast<std::size_t>(index)].label;

        if (m_timeScale <= 0.f)
            return "paused";

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.2f h/s",
                      static_cast<double>(m_timeScale / static_cast<float>(GameTime::kMinutesPerHour)));
        return buffer;
    }

    int getHour() const { return m_hour; }
    int getMinute() const { return m_minute; }
    int getDays() const { return m_days; }

    int getYear() const { return GameTime::yearOf(m_days); }
    int getMonth() const { return GameTime::monthOf(m_days); }
    int getDayOfMonth() const { return GameTime::dayOfMonthOf(m_days); }

    // Return the total minutes from the beginning of the simulation
    std::int64_t getTimestamp() const
    {
        return static_cast<std::int64_t>(m_days) * GameTime::kMinutesPerDay
            + static_cast<std::int64_t>(m_hour) * GameTime::kMinutesPerHour
            + m_minute;
    }

    // Short human-readable date, e.g. "Year 1, Spring, Day 12".
    std::string formatDate() const
    {
        return "Year " + std::to_string(getYear()) + ", " + getSeasonName()
             + ", Day " + std::to_string(getDayOfMonth());
    }

    // 24-hour clock, e.g. "08:00".
    std::string formatClock() const
    {
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", m_hour, m_minute);
        return buffer;
    }

    std::string getSeasonName() const
    {
        switch ((getMonth() - 1) / 3)
        {
            case 0:  return "Spring";
            case 1:  return "Summer";
            case 2:  return "Autumn";
            default: return "Winter";
        }
    }

    // Triggered when a new day starts
    void onNewDay(std::function<void()> callback) { m_newDayCallback = std::move(callback); }

private:
    void tick()
    {
        ++m_minute;
        if (m_minute >= GameTime::kMinutesPerHour)
        {
            m_minute = 0;
            ++m_hour;

            if (m_hour >= GameTime::kHoursPerDay)
            {
                m_hour = 0;
                ++m_days;

                if (m_newDayCallback) m_newDayCallback();
            }
        }
    }

    static const std::vector<Speed>& presets()
    {
        static const std::vector<Speed> kPresets{
            {    12.f, "12 min/s" },
            {    60.f, "1 hour/s" },
            {   360.f, "6 hours/s" },
            {  1440.f, "1 day/s" },
            { 10080.f, "1 week/s" },
            { 43200.f, "1 month/s" },
        };
        return kPresets;
    }

    float m_timeScale;  // in-game minutes per real second
    float m_accumulator = 0.0f;
    int m_hour = 0;
    int m_minute = 0;
    int m_days = 0;
    bool m_paused = false;

    std::function<void()> m_newDayCallback;
};
