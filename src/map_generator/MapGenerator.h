#pragma once

#include "Chunk.h"
#include "MapConfig.h"
#include "MapSize.h"
#include "generate_terrain.h"

#include <BS_thread_pool.hpp>
#include <SharedContainer.h>

#include <SFML/Graphics.hpp>

// On Windows, SFML pulls in <windows.h>, which defines the legacy `near`/`far`
// macros. NOMINMAX (which SFML sets) only covers `min`/`max`, so `near` and
// `far` still mangle any identifier spelled that way in a translation unit that
// includes this header. Undefine them so the names stay usable in our code.
#ifdef _WIN32
    #undef near
    #undef far
#endif

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Loads map config, generates chunks on worker threads, streams them in and out
// around the camera, meshes them and draws them.
//
// The actual "what is the terrain here" work lives in GenerateTerrain, which is
// stateless and cheap to share. Keeping sampling out of this class is what makes
// the noise testable without constructing a renderer and a thread pool.
class MapGenerator
{
private:
	// CHUNK variables
	// c_chunk_tiles is the chunk size in tiles; c_chunks is keyed by the chunk's
	// top-left tile coordinate. Everything outside generation/render works in
	// tile space, keeping pixels confined to CoordMath.
	ChunkMap        c_chunks;
	int             c_chunk_tiles;
	int             c_chunk_margin;

	// World size (Normal / Big / Enormous) and its cached bounds. The world is a
	// finite square of chunks centred on the tile origin, so streaming and every
	// tile query stay inside it. Recomputed by refreshWorldBounds on a size change.
	MapSize         m_map_size;
	sf::IntRect     c_world_tiles;      // world rect in tiles
	sf::IntRect     c_world_pixels;     // world rect in world pixels
	void            refreshWorldBounds();

	// SHARED variables
	std::atomic<sf::Vector2i>       s_camera_position;
	std::atomic<sf::Vector2i>       s_view_size;
	std::atomic<bool>               s_running{ true };
	// Half the world side, in tiles. The workers read this to skip chunks that
	// lie outside the finite world; it is atomic so a size change on the main
	// thread cannot tear the value a worker is reading. A chunk is inside the
	// world when -half <= position < half on both axes (positions are chunk
	// aligned, so this is exactly "the chunk's tiles are all inside").
	std::atomic<int>                s_world_half_tiles{ 0 };

	// THREAD variables
	BS::thread_pool<>                               t_threads{ 3 };
	mutable std::mutex                              t_mutex;
	SharedContainer<std::shared_ptr<Chunk>>         tc_chunks_ready;
	SharedContainer<sf::Vector2i>                   tc_chunks_in_queue;

	// TERRAIN variables
	MapConfig       m_config;
	int             m_seed;         // terrain seed, applied to m_terrain by setNoises()
	GenerateTerrain m_terrain;      // holds a reference to m_config; declared after it

	// INHERITED variables
	int& i_frames;

	// DEBUG variables
	bool            d_wire_frame{ false };

	// When set, stream() fills the queue and generates chunks on the calling
	// thread instead of waiting on the worker threads. The workers pace
	// themselves in real time, which starves a headless run that advances the
	// clock far faster than real time; synchronous mode keeps chunk delivery
	// in step with the sim. The game leaves this off.
	bool            m_synchronous{ false };

	// Chunks in view, recomputed by stream(). render() draws this list after
	// stream() has updated it; a headless caller streams without drawing.
	std::vector<std::shared_ptr<Chunk>> m_visible_chunks;

	// GENERATE MAP SUPPORT FUNCTIONS
	std::shared_ptr<Chunk>  generateChunk(int tiles_per_side, const sf::Vector2i& tile_position);
	// Shared tail of every constructor: derive the chunk sizing from the config
	// and spin up the workers unless synchronous.
	void                    launch();
	void                    startChunksGenerator();
	// Chunk key (top-left tile) that contains a tile. Floors toward -inf so the
	// lookup matches how chunks are actually keyed, unlike getNextChunkPosition.
	sf::Vector2i            chunkOf(const sf::Vector2i& tile) const;
	// (Re)build a chunk's triangles from its tile_types by greedy meshing
	// equal-coloured tiles into rectangles.
	void                    buildChunkVertices(Chunk& chunk);

