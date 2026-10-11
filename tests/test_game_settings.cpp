#include <doctest/doctest.h>

#include "GameSettings.h"
#include "MapGenerator.h"
#include "MapConfig.h"

#include <memory>
#include <string>

namespace
{
std::string mapConfigPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json";
#else
    return "config/map_data.json";
#endif
}
} // namespace

TEST_CASE("GameSettings defaults describe a run with no menu input")
{
    GameSettings settings;

    CHECK(settings.seed == 1);
    CHECK(settings.street_level == 1);
    CHECK(settings.start_hour == 8);
    CHECK(settings.start_minute == 0);
    CHECK(settings.speed_index == 1);
    CHECK(settings.entity_file == "config/entity_data.json");
    CHECK(settings.buildings_file == "config/buildings.json");
}

TEST_CASE("MapGenerator adopts a captured config verbatim")
{
    // The menu captures its preview's config and a run rebuilds the world from
    // it, so the two must agree exactly - terrain and seed alike.
    int frames = 0;
    const MapConfig source = loadMapConfig(mapConfigPath());

    MapConfig tuned = source;
    tuned.seed = 4242;
    tuned.land_amount = 0.51f;
    tuned.rainfall = 0.33f;
    tuned.ore_richness = 0.77f;

    MapGenerator map(frames, tuned, /*synchronous=*/true);

    const MapConfig& live = map.getConfig();
    CHECK(live.seed == 4242);
    CHECK(live.land_amount == doctest::Approx(tuned.land_amount));
    CHECK(live.rainfall == doctest::Approx(tuned.rainfall));
    CHECK(live.ore_richness == doctest::Approx(tuned.ore_richness));
    CHECK(map.getSeed() == 4242);
}

TEST_CASE("A run recreated from a settings snapshot matches the menu preview")
{
    // Start from a preview map, snapshot it (as the menu does on Play), then
    // build a run from the snapshot. The run must reproduce the preview.
    int previewFrames = 0;
    auto preview = std::make_shared<MapGenerator>(previewFrames, mapConfigPath(), /*synchronous=*/true);
    preview->setSeed(1337);
    preview->setLandAmount(0.44f);

    GameSettings settings;
    settings.map = preview->getConfig();
    settings.seed = preview->getSeed();

    CHECK(settings.seed == 1337);
    CHECK(settings.map.seed == 1337);
    CHECK(settings.map.land_amount == doctest::Approx(0.44f));

    int runFrames = 0;
    auto run = std::make_shared<MapGenerator>(runFrames, settings.map, /*synchronous=*/true);

    CHECK(run->getSeed() == settings.seed);
    CHECK(run->getConfig().land_amount == doctest::Approx(settings.map.land_amount));
}

TEST_CASE("setSeed keeps the live config in step")
{
    // The menu slider edits the seed through setSeed; a later snapshot must
    // carry the new value, so setSeed has to update the config too.
    int frames = 0;
    MapGenerator map(frames, mapConfigPath(), /*synchronous=*/true);
    map.setSeed(9001);

    CHECK(map.getSeed() == 9001);
    CHECK(map.getConfig().seed == 9001);
}
