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

TEST_CASE("the shipped catalog loads and anchors the city center")
{
    const Buildings::Catalog catalog = Buildings::loadCatalog(shippedCatalogPath());
    REQUIRE(!catalog.all().empty());

    const Buildings::Def* city_center = catalog.byId("city_center");
    REQUIRE(city_center != nullptr);
    CHECK(city_center->is_anchor);
    CHECK(city_center->max_count == 1);

    // Every def has a stable id and a renderable element.
    for (const auto& def : catalog.all())
    {
        CHECK_FALSE(def.id.empty());
        CHECK(def.element != Elements::count);
    }
}
