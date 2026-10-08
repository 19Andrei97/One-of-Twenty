#pragma once

#include "Goods.h"
#include "Jobs.h"

#include <array>
#include <cstddef>
#include <vector>

// The production side of the economy, as pure functions over a `Goods::Stock`.
// Buildings run recipes: a farm grows food with no raw input, a workshop turns
// wood into planks and iron into tools. Job assignment is a pure function of the
// desired staffing and what is actually staffed, so "responds to shortages" can
// be tested without a map or a registry.
namespace Economy
{
// What a placed building does. `None` is a gatherable tile that has not been
// built on.
enum class Building
{
    None = 0,
    Farm,
    Workshop,

    Count
};

[[nodiscard]] inline std::string buildingName(const Building building)
{
    switch (building)
    {
        case Building::Farm:     return "Farm";
        case Building::Workshop: return "Workshop";
        default:                 return "Site";
    }
}

// The recipes a building can run, in preference order: the first one whose
// inputs are available is the one applied. A farm's recipe has no input (pure
// labour); a workshop spends the most valuable material first.
[[nodiscard]] inline const std::vector<Goods::Recipe>& recipesFor(const Building building)
{
    static const std::vector<Goods::Recipe> kFarm{
        { Goods::Good::Wood, 0, Goods::Good::Food, 1, "farm" },
    };
    static const std::vector<Goods::Recipe> kWorkshop{
        { Goods::Good::Iron, 1, Goods::Good::Tools, 1, "iron->tools" },
        { Goods::Good::Wood, 2, Goods::Good::Planks, 1, "wood->planks" },
    };
    static const std::vector<Goods::Recipe> kNone{};

    switch (building)
    {
        case Building::Farm:     return kFarm;
        case Building::Workshop: return kWorkshop;
        default:                 return kNone;
    }
}

// Run a building once: the first recipe whose inputs are available is applied.
// Returns false when nothing could be made (empty workshop, unknown building).
[[nodiscard]] inline bool produceOnce(Goods::Stock& stock, const Building building)
{
    for (const auto& recipe : recipesFor(building))
    {
        if (Goods::applyRecipe(stock, recipe))
            return true;
    }
    return false;
}

// What one meal costs and restores. A meal draws this much food from the store;
// when the store is empty the entity forages the tile directly instead.
inline constexpr int kMealFoodCost{ 1 };

// Staffing: how many entities should be in each job. Job 0 (Idle) is unused.
using Staffing = Jobs::JobTargets;

// The most understaffed job, given the targets and the current counts: the job
// with the largest positive (target - current) gap, ties broken by enum order.
// Returns Idle when every target is met. This is the "responds to shortages"
// rule in one place.
[[nodiscard]] inline Jobs::Job assignJob(const Jobs::JobTargets& targets,
                                         const std::array<int, Jobs::kJobCount>& current)
{
    Jobs::Job best = Jobs::Job::Idle;
    int bestGap = 0;

    for (std::size_t i = 1; i < Jobs::kJobCount; ++i)
    {
        const int gap = targets[i] - current[i];
        if (gap > bestGap)
        {
            bestGap = gap;
            best = static_cast<Jobs::Job>(i);
        }
    }
    return best;
}

} // namespace Economy
