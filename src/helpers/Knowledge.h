#pragma once

#include "Chunk.h"
#include "Resources.h"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The civilization's shared map knowledge.
//
// Knowledge belongs to the settlement, not to an individual: every entity that
// walks somewhere observes its surroundings through the same store, so a
// resource found by one is known to all. This replaces the per-entity memory
// map, which duplicated the same tile results for every entity and grew with
// the population. The store stays small regardless of how many entities exist.
//
// Two things are tracked:
//  * `m_tiles` - the known resource tiles, bucketed by element. Queries pick the
//    nearest to the asking entity, so an entity never treks to a far tile just
//    because it was discovered first. Deduplicated and capped, so repeated
//    observations of the same area add nothing.
//  * `m_known_cells` - which coarse cells of the world have been observed. Full
//    per-tile coverage would grow without bound, so coverage is bucketed into
//    `m_cell_size`-tile cells. This is enough to measure how much of the map the
//    settlement has seen and to point an explorer at the frontier.
class CivKnowledge
{
public:
    // Named kinds of remembered location, so callers do not pass raw predicates.
    enum class Kind
    {
        Water,
        Food
    };

    explicit CivKnowledge(int cell_size = 8, std::size_t max_cells = 65536,
                          std::size_t max_tiles = 200000)
        : m_max_tiles(max_tiles)
        , m_cell_size(std::max(1, cell_size))
        , m_max_cells(max_cells)
    {}

    // Merge an observation into the knowledge: the closest tile of each element
    // (already reduced by the map scan) is remembered, and the coarse cells
    // around the observed tile are marked explored.
    void remember(const std::unordered_map<Elements, sf::Vector2i>& found)
    {
        for (const auto& [element, pos] : found)
        {
            if (m_tile_set.size() >= m_max_tiles)
                return;
            if (!m_tile_set.insert(pos).second)
                continue; // already known, do not spend capacity twice
            m_tiles[element].push_back(pos);
        }
    }

    // Mark every tile within `radius` of `center` as observed, bucketed into
    // cells. Bounded: once `m_max_cells` cells are known, further observations
    // are ignored rather than letting the store grow without limit.
    void observe(const sf::Vector2i& center, int radius)
    {
        if (m_max_cells == 0 || m_known_cells.size() >= m_max_cells)
            return;

        const sf::Vector2i min = cellOf(center) - sf::Vector2i{ radius / m_cell_size + 1,
                                                               radius / m_cell_size + 1 };
        const sf::Vector2i max = cellOf(center) + sf::Vector2i{ radius / m_cell_size + 1,
                                                               radius / m_cell_size + 1 };
        for (int x = min.x; x <= max.x; ++x)
            for (int y = min.y; y <= max.y; ++y)
            {
                if (m_known_cells.size() >= m_max_cells)
                    return;
                m_known_cells.insert(sf::Vector2i{ x, y });
            }
    }

    // Closest remembered location of the given kind, if any. Scans the tiles of
    // every element that matches the kind, picking the nearest to `from`.
    [[nodiscard]] std::optional<sf::Vector2i> findNearest(const sf::Vector2i& from, const Kind kind) const
    {
        std::optional<sf::Vector2i> best;
        int bestDist = 0;

        for (const auto& [element, positions] : m_tiles)
        {
            const bool usable = (kind == Kind::Water) ? Resources::isWater(element)
                                                      : Resources::isFood(element);
            if (!usable)
                continue;

            for (const auto& pos : positions)
            {
                const int dist = squaredDistance(pos, from);
                if (!best || dist < bestDist)
                {
                    best = pos;
                    bestDist = dist;
                }
            }
        }
        return best;
    }

    // Closest remembered tile of one element, if any.
    [[nodiscard]] std::optional<sf::Vector2i> location(const Elements element,
                                                       const sf::Vector2i& from) const
    {
        const auto it = m_tiles.find(element);
        if (it == m_tiles.end())
            return std::nullopt;

        std::optional<sf::Vector2i> best;
        int bestDist = 0;
        for (const auto& pos : it->second)
        {
            const int dist = squaredDistance(pos, from);
            if (!best || dist < bestDist)
            {
                best = pos;
                bestDist = dist;
            }
        }
        return best;
    }

    // Whether the coarse cell containing `tile` has been observed.
    [[nodiscard]] bool isExplored(const sf::Vector2i& tile) const
    {
        return m_known_cells.find(cellOf(tile)) != m_known_cells.end();
    }

    // Number of explored coarse cells, and the number of known resource tiles.
    // Together these are the "how much does the civilization know" readout for
    // the HUD.
    [[nodiscard]] std::size_t exploredCells() const noexcept { return m_known_cells.size(); }
    [[nodiscard]] std::size_t knownLocations() const noexcept { return m_tile_set.size(); }
    [[nodiscard]] int cellSize() const noexcept { return m_cell_size; }

    // Nearest unexplored cell to `from`, searched ring by ring out to
    // `max_rings`. This is the explorer's frontier: the closest edge of the known
    // world. Returns nullopt when everything within reach is already explored.
    [[nodiscard]] std::optional<sf::Vector2i> nearestFrontier(const sf::Vector2i& from,
                                                              int max_rings = 12) const
    {
        // Always a cell coordinate, never a tile: the caller scales it back to
        // tile/world space. When the cell underfoot is itself unknown, that cell
        // is the frontier.
        const sf::Vector2i origin = cellOf(from);
        if (m_known_cells.find(origin) == m_known_cells.end())
            return origin;

        for (int ring = 1; ring <= max_rings; ++ring)
        {
            for (int dx = -ring; dx <= ring; ++dx)
                for (int dy = -ring; dy <= ring; ++dy)
                {
                    if (std::max(std::abs(dx), std::abs(dy)) != ring)
                        continue;
                    const sf::Vector2i cell{ origin.x + dx, origin.y + dy };
                    if (m_known_cells.find(cell) == m_known_cells.end())
                        return cell;
                }
        }
        return std::nullopt;
    }

    // Known resource tiles bucketed by element, for callers that rank by job
    // preference rather than proximity.
    const std::unordered_map<Elements, std::vector<sf::Vector2i>>& tiles() const noexcept { return m_tiles; }

private:
    [[nodiscard]] static int squaredDistance(const sf::Vector2i& a, const sf::Vector2i& b) noexcept
    {
        const int dx = a.x - b.x;
        const int dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    // Cell coordinate that contains a tile. Floors toward negative infinity, so
    // tiles left of / above the origin land in the correct cell.
    [[nodiscard]] sf::Vector2i cellOf(const sf::Vector2i& tile) const noexcept
    {
        const auto floorDiv = [this](const int value) {
            return (value >= 0) ? value / m_cell_size
                                : -(((-value) + m_cell_size - 1) / m_cell_size);
        };
        return sf::Vector2i{ floorDiv(tile.x), floorDiv(tile.y) };
    }

    std::unordered_map<Elements, std::vector<sf::Vector2i>> m_tiles;
    std::unordered_set<sf::Vector2i, Vector2iHash>          m_tile_set;
    std::unordered_set<sf::Vector2i, Vector2iHash>          m_known_cells;
    std::size_t                                             m_max_tiles{ 200000 };
    int                                                     m_cell_size{ 8 };
    std::size_t                                             m_max_cells{ 65536 };
};
