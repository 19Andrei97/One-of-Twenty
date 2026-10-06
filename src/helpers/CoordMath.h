#pragma once

#include <SFML/System/Vector2.hpp>
#include <cmath>

// Conversions between world (pixel) coordinates and tile coordinates.
//
// Uses floor division rather than C++'s truncating integer division so that
// tiles left of / above the origin are numbered correctly (e.g. pixel -1
// belongs to tile -1, not tile 0).
namespace CoordMath
{
    inline sf::Vector2i worldToTile(const sf::Vector2i& world, int tileSize)
    {
        return sf::Vector2i
        {
            static_cast<int>(std::floor(static_cast<float>(world.x) / static_cast<float>(tileSize))),
            static_cast<int>(std::floor(static_cast<float>(world.y) / static_cast<float>(tileSize)))
        };
    }

    inline sf::Vector2i tileToWorld(const sf::Vector2i& tile, int tileSize)
    {
        return sf::Vector2i{ tile.x * tileSize, tile.y * tileSize };
    }
}
