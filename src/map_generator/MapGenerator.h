#pragma once

#include "Chunk.h"
#include "MapConfig.h"
#include "generate_terrain.h"

#include <BS_thread_pool.hpp>
#include <SharedContainer.h>

#include <SFML/Graphics.hpp>

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

	// SHARED variables
	std::atomic<sf::Vector2i>       s_camera_position;
	std::atomic<sf::Vector2i>       s_view_size;
	std::atomic<bool>               s_running{ true };

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

	// GENERATE MAP SUPPORT FUNCTIONS
	std::shared_ptr<Chunk>  generateChunk(int tiles_per_side, const sf::Vector2i& tile_position);
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
		: m_config(loadMapConfig(map_file))
		, m_seed(m_config.seed)
		, m_terrain(m_config)
		, i_frames(frames)
	{
		c_chunk_tiles = m_config.chunk_tile_size;
		c_chunk_margin = m_config.chunk_margin;

		s_running = true;

		// Generate Threads
		(void)t_threads.submit_task([this] { fillQueueChunks(); }); // Find chunks to create.
		(void)t_threads.submit_task([this] { startChunksGenerator(); });
		(void)t_threads.submit_task([this] { startChunksGenerator(); });
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
	void render(const sf::IntRect& viewBounds, sf::RenderTarget& window);
	void fillQueueChunks();

	// SETTERS
	void setSeed(int seed = Random::get(1, 1000000)) { m_seed = seed; }
	void setNoises();

	void setContFreq(float freq)    { m_config.cont_freq = freq; }
	void setWarpFreq(float freq)    { m_config.warp_freq = freq; }
	void setMineralFreq(float freq) { m_config.mineral_freq = freq; }

	void setContMult(float mult)    { m_config.cont_multiplier = mult; }
	void setMineralMult(float mult) { m_config.mineral_multiplier = mult; }

	bool setTileColor(const sf::Vector2i& pos, const Elements& new_element);
	bool setChunkUnload(const sf::Vector2i& pos, bool unload);

	// DEBUG
	void setDebugWireFrame(bool status) { d_wire_frame = status; }
	void print()
	{
		LOG_INFO("Seed: {}.", m_seed);
		LOG_INFO("Mineral Frequency: {}.", m_config.mineral_freq);
		LOG_INFO("Continent Frequency: {}.", m_config.cont_freq);
		LOG_INFO("Warp Frequency: {}.", m_config.warp_freq);
		LOG_INFO("Mineral Multiplier: {}.", m_config.mineral_multiplier);
		LOG_INFO("Continent Multiplier: {}.", m_config.cont_multiplier);
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
