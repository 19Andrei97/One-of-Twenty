#include "HeadlessSimulation.h"

#include "MapGenerator.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>

#include <nlohmann/json.hpp>

// The headless runner is what lets the simulation run with no screen. These
// tests pin the two properties it rests on: chunk streaming works with no GL
// context, and the clock/map/entity loop advances a settlement without a window.

namespace
{

std::string sourceDir()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return ONE_OF_TWENTY_SOURCE_DIR;
#else
    return ".";
#endif
}

// A config.json that points every asset at the source tree (ctest runs from the
// build tree, so relative paths would miss) and logs to the temp directory.
// Built as a JSON object so paths are escaped correctly: on Windows they contain
// backslashes, which are invalid escapes if interpolated into a raw JSON string.
std::string headlessConfig()
{
    const std::filesystem::path src = sourceDir();
    const std::filesystem::path tmp = std::filesystem::temp_directory_path();

    const nlohmann::json cfg = {
        { "map",       { { "file", (src / "config/map_data.json").string() } } },
        { "entity",    { { "file", (src / "config/entity_data.json").string() } } },
        { "buildings", { { "file", (src / "config/buildings.json").string() } } },
        { "logger",    { { "file", (tmp / "one_of_twenty_headless_test.log").string() },
                         { "level", "warn" } } },
        { "time",      { { "start_hour", 8 }, { "start_minute", 0 }, { "speed_index", 1 } } },
    };

    const std::filesystem::path path = tmp / "one_of_twenty_headless_test.json";
    std::ofstream out(path);
    out << cfg.dump(2);
    return path.string();
}

} // namespace

TEST_CASE("MapGenerator streams chunks synchronously with no display")
{
    // The synchronous generator loads chunks on the calling thread, so this runs
    // without an X11 display (the render test skips there).
    int frames = 0;
    MapGenerator map(frames, sourceDir() + "/config/map_data.json", /*synchronous=*/true);
    map.setSeed(42);
    map.setNoises();

    const sf::IntRect view{ { 0, 0 }, { 512, 512 } };
    map.stream(view);

    bool loaded = false;
    for (const auto& line : map.getPositionInfo({ 8, 8 }))
        if (line.rfind("Type:", 0) == 0)
            loaded = true;
    CHECK(loaded);

    // The authoritative lookup now agrees with the noise sample, because a chunk
    // really is loaded (not a noise fallback).
    CHECK(map.getElementAtWorld({ 8, 8 }) == map.getBiomeElement({ 8, 8 }));
}

TEST_CASE("a headless run advances the settlement with no display")
{
    HeadlessSimulation sim(headlessConfig());
    CHECK(sim.population() == 0); // nothing seeded until seedPopulation/run

    sim.setSeed(7);
    sim.setPopulation(8);         // seeds the founders explicitly
    sim.setTimeScale(120.f);      // 2 in-game hours per real second
    sim.setStartHour(8);
    sim.setReportIntervalDays(0); // quiet

    sim.runDays(3.0);

    // Three in-game days without a window: the settlement is alive, has gathered
    // resources, and has advanced the clock.
    CHECK(sim.day() >= 3);
    CHECK(sim.population() > 0);
    CHECK(sim.gathers() > 0);

    // The sim runs the same systems as the game, so it can drive a construction
    // plan and (eventually) complete a building without a render target.
    CHECK(sim.buildings() >= 0);
}

TEST_CASE("a headless settlement can build and feed itself")
{
    // The whole point of the runner is that the *real* simulation loop (decision
    // -> path -> gather/build -> food) runs with no window. A settlement that
    // completes buildings and stockpiles food proves that loop is intact
    // headless. Seed 7 / rng-seed 4 thrive at the default 6 h/s.
    HeadlessSimulation sim(headlessConfig());
    sim.setSeed(7);
    sim.setRandomSeed(4);
    sim.setPopulation(8);
    sim.setTimeScale(360.f);      // 6 in-game hours per real second
    sim.setStartHour(8);
    sim.setReportIntervalDays(0);

    sim.runDays(60.0);

    CHECK(sim.completedBuildings() > 0);   // builders finish sites
    CHECK(sim.food() > 0);                 // farms feed the settlement
    CHECK(sim.population() > 8);           // and it grows
    CHECK(sim.deaths() == 0);              // nobody starved
}

TEST_CASE("the headless runner is repeatable for a fixed seed")
{
    auto runOnce = []() {
        HeadlessSimulation sim(headlessConfig());
        sim.setSeed(1234);
        sim.setRandomSeed(12345);
        sim.setPopulation(6);
        sim.setTimeScale(120.f);
        sim.setReportIntervalDays(0);
        sim.runDays(2.0);
        return std::make_tuple(sim.population(), sim.gathers(), sim.deaths());
    };

    CHECK(runOnce() == runOnce());
}
