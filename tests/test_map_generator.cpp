#include "pch.h"

#include "MapGenerator.h"

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

std::unique_ptr<MapGenerator> makeGenerator(sf::Font& font, int& frames)
{
    return std::make_unique<MapGenerator>(font, frames, mapConfigPath());
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

// Water is the only biome family with a zero red channel.
bool isWater(MapGenerator& generator, const sf::Vector2i& world)
{
    return generator.getBiomeColor(world).r == 0;
}
} // namespace

TEST_CASE("MapGenerator constructs and destructs without touching freed members")
{
    sf::Font font;
    int frames = 0;

    for (int i = 0; i < 5; ++i)
    {
        auto generator = makeGenerator(font, frames);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        // generator goes out of scope here; workers must be joined first.
    }

    CHECK(true);
}

TEST_CASE("biome colors are deterministic for a fixed seed")
{
    sf::Font font;
    int frames = 0;

    auto a = makeGenerator(font, frames);
    a->setSeed(123456);
    a->setNoises();

    auto b = makeGenerator(font, frames);
    b->setSeed(123456);
    b->setNoises();

    for (int x = -kSampleRange; x <= kSampleRange; x += kSampleStep)
        for (int y = -kSampleRange; y <= kSampleRange; y += kSampleStep)
            CHECK(a->getBiomeColor({ x, y }) == b->getBiomeColor({ x, y }));
}

TEST_CASE("different seeds produce a different map")
{
    sf::Font font;
    int frames = 0;

    auto a = makeGenerator(font, frames);
    a->setSeed(1);
    a->setNoises();

    auto b = makeGenerator(font, frames);
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
    sf::Font font;
    int frames = 0;

    auto generator = makeGenerator(font, frames);
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
    sf::Font font;
    int frames = 0;

    const std::string config = makeVariantConfig("island", [](nlohmann::json& js) {
        js["island"]["enabled"] = true;
        js["island"]["falloff"] = 0.4;
    });

    auto generator = std::make_unique<MapGenerator>(font, frames, config);
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
        originWater += isWater(*generator, { offset, offset }) ? 1 : 0;

        const int edge = edgeTiles * tileSize;
        edgeWater += isWater(*generator, { edge + offset, edge + offset }) ? 1 : 0;
    }

    // Origins should hold some land; the far edge must be fully ocean.
    CHECK(originWater < samples);
    CHECK(edgeWater == samples);
}

TEST_CASE("rivers carve water into otherwise dry land")
{
    sf::Font font;
    int frames = 0;

    // Compare the same seed with rivers off and on: rivers must only add water,
    // never remove it, and must not drown the whole map.
    const std::string plainConfig = makeVariantConfig("river_off", [](nlohmann::json& js) {
        js["island"]["enabled"] = false;
        js["river"]["enabled"] = false;
    });
    const std::string riverConfig = makeVariantConfig("river_on", [](nlohmann::json& js) {
        js["island"]["enabled"] = false;
        js["river"]["enabled"] = true;
        js["river"]["freq"] = 0.01;
        js["river"]["threshold"] = 0.06;
    });

    auto plain = std::make_unique<MapGenerator>(font, frames, plainConfig);
    plain->setSeed(2024);
    plain->setNoises();

    auto river = std::make_unique<MapGenerator>(font, frames, riverConfig);
    river->setSeed(2024);
    river->setNoises();

    const int tileSize = plain->getTileSize();
    int extraWater = 0;
    int removedWater = 0;
    int addedWater = 0;
    int land = 0;
    constexpr int range = 1000;
    for (int x = -range; x <= range; x += 2 * tileSize)
    {
        for (int y = -range; y <= range; y += 2 * tileSize)
        {
            const bool plainWater = isWater(*plain, { x, y });
            const bool riverWater = isWater(*river, { x, y });

            if (riverWater && !plainWater) ++extraWater;
            if (!riverWater && plainWater) ++removedWater;
            if (riverWater) ++addedWater;
            else ++land;
        }
    }

    // Rivers only convert land to water, and they clearly do so here.
    CHECK(removedWater == 0);
    CHECK(extraWater > 0);
    CHECK(addedWater > 0);
    CHECK(land > 0);
}

