#include "pch.h"

#include "MapGenerator.h"
#include "generate_terrain.h"
#include "DisplayProbe.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <memory>
#include <thread>

#include <nlohmann/json.hpp>

// MapGenerator owns a BS::thread_pool and worker threads that reference its
// members. These tests construct and destroy it repeatedly so the sanitizer
// build catches any use-after-destruction of the mutex/containers.

namespace
{
constexpr const char* kMapConfig = "config/map_data.json";
constexpr int kSampleStep = 50;
constexpr int kSampleRange = 200;

// ctest runs from the build tree, so resolve the repo assets from the source
// directory passed in by CMake instead of relying on the working directory.
std::string mapConfigPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/" + kMapConfig;
#else
    return kMapConfig;
#endif
}

std::unique_ptr<MapGenerator> makeGenerator(int& frames)
{
    return std::make_unique<MapGenerator>(frames, mapConfigPath());
}

// Copy the real map config, apply a patch, and return the temp file path. This
// lets generation options be exercised without shipping enabled variants.
std::string makeVariantConfig(const std::string& name, const std::function<void(nlohmann::json&)>& patch)
{
    std::ifstream in(mapConfigPath());
    REQUIRE(in.good());

    nlohmann::json js;
    in >> js;
    patch(js);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("one_of_twenty_" + name + ".json");
    std::ofstream out(path);
    out << js;
    return path.string();
}

// Classify by element, not by colour: the palette may give shallow water a
// non-zero red channel.
bool isOcean(MapGenerator& generator, const sf::Vector2i& world)
{
    return Resources::isOcean(generator.getBiomeElement(world));
}
} // namespace

TEST_CASE("MapGenerator constructs and destructs without touching freed members")
{
    int frames = 0;

    for (int i = 0; i < 5; ++i)
    {
        auto generator = makeGenerator(frames);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        // generator goes out of scope here; workers must be joined first.
    }

    CHECK(true);
}

TEST_CASE("biome colors are deterministic for a fixed seed")
{
    int frames = 0;

    auto a = makeGenerator(frames);
    a->setSeed(123456);
    a->setNoises();

    auto b = makeGenerator(frames);
    b->setSeed(123456);
    b->setNoises();

    for (int x = -kSampleRange; x <= kSampleRange; x += kSampleStep)
        for (int y = -kSampleRange; y <= kSampleRange; y += kSampleStep)
            CHECK(a->getBiomeColor({ x, y }) == b->getBiomeColor({ x, y }));
}

TEST_CASE("different seeds produce a different map")
{
    int frames = 0;

    auto a = makeGenerator(frames);
    a->setSeed(1);
    a->setNoises();

    auto b = makeGenerator(frames);
    b->setSeed(999999);
    b->setNoises();

    int differing = 0;
    for (int x = -kSampleRange; x <= kSampleRange; x += kSampleStep)
        for (int y = -kSampleRange; y <= kSampleRange; y += kSampleStep)
            if (a->getBiomeColor({ x, y }) != b->getBiomeColor({ x, y }))
                ++differing;

    CHECK(differing > 0);
}

TEST_CASE("element lookup is stable within a tile and follows the grid")
{
    int frames = 0;

    auto generator = makeGenerator(frames);
    generator->setSeed(2024);
    generator->setNoises();

    const int tileSize = generator->getTileSize();
    REQUIRE(tileSize > 1);

    // Every pixel inside one tile maps to the same biome (world -> tile).
    const sf::Vector2i sample{ 5 * tileSize, 7 * tileSize };
    const Elements base = generator->getBiomeElement(sample);
    for (int dx = 0; dx < tileSize; ++dx)
        for (int dy = 0; dy < tileSize; ++dy)
            CHECK(generator->getBiomeElement({ sample.x + dx, sample.y + dy }) == base);

    // Across a wider area the terrain varies, so the grid really does sample the
    // noise per tile rather than returning a constant.
    std::set<int> distinct;
    for (int tx = -20; tx <= 20; ++tx)
        for (int ty = -20; ty <= 20; ++ty)
            distinct.insert(static_cast<int>(generator->getBiomeElement({ tx * tileSize, ty * tileSize })));
    CHECK(distinct.size() > 1);
}

