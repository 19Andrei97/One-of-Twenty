#pragma once

#include "../map_generator/Chunk.h"

// What a terrain element is usable for. Kept in one place so map scanning
// (MapGenerator), shared knowledge (CivKnowledge) and the decision policy agree
// on which tiles are drinkable or worth gathering.
namespace Resources
{
    // Drinkable: only the surface ocean. Deep water is not reachable on foot.
    inline constexpr bool isWater(const Elements element) noexcept
    {
        return element == Elements::ocean;
    }

    // Any water, including the deep ocean an entity cannot stand in. Used by
    // collision and pathing to keep entities out of the sea.
    inline constexpr bool isOcean(const Elements element) noexcept
    {
        return element == Elements::ocean
            || element == Elements::deep_ocean
            || element == Elements::very_deep_ocean;
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

    // A placed building. Buildings run production recipes or apply an effect
    // rather than being gathered, so they are classified apart from the workable
    // deposits.
    inline constexpr bool isBuilding(const Elements element) noexcept
    {
        return element == Elements::farm || element == Elements::workshop
            || element == Elements::house || element == Elements::city_center
            || element == Elements::road;
    }

    // A road: a walkable building whose whole purpose is to speed movement.
    inline constexpr bool isRoad(const Elements element) noexcept
    {
        return element == Elements::road;
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

    // Short, stable label for an element, for the event log and HUD. Not a
    // display name: keep it greppable and independent of any locale.
    [[nodiscard]] inline const char* name(const Elements element) noexcept
    {
        switch (element)
        {
            case Elements::very_deep_ocean: return "very_deep_ocean";
            case Elements::deep_ocean:      return "deep_ocean";
            case Elements::ocean:           return "ocean";
            case Elements::sand:            return "sand";
            case Elements::hill:            return "stone";
            case Elements::forest:          return "forest";
            case Elements::mountain:        return "mountain";
            case Elements::snow:            return "snow";
            case Elements::clay:            return "clay";
            case Elements::iron:            return "iron";
            case Elements::silver:          return "silver";
            case Elements::farm:            return "farm";
            case Elements::workshop:        return "workshop";
            case Elements::house:           return "house";
            case Elements::city_center:     return "city_center";
            case Elements::road:            return "road";
            case Elements::test:            return "test";
            default:                        return "unknown";
        }
    }

    // Human-readable label for an element, for the tile info readout. Unlike
    // name(), this is a display string ("Hill", not the "stone" resource id) and
    // is safe to reword without breaking the event log or saves.
    [[nodiscard]] inline const char* displayName(const Elements element) noexcept
    {
        switch (element)
        {
            case Elements::very_deep_ocean: return "Very Deep Ocean";
            case Elements::deep_ocean:      return "Deep Ocean";
            case Elements::ocean:           return "Ocean";
            case Elements::sand:            return "Sand";
            case Elements::hill:            return "Hill";
            case Elements::forest:          return "Forest";
            case Elements::mountain:        return "Mountain";
            case Elements::snow:            return "Snow";
            case Elements::clay:            return "Clay";
            case Elements::iron:            return "Iron";
            case Elements::silver:          return "Silver";
            case Elements::farm:            return "Farm";
            case Elements::workshop:        return "Workshop";
            case Elements::house:           return "House";
            case Elements::city_center:     return "City Center";
            case Elements::road:            return "Road";
            case Elements::test:            return "Test";
            default:                        return "Unknown";
        }
    }
}
