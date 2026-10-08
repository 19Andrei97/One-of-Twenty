#include <pch.h>

#include "generate_terrain.h"

GenerateTerrain::GenerateTerrain(const MapConfig& config)
: m_config(config)
{
	// Continent: smooth OpenSimplex2 fBm is the base landmass shape.
	m_noise_continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	m_noise_continent.SetFractalType(FastNoiseLite::FractalType_FBm);
	m_noise_continent.SetFractalOctaves(5);
	m_noise_continent.SetFractalLacunarity(2.0f);
	m_noise_continent.SetFractalGain(0.5f);

	// Domain warp: the field that bends every other coordinate. Perlin at a low
	// frequency keeps the bend broad and non-repeating.
	m_noise_warp.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_warp.SetFractalType(FastNoiseLite::FractalType_FBm);
	m_noise_warp.SetFractalOctaves(2);

	// Ridged detail: FastNoiseLite's ridged fractal returns a crest where the
	// underlying noise crosses zero, which is exactly a mountain ridge.
	m_noise_detail.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	m_noise_detail.SetFractalType(FastNoiseLite::FractalType_Ridged);
	m_noise_detail.SetFractalOctaves(4);

	m_noise_clay.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_clay.SetFractalType(FastNoiseLite::FractalType_FBm);

	m_noise_iron.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_iron.SetFractalType(FastNoiseLite::FractalType_FBm);

	m_noise_silver.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_silver.SetFractalType(FastNoiseLite::FractalType_FBm);

	// River: a single low-frequency Perlin field, carved where it crosses zero.
	// One octave keeps the zero contour a long, smooth curve; adding octaves
	// shatters it into disconnected specks (measured: ~2k components instead of
	// ~140 for the same coverage). The shared domain warp bends the contour so
	// the channels meander instead of tracking the noise grid.
	m_noise_river.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	m_noise_river.SetFractalType(FastNoiseLite::FractalType_None);

	m_noise_lake.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	m_noise_lake.SetFractalType(FastNoiseLite::FractalType_FBm);

	setSeed(m_config.seed);
}

/*
*       Re-seed every field from the terrain seed and apply the config
*       frequencies. Each ore, the lake and the river are offset so their deposits
*       and channels never line up with the continent noise.
*/
void GenerateTerrain::setSeed(int seed)
{
	m_noise_continent.SetSeed(seed);
	m_noise_continent.SetFrequency(m_config.cont_freq);

	m_noise_warp.SetSeed(seed + 7);
	m_noise_warp.SetFrequency(m_config.warp_freq);

	m_noise_detail.SetSeed(seed + 5);
	m_noise_detail.SetFrequency(m_config.mountain_freq);

	m_noise_clay.SetSeed(seed + 2);
	m_noise_clay.SetFrequency(m_config.mineral_freq);

	m_noise_iron.SetSeed(seed + 3);
	m_noise_iron.SetFrequency(m_config.mineral_freq);

	m_noise_silver.SetSeed(seed + 4);
	m_noise_silver.SetFrequency(m_config.mineral_freq);

	m_noise_river.SetSeed(seed + 1);
	m_noise_river.SetFrequency(m_config.river_freq);

	m_noise_lake.SetSeed(seed + 6);
	m_noise_lake.SetFrequency(m_config.lake_freq);
}

/*
*       Warp a tile coordinate through the shared domain-warp field. The two axes
*       read the same field at two frequencies so the x and y offsets differ; the
*       result is a smooth, non-linear bend of the whole noise domain.
*/
sf::Vector2f GenerateTerrain::warp(const sf::Vector2i& tile) const
{
	const float x = static_cast<float>(tile.x);
	const float y = static_cast<float>(tile.y);
	const float warpX = m_noise_warp.GetNoise(x, y);
	const float warpY = m_noise_warp.GetNoise(x + 1000.0f, y - 1000.0f);
	return { x + warpX * m_config.warp_amplitude, y + warpY * m_config.warp_amplitude };
}

