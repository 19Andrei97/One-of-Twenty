#include "Appearance.h"
#include "EntityConfig.h"
#include "EntityManager.h"
#include "GameClock.h"
#include "Jobs.h"
#include "MapGenerator.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

// The appearance helpers are pure value types, so they are tested directly; the
// loader and the repaint-on-reassign path are exercised through EntityConfig and
// the real EntityManager.

namespace
{
std::string appearanceEntityConfigPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/entity_data.json";
#else
    return "config/entity_data.json";
#endif
}

std::string writeTempAppearanceConfig(const std::string& name, const std::string& appearanceJson)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("appearance_test_" + name + ".json");
    std::ofstream out(path);
    out << R"({
        "needs": { "sleep_gain_per_hour": 2 },
        "appearance": )" << appearanceJson << R"(,
        "decision": {
            "idle_tolerance": 3,
            "sleep":  { "threshold": 0.20, "bias": 1.0 },
            "work":   { "threshold": 0.50, "bias": 1.0 }
        }
    })";
    return path.string();
}
} // namespace

TEST_CASE("shape names round-trip and unknown names are rejected")
{
    for (std::size_t i = 0; i < Appearance::kShapeCount; ++i)
    {
        const auto shape = static_cast<Appearance::Shape>(i);
        const auto parsed = Appearance::shapeFromString(Appearance::shapeName(shape));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == shape);
    }

    CHECK_FALSE(Appearance::shapeFromString("hexagon").has_value());
    // Parsing is case-insensitive, like the job and logger parsers.
    CHECK(Appearance::shapeFromString("SQUARE").value() == Appearance::Shape::Square);
}

TEST_CASE("a look builds a centred shape of the right fill and point count")
{
    Appearance::Look look;
    look.shape = Appearance::Shape::Square;
    look.fill = sf::Color{ 10, 20, 30 };
    look.radius = 6.f;

    const sf::CircleShape circle = look.makeShape();
    CHECK(circle.getPointCount() == 4);
    CHECK(circle.getFillColor() == sf::Color(10, 20, 30));
    CHECK(circle.getOrigin() == sf::Vector2f(6.f, 6.f));
}

TEST_CASE("a triangle look has three points, a diamond four")
{
    Appearance::Look triangle;
    triangle.shape = Appearance::Shape::Triangle;
    CHECK(triangle.makeShape().getPointCount() == 3);

    Appearance::Look diamond;
    diamond.shape = Appearance::Shape::Diamond;
    CHECK(diamond.makeShape().getPointCount() == 4);
}

TEST_CASE("the default look matches the pre-JSON appearance")
{
    // Backward compatibility: with no appearance configured an entity is still a
    // white 10-unit circle, exactly as before the block existed.
    const Appearance::Look look;
    const sf::CircleShape circle = look.makeShape();
    CHECK(circle.getFillColor() == sf::Color::White);
    CHECK(circle.getRadius() == doctest::Approx(10.f));
    CHECK(circle.getPointCount() == 4);
}

TEST_CASE("loadEntityConfig reads the shipped appearance block")
{
    const EntityConfig cfg = loadEntityConfig(appearanceEntityConfigPath());

    // Per-profession looks are configured and distinct, so jobs can be told
    // apart on screen.
    CHECK(cfg.has_job_look[Jobs::index(Jobs::Job::Farmer)]);
    CHECK(cfg.job_looks[Jobs::index(Jobs::Job::Farmer)].fill == sf::Color(90, 200, 90));
    CHECK(cfg.job_looks[Jobs::index(Jobs::Job::Lumberjack)].shape == Appearance::Shape::Square);
    CHECK(cfg.job_looks[Jobs::index(Jobs::Job::Explorer)].shape == Appearance::Shape::Triangle);
    CHECK_FALSE(cfg.has_job_look[Jobs::index(Jobs::Job::Idle)]);

    // A per-type look (the cat) is honored too.
    CHECK(cfg.type_looks[static_cast<std::size_t>(EntityType::Animal_Cat)].shape
          == Appearance::Shape::Triangle);
}

TEST_CASE("an appearance entry accepts a named color and a 4-component color")
{
    const std::string file = writeTempAppearanceConfig("colors", R"({
        "jobs": {
            "farmer": { "color": "red" },
            "miner":  { "color": [ 1, 2, 3, 128 ] }
        }
    })");

    const EntityConfig cfg = loadEntityConfig(file);

    CHECK(cfg.job_looks[Jobs::index(Jobs::Job::Farmer)].fill == sf::Color::Red);
    CHECK(cfg.job_looks[Jobs::index(Jobs::Job::Miner)].fill == sf::Color(1, 2, 3, 128));

    std::filesystem::remove(file);
}

TEST_CASE("an entity config without the appearance block still loads with defaults")
{
    // Older configs have no "appearance" block; the loader must not throw and
    // every look must stay at the default.
    const std::string file = writeTempAppearanceConfig("empty", R"({})");
    const EntityConfig cfg = loadEntityConfig(file);

    for (std::size_t i = 0; i < Jobs::kJobCount; ++i)
        CHECK_FALSE(cfg.has_job_look[i]);
    CHECK(cfg.job_looks[Jobs::index(Jobs::Job::Farmer)].fill == sf::Color::White);

    std::filesystem::remove(file);
}

TEST_CASE("reassigning a job repaints the entity from the config")
{
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(
        frames,
#ifdef ONE_OF_TWENTY_SOURCE_DIR
        std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json"
#else
        "config/map_data.json"
#endif
    );
    auto clock = std::make_shared<GameClock>(60.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, appearanceEntityConfigPath());

    // A spawned generic human defaults to the builder look (a gold square).
    const sf::Vector2i spawn = entities.findHabitableSpawn();
    const entt::entity entity = entities.addEntity(EntityType::Human_Generic, spawn);

    const auto* shape = entities.registry().try_get<CShape>(entity);
    REQUIRE(shape != nullptr);
    CHECK(shape->circle.getFillColor() == sf::Color(220, 180, 90));
    CHECK(shape->circle.getPointCount() == 4);

    // The cat's per-type look is a triangle; a cat spawns idle so it uses it.
    const entt::entity cat = entities.addEntity(EntityType::Animal_Cat, spawn);
    const auto* cat_shape = entities.registry().try_get<CShape>(cat);
    REQUIRE(cat_shape != nullptr);
    CHECK(cat_shape->circle.getPointCount() == 3);
    CHECK(cat_shape->circle.getFillColor() == sf::Color(180, 180, 205));
}
