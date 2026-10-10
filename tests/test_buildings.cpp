#include "Buildings.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

// Buildings is a pure value type (a parsed catalog plus free helpers), so it is
// unit-tested directly, without a map, renderer or EntityManager.

namespace
{
std::string writeTempBuildings(const std::string& tag, const std::string& body)
{
    const std::string path = "test_buildings_" + tag + ".json";
    std::ofstream out(path);
    out << body;
    out.close();
    return path;
}

std::string shippedCatalogPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/buildings.json";
#else
    return "config/buildings.json";
#endif
}
} // namespace

TEST_CASE("a building catalog parses cost, recipes and settlement tuning")
{
    const std::string file = writeTempBuildings("parse", R"({
        "settlement": { "max_concurrent_sites": 2, "build_radius_tiles": 8, "road_cadence_hours": 12 },
        "buildings": [
            {
                "id": "hut", "name": "Hut", "element": "house",
                "color": [10, 20, 30],
                "cost": { "wood": 4, "stone": 1 },
                "build_hours": 3,
                "population_capacity": 2,
                "priority": 7
            },
            {
                "id": "mill", "name": "Mill", "element": "farm",
                "recipes": [ { "input": "wood", "input_amount": 1, "output": "food", "output_amount": 5, "label": "mill" } ]
            }
        ]
    })");

    const Buildings::Catalog catalog = Buildings::loadCatalog(file);
    REQUIRE(catalog.all().size() == 2);

    const Buildings::Def* hut = catalog.byId("hut");
    REQUIRE(hut != nullptr);
    CHECK(hut->name == "Hut");
    CHECK(hut->element == Elements::house);
    CHECK(hut->color == sf::Color(10, 20, 30));
    CHECK(hut->build_hours == 3);
    CHECK(hut->population_capacity == 2);
    CHECK(hut->priority == 7);
    REQUIRE(hut->costs.size() == 2);
    // Costs come out of the JSON object in key order, so match by good rather
    // than relying on the order they were written.
    int wood_cost = -1;
    int stone_cost = -1;
    for (const auto& cost : hut->costs)
    {
        if (cost.good == Goods::Good::Wood)  wood_cost = cost.amount;
        if (cost.good == Goods::Good::Stone) stone_cost = cost.amount;
    }
    CHECK(wood_cost == 4);
    CHECK(stone_cost == 1);

    const Buildings::Def* mill = catalog.byId("mill");
    REQUIRE(mill != nullptr);
    REQUIRE(mill->recipes.size() == 1);
    CHECK(mill->recipes[0].input_good == Goods::Good::Wood);
    CHECK(mill->recipes[0].input_amount == 1);
    CHECK(mill->recipes[0].output_good == Goods::Good::Food);
    CHECK(mill->recipes[0].output_amount == 5);

    // Element lookup mirrors id lookup, for rendering and tile cost.
    CHECK(catalog.byElement(Elements::house) == hut);
    CHECK(catalog.byElement(Elements::mountain) == nullptr);

    const Buildings::Settlement settlement = Buildings::loadSettlement(file);
    CHECK(settlement.max_concurrent_sites == 2);
    CHECK(settlement.build_radius_tiles == 8);
    CHECK(settlement.road_cadence_hours == 12);

    std::filesystem::remove(file);
}

TEST_CASE("an unknown element or good name fails loudly")
{
    const std::string bad_element = writeTempBuildings("bad_element", R"({
        "buildings": [ { "id": "x", "element": "not_a_tile" } ]
    })");
    CHECK_THROWS_AS((void)Buildings::loadCatalog(bad_element), std::runtime_error);
    std::filesystem::remove(bad_element);

    const std::string bad_good = writeTempBuildings("bad_good", R"({
        "buildings": [ { "id": "x", "element": "house", "cost": { "unobtainium": 1 } } ]
    })");
    CHECK_THROWS_AS((void)Buildings::loadCatalog(bad_good), std::runtime_error);
    std::filesystem::remove(bad_good);
}

TEST_CASE("affordable and spend move the exact cost in and out of the stock")
{
    const std::string file = writeTempBuildings("spend", R"({
        "buildings": [ { "id": "hut", "element": "house", "cost": { "wood": 4, "stone": 1 } } ]
    })");
    const Buildings::Catalog catalog = Buildings::loadCatalog(file);
    const Buildings::Def* hut = catalog.byId("hut");
    REQUIRE(hut != nullptr);

    Goods::Stock stock;
    CHECK_FALSE(Buildings::affordable(stock, *hut)); // nothing stored

    stock.add(Goods::Good::Wood, 4);
    CHECK_FALSE(Buildings::affordable(stock, *hut)); // stone still short
    stock.add(Goods::Good::Stone, 1);
    CHECK(Buildings::affordable(stock, *hut));

    Buildings::spend(stock, *hut);
    CHECK(stock.count(Goods::Good::Wood) == 0);
    CHECK(stock.count(Goods::Good::Stone) == 0);
    CHECK_FALSE(Buildings::affordable(stock, *hut));

    std::filesystem::remove(file);
}

