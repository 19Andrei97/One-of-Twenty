#pragma once

#include <array>
#include <string>

#include "Chunk.h"
#include "Config.h"

#include <nlohmann/json.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

// Parameters that control how terrain is sampled, plus the biome palette.
//
// The parameters are grouped the way a player thinks about a map, not the way
// the noise is implemented, so each one can be exposed as a single, clearly
// named slider:
//
//   Land        land_amount, continent_size, coast_roughness
//   Mountains   mountain_height, mountain_scale
//   Climate     temperature, rainfall, snow_line
//   Water       river_density, river_size, lake_level, lake_size
//   Resources   ore_richness
//
// Loaded once from config/map_data.json and shared read-only by the terrain
// generator, chunk store and renderer, so generation never re-reads the file.
struct MapConfig
{
        int                             tile_size_px{ 16 };
        int                             chunk_tile_size{ 32 };
        int                             chunk_margin{ 2 };

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
        // Rivers and lakes can be switched off entirely (used to isolate them in
        // tests and to build a deliberately dry world); both default on.
        bool                            river_enabled{ true };
        bool                            lake_enabled{ true };
        // How many rivers are carved: higher values widen the channels.
        float                           river_density{ 0.012f };
        // River meander scale: small values give long, sweeping rivers.
        float                           river_size{ 0.0035f };
        // Height of the water table for inland lakes: higher floods more basins.
        float                           lake_level{ 0.3f };
        // How readily a low basin becomes a lake: higher gives larger, fewer.
        float                           lake_size{ 0.62f };

        // --- Resources ---
        // How abundant ore deposits are: higher values grow the deposits.
        float                           ore_richness{ 0.15f };

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

        cfg.tile_size_px = js_map["tile_size"];
        cfg.chunk_tile_size = js_map["chunk_tile_size"];
        cfg.chunk_margin = js_map["chunk_margin"];
        cfg.seed = js_map.value("seed", cfg.seed);

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

        // Water. Rivers/lakes also accept the older nested `river`/`lake` blocks
        // with an `enabled` flag, so existing configs keep working.
        cfg.river_density    = js_map.value("river_density", cfg.river_density);
        cfg.river_size       = js_map.value("river_size", cfg.river_size);
        cfg.lake_level       = js_map.value("lake_level", cfg.lake_level);
        cfg.lake_size        = js_map.value("lake_size", cfg.lake_size);

        if (js_map.contains("river"))
        {
                cfg.river_enabled = js_map["river"].value("enabled", cfg.river_enabled);
                cfg.river_density = js_map["river"].value("threshold", cfg.river_density);
                cfg.river_size    = js_map["river"].value("freq", cfg.river_size);
        }
        if (js_map.contains("lake"))
        {
                cfg.lake_enabled = js_map["lake"].value("enabled", cfg.lake_enabled);
                cfg.lake_level   = js_map["lake"].value("level", cfg.lake_level);
                cfg.lake_size    = js_map["lake"].value("threshold", cfg.lake_size);
        }

        // Resources.
        cfg.ore_richness     = js_map.value("ore_richness", cfg.ore_richness);

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
