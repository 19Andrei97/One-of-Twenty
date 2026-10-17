#include <doctest/doctest.h>

#include "TileBlock.h"

#include <vector>

TEST_CASE("TileBlock::elementAt reads the row-major buffer inside the window")
{
    // A 3x3 block of distinct elements at tiles (10, 20)..(12, 22).
    std::vector<Elements> tiles{
        Elements::sand,  Elements::hill,    Elements::forest,
        Elements::lake,  Elements::snow,    Elements::clay,
        Elements::iron,  Elements::silver,  Elements::mountain,
    };
    const TileBlock block{ sf::Vector2i{ 10, 20 }, 3, &tiles };

    // Row-major: index = y * side + x.
    CHECK(block.elementAt({ 10, 20 }) == Elements::sand);
    CHECK(block.elementAt({ 11, 20 }) == Elements::hill);
    CHECK(block.elementAt({ 12, 20 }) == Elements::forest);
    CHECK(block.elementAt({ 10, 21 }) == Elements::lake);
    CHECK(block.elementAt({ 11, 21 }) == Elements::snow);
    CHECK(block.elementAt({ 12, 21 }) == Elements::clay);
    CHECK(block.elementAt({ 10, 22 }) == Elements::iron);
    CHECK(block.elementAt({ 11, 22 }) == Elements::silver);
    CHECK(block.elementAt({ 12, 22 }) == Elements::mountain);
}

TEST_CASE("TileBlock::elementAt reports deep ocean outside the window")
{
    std::vector<Elements> tiles{ Elements::hill, Elements::hill, Elements::hill, Elements::hill };
    const TileBlock block{ sf::Vector2i{ 0, 0 }, 2, &tiles };

    // Inside.
    CHECK(block.elementAt({ 0, 0 }) == Elements::hill);
    CHECK(block.elementAt({ 1, 1 }) == Elements::hill);

    // One past every edge, and a negative offset, all read as impassable water.
    CHECK(block.elementAt({ 2, 0 }) == Elements::very_deep_ocean);
    CHECK(block.elementAt({ 0, 2 }) == Elements::very_deep_ocean);
    CHECK(block.elementAt({ -1, 0 }) == Elements::very_deep_ocean);
    CHECK(block.elementAt({ 0, -1 }) == Elements::very_deep_ocean);
    CHECK(block.elementAt({ 100, 100 }) == Elements::very_deep_ocean);

    // A negative-origin window still resolves its own tiles correctly.
    const TileBlock neg{ sf::Vector2i{ -5, -5 }, 2, &tiles };
    CHECK(neg.elementAt({ -5, -5 }) == Elements::hill);
    CHECK(neg.elementAt({ -4, -4 }) == Elements::hill);
    CHECK(neg.elementAt({ -6, -5 }) == Elements::very_deep_ocean);
    CHECK(neg.elementAt({ -5, -3 }) == Elements::very_deep_ocean);
}
