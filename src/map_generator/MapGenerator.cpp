#include <pch.h>

#include "MapGenerator.h"

/*
*	Decide which element belongs at a tile coordinate.
*
*	The noise is sampled in tile space so the terrain no longer depends on the
*	pixel tile size (a 16px or 32px tile covers the same world noise). Rivers and
*	island shaping only run when enabled in config, so the base map is unchanged.
*/
Elements MapGenerator::elementAtTile(const sf::Vector2i& tile) const {

	sf::Vector2f coord_f = static_cast<sf::Vector2f>(tile);

	// Generate noise and wrap for natural environment
	float warpX = coord_f.x + m_noise_wrap.GetNoise(coord_f.x, coord_f.y) * 100.0f;
	float warpY = coord_f.y + m_noise_wrap.GetNoise(coord_f.x, coord_f.y) * 100.0f;
	float continent = (m_noise_continent.GetNoise(warpX * m_cont_multiplier, warpY * m_cont_multiplier) + 1.0f) * 0.5f;

	// Island shaping: pull the coast inward so the world is surrounded by water.
	if (m_island.enabled)
		continent *= islandFalloff(tile);

	// --- RIVER ---
	// A river cuts across the map where the river field crosses zero, but only on
	// land (above the deep ocean) so it does not carve through the seabed.
	if (m_river.enabled
		&& continent > m_thresholds.at(Elements::deep_ocean)
		&& continent < m_thresholds.at(Elements::snow))
	{
		const float riverField = m_noise_river.GetNoise(coord_f.x, coord_f.y);

		if (std::abs(riverField) < m_river.value.threshold)
			return Elements::ocean;
	}

	// Generate mineral noise
	float mineral = (m_noise_mineral.GetNoise(warpX * m_mineral_multiplier, warpY * m_mineral_multiplier) + 1.0f) * 0.5f;

	// --- OCEAN ---

	if (continent < m_thresholds.at(Elements::very_deep_ocean)) return Elements::very_deep_ocean;
	if (continent < m_thresholds.at(Elements::deep_ocean)) return Elements::deep_ocean;
	if (continent < m_thresholds.at(Elements::ocean)) return Elements::ocean;
	if (continent < m_thresholds.at(Elements::sand)) return Elements::sand;

	// --- CONTINENT ---
	if (continent < m_thresholds.at(Elements::hill))
	{
		if (mineral > m_thresholds.at(Elements::clay))
			return Elements::clay;

		return Elements::hill;
	}

	if (continent < m_thresholds.at(Elements::forest))
	{
		if (mineral > m_thresholds.at(Elements::iron))
			return Elements::iron;

		return Elements::forest;
	}


	if (continent < m_thresholds.at(Elements::muntain))
	{
		if (mineral > m_thresholds.at(Elements::silver))
			return Elements::silver;

		return Elements::muntain;
	}


	return Elements::snow;
}

/*
*	Radial falloff for island generation. Returns 1 near the origin and drops to
*	0 at the configured edge, so land fades into ocean with a soft coastline.
*/
float MapGenerator::islandFalloff(const sf::Vector2i& tile) const
{
	const float x = static_cast<float>(tile.x) / c_chunk_tiles;
	const float y = static_cast<float>(tile.y) / c_chunk_tiles;
	const float distance = std::sqrt(x * x + y * y);

	// m_island.value is the distance (in chunks) where land gives way to water.
	const float edge = std::max(0.001f, m_island.value);
	return std::clamp(1.0f - distance / edge, 0.0f, 1.0f);
}

/*
*	Decide which color for the biome. Coordinates are world pixels.
*/
sf::Color MapGenerator::getBiomeColor(const sf::Vector2i& coord) {
	return m_biomes[elementAtWorld(coord)];
}

/*
*	Decide which element belongs at a world coordinate.
*/
Elements MapGenerator::elementAtWorld(const sf::Vector2i& coord) const {
	return elementAtTile(worldToTile(coord));
}

/*
*	Decide which element belongs at a world coordinate (public entry point).
*/
Elements MapGenerator::getBiomeElement(const sf::Vector2i& coord) {
	return elementAtWorld(coord);
}

