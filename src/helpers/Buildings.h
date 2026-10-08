#pragma once

#include "Chunk.h"
#include "Config.h"
#include "Goods.h"
#include "MoveCost.h"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

// The building catalog: the single, data-driven source of truth for what a
// building costs, how it looks, what it does once finished and how it changes
// movement. Adding a new building is one `Elements` value plus one entry in
// config/buildings.json - no new switch arm here.
//
// This is a pure value type (no map, registry, clock or renderer), so the
// catalog and its helpers can be unit-tested directly, mirroring Goods.h and
// Jobs.h.
namespace Buildings
{

// One ingredient in a building's cost: a good and how much of it is spent from
// the settlement stockpile when construction is planned.
struct Cost
{
    Goods::Good good{ Goods::Good::Wood };
    int          amount{ 0 };
};

// A building definition.
struct Def
{
    std::string   id;                    // stable key, e.g. "city_center"
    std::string   name;                  // display name, e.g. "City Center"
    Elements      element{ Elements::test };   // the tile written into the map
    sf::Color     color{ sf::Color::White };   // how that tile is drawn

    std::vector<Cost> costs;             // goods spent to start construction

    sf::Vector2i  footprint{ 1, 1 };     // reserved tiles (1x1 for now)
    int           build_hours{ 0 };      // construction time once a builder works it
    int           work_per_hour{ 1 };    // work applied per in-game hour of building
    float         walk_cost{ 0.f };      // >0 overrides MoveCost for this element
    int           population_capacity{ 0 };// added to the cap once complete
    bool          is_anchor{ false };    // the city center: the settlement anchor
    int           priority{ 100 };       // lower builds first
    int           max_count{ 0 };        // 0 = unlimited
    std::vector<Goods::Recipe> recipes;  // production once complete
};

// Parsed `settlement` block: settlement-wide construction tuning.
struct Settlement
{
    int max_concurrent_sites{ 4 };
    int build_radius_tiles{ 24 };
    int road_cadence_hours{ 24 };
};

// Parse an element name (the task's stable ids) into an `Elements`. Throws
// std::runtime_error on an unknown name, so a typo in the catalog fails loudly
// rather than silently mapping to some default tile.
[[nodiscard]] inline Elements elementFromString(const std::string& name)
{
    if (name == "very_deep_ocean") return Elements::very_deep_ocean;
    if (name == "deep_ocean")      return Elements::deep_ocean;
    if (name == "ocean")           return Elements::ocean;
    if (name == "sand")            return Elements::sand;
    if (name == "hill")            return Elements::hill;
    if (name == "forest")          return Elements::forest;
    if (name == "mountain")        return Elements::mountain;
    if (name == "snow")            return Elements::snow;
    if (name == "clay")            return Elements::clay;
    if (name == "iron")            return Elements::iron;
    if (name == "silver")          return Elements::silver;
    if (name == "lake")            return Elements::lake;
    if (name == "river")           return Elements::river;
    if (name == "farm")            return Elements::farm;
    if (name == "workshop")        return Elements::workshop;
    if (name == "house")           return Elements::house;
    if (name == "city_center")     return Elements::city_center;
    if (name == "road")            return Elements::road;
    if (name == "test")            return Elements::test;

    throw std::runtime_error("Unknown building element: '" + name + "'");
}

// Parse a good name (lower-case) into a `Goods::Good`. Throws on an unknown name.
[[nodiscard]] inline Goods::Good goodFromString(const std::string& name)
{
    for (std::size_t i = 0; i < Goods::kGoodCount; ++i)
    {
        const auto good = static_cast<Goods::Good>(i);
        std::string lowered = Goods::name(good);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowered == name)
            return good;
    }
    throw std::runtime_error("Unknown good: '" + name + "'");
}

// The catalog of every building definition, in file order. Order is the
// tie-breaker for construction priority, so it is part of the data, not an
// accident of iteration.
class Catalog
{
public:
    void add(Def def) { m_defs.push_back(std::move(def)); }

    [[nodiscard]] const std::vector<Def>& all() const noexcept { return m_defs; }

    [[nodiscard]] const Def* byId(std::string_view id) const
    {
        for (const auto& def : m_defs)
            if (def.id == id)
                return &def;
        return nullptr;
    }

    [[nodiscard]] const Def* byElement(const Elements element) const
    {
        for (const auto& def : m_defs)
            if (def.element == element)
                return &def;
        return nullptr;
    }

    // Index of a def within `all()`, so a placed building can reference its
    // definition without copying it. Returns kNoIndex when unknown.
    static constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);
    [[nodiscard]] std::size_t indexOf(const Def* def) const
    {
        if (!def)
            return kNoIndex;
        for (std::size_t i = 0; i < m_defs.size(); ++i)
            if (&m_defs[i] == def)
                return i;
        return kNoIndex;
    }

    // (element, color) pairs for every definition, so MapGenerator can render
    // building tiles with their catalog color.
    [[nodiscard]] std::vector<std::pair<Elements, sf::Color>> colors() const
    {
        std::vector<std::pair<Elements, sf::Color>> out;
        out.reserve(m_defs.size());
        for (const auto& def : m_defs)
            out.emplace_back(def.element, def.color);
        return out;
    }

