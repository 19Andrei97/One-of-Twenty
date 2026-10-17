#pragma once

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <array>
#include <cmath>

// Small vector helpers shared by the entity code, so the distance and
// neighbour-offset arithmetic lives in one place instead of being re-derived at
// each call site. The integer results are exact; `roundedVector` is the one
// float-to-pixel rule.
namespace VectorMath
{
    // The 8-neighbourhood offsets (orthogonal first, then diagonal). Shared so
    // the collision pass and any future neighbour walk agree on the order.
    inline constexpr std::array<sf::Vector2i, 8> kNeighbours8{
        sf::Vector2i{ 1, 0 }, sf::Vector2i{ -1, 0 }, sf::Vector2i{ 0, 1 }, sf::Vector2i{ 0, -1 },
        sf::Vector2i{ 1, 1 }, sf::Vector2i{ 1, -1 }, sf::Vector2i{ -1, 1 }, sf::Vector2i{ -1, -1 }
    };

    // The 4-neighbourhood offsets (orthogonal only).
    inline constexpr std::array<sf::Vector2i, 4> kNeighbours4{
        sf::Vector2i{ 1, 0 }, sf::Vector2i{ -1, 0 }, sf::Vector2i{ 0, 1 }, sf::Vector2i{ 0, -1 }
    };

    // Squared Euclidean distance. Squared so comparisons stay integer and avoid
    // a square root in the per-candidate selection loops.
    [[nodiscard]] inline int distanceSquared(const sf::Vector2i& a, const sf::Vector2i& b) noexcept
    {
        const int dx = a.x - b.x;
        const int dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    // Chebyshev distance (the larger axis gap): the metric for "how many tiles
    // apart", where a diagonal neighbour counts as one step.
    [[nodiscard]] inline int chebyshevDistance(const sf::Vector2i& a, const sf::Vector2i& b) noexcept
    {
        return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
    }

    // Round a float vector to whole pixels, component by component. A scaled
    // direction (a normalized push, a movement step) is rounded this way at every
    // call site, so the rounding rule lives here instead of being repeated.
    [[nodiscard]] inline sf::Vector2i roundedVector(const sf::Vector2f& v) noexcept
    {
        return sf::Vector2i{ static_cast<int>(std::lround(v.x)),
                             static_cast<int>(std::lround(v.y)) };
    }
}
