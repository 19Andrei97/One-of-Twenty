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
// The pipeline follows the layered model most procedural worlds use, which is
// what makes an Earth-like map and keeps each slider meaningful:
//
//   domain warp -> continent + shelf -> ridged mountain belts -> island falloff
//   -> elevation bands -> lake basins -> river channels -> climate biomes.
//
// Climate is sampled separately (latitude temperature + moisture) and only
// re-classifies the *land* bands: the elevation bands still decide where the
// coast and the peaks are, so the sea is never turned into a desert.
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

        // How much wood a forest tile holds. A deterministic pseudo-random amount
        // in [tree_wood_min, tree_wood_max], derived from a dedicated noise field
        // so a forest reads as many differently sized stands. Only meaningful for
        // a forest tile; other tiles return 0.
        int treeAmountAtTile(const sf::Vector2i& tile) const;

        // Composed land elevation in [0,1] at a tile, after warping, mountain
        // belts, the configured height range and island falloff. Exposed so
        // callers can tell a basin (a lake sits in one) from a peak without
        // re-deriving it.
        float elevationAtTile(const sf::Vector2i& tile) const;

        // The climate sample at a tile, in [0,1]. Exposed alongside elevation so
        // the biome decision can be inspected (tests, debug overlays) without
        // re-running the noise.
        float temperatureAtTile(const sf::Vector2i& tile) const;
        float moistureAtTile(const sf::Vector2i& tile) const;

        const MapConfig& config() const { return m_config; }

private:
        const MapConfig&                m_config;

        FastNoiseLite                   m_noise_continent;
        FastNoiseLite                   m_noise_warp;
        // Ridged belt field: roughens the highlands so mountains form crests
        // instead of the lumpy blobs plain fBm gives.
        FastNoiseLite                   m_noise_detail;
        // Very low-frequency mask that decides where mountain belts run, so
        // ranges are elongated belts rather than peaks scattered everywhere.
        FastNoiseLite                   m_noise_range_mask;
        // Climate layers: latitude-driven temperature plus a moisture field,
        // which together pick the land biome (Whittaker-style).
        FastNoiseLite                   m_noise_temperature;
        FastNoiseLite                   m_noise_moisture;
        // One field per ore: a shared mineral field made clay/iron/silver spike at
        // the same spots; separate fields (seeded apart) keep deposits independent.
        FastNoiseLite                   m_noise_clay;
        FastNoiseLite                   m_noise_iron;
        FastNoiseLite                   m_noise_silver;
        FastNoiseLite                   m_noise_river;
        FastNoiseLite                   m_noise_lake;
        // Forest density field: sizes each forest tile's wood pile so stands vary
        // (a few big trees, many small ones) rather than every tile holding the
        // same amount.
        FastNoiseLite                   m_noise_tree;

        // The warped coordinate for a tile plus the composed elevation and the
        // climate values, computed together so a full sample touches each noise
        // field only once.
        struct Sample
        {
                sf::Vector2f warped;
                float        elevation;
                float        temperature;
                float        moisture;
        };
        Sample sampleAt(const sf::Vector2i& tile) const;

        // Warp a tile coordinate through the shared domain-warp field, giving the
        // natural, non-linear coastlines and river bends instead of noise-grid bands.
        sf::Vector2f warp(const sf::Vector2i& tile) const;

        float islandFalloff(const sf::Vector2i& tile) const;

        // Pick a land biome from the elevation band and the climate sample.
        Elements classifyLand(float elevation, float temperature, float moisture) const;
};
