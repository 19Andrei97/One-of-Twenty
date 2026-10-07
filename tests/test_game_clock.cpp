#include <doctest/doctest.h>

#include "GameClock.h"

#include <string>

namespace
{
    // Advance the clock by `minutes` in-game minutes. The clock is configured
    // with timeScale = 1 so one update of 1s of deltaTime advances one minute.
    void advanceMinutes(GameClock& clock, int minutes)
    {
        for (int i = 0; i < minutes; ++i)
            clock.update(1.0f);
    }
}

TEST_CASE("clock starts at midnight of day zero")
{
    GameClock clock;
    CHECK(clock.getHour() == 0);
    CHECK(clock.getMinute() == 0);
    CHECK(clock.getDays() == 0);
    CHECK(clock.getTimestamp() == 0);
    CHECK(clock.getYear() == 1);
    CHECK(clock.getMonth() == 1);
    CHECK(clock.getDayOfMonth() == 1);
}

TEST_CASE("minutes roll into hours")
{
    GameClock clock(1.0f);
    advanceMinutes(clock, 59);
    CHECK(clock.getMinute() == 59);
    CHECK(clock.getHour() == 0);

    advanceMinutes(clock, 1);
    CHECK(clock.getMinute() == 0);
    CHECK(clock.getHour() == 1);
}

TEST_CASE("hours roll into days")
{
    GameClock clock(1.0f);
    advanceMinutes(clock, 24 * 60);
    CHECK(clock.getDays() == 1);
    CHECK(clock.getHour() == 0);
    CHECK(clock.getMinute() == 0);
    CHECK(clock.getTimestamp() == 1440);
}

TEST_CASE("onNewDay fires exactly once per day")
{
    GameClock clock(1.0f);

    int calls = 0;
    clock.onNewDay([&calls] { ++calls; });

    advanceMinutes(clock, 24 * 60 - 1);
    CHECK(calls == 0);

    advanceMinutes(clock, 1); // day 1 begins
    CHECK(calls == 1);

    advanceMinutes(clock, 24 * 60);
    CHECK(calls == 2);
}

TEST_CASE("pause freezes the clock")
{
    GameClock clock(1.0f);
    advanceMinutes(clock, 30);

    clock.pause(true);
    CHECK(clock.isPaused());
    advanceMinutes(clock, 120);
    CHECK(clock.getMinute() == 30);
    CHECK(clock.getTimestamp() == 30);

    clock.pause(false);
    CHECK_FALSE(clock.isPaused());
    advanceMinutes(clock, 5);
    CHECK(clock.getMinute() == 35);
}

TEST_CASE("togglePause flips between running and stopped")
{
    GameClock clock(1.0f);
    CHECK_FALSE(clock.isPaused());

    clock.togglePause();
    CHECK(clock.isPaused());
    advanceMinutes(clock, 10);
    CHECK(clock.getTimestamp() == 0);

    clock.togglePause();
    CHECK_FALSE(clock.isPaused());
    advanceMinutes(clock, 10);
    CHECK(clock.getTimestamp() == 10);
}

TEST_CASE("large deltaTime advances multiple ticks")
{
    GameClock clock(1.0f);
    clock.update(90.0f); // 90 in-game minutes in one step

    CHECK(clock.getHour() == 1);
    CHECK(clock.getMinute() == 30);
}

TEST_CASE("calendar rolls days into months and years")
{
    GameClock clock(1.0f);

    // 29 days in: still the first month.
    advanceMinutes(clock, 29 * 24 * 60);
    CHECK(clock.getYear() == 1);
    CHECK(clock.getMonth() == 1);
    CHECK(clock.getDayOfMonth() == 30);

    // One more day rolls into month 2.
    advanceMinutes(clock, 24 * 60);
    CHECK(clock.getMonth() == 2);
    CHECK(clock.getDayOfMonth() == 1);

    // A full year (360 days) rolls into year 2.
    GameClock year(1.0f);
    advanceMinutes(year, GameTime::kDaysPerYear * 24 * 60);
    CHECK(year.getYear() == 2);
    CHECK(year.getMonth() == 1);
    CHECK(year.getDayOfMonth() == 1);
}

