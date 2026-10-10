#include "EntityDecision.h"
#include "EntityManager.h"
#include "EventLog.h"
#include "GameClock.h"
#include "Knowledge.h"
#include "MapGenerator.h"
#include "MapStream.h"
#include "Resources.h"
#include "RunSummary.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

// The event log, the run history and the interrupt rule are pure value types, so
// most of these cases need no map, registry or clock. The end-to-end cases at
// the bottom drive the real EntityManager update loop to check that a run is
// recorded and that reloading the tuning works without a rebuild.

namespace
{
std::string obsEntityConfigPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/entity_data.json";
#else
    return "config/entity_data.json";
#endif
}

std::string mapConfigPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json";
#else
    return "config/map_data.json";
#endif
}

// A valid but minimal entity config, written to a temp file so a test can tune
// survival numbers without touching the shipped file.
std::string writeTempObservabilityConfig(const std::string& name, const std::string& survivalJson)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("obs_test_" + name + ".json");
    std::ofstream out(path);
    out << R"({
        "needs": { "sleep_gain_per_hour": 2 },
        "survival": )" << survivalJson << R"(,
        "decision": {
            "idle_tolerance": 3,
            "sleep":  { "threshold": 0.20, "bias": 1.0 },
            "work":   { "threshold": 0.50, "bias": 1.0 }
        }
    })";
    return path.string();
}
} // namespace

TEST_CASE("the event log records, counts and formats events")
{
    Observability::EventLog log;

    log.record(Observability::EventKind::Birth, 120, 1);
    log.record(Observability::EventKind::Gather, 180, 1, "Wood");
    log.record(Observability::EventKind::Death, Observability::EventCause::Starved, 240);

    REQUIRE(log.size() == 3);
    CHECK(log.countOf(Observability::EventKind::Birth) == 1);
    CHECK(log.countOf(Observability::EventKind::Death) == 1);
    CHECK(log.countOf(Observability::EventKind::Death, Observability::EventCause::Starved) == 1);
    CHECK(log.countOf(Observability::EventKind::Death, Observability::EventCause::Aged) == 0);

    const auto counts = log.counts();
    CHECK(counts[static_cast<std::size_t>(Observability::EventKind::Gather)] == 1);

    CHECK(log.events().back().format().rfind("death(starved)", 0) == 0);
    CHECK(log.since(180).size() == 2);

    const auto recent = log.recent(2);
    REQUIRE(recent.size() == 2);
    CHECK(recent.front().kind == Observability::EventKind::Gather);
    CHECK(recent.back().kind == Observability::EventKind::Death);
}

TEST_CASE("the event log is a bounded ring, dropping the oldest first")
{
    Observability::EventLog log(4);
    for (int i = 0; i < 10; ++i)
        log.record(Observability::EventKind::Gather, i, 1);

    CHECK(log.size() == 4);
    CHECK(log.capacity() == 4);
    // The survivors are the four newest (minutes 6..9), oldest first.
    CHECK(log.events().front().timestamp_min == 6);
    CHECK(log.events().back().timestamp_min == 9);
}

TEST_CASE("the run history samples on an interval and summarizes the shape")
{
    Observability::RunHistory history(1440); // one in-game day

    Observability::RunPoint a; a.timestamp_min = 0;    a.population = 8;
    Observability::RunPoint b; b.timestamp_min = 600;  b.population = 9;   // same day: dropped
    Observability::RunPoint c; c.timestamp_min = 1440; c.population = 11;
    Observability::RunPoint d; d.timestamp_min = 2880; d.population = 6;

    history.sample(a);
    history.sample(b);
    history.sample(c);
    history.sample(d);

    REQUIRE(history.size() == 3);
    CHECK(history.points().front().timestamp_min == 0);

    const auto summary = history.summarize();
    CHECK(summary.start_min == 0);
    CHECK(summary.end_min == 2880);
    CHECK(summary.days() == 2);
    CHECK(summary.min_population == 6);
    CHECK(summary.peak_population == 11);
    CHECK(summary.end_population == 6);
    CHECK(summary.format().rfind("run:", 0) == 0);
}

