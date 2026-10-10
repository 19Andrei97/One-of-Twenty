#pragma once

#include "Components_Entities.h"

#include <algorithm>

// Pure rules for entity life and death. Kept out of EntityManager (which owns
// the registry, the clock and the stockpile) so the "should this entity die"
// and "is it comfortable enough to breed" logic can be unit-tested on its own,
// without a map, a registry or a renderer.
namespace EntityVitals
{

// A day ended without food and the entity has now gone hungry for the lethal
// number of consecutive days. `days_without_food` counts the streak, so a
// single missed meal is harmless and a run of them drains health.
[[nodiscard]] inline bool isStarving(const CBasicNeeds& needs, const int lethal_days) noexcept
{
    return needs.days_without_food >= lethal_days;
}

[[nodiscard]] inline bool isAged(const CLifespan& lifespan) noexcept
{
    return lifespan.remaining <= 0;
}

// How comfortable an entity is overall: 1 when rested, 0 when exhausted. Sleep
// is the only need left, so comfort is just the rested fraction. Reproduction is
// gated on this.
[[nodiscard]] inline float comfort(const CBasicNeeds& needs) noexcept
{
    const int rested = CBasicNeeds::kMax - needs.sleep;
    const int worst = rested;
    return std::clamp(static_cast<float>(worst) / static_cast<float>(CBasicNeeds::kMax), 0.f, 1.f);
}

// How health moves over one in-game hour: draining while starved, slowly
// recovering once the entity is rested again, and holding steady in between.
// Returning a signed delta keeps the rule pure and easy to test.
[[nodiscard]] inline int healthChange(const CBasicNeeds& needs,
                                      const int lethal_days,
                                      const int damage_per_hour,
                                      const int regen_per_hour) noexcept
{
    if (isStarving(needs, lethal_days))
        return -damage_per_hour;
    if (needs.rested())
        return regen_per_hour;
    return 0;
}

} // namespace EntityVitals
