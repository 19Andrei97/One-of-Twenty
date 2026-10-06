#include "pch.h"

#include "MapGenerator.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

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
}
