#include "EntityConfig.h"
#include "EntityManager.h"
#include "EntityVitals.h"
#include "GameClock.h"
#include "MapStream.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

// EntityVitals is a pure helper over CBasicNeeds/CLifespan, so most of these
// cases build no map, registry or clock. The end-to-end case at the bottom
// exercises the real update loop.

namespace
{
std::string entityConfigPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/entity_data.json";
#else
    return "config/entity_data.json";
#endif
}

// Write a short-lived entity config so a test can shrink a lifespan that is
// otherwise on the scale of a human life.
std::string writeTempEntityConfig(const std::string& name, const std::string& survivalJson)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("entity_test_" + name + ".json");
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

TEST_CASE("isStarving trips once enough days pass without food")
{
    CBasicNeeds needs; // days_without_food 0
    CHECK_FALSE(EntityVitals::isStarving(needs, 3));

    needs.days_without_food = 2;
    CHECK_FALSE(EntityVitals::isStarving(needs, 3));

    needs.days_without_food = 3;
    CHECK(EntityVitals::isStarving(needs, 3));
    CHECK(EntityVitals::isStarving(needs, 2));

    // A non-zero threshold is inclusive: four hungry days clear a limit of four.
    CHECK_FALSE(EntityVitals::isStarving(needs, 4));
    needs.days_without_food = 4;
    CHECK(EntityVitals::isStarving(needs, 4));
}

TEST_CASE("isAged reports an exhausted lifespan")
{
    CHECK_FALSE(EntityVitals::isAged(CLifespan{ 10 }));
    CHECK(EntityVitals::isAged(CLifespan{ 0 }));
}

TEST_CASE("comfort is one when rested and zero when exhausted")
{
    CBasicNeeds needs; // sleep 0
    CHECK(EntityVitals::comfort(needs) == doctest::Approx(1.f));

    needs.sleep = 50;
    CHECK(EntityVitals::comfort(needs) == doctest::Approx(0.5f));

    needs.sleep = 100;
    CHECK(EntityVitals::comfort(needs) == doctest::Approx(0.f));
}

TEST_CASE("loadEntityConfig reads the survival block")
{
    const EntityConfig cfg = loadEntityConfig(entityConfigPath());

    CHECK(cfg.survival.initial_population == 8);
    CHECK(cfg.survival.max_population == 120);
    CHECK(cfg.survival.lethal_days_without_food == 3);
    CHECK(cfg.survival.food_per_person_per_day == 1);
    // Lifespan is authored in years and stored in hours.
    CHECK(cfg.survival.lifespan_hours == 65 * GameTime::kHoursPerYear);
    CHECK(cfg.survival.birth_comfort == doctest::Approx(0.8f));
    // Birth interval is authored in days and stored in hours.
    CHECK(cfg.survival.birth_interval_hours == 300 * GameTime::kHoursPerDay);
    CHECK(cfg.survival.starvation_damage_per_hour == 4);
    CHECK(cfg.survival.health_regen_per_hour == 1);
}

TEST_CASE("loadEntityConfig accepts the older hours-based keys")
{
    const std::string file = writeTempEntityConfig("legacy",
        R"({ "initial_population": 4, "max_population": 20, "lifespan_hours": 720, "birth_cooldown_hours": 48 })");
    const EntityConfig cfg = loadEntityConfig(file);

    CHECK(cfg.survival.lifespan_hours == 720);
    CHECK(cfg.survival.birth_interval_hours == 48);
    std::filesystem::remove(file);
}

TEST_CASE("survival defaults apply when the block is absent")
{
    const EntityConfig cfg = EntityConfig{};
    CHECK(cfg.survival.initial_population > 0);
    CHECK(cfg.survival.max_population >= cfg.survival.initial_population);
    CHECK(cfg.survival.lifespan_hours > 0);
}

