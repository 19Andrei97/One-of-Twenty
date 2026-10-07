#pragma once

#include "../map_generator/Chunk.h"

// What a terrain element is usable for. Kept in one place so memory gathering
// (MapGenerator), memory queries (CMemory) and the decision policy agree on
// which tiles are drinkable or worth gathering.
namespace Resources
{
    // Drinkable: only the surface ocean. Deep water is not reachable on foot.
    inline constexpr bool isWater(const Elements element) noexcept
    {
        return element == Elements::ocean;
    }

    // Edible land: the tiles an entity forages to satisfy hunger. Kept apart
    // from the workable deposits below so eating draws from food, not ore.
    inline constexpr bool isFood(const Elements element) noexcept
    {
        return element == Elements::forest || element == Elements::hill;
    }

    // Workable land: the tiles an entity can gather to produce a settlement
    // resource.
    inline constexpr bool isGatherable(const Elements element) noexcept
    {
        switch (element)
        {
            case Elements::hill:    // stone
            case Elements::forest:  // wood
            case Elements::clay:    // clay
            case Elements::iron:    // iron
            case Elements::silver:  // silver
                return true;
            default:
                return false;
        }
    }

    // Consumable: what eating and drinking withdraw from the settlement stores.
    // Water is drunk straight from the tile; food is eaten from the stockpile.
    inline constexpr bool isConsumable(const Elements element) noexcept
    {
        return isWater(element) || isFood(element);
    }

    // Any tile worth remembering so an entity knows where to go: water to drink,
    // food to eat, gatherables to work.
    inline constexpr bool isResource(const Elements element) noexcept
    {
        return isConsumable(element) || isGatherable(element);
    }
}
