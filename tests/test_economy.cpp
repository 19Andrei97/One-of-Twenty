#include "Economy.h"
#include "EntityManager.h"
#include "GameClock.h"
#include "Goods.h"
#include "Jobs.h"
#include "MapGenerator.h"

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

// The economy helpers are pure value types (a Stock and pure functions over it),
// so most of these cases need no map, registry or clock. The end-to-end cases at
// the bottom drive the real EntityManager update loop.

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

// Drive the generator's render loop until the chunk containing `world` is
// loaded, so a tile edit (setTileColor) can land. Mirrors the streaming the game
// loop performs; returns false when no GL context is available (no DISPLAY).
bool ensureChunkLoaded(MapGenerator& map, int& frames, const sf::Vector2i& world)
{
#if defined(__linux__)
    if (std::getenv("DISPLAY") == nullptr)
        return false;
#endif

    sf::RenderTexture target;
    try
    {
        if (!target.resize({ 320, 240 }))
            return false;
    }
    catch (const sf::Exception&)
    {
        return false;
    }

    const sf::IntRect view{ { 0, 0 }, { 320, 240 } };
    for (int frame = 0; frame < 300; ++frame)
    {
        frames = frame;
        target.clear();
        map.render(view, target);
        target.display();

        for (const auto& line : map.getPositionInfo(world))
            if (line.rfind("Type:", 0) == 0)
                return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

std::string writeTempEntityConfig(const std::string& name, const std::string& economyJson)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("economy_test_" + name + ".json");
    std::ofstream out(path);
    out << R"({
        "needs": { "hunger_decay_per_hour": 3, "thirst_decay_per_hour": 5, "sleep_gain_per_hour": 2 },
        "survival": { "initial_population": 8, "max_population": 40, "lifespan_years": 65 },
        "economy": )" << economyJson << R"(,
        "decision": {
            "idle_tolerance": 3,
            "thirst": { "threshold": 0.20, "bias": 1.0 },
            "hunger": { "threshold": 0.20, "bias": 1.0 },
            "sleep":  { "threshold": 0.20, "bias": 1.0 },
            "work":   { "threshold": 0.50, "bias": 1.0 }
        }
    })";
    return path.string();
}
} // namespace

TEST_CASE("job names round-trip and unknown names are rejected")
{
    for (std::size_t i = 0; i < Jobs::kJobCount; ++i)
    {
        const auto job = static_cast<Jobs::Job>(i);
        const auto parsed = Jobs::fromString(Jobs::name(job));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == job);
    }

    CHECK_FALSE(Jobs::fromString("wizard").has_value());
    CHECK_FALSE(Jobs::fromString("").has_value());
    CHECK(Jobs::fromString("FARMER") == Jobs::Job::Farmer); // case-insensitive
}

TEST_CASE("a terrain element yields its raw good")
{
    CHECK(Goods::fromElement(Elements::forest) == Goods::Good::Wood);
    CHECK(Goods::fromElement(Elements::hill) == Goods::Good::Stone);
    CHECK(Goods::fromElement(Elements::clay) == Goods::Good::Clay);
    CHECK(Goods::fromElement(Elements::iron) == Goods::Good::Iron);
    CHECK(Goods::fromElement(Elements::silver) == Goods::Good::Silver);
}

TEST_CASE("a stock adds, takes and clamps at zero")
{
    Goods::Stock stock;
    CHECK(stock.count(Goods::Good::Wood) == 0);

    stock.add(Goods::Good::Wood, 3);
    CHECK(stock.count(Goods::Good::Wood) == 3);
    CHECK(stock.has(Goods::Good::Wood, 3));

    CHECK(stock.take(Goods::Good::Wood, 2) == 2);
    CHECK(stock.count(Goods::Good::Wood) == 1);

    // Taking more than is held returns only what existed and never goes negative.
    CHECK(stock.take(Goods::Good::Wood, 99) == 1);
    CHECK(stock.count(Goods::Good::Wood) == 0);

    // The total sums every good; rawTotal excludes crafted goods.
    stock.add(Goods::Good::Food, 4);
    stock.add(Goods::Good::Planks, 2);
    CHECK(stock.total() == 6);
    CHECK(stock.rawTotal() == 4);
}

