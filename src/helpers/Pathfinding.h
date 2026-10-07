#pragma once

#include "Chunk.h"

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

// Grid pathfinding over the terrain cost map (A*).
//
// Pure and header-only: it takes a "cost of entering this tile" callable and a
// "can I stand on this tile" callable, so it can be unit-tested on a small
// hand-written grid with no map, registry, threads or renderer. `EntityManager`
// supplies the terrain-backed callables and turns the returned tiles into
// waypoints.
namespace Pathfinding
{
    // Upper bound on expanded nodes, so a pathological or fully-blocked target
    // fails fast instead of scanning the whole world.
    inline constexpr int kDefaultNodeCap = 20000;

    // Pack a tile coordinate into one key. Built unsigned because shifting a
    // negative signed value left is undefined behaviour (UBSan flags it).
    inline std::uint64_t tileKey(const sf::Vector2i& tile) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile.x)) << 32)
             | static_cast<std::uint32_t>(tile.y);
    }

    // Octile distance with a unit diagonal weight (D = 1, D2 = 1): admissible
    // for 8-way movement whose diagonal step costs the same as a straight one,
    // so A* stays optimal.
    inline float octileHeuristic(const sf::Vector2i& a, const sf::Vector2i& b) noexcept
    {
        const float dx = std::abs(static_cast<float>(a.x - b.x));
        const float dy = std::abs(static_cast<float>(a.y - b.y));
        return std::max(dx, dy);
    }

    // A* from `start` to `goal` (both tiles), 8-way, never cutting a diagonal
    // between two blocked tiles. Returns the full tile path including both ends,
    // or an empty vector when no route exists within `nodeCap` expansions. A
    // non-walkable `start` is allowed (an entity standing in water can walk out);
    // a non-walkable `goal` is not.
    template <typename CostFn, typename WalkableFn>
    std::vector<sf::Vector2i> findPath(const sf::Vector2i& start,
                                       const sf::Vector2i& goal,
                                       CostFn cost,
                                       WalkableFn walkable,
                                       const int nodeCap = kDefaultNodeCap)
    {
        std::vector<sf::Vector2i> path;
        if (!walkable(goal))
            return path;

        path.push_back(start);
        if (start == goal)
            return path;
        path.clear();

        struct Node
        {
            sf::Vector2i tile;
            float        f;
        };
        const auto worse = [](const Node& a, const Node& b) { return a.f > b.f; };
        std::priority_queue<Node, std::vector<Node>, decltype(worse)> open(worse);

        std::unordered_map<std::uint64_t, sf::Vector2i> came_from;
        std::unordered_map<std::uint64_t, float>        g_score;

        g_score[tileKey(start)] = 0.f;
        open.push({ start, octileHeuristic(start, goal) });

        static constexpr std::array<sf::Vector2i, 8> kNeighbours{ {
            { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
            { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 },
        } };
        constexpr float kDiagonal{ 1.41421356f };

        int expanded = 0;
        bool found = false;
        while (!open.empty() && expanded < nodeCap)
        {
            const sf::Vector2i current = open.top().tile;
            open.pop();
            ++expanded;

            if (current == goal)
            {
                found = true;
                break;
            }

            const float currentG = g_score[tileKey(current)];

            for (const auto& step : kNeighbours)
            {
                const sf::Vector2i next{ current.x + step.x, current.y + step.y };
                if (!walkable(next))
                    continue;

                const bool diagonal = step.x != 0 && step.y != 0;
                // Do not slip between two blocked tiles.
                if (diagonal && (!walkable({ current.x + step.x, current.y }) ||
                                 !walkable({ current.x, current.y + step.y })))
                    continue;

                const float enter = cost(next);
                const float tentative = currentG + (diagonal ? enter * kDiagonal : enter);

                const std::uint64_t nextKey = tileKey(next);
                const auto it = g_score.find(nextKey);
                if (it != g_score.end() && tentative >= it->second)
                    continue;

                g_score[nextKey] = tentative;
                came_from[nextKey] = current;
                open.push({ next, tentative + octileHeuristic(next, goal) });
            }
        }

        if (!found)
            return {};

        sf::Vector2i node = goal;
        while (node != start)
        {
            path.push_back(node);
            const auto it = came_from.find(tileKey(node));
            if (it == came_from.end())
                return {}; // should not happen once `found` is set
            node = it->second;
        }
        path.push_back(start);
        std::reverse(path.begin(), path.end());
        return path;
    }
} // namespace Pathfinding
