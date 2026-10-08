#include "Chunk.h"
#include "MoveCost.h"

#include <doctest/doctest.h>

#include <array>

// MoveCost is a pure function of the terrain element, so it needs no map.

TEST_CASE("every element has a positive, finite move cost")
{
    // A cost of 0 multiplies an entity's speed to 0 and freezes it, so nothing
    // in the enum may map to 0 (or to a negative / NaN multiplier). A road is a
    // deliberate speed-up (cost > 1) over bare ground.
    for (int i = 0; i <= static_cast<int>(Elements::test); ++i)
    {
        const auto element = static_cast<Elements>(i);
        const float cost = MoveCost::moveCost(element);
        CHECK(cost > 0.f);
        CHECK(cost == cost);          // not NaN
        if (element == Elements::road)
            CHECK(cost > 1.0f);       // a road is faster than open ground
        else
            CHECK(cost <= 1.0f);      // nothing else is a speed-up
    }
}

TEST_CASE("open ground is faster than rough or wet terrain")
{
    CHECK(MoveCost::moveCost(Elements::test) == doctest::Approx(MoveCost::kDefault));
    CHECK(MoveCost::moveCost(Elements::forest) < MoveCost::kDefault);
    CHECK(MoveCost::moveCost(Elements::hill) < MoveCost::moveCost(Elements::forest));
    CHECK(MoveCost::moveCost(Elements::ocean) < MoveCost::moveCost(Elements::sand));
    CHECK(MoveCost::moveCost(Elements::very_deep_ocean) < MoveCost::moveCost(Elements::ocean));
}

TEST_CASE("move cost is stable for a given element")
{
    CHECK(MoveCost::moveCost(Elements::forest) == MoveCost::moveCost(Elements::forest));
    CHECK(MoveCost::moveCost(Elements::silver) == doctest::Approx(0.4f));
}