TEST_CASE("food spoils as a percentage of what is stored")
{
    Goods::Stock stock;
    stock.add(Goods::Good::Food, 100);

    Goods::spoil(stock, 5);
    CHECK(stock.count(Goods::Good::Food) == 95);

    // A percentage that rounds to nothing loses nothing, so small stores survive.
    Goods::spoil(stock, 1);
    CHECK(stock.count(Goods::Good::Food) == 95);

    Goods::spoil(stock, 0); // disabled
    CHECK(stock.count(Goods::Good::Food) == 95);
}

TEST_CASE("a recipe consumes inputs and produces outputs, or fails cleanly")
{
    Goods::Stock stock;
    stock.add(Goods::Good::Wood, 2);

    const Goods::Recipe planks{ Goods::Good::Wood, 2, Goods::Good::Planks, 1, "test" };
    REQUIRE(Goods::applyRecipe(stock, planks));
    CHECK(stock.count(Goods::Good::Wood) == 0);
    CHECK(stock.count(Goods::Good::Planks) == 1);

    // Without the inputs nothing is spent or produced.
    const int woodBefore = stock.count(Goods::Good::Wood);
    CHECK_FALSE(Goods::applyRecipe(stock, planks));
    CHECK(stock.count(Goods::Good::Wood) == woodBefore);
    CHECK(stock.count(Goods::Good::Planks) == 1);
}

TEST_CASE("a farm grows food with no input")
{
    Goods::Stock stock;
    REQUIRE(Economy::produceOnce(stock, Economy::Building::Farm));
    CHECK(stock.count(Goods::Good::Food) == 1);
    CHECK(stock.count(Goods::Good::Wood) == 0);
}

TEST_CASE("a workshop prefers valuable inputs and falls back")
{
    Goods::Stock stock;
    stock.add(Goods::Good::Iron, 1);
    stock.add(Goods::Good::Wood, 2);

    // Iron is the more valuable input, so it is spent first (iron -> tools).
    REQUIRE(Economy::produceOnce(stock, Economy::Building::Workshop));
    CHECK(stock.count(Goods::Good::Tools) == 1);
    CHECK(stock.count(Goods::Good::Iron) == 0);
    CHECK(stock.count(Goods::Good::Wood) == 2);
    CHECK(stock.count(Goods::Good::Planks) == 0);

    // With no iron, wood -> planks.
    REQUIRE(Economy::produceOnce(stock, Economy::Building::Workshop));
    CHECK(stock.count(Goods::Good::Planks) == 1);
    CHECK(stock.count(Goods::Good::Wood) == 0);

    // Empty and unknown buildings make nothing.
    CHECK_FALSE(Economy::produceOnce(stock, Economy::Building::Workshop));
    CHECK_FALSE(Economy::produceOnce(stock, Economy::Building::None));
}

TEST_CASE("job assignment fills the largest staffing gap")
{
    Jobs::JobTargets targets{};
    targets[Jobs::index(Jobs::Job::Farmer)] = 4;
    targets[Jobs::index(Jobs::Job::Builder)] = 2;

    std::array<int, Jobs::kJobCount> current{};
    CHECK(Economy::assignJob(targets, current) == Jobs::Job::Farmer);

    current[Jobs::index(Jobs::Job::Farmer)] = 4;
    CHECK(Economy::assignJob(targets, current) == Jobs::Job::Builder);

    current[Jobs::index(Jobs::Job::Builder)] = 2;
    CHECK(Economy::assignJob(targets, current) == Jobs::Job::Idle); // fully staffed
}

