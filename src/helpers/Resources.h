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

    // Forageable/gatherable land: the tiles an entity can work to produce a
    // resource for the settlement.
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

    // Any tile worth remembering so an entity knows where to go: water to drink,
    // gatherables to work.
    inline constexpr bool isResource(const Elements element) noexcept
    {
        return isWater(element) || isGatherable(element);
    }
}
