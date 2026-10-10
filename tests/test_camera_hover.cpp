#include "EntityManager.h"
#include "GameClock.h"
#include "MapGenerator.h"
#include "Scene_Play.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// The hover readout and the camera-while-paused rule are behaviours of the real
// entity manager and scene, so these drive the real objects. The hover cases only
// need the manager; the camera case builds a full Scene_Play (no window: its
// update path never touches the render target).

namespace
{
std::string fontPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/fonts/arial.ttf";
#else
    return "fonts/arial.ttf";
#endif
}

std::string configPath(const std::string& file)
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/" + file;
#else
    return "config/" + file;
#endif
}

nlohmann::json playConfig()
{
    nlohmann::json cfg;
    cfg["window"]["width"] = 1280;
    cfg["window"]["height"] = 720;
    cfg["map"]["file"] = configPath("map_data.json");
    cfg["hud"]["file"] = configPath("hud_menu_data.json");
    cfg["entity"]["file"] = configPath("entity_data.json");
    cfg["buildings"]["file"] = configPath("buildings.json");
    cfg["time"]["speed_index"] = 1;
    return cfg;
}
} // namespace

TEST_CASE("the hover readout resolves the entity under the pointer")
{
    // The per-entity readout used to be drawn over every entity. Now the manager
    // answers "which entity is under this world position?", so the scene can draw
    // a box for exactly that one. Two entities are placed apart so a point between
    // them resolves to neither, and a point on a body resolves to it.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, configPath("map_data.json"));
    auto clock = std::make_shared<GameClock>(120.f);
    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, configPath("entity_data.json"));

    const sf::Vector2i spawn = entities.findHabitableSpawn();
    // `near`/`far` are macros in the Windows SDK headers (they expand to nothing),
    // so the identifiers must not be those exact words or MSVC fails to compile.
    const entt::entity near_entity = entities.addEntity(EntityType::Human_Generic, spawn);
    const entt::entity far_entity = entities.addEntity(EntityType::Human_Generic, spawn + sf::Vector2i{ 200, 200 });

    CHECK(entities.entityAtWorld(spawn) == near_entity);
    CHECK(entities.entityAtWorld(spawn + sf::Vector2i{ 2, 0 }) == near_entity);
    CHECK(entities.entityAtWorld(spawn + sf::Vector2i{ 200, 200 }) == far_entity);
    CHECK_FALSE(entities.entityAtWorld(spawn + sf::Vector2i{ 100, 100 }).has_value());
}

TEST_CASE("the camera pans while the simulation is paused")
{
    // A paused clock advances no in-game minutes, so time-based entity movement
    // stops; the camera is view-only and must keep responding to the keys. The
    // entity positions are checked too, so "paused" still means the world is
    // frozen even though the view moves.
    sf::Font font;
    REQUIRE(font.openFromFile(fontPath()));

    Scene_Play scene(nullptr, font, playConfig());
    scene.setPaused(true);
    REQUIRE(scene.isPaused());
    REQUIRE(scene.getGameClock()->isPaused());

    const sf::View before = scene.getCamera()->getCamera();
    const auto before_entities = scene.getEntityManager()->entityPositions();
    const std::int64_t before_minutes = scene.getGameClock()->getTimestamp();

    sf::Event::KeyPressed w{
        sf::Keyboard::Key::W, sf::Keyboard::Scancode::W, false, false, false, false
    };
    scene.sUserInput(w);
    scene.update(0.5f);
    scene.sUserInput(sf::Event::KeyReleased{
        sf::Keyboard::Key::W, sf::Keyboard::Scancode::W, false, false, false, false });

    // 500 px/s for 0.5 s: the view centre moves up by ~250 world units.
    const float moved = before.getCenter().y - scene.getCamera()->getCamera().getCenter().y;
    CHECK(moved > 200.f);
    CHECK(moved < 300.f);

    // The world stayed frozen: no in-game minute passed and no entity moved.
    CHECK(scene.getGameClock()->getTimestamp() == before_minutes);
    CHECK(scene.getEntityManager()->entityPositions() == before_entities);
}
