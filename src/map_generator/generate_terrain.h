#pragma once

#include "Chunk.h"
#include "MapConfig.h"
#include "FastNoiseLite.h"

#include <SFML/System/Vector2.hpp>

// Standalone, stateless terrain sampling: one object holds the noise fields and
// the config-derived thresholds, and answers "what element is at this tile /
// world position". It owns no chunks and no threads, so both MapGenerator and
// the tests can use it directly.
class GenerateTerrain
{
public:
	explicit GenerateTerrain(const MapConfig& config);

	// Terrain generation uses the floating-point seed; every field is offset
	// from it so terrain, ore and river noise never align.
	void setSeed(int seed);

	// Element at a tile (the pure noise answer, before any edit).
	Elements elementAtTile(const sf::Vector2i& tile) const;
	// Element at a world pixel position; floors to the owning tile.
	Elements elementAtWorld(const sf::Vector2i& world) const;

	// Resource noise value in [0,1]. Returns 0 for a non-resource element.
	float resourceValue(const sf::Vector2i& world, Elements resource) const;

	const MapConfig& config() const { return m_config; }

private:
	const MapConfig&                m_config;

	FastNoiseLite                   m_noise_continent;
	FastNoiseLite                   m_noise_wrap;
	// One field per ore: a shared mineral field made clay/iron/silver spike at
	// the same spots; separate fields (seeded apart) keep deposits independent.
	FastNoiseLite                   m_noise_clay;
	FastNoiseLite                   m_noise_iron;
	FastNoiseLite                   m_noise_silver;
	FastNoiseLite                   m_noise_river;

	float                           islandFalloff(const sf::Vector2i& tile) const;
};