TEST_CASE("an empty run history summarizes to zeroes")
{
    Observability::RunHistory history;
    CHECK(history.empty());
    const auto summary = history.summarize();
    CHECK(summary.days() == 0);
    CHECK(summary.peak_population == 0);
}

TEST_CASE("interruptFor lets a critical need preempt a non-survival plan")
{
    EntityDecision::Config cfg; // shipped thresholds
    CPersonality personality;

    CBasicNeeds comfortable;    // rested
    CHECK_FALSE(EntityDecision::interruptFor(comfortable, personality, cfg, ActionTypes::Gathering).has_value());

    CBasicNeeds exhausted;
    exhausted.sleep = 100;      // critical fatigue

    // A gather trip (and a wander) are dropped for sleep.
    CHECK(EntityDecision::interruptFor(exhausted, personality, cfg, ActionTypes::Gathering)
          == EntityDecision::Need::Sleep);
    CHECK(EntityDecision::interruptFor(exhausted, personality, cfg, ActionTypes::Moving)
          == EntityDecision::Need::Sleep);

    // But a plan already addressing sleep is not abandoned (no thrash).
    CHECK_FALSE(EntityDecision::interruptFor(exhausted, personality, cfg, ActionTypes::Sleeping).has_value());
}

TEST_CASE("servedNeed maps actions to the need they satisfy")
{
    CHECK(EntityDecision::servedNeed(ActionTypes::Sleeping) == EntityDecision::Need::Sleep);
    CHECK(EntityDecision::servedNeed(ActionTypes::Gathering) == EntityDecision::Need::Work);
    CHECK(EntityDecision::servedNeed(ActionTypes::Building) == EntityDecision::Need::Work);
    CHECK(EntityDecision::servedNeed(ActionTypes::Moving) == EntityDecision::Need::None);
}

TEST_CASE("reloading the config re-reads tuning from disk and rejects a bad file")
{
    const std::string file = writeTempObservabilityConfig("reload",
        R"({ "initial_population": 8, "max_population": 40, "lifespan_years": 65 })");

    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, mapConfigPath());
    auto clock = std::make_shared<GameClock>(60.f);
    float delta = 1.f / 60.f;

    EntityManager entities(font, map, clock, delta, file);
    CHECK(entities.maxPopulation() == 40);

    // Rewrite the file with a new cap, then reload in place.
    {
        std::ofstream out(file);
        out << R"({
            "needs": { "sleep_gain_per_hour": 2 },
            "survival": { "initial_population": 8, "max_population": 99, "lifespan_years": 65 },
            "decision": {
                "idle_tolerance": 3,
                "sleep":  { "threshold": 0.20, "bias": 1.0 },
                "work":   { "threshold": 0.50, "bias": 1.0 }
            }
        })";
    }
    entities.reloadConfig();
    CHECK(entities.maxPopulation() == 99);

    // A reload from a missing file throws; the tuning is left as it was.
    std::filesystem::remove(file);
    CHECK_THROWS_AS(entities.reloadConfig(), std::runtime_error);
    CHECK(entities.maxPopulation() == 99);
}

TEST_CASE("a run records births, deaths, gathers and a summary")
{
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, mapConfigPath());
    auto clock = std::make_shared<GameClock>(60.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, obsEntityConfigPath());
    entities.seedPopulation();
    REQUIRE(entities.population() == 8);

    // Advance several in-game days, ticking the clock a minute at a time like the
    // game loop does. The seeded settlement forages and drinks, so gatherings and
    // discoveries should be recorded; the run history should fill to one sample
    // per day.
    for (int hour = 0; hour < 24 * 3; ++hour)
    {
        for (int step = 0; step < 60; ++step)
            clock->update(delta);
        entities.update();
    }

    const auto& events = entities.events();
    CHECK(events.countOf(Observability::EventKind::Discovery) > 0);
    CHECK(events.countOf(Observability::EventKind::Gather) > 0);

    const auto history = entities.history();
    CHECK(history.size() >= 3);
    CHECK(history.interval() == GameTime::kMinutesPerDay);

    const auto summary = entities.runSummary();
    CHECK(summary.days() >= 2);
    CHECK(summary.peak_population >= 1);
    CHECK(summary.min_population >= 0);
    CHECK(summary.end_population == entities.population());
}