/*
*	Generate a chunk of terrain, tiles_per_side square. tile_position is the top
*	left tile of the chunk; pixels are derived only when emitting vertices.
*/
std::shared_ptr<MapGenerator::Chunk> MapGenerator::generateChunk(const int tiles_per_side, const sf::Vector2i& tile_position)
{
	auto chunk		= std::make_shared<Chunk>();
	chunk->position = tile_position;
	chunk->vertices.setPrimitiveType(sf::PrimitiveType::Triangles);

	const std::size_t tile_count = static_cast<std::size_t>(tiles_per_side) * tiles_per_side;

	// Classify every tile once, into a single flat buffer (row-major). Keeping
	// the element alongside its color lets the greedy pass below compare colors
	// directly instead of re-running the noise or doing a reverse color lookup.
	std::vector<std::pair<Elements, sf::Color>> tiles(tile_count);

	for (int ty = 0; ty < tiles_per_side; ++ty)
	{
		for (int tx = 0; tx < tiles_per_side; ++tx)
		{
			const sf::Vector2i tile{ tile_position.x + tx, tile_position.y + ty };

			const Elements element = elementAtTile(tile);
			tiles[static_cast<std::size_t>(ty) * tiles_per_side + tx] = { element, m_biomes[element] };
			chunk->tile_types[tile] = element;
		}
	}

	// Greedy pass: merge runs of the same color into rectangles. Tiles already
	// covered by a rectangle are marked so the scan never emits them twice. The
	// visited grid is flat and bit-packed, one allocation for the whole chunk.
	std::vector<bool> visited(tile_count, false);

	for (int y = 0; y < tiles_per_side; ++y)
	{
		for (int x = 0; x < tiles_per_side; ++x)
		{
			if (visited[static_cast<std::size_t>(y) * tiles_per_side + x]) continue;

			const sf::Color base = tiles[static_cast<std::size_t>(y) * tiles_per_side + x].second;

			// Step 1: find maximum possible width
			int maxW = 0;
			while (x + maxW < tiles_per_side &&
				tiles[static_cast<std::size_t>(y) * tiles_per_side + (x + maxW)].second == base &&
				!visited[static_cast<std::size_t>(y) * tiles_per_side + (x + maxW)]) {
				maxW++;
			}

			int bestW = 1, bestH = 1, bestArea = 1;

			// Step 2: expand downward row by row
			int h = 0;
			bool expand = true;
			while (expand && y + h < tiles_per_side) {
				// check row y+h for consistency up to current maxW
				for (int w = 0; w < maxW; ++w) {
					const std::size_t idx = static_cast<std::size_t>(y + h) * tiles_per_side + (x + w);
					if (tiles[idx].second != base || visited[idx]) {
						maxW = w; // shrink width if mismatch found
						break;
					}
				}
				if (maxW == 0) break; // no more expansion possible

				h++;
				int area = maxW * h;
				if (area > bestArea) {
					bestArea = area;
					bestW = maxW;
					bestH = h;
				}
			}

			// Step 3: emit rectangle. Tiles are converted to pixels here, the only
			// place generation touches world coordinates.
			const sf::Vector2i rectWorld = tileToWorld({ tile_position.x + x, tile_position.y + y });
			float worldX = static_cast<float>(rectWorld.x);
			float worldY = static_cast<float>(rectWorld.y);
			float wpx = static_cast<float>(bestW * m_tile_size_px);
			float hpx = static_cast<float>(bestH * m_tile_size_px);

			chunk->vertices.append({ {worldX,       worldY},       base });
			chunk->vertices.append({ {worldX + wpx, worldY},       base });
			chunk->vertices.append({ {worldX + wpx, worldY + hpx}, base });

			chunk->vertices.append({ {worldX,       worldY},       base });
			chunk->vertices.append({ {worldX + wpx, worldY + hpx}, base });
			chunk->vertices.append({ {worldX,       worldY + hpx}, base });

			// Step 4: mark visited
			for (int yy = 0; yy < bestH; ++yy)
				for (int xx = 0; xx < bestW; ++xx)
					visited[static_cast<std::size_t>(y + yy) * tiles_per_side + (x + xx)] = true;
		}
	}

	return chunk;
}

/*
*	Generate chunks if needed. (made to run on separate thread)
*/
void MapGenerator::startChunksGenerator()
{ 
	while (s_running) 
	{ 
		std::optional<sf::Vector2i> optChunkPos = tc_chunks_in_queue.pop();

		if (!optChunkPos.has_value()) 
		{ 
			std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
			continue; 
		} 
	
		auto chunk = generateChunk(c_chunk_tiles, *optChunkPos);

		// tc_chunks_ready has its own mutex; taking t_mutex here would only add
		// contention on the chunk map for no benefit.
		tc_chunks_ready.push(chunk);
	} 
}