TEST_CASE("produceOnce applies the first recipe whose inputs are available")
{
    const std::string file = writeTempBuildings("recipes", R"({
        "buildings": [
            {
                "id": "workshop", "element": "workshop",
                "recipes": [
                    { "input": "iron", "input_amount": 1, "output": "tools", "output_amount": 1 },
                    { "input": "wood", "input_amount": 2, "output": "planks", "output_amount": 1 }
                ]
            }
        ]
    })");
    const Buildings::Catalog catalog = Buildings::loadCatalog(file);
    const Buildings::Def* workshop = catalog.byId("workshop");
    REQUIRE(workshop != nullptr);

    // Only wood: the first recipe (iron) is skipped, the second fires.
    Goods::Stock stock;
    stock.add(Goods::Good::Wood, 2);
    CHECK(Buildings::produceOnce(stock, *workshop));
    CHECK(stock.count(Goods::Good::Wood) == 0);
    CHECK(stock.count(Goods::Good::Planks) == 1);

    // Nothing available: the building idles an hour instead of producing.
    Goods::Stock empty;
    CHECK_FALSE(Buildings::produceOnce(empty, *workshop));

    std::filesystem::remove(file);
}

TEST_CASE("a road's walk cost overrides the terrain fallback")
{
    const std::string file = writeTempBuildings("walk", R"({
        "buildings": [
            { "id": "road", "element": "road", "walk_cost": 1.5 },
            { "id": "house", "element": "house" }
        ]
    })");
    const Buildings::Catalog catalog = Buildings::loadCatalog(file);

    // The catalog overrides the element's default movement cost.
    CHECK(Buildings::walkCost(Elements::road, catalog) == doctest::Approx(1.5f));
    // A building with no override falls back to MoveCost for its element.
    CHECK(Buildings::walkCost(Elements::house, catalog) == doctest::Approx(MoveCost::moveCost(Elements::house)));
    // An element absent from the catalog also falls back.
    CHECK(Buildings::walkCost(Elements::forest, catalog) == doctest::Approx(MoveCost::moveCost(Elements::forest)));

    std::filesystem::remove(file);
}

TEST_CASE("defaultBuildable rejects sand and water but allows dry land")
{
    // Every building is placed on dry land only, so the beach and the sea are
    // never built on. This is the base gate the catalog's own allow-list narrows.
    CHECK_FALSE(Buildings::defaultBuildable(Elements::sand));
    CHECK_FALSE(Buildings::defaultBuildable(Elements::ocean));
    CHECK_FALSE(Buildings::defaultBuildable(Elements::deep_ocean));
    CHECK_FALSE(Buildings::defaultBuildable(Elements::very_deep_ocean));
    CHECK_FALSE(Buildings::defaultBuildable(Elements::lake));

    CHECK(Buildings::defaultBuildable(Elements::hill));
    CHECK(Buildings::defaultBuildable(Elements::forest));
    CHECK(Buildings::defaultBuildable(Elements::mountain));
    CHECK(Buildings::defaultBuildable(Elements::snow));
    CHECK(Buildings::defaultBuildable(Elements::clay));
}

TEST_CASE("housesWanted raises a house once the housing is full")
{
    // No shortfall: no houses wanted, however many people.
    CHECK(Buildings::housesWanted(0, 120, 30) == 0);
    CHECK(Buildings::housesWanted(120, 120, 30) == 0);
    CHECK(Buildings::housesWanted(50, 120, 30) == 0);

    // A shortfall wants enough houses to cover it plus one to spare (the house is
    // full, so another goes up).
    CHECK(Buildings::housesWanted(121, 120, 30) == 2); // 1 short, one more house
    CHECK(Buildings::housesWanted(149, 120, 30) == 2); // 29 short, still one more
    CHECK(Buildings::housesWanted(150, 120, 30) == 2); // 30 short, one covers it
    CHECK(Buildings::housesWanted(420, 120, 30) == 11); // 300 short

    // A non-positive per-house size never demands houses (guards a bad config).
    CHECK(Buildings::housesWanted(200, 0, 0) == 0);
}

TEST_CASE("needsFoodProducer sizes the food to the housing")
{
    // Fully covered: no more producers, whether the reach is exact or spare.
    CHECK_FALSE(Buildings::needsFoodProducer(360, 400));
    CHECK_FALSE(Buildings::needsFoodProducer(60, 60));

    // Short of the people the settlement could house: one more producer is wanted.
    CHECK(Buildings::needsFoodProducer(360, 340));
    CHECK(Buildings::needsFoodProducer(60, 0));
}

