#include <doctest/doctest.h>

#include "VectorMath.h"

TEST_CASE("distanceSquared matches the hand-computed square")
{
    CHECK(VectorMath::distanceSquared({ 0, 0 }, { 0, 0 }) == 0);
    CHECK(VectorMath::distanceSquared({ 0, 0 }, { 3, 4 }) == 25);
    CHECK(VectorMath::distanceSquared({ 3, 4 }, { 0, 0 }) == 25);
    CHECK(VectorMath::distanceSquared({ -1, -1 }, { 2, 3 }) == 25);
    CHECK(VectorMath::distanceSquared({ 10, 10 }, { 10, 13 }) == 9);

    // Symmetric.
    CHECK(VectorMath::distanceSquared({ 5, -7 }, { -2, 4 })
          == VectorMath::distanceSquared({ -2, 4 }, { 5, -7 }));
}

TEST_CASE("chebyshevDistance is the larger axis gap")
{
    CHECK(VectorMath::chebyshevDistance({ 0, 0 }, { 0, 0 }) == 0);
    CHECK(VectorMath::chebyshevDistance({ 0, 0 }, { 3, 4 }) == 4);
    // A diagonal neighbour is one step, unlike Euclidean distance.
    CHECK(VectorMath::chebyshevDistance({ 0, 0 }, { 1, 1 }) == 1);
    CHECK(VectorMath::chebyshevDistance({ -5, 2 }, { 3, -4 }) == 8);
}

TEST_CASE("roundedVector rounds each component to whole pixels")
{
    CHECK(VectorMath::roundedVector(sf::Vector2f{ 0.f, 0.f }) == sf::Vector2i{ 0, 0 });
    CHECK(VectorMath::roundedVector(sf::Vector2f{ 1.4f, 2.6f }) == sf::Vector2i{ 1, 3 });
    // std::lround rounds half away from zero: -2.5 -> -3.
    CHECK(VectorMath::roundedVector(sf::Vector2f{ -1.5f, -2.5f }) == sf::Vector2i{ -2, -3 });
    CHECK(VectorMath::roundedVector(sf::Vector2f{ 0.5f, -0.5f }) == sf::Vector2i{ 1, -1 });
    CHECK(VectorMath::roundedVector(sf::Vector2f{ 7.9f, -7.9f }) == sf::Vector2i{ 8, -8 });
}

TEST_CASE("the neighbour tables hold the expected offsets")
{
    // The 4-neighbourhood is orthogonal only, no duplicates.
    CHECK(VectorMath::kNeighbours4.size() == 4);
    for (const auto& d : VectorMath::kNeighbours4)
        CHECK(VectorMath::chebyshevDistance({ 0, 0 }, d) == 1);
    CHECK(VectorMath::distanceSquared({ 0, 0 }, VectorMath::kNeighbours4[0]) == 1);

    // The 8-neighbourhood adds the four diagonals, all distinct.
    CHECK(VectorMath::kNeighbours8.size() == 8);
    for (std::size_t i = 0; i < VectorMath::kNeighbours8.size(); ++i)
    {
        CHECK(VectorMath::chebyshevDistance({ 0, 0 }, VectorMath::kNeighbours8[i]) == 1);
        for (std::size_t j = i + 1; j < VectorMath::kNeighbours8.size(); ++j)
            CHECK_FALSE(VectorMath::kNeighbours8[i] == VectorMath::kNeighbours8[j]);
    }
}