/*
*	Helper function to render. Find next chunk position relative to pos.
*/
sf::Vector2i getNextChunkPosition(const sf::Vector2i& pos, int multiple) {
	auto next = [multiple](int p)->int {
		int r = p % multiple;
		if (r == 0) return p;
		return (p > 0) ? (p + (multiple - r)) : (p - r);
		};
	return { next(pos.x), next(pos.y) };
}

/*
*	Helper function to render. Check if there is intersection between view and chunk.
*/
bool chunkInView(const sf::Vector2i& chunkPos, const sf::Vector2i& chunkSize, const sf::IntRect& viewBounds)
{
	const int preload = chunkSize.x; // preload 1 chunk around

	sf::IntRect expandedView = viewBounds;
	expandedView.position.x -= preload * 4;
	expandedView.position.y -= preload * 4;
	expandedView.size.x += preload * 8;
	expandedView.size.y += preload * 8;

	sf::IntRect chunkBounds(
		sf::Vector2i{ chunkPos.x , chunkPos.y },
		sf::Vector2i{ chunkSize.x , chunkSize.y }
	);

	return chunkBounds.findIntersection(expandedView) != std::nullopt;
}

/*
*	Render chunks based on view boundaries.
*/
void MapGenerator::render(const sf::IntRect& viewBounds, sf::RenderTarget& window) {
	// Convert the pixel view into tile space once, then stay in tiles for the
	// rest of the frame (chunk keys, hit tests, eviction distances).
	const sf::Vector2i viewTopLeft = worldToTile(viewBounds.position);
	const sf::Vector2i viewBottomRight = worldToTile(viewBounds.position + viewBounds.size);
	const sf::Vector2i viewTileSize = viewBottomRight - viewTopLeft;

	const sf::Vector2i chunk_alligned_position = getNextChunkPosition(viewTopLeft, c_chunk_tiles);
	const sf::IntRect viewTiles{ chunk_alligned_position, viewTileSize };

	// UPDATE in case of changes, only every 20 frames
	if (m_reset && i_frames % 20 == 0)
	{
		m_reset = false;

		setNoises();
		print();

		std::lock_guard<std::mutex> lock(t_mutex);
		c_chunks.clear();
	}
	
	// Send camera data to worker (tile space)
	s_camera_position.store(chunk_alligned_position);
	s_view_size.store(viewTileSize);

	// Pull ready chunks from worker
	while (true) 
	{
		if (tc_chunks_ready.empty()) break; // No more chunks ready

		auto chunk = tc_chunks_ready.pop();

		std::lock_guard<std::mutex> lock(t_mutex);
		c_chunks[(*chunk)->position] = *chunk;
	}

	// Calculate visible chunks
	std::vector<std::shared_ptr<Chunk>> visibleChunks;
	{
		std::lock_guard<std::mutex> lock(t_mutex);

		for (auto it = c_chunks.begin(); it != c_chunks.end(); ) 
		{
			const sf::Vector2i& pos = it->first;

			if (chunkInView(pos, { c_chunk_tiles, c_chunk_tiles }, viewTiles)) 
			{
				visibleChunks.push_back(it->second);
				++it;
			}
			else 
			{
				// Calculate chunk distance from view
				sf::Vector2i chunkCenter = pos + sf::Vector2i(c_chunk_tiles / 2, c_chunk_tiles / 2);
				sf::Vector2i viewCenter = viewTiles.position + (viewTiles.size / 2);

				int dx = std::abs(chunkCenter.x - viewCenter.x) / c_chunk_tiles;
				int dy = std::abs(chunkCenter.y - viewCenter.y) / c_chunk_tiles;

				if (dx > (viewTiles.size.x / c_chunk_tiles) / 2 + c_chunk_margin ||
					dy > (viewTiles.size.y / c_chunk_tiles) / 2 + c_chunk_margin) 
				{
					// Double-check before evicting: a chunk pinned via
					// setChunkUnload (an entity or a pending change still
					// references it) must survive even when it is far away.
					if (it->second && !it->second->unload)
					{
						++it;
						continue;
					}

					// Too far — unload it
					it = c_chunks.erase(it);
				}
				else {
					++it;
				}
			}
		}
	}

	// Draw all chunks in view
	for (auto& chunk : visibleChunks) 
	{
		if (d_wire_frame)
		{
			// Before drawing your map
			window.pushGLStates();
			glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
			glDisable(GL_TEXTURE_2D);

			// Draw your map as usual
			window.draw(chunk->vertices);

			// Restore default fill mode
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			window.popGLStates();
		}
		else if(chunk->vertices.getVertexCount() != 0)
			window.draw(chunk->vertices);

		//if (d_wire_frame)
			//window.draw(chunk->wire);
	}

	//std::cout << c_chunks.size() << '\n';
}