private:
    std::vector<Def> m_defs;
};

// Build a Catalog from a parsed buildings file. Throws std::runtime_error when
// the file cannot be read (loadJsonFile does that) or a key is malformed.
[[nodiscard]] inline Catalog loadCatalog(const std::string& path)
{
    const nlohmann::json js = loadJsonFile(path);

    Catalog catalog;
    for (const auto& entry : js.at("buildings"))
    {
        Def def;
        def.id    = entry.at("id").get<std::string>();
        def.name  = entry.value("name", def.id);
        def.element = elementFromString(entry.at("element").get<std::string>());

        if (entry.contains("color"))
        {
            const auto& c = entry.at("color");
            def.color = sf::Color{ static_cast<std::uint8_t>(c.at(0).get<int>()),
                                   static_cast<std::uint8_t>(c.at(1).get<int>()),
                                   static_cast<std::uint8_t>(c.at(2).get<int>()) };
        }

        if (entry.contains("cost"))
            for (const auto& [good_name, amount] : entry.at("cost").items())
                def.costs.push_back(Cost{ goodFromString(good_name), amount.get<int>() });

        def.footprint = sf::Vector2i{ entry.value("footprint_x", 1), entry.value("footprint_y", 1) };
        def.build_hours        = entry.value("build_hours", def.build_hours);
        def.work_per_hour      = entry.value("work_per_hour", def.work_per_hour);
        def.walk_cost          = entry.value("walk_cost", def.walk_cost);
        def.population_capacity = entry.value("population_capacity", def.population_capacity);
        def.is_anchor          = entry.value("is_anchor", def.is_anchor);
        def.priority           = entry.value("priority", def.priority);
        def.max_count          = entry.value("max_count", def.max_count);

        if (entry.contains("recipes"))
            for (const auto& r : entry.at("recipes"))
            {
                Goods::Recipe recipe;
                if (r.contains("input"))
                    recipe.input_good = goodFromString(r.at("input").get<std::string>());
                recipe.input_amount  = r.value("input_amount", 0);
                recipe.output_good   = goodFromString(r.at("output").get<std::string>());
                recipe.output_amount = r.value("output_amount", 1);
                recipe.label         = r.value("label", std::string{ def.id });
                def.recipes.push_back(std::move(recipe));
            }

        catalog.add(std::move(def));
    }

    return catalog;
}

// Parse just the settlement block, so it can be loaded alongside the catalog.
[[nodiscard]] inline Settlement loadSettlement(const std::string& path)
{
    const nlohmann::json js = loadJsonFile(path);
    Settlement settlement;
    if (js.contains("settlement"))
    {
        const auto& s = js.at("settlement");
        settlement.max_concurrent_sites     = s.value("max_concurrent_sites", settlement.max_concurrent_sites);
        settlement.build_radius_tiles       = s.value("build_radius_tiles", settlement.build_radius_tiles);
        settlement.road_cadence_hours       = s.value("road_cadence_hours", settlement.road_cadence_hours);
    }
    return settlement;
}

// Movement cost for an element, layering the catalog's override on top of the
// terrain cost: a building that declares `walk_cost` (a road) uses it, anything
// else falls back to MoveCost. This is what movement and pathfinding consult, so
// a road's speed is JSON-driven while terrain is untouched.
[[nodiscard]] inline float walkCost(const Elements element, const Catalog& catalog) noexcept
{
    if (const Def* def = catalog.byElement(element))
        if (def->walk_cost > 0.f)
            return def->walk_cost;
    return MoveCost::moveCost(element);
}

// Whether the stock holds every ingredient of the def's cost.
[[nodiscard]] inline bool affordable(const Goods::Stock& stock, const Def& def) noexcept
{
    for (const auto& cost : def.costs)
        if (!stock.has(cost.good, cost.amount))
            return false;
    return true;
}

// Spend the def's cost from the stock. Call only after `affordable` returned
// true, so a partially-spent building cannot leave the store short.
inline void spend(Goods::Stock& stock, const Def& def) noexcept
{
    for (const auto& cost : def.costs)
        stock.take(cost.good, cost.amount);
}

// Run the def once: the first recipe whose inputs are available is applied.
// Returns false when there is nothing to make (no recipes, or a workshop with no
// inputs), so a building with nothing to work simply idles an hour.
[[nodiscard]] inline bool produceOnce(Goods::Stock& stock, const Def& def) noexcept
{
    for (const auto& recipe : def.recipes)
        if (Goods::applyRecipe(stock, recipe))
            return true;
    return false;
}

} // namespace Buildings
