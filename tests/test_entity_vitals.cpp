#include "EntityConfig.h"
#include "EntityManager.h"
#include "EntityVitals.h"

#include <doctest/doctest.h>

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
} // namespace

TEST_CASE("isStarving trips at or below the lethal threshold")
{
    CBasicNeeds needs; // thirst/hunger 100
    CHECK_FALSE(EntityVitals::isStarving(needs, 0));

    needs.thirst = 1;
    CHECK_FALSE(EntityVitals::isStarving(needs, 0));

    needs.thirst = 0;
    CHECK(EntityVitals::isStarving(needs, 0));

    needs.thirst = 100;
    needs.hunger = 0;
    CHECK(EntityVitals::isStarving(needs, 0));

    // A non-zero threshold is inclusive.
    needs.hunger = 5;
    CHECK(EntityVitals::isStarving(needs, 5));
    CHECK_FALSE(EntityVitals::isStarving(needs, 4));
}

TEST_CASE("isAged reports an exhausted lifespan")
{
    CHECK_FALSE(EntityVitals::isAged(CLifespan{ 10 }));
    CHECK(EntityVitals::isAged(CLifespan{ 0 }));
}

TEST_CASE("comfort is one when rested and fed, zero when any need is dire")
{
    CBasicNeeds needs; // thirst/hunger 100, sleep 0
    CHECK(EntityVitals::comfort(needs) == doctest::Approx(1.f));

    needs.hunger = 50;
    CHECK(EntityVitals::comfort(needs) == doctest::Approx(0.5f));

    // Sleep is a fatigue counter: 50 sleep is 50 rest, so it caps comfort at 0.5.
    needs.hunger = 100;
    needs.sleep = 50;
    CHECK(EntityVitals::comfort(needs) == doctest::Approx(0.5f));

    needs.sleep = 100;
    CHECK(EntityVitals::comfort(needs) == doctest::Approx(0.f));

    // The worst need governs.
    needs.sleep = 0;
    needs.thirst = 20;
    CHECK(EntityVitals::comfort(needs) == doctest::Approx(0.2f));
}

TEST_CASE("loadEntityConfig reads the survival block")
{
    const EntityConfig cfg = loadEntityConfig(entityConfigPath());

    CHECK(cfg.survival.initial_population == 8);
    CHECK(cfg.survival.max_population == 40);
    CHECK(cfg.survival.lethal_threshold == 0);
    CHECK(cfg.survival.lifespan_hours == 720);
    CHECK(cfg.survival.birth_comfort == doctest::Approx(0.8f));
    CHECK(cfg.survival.birth_cooldown_hours == 48);
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
    // End to end through the real update loop: seeded entities age one hour per
    // in-game hour, so after more than `lifespan_hours` every founder must have
    // died. Births may add more, but cannot keep the founders alive.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());

    CHECK(entities.population() == 0);

    entities.seedPopulation();
    CHECK(entities.population() == 8);
    CHECK(entities.population() == entities.entityCount());
    CHECK(entities.maxPopulation() == 40);
    CHECK(entities.lifespanHours() == 720);

    const auto advanceHour = [&]()
    {
        for (int step = 0; step < 60; ++step)
            clock->update(delta);
        entities.update();
    };

    for (int hour = 0; hour < 760; ++hour)
        advanceHour();

    CHECK(entities.deaths() >= 8);
    CHECK(entities.population() <= entities.maxPopulation());
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

TEST_CASE("the seeded settlement has water and food within vision")
{
    // The founders spawn together on a habitable site, so the whole population
    // can drink and forage without leaving the neighbourhood. The world origin is
    // inland on most seeds, so seeding there would starve the settlement.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");

    float delta = 1.f / 60.f;
    auto clock = std::make_shared<GameClock>(120.f);
    EntityManager entities(font, map, clock, delta, entityConfigPath());

    const sf::Vector2i spawn = entities.findHabitableSpawn();
    const auto nearby = map->getResourcesWithinBoundary(spawn, CVision{}.radius);
    const bool hasWater = std::any_of(nearby.begin(), nearby.end(),
            [](const auto& entry) { return Resources::isWater(entry.first); });
    const bool hasFood = std::any_of(nearby.begin(), nearby.end(),
            [](const auto& entry) { return Resources::isFood(entry.first); });
    CHECK(hasWater);
    CHECK(hasFood);

    entities.seedPopulation();
    CHECK(entities.population() == 8);
}

TEST_CASE("the seeded population survives several simulated days")
{
    // End to end through the real update loop: with water and food in reach the
    // settlement neither goes extinct nor grows without bound, and it reproduces.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(120.f);
    clock->setTime(8, 0); // mirror the scene, which starts mid-morning

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());
    entities.seedPopulation();

    for (int frame = 0; frame < 60 * 24 * 4; ++frame)
    {
        clock->update(delta);
        entities.update();
    }

    CHECK(entities.population() > 0);
    CHECK(entities.population() <= entities.maxPopulation());
    CHECK(entities.births() > 0);
}
