#pragma once

#include "MapGenerator.h"

#include <SFML/Graphics.hpp>

#include <chrono>
#include <cstdlib>
#include <thread>

// Streaming support for end-to-end tests.
//
// EntityManager writes a building into the map with setTileColor, which only
// works once the tile's chunk is loaded, and chunks load only when the renderer
// runs. The game loop calls MapGenerator::render every frame; a test that drives
// the real update loop must do the same or its settlement can never found a farm.
namespace TestSupport
{

// Becomes true after the camera view has been rendered enough times for the
// worker threads to deliver the chunks. Used to stop paying the render cost once
// the neighbourhood is loaded (chunks stay loaded while the view is unchanged).
inline bool primeChunks(MapGenerator& map, int& frames, const sf::Vector2i& centerWorld, const int halfExtentPx)
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

    const sf::IntRect view{ centerWorld - sf::Vector2i{ halfExtentPx, halfExtentPx },
                            sf::Vector2i{ halfExtentPx * 2, halfExtentPx * 2 } };

    for (int frame = 0; frame < 200; ++frame)
    {
        frames = frame;
        target.clear();
        map.render(view, target);
        target.display();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

} // namespace TestSupport