TEST_CASE("a busy entity drops a wander for a critical need in the update loop")
{
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, mapConfigPath());
    auto clock = std::make_shared<GameClock>(60.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, obsEntityConfigPath());

    // Found the entity where the settlement would: a land tile with a
    // gatherable in reach, so it is a real, workable site.
    const sf::Vector2i spawn = entities.findHabitableSpawn();
    const entt::entity entity = entities.addEntity(EntityType::Human_Generic, spawn);
    REQUIRE(entities.entityCount() == 1);

    // One update lets the settlement remember nearby resources in its shared
    // knowledge.
    entities.update();

    // Force the entity onto a long, non-survival errand and make it critically
    // exhausted. The update should abandon the wander to sleep.
    auto* queue = entities.actionsOf(entity);
    REQUIRE(queue != nullptr);
    queue->actions.clear();
    queue->actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, sf::Vector2i{ 100000, 100000 }));

    auto* needs = entities.needsOf(entity);
    REQUIRE(needs != nullptr);
    needs->sleep = CBasicNeeds::kMax;

    entities.update();

    const auto* after = entities.actionsOf(entity);
    REQUIRE(after != nullptr);
    REQUIRE_FALSE(after->actions.empty());

    // Sleep needs no target, so the wander is always replaced by a sleep action.
    bool sleeping = false;
    for (const auto& action : after->actions)
        if (action && action->action_name == ActionTypes::Sleeping)
            sleeping = true;
    CHECK(sleeping);
}

TEST_CASE("a fresh settlement survives its first day within the population cap")
{
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, mapConfigPath());
    auto clock = std::make_shared<GameClock>(60.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, obsEntityConfigPath());
    entities.seedPopulation();

    const int cap = entities.maxPopulation();

    // One full in-game day, hour by hour.
    for (int hour = 0; hour < 24; ++hour)
    {
        for (int step = 0; step < 60; ++step)
            clock->update(delta);
        entities.update();

        // The population is always a real count within the configured band: a
        // settle that loses everyone, or overshoots its cap, is a regression.
        CHECK(entities.population() >= 1);
        CHECK(entities.population() <= cap);
    }

    CHECK(entities.births() >= 0);
    CHECK(entities.deaths() >= 0);
}

TEST_CASE("a builder finishes a site on the tile it stands on")
{
    // End-to-end construction: the settlement cost-starts a site, a builder walks
    // to it and works it. Because work only accrues on the tile, the finished
    // building's completion must be recorded while the builder is standing there
    // (offTileWork stays zero). The staffing is pinned to a single builder so the
    // shortage rule cannot reassign it mid-run.
    const std::filesystem::path cfg =
        std::filesystem::temp_directory_path() / "obs_test_builder.json";
    {
        std::ofstream out(cfg);
        out << R"({
            "needs": { "sleep_gain_per_hour": 2 },
            "survival": { "initial_population": 1, "max_population": 99, "lifespan_years": 65 },
            "entity_types": { "Human_Generic": { "job": "builder" } },
            "economy": { "jobs": { "farmer": 0, "lumberjack": 0, "miner": 0, "builder": 1, "explorer": 0 } },
            "decision": {
                "idle_tolerance": 3,
                "sleep": { "threshold": 0.20, "bias": 1.0 },
                "work":  { "threshold": 0.50, "bias": 1.0 }
            }
        })";
    }

    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, mapConfigPath());
    auto clock = std::make_shared<GameClock>(60.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, cfg.string());

    const sf::Vector2i spawn = entities.findHabitableSpawn();
    TestSupport::primeChunks(*map, frames, spawn, 512);
    entities.addEntity(EntityType::Human_Generic, spawn); // builder by config

    entities.planConstructionForTest();
    REQUIRE(entities.buildingCount() > 0);

    for (int step = 0; step < 60 * 60 * 24 * 4 && entities.completedBuildingCount() == 0; ++step)
    {
        clock->update(delta);
        entities.update();
        entities.resolveCollisions();
    }

    CHECK(entities.completedBuildingCount() > 0);
    CHECK(entities.offTileWork() == 0);

    std::filesystem::remove(cfg);
}

