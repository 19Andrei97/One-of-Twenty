#include <pch.h>

#include "MapGenerator.h"

/*
*       Generate a chunk of terrain, tiles_per_side square. tile_position is the top
*       left tile of the chunk; pixels are derived only when emitting vertices.
*/
std::shared_ptr<Chunk> MapGenerator::generateChunk(const int tiles_per_side, const sf::Vector2i& tile_position)
{
	auto chunk = std::make_shared<Chunk>();
	chunk->position = tile_position;

	// Fill the authoritative tile map once. The mesh below is only ever a
	// function of this map, so edits made later through setTileColor stay in
	// sync with what is drawn and reported.
	chunk->tile_types.reserve(static_cast<std::size_t>(tiles_per_side) * tiles_per_side);
	for (int ty = 0; ty < tiles_per_side; ++ty)
		for (int tx = 0; tx < tiles_per_side; ++tx)
		{
			const sf::Vector2i tile{ tile_position.x + tx, tile_position.y + ty };
			chunk->tile_types[tile] = m_terrain.elementAtTile(tile);
		}

	buildChunkVertices(*chunk);

	return chunk;
}

/*
*       Greedy-mesh a chunk's tile_types into triangles. Equal-coloured runs are
*       merged into rectangles; a flat, bit-packed visited grid avoids re-emitting a
*       tile and keeps the pass to a single allocation. Pixels are produced only here.
*/
void MapGenerator::buildChunkVertices(Chunk& chunk)
{
	chunk.vertices.clear();
	chunk.vertices.setPrimitiveType(sf::PrimitiveType::Triangles);

	const int side = c_chunk_tiles;
	const std::size_t tile_count = static_cast<std::size_t>(side) * side;

	std::vector<sf::Color> colors(tile_count);
	for (int y = 0; y < side; ++y)
		for (int x = 0; x < side; ++x)
		{
			const auto it = chunk.tile_types.find({ chunk.position.x + x, chunk.position.y + y });
			const Elements element = (it != chunk.tile_types.end()) ? it->second : Elements::very_deep_ocean;
			colors[static_cast<std::size_t>(y) * side + x] = m_config.biome_colors[static_cast<std::size_t>(element)];
		}

	std::vector<bool> visited(tile_count, false);

	for (int y = 0; y < side; ++y)
	{
		for (int x = 0; x < side; ++x)
		{
			if (visited[static_cast<std::size_t>(y) * side + x]) continue;

			const sf::Color base = colors[static_cast<std::size_t>(y) * side + x];

			// Step 1: find maximum possible width
			int maxW = 0;
			while (x + maxW < side &&
				colors[static_cast<std::size_t>(y) * side + (x + maxW)] == base &&
				!visited[static_cast<std::size_t>(y) * side + (x + maxW)]) {
				maxW++;
			}

			int bestW = 1, bestH = 1, bestArea = 1;

			// Step 2: expand downward row by row
			int h = 0;
			bool expand = true;
			while (expand && y + h < side) {
				// check row y+h for consistency up to current maxW
				for (int w = 0; w < maxW; ++w) {
					const std::size_t idx = static_cast<std::size_t>(y + h) * side + (x + w);
					if (colors[idx] != base || visited[idx]) {
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

			const sf::Vector2i rectWorld = tileToWorld({ chunk.position.x + x, chunk.position.y + y });
			float worldX = static_cast<float>(rectWorld.x);
			float worldY = static_cast<float>(rectWorld.y);
			float wpx = static_cast<float>(bestW * m_config.tile_size_px);
			float hpx = static_cast<float>(bestH * m_config.tile_size_px);

			chunk.vertices.append({ {worldX,       worldY},       base });
			chunk.vertices.append({ {worldX + wpx, worldY},       base });
			chunk.vertices.append({ {worldX + wpx, worldY + hpx}, base });

			chunk.vertices.append({ {worldX,       worldY},       base });
			chunk.vertices.append({ {worldX + wpx, worldY + hpx}, base });
			chunk.vertices.append({ {worldX,       worldY + hpx}, base });

			// Step 4: mark visited
			for (int yy = 0; yy < bestH; ++yy)
				for (int xx = 0; xx < bestW; ++xx)
					visited[static_cast<std::size_t>(y + yy) * side + (x + xx)] = true;
		}
	}
}

/*
*       Generate chunks if needed. (made to run on separate thread)
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
*       Helper function to render. Find next chunk position relative to pos.
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
*       Helper function to render. Check if there is intersection between view and chunk.
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
*       Render chunks based on view boundaries.
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
		// fillQueueChunks dedups against the queue and the ready list, but a chunk
		// can be queued again in the window between the worker popping it and
		// pushing it to the ready list. A second copy would clobber an edited
		// chunk (setTileColor) or a pinned one, so keep what is already loaded.
		if (c_chunks.find((*chunk)->position) == c_chunks.end())
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
		else if (chunk->vertices.getVertexCount() != 0)
			window.draw(chunk->vertices);

		//if (d_wire_frame)
			//window.draw(chunk->wire);
	}

	//std::cout << c_chunks.size() << '\n';
}

/*
*       Function to fill the queue of chunks to generate. (Made to be run on different thread)
*/
void MapGenerator::fillQueueChunks()
{
	while (s_running)
	{
		// All positions below are chunk (tile) coordinates.
		sf::Vector2i alignedPos = s_camera_position.load();
		sf::Vector2i viewSize = s_view_size.load();
		int num_tile_plus{ c_chunk_tiles * c_chunk_margin };

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
*       Re-apply the terrain seed (and, through the live config reference, any
*       frequency changed since construction) to the noise fields.
*/
void MapGenerator::setNoises()
{
	m_terrain.setSeed(m_seed);
}

/*
*       Return the information at the requested map position.
*/
std::vector<std::string> MapGenerator::getPositionInfo(sf::Vector2i pos)
{
	std::vector<std::string> result;

	sf::Vector2i chunkPos = chunkOf(worldToTile(pos));

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

	// Report the stored tile, not a fresh noise lookup: they differ after any
	// edit made through setTileColor.
	const auto tileIt = it->second->tile_types.find(tile);
	const Elements element = (tileIt != it->second->tile_types.end()) ? tileIt->second : getBiomeElement(pos);
	result.push_back("Type: " + std::to_string(static_cast<int>(element)));
	result.push_back("X: " + std::to_string(static_cast<int>(tileWorld.x)));
	result.push_back("Y: " + std::to_string(static_cast<int>(tileWorld.y)));

	return result;
}

/*
*       Return a random coord in px in the provided radius != than water
*/
sf::Vector2i MapGenerator::getLocationWithinBound(sf::Vector2i& pos, float radius)
{
	sf::Vector2i chunkPos = chunkOf(worldToTile(pos));

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
std::unordered_map<Elements, sf::Vector2i> MapGenerator::getResourcesWithinBoundary(const sf::Vector2i& pos, float radius) const
{
	std::unordered_map<Elements, std::pair<float, sf::Vector2i>> closest;
	const sf::Vector2i centerTile = worldToTile(pos);
	const int tileRadius = static_cast<int>(radius / m_config.tile_size_px) + 1;
	// One lock for the whole scan. Taking it per tile (through
	// getElementAtWorld) would re-lock the chunk map once per candidate,
	// and this runs for every entity every frame.
	std::lock_guard<std::mutex> lock(t_mutex);

	for (int dx = -tileRadius; dx <= tileRadius; ++dx)
	{
		for (int dy = -tileRadius; dy <= tileRadius; ++dy)
		{
			const sf::Vector2i tile = centerTile + sf::Vector2i(dx, dy);
			const sf::Vector2i tileWorldPos = tileToWorld(tile);
			const float dist = std::hypot(static_cast<float>(tileWorldPos.x - pos.x),
			                              static_cast<float>(tileWorldPos.y - pos.y));
			if (dist > radius)
				continue;

			// Read the authoritative map (we already hold t_mutex), not a
			// fresh noise sample, and fall back to noise where no chunk is
			// loaded. Remember anything usable (water to drink, land to
			// work), so an entity's memory covers both needs.
			const auto chunkIt = c_chunks.find(chunkOf(tile));
			Elements element = m_terrain.elementAtTile(tile);
			if (chunkIt != c_chunks.end() && chunkIt->second)
			{
				const auto tileIt = chunkIt->second->tile_types.find(tile);
				if (tileIt != chunkIt->second->tile_types.end())
					element = tileIt->second;
			}
			if (!Resources::isResource(element))
				continue;

			auto it = closest.find(element);
			if (it == closest.end() || dist < it->second.first)
				closest[element] = { dist, tileWorldPos };
		}
	}

	// Convert to final result (only closest of each type)
	std::unordered_map<Elements, sf::Vector2i> resources;
	for (const auto& [element, pair] : closest)
		resources[element] = pair.second;
	return resources;
}

// Return the cost of the tile position.
float MapGenerator::getTileCost(const sf::Vector2i& pos)
{
        sf::Vector2i chunkPos = chunkOf(worldToTile(pos));

        std::lock_guard<std::mutex> lock(t_mutex);

        auto it = c_chunks.find(chunkPos);
        if (it == c_chunks.end() || !it->second)
        {
                // No chunk loaded here: fall back to the neutral cost rather than
                // 0, which would freeze the entity for the frame.
                return MoveCost::kDefault;
        }

        // Tile that actually contains pos (floored, so negative positions map
        // correctly).
        const sf::Vector2i tileCoord = worldToTile(pos);

        // Read the authoritative value directly: we already hold t_mutex, so
        // calling getElementAtWorld (which locks) would deadlock.
        const auto tileIt = it->second->tile_types.find(tileCoord);
        const Elements element = (tileIt != it->second->tile_types.end())
                ? tileIt->second
                : m_terrain.elementAtTile(tileCoord);

        return MoveCost::moveCost(element);
}

/*
*       Pin or release the chunk containing a world position. A pinned chunk
*       (unload == false) is kept even when it streams outside the view margin.
*       Returns false if no chunk is loaded at that position.
*/
bool MapGenerator::setChunkUnload(const sf::Vector2i& pos, bool unload)
{
	sf::Vector2i chunkPos = chunkOf(worldToTile(pos));

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
	sf::Vector2i chunkPos = chunkOf(worldToTile(pos));

	std::lock_guard<std::mutex> lock(t_mutex);

	auto it = c_chunks.find(chunkPos);
	if (it == c_chunks.end() || !it->second)
		return false;

	// Find world tile and biome color
	sf::Vector2i tile = worldToTile(pos);

	// Trova la tile corrispondente nel chunk
	auto& chunk = it->second;
	const auto found = chunk->tile_types.find(tile);
	if (found == chunk->tile_types.end())
		return false;

	LOG_DEBUG("Tile updated from {} to {} ", static_cast<int>(found->second), static_cast<int>(new_element));

	found->second = new_element;

	// The mesh is derived from tile_types, so rebuild it or the edit would
	// never show up (and getPositionInfo would disagree with what is drawn).
	buildChunkVertices(*chunk);

	return true;
}

/*
*       Translate coordinates
*/
sf::Vector2i MapGenerator::chunkOf(const sf::Vector2i& tile) const
{
	const auto floorToChunk = [this](int v) {
		const int chunk = static_cast<int>(std::floor(static_cast<float>(v) / static_cast<float>(c_chunk_tiles)));
		return chunk * c_chunk_tiles;
	};
	return { floorToChunk(tile.x), floorToChunk(tile.y) };
}

/*
*       Element queries, delegated to the stateless terrain sampler.
*/
Elements MapGenerator::getBiomeElement(const sf::Vector2i& coord)
{
	return m_terrain.elementAtWorld(coord);
}

Elements MapGenerator::getElementAtWorld(const sf::Vector2i& coord) const
{
	const sf::Vector2i tile = worldToTile(coord);

	// Compute the chunk key before locking: chunkOf is const and touches
	// only c_chunk_tiles, and calling it inside the critical section would
	// do redundant work under the lock.
	const sf::Vector2i chunkPos = chunkOf(tile);

	std::lock_guard<std::mutex> lock(t_mutex);

	auto it = c_chunks.find(chunkPos);
	if (it != c_chunks.end() && it->second)
	{
		const auto tileIt = it->second->tile_types.find(tile);
		if (tileIt != it->second->tile_types.end())
			return tileIt->second;
	}

	return m_terrain.elementAtTile(tile);
}

float MapGenerator::getResourceValue(const sf::Vector2i& coord, Elements resource) const
{
	return m_terrain.resourceValue(coord, resource);
}

sf::Color MapGenerator::getBiomeColor(const sf::Vector2i& coord)
{
	return m_config.biome_colors[static_cast<std::size_t>(m_terrain.elementAtWorld(coord))];
}

sf::Vector2i MapGenerator::worldToTile(sf::Vector2i pos) const
{
	return CoordMath::worldToTile(pos, m_config.tile_size_px);
}

sf::Vector2i MapGenerator::tileToWorld(sf::Vector2i tile) const
{
	return CoordMath::tileToWorld(tile, m_config.tile_size_px);
}
