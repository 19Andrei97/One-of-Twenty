#pragma once

#include <array>
#include <string>

#include "Chunk.h"
#include "Config.h"
#include "MapSize.h"

#include <nlohmann/json.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <optional>

// The gatherable element a config key names ("forest", "hill", "clay", "iron",
// "silver"). Returns nullopt for anything else, so a typo in a config is ignored
// rather than silently creating a deposit on a non-resource element.
[[nodiscard]] inline std::optional<Elements> depositElementFromName(const std::string& name)
{
        if (name == "forest") return Elements::forest;
        if (name == "hill")   return Elements::hill;
        if (name == "clay")   return Elements::clay;
        if (name == "iron")   return Elements::iron;
        if (name == "silver") return Elements::silver;
        return std::nullopt;
}

// Parameters that control how terrain is sampled, plus the biome palette.
//
// The parameters are grouped the way a player thinks about a map, not the way
// the noise is implemented, so each one can be exposed as a single, clearly
// named slider:
//
//   Land        land_amount, continent_size, coast_roughness
//   Mountains   mountain_height, mountain_scale
//   Climate     temperature, rainfall, snow_line
//   Water       lake_level, lake_size
//   Resources   ore_richness
//
// Loaded once from config/map_data.json and shared read-only by the terrain
// generator, chunk store and renderer, so generation never re-reads the file.
struct MapConfig
{
        int                             tile_size_px{ 16 };
        int                             chunk_tile_size{ 32 };
        int                             chunk_margin{ 2 };

        // Selectable world size (Normal / Big / Enormous). The world is a finite
        // square of chunks; the count per size lives in MapSize.h.
        MapSize                         map_size{ MapSize::Normal };

        int                             seed{ 1 };

        // --- Land ---
        // Fraction of the world above sea level, in [0,1]: this is the sea
        // level. Raise it for a watery world, lower it for a big continent.
        float                           land_amount{ 0.35f };
        // Scale of the landmass. Small values give a few huge continents; large
        // values give an archipelago of many small islands.
        float                           continent_size{ 0.023f };
        // How much the coastline wiggles: 0 is smooth, 1 is very ragged.
        float                           coast_roughness{ 0.5f };

        // --- Mountains ---
        // Peak height added on top of the base land. 0 is a flat world.
        float                           mountain_height{ 0.35f };
        // Width of the mountain belts: small values give long, thin ranges;
        // large values give broad highlands.
        float                           mountain_scale{ 0.02f };

        // --- Climate ---
        // Overall warmth: shifts the latitude temperature gradient. Low is a
        // frozen world, high is a hot one.
        float                           temperature{ 0.5f };
        // Overall wetness: low is dry (deserts), high is lush (dense forest).
        float                           rainfall{ 0.5f };
        // Elevation above which snow appears; lower pushes the snow line down.
        float                           snow_line{ 0.95f };

        // --- Water ---
        // Inland lakes can be switched off entirely (used to isolate terrain in
        // tests); they default on.
        bool                            lake_enabled{ true };
        // Height of the water table for inland lakes: higher floods more basins.
        float                           lake_level{ 0.3f };
        // How readily a low basin becomes a lake: higher gives larger, fewer.
        float                           lake_size{ 0.62f };

        // --- Resources ---
        // How abundant ore deposits are: higher values grow the deposits.
        float                           ore_richness{ 0.15f };

        // How much of its good one tile of a gatherable holds. Every gatherable
        // (forest, hill and the three ores) is a *finite* pile: a gather takes one
        // unit and the tile turns to `barren` once the pile reaches zero. The size
        // of a tile's pile is a deterministic pseudo-random amount drawn from the
        // element's own range, scaled by the biome's character and by
        // `resource_richness`, so the same resource is richer in the biome it
        // belongs to (lush forest holds more wood than dry scrub).
        struct Deposit
        {
                int   min{ 1 };
                int   max{ 1 };
                // How strongly the local climate scales this deposit: positive makes
                // the resource richer in lush (warm, wet) ground, negative makes it
                // richer in dry, exposed ground. 0 ignores climate. This is what
                // gives a biome more of one resource than another.
                float biome_bonus{ 0.0f };
        };
        std::array<Deposit, kElementCount> deposits{};
        // A global multiplier on every deposit's size: the single "how plentiful
        // are raw materials" control. 0.5 is the neutral (x1) setting.
        float                           resource_richness{ 0.5f };

        // --- Shaping (not player-facing) ---
        // Radial falloff so the world is an island surrounded by ocean.
        bool                            island_enabled{ false };
        float                           island_falloff{ 0.4f };

        // Depth/height range the composed elevation is remapped into. Defaults
        // to the full [0,1] so the classic map is unchanged; shrinking it lowers
        // the peaks (and raising min floods the lowlands) without touching the
        // noise itself.
        float                           height_min{ 0.0f };
        float                           height_max{ 1.0f };

        // Indexed by Elements, not keyed by it: sampling reads these on every
        // tile, so a flat array beats a hash lookup. Entries absent from the
        // config keep their default-constructed value (black / 0).
        std::array<sf::Color, kElementCount> biome_colors{};
        std::array<float, kElementCount>     thresholds{};
};