/*
*	Function to fill the queue of chunks to generate. (Made to be run on different thread)
*/
void MapGenerator::fillQueueChunks()
{
	while (s_running)
	{
		// All positions below are chunk (tile) coordinates.
		sf::Vector2i alignedPos = s_camera_position.load();
		sf::Vector2i viewSize	= s_view_size.load();
		int num_tile_plus		{ c_chunk_tiles * c_chunk_margin };

		// Find missing chunks and add them to the queue
		for (int y = alignedPos.y - num_tile_plus; y < alignedPos.y + viewSize.y + num_tile_plus; y += c_chunk_tiles)
		{
			for (int x = alignedPos.x - num_tile_plus; x < alignedPos.x + viewSize.x + num_tile_plus; x += c_chunk_tiles)
			{
				sf::Vector2i chunkPos(x, y); 

				LOG_TRACE("Chunk Position (tile): {} {}", chunkPos.x, chunkPos.y);

				
				{
					std::lock_guard<std::mutex> lock(t_mutex);
					if (c_chunks.find(chunkPos) != c_chunks.end()
						|| tc_chunks_ready.containsIf([&](const std::shared_ptr<Chunk>& chunk) { return chunk && chunk->position == chunkPos; })
						|| tc_chunks_in_queue.contains(chunkPos))
						continue;
				}
				
				tc_chunks_in_queue.push(chunkPos);
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

/*
*	Re-set seed and frequency for FastNoiseLite obj
*/
void MapGenerator::setNoises()
{
	m_noise_continent.SetSeed(m_seed);
	m_noise_continent.SetFrequency(m_cont_freq);
	
	m_noise_wrap.SetSeed(m_seed);
	m_noise_wrap.SetFrequency(m_warp_freq);

	m_noise_mineral.SetSeed(m_seed);
	m_noise_mineral.SetFrequency(m_mineral_freq);

	// Offset the seed so rivers do not align with the continent or mineral noise.
	m_noise_river.SetSeed(m_seed + 1);
	m_noise_river.SetFrequency(m_river_freq);
}

/*
*	Return the information at the requested map position.
*/
std::vector<std::string> MapGenerator::getPositionInfo(sf::Vector2i pos)
{
    std::vector<std::string> result;

    sf::Vector2i chunkPos = getNextChunkPosition(worldToTile(pos), c_chunk_tiles);

    std::lock_guard<std::mutex> lock(t_mutex);

    auto it = c_chunks.find(chunkPos);
    if (it == c_chunks.end() || !it->second)
    {
        result.push_back("Unknown");
        return result;
    }

    // Find world tile and its element
    sf::Vector2i tile = worldToTile(pos);
    sf::Vector2i tileWorld = tileToWorld(tile);

    result.push_back("Type: " + std::to_string(static_cast<int>(getBiomeElement(pos))));
    result.push_back("X: " + std::to_string(static_cast<int>(tileWorld.x)));
    result.push_back("Y: " + std::to_string(static_cast<int>(tileWorld.y)));

    return result;
}

/*
*	Return a random coord in px in the provided radius != than water
*/
sf::Vector2i MapGenerator::getLocationWithinBound(sf::Vector2i& pos, float radius)
{
	sf::Vector2i chunkPos = getNextChunkPosition(worldToTile(pos), c_chunk_tiles);

	std::lock_guard<std::mutex> lock(t_mutex);

	auto it = c_chunks.find(chunkPos);
	if (it == c_chunks.end() || !it->second)
	{
		return pos; // If chunk is not found return current position
	}

	sf::Vector2i random{ 0, 0 };

	// Bounded retry: a region that is entirely water would otherwise spin
	// forever, since only non-water tiles have a non-zero red channel.
	constexpr int max_attempts = 64;
	for (int attempt = 0; attempt < max_attempts; ++attempt)
	{
		random.x = Random::get<int, int, int>(pos.x - radius, pos.x + radius);
		random.y = Random::get<int, int, int>(pos.y - radius, pos.y + radius);

		if (getBiomeColor(random).r != 0)
			return random;
	}

	return pos; // No land tile found within the radius.
}


// Return a map of resources found within the boundaries.
std::unordered_map<Elements, sf::Vector2i> MapGenerator::getResourcesWithinBoundary(sf::Vector2i& pos, float radius)
{
	std::unordered_map<Elements, std::pair<float, sf::Vector2i>> closest;
	sf::Vector2i centerTile = worldToTile(pos);
	int tileRadius = static_cast<int>(radius / m_tile_size_px);

	for (int dx = -tileRadius; dx <= tileRadius; ++dx)
	{
		for (int dy = -tileRadius; dy <= tileRadius; ++dy)
		{
			sf::Vector2i tile = centerTile + sf::Vector2i(dx, dy);
			sf::Vector2i tileWorldPos = tileToWorld(tile);
			float dist = std::hypot(tileWorldPos.x - pos.x, tileWorldPos.y - pos.y);

			if (dist > radius)
				continue;

			const Elements element = getBiomeElement(tileWorldPos);

			if (element == Elements::ocean || element == Elements::hill)
			{
				auto it = closest.find(element);
				if (it == closest.end() || dist < it->second.first)
				{
					closest[element] = { dist, tileWorldPos }; // store world coords
				}
			}
		}
	}

	// Convert to final result (only closest of each type)
	std::unordered_map<Elements, sf::Vector2i> resources;
	for (const auto& [element, pair] : closest)
	{
		resources[element] = pair.second;
	}
	return resources;
}

// Return the cost of the tile position.
float MapGenerator::getTileCost(const sf::Vector2i& pos)
{
	sf::Vector2i chunkPos = getNextChunkPosition(worldToTile(pos), c_chunk_tiles);

	std::lock_guard<std::mutex> lock(t_mutex);

	auto it = c_chunks.find(chunkPos);
	if (it == c_chunks.end() || !it->second)
	{
		return 0; // No chunk found, return the input as fallback
	}

	// Optional: snap pos to the center of the nearest tile
	sf::Vector2i tile
	{ 
		pos.x / m_tile_size_px * m_tile_size_px + m_tile_size_px / 2, 
		pos.y / m_tile_size_px * m_tile_size_px + m_tile_size_px / 2 
	};

	const Elements element{ getBiomeElement(tile) };

	if (element == Elements::hill)
		return 1;
	if (element == Elements::forest)
		return 0.8;
	if (element == Elements::sand)
		return 0.5;
	if (element == Elements::muntain)
		return 0.5;
	if (element == Elements::snow || element == Elements::ocean)
		return 0.3;

	
	return 0;
}


/*
*	Pin or release the chunk containing a world position. A pinned chunk
*	(unload == false) is kept even when it streams outside the view margin.
*	Returns false if no chunk is loaded at that position.
*/
bool MapGenerator::setChunkUnload(const sf::Vector2i& pos, bool unload)
{
	sf::Vector2i chunkPos = getNextChunkPosition(worldToTile(pos), c_chunk_tiles);

	std::lock_guard<std::mutex> lock(t_mutex);

	auto it = c_chunks.find(chunkPos);
	if (it == c_chunks.end() || !it->second)
		return false;

	it->second->unload = unload;
	return true;
}

//Cambia il colore di una tile specifica nella mappa.
bool MapGenerator::setTileColor(const sf::Vector2i& pos, const Elements& new_element)
{
	sf::Vector2i chunkPos = getNextChunkPosition(worldToTile(pos), c_chunk_tiles);

	std::lock_guard<std::mutex> lock(t_mutex);

	auto it = c_chunks.find(chunkPos);
	if (it == c_chunks.end() || !it->second)
		return false;

	// Find world tile and biome color
	sf::Vector2i tile = worldToTile(pos);

	// Trova la tile corrispondente nel chunk
	auto& chunk = it->second;
	for (auto& [key, val] : chunk->tile_types)
	{
		if (static_cast<int>(key.x) == static_cast<int>(tile.x) &&
			static_cast<int>(key.y) == static_cast<int>(tile.y))
		{
			LOG_DEBUG("Tile updated from {} to {} ", static_cast<int>(val), static_cast<int>(new_element));

			val = new_element;

			return true;
		}
	}
	return false;
}

/*
*	Translate coordinates
*/
sf::Vector2i MapGenerator::worldToTile(sf::Vector2i pos) const
{
	return CoordMath::worldToTile(pos, m_tile_size_px);
}

sf::Vector2i MapGenerator::tileToWorld(sf::Vector2i tile) const
{
	return CoordMath::tileToWorld(tile, m_tile_size_px);
}