	sf::Vector2i            worldToTile(sf::Vector2i pos) const;
	sf::Vector2i            tileToWorld(sf::Vector2i tile) const;

public:
	// RESET VARIABLE
	bool m_reset{ false };

	// CONSTRUCTORS
	MapGenerator(int& frames, const std::string& map_file)
		: MapGenerator(frames, map_file, /*synchronous=*/false)
	{
	}

	// As above, but with the option to generate chunks synchronously on the
	// calling thread. A headless run that advances the clock far faster than real
	// time uses that so chunk delivery keeps up with the simulation.
	MapGenerator(int& frames, const std::string& map_file, bool synchronous)
		: m_config(loadMapConfig(map_file))
		, m_seed(m_config.seed)
		, m_terrain(m_config)
		, i_frames(frames)
		, m_synchronous(synchronous)
	{
		c_chunk_tiles = m_config.chunk_tile_size;
		c_chunk_margin = m_config.chunk_margin;
		m_map_size = m_config.map_size;
		refreshWorldBounds();
		launch();
	}

	// Build directly from a MapConfig value, so a run can be recreated from the
	// menu's captured settings instead of re-reading the map file. The config is
	// the single source of truth for terrain and seed.
	MapGenerator(int& frames, MapConfig config, bool synchronous = false)
		: m_config(std::move(config))
		, m_seed(m_config.seed)
		, m_terrain(m_config)
		, i_frames(frames)
		, m_synchronous(synchronous)
	{
		c_chunk_tiles = m_config.chunk_tile_size;
		c_chunk_margin = m_config.chunk_margin;
		m_map_size = m_config.map_size;
		refreshWorldBounds();
		launch();
	}

	// DECONSTRUCTOR
	~MapGenerator()
	{
		// Stop the workers and wait for them before any member is destroyed.
		// The thread pool is declared before t_mutex and the shared containers,
		// so it is destroyed last; without this wait a worker could still touch
		// those members after they are gone.
		s_running = false;
		t_threads.wait();
	}

	// RENDERING
	// Update the chunk stream (load new chunks, evict far ones) for a view,
	// with no drawing and no GL: the headless simulation runs entirely on
	// this. Terrain sampling and the workers never needed a render context.
	void stream(const sf::IntRect& viewBounds);
	void render(const sf::IntRect& viewBounds, sf::RenderTarget& window);
	void fillQueueChunks();
	// One pass of each worker's job, with no loop and no sleep, so stream()
	// can run them inline when m_synchronous is set.
	void queueMissingChunks();
	void generateQueuedChunks();

	// SETTERS
	void setSeed(int seed = Random::get(1, 1000000))
        {
                m_seed = seed;
                // Keep the live config in step so a snapshot of it (the menu) carries
                // the same seed the world was built with.
                m_config.seed = seed;
        }
	void setNoises();

	// Change the world size (Normal / Big / Enormous). Recomputes the bounds and
	// drops the loaded chunks, so the next stream rebuilds the view in the new
	// world. A size change is a whole-world change; the caller also marks the map
	// for a reset so the HUD/seed state stays consistent.
	void setMapSize(MapSize size);

	// Terrain tuning, named the way the HUD presents it. Each setter writes the
	// live config, which the sampler reads on the next sample; a change only
	// needs a chunk reset (m_reset) to take effect.
	void setLandAmount(float amount)         { m_config.land_amount = amount; }
	void setContinentSize(float size)        { m_config.continent_size = size; }
	void setCoastRoughness(float roughness)  { m_config.coast_roughness = roughness; }

	void setMountainHeight(float height)     { m_config.mountain_height = height; }
	void setMountainScale(float scale)       { m_config.mountain_scale = scale; }

	void setTemperature(float value)         { m_config.temperature = value; }
	void setRainfall(float value)            { m_config.rainfall = value; }
	void setSnowLine(float value)            { m_config.snow_line = value; }

	void setLakeLevel(float level)           { m_config.lake_level = level; }
	void setLakeSize(float size)             { m_config.lake_size = size; }

	void setOreRichness(float richness)      { m_config.ore_richness = richness; }

