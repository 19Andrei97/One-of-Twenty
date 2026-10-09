#include "pch.h"

#include "generate_terrain.h"

#include <doctest/doctest.h>

#include <map>

namespace
{
MapConfig terrainConfig()
{
    return loadMapConfig(std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
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
    CHECK(countOf(Elements::river) > 0);

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
