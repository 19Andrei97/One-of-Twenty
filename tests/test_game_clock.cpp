#include <doctest/doctest.h>

#include "GameClock.h"

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
    advanceMinutes(clock, 120);
    CHECK(clock.getMinute() == 30);
    CHECK(clock.getTimestamp() == 30);

    clock.pause(false);
    advanceMinutes(clock, 5);
    CHECK(clock.getMinute() == 35);
}

TEST_CASE("large deltaTime advances multiple ticks")
{
    GameClock clock(1.0f);
    clock.update(90.0f); // 90 in-game minutes in one step

    CHECK(clock.getHour() == 1);
    CHECK(clock.getMinute() == 30);
}
