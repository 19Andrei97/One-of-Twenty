#include "EntityConfig.h"
#include "EntityManager.h"
#include "Random.h"
#include "Resources.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>

// End-to-end movement/collision cases: they run the real update loop over a real
// generated map, so they exercise the router and the collision pass together.

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

std::shared_ptr<MapGenerator> makeMap(int& frames)
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
#else
    return std::make_shared<MapGenerator>(frames, "config/map_data.json");
#endif
}
} // namespace

TEST_CASE("entities never stand in the ocean during a run")
{
    // Mirrors Scene_Play: update the manager, then resolve collisions each frame.
    // Seeded so the wander is repeatable; the invariant under test is "on land",
    // independent of how many entities survive.
    Random::mt.seed(20241007);

    sf::Font font;
    int frames = 0;
    auto map = makeMap(frames);
    auto clock = std::make_shared<GameClock>(120.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());
    entities.seedPopulation();

    const int tileSize = map->getTileSize();
    for (int frame = 0; frame < 60 * 24 * 3; ++frame)
    {
        clock->update(delta);
        entities.update();
        entities.resolveCollisions();
    }

    REQUIRE(entities.population() > 0);
    for (const auto& pos : entities.entityPositions())
    {
        const auto tile = CoordMath::worldToTile(pos, tileSize);
        const auto element = map->getElementAtWorld(CoordMath::tileToWorld(tile, tileSize));
        CHECK_FALSE(Resources::isOcean(element));
    }
}

TEST_CASE("collision separates entities that share a position")
{
    sf::Font font;
    int frames = 0;
    auto map = makeMap(frames);
    auto clock = std::make_shared<GameClock>(120.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());

    const sf::Vector2i spawn = entities.findHabitableSpawn();
    entities.addEntity(EntityType::Human_Generic, spawn);
    entities.addEntity(EntityType::Human_Generic, spawn);
    entities.addEntity(EntityType::Human_Generic, spawn);

    entities.resolveCollisions();

    const auto positions = entities.entityPositions();
    REQUIRE(positions.size() == 3);
    for (std::size_t i = 0; i < positions.size(); ++i)
        for (std::size_t j = i + 1; j < positions.size(); ++j)
        {
            const sf::Vector2i d = positions[i] - positions[j];
            CHECK((d.x * d.x + d.y * d.y) >= 12 * 12);
        }
}

TEST_CASE("the settlement drinks: thirst is replenished during a run")
{
    // Directly verifies the water-target approach: over a few days some entity's
    // thirst must reset upward (a drink), which only happens if it reached a
    // shore tile next to the sea. Sampling hourly catches the reset because a
    // drink fills the need and it then decays slowly.
    //
    // Randomness is clock-seeded, so seed it here to keep the run repeatable.
    Random::mt.seed(20241007);

    sf::Font font;
    int frames = 0;
    auto map = makeMap(frames);
    auto clock = std::make_shared<GameClock>(120.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());
    entities.seedPopulation();

    bool drank = false;
    int previousThirst = -1;
    for (int hour = 0; hour < 24 * 3 && !drank; ++hour)
    {
        for (int step = 0; step < 60; ++step)
        {
            clock->update(delta);
            entities.update();
            entities.resolveCollisions();
        }

        const auto needs = entities.firstNeeds();
        if (needs)
        {
            if (previousThirst >= 0 && needs->thirst > previousThirst)
                drank = true;
            previousThirst = needs->thirst;
        }
        else
        {
            previousThirst = -1; // first entity died; resample the next one
        }
    }

    CHECK(drank);
    for (const auto& pos : entities.entityPositions())
    {
        const auto tile = CoordMath::worldToTile(pos, map->getTileSize());
        CHECK_FALSE(Resources::isOcean(map->getElementAtWorld(CoordMath::tileToWorld(tile, map->getTileSize()))));
    }
}