TEST_CASE("job preference orders each role's favourite resources")
{
    // A farmer values forage over hill, a lumberjack only wood.
    CHECK(Goods::jobPreference(Jobs::Job::Farmer, Elements::forest) == 0);
    CHECK(Goods::jobPreference(Jobs::Job::Farmer, Elements::hill) == 1);
    CHECK(Goods::jobPreference(Jobs::Job::Lumberjack, Elements::forest) == 0);
    CHECK(Goods::jobPreference(Jobs::Job::Lumberjack, Elements::hill) == -1);

    // A miner ranks the ores rarest-first.
    CHECK(Goods::jobPreference(Jobs::Job::Miner, Elements::silver) == 0);
    CHECK(Goods::jobPreference(Jobs::Job::Miner, Elements::iron) == 1);
    CHECK(Goods::jobPreference(Jobs::Job::Miner, Elements::clay) == 2);

    // An idle entity has no preference for anything.
    CHECK(Goods::jobPreference(Jobs::Job::Idle, Elements::forest) == -1);
}

TEST_CASE("building is gated on the wood in the stores")
{
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    map->setSeed(42);
    map->setNoises();
    auto clock = std::make_shared<GameClock>(60.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());
    CHECK(entities.buildingCount() == 0);

    const int tileSize = map->getTileSize();
    const sf::Vector2i site{ 2 * tileSize, 2 * tileSize };
    if (!ensureChunkLoaded(*map, frames, site))
    {
        MESSAGE("Skipping building test: no chunk could be loaded");
        return;
    }

    // The shipped config charges wood, and a fresh settlement has none stored.
    CHECK_FALSE(entities.placeBuilding(Economy::Building::Farm, site));
    CHECK(entities.buildingCount() == 0);
}

TEST_CASE("a farm built with stock produces food each hour")
{
    const std::string file = writeTempEntityConfig("farm",
        R"({ "farm_food_per_hour": 2, "farm_wood_cost": 0, "workshop_wood_cost": 0 })");

    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    map->setSeed(42);
    map->setNoises();
    auto clock = std::make_shared<GameClock>(60.f);
    clock->setTime(8, 0);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, file);

    const int tileSize = map->getTileSize();
    const sf::Vector2i site{ 2 * tileSize, 2 * tileSize };
    if (!ensureChunkLoaded(*map, frames, site))
    {
        MESSAGE("Skipping farm production test: no chunk could be loaded");
        std::filesystem::remove(file);
        return;
    }

    // A zero wood cost lets the test place a farm without first gathering wood.
    REQUIRE(entities.placeBuilding(Economy::Building::Farm, site));
    CHECK(entities.buildingCount() == 1);

    const int foodBefore = entities.good(Goods::Good::Food);
    for (int hour = 0; hour < 5; ++hour)
    {
        for (int step = 0; step < 60; ++step)
            clock->update(delta);
        entities.update();
    }

    // Five hours at two food per hour (the farm is the only producer; the
    // founders may also forage, which only adds to the store).
    CHECK(entities.good(Goods::Good::Food) >= foodBefore + 10);
    CHECK(entities.foodProduced() == 10);

    std::filesystem::remove(file);
}

TEST_CASE("a placed building cannot share a tile with another")
{
    const std::string file = writeTempEntityConfig("unique_tile",
        R"({ "farm_wood_cost": 0, "workshop_wood_cost": 0 })");

    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    map->setSeed(42);
    map->setNoises();
    auto clock = std::make_shared<GameClock>(60.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, file);

    const int tileSize = map->getTileSize();
    const sf::Vector2i site{ 2 * tileSize, 2 * tileSize };
    if (!ensureChunkLoaded(*map, frames, site))
    {
        MESSAGE("Skipping building-tile test: no chunk could be loaded");
        std::filesystem::remove(file);
        return;
    }

    REQUIRE(entities.placeBuilding(Economy::Building::Farm, site));
    CHECK(entities.buildingCount() == 1);
    CHECK_FALSE(entities.placeBuilding(Economy::Building::Workshop, site)); // occupied
    CHECK(entities.buildingCount() == 1);

    std::filesystem::remove(file);
}

