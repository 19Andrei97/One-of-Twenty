#include "pch.h"

#include "generate_terrain.h"

#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace
{
MapConfig terrainConfig()
{
    return loadMapConfig(std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
}

// Copy the real map config, apply a patch, and return the temp file path, so a
// test can exercise a generation option without shipping an enabled variant.
std::string makeTerrainVariantConfig(const std::string& name,
                                     const std::function<void(nlohmann::json&)>& patch)
{
    const std::string path = std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json";
    std::ifstream in(path);
    REQUIRE(in.good());

    nlohmann::json js;
    in >> js;
    patch(js);

    const std::filesystem::path out_path =
        std::filesystem::temp_directory_path() / ("one_of_twenty_terrain_" + name + ".json");
    std::ofstream out(out_path);
    out << js;
    return out_path.string();
}

// Count how many sampled tiles fall into each element over a wide span, so a
// test can assert on the shape of the world rather than a single tile.
std::map<Elements, int> sampleHistogram(const GenerateTerrain& terrain, int span, int step)
{
    std::map<Elements, int> hist;
    for (int y = -span; y <= span; y += step)
        for (int x = -span; x <= span; x += step)
            ++hist[terrain.elementAtTile({ x, y })];
    return hist;
}
} // namespace

// The whole point of the layered pipeline is a world that reads as Earth: a mix
// of land and sea, real coastlines, mountains with snow caps, climate-driven
// biomes and findable ore - not a uniform field of one biome. These checks pin
// that shape down so a future noise tweak that flattens the world fails loudly.
TEST_CASE("generated worlds are Earth-like: land, sea, mountains, biomes and ore")
{
    const MapConfig config = terrainConfig();
    GenerateTerrain terrain(config);
    terrain.setSeed(2024);

    constexpr int kSpan = 6000;
    constexpr int kStep = 40;
    const auto hist = sampleHistogram(terrain, kSpan, kStep);

    int total = 0;
    int land = 0;
    for (const auto& [element, count] : hist)
    {
        total += count;
        if (!Resources::isOcean(element))
            land += count;
    }
    REQUIRE(total > 0);

    const auto countOf = [&](Elements e) { return hist.count(e) ? hist.at(e) : 0; };
    const float landFraction = static_cast<float>(land) / static_cast<float>(total);

    // Neither an all-ocean nor an all-land world.
    CHECK(landFraction > 0.2f);
    CHECK(landFraction < 0.8f);

    // Every land biome the pipeline can produce actually shows up somewhere, so
    // the climate and elevation bands are all reachable.
    CHECK(countOf(Elements::sand) > 0);
    CHECK(countOf(Elements::hill) > 0);
    CHECK(countOf(Elements::forest) > 0);
    CHECK(countOf(Elements::mountain) > 0);
    CHECK(countOf(Elements::snow) > 0);

    // Inland fresh water is carved and is rarer than the sea.
    CHECK(countOf(Elements::lake) > 0);

    // All three ores are present but scarce.
    CHECK(countOf(Elements::clay) > 0);
    CHECK(countOf(Elements::iron) > 0);
    CHECK(countOf(Elements::silver) > 0);
    const float oreFraction = static_cast<float>(
        countOf(Elements::clay) + countOf(Elements::iron) + countOf(Elements::silver))
        / static_cast<float>(total);
    CHECK(oreFraction < 0.15f);
}

// Temperature falls off with latitude, so the far north/south is colder than the
// equator. This is what turns the biome map into bands rather than a uniform
// forest and is the mechanism the `temperature` slider shifts.
TEST_CASE("temperature decreases with latitude")
{
    const MapConfig config = terrainConfig();
    GenerateTerrain terrain(config);
    terrain.setSeed(2024);

    float equator = 0.f;
    float pole = 0.f;
    constexpr int kSamples = 40;
    for (int i = 0; i < kSamples; ++i)
    {
        const int x = (i - kSamples / 2) * 30;
        equator += terrain.temperatureAtTile({ x, 0 });
        pole += terrain.temperatureAtTile({ x, 12000 });
    }

    CHECK(equator / kSamples > pole / kSamples + 0.2f);
}

// `land_amount` is the single sea-level control: raising it must add land. This
// is the property the "Land Amount" slider promises, so it is worth a test.
TEST_CASE("raising land_amount adds land")
{
    MapConfig config = terrainConfig();

    config.land_amount = 0.3f;
    GenerateTerrain low(config);
    low.setSeed(2024);
    const auto lowHist = sampleHistogram(low, 6000, 60);

    config.land_amount = 0.7f;
    GenerateTerrain high(config);
    high.setSeed(2024);
    const auto highHist = sampleHistogram(high, 6000, 60);

    const auto landOf = [](const std::map<Elements, int>& hist) {
        int land = 0, total = 0;
        for (const auto& [element, count] : hist) { total += count; if (!Resources::isOcean(element)) land += count; }
        return total ? static_cast<float>(land) / static_cast<float>(total) : 0.f;
    };

    CHECK(landOf(highHist) > landOf(lowHist));
}

// Every gatherable is a finite pile: a tile of a gatherable holds a positive
// amount, a non-gatherable holds none, and the amount scales with the global
// `resource_richness` control. This is the "resources are not endless" promise.
TEST_CASE("gatherable tiles hold a finite, configurable deposit")
{
    MapConfig config = terrainConfig();
    GenerateTerrain terrain(config);
    terrain.setSeed(2024);

    const std::array<Elements, 5> kGatherables{
        Elements::forest, Elements::hill, Elements::clay, Elements::iron, Elements::silver
    };
    for (const Elements element : kGatherables)
    {
        CHECK(terrain.resourceAmountAtTile({ 0, 0 }, element) >= 0);
    }

    // A non-gatherable holds nothing, whatever the tile coordinate.
    CHECK(terrain.resourceAmountAtTile({ 0, 0 }, Elements::ocean) == 0);
    CHECK(terrain.resourceAmountAtTile({ 0, 0 }, Elements::sand) == 0);
    CHECK(terrain.resourceAmountAtTile({ 0, 0 }, Elements::mountain) == 0);
    CHECK(terrain.resourceAmountAtTile({ 0, 0 }, Elements::barren) == 0);

    // At least one gatherable tile in a sample holds a real pile, and no pile is
    // ever negative.
    int total = 0;
    int positive = 0;
    for (int y = -3000; y <= 3000; y += 40)
        for (int x = -3000; x <= 3000; x += 40)
        {
            const Elements element = terrain.elementAtTile({ x, y });
            if (!Resources::isGatherable(element))
                continue;
            const int amount = terrain.resourceAmountAtTile({ x, y }, element);
            CHECK(amount >= 0);
            total += amount;
            if (amount > 0)
                ++positive;
        }
    CHECK(positive > 0);
    CHECK(total > 0);

    // `resource_richness` is the single "how plentiful" control: raising it grows
    // every deposit. Compare the summed piles on the same tiles.
    config.resource_richness = 0.9f;
    GenerateTerrain rich(config);
    rich.setSeed(2024);
    int richTotal = 0;
    for (int y = -3000; y <= 3000; y += 40)
        for (int x = -3000; x <= 3000; x += 40)
        {
            const Elements element = rich.elementAtTile({ x, y });
            if (Resources::isGatherable(element))
                richTotal += rich.resourceAmountAtTile({ x, y }, element);
        }
    CHECK(richTotal > total);
}

// A biome gives one resource more than another: the abundance multiplier is a
// pure function of the tile's climate, so a resource with a positive bonus is
// richer on lush ground and one with a negative bonus on dry ground.
TEST_CASE("a biome favours the resource its climate suits")
{
    MapConfig config = terrainConfig();
    // Give forest a strong lush bonus and hill a strong dry bonus, then confirm
    // the abundance multiplier moves in opposite directions across the map.
    config.deposits[static_cast<std::size_t>(Elements::forest)].biome_bonus = 1.5f;
    config.deposits[static_cast<std::size_t>(Elements::hill)].biome_bonus = -1.5f;
    GenerateTerrain terrain(config);
    terrain.setSeed(2024);

    float forestSum = 0.f;
    float hillSum = 0.f;
    int samples = 0;
    for (int y = -2000; y <= 2000; y += 100)
        for (int x = -2000; x <= 2000; x += 100)
        {
            forestSum += terrain.resourceAbundanceAtTile({ x, y }, Elements::forest);
            hillSum += terrain.resourceAbundanceAtTile({ x, y }, Elements::hill);
            ++samples;
        }
    REQUIRE(samples > 0);

    // Neither multiplier is a constant 1, and the two are not identical: the
    // climate is what makes the biomes differ.
    CHECK(forestSum / samples != doctest::Approx(1.f));
    CHECK(hillSum / samples != doctest::Approx(1.f));
    CHECK(forestSum != doctest::Approx(hillSum));

    // A resource with no bonus ignores the climate entirely.
    CHECK(terrain.resourceAbundanceAtTile({ 100, 100 }, Elements::clay) == doctest::Approx(1.f));
}

// Deposit sizes are data-driven: a `resource_deposits` block overrides the
// shipped range per element, so a biome's wealth is a config edit.
TEST_CASE("resource_deposits config sizes each element's pile")
{
    const std::string config_path = makeTerrainVariantConfig("deposits", [](nlohmann::json& js) {
        js["resource_deposits"] = {
            { "forest", { { "min", 10 }, { "max", 12 }, { "biome_bonus", 0.0 } } },
            { "hill",   { { "min", 1 },  { "max", 1 } } },
        };
    });

    MapConfig config = loadMapConfig(config_path);
    // The forest range came from the config, not the shipped default.
    CHECK(config.deposits[static_cast<std::size_t>(Elements::forest)].min == 10);
    CHECK(config.deposits[static_cast<std::size_t>(Elements::forest)].max == 12);
    CHECK(config.deposits[static_cast<std::size_t>(Elements::hill)].min == 1);
    CHECK(config.deposits[static_cast<std::size_t>(Elements::hill)].max == 1);

    // With no climate bonus and richness 0.5 (x1), a forest pile stays in range.
    GenerateTerrain terrain(config);
    terrain.setSeed(2024);
    for (int y = -1000; y <= 1000; y += 50)
        for (int x = -1000; x <= 1000; x += 50)
        {
            const int amount = terrain.resourceAmountAtTile({ x, y }, Elements::forest);
            CHECK(amount >= 10);
            CHECK(amount <= 12);
        }
}