// Build a MapConfig from a parsed map file. Throws std::runtime_error when the
// file cannot be read (loadJsonFile does that) or a required key is missing.
//
// Every gameplay parameter is optional: a missing key keeps its default, so an
// older config still loads and a config only needs the values it changes.
inline MapConfig loadMapConfig(const std::string& path)
{
        const nlohmann::json js_map = loadJsonFile(path);

        MapConfig cfg;

        // Shipped deposit sizes. A config that names only some of them keeps these
        // for the rest, so a resource is never accidentally left at the 1-unit
        // placeholder.
        const auto setDeposit = [&cfg](const char* name, const int lo, const int hi, const float bonus)
        {
                const auto element = depositElementFromName(name);
                if (!element)
                        return;
                const auto index = static_cast<std::size_t>(*element);
                cfg.deposits[index].min = lo;
                cfg.deposits[index].max = hi;
                cfg.deposits[index].biome_bonus = bonus;
        };
        setDeposit("forest", 2, 8, 0.8f);   // lush ground holds more wood
        setDeposit("hill",   2, 6, -0.3f);  // dry, exposed ground holds more stone
        setDeposit("clay",   1, 4, 0.0f);
        setDeposit("iron",   1, 3, 0.0f);
        setDeposit("silver", 1, 2, 0.0f);

        cfg.tile_size_px = js_map["tile_size"];
        cfg.chunk_tile_size = js_map["chunk_tile_size"];
        cfg.chunk_margin = js_map["chunk_margin"];
        cfg.seed = js_map.value("seed", cfg.seed);

        // World size: a name ("Normal"/"Big"/"Enormous", case-insensitive). An
        // unknown name throws through mapSizeFromString, so a typo fails loudly.
        if (js_map.contains("map_size"))
                cfg.map_size = mapSizeFromString(js_map["map_size"].get<std::string>());

        // Land.
        cfg.land_amount      = js_map.value("land_amount", cfg.land_amount);
        cfg.continent_size   = js_map.value("continent_size", cfg.continent_size);
        cfg.coast_roughness  = js_map.value("coast_roughness", cfg.coast_roughness);

        // Mountains.
        cfg.mountain_height  = js_map.value("mountain_height", cfg.mountain_height);
        cfg.mountain_scale   = js_map.value("mountain_scale", cfg.mountain_scale);

        // Climate.
        cfg.temperature      = js_map.value("temperature", cfg.temperature);
        cfg.rainfall         = js_map.value("rainfall", cfg.rainfall);
        cfg.snow_line        = js_map.value("snow_line", cfg.snow_line);

        // Water. Lakes also accept the older nested `lake` block with an
        // `enabled` flag, so existing configs keep working.
        cfg.lake_level       = js_map.value("lake_level", cfg.lake_level);
        cfg.lake_size        = js_map.value("lake_size", cfg.lake_size);

        if (js_map.contains("lake"))
        {
                cfg.lake_enabled = js_map["lake"].value("enabled", cfg.lake_enabled);
                cfg.lake_level   = js_map["lake"].value("level", cfg.lake_level);
                cfg.lake_size    = js_map["lake"].value("threshold", cfg.lake_size);
        }

        // Resources.
        cfg.ore_richness     = js_map.value("ore_richness", cfg.ore_richness);
        cfg.resource_richness = js_map.value("resource_richness", cfg.resource_richness);

        // Per-resource deposit sizes. A `resource_deposits` block overrides the
        // shipped defaults per element, so a biome's wealth is a data edit. Each
        // entry accepts a `[min, max]` pair (or a `{ "min": .., "max": .. }`
        // object) plus an optional `biome_bonus`.
        if (js_map.contains("resource_deposits"))
        {
                for (const auto& [name, value] : js_map["resource_deposits"].items())
                {
                        const auto element = depositElementFromName(name);
                        if (!element)
                                continue;
                        auto& deposit = cfg.deposits[static_cast<std::size_t>(*element)];
                        if (value.is_array() && value.size() >= 2)
                        {
                                deposit.min = value[0].get<int>();
                                deposit.max = value[1].get<int>();
                        }
                        else if (value.is_object())
                        {
                                deposit.min = value.value("min", deposit.min);
                                deposit.max = value.value("max", deposit.max);
                                deposit.biome_bonus = value.value("biome_bonus", deposit.biome_bonus);
                        }
                        if (deposit.max < deposit.min)
                                std::swap(deposit.min, deposit.max);
                }
        }

        // The legacy `tree_wood_min`/`tree_wood_max` keys still size the forest
        // deposit, so an older config keeps its wood.
        if (js_map.contains("tree_wood_min") || js_map.contains("tree_wood_max"))
        {
                auto& forest = cfg.deposits[static_cast<std::size_t>(Elements::forest)];
                forest.min = js_map.value("tree_wood_min", forest.min);
                forest.max = js_map.value("tree_wood_max", forest.max);
                if (forest.max < forest.min)
                        std::swap(forest.min, forest.max);
        }

        for (const auto& [key, value] : js_map["elements"].items()) {
                cfg.biome_colors[static_cast<std::size_t>(std::stoi(key))] = {
                        static_cast<std::uint8_t>(value[0]),
                        static_cast<std::uint8_t>(value[1]),
                        static_cast<std::uint8_t>(value[2]),
                };
        }

        for (const auto& [key, value] : js_map["heights"].items()) {
                cfg.thresholds[static_cast<std::size_t>(std::stoi(key))] = value.get<float>();
        }

        if (js_map.contains("island") && js_map["island"].value("enabled", false))
        {
                cfg.island_enabled = true;
                cfg.island_falloff = js_map["island"].value("falloff", 0.4f);
        }

        if (js_map.contains("height_range"))
        {
                cfg.height_min = js_map["height_range"].value("min", 0.0f);
                cfg.height_max = js_map["height_range"].value("max", 1.0f);
                if (cfg.height_max < cfg.height_min)
                        std::swap(cfg.height_min, cfg.height_max);
        }

        return cfg;
}
