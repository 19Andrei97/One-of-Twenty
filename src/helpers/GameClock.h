#pragma once

#include <functional>

class GameClock {
public:
    GameClock(float timeScale = 60.0f)
        : m_timeScale(timeScale) {
    }

    void update(float deltaTime) {
        if (m_paused) return;

        m_accumulator += deltaTime * m_timeScale;

        while (m_accumulator >= 1.0f) {
            m_accumulator -= 1.0f;
            tick();
        }
    }

    // SETTERS
    void setTimeScale(float scale) { m_timeScale = scale; }
    void pause(bool p) { m_paused = p; }

    // GETTERS
    int getHour() const { return m_hour; }
    int getMinute() const { return m_minute; }
    int getDays() const { return m_days; }
    // Return the total minutes from the beginning of the simulation
    std::int64_t getTimestamp() const 
    {
        return static_cast<std::int64_t>(m_days) * 24 * 60
            + static_cast<std::int64_t>(m_hour) * 60
            + m_minute;
    }

    // Triggered when a new day starts
    void onNewDay(std::function<void()> callback) { m_newDayCallback = callback; }

private:
    void tick() {
        ++m_minute;
        if (m_minute >= 60) {
            m_minute = 0;
            ++m_hour;

            if (m_hour >= 24) {
                m_hour = 0;
                ++m_days;

                if (m_newDayCallback) m_newDayCallback();
            }
        }
    }

    float m_timeScale;  // how much faster than real time
    float m_accumulator = 0.0f;
    int m_hour = 0;
    int m_minute = 0;
    int m_days = 0;
    bool m_paused = false;

    std::function<void()> m_newDayCallback;
};