TEST_CASE("setChunkUnload reports when no chunk is loaded")
{
    sf::Font font;
    int frames = 0;

    auto generator = makeGenerator(font, frames);

    // Nothing has been streamed yet, so there is no chunk to pin.
    CHECK_FALSE(generator->setChunkUnload({ 8, 8 }, false));
}

TEST_CASE("render streams chunks and shuts down cleanly")
{
#if defined(__linux__)
    // SFML aborts the process (SIGABRT) when it cannot open an X11 display,
    // which cannot be caught. Skip instead of crashing on headless machines.
    if (std::getenv("DISPLAY") == nullptr)
    {
        MESSAGE("Skipping render test: no X11 display (run under xvfb-run)");
        return;
    }
#endif

    sf::Font font;
    int frames = 0;

    auto generator = makeGenerator(font, frames);
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

    // ...and be evicted once it is released again.
    REQUIRE(generator->setChunkUnload({ 8, 8 }, true));
    for (int frame = 0; frame < 5; ++frame)
    {
        frames = frame;
        generator->render(farView, target);
    }

    bool evicted = true;
    for (const auto& line : generator->getPositionInfo({ 8, 8 }))
        if (line.rfind("Type:", 0) == 0)
            evicted = false;
    CHECK(evicted);
}

TEST_CASE("setTileColor updates the map and the rendered chunk consistently")
{
#if defined(__linux__)
    if (std::getenv("DISPLAY") == nullptr)
    {
        MESSAGE("Skipping render test: no X11 display (run under xvfb-run)");
        return;
    }
#endif

    sf::Font font;
    int frames = 0;

    auto generator = makeGenerator(font, frames);
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

    // getPositionInfo must report the stored edit rather than the raw noise.
    bool reported = false;
    for (const auto& line : generator->getPositionInfo(editedWorld))
        if (line == "Type: " + std::to_string(static_cast<int>(Elements::test)))
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

TEST_CASE("height_range caps peaks and floors the lowlands")
{
    sf::Font font;
    int frames = 0;

    // A narrow range well below the snow/mountain thresholds must remove every
    // high biome and, with a min above the ocean thresholds, flood the lowlands.
    const std::string flatConfig = makeVariantConfig("height_narrow", [](nlohmann::json& js) {
        js["island"]["enabled"] = false;
        js["river"]["enabled"] = false;
        js["height_range"]["min"] = 0.0;
        js["height_range"]["max"] = 0.3;
    });

    auto generator = std::make_unique<MapGenerator>(font, frames, flatConfig);
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
            if (isWater(*generator, { x, y })) ++water;
            if (e == Elements::muntain || e == Elements::snow) ++high;
        }
    }

    // Max 0.3 sits below the sand threshold (0.5), so no tile is above the beach.
    CHECK(high == 0);
    CHECK(water > 0);
    CHECK(total > 0);

    // Raising the floor above the ocean band turns the shallow sea into land:
    // the same seed gets strictly less water than the baseline.
    const std::string floodConfig = makeVariantConfig("height_flood", [](nlohmann::json& js) {
        js["island"]["enabled"] = false;
        js["river"]["enabled"] = false;
        js["height_range"]["min"] = 0.5;
        js["height_range"]["max"] = 1.0;
    });

    auto flooded = std::make_unique<MapGenerator>(font, frames, floodConfig);
    flooded->setSeed(2024);
    flooded->setNoises();

    int floodedWater = 0;
    for (int x = -range; x <= range; x += 3 * tileSize)
        for (int y = -range; y <= range; y += 3 * tileSize)
            if (isWater(*flooded, { x, y })) ++floodedWater;

    CHECK(floodedWater == 0);
}