TEST_CASE("every spawned entity gets a role, and the settlement staffs its jobs")
{
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f);
    clock->setTime(8, 0);

    // One entity per job keeps the staffing arithmetic easy to check.
    const std::string file = writeTempEntityConfig("staffing",
        R"({ "jobs": { "Farmer": 1, "Lumberjack": 1, "Miner": 1, "Builder": 1 } })");

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, file);
    entities.seedPopulation();
    REQUIRE(entities.population() == 8);

    // A couple of update steps let the shortage rule redistribute idle founders.
    for (int i = 0; i < 4; ++i)
        entities.update();

    const auto counts = entities.jobCounts();
    CHECK(counts[Jobs::index(Jobs::Job::Farmer)] >= 1);
    CHECK(counts[Jobs::index(Jobs::Job::Farmer)] +
          counts[Jobs::index(Jobs::Job::Lumberjack)] +
          counts[Jobs::index(Jobs::Job::Miner)] +
          counts[Jobs::index(Jobs::Job::Builder)] == 8);

    // The first entity always has a concrete job, never the default Idle.
    CHECK(entities.firstJob() != Jobs::Job::Idle);

    std::filesystem::remove(file);
}

TEST_CASE("an entity config without the economy block still loads with defaults")
{
    // Backward compatibility: an older file has no "economy" or "entity_types"
    // block. The loader must fall back to shipped defaults rather than throwing.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "economy_test_legacy.json";
    {
        std::ofstream out(path);
        out << R"({
            "needs": { "hunger_decay_per_hour": 3, "thirst_decay_per_hour": 5, "sleep_gain_per_hour": 2 },
            "decision": {
                "idle_tolerance": 3,
                "thirst": { "threshold": 0.20, "bias": 1.0 },
                "hunger": { "threshold": 0.20, "bias": 1.0 },
                "sleep":  { "threshold": 0.20, "bias": 1.0 }
            }
        })";
    }

    const EntityConfig cfg = loadEntityConfig(path.string());

    // Food production is staffed by default so a settlement can start.
    CHECK(cfg.economy.job_targets[Jobs::index(Jobs::Job::Farmer)] > 0);
    CHECK(cfg.economy.farm_wood_cost > 0);
    CHECK(cfg.economy.food_spoilage_percent_per_day >= 0);

    // Unlisted entity types fall back to a default role instead of a hard error.
    CHECK_FALSE(cfg.has_type_job[static_cast<std::size_t>(EntityType::Human_Generic)]);

    std::filesystem::remove(path);
}

TEST_CASE("the shipped config maps entity types to jobs")
{
    const EntityConfig cfg = loadEntityConfig(entityConfigPath());

    CHECK(cfg.has_type_job[static_cast<std::size_t>(EntityType::Human_Generic)]);
    CHECK(cfg.type_jobs[static_cast<std::size_t>(EntityType::Human_Generic)] == Jobs::Job::Builder);
    CHECK(cfg.type_jobs[static_cast<std::size_t>(EntityType::Human_Farmer)] == Jobs::Job::Farmer);
    CHECK(cfg.type_jobs[static_cast<std::size_t>(EntityType::Human_Lumberjack)] == Jobs::Job::Lumberjack);
    CHECK(cfg.type_jobs[static_cast<std::size_t>(EntityType::Animal_Dog)] == Jobs::Job::Idle);

    CHECK(cfg.economy.job_targets[Jobs::index(Jobs::Job::Farmer)] == 4);
    CHECK(cfg.economy.job_targets[Jobs::index(Jobs::Job::Lumberjack)] == 2);
}
