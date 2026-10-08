#pragma once

#include "Chunk.h"
#include "MapConfig.h"
#include "FastNoiseLite.h"

#include <SFML/System/Vector2.hpp>

// Standalone, stateless terrain sampling: one object holds the noise fields and
// the config-derived thresholds, and answers "what element is at this tile /
// world position". It owns no chunks and no threads, so both MapGenerator and
// the tests can use it directly.
//
// The pipeline is the one most tile-based games use:
//   domain warp -> continent field -> ridged highland detail -> island falloff
//   -> depth/biome bands -> lake basins -> river channels.
class GenerateTerrain
{
public:
	explicit GenerateTerrain(const MapConfig& config);

	// Terrain generation uses the floating-point seed; every field is offset
	// from it so terrain, ore, lake and river noise never align.
	void setSeed(int seed);

	// Element at a tile (the pure noise answer, before any edit).
	Elements elementAtTile(const sf::Vector2i& tile) const;
	// Element at a world pixel position; floors to the owning tile.
	Elements elementAtWorld(const sf::Vector2i& world) const;

	// Resource noise value in [0,1]. Returns 0 for a non-resource element.
	float resourceValue(const sf::Vector2i& world, Elements resource) const;

	// Composed land elevation in [0,1] at a tile, after warping, highland
	// detail, the configured height range and island falloff. Exposed so callers
	// can tell a basin (a lake sits in one) from a peak without re-deriving it.
	float elevationAtTile(const sf::Vector2i& tile) const;

	const MapConfig& config() const { return m_config; }

private:
	const MapConfig&                m_config;

	FastNoiseLite                   m_noise_continent;
	FastNoiseLite                   m_noise_warp;
	// Ridged detail field: roughens the highlands so mountains form crests
	// instead of the lumpy blobs plain fBm gives.
	FastNoiseLite                   m_noise_detail;
	// One field per ore: a shared mineral field made clay/iron/silver spike at
	// the same spots; separate fields (seeded apart) keep deposits independent.
	FastNoiseLite                   m_noise_clay;
	FastNoiseLite                   m_noise_iron;
	FastNoiseLite                   m_noise_silver;
	FastNoiseLite                   m_noise_river;
	FastNoiseLite                   m_noise_lake;

	// The warped coordinate for a tile plus the composed elevation, computed
	// together so a full sample touches each noise field only once.
	struct Sample
	{
		sf::Vector2f warped;
		float        elevation;
	};
	Sample sampleAt(const sf::Vector2i& tile) const;

	// Warp a tile coordinate through the shared domain-warp field, giving the
	// natural, non-linear coastlines and river bends instead of noise-grid bands.
	sf::Vector2f warp(const sf::Vector2i& tile) const;

	float islandFalloff(const sf::Vector2i& tile) const;
};
