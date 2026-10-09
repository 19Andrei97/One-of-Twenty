#include <pch.h>

#include "generate_terrain.h"

namespace
{
        // Base cutoffs for the ore fields, chosen so each deposit covers a few
        // percent of its land band. `ore_richness` shifts all three together, so
        // these are the "default richness" shape and not player-facing knobs.
        constexpr float kClayCutoff{ 0.72f };
        constexpr float kIronCutoff{ 0.68f };
        constexpr float kSilverCutoff{ 0.66f };
}

GenerateTerrain::GenerateTerrain(const MapConfig& config)
: m_config(config)
{
        // Continent: smooth OpenSimplex2 fBm is the base landmass shape.
        m_noise_continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        m_noise_continent.SetFractalType(FastNoiseLite::FractalType_FBm);
        // A low persistence keeps the high octaves faint, so the continent stays a
        // few big landmasses with a clean coast instead of a speckle of islands
        // and inland seas where the detail crosses sea level.
        m_noise_continent.SetFractalOctaves(5);
        m_noise_continent.SetFractalLacunarity(2.0f);
        m_noise_continent.SetFractalGain(0.38f);

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

        // Range mask: one very low-frequency fBm octave band decides where the
        // mountain belts are, so ranges read as elongated chains instead of
        // uniform lumps. OpenSimplex2 keeps the belts smooth and organic.
        m_noise_range_mask.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        m_noise_range_mask.SetFractalType(FastNoiseLite::FractalType_FBm);
        m_noise_range_mask.SetFractalOctaves(2);

        // Climate: broad temperature and moisture fields. Their frequencies are
        // fixed (broad, continental scale); the player-facing sliders shift the
        // overall warmth and wetness rather than the scale.
        m_noise_temperature.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        m_noise_temperature.SetFractalType(FastNoiseLite::FractalType_FBm);
        m_noise_temperature.SetFractalOctaves(3);

        m_noise_moisture.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        m_noise_moisture.SetFractalType(FastNoiseLite::FractalType_FBm);
        m_noise_moisture.SetFractalOctaves(3);

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
        // The continent frequency is applied as a coordinate scale in sampleAt, so
        // the field itself runs at unit frequency; this keeps `continent_size` the
        // single knob that controls the landmass scale.
        m_noise_continent.SetFrequency(1.0f);

        m_noise_warp.SetSeed(seed + 7);
        m_noise_warp.SetFrequency(0.007f);

        m_noise_detail.SetSeed(seed + 5);
        m_noise_detail.SetFrequency(m_config.mountain_scale);

        m_noise_range_mask.SetSeed(seed + 8);
        // A fixed low frequency, much lower than the detail: the mask changes over
        // a whole continent so belts are long.
        m_noise_range_mask.SetFrequency(0.004f);

        // Broad climate bands (a few thousand tiles across) so a biome reads as a
        // region rather than a per-tile speckle.
        m_noise_temperature.SetSeed(seed + 9);
        m_noise_temperature.SetFrequency(0.0009f);

        m_noise_moisture.SetSeed(seed + 10);
        m_noise_moisture.SetFrequency(0.0012f);

        // Ore fields: a mid frequency so each deposit is a compact patch, not a
        // continent-wide blob, and the three read different seeds so they never
        // overlap into one deposit.
        m_noise_clay.SetSeed(seed + 2);
        m_noise_clay.SetFrequency(0.02f);

        m_noise_iron.SetSeed(seed + 3);
        m_noise_iron.SetFrequency(0.02f);

        m_noise_silver.SetSeed(seed + 4);
        m_noise_silver.SetFrequency(0.02f);

        m_noise_river.SetSeed(seed + 1);
        m_noise_river.SetFrequency(m_config.river_size);

        m_noise_lake.SetSeed(seed + 6);
        m_noise_lake.SetFrequency(0.004f);
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

        // `coast_roughness` scales the warp from none (smooth coasts) to a strong
        // bend (ragged, fjord-like coasts). The old fixed amplitude of 45 tiles is
        // the 1.0 end of the slider.
        const float amplitude = 45.0f * m_config.coast_roughness;
        return { x + warpX * amplitude, y + warpY * amplitude };
}