TEST_CASE("ageing retires the starting population once its lifespan runs out")
{
    // End to end through the real update loop with a deliberately short
    // lifespan (via a temp config), so the run stays quick. Every founder is
    // eventually removed by old age; births may add more but cannot keep the
    // founders alive.
    const std::string file = writeTempEntityConfig("short_life",
        R"({ "initial_population": 8, "max_population": 40, "lifespan_years": 1 })");

    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, file);

    CHECK(entities.population() == 0);

    entities.seedPopulation();
    CHECK(entities.population() == 8);
    CHECK(entities.population() == entities.entityCount());
    CHECK(entities.maxPopulation() == 40);
    CHECK(entities.lifespanHours() == GameTime::kHoursPerYear);

    const auto advanceHour = [&]()
    {
        for (int step = 0; step < 60; ++step)
            clock->update(delta);
        entities.update();
    };

    // Run past a full lifespan (plus the ageing spread) so every founder ages out.
    for (int hour = 0; hour < GameTime::kHoursPerYear + 100; ++hour)
        advanceHour();

    CHECK(entities.deaths() >= 8);
    CHECK(entities.population() <= entities.maxPopulation());

    std::filesystem::remove(file);
}

TEST_CASE("seedPopulation is idempotent")
{
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());

    entities.seedPopulation();
    const int seeded = entities.population();
    entities.seedPopulation();

    CHECK(entities.population() == seeded);
}

TEST_CASE("the seeded settlement has gatherables within vision")
{
    // The founders spawn together on a workable site, so the settlement can
    // gather wood (to build its first farm) without leaving the neighbourhood.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");

    float delta = 1.f / 60.f;
    auto clock = std::make_shared<GameClock>(120.f);
    EntityManager entities(font, map, clock, delta, entityConfigPath());

    const sf::Vector2i spawn = entities.findHabitableSpawn();
    const auto nearby = map->getResourcesWithinBoundary(spawn, CVision{}.radius);
    const bool hasGatherable = std::any_of(nearby.begin(), nearby.end(),
            [](const auto& entry) { return Resources::isGatherable(entry.first); });
    CHECK(hasGatherable);

    entities.seedPopulation();
    CHECK(entities.population() == 8);
}

TEST_CASE("the seeded population neither dies out nor grows without bound")
{
    // End to end through the real update loop: with wood to gather and farms to
    // work the settlement survives several simulated years. Reproduction is slow
    // (a multi-year interval), so over this window it should hold together rather
    // than boom; the hard cap is what keeps it from running away.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(120.f);
    clock->setTime(8, 0); // mirror the scene, which starts mid-morning

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());
    entities.seedPopulation();

    // Stream the neighbourhood in, as the game's render loop does: without a
    // loaded chunk a construction site cannot be written to the map and the
    // settlement can never found its first farm.
    const sf::Vector2i spawn = entities.findHabitableSpawn();
    TestSupport::primeChunks(*map, frames, spawn, 512);

    // Five simulated years. One update per in-game hour (120 min/s, 0.5 s of
    // real time) keeps the run quick while still driving the hourly survival
    // systems exactly as the game loop does.
    for (int hour = 0; hour < GameTime::kHoursPerYear * 5; ++hour)
    {
        clock->update(0.5f);
        entities.update();
    }

    CHECK(entities.population() > 0);
    CHECK(entities.population() <= entities.maxPopulation());
}

TEST_CASE("a comfortable settlement reproduces and births show up over time")
{
    // The birth gate is per entity and slow, so give a small, hand-tuned
    // settlement a short interval to prove the loop actually produces children.
    // It still needs enough founders to staff a builder (a settlement of pure
    // farmers cannot finish the very farms that feed it).
    const std::string file = writeTempEntityConfig("breeders",
        R"({ "initial_population": 8, "max_population": 40, "lifespan_years": 5, "birth_interval_days": 30 })");

    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(120.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, file);
    entities.seedPopulation();

    // Stream chunks so the settlement can found farms and feed itself.
    TestSupport::primeChunks(*map, frames, entities.findHabitableSpawn(), 512);

    bool born = false;
    for (int hour = 0; hour < 24 * 200 && !born; ++hour)
    {
        for (int step = 0; step < 60; ++step)
            clock->update(delta);
        entities.update();
        born = entities.births() > 0;
    }

    CHECK(born);
    CHECK(entities.births() > 0);
    CHECK(entities.population() > 4);

    std::filesystem::remove(file);
}