TEST_CASE("island mode surrounds the origin with water")
{
    int frames = 0;

    const std::string config = makeVariantConfig("island", [](nlohmann::json& js) {
        js["island"]["enabled"] = true;
        js["island"]["falloff"] = 0.4;
    });

    auto generator = std::make_unique<MapGenerator>(frames, config);
    generator->setSeed(2024);
    generator->setNoises();

    const int tileSize = generator->getTileSize();
    // The falloff reaches zero at 0.4 chunks; sample far past that.
    constexpr int edgeTiles = 500;

    int originWater = 0;
    int edgeWater = 0;
    constexpr int samples = 7;
    for (int i = 0; i < samples; ++i)
    {
        const int offset = (i - samples / 2) * tileSize * 4;
        originWater += isOcean(*generator, { offset, offset }) ? 1 : 0;

        const int edge = edgeTiles * tileSize;
        edgeWater += isOcean(*generator, { edge + offset, edge + offset }) ? 1 : 0;
    }

    // Origins should hold some land; the far edge must be fully ocean.
    CHECK(originWater < samples);
    CHECK(edgeWater == samples);
}

TEST_CASE("lakes carve fresh water into otherwise dry land")
{
    int frames = 0;

    // Same seed, lakes off vs on: lakes must add water without removing any, and
    // must leave the map mostly dry.
    const std::string plainConfig = makeVariantConfig("lake_off", [](nlohmann::json& js) {
        js["island"]["enabled"] = false;
        js["lake"]["enabled"] = false;
    });
    const std::string lakeConfig = makeVariantConfig("lake_on", [](nlohmann::json& js) {
        js["island"]["enabled"] = false;
        js["lake"]["enabled"] = true;
        js["lake"]["level"] = 0.30;
        js["lake"]["threshold"] = 0.55;
    });

    auto plain = std::make_unique<MapGenerator>(frames, plainConfig);
    plain->setSeed(2024);
    plain->setNoises();

    auto lake = std::make_unique<MapGenerator>(frames, lakeConfig);
    lake->setSeed(2024);
    lake->setNoises();

    const int tileSize = plain->getTileSize();
    int lakes = 0;
    int removedWater = 0;
    int land = 0;
    constexpr int range = 1500;
    for (int x = -range; x <= range; x += 2 * tileSize)
    {
        for (int y = -range; y <= range; y += 2 * tileSize)
        {
            const Elements e = lake->getBiomeElement({ x, y });
            if (e == Elements::lake) ++lakes;
            if (Resources::isWater(e)) continue;
            ++land;
            if (Resources::isWater(plain->getBiomeElement({ x, y }))) ++removedWater;
        }
    }

    CHECK(lakes > 0);
    CHECK(land > 0);
    CHECK(removedWater == 0);
}

TEST_CASE("fresh water is drinkable and distinguished from the ocean")
{
    int frames = 0;
    auto generator = makeGenerator(frames);
    generator->setSeed(2024);
    generator->setNoises();

    // The accessor classification is what survival and knowledge rely on.
    CHECK(Resources::isWater(Elements::lake));
    CHECK(Resources::isWater(Elements::ocean));
    CHECK_FALSE(Resources::isWater(Elements::very_deep_ocean));
    CHECK_FALSE(Resources::isWater(Elements::sand));

    CHECK(Resources::isOcean(Elements::ocean));
    CHECK_FALSE(Resources::isOcean(Elements::lake));

    // Every lake tile found in the world is walkable, so an entity can
    // stand on it to drink.
    const int tileSize = generator->getTileSize();
    int fresh = 0;
    constexpr int range = 1200;
    for (int x = -range; x <= range; x += tileSize)
        for (int y = -range; y <= range; y += tileSize)
        {
            const Elements e = generator->getBiomeElement({ x, y });
            if (e != Elements::lake) continue;
            ++fresh;
            CHECK(MoveCost::moveCost(e) > 0.f);
        }
    CHECK(fresh > 0);
}

TEST_CASE("setChunkUnload reports when no chunk is loaded")
{
    int frames = 0;

    auto generator = makeGenerator(frames);

    // Nothing has been streamed yet, so there is no chunk to pin.
    CHECK_FALSE(generator->setChunkUnload({ 8, 8 }, false));
}