/*
*       Warped coordinate plus the composed elevation. One pass computes both, so
*       elementAtTile and elevationAtTile never disagree and each field is sampled
*       once per tile.
*/
GenerateTerrain::Sample GenerateTerrain::sampleAt(const sf::Vector2i& tile) const
{
	Sample sample;
	sample.warped = warp(tile);

	// Base landmass, remapped to [0,1].
	float continent = (m_noise_continent.GetNoise(sample.warped.x * m_config.cont_multiplier,
	sample.warped.y * m_config.cont_multiplier) + 1.0f) * 0.5f;

	// Ridged detail only roughens the highlands, so lowlands stay smooth while
	// mountains gain crests. The ridged field is [0,1]; remap to [-1,1] and
	// scale by how high the base already is.
	const float ridge = m_noise_detail.GetNoise(sample.warped.x, sample.warped.y) * 2.0f - 1.0f;
	continent += ridge * m_config.mountain_strength * continent;

	// Remap into the configured depth/height range before any threshold is
	// applied. The default [0,1] is an identity transform.
	continent = m_config.height_min + (m_config.height_max - m_config.height_min) * continent;

	// Island shaping: pull the coast inward so the world is surrounded by water.
	if (m_config.island_enabled)
		continent *= islandFalloff(tile);

	sample.elevation = std::clamp(continent, 0.0f, 1.0f);
	return sample;
}

/*
*       Decide which element belongs at a tile coordinate.
*       
*       Everything is sampled in tile space so terrain does not depend on the pixel
*       tile size. Lakes and rivers only run when enabled, and the whole pipeline is
*       deterministic in the seed.
*/
Elements GenerateTerrain::elementAtTile(const sf::Vector2i& tile) const
{
	const Sample sample = sampleAt(tile);
	const float continent = sample.elevation;

	const auto below = [&](Elements e) { return continent < m_config.thresholds[static_cast<std::size_t>(e)]; };

	// --- OCEAN ---

	if (below(Elements::very_deep_ocean)) return Elements::very_deep_ocean;
	if (below(Elements::deep_ocean))      return Elements::deep_ocean;
	if (below(Elements::ocean))           return Elements::ocean;

	// --- LAKE ---
	// A basin is low land above the sea and below the highlands. Flood only
	// where the lake field peaks, so lakes are discrete pools rather than a
	// second ocean. Checked before the beach so a basin reads as water, not sand.
	if (m_config.lake_enabled
		&& continent < m_config.thresholds[static_cast<std::size_t>(Elements::hill)]
		&& continent > m_config.lake_level
		&& m_noise_lake.GetNoise(sample.warped.x, sample.warped.y) > m_config.lake_threshold)
	{
		return Elements::lake;
	}

	if (below(Elements::sand)) return Elements::sand;

	// --- RIVER ---
	// A river follows a level set of the low-frequency field: wherever it crosses
	// zero. Carved only on ground that is not already a lake or the shore, and
	// stopped short of the peaks so channels stay in the valleys.
	if (m_config.river_enabled
		&& continent < m_config.thresholds[static_cast<std::size_t>(Elements::snow)])
	{
		const float riverField = std::abs(m_noise_river.GetNoise(sample.warped.x, sample.warped.y));
		if (riverField < m_config.river_threshold)
		return Elements::river;
	}

	// --- CONTINENT ---
	// Ores read their own field through the same warp, so deposits are
	// uncorrelated and land where the terrain looks right.
	const auto resourceNoise = [&](const FastNoiseLite& noise) {
		return (noise.GetNoise(sample.warped.x * m_config.mineral_multiplier,
		sample.warped.y * m_config.mineral_multiplier) + 1.0f) * 0.5f;
	};

	if (below(Elements::hill))
	{
		if (resourceNoise(m_noise_clay) > m_config.thresholds[static_cast<std::size_t>(Elements::clay)])
		return Elements::clay;

		return Elements::hill;
	}

	if (below(Elements::forest))
	{
		if (resourceNoise(m_noise_iron) > m_config.thresholds[static_cast<std::size_t>(Elements::iron)])
		return Elements::iron;

		return Elements::forest;
	}

	if (below(Elements::mountain))
	{
		if (resourceNoise(m_noise_silver) > m_config.thresholds[static_cast<std::size_t>(Elements::silver)])
		return Elements::silver;

		return Elements::mountain;
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

float GenerateTerrain::elevationAtTile(const sf::Vector2i& tile) const
{
	return sampleAt(tile).elevation;
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

	const sf::Vector2f warped = warp(CoordMath::worldToTile(coord, m_config.tile_size_px));
	return (field->GetNoise(warped.x * m_config.mineral_multiplier,
	warped.y * m_config.mineral_multiplier) + 1.0f) * 0.5f;
}