TEST_CASE("a fast clock does not outrun survival")
{
    // Regression: needs decay in in-game hours, so if movement ran on real time
    // the settlement would starve before it could walk to work. At one month per
    // second (the fastest preset) the founders must still survive a year.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(43200.f); // 1 month/s
    clock->setTime(8, 0);

    // One in-game hour per update: 43200 * (1/720) == 60 minutes.
    float delta = 1.f / 720.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());
    entities.seedPopulation();

    TestSupport::primeChunks(*map, frames, entities.findHabitableSpawn(), 512);

    for (int hour = 0; hour < GameTime::kHoursPerYear; ++hour)
    {
        clock->update(delta);
        entities.update();
    }

    CHECK(entities.population() > 0);
    CHECK(entities.population() <= entities.maxPopulation());
}

TEST_CASE("a paused clock freezes entity movement")
{
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f); // 1 hour/s
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());
    entities.seedPopulation();

    // Stream chunks so the settlement can found farms and feed itself.
    TestSupport::primeChunks(*map, frames, entities.findHabitableSpawn(), 512);

    const auto runFrames = [&](int count)
    {
        for (int i = 0; i < count; ++i)
        {
            clock->update(delta);
            entities.update();
        }
    };

    // Warm up so entities have begun walking to work and rest.
    runFrames(60 * 200);
    const auto moving = entities.entityPositions();
    CHECK(entities.population() > 0);

    clock->pause(true);
    const auto frozenTimestamp = clock->getTimestamp();
    runFrames(60 * 200);
    CHECK(clock->getTimestamp() == frozenTimestamp);
    CHECK(entities.entityPositions() == moving);

    clock->pause(false);
    runFrames(60 * 200);
    CHECK(clock->getTimestamp() > frozenTimestamp);
}

TEST_CASE("health drains while starving and recovers when rested")
{
    CBasicNeeds needs; // rested, not hungry

    // Rested and fed: health regenerates.
    CHECK(EntityVitals::healthChange(needs, 3, 4, 1) == 1);

    // Hungry for the lethal number of days: health drains.
    needs.days_without_food = 3;
    CHECK(EntityVitals::healthChange(needs, 3, 4, 1) == -4);

    // Neither starving nor rested: health holds steady.
    needs.days_without_food = 0;
    needs.sleep = CBasicNeeds::kMax; // exhausted
    CHECK(EntityVitals::healthChange(needs, 3, 4, 1) == 0);
}

TEST_CASE("CHealth clamps damage and healing")
{
    CHealth health;
    CHECK(health.isAlive());
    CHECK(health.value == CHealth::kMax);

    health.damage(30);
    CHECK(health.value == CHealth::kMax - 30);

    health.heal(1000);
    CHECK(health.value == CHealth::kMax);

    health.damage(1000);
    CHECK(health.value == 0);
    CHECK_FALSE(health.isAlive());
}

TEST_CASE("health drains under constant starvation and kills the settlement")
{
    // With no farm and no stored food, every entity misses its daily meal from
    // the first day, so each accrues hungry days and health drains once the
    // lethal streak is reached. This exercises consumeFoodDaily + applyHealth +
    // killTheDying through the real update loop.
    const std::string file = writeTempEntityConfig("starve",
        R"({ "initial_population": 8, "max_population": 40, "lifespan_years": 100, "lethal_days_without_food": 3, "food_per_person_per_day": 1 })");

    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f);
    clock->setTime(8, 0);

    float delta = 1.f;
    EntityManager entities(font, map, clock, delta, file);
    entities.seedPopulation();

    const auto health = entities.firstHealth();
    REQUIRE(health.has_value());
    CHECK(*health == CHealth::kMax);

    // 3 hungry days before health drains, then 100 health at 4/hour is 25 more
    // hours. One update per in-game hour (60 min/s scale, a 1 s delta); run
    // well past 3 days + 25 hours.
    for (int hour = 0; hour < 120; ++hour)
    {
        clock->update(1.f);
        entities.update();
    }

    CHECK(entities.deaths() >= 8);
    CHECK(entities.population() == 0);

    std::filesystem::remove(file);
}