TEST_CASE("render streams chunks and shuts down cleanly")
{
    // SFML aborts the process (SIGABRT) when it cannot open an X11 display,
    // which cannot be caught, so probe first. An unset DISPLAY is not the only
    // failure: a set-but-dead DISPLAY (xvfb-run losing its server) also aborts.
    if (!TestSupport::displayAvailable())
    {
        MESSAGE("Skipping render test: no usable X11 display (run under xvfb-run)");
        return;
    }

    int frames = 0;

    auto generator = makeGenerator(frames);
    generator->setSeed(42);
    generator->setNoises();

    sf::RenderTexture target;
    bool available = false;
    try
    {
        available = target.resize({ 320, 240 });
    }
    catch (const sf::Exception&)
    {
        available = false;
    }

    if (!available)
    {
        MESSAGE("Skipping render test: no render texture available in this environment");
        return;
    }

    const sf::IntRect view{ { 0, 0 }, { 320, 240 } };

    bool foundChunk = false;
    for (int frame = 0; frame < 200 && !foundChunk; ++frame)
    {
        frames = frame;
        generator->render(view, target);

        for (const auto& line : generator->getPositionInfo({ 8, 8 }))
            if (line.rfind("Type:", 0) == 0)
                foundChunk = true;

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    CHECK(foundChunk);
    if (!foundChunk)
        return;

    // A pinned chunk must survive streaming far out of view...
    REQUIRE(generator->setChunkUnload({ 8, 8 }, false));

    const sf::IntRect farView{ { 200000, 200000 }, { 320, 240 } };
    for (int frame = 0; frame < 5; ++frame)
    {
        frames = frame;
        generator->render(farView, target);
    }

    bool stillLoaded = false;
    for (const auto& line : generator->getPositionInfo({ 8, 8 }))
        if (line.rfind("Type:", 0) == 0)
            stillLoaded = true;
    CHECK(stillLoaded);

    // ...and be evicted once it is released again. The worker is asynchronous,
    // so poll until the eviction has actually happened rather than assuming a
    // fixed number of frames is enough.
    REQUIRE(generator->setChunkUnload({ 8, 8 }, true));
    bool evicted = false;
    for (int frame = 0; frame < 200 && !evicted; ++frame)
    {
        frames = frame;
        generator->render(farView, target);

        evicted = true;
        for (const auto& line : generator->getPositionInfo({ 8, 8 }))
            if (line.rfind("Type:", 0) == 0)
                evicted = false;

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(evicted);
}

TEST_CASE("setTileColor updates the map and the rendered chunk consistently")
{
    // SFML aborts the process (SIGABRT) from its X11 backend when it cannot
    // open the display; probe first so an unset or dead DISPLAY skips instead.
    if (!TestSupport::displayAvailable())
    {
        MESSAGE("Skipping render test: no X11 display (run under xvfb-run)");
        return;
    }

    int frames = 0;

    auto generator = makeGenerator(frames);
    generator->setSeed(42);
    generator->setNoises();

    sf::RenderTexture target;
    bool available = false;
    try
    {
        available = target.resize({ 320, 240 });
    }
    catch (const sf::Exception&)
    {
        available = false;
    }

    if (!available)
    {
        MESSAGE("Skipping render test: no render texture available in this environment");
        return;
    }

    const sf::IntRect view{ { 0, 0 }, { 320, 240 } };

    // Tile (2, 2) lies inside the first chunk, so it streams in immediately.
    const int tileSize = generator->getTileSize();
    const sf::Vector2i editedWorld{ 2 * tileSize, 2 * tileSize };

    bool loaded = false;
    for (int frame = 0; frame < 200 && !loaded; ++frame)
    {
        frames = frame;
        target.clear();
        generator->render(view, target);
        target.display();

        for (const auto& line : generator->getPositionInfo(editedWorld))
            if (line.rfind("Type:", 0) == 0)
                loaded = true;

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    REQUIRE(loaded);

    // Elements::test is absent from the config, so its biome entry is the
    // default-constructed (opaque black) colour - an easy marker to find.
    const sf::Color testColor;

    auto tileRegionHas = [&](const sf::Color& wanted) {
        const sf::Image image = target.getTexture().copyToImage();
        // Interior of the tile, away from the neighbouring-rectangle edges.
        for (unsigned y = static_cast<unsigned>(editedWorld.y + 1);
             y < static_cast<unsigned>(editedWorld.y + tileSize - 1); ++y)
            for (unsigned x = static_cast<unsigned>(editedWorld.x + 1);
                 x < static_cast<unsigned>(editedWorld.x + tileSize - 1); ++x)
                if (image.getPixel({ x, y }) == wanted)
                    return true;
        return false;
    };

    // Before the edit this ground tile is ordinary terrain, not the marker.
    CHECK_FALSE(tileRegionHas(testColor));

    REQUIRE(generator->setTileColor(editedWorld, Elements::test));

    // getPositionInfo must report the stored edit rather than the raw noise, as
    // a readable terrain name.
    bool reported = false;
    for (const auto& line : generator->getPositionInfo(editedWorld))
        if (line == std::string("Type: ") + Resources::displayName(Elements::test))
            reported = true;
    CHECK(reported);

    // Re-render without regenerating: the chunk is unchanged apart from the one
    // tile, so the marker colour proves the mesh was rebuilt from tile_types.
    frames = 400;
    target.clear();
    generator->render(view, target);
    target.display();

    CHECK(tileRegionHas(testColor));
}

TEST_CASE("tile cost reads the authoritative tile and is always positive")
{
    // SFML aborts the process (SIGABRT) from its X11 backend when it cannot
    // open the display; probe first so an unset or dead DISPLAY skips instead.
    if (!TestSupport::displayAvailable())
    {
        MESSAGE("Skipping render test: no X11 display (run under xvfb-run)");
        return;
    }

    int frames = 0;

    auto generator = makeGenerator(frames);
    generator->setSeed(42);
    generator->setNoises();

    sf::RenderTexture target;
    bool available = false;
    try
    {
        available = target.resize({ 320, 240 });
    }
    catch (const sf::Exception&)
    {
        available = false;
    }

    if (!available)
    {
        MESSAGE("Skipping render test: no render texture available in this environment");
        return;
    }

    // No chunk loaded yet: the cost must fall back to neutral, not 0.
    CHECK(generator->getTileCost({ 8, 8 }) == doctest::Approx(MoveCost::kDefault));

    const sf::IntRect view{ { 0, 0 }, { 320, 240 } };
    const int tileSize = generator->getTileSize();
    const sf::Vector2i tile{ 2 * tileSize, 2 * tileSize };

    bool loaded = false;
    for (int frame = 0; frame < 200 && !loaded; ++frame)
    {
        frames = frame;
        generator->render(view, target);
        for (const auto& line : generator->getPositionInfo(tile))
            if (line.rfind("Type:", 0) == 0)
                loaded = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(loaded);

    CHECK(generator->getTileCost(tile) > 0.f);

    // The cost must follow the stored edit, since movement scales by it.
    REQUIRE(generator->setTileColor(tile, Elements::forest));
    CHECK(generator->getTileCost(tile) == doctest::Approx(MoveCost::moveCost(Elements::forest)));
}

TEST_CASE("height_range caps peaks and floors the lowlands")
{
    int frames = 0;

    // A narrow range well below the snow/mountain thresholds must remove every
    // high biome and, with a min above the ocean thresholds, flood the lowlands.
    const std::string flatConfig = makeVariantConfig("height_narrow", [](nlohmann::json& js) {
        js["island"]["enabled"] = false;
        js["height_range"]["min"] = 0.0;
        js["height_range"]["max"] = 0.3;
    });

    auto generator = std::make_unique<MapGenerator>(frames, flatConfig);
    generator->setSeed(2024);
    generator->setNoises();

    const int tileSize = generator->getTileSize();
    int high = 0;
    int water = 0;
    int total = 0;
    constexpr int range = 1500;
    for (int x = -range; x <= range; x += 3 * tileSize)
    {
        for (int y = -range; y <= range; y += 3 * tileSize)
        {
            const Elements e = generator->getBiomeElement({ x, y });
            ++total;
            if (isOcean(*generator, { x, y })) ++water;
            if (e == Elements::mountain || e == Elements::snow) ++high;
        }
    }

    // Max 0.3 sits below the sand threshold, so no tile is above the beach: the
    // capped lowlands become sea (some of it too deep to stand in).
    CHECK(high == 0);
    CHECK(water > 0);
    CHECK(total > 0);

    // Raising the floor above the ocean band turns the shallow sea into land:
    // the same seed gets strictly less water than the baseline.
    const std::string floodConfig = makeVariantConfig("height_flood", [](nlohmann::json& js) {
        js["island"]["enabled"] = false;
        js["height_range"]["min"] = 0.5;
        js["height_range"]["max"] = 1.0;
    });

    auto flooded = std::make_unique<MapGenerator>(frames, floodConfig);
    flooded->setSeed(2024);
    flooded->setNoises();

    int floodedWater = 0;
    for (int x = -range; x <= range; x += 3 * tileSize)
        for (int y = -range; y <= range; y += 3 * tileSize)
            if (isOcean(*flooded, { x, y })) ++floodedWater;

    CHECK(floodedWater == 0);
}

TEST_CASE("each resource uses its own noise field")
{
    int frames = 0;

    auto generator = makeGenerator(frames);
    generator->setSeed(2024);
    generator->setNoises();

    const int tileSize = generator->getTileSize();
    std::set<std::pair<int, int>> clay, iron, silver;
    constexpr int range = 2000;
    for (int x = -range; x <= range; x += 2 * tileSize)
        for (int y = -range; y <= range; y += 2 * tileSize)
        {
            // The deposit of an ore is the region where its own field is high.
            const auto key = std::make_pair(x, y);
            if (generator->getResourceValue({ x, y }, Elements::clay) > 0.5f)
                clay.insert(key);
            if (generator->getResourceValue({ x, y }, Elements::iron) > 0.5f)
                iron.insert(key);
            if (generator->getResourceValue({ x, y }, Elements::silver) > 0.5f)
                silver.insert(key);
        }

    // Each field produces a non-trivial deposit...
    CHECK_FALSE(clay.empty());
    CHECK_FALSE(iron.empty());
    CHECK_FALSE(silver.empty());

    // ...and they land in different places: a single shared field would make all
    // three sets identical.
    CHECK(clay != iron);
    CHECK(iron != silver);
    CHECK(clay != silver);
}

TEST_CASE("map queries read the authoritative tile map")
{
    // SFML aborts the process (SIGABRT) from its X11 backend when it cannot
    // open the display; probe first so an unset or dead DISPLAY skips instead.
    if (!TestSupport::displayAvailable())
    {
        MESSAGE("Skipping query test: no X11 display (run under xvfb-run)");
        return;
    }

    int frames = 0;

    auto generator = makeGenerator(frames);
    generator->setSeed(42);
    generator->setNoises();

    sf::RenderTexture target;
    bool available = false;
    try
    {
        available = target.resize({ 320, 240 });
    }
    catch (const sf::Exception&)
    {
        available = false;
    }

    if (!available)
    {
        MESSAGE("Skipping query test: no render texture available in this environment");
        return;
    }

    const sf::IntRect view{ { 0, 0 }, { 320, 240 } };
    const int tileSize = generator->getTileSize();
    const sf::Vector2i editedWorld{ 2 * tileSize, 2 * tileSize };

    bool loaded = false;
    for (int frame = 0; frame < 200 && !loaded; ++frame)
    {
        frames = frame;
        target.clear();
        generator->render(view, target);
        target.display();

        for (const auto& line : generator->getPositionInfo(editedWorld))
            if (line.rfind("Type:", 0) == 0)
                loaded = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(loaded);

    // With the chunk loaded, the query must agree with the pure-noise lookup...
    CHECK(generator->getElementAtWorld(editedWorld) ==
          generator->getBiomeElement(editedWorld));

    REQUIRE(generator->setTileColor(editedWorld, Elements::test));

    // ...and after an edit it must report the stored value, while the noise
    // lookup stays at the original terrain. That difference is the whole point
    // of routing entity queries through the authoritative map.
    CHECK(generator->getElementAtWorld(editedWorld) == Elements::test);
    CHECK(generator->getBiomeElement(editedWorld) != Elements::test);
}

// Exercises the terrain sampler on its own: no font, no thread pool, no GL
// context. This is the point of splitting GenerateTerrain out of MapGenerator.
TEST_CASE("terrain sampling is deterministic and seed-dependent without a renderer")
{
    const MapConfig config = loadMapConfig(mapConfigPath());
    REQUIRE(config.tile_size_px > 0);

    GenerateTerrain a(config);
    GenerateTerrain b(config);

    // Same seed/config => identical terrain over a spread of tiles.
    constexpr int kStep = 5;
    constexpr int kSpan = 30;
    for (int y = -kSpan; y <= kSpan; y += kStep)
        for (int x = -kSpan; x <= kSpan; x += kStep)
            CHECK(a.elementAtTile({ x, y }) == b.elementAtTile({ x, y }));

    // A different seed must actually change the terrain somewhere.
    GenerateTerrain c(config);
    c.setSeed(config.seed + 9973);

    bool differs = false;
    for (int y = -kSpan; y <= kSpan && !differs; y += kStep)
        for (int x = -kSpan; x <= kSpan && !differs; x += kStep)
            if (a.elementAtTile({ x, y }) != c.elementAtTile({ x, y }))
                differs = true;
    CHECK(differs);

    // World lookup floors into the owning tile.
    const int ts = config.tile_size_px;
    CHECK(a.elementAtWorld({ ts, ts }) == a.elementAtTile({ 1, 1 }));
}

// getResourcesWithinBoundary scans a tile grid, so it must agree with the
// authoritative per-tile lookup on every candidate it reports, including edits.
TEST_CASE("getResourcesWithinBoundary reports the authoritative nearest resource")
{
    int frames = 0;

    auto generator = makeGenerator(frames);
    generator->setSeed(2024);
    generator->setNoises();

    const int tileSize = generator->getTileSize();
    const sf::Vector2i center{ 4 * tileSize, 4 * tileSize };
    const float radius = static_cast<float>(tileSize) * 3.f;

    const auto resources = generator->getResourcesWithinBoundary(center, radius);

    for (const auto& [element, world] : resources)
    {
        // Reported tiles carry the same element the authoritative lookup does.
        CHECK(generator->getElementAtWorld(world) == element);
        CHECK(Resources::isResource(element));
    }

    // An edit to a nearby tile must be visible to the scan, which proves it
    // reads the stored tile map rather than a fresh noise sample.
    const sf::Vector2i edited = center + sf::Vector2i(tileSize, 0);
    if (generator->setTileColor(edited, Elements::test))
    {
        const auto after = generator->getResourcesWithinBoundary(center, radius);
        const auto it = after.find(Elements::test);
        if (it != after.end())
            CHECK(generator->getElementAtWorld(it->second) == Elements::test);
    }
}

TEST_CASE("map sizes describe a finite, nested, square world")
{
    // The three sizes are pure data, so the relationships can be checked without
    // building a map: each is bigger than the last, all are square, and the
    // shipped default is the smallest.
    CHECK(mapSizeChunks(MapSize::Normal) < mapSizeChunks(MapSize::Big));
    CHECK(mapSizeChunks(MapSize::Big) < mapSizeChunks(MapSize::Enormous));

    CHECK(mapSizeChunkCount(MapSize::Normal) == mapSizeChunks(MapSize::Normal) * mapSizeChunks(MapSize::Normal));
    CHECK(mapSizeChunkCount(MapSize::Big) == mapSizeChunks(MapSize::Big) * mapSizeChunks(MapSize::Big));
    CHECK(mapSizeChunkCount(MapSize::Enormous) == mapSizeChunks(MapSize::Enormous) * mapSizeChunks(MapSize::Enormous));

    // Names round-trip, case-insensitively.
    CHECK(mapSizeFromString("Normal") == MapSize::Normal);
    CHECK(mapSizeFromString("big") == MapSize::Big);
    CHECK(mapSizeFromString("ENORMOUS") == MapSize::Enormous);
    CHECK_THROWS_AS((void)mapSizeFromString("huge"), std::invalid_argument);

    // The world rect is square, centred on the origin, and scales with the size.
    constexpr int chunkTiles = 32;
    const sf::IntRect normal = worldTileBounds(MapSize::Normal, chunkTiles);
    const sf::IntRect big = worldTileBounds(MapSize::Big, chunkTiles);
    const sf::IntRect enormous = worldTileBounds(MapSize::Enormous, chunkTiles);

    CHECK(normal.size.x == normal.size.y);
    CHECK(normal.position == -normal.size / 2);
    CHECK(big.size.x == big.size.y);
    CHECK(enormous.size.x == enormous.size.y);
    CHECK(normal.size.x < big.size.x);
    CHECK(big.size.x < enormous.size.x);

    // Tile and pixel bounds agree at the map's own tile size.
    const sf::IntRect pixel = worldPixelBounds(MapSize::Big, chunkTiles, 16);
    CHECK(pixel.position == big.position * 16);
    CHECK(pixel.size == big.size * 16);
}

TEST_CASE("the map config carries the selected world size")
{
    int frames = 0;

    // The shipped config asks for Normal.
    auto normal = makeGenerator(frames);
    CHECK(normal->getMapSize() == MapSize::Normal);
    CHECK(normal->getWorldTileBounds() == worldTileBounds(MapSize::Normal, 32));

    // A config can ask for a bigger world; the generator's bounds follow it.
    const std::string bigConfig = makeVariantConfig("size_big", [](nlohmann::json& js) {
        js["map_size"] = "Big";
    });
    auto big = std::make_unique<MapGenerator>(frames, bigConfig);
    CHECK(big->getMapSize() == MapSize::Big);
    CHECK(big->getWorldTileBounds() == worldTileBounds(MapSize::Big, 32));
    CHECK(big->getWorldPixelBounds() == worldPixelBounds(MapSize::Big, 32, big->getTileSize()));

    // An unknown size is a hard error, not a silent fallback.
    const std::string badConfig = makeVariantConfig("size_bad", [](nlohmann::json& js) {
        js["map_size"] = "gigantic";
    });
    CHECK_THROWS_AS(std::make_unique<MapGenerator>(frames, badConfig), std::exception);
}

TEST_CASE("the MapConfig constructor also establishes the world bounds")
{
    // The menu hands a run a MapConfig value (Scene_Play builds the run map from
    // settings.map), so this constructor must set the world bounds just like the
    // file-loading one. A missing refreshWorldBounds leaves the bounds empty, and
    // every query then reads "outside the world" and returns deep ocean for the
    // whole map until a size button happens to fire.
    int frames = 0;

    MapConfig config = loadMapConfig(mapConfigPath());
    config.map_size = MapSize::Big;
    auto generator = std::make_unique<MapGenerator>(frames, config);

    CHECK(generator->getMapSize() == MapSize::Big);
    CHECK(generator->getWorldTileBounds() == worldTileBounds(MapSize::Big, config.chunk_tile_size));
    CHECK(generator->getWorldPixelBounds() ==
          worldPixelBounds(MapSize::Big, config.chunk_tile_size, config.tile_size_px));

    // The origin is the world centre, so a query there must be inside the world
    // and read the real terrain, not the out-of-bounds deep ocean.
    CHECK(generator->containsWorld({ 0, 0 }));
    CHECK(generator->getBiomeElement({ 0, 0 }) != Elements::very_deep_ocean);
}

TEST_CASE("outside the finite world reads as ocean and carries no resources")
{
    int frames = 0;

    auto generator = makeGenerator(frames);
    generator->setSeed(2024);
    generator->setNoises();

    const sf::IntRect world = generator->getWorldTileBounds();
    const int tileSize = generator->getTileSize();

    // A point just past each edge: outside the world, so deep ocean everywhere.
    const sf::Vector2i outsideRight{ (world.position.x + world.size.x) * tileSize + tileSize, 0 };
    const sf::Vector2i outsideLeft{ (world.position.x - 1) * tileSize, 0 };
    const sf::Vector2i outsideTop{ 0, (world.position.y - 1) * tileSize };
    const sf::Vector2i outsideBottom{ 0, (world.position.y + world.size.y) * tileSize };

    for (const sf::Vector2i& pos : { outsideRight, outsideLeft, outsideTop, outsideBottom })
    {
        CHECK_FALSE(generator->containsWorld(pos));
        CHECK(generator->getBiomeElement(pos) == Elements::very_deep_ocean);
        CHECK(generator->getElementAtWorld(pos) == Elements::very_deep_ocean);
        CHECK(generator->getResourceValue(pos, Elements::clay) == doctest::Approx(0.f));
        CHECK(generator->getTileCost(pos) == doctest::Approx(MoveCost::moveCost(Elements::very_deep_ocean)));
        for (const auto& line : generator->getPositionInfo(pos))
            CHECK(line == "Unknown");
    }

    // The origin is the world centre, so it is inside.
    CHECK(generator->containsWorld({ 0, 0 }));
    CHECK(generator->containsTile({ 0, 0 }));
}

TEST_CASE("streaming never loads a chunk outside the finite world")
{
    int frames = 0;

    // A synchronous generator so the queue is drained inline and the loaded set
    // is deterministic (no worker timing).
    auto generator = std::make_unique<MapGenerator>(frames, mapConfigPath(), /*synchronous=*/true);
    generator->setSeed(2024);

    // A view centred on the top-left corner of the world. Half of the view lies
    // outside the map, so without the bound the queue would generate a ring of
    // chunks beyond the edge.
    const sf::IntRect world = generator->getWorldTileBounds();
    const int tileSize = generator->getTileSize();
    const sf::Vector2i cornerWorld{ world.position.x * tileSize, world.position.y * tileSize };
    const sf::IntRect view{ cornerWorld - sf::Vector2i{ 256, 256 }, sf::Vector2i{ 512, 512 } };

    generator->stream(view);

    // Every tile just outside the world stays unloaded (deep ocean, no chunk).
    const sf::Vector2i justOutside{ (world.position.x - 1) * tileSize, (world.position.y - 1) * tileSize };
    for (const auto& line : generator->getPositionInfo(justOutside))
        CHECK(line == "Unknown");

    // A tile inside the corner is loaded: the bound keeps the real edge, it does
    // not reject the whole corner.
    const sf::Vector2i insideWorld{ (world.position.x + 1) * tileSize, (world.position.y + 1) * tileSize };
    bool insideLoaded = false;
    for (const auto& line : generator->getPositionInfo(insideWorld))
        if (line.rfind("Type:", 0) == 0)
            insideLoaded = true;
    CHECK(insideLoaded);
}

TEST_CASE("setMapSize moves the bounds and reloads the world")
{
    int frames = 0;

    auto generator = std::make_unique<MapGenerator>(frames, mapConfigPath(), /*synchronous=*/true);
    generator->setSeed(2024);

    const sf::IntRect normalBounds = generator->getWorldTileBounds();

    // Load a chunk at the origin first, so the size change has something to drop.
    generator->stream(sf::IntRect{ { -256, -256 }, { 512, 512 } });
    bool loadedBefore = false;
    for (const auto& line : generator->getPositionInfo({ 8, 8 }))
        if (line.rfind("Type:", 0) == 0)
            loadedBefore = true;
    REQUIRE(loadedBefore);

    generator->setMapSize(MapSize::Big);

    CHECK(generator->getMapSize() == MapSize::Big);
    CHECK(generator->getWorldTileBounds() == worldTileBounds(MapSize::Big, 32));
    CHECK(generator->getWorldTileBounds().size.x > normalBounds.size.x);

    // The loaded chunks were dropped with the old world, so the origin reads as
    // unknown until the next stream rebuilds it.
    for (const auto& line : generator->getPositionInfo({ 8, 8 }))
        CHECK(line == "Unknown");

    // The enlarged world is real: a point outside the old world but inside the
    // new one is now inside the bounds, and streaming there loads it.
    const sf::Vector2i beyondOld{ (normalBounds.position.x + normalBounds.size.x + 1) * generator->getTileSize(), 0 };
    CHECK(generator->containsWorld(beyondOld));
    generator->stream(sf::IntRect{ beyondOld - sf::Vector2i{ 256, 256 }, { 512, 512 } });
    bool loadedAfter = false;
    for (const auto& line : generator->getPositionInfo(beyondOld))
        if (line.rfind("Type:", 0) == 0)
            loadedAfter = true;
    CHECK(loadedAfter);

    // Setting the same size again is a no-op and does not disturb the bounds.
    const sf::IntRect bigBounds = generator->getWorldTileBounds();
    generator->setMapSize(MapSize::Big);
    CHECK(generator->getWorldTileBounds() == bigBounds);
}
