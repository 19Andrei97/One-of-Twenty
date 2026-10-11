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
    // `near`/`far` are legacy macros in windows.h (SFML pulls it in), which would
    // mangle these declarations on MSVC, so use MSVC-safe names.
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

TEST_CASE("the camera stays inside the finite world")
{
    // The world is a finite square, so the camera must not pan past its edge.
    // A camera that starts near the top-left corner and pans hard out of the map
    // is clamped so its view keeps showing the map.
    Camera camera(1280, 720);
    const sf::IntRect world{ { -1000, -1000 }, { 2000, 2000 } };
    camera.setWorldBounds(world);

    for (int i = 0; i < 200; ++i)
        camera.move(-1000.f, -1000.f);

    const sf::View& view = camera.getCamera();
    const float halfW = view.getSize().x / 2.f;
    const float halfH = view.getSize().y / 2.f;
    CHECK(view.getCenter().x - halfW >= static_cast<float>(world.position.x) - 1.f);
    CHECK(view.getCenter().y - halfH >= static_cast<float>(world.position.y) - 1.f);

    // Panning the other way clamps to the far edge.
    for (int i = 0; i < 400; ++i)
        camera.move(1000.f, 1000.f);
    const sf::View& far = camera.getCamera();
    CHECK(far.getCenter().x + halfW <= static_cast<float>(world.position.x + world.size.x) + 1.f);
    CHECK(far.getCenter().y + halfH <= static_cast<float>(world.position.y + world.size.y) + 1.f);
}

TEST_CASE("a world-size button resizes the map and confines the camera")
{
    // The size buttons are registered by the HUD and act on the scene's map; the
    // scene must also pull the camera back inside the new world. Clicking the
    // buttons drives the real callback chain (the HUD camera is 1280x720 with its
    // origin at 0,0, so the button positions from the config are the click points).
    sf::Font font;
    REQUIRE(font.openFromFile(fontPath()));

    Scene_Play scene(nullptr, font, playConfig());
    auto map = scene.getMap();
    REQUIRE(map->getMapSize() == MapSize::Normal);

    Hud* hud = scene.getHud();
    REQUIRE(hud != nullptr);
    REQUIRE(hud->hasButtonCallback("map_size_enormous"));
    REQUIRE(hud->hasButtonCallback("map_size_normal"));

    const auto click = [&](const sf::Vector2f& at) {
        hud->input(sf::Event::MouseButtonPressed{ sf::Mouse::Button::Left, { static_cast<int>(at.x), static_cast<int>(at.y) } }, at);
    };

    // Grow the world: the map follows and the camera stays valid.
    click({ 250.f, 275.f }); // over the Enormous button
    CHECK(map->getMapSize() == MapSize::Enormous);
    CHECK(map->getWorldTileBounds() == worldTileBounds(MapSize::Enormous, 32));

    // Push the camera hard against the Enormous edge, then shrink back to Normal:
    // the camera must be pulled inside the (much smaller) new world.
    for (int i = 0; i < 200; ++i)
        scene.getCamera()->move(1e6f, 1e6f);

    click({ 50.f, 275.f }); // over the Normal button
    CHECK(map->getMapSize() == MapSize::Normal);
    CHECK(map->getWorldTileBounds() == worldTileBounds(MapSize::Normal, 32));

    const sf::View& view = scene.getCamera()->getCamera();
    const sf::IntRect bounds = map->getWorldPixelBounds();
    CHECK(view.getCenter().x + view.getSize().x / 2.f <= static_cast<float>(bounds.position.x + bounds.size.x) + 1.f);
    CHECK(view.getCenter().y + view.getSize().y / 2.f <= static_cast<float>(bounds.position.y + bounds.size.y) + 1.f);
}