TEST_CASE("seasons follow the months")
{
    GameClock clock(1.0f);
    clock.setTime(12, 0);

    advanceMinutes(clock, (1 - 1) * GameTime::kDaysPerMonth * 24 * 60);
    CHECK(clock.getSeasonName() == "Spring");

    advanceMinutes(clock, 3 * GameTime::kDaysPerMonth * 24 * 60);
    CHECK(clock.getSeasonName() == "Summer");

    advanceMinutes(clock, 3 * GameTime::kDaysPerMonth * 24 * 60);
    CHECK(clock.getSeasonName() == "Autumn");

    advanceMinutes(clock, 3 * GameTime::kDaysPerMonth * 24 * 60);
    CHECK(clock.getSeasonName() == "Winter");
}

TEST_CASE("speed presets are ordered and faster/slower step through them")
{
    GameClock clock;
    CHECK(clock.getSpeedCount() >= 3);

    // Presets must be strictly ascending for faster()/slower() to make sense.
    for (int i = 1; i < clock.getSpeedCount(); ++i)
        CHECK(clock.getSpeed(i).minutesPerSecond > clock.getSpeed(i - 1).minutesPerSecond);

    clock.setSpeedIndex(0);
    CHECK(clock.getSpeedIndex() == 0);
    const float first = clock.getTimeScale();

    clock.faster();
    CHECK(clock.getSpeedIndex() == 1);
    CHECK(clock.getTimeScale() > first);

    clock.slower();
    CHECK(clock.getSpeedIndex() == 0);
    CHECK(clock.getTimeScale() == doctest::Approx(first));

    // slower() at the bottom and faster() at the top clamp instead of wrapping.
    clock.slower();
    CHECK(clock.getSpeedIndex() == 0);

    clock.setSpeedIndex(clock.getSpeedCount() - 1);
    clock.faster();
    CHECK(clock.getSpeedIndex() == clock.getSpeedCount() - 1);
}

TEST_CASE("speed presets carry a human label")
{
    GameClock clock;
    clock.setSpeedIndex(0);
    CHECK_FALSE(clock.getSpeedLabel().empty());
    CHECK(clock.getSpeedLabel() == clock.getSpeed(0).label);

    clock.setSpeedIndex(clock.getSpeedCount() - 1);
    CHECK(clock.getSpeedLabel() == clock.getSpeed(clock.getSpeedCount() - 1).label);
}

TEST_CASE("a custom time scale still steps to the nearest preset")
{
    GameClock clock(120.f); // 2 hours/s, not a preset
    CHECK(clock.getSpeedIndex() == -1);
    CHECK_FALSE(clock.getSpeedLabel().empty());

    clock.faster();
    CHECK(clock.getTimeScale() == doctest::Approx(360.f)); // next preset above

    clock.setTimeScale(120.f);
    clock.slower();
    CHECK(clock.getTimeScale() == doctest::Approx(60.f)); // next preset below
}

TEST_CASE("setTime jumps to a time of day and wraps")
{
    GameClock clock(1.0f);
    clock.setTime(8, 30);
    CHECK(clock.getHour() == 8);
    CHECK(clock.getMinute() == 30);
    CHECK(clock.getTimestamp() == 8 * 60 + 30);

    clock.setTime(25, 70); // wraps into the next day's hour/minute
    CHECK(clock.getHour() == 1);
    CHECK(clock.getMinute() == 10);
}

TEST_CASE("formatted date and clock are readable")
{
    GameClock clock(1.0f);
    clock.setTime(8, 5);
    CHECK(clock.formatClock() == "08:05");
    CHECK(clock.formatDate() == "Year 1, Spring, Day 1");

    advanceMinutes(clock, GameTime::kDaysPerYear * 24 * 60 + 2 * GameTime::kDaysPerMonth * 24 * 60);
    CHECK(clock.getYear() == 2);
    CHECK(clock.getMonth() == 3);
    CHECK(clock.getDayOfMonth() == 1);
    CHECK(clock.formatDate() == "Year 2, Spring, Day 1");
}