TEST_CASE("a comfortable store suppresses another food producer")
{
    // 60 people eat 1 each a day; a 3-day reserve is 180. A store that covers it
    // makes a new farm redundant even though the standing reach falls short.
    CHECK_FALSE(Buildings::needsFoodProducer(60, 0, 180, 1, 3));
    CHECK_FALSE(Buildings::needsFoodProducer(60, 0, 500, 1, 3));

    // Short of the reserve: the head-count shortfall still wants a producer.
    CHECK(Buildings::needsFoodProducer(60, 0, 179, 1, 3));

    // The gate only opens the door; a settlement whose authors are already covered
    // still wants none, however small the store.
    CHECK_FALSE(Buildings::needsFoodProducer(60, 60, 0, 1, 3));

    // A disabled reserve or a zero appetite falls back to the plain head-count
    // test, so an old config is unchanged.
    CHECK(Buildings::needsFoodProducer(60, 0, 10000, 1, 0));
    CHECK(Buildings::needsFoodProducer(60, 0, 10000, 0, 3));
}

TEST_CASE("a def's allowed_terrain narrows where it may stand")
{
    const std::string file = writeTempBuildings("terrain", R"({
        "buildings": [
            { "id": "hut",  "element": "house" },
            { "id": "mine", "element": "workshop", "allowed_terrain": [ "mountain" ] },
            { "id": "quarry", "element": "workshop", "allowed_terrain": [ "hill", "mountain" ] }
        ]
    })");
    const Buildings::Catalog catalog = Buildings::loadCatalog(file);

    // No allow-list: any element is acceptable (the base gate still applies).
    const Buildings::Def* hut = catalog.byId("hut");
    REQUIRE(hut != nullptr);
    CHECK(hut->allowed_terrain.empty());
    CHECK(hut->allowsTerrain(Elements::forest));
    CHECK(hut->allowsTerrain(Elements::mountain));

    // A single-tile restriction only admits that tile.
    const Buildings::Def* mine = catalog.byId("mine");
    REQUIRE(mine != nullptr);
    REQUIRE(mine->allowed_terrain.size() == 1);
    CHECK(mine->allowsTerrain(Elements::mountain));
    CHECK_FALSE(mine->allowsTerrain(Elements::hill));
    CHECK_FALSE(mine->allowsTerrain(Elements::forest));

    // Multiple entries admit any of them.
    const Buildings::Def* quarry = catalog.byId("quarry");
    REQUIRE(quarry != nullptr);
    CHECK(quarry->allowsTerrain(Elements::hill));
    CHECK(quarry->allowsTerrain(Elements::mountain));
    CHECK_FALSE(quarry->allowsTerrain(Elements::forest));

    // An unknown terrain name fails loudly.
    const std::string bad = writeTempBuildings("bad_terrain", R"({
        "buildings": [ { "id": "x", "element": "house", "allowed_terrain": [ "swamp" ] } ]
    })");
    CHECK_THROWS_AS((void)Buildings::loadCatalog(bad), std::runtime_error);
    std::filesystem::remove(bad);
    std::filesystem::remove(file);
}

TEST_CASE("the shipped catalog loads and anchors the city center")
{
    const Buildings::Catalog catalog = Buildings::loadCatalog(shippedCatalogPath());
    REQUIRE(!catalog.all().empty());

    const Buildings::Def* city_center = catalog.byId("city_center");
    // The city center is the founding anchor: it houses the first villagers, so a
    // settlement has a cap before any house is up.
    REQUIRE(city_center != nullptr);
    CHECK(city_center->is_anchor);
    CHECK(city_center->max_count == 1);
    CHECK(city_center->population_capacity >= 20);

    // A house houses a village-sized crowd drawn from a range, not a single
    // family, so the planner does not paper the map with houses.
    const Buildings::Def* house = catalog.byId("house");
    REQUIRE(house != nullptr);
    CHECK(house->population_capacity == 20);
    CHECK(house->population_capacity_max == 40);
    CHECK(house->allowsTerrain(Elements::hill));

    // A farm feeds a 40-80 person band; the planner only builds as many as the
    // housed population needs.
    const Buildings::Def* farm = catalog.byId("farm");
    REQUIRE(farm != nullptr);
    CHECK(farm->feeds_population == 40);
    CHECK(farm->feeds_population_max == 80);

    // The settlement gap is set, so the planner keeps a village-like spacing.
    const Buildings::Settlement settlement = Buildings::loadSettlement(shippedCatalogPath());
    CHECK(settlement.min_spacing_tiles >= 1);

    // Every def has a stable id and a renderable element.
    for (const auto& def : catalog.all())
    {
        CHECK_FALSE(def.id.empty());
        CHECK(def.element != Elements::count);
    }
}