/*
*       Warped coordinate plus the composed elevation and climate, computed in one
*       pass so elementAtTile and elevationAtTile never disagree and each field is
*       sampled once per tile.
*/
GenerateTerrain::Sample GenerateTerrain::sampleAt(const sf::Vector2i& tile) const
{
        Sample sample;
        sample.warped = warp(tile);

        // Base landmass, remapped to [0,1] and shifted by `land_amount`, which is
        // the sea level: a higher value means more land. The shift is a plain add
        // rather than a threshold so it composes with the elevation remap below.
        float continent = (m_noise_continent.GetNoise(sample.warped.x * m_config.continent_size,
        sample.warped.y * m_config.continent_size) + 1.0f) * 0.5f
        + (m_config.land_amount - 0.5f) * 0.8f;

        // Ridged detail only roughens the highlands, so lowlands stay smooth while
        // mountains gain crests. A second, much lower-frequency mask decides where
        // the belts run. The mask is smoothstepped so most of the map has no uplift
        // at all and the mountains gather into a few elongated chains, instead of
        // roughening every tile and speckling the lowland biomes.
        const float rangeRaw = (m_noise_range_mask.GetNoise(sample.warped.x, sample.warped.y) + 1.0f) * 0.5f;
        const float beltT = std::clamp((rangeRaw - 0.45f) / 0.30f, 0.0f, 1.0f);
        const float rangeMask = beltT * beltT * (3.0f - 2.0f * beltT);
        // The ridged fractal already returns [-1,1] with crests near +1. Only the
        // positive half raises the land (clamped at 0): adding the negative half
        // would carve trenches below sea level and drown the world.
        const float ridge = std::max(0.0f, m_noise_detail.GetNoise(sample.warped.x, sample.warped.y));
        // The 1.6 gain lets a fully raised belt reach the top of the elevation
        // range (snow caps) from an average base instead of only lifting the
        // already-high ground a little.
        continent += ridge * m_config.mountain_height * rangeMask * 1.6f;

        // Remap into the configured depth/height range before any threshold is
        // applied. The default [0,1] is an identity transform.
        continent = m_config.height_min + (m_config.height_max - m_config.height_min) * continent;

        // Island shaping: pull the coast inward so the world is surrounded by water.
        if (m_config.island_enabled)
                continent *= islandFalloff(tile);

        sample.elevation = std::clamp(continent, 0.0f, 1.0f);

        // Climate. Temperature falls off with latitude (|y|), so the poles are
        // cold and the equator warm; the `temperature` slider shifts the whole
        // gradient. Moisture is its own field plus the `rainfall` shift.
        const float latitude = std::abs(static_cast<float>(tile.y));
        const float latNorm = std::min(1.0f, latitude / 6000.0f);
        const float tempNoise = (m_noise_temperature.GetNoise(sample.warped.x, sample.warped.y) + 1.0f) * 0.5f;
        sample.temperature = std::clamp(
                0.75f * (1.0f - latNorm) + 0.25f * tempNoise
                + (m_config.temperature - 0.5f) * 1.2f, 0.0f, 1.0f);

        const float moistNoise = (m_noise_moisture.GetNoise(sample.warped.x, sample.warped.y) + 1.0f) * 0.5f;
        sample.moisture = std::clamp(moistNoise + (m_config.rainfall - 0.5f) * 1.2f, 0.0f, 1.0f);

        return sample;
}

