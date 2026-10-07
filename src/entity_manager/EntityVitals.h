#pragma once

#include "Components_Entities.h"

#include <algorithm>

// Pure rules for entity life and death. Kept out of EntityManager (which owns
// the registry, the clock and the stockpile) so the "should this entity die"
// and "is it comfortable enough to breed" logic can be unit-tested on its own,
// without a map, a registry or a renderer.
namespace EntityVitals
{

// Fullness counters run 100 (comfortable) down to 0 (dire), so a value at or
// below the lethal threshold means starvation or dehydration.
[[nodiscard]] inline bool isStarving(const CBasicNeeds& needs, const int lethal_threshold) noexcept
{
    return needs.thirst <= lethal_threshold || needs.hunger <= lethal_threshold;
}

[[nodiscard]] inline bool isAged(const CLifespan& lifespan) noexcept
{
    return lifespan.remaining <= 0;
}

// How comfortable an entity is overall: 1 when every need is satisfied, 0 when
// any need is at its worst. Sleep is a fatigue counter, so it counts as
// (kMax - sleep). Reproduction is gated on this.
[[nodiscard]] inline float comfort(const CBasicNeeds& needs) noexcept
{
    const int rested = CBasicNeeds::kMax - needs.sleep;
    const int worst = std::min({ needs.thirst, needs.hunger, rested });
    return std::clamp(static_cast<float>(worst) / static_cast<float>(CBasicNeeds::kMax), 0.f, 1.f);
}

} // namespace EntityVitals
