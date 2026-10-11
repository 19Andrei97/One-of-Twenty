#pragma once

#include "MapGenerator.h"

#include <SFML/System/Vector2.hpp>

#include <chrono>
#include <thread>

// Streaming support for end-to-end tests.
//
// EntityManager writes a building into the map with setTileColor, which only
// works once the tile's chunk is loaded, and chunks load only while the map is
// being streamed. MapGenerator::stream does that bookkeeping with no drawing and
// no GL, so a test can load chunks without an X11 display (the game loop calls
// render, which streams and then draws).
namespace TestSupport
{

// Stream a view around `centerWorld` until the worker threads have delivered the
// chunks. Chunks stay loaded while the view is unchanged, so this only pays the
// generation cost once.
inline bool primeChunks(MapGenerator& map, int& frames, const sf::Vector2i& centerWorld, const int halfExtentPx)
{
    const sf::IntRect view{ centerWorld - sf::Vector2i{ halfExtentPx, halfExtentPx },
                            sf::Vector2i{ halfExtentPx * 2, halfExtentPx * 2 } };

    for (int frame = 0; frame < 200; ++frame)
    {
        frames = frame;
        map.stream(view);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

} // namespace TestSupport
