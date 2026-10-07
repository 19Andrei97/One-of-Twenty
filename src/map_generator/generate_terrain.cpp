#include <pch.h>

#include "generate_terrain.h"

GenerateTerrain::GenerateTerrain(const MapConfig& config)
	: m_config(config)
{
	m_noise_continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	m_noise_continent.SetFractalType(FastNoiseLite::FractalType_FBm);

	m_noise_wrap.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_wrap.SetFractalType(FastNoiseLite::FractalType_FBm);

	m_noise_clay.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_clay.SetFractalType(FastNoiseLite::FractalType_FBm);

	m_noise_iron.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_iron.SetFractalType(FastNoiseLite::FractalType_FBm);

	m_noise_silver.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_silver.SetFractalType(FastNoiseLite::FractalType_FBm);

	// A single octave gives the river field narrow, non-branching channels.
	m_noise_river.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_river.SetFractalType(FastNoiseLite::FractalType_None);

	setSeed(m_config.seed);
}

/*
*       Re-seed every field from the terrain seed and apply the config
*       frequencies. Each ore and the river are offset so their deposits and
*       channels never line up with the continent noise.
*/
void GenerateTerrain::setSeed(int seed)
{
	m_noise_continent.SetSeed(seed);
	m_noise_continent.SetFrequency(m_config.cont_freq);

	m_noise_wrap.SetSeed(seed);
	m_noise_wrap.SetFrequency(m_config.warp_freq);

	m_noise_clay.SetSeed(seed + 2);
	m_noise_clay.SetFrequency(m_config.mineral_freq);

	m_noise_iron.SetSeed(seed + 3);
	m_noise_iron.SetFrequency(m_config.mineral_freq);

	m_noise_silver.SetSeed(seed + 4);
	m_noise_silver.SetFrequency(m_config.mineral_freq);

	m_noise_river.SetSeed(seed + 1);
	m_noise_river.SetFrequency(m_config.river_freq);
}

/*
*       Decide which element belongs at a tile coordinate.
*
*       The noise is sampled in tile space so the terrain no longer depends on the
*       pixel tile size (a 16px or 32px tile covers the same world noise). Rivers and
*       island shaping only run when enabled in config, so the base map is unchanged.
*/
Elements GenerateTerrain::elementAtTile(const sf::Vector2i& tile) const {

	sf::Vector2f coord_f = static_cast<sf::Vector2f>(tile);

	// Generate noise and wrap for natural environment
	float warpX = coord_f.x + m_noise_wrap.GetNoise(coord_f.x, coord_f.y) * 100.0f;
	float warpY = coord_f.y + m_noise_wrap.GetNoise(coord_f.x, coord_f.y) * 100.0f;
	float continent = (m_noise_continent.GetNoise(warpX * m_config.cont_multiplier, warpY * m_config.cont_multiplier) + 1.0f) * 0.5f;

	// Remap the raw [0,1] continent field into the configured depth/height
	// range before any threshold is applied, so the same thresholds read as
	// different peaks. The default [0,1] is an identity transform.
	continent = m_config.height_min + (m_config.height_max - m_config.height_min) * continent;

	// Island shaping: pull the coast inward so the world is surrounded by water.
	if (m_config.island_enabled)
		continent *= islandFalloff(tile);

	// --- RIVER ---
	// A river cuts across the map where the river field crosses zero, but only on
	// land (above the deep ocean) so it does not carve through the seabed.
	if (m_config.river_enabled
		&& continent > m_config.thresholds.at(Elements::deep_ocean)
		&& continent < m_config.thresholds.at(Elements::snow))
	{
		const float riverField = m_noise_river.GetNoise(coord_f.x, coord_f.y);

		if (std::abs(riverField) < m_config.river_threshold)
			return Elements::ocean;
	}

	// Per-resource noise: each ore reads its own field, so the deposits are
	// uncorrelated instead of all riding one shared mineral field.
	const auto resourceNoise = [&](const FastNoiseLite& noise) {
		const float x = warpX * m_config.mineral_multiplier;
		const float y = warpY * m_config.mineral_multiplier;
		return (noise.GetNoise(x, y) + 1.0f) * 0.5f;
	};

	// --- OCEAN ---

	if (continent < m_config.thresholds.at(Elements::very_deep_ocean)) return Elements::very_deep_ocean;
	if (continent < m_config.thresholds.at(Elements::deep_ocean)) return Elements::deep_ocean;
	if (continent < m_config.thresholds.at(Elements::ocean)) return Elements::ocean;
	if (continent < m_config.thresholds.at(Elements::sand)) return Elements::sand;

	// --- CONTINENT ---
	if (continent < m_config.thresholds.at(Elements::hill))
	{
		if (resourceNoise(m_noise_clay) > m_config.thresholds.at(Elements::clay))
			return Elements::clay;

		return Elements::hill;
	}

	if (continent < m_config.thresholds.at(Elements::forest))
	{
		if (resourceNoise(m_noise_iron) > m_config.thresholds.at(Elements::iron))
			return Elements::iron;

		return Elements::forest;
	}


	if (continent < m_config.thresholds.at(Elements::muntain))
	{
		if (resourceNoise(m_noise_silver) > m_config.thresholds.at(Elements::silver))
			return Elements::silver;

		return Elements::muntain;
	}


	return Elements::snow;
}

/*
*       Radial falloff for island generation. Returns 1 near the origin and drops to
*       0 at the configured edge, so land fades into ocean with a soft coastline.
*/
float GenerateTerrain::islandFalloff(const sf::Vector2i& tile) const
{
	const float x = static_cast<float>(tile.x) / m_config.chunk_tile_size;
	const float y = static_cast<float>(tile.y) / m_config.chunk_tile_size;
	const float distance = std::sqrt(x * x + y * y);

	// m_config.island_falloff is the distance (in chunks) where land gives way to water.
	const float edge = std::max(0.001f, m_config.island_falloff);
	return std::clamp(1.0f - distance / edge, 0.0f, 1.0f);
}

Elements GenerateTerrain::elementAtWorld(const sf::Vector2i& coord) const {
	return elementAtTile(CoordMath::worldToTile(coord, m_config.tile_size_px));
}

/*
*       Sample the per-resource noise field at a world position, in [0,1]. The warp
*       and multiplier mirror elementAtTile so the value matches what generation saw.
*/
float GenerateTerrain::resourceValue(const sf::Vector2i& coord, Elements resource) const
{
	const FastNoiseLite* field = nullptr;
	switch (resource)
	{
	case Elements::clay:    field = &m_noise_clay;   break;
	case Elements::iron:    field = &m_noise_iron;   break;
	case Elements::silver:  field = &m_noise_silver; break;
	default:                return 0.0f;
	}

	const sf::Vector2f coord_f = static_cast<sf::Vector2f>(CoordMath::worldToTile(coord, m_config.tile_size_px));
	const float warpX = (coord_f.x + m_noise_wrap.GetNoise(coord_f.x, coord_f.y) * 100.0f) * m_config.mineral_multiplier;
	const float warpY = (coord_f.y + m_noise_wrap.GetNoise(coord_f.x, coord_f.y) * 100.0f) * m_config.mineral_multiplier;
	return (field->GetNoise(warpX, warpY) + 1.0f) * 0.5f;
}
