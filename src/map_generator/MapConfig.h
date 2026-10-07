#pragma once

#include <string>
#include <unordered_map>

#include "Chunk.h"
#include "Config.h"

#include <nlohmann/json.hpp>
#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

// Parameters that control how terrain is sampled, plus the biome palette. Loaded
// once from config/map_data.json and shared read-only by the terrain generator,
// chunk store and renderer, so generation never re-reads the file.
struct MapConfig
{
	int                             tile_size_px{ 16 };
	int                             chunk_tile_size{ 32 };
	int                             chunk_margin{ 2 };

	int                             seed{ 1 };

	float                           cont_multiplier{ 0.018f };
	float                           mineral_multiplier{ 0.15f };

	float                           cont_freq{ 0.023f };
	float                           warp_freq{ 0.007f };
	float                           mineral_freq{ 0.004f };
	float                           river_freq{ 0.01f };

	bool                            island_enabled{ false };
	float                           island_falloff{ 0.4f };

	bool                            river_enabled{ false };
	float                           river_threshold{ 0.03f };

	// Depth/height range the continent field is remapped into. Defaults to the
	// full [0,1] so the classic map is unchanged; shrinking it lowers the peaks
	// (and raising min floods the lowlands) without touching the noise itself.
	float                           height_min{ 0.0f };
	float                           height_max{ 1.0f };

	std::unordered_map<Elements, sf::Color> biome_colors;
	std::unordered_map<Elements, float>     thresholds;
};

// Build a MapConfig from a parsed map file. Throws std::runtime_error when the
// file cannot be read (loadJsonFile does that) or a required key is missing.
inline MapConfig loadMapConfig(const std::string& path)
{
	const nlohmann::json js_map = loadJsonFile(path);

	MapConfig cfg;

	cfg.tile_size_px = js_map["tile_size"];
	cfg.chunk_tile_size = js_map["chunk_tile_size"];
	cfg.chunk_margin = js_map["chunk_margin"];

	cfg.cont_multiplier = static_cast<float>(js_map["cont_multiplier"]);
	cfg.mineral_multiplier = static_cast<float>(js_map["mineral_multiplier"]);
	cfg.cont_freq = static_cast<float>(js_map["cont_freq"]);
	cfg.warp_freq = static_cast<float>(js_map["warp_freq"]);
	cfg.mineral_freq = static_cast<float>(js_map["mineral_freq"]);

	for (const auto& [key, value] : js_map["elements"].items()) {
		cfg.biome_colors[static_cast<Elements>(std::stoi(key))] = {
			static_cast<std::uint8_t>(value[0]),
			static_cast<std::uint8_t>(value[1]),
			static_cast<std::uint8_t>(value[2]),
		};
	}

	for (const auto& [key, value] : js_map["heights"].items()) {
		cfg.thresholds[static_cast<Elements>(std::stoi(key))] = value.get<float>();
	}

	if (js_map.contains("island") && js_map["island"].value("enabled", false))
	{
		cfg.island_enabled = true;
		cfg.island_falloff = js_map["island"].value("falloff", 0.4f);
	}

	if (js_map.contains("river") && js_map["river"].value("enabled", false))
	{
		cfg.river_enabled = true;
		cfg.river_threshold = js_map["river"].value("threshold", 0.03f);
		cfg.river_freq = js_map["river"].value("freq", 0.01f);
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
