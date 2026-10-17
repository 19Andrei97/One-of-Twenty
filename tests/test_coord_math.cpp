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

TEST_CASE("TileGrid delegates to the free conversions")
{
    const CoordMath::TileGrid grid{ 16 };

    CHECK(grid.worldToTile({ 0, 0 }) == sf::Vector2i{ 0, 0 });
    CHECK(grid.worldToTile({ -1, -1 }) == sf::Vector2i{ -1, -1 });
    CHECK(grid.worldToTile({ 31, 31 }) == sf::Vector2i{ 1, 1 });
    CHECK(grid.tileToWorld({ 3, 4 }) == sf::Vector2i{ 48, 64 });
    CHECK(grid.tileToWorld({ -2, -2 }) == sf::Vector2i{ -32, -32 });

    for (int t = -8; t <= 8; ++t)
    {
        const sf::Vector2i tile{ t, -t };
        CHECK(grid.worldToTile(grid.tileToWorld(tile)) == tile);
    }
}

TEST_CASE("TileGrid::tileCorner snaps a world position to its tile")
{
    const CoordMath::TileGrid grid{ 16 };

    // Already on a corner: unchanged.
    CHECK(grid.tileCorner({ 0, 0 }) == sf::Vector2i{ 0, 0 });
    CHECK(grid.tileCorner({ 32, -48 }) == sf::Vector2i{ 32, -48 });

    // Interior and negative positions snap down (floor), matching worldToTile.
    CHECK(grid.tileCorner({ 20, 20 }) == sf::Vector2i{ 16, 16 });
    CHECK(grid.tileCorner({ -1, -1 }) == sf::Vector2i{ -16, -16 });
    CHECK(grid.tileCorner({ -17, 5 }) == sf::Vector2i{ -32, 0 });

    // The corner is always the top-left of the tile worldToTile reports.
    for (int x = -40; x <= 40; x += 7)
    {
        for (int y = -40; y <= 40; y += 7)
        {
            const sf::Vector2i world{ x, y };
            CHECK(grid.worldToTile(grid.tileCorner(world)) == grid.worldToTile(world));
        }
    }
}
