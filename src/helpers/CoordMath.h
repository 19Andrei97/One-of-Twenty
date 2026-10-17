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

    // A tile size carried as a value, so a caller that already knows the grid
    // does not have to fetch the size and thread it through every conversion.
    // Every member delegates to the free functions above, which stay the single
    // source of truth for the floor-division formula.
    struct TileGrid
    {
        int tile_size{ 0 };

        [[nodiscard]] sf::Vector2i worldToTile(const sf::Vector2i& world) const
        {
            return CoordMath::worldToTile(world, tile_size);
        }

        [[nodiscard]] sf::Vector2i tileToWorld(const sf::Vector2i& tile) const
        {
            return CoordMath::tileToWorld(tile, tile_size);
        }

        // Snap a world position to the top-left corner of the tile it falls in.
        [[nodiscard]] sf::Vector2i tileCorner(const sf::Vector2i& world) const
        {
            return CoordMath::tileToWorld(CoordMath::worldToTile(world, tile_size), tile_size);
        }
    };
}
