#include <doctest/doctest.h>

#include "CoordMath.h"

TEST_CASE("worldToTile floors toward negative infinity")
{
    CHECK(CoordMath::worldToTile({ 0, 0 }, 16) == sf::Vector2i{ 0, 0 });
    CHECK(CoordMath::worldToTile({ 15, 15 }, 16) == sf::Vector2i{ 0, 0 });
    CHECK(CoordMath::worldToTile({ 16, 16 }, 16) == sf::Vector2i{ 1, 1 });
    CHECK(CoordMath::worldToTile({ 31, 31 }, 16) == sf::Vector2i{ 1, 1 });

    // C++'s truncating division would wrongly map these to tile 0.
    CHECK(CoordMath::worldToTile({ -1, -1 }, 16) == sf::Vector2i{ -1, -1 });
    CHECK(CoordMath::worldToTile({ -16, -16 }, 16) == sf::Vector2i{ -1, -1 });
    CHECK(CoordMath::worldToTile({ -17, -17 }, 16) == sf::Vector2i{ -2, -2 });
}

TEST_CASE("tileToWorld scales by tile size")
{
    CHECK(CoordMath::tileToWorld({ 0, 0 }, 16) == sf::Vector2i{ 0, 0 });
    CHECK(CoordMath::tileToWorld({ 3, 4 }, 16) == sf::Vector2i{ 48, 64 });
    CHECK(CoordMath::tileToWorld({ -2, -2 }, 16) == sf::Vector2i{ -32, -32 });
}

TEST_CASE("tileToWorld is a left inverse of worldToTile")
{
    for (int t = -8; t <= 8; ++t)
    {
        const sf::Vector2i tile{ t, t };
        CHECK(CoordMath::worldToTile(CoordMath::tileToWorld(tile, 16), 16) == tile);
    }
}