	bool setTileColor(const sf::Vector2i& pos, const Elements& new_element);
	bool setChunkUnload(const sf::Vector2i& pos, bool unload);

	// Take one unit of wood from the forest tile under a world position.
	// Returns the wood remaining on the tile after the harvest; when it reaches
	// zero the stand is cleared and the tile becomes a hill (the mesh is rebuilt
	// so the change renders immediately). Returns -1 when the tile no longer
	// holds a tree (depleted, or not a forest), so the caller can skip crediting a
	// trip to a stale target.
	int harvestWood(const sf::Vector2i& pos);

	// Remaining wood on the forest tile under a world position, or 0 when the
	// tile holds no tree. For the tile info readout.
	int woodAt(const sf::Vector2i& pos) const;

	// Overwrite the palette entries for building elements with the colors the
	// building catalog declares, so a structure renders with its JSON color.
	// Terrain colors are untouched: only the pairs passed in change. Call once
	// after the map and the entity manager exist.
	void applyBuildingColors(const std::vector<std::pair<Elements, sf::Color>>& colors);

	// DEBUG
	void setDebugWireFrame(bool status) { d_wire_frame = status; }
	void print()
	{
		LOG_INFO("Seed: {}.", m_seed);
		LOG_INFO("Land amount: {}.", m_config.land_amount);
		LOG_INFO("Continent size: {}.", m_config.continent_size);
		LOG_INFO("Mountain height: {}.", m_config.mountain_height);
		LOG_INFO("Temperature: {}.", m_config.temperature);
		LOG_INFO("Rainfall: {}.", m_config.rainfall);
	}

	// GETTERS
	Elements       getBiomeElement(const sf::Vector2i& coord);
	// Authoritative element at a world position: reads the loaded chunk's
	// tile_types (the source of truth after any edit) and falls back to a fresh
	// noise sample only where no chunk is loaded. Prefer this over
	// getBiomeElement for anything that must agree with the rendered map.
	Elements       getElementAtWorld(const sf::Vector2i& coord) const;
	// Resource noise value in [0,1] at a world position. Returns 0 for a
	// non-resource element.
	float          getResourceValue(const sf::Vector2i& coord, Elements resource) const;
	sf::Color      getBiomeColor(const sf::Vector2i& coord);
	int            getTileSize() const { return m_config.tile_size_px; }
	int            getSeed()     const { return m_seed; }

	// World size and its bounds. `getWorldTileBounds` / `getWorldPixelBounds`
	// return the finite world rectangle the camera and streaming clamp to;
	// `containsTile` / `containsWorld` answer whether a point lies inside it.
	MapSize        getMapSize()        const { return m_map_size; }
	sf::IntRect    getWorldTileBounds()  const { return c_world_tiles; }
	sf::IntRect    getWorldPixelBounds() const { return c_world_pixels; }
	bool           containsTile(const sf::Vector2i& tile) const
	{
		return c_world_tiles.contains(tile);
	}
	bool           containsWorld(const sf::Vector2i& world) const
	{
		return c_world_pixels.contains(world);
	}

	// The live terrain config, so a caller can capture the tuned world (the
	// menu) or rebuild one from it.
	const MapConfig& getConfig() const { return m_config; }
	float          getTileCost(const sf::Vector2i& pos);
	std::vector<std::string> getPositionInfo(sf::Vector2i pos);
	sf::Vector2i   getLocationWithinBound(sf::Vector2i& pos, float radius);
	std::unordered_map<Elements, sf::Vector2i> getResourcesWithinBoundary(const sf::Vector2i& pos, float radius) const;
        // Copy the elements of a square block of tiles into `out` as a dense
        // row-major buffer (index y * side + x). Takes the chunk-map lock once for
        // the whole block, so a caller that needs many tile reads (pathfinding)
        // does not re-lock per tile. `side` is clamped to kMaxTileBlock; the
        // clamped side is returned so the caller can index `out`.
        static constexpr int kMaxTileBlock = 256;
        int copyTileBlock(const sf::Vector2i& topLeftTile, int side,
                          std::vector<Elements>& out) const;

	bool getDebugWireFrame()   const { return d_wire_frame; }
};
