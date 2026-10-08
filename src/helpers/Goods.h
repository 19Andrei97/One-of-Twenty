#pragma once

#include "../map_generator/Chunk.h"
#include "Jobs.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

// The settlement's stores, expressed as goods rather than raw terrain. A tile
// yields a raw good (wood from forest, stone from hill, ...); a building runs a
// recipe that turns raw goods into something else. Keeping this a small, pure
// value type means the economy can be reasoned about and unit-tested without a
// map, a registry or a clock.
namespace Goods
{
enum class Good
{
    Food,
    Wood,
    Stone,
    Clay,
    Iron,
    Silver,

    Planks,     // crafted: wood -> planks
    Tools,      // crafted: iron -> tools

    Count
};

inline constexpr std::size_t kGoodCount = static_cast<std::size_t>(Good::Count);

[[nodiscard]] inline constexpr std::size_t index(const Good good) noexcept
{
    return static_cast<std::size_t>(good);
}

[[nodiscard]] inline std::string name(const Good good)
{
    switch (good)
    {
        case Good::Food:   return "Food";
        case Good::Wood:   return "Wood";
        case Good::Stone:  return "Stone";
        case Good::Clay:   return "Clay";
        case Good::Iron:   return "Iron";
        case Good::Silver: return "Silver";
        case Good::Planks: return "Planks";
        case Good::Tools:  return "Tools";
        default:           return "?";
    }
}

// The raw good a gatherable terrain element yields. Returns Wood for anything
// that is not a known raw deposit, so a caller never has to special-case a
// building tile (those are handled by the recipes, not here).
[[nodiscard]] inline Good fromElement(const Elements element) noexcept
{
    switch (element)
    {
        case Elements::forest: return Good::Wood;
        case Elements::hill:   return Good::Stone;
        case Elements::clay:   return Good::Clay;
        case Elements::iron:   return Good::Iron;
        case Elements::silver: return Good::Silver;
        default:               return Good::Wood;
    }
}

// The settlement's stock, one counter per good. A plain value type so it can be
// copied, snapshotted (for deltas over time) and tested directly.
struct Stock
{
    std::array<int, kGoodCount> amount{};

    [[nodiscard]] int count(const Good good) const noexcept { return amount[index(good)]; }

    void add(const Good good, const int quantity) noexcept
    {
        amount[index(good)] = std::max(0, amount[index(good)] + quantity);
    }

    // Remove up to `quantity`, returning how many were actually taken (so a
    // production step can tell whether it had enough inputs).
    int take(const Good good, const int quantity) noexcept
    {
        const int taken = std::min(amount[index(good)], std::max(0, quantity));
        amount[index(good)] -= taken;
        return taken;
    }

    [[nodiscard]] bool has(const Good good, const int quantity) const noexcept
    {
        return amount[index(good)] >= quantity;
    }

    [[nodiscard]] int total() const noexcept
    {
        int total = 0;
        for (const int quantity : amount)
            total += quantity;
        return total;
    }

    // Total held of the raw materials (everything except the crafted goods).
    [[nodiscard]] int rawTotal() const noexcept
    {
        int total = 0;
        for (std::size_t i = 0; i < index(Good::Planks); ++i)
            total += amount[i];
        return total;
    }
};

// The raw good a job prefers to gather. A farmer values forage (food), a
// lumberjack wood, a miner ore (rarest first) and a builder wood and stone.
// `rank` is the job's preference order: 0 is most preferred. Returns -1 when the
// job has no preference for the element, so a caller can fall through to a
// default.
[[nodiscard]] inline int jobPreference(const Jobs::Job job, const Elements element) noexcept
{
    switch (job)
    {
        case Jobs::Job::Farmer:
            if (element == Elements::forest) return 0;      // forage
            if (element == Elements::hill)   return 1;
            return -1;

        case Jobs::Job::Lumberjack:
            if (element == Elements::forest) return 0;
            return -1;

        case Jobs::Job::Miner:
            if (element == Elements::silver) return 0;
            if (element == Elements::iron)   return 1;
            if (element == Elements::clay)   return 2;
            return -1;

        case Jobs::Job::Builder:
            if (element == Elements::forest) return 0;      // wood
            if (element == Elements::hill)   return 1;      // stone
            return -1;

        default:
            return -1;
    }
}

// One production step: spend `input` of `input_good` and gain `output` of
// `output_good`. A zero input means the step is pure labour (a farm grows food
// with no raw material).
struct Recipe
{
    Good input_good{ Good::Wood };
    int  input_amount{ 0 };
    Good output_good{ Good::Planks };
    int  output_amount{ 1 };
    const char* label{ "recipe" };
};

// Apply a recipe to a stock. Returns false (and changes nothing) when the inputs
// are not available, so a caller can fall back to a cheaper recipe.
[[nodiscard]] inline bool applyRecipe(Stock& stock, const Recipe& recipe) noexcept
{
    if (recipe.input_amount > 0 && !stock.has(recipe.input_good, recipe.input_amount))
        return false;

    stock.take(recipe.input_good, recipe.input_amount);
    stock.add(recipe.output_good, recipe.output_amount);
    return true;
}

// Food spoils a little every in-game day, so a store only grows by out-producing
// the loss. Integer percentages keep it deterministic and testable.
inline void spoil(Stock& stock, const int percent_per_day) noexcept
{
    if (percent_per_day <= 0)
        return;
    const int food = stock.count(Good::Food);
    const int lost = food * percent_per_day / 100;
    if (lost > 0)
        stock.take(Good::Food, lost);
}

} // namespace Goods
