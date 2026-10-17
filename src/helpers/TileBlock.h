#pragma once

#include "../map_generator/Chunk.h"

#include <SFML/System/Vector2.hpp>

#include <cstddef>
#include <vector>

// A read-only view over a dense block of tile elements, as produced by
// `MapGenerator::copyTileBlock`: the block's top-left tile, its side length and
// the row-major buffer (index `y * side + x`). It exists so a consumer that
// needs "the element at this tile, or impassable ground if it is off the
// snapshot" does not have to re-derive the bounds and index arithmetic.
struct TileBlock
{
    sf::Vector2i                top_left{ 0, 0 };
    int                         side{ 0 };
    const std::vector<Elements>* tiles{ nullptr };

    // The element at a tile, or `very_deep_ocean` (unwalkable, no resource) when
    // the tile falls outside the copied window: the search cannot wander off the
    // snapshot it was given.
    [[nodiscard]] Elements elementAt(const sf::Vector2i& tile) const
    {
        const sf::Vector2i rel = tile - top_left;
        if (rel.x < 0 || rel.y < 0 || rel.x >= side || rel.y >= side)
            return Elements::very_deep_ocean;
        return (*tiles)[static_cast<std::size_t>(rel.y) * static_cast<std::size_t>(side)
                        + static_cast<std::size_t>(rel.x)];
    }
};
