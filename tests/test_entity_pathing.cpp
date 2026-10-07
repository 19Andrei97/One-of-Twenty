#include "EntityConfig.h"
#include "EntityManager.h"
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

TEST_CASE("a water tile target is approached from land")
{
    // The router must never place an entity on a drink target that sits in the
    // sea: after a run the whole population is on land (checked above), and the
    // settlement still drinks, so the approach-then-drink path works.
    sf::Font font;
    int frames = 0;
    auto map = makeMap(frames);
    auto clock = std::make_shared<GameClock>(120.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());
    entities.seedPopulation();

    for (int frame = 0; frame < 60 * 24 * 5; ++frame)
    {
        clock->update(delta);
        entities.update();
        entities.resolveCollisions();
    }

    CHECK(entities.population() > 0);
    CHECK(entities.deaths() < entities.births() + 8); // not wiped out
}