// CivKnowledge is a pure value type (Chunk.h + Resources.h only), so it is
// tested directly without a map or registry.
TEST_CASE("shared knowledge merges observations and deduplicates tiles")
{
    CivKnowledge knowledge(8);

    const std::unordered_map<Elements, sf::Vector2i> first{
        { Elements::ocean, { 10, 10 } },
        { Elements::forest, { 20, 20 } },
    };
    knowledge.remember(first);
    CHECK(knowledge.knownLocations() == 2);
    CHECK(knowledge.location(Elements::ocean, { 0, 0 }) == std::optional<sf::Vector2i>{ { 10, 10 } });

    // Re-observing the same tiles adds nothing, so knowledge does not grow with
    // repeated scans of the same area.
    knowledge.remember(first);
    CHECK(knowledge.knownLocations() == 2);

    // A second entity's observation merges into the one shared store.
    knowledge.remember({ { Elements::ocean, { 50, 50 } } });
    CHECK(knowledge.knownLocations() == 3);
}

TEST_CASE("shared knowledge finds the nearest remembered location of a kind")
{
    CivKnowledge knowledge(8);
    knowledge.remember({ { Elements::forest, { 100, 0 } } });
    knowledge.remember({ { Elements::farm, { 5, 0 } } });
    knowledge.remember({ { Elements::forest, { 9, 0 } } });

    // Food covers farm tiles; the nearest to the query wins.
    CHECK(knowledge.findNearest({ 0, 0 }, CivKnowledge::Kind::Food) == std::optional<sf::Vector2i>{ { 5, 0 } });
}

TEST_CASE("explored coverage is bucketed and bounded by max_cells")
{
    CivKnowledge knowledge(8);
    knowledge.observe({ 0, 0 }, 8);
    CHECK(knowledge.isExplored({ 0, 0 }));
    CHECK(knowledge.isExplored({ 7, 7 }));          // same 8-tile cell as the origin
    CHECK(knowledge.isExplored({ -1, -1 }));        // the cell containing the origin too

    // The cap holds even after many observations: coverage cannot grow without
    // bound no matter how long the settlement explores.
    CivKnowledge capped(8, /*max_cells=*/4);
    for (int i = 0; i < 100; ++i)
        capped.observe({ i * 40, i * 40 }, 8);
    CHECK(capped.exploredCells() == 4);
}

TEST_CASE("the frontier is the nearest unexplored cell")
{
    // Nothing observed yet: the unexplored cell underfoot is the frontier, and
    // it is returned as a cell coordinate (the caller scales it back to tiles).
    CivKnowledge fresh(8);
    CHECK(fresh.nearestFrontier({ 3, 3 }) == std::optional<sf::Vector2i>{ { 0, 0 } });

    // With the surrounding cells known, the frontier is the nearest unknown cell
    // at the edge of the known world.
    CivKnowledge knowledge(8);
    knowledge.observe({ 0, 0 }, 0);
    CHECK(knowledge.nearestFrontier({ 0, 0 }) == std::optional<sf::Vector2i>{ { -2, -2 } });
}
