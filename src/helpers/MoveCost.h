#pragma once

#include "../map_generator/Chunk.h"

// Movement cost multiplier for a terrain element. Multiplied by an entity's
// speed, so 1 means "full speed" and lower values are slower going. This is the
// single source of truth for how terrain slows movement; it is a pure function
// of the element so it can be unit-tested without a map or a renderer.
//
// Every element has a non-zero cost on purpose: a cost of 0 froze an entity in
// place (speed * 0), which made impassable-looking terrain a trap.
namespace MoveCost
{
    inline constexpr float kDefault{ 1.0f };

    // Cheapest possible entry cost. Callers that need a finite upper bound on
    // path length (or a node budget) can divide a tile distance by this.
    inline constexpr float kMin{ 0.15f };

    inline constexpr float moveCost(const Elements element) noexcept
    {
        switch (element)
        {
            // Open water is traversable but slow (wading/swimming).
            case Elements::very_deep_ocean: return 0.15f;
            case Elements::deep_ocean:      return 0.2f;
            case Elements::ocean:           return 0.3f;

            // Loose ground.
            case Elements::sand:            return 0.5f;
            case Elements::snow:            return 0.3f;

            // Uphill and dense cover.
            case Elements::hill:            return 0.6f;
            case Elements::forest:          return 0.7f;
            case Elements::mountain:         return 0.4f;

            // Workable deposits.
            case Elements::clay:            return 0.5f;
            case Elements::iron:            return 0.4f;
            case Elements::silver:          return 0.4f;

            // Built structures sit on cleared ground and are quick to cross.
            case Elements::farm:            return kDefault;
            case Elements::workshop:        return kDefault;
            case Elements::house:           return kDefault;
            case Elements::city_center:     return kDefault;

            // A road is quicker than plain ground. The catalog can override this
            // per building via Buildings::walkCost; this is the terrain fallback
            // when no catalog is consulted.
            case Elements::road:            return 1.5f;

            case Elements::test:            return kDefault;
        }
        return kDefault;
    }
}