/*
*       Decide which element belongs at a tile coordinate.
*
*       Everything is sampled in tile space so terrain does not depend on the pixel
*       tile size. The whole pipeline is deterministic in the seed.
*/
Elements GenerateTerrain::elementAtTile(const sf::Vector2i& tile) const
{
        const Sample sample = sampleAt(tile);
        const float elevation = sample.elevation;

        const auto below = [&](Elements e) { return elevation < m_config.thresholds[static_cast<std::size_t>(e)]; };

        // --- OCEAN ---

        if (below(Elements::very_deep_ocean)) return Elements::very_deep_ocean;
        if (below(Elements::deep_ocean))      return Elements::deep_ocean;
        if (below(Elements::ocean))           return Elements::ocean;

        // --- LAKE ---
        // A basin is low land above the sea and below the highlands. Flood only
        // where the lake field peaks, so lakes are discrete pools rather than a
        // second ocean. Checked before the beach so a basin reads as water, not sand.
        if (m_config.lake_enabled
                && elevation < m_config.thresholds[static_cast<std::size_t>(Elements::hill)]
                && elevation > m_config.lake_level
                && m_noise_lake.GetNoise(sample.warped.x, sample.warped.y) > m_config.lake_size)
        {
                return Elements::lake;
        }

        if (below(Elements::sand)) return Elements::sand;

        // --- RIVER ---
        // A river follows a level set of the low-frequency field: wherever it crosses
        // zero. Carved only on ground that is not already a lake or the shore, and
        // stopped short of the peaks so channels stay in the valleys. `river_density`
        // is the half-width of the channel.
        if (m_config.river_enabled
                && elevation < m_config.thresholds[static_cast<std::size_t>(Elements::snow)])
        {
                const float riverField = std::abs(m_noise_river.GetNoise(sample.warped.x, sample.warped.y));
                if (riverField < m_config.river_density)
                        return Elements::river;
        }

        // --- CONTINENT ---
        // Land biomes come from elevation + climate, so the map has rainforest,
        // temperate forest, dry grassland and desert instead of one uniform
        // "forest" band.
        const Elements base = classifyLand(elevation, sample.temperature, sample.moisture);

        // Ore deposits sit on top of the biome: a deposit only forms on the
        // matching land band (clay on the open lowland, iron in forest, silver in
        // the highlands) where its own field is high. Each ore has a fixed base
        // cutoff tuned to a few percent coverage; `ore_richness` shifts all three
        // together, so the one slider is the whole "how much ore" control.
        const float richnessShift = (m_config.ore_richness - 0.5f) * 0.4f;
        const auto oreAt = [&](const FastNoiseLite& noise, float base_cutoff) {
                const float value = (noise.GetNoise(sample.warped.x, sample.warped.y) + 1.0f) * 0.5f;
                return value > base_cutoff - richnessShift;
        };

        if (base == Elements::hill && oreAt(m_noise_clay, kClayCutoff))
                return Elements::clay;
        if (base == Elements::forest && oreAt(m_noise_iron, kIronCutoff))
                return Elements::iron;
        if (base == Elements::mountain && oreAt(m_noise_silver, kSilverCutoff))
                return Elements::silver;

        return base;
}

/*
*       Choose a land biome from the elevation band and the climate sample. The
*       elevation bands still decide where the hill/forest/mountain/snow levels
*       are; temperature and moisture only pick the flavour within a band, so the
*       coastline and the peaks stay where the elevation put them.
*
*       The five shipped land elements are reused (forest, hill, sand, mountain,
*       snow) because survival, economy and pathfinding classify by element:
*       changing the palette would change gameplay, not just looks.
*/
Elements GenerateTerrain::classifyLand(float elevation, float temperature, float moisture) const
{
        const auto threshold = [&](Elements e) { return m_config.thresholds[static_cast<std::size_t>(e)]; };

        // Called only for land above the beach (elevation >= the sand band), so
        // every branch below returns a land element and never water.

        // Bare rock above the forest line.
        if (elevation >= threshold(Elements::forest))
        {
                // Snow caps the cold peaks; the `snow_line` slider moves the height
                // at which the cap starts.
                if (elevation >= m_config.snow_line && temperature < 0.35f)
                        return Elements::snow;
                return Elements::mountain;
        }

        // Mid elevations (hill/forest bands). Cold or dry ground is open hill/stone;
        // warm and wet ground is forest. This is what makes the map read as Earth:
        // rainforest near the equator, dry grass where it is hot and arid, and bare
        // rock toward the poles.
        const float lushness = 0.6f * moisture + 0.4f * temperature;
        return (lushness > 0.5f) ? Elements::forest : Elements::hill;
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

float GenerateTerrain::temperatureAtTile(const sf::Vector2i& tile) const
{
        return sampleAt(tile).temperature;
}

float GenerateTerrain::moistureAtTile(const sf::Vector2i& tile) const
{
        return sampleAt(tile).moisture;
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
        // The ore fields carry their own frequency (set in setSeed), so the sample
        // here matches elementAtTile exactly; `ore_richness` only shifts the
        // threshold there and must not scale the sampling here.
        return (field->GetNoise(warped.x, warped.y) + 1.0f) * 0.5f;
}
