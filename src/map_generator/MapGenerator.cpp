#include <pch.h>

#include "MapGenerator.h"

/*
*	Decide which color for the biome.
*/
sf::Color MapGenerator::getBiomeColor(const sf::Vector2i& coord) {

	sf::Vector2f coord_f = static_cast<sf::Vector2f>(coord);

	// Generate noise and wrap for natural environment
	float warpX = coord_f.x + m_noise_wrap.GetNoise(coord_f.x, coord_f.y) * 100.0f;
	float warpY = coord_f.y + m_noise_wrap.GetNoise(coord_f.x, coord_f.y) * 100.0f;
	float continent = (m_noise_continent.GetNoise(warpX * m_cont_multiplier, warpY * m_cont_multiplier) + 1.0f) * 0.5f;

	// Generate mineral noise
	float mineral = (m_noise_mineral.GetNoise(warpX * m_mineral_multiplier, warpY * m_mineral_multiplier) + 1.0f) * 0.5f;

	// --- OCEAN ---

	if (continent < m_thresholds[Elements::very_deep_ocean]) return m_biomes[Elements::very_deep_ocean];
	if (continent < m_thresholds[Elements::deep_ocean]) return m_biomes[Elements::deep_ocean];
	if (continent < m_thresholds[Elements::ocean]) return m_biomes[Elements::ocean];
	if (continent < m_thresholds[Elements::sand]) return m_biomes[Elements::sand];

	// --- CONTINENT ---
	if (continent < m_thresholds[Elements::hill])
	{
		if (mineral > m_thresholds[Elements::clay])
			return m_biomes[Elements::clay];

		return m_biomes[Elements::hill];
	}

	if (continent < m_thresholds[Elements::forest])
	{
		if (mineral > m_thresholds[Elements::iron])
			return m_biomes[Elements::iron];

		return m_biomes[Elements::forest];
	}


	if (continent < m_thresholds[Elements::muntain])
	{
		if (mineral > m_thresholds[Elements::silver])
			return m_biomes[Elements::silver];

		return m_biomes[Elements::muntain];
	}


	return m_biomes[Elements::snow];
}

/*
*	Generate a chunk of terrain based on height and width in tiles. Position is the top left of the chunk in world.
*/
std::shared_ptr<MapGenerator::Chunk> MapGenerator::generateChunk(const int height, const int width, const sf::Vector2i& position)
{
	auto chunk		= std::make_shared<Chunk>();
	chunk->position = position;
	chunk->vertices.setPrimitiveType(sf::PrimitiveType::Triangles);

	// Step 1: store colors of all tiles
	std::vector<std::vector<sf::Color>> colors(height * width, std::vector<sf::Color>(height * width));

	for (int ty = 0; ty < height * width; ty += m_tile_size_px)
	{
		for (int tx = 0; tx < height * width; tx += m_tile_size_px)
		{
			sf::Vector2i world{ position.x + tx , position.y + ty };

			colors[ty / m_tile_size_px][tx / m_tile_size_px] = getBiomeColor(world);

			// TO BE IMPROVED, TEST
			for (auto& [el, val] : m_biomes)
			{
				if (val == colors[ty / m_tile_size_px][tx / m_tile_size_px])
					chunk->tile_types[sf::Vector2i{ ty / m_tile_size_px, tx / m_tile_size_px }] = el;
			}
		}
	}

	int tiles_per_side{ height * width / m_tile_size_px };

	// Step 2: scan rectangles of same color
	std::vector<std::vector<bool>> visited(tiles_per_side, std::vector<bool>(tiles_per_side, false));

	for (int y = 0; y < tiles_per_side; ++y)
	{
		for (int x = 0; x < tiles_per_side; ++x)
		{
			if (visited[y][x]) continue;

			sf::Color base = colors[y][x];

			// Step 1: find maximum possible width
			int maxW = 0;
			while (x + maxW < tiles_per_side &&
				colors[y][x + maxW] == base &&
				!visited[y][x + maxW]) {
				maxW++;
			}

			int bestW = 1, bestH = 1, bestArea = 1;

			// Step 2: expand downward row by row
			int h = 0;
			bool expand = true;
			while (expand && y + h < tiles_per_side) {
				// check row y+h for consistency up to current maxW
				for (int w = 0; w < maxW; ++w) {
					if (colors[y + h][x + w] != base || visited[y + h][x + w]) {
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

			// Step 3: emit rectangle
			float worldX = position.x + x * m_tile_size_px;
			float worldY = position.y + y * m_tile_size_px;
			float wpx = bestW * m_tile_size_px;
			float hpx = bestH * m_tile_size_px;

			chunk->vertices.append({ {worldX,       worldY},       base });
			chunk->vertices.append({ {worldX + wpx, worldY},       base });
			chunk->vertices.append({ {worldX + wpx, worldY + hpx}, base });

			chunk->vertices.append({ {worldX,       worldY},       base });
			chunk->vertices.append({ {worldX + wpx, worldY + hpx}, base });
			chunk->vertices.append({ {worldX,       worldY + hpx}, base });

			// Step 4: mark visited
			for (int yy = 0; yy < bestH; ++yy)
				for (int xx = 0; xx < bestW; ++xx)
					visited[y + yy][x + xx] = true;
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
	
		auto chunk = generateChunk(c_chunk_size, c_chunk_size, *optChunkPos);

		std::lock_guard<std::mutex> lock(t_mutex);
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
	int num_tiles_per_chunk = c_chunk_size * c_chunk_size;
	sf::Vector2i chunk_alligned_position = getNextChunkPosition(viewBounds.position, num_tiles_per_chunk);

	// UPDATE in case of changes, only every 20 frames
	if (m_reset && i_frames % 20 == 0)
	{
		m_reset = false;

		setNoises();
		print();

		std::lock_guard<std::mutex> lock(t_mutex);
		c_chunks.clear();
	}
	
	// Send camera data to worker
	s_camera_position.store(chunk_alligned_position);
	s_view_size.store(viewBounds.size);

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

			if (chunkInView(pos, { num_tiles_per_chunk, num_tiles_per_chunk }, viewBounds)) 
			{
				visibleChunks.push_back(it->second);
				++it;
			}
			else 
			{
				// Calculate chunk distance from view
				sf::Vector2i chunkCenter = pos + sf::Vector2i(num_tiles_per_chunk / 2, num_tiles_per_chunk / 2);
				sf::Vector2i viewCenter = viewBounds.position + (viewBounds.size / 2);

				int dx = std::abs(chunkCenter.x - viewCenter.x) / num_tiles_per_chunk;
				int dy = std::abs(chunkCenter.y - viewCenter.y) / num_tiles_per_chunk;

				if (dx > (viewBounds.size.x / num_tiles_per_chunk) / 2 + c_chunk_margin ||
					dy > (viewBounds.size.y / num_tiles_per_chunk) / 2 + c_chunk_margin) 
				{
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
		sf::Vector2i alignedPos = s_camera_position.load();
		sf::Vector2i viewSize	= s_view_size.load();
		int num_tiles_per_chunk	{ c_chunk_size * c_chunk_size };
		int num_tile_plus		{ num_tiles_per_chunk * c_chunk_margin };

		// Find missing chunks and add them to the queue
		for (int y = alignedPos.y - num_tile_plus; y < alignedPos.y + viewSize.y + num_tile_plus; y += num_tiles_per_chunk)
		{
			for (int x = alignedPos.x - num_tile_plus; x < alignedPos.x + viewSize.x + num_tile_plus; x += num_tiles_per_chunk)
			{
				sf::Vector2i chunkPos(x, y); 
				sf::Vector2i chunkPosTile{ worldToTile(chunkPos)};

				LOG_DEBUG("Chunk Position in World: {} {}", chunkPos.x, chunkPos.y);
				LOG_DEBUG("Chunk Position in Tile: {} {}", chunkPosTile.x, chunkPosTile.y);

				
				{
					std::lock_guard<std::mutex> lock(t_mutex);
					if (c_chunks.find(chunkPos) != c_chunks.end() || tc_chunks_ready.containsPosition(chunkPos) || tc_chunks_in_queue.contains(chunkPos))
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
}

/*
*	Return the information at the requested map position.
*/
std::vector<std::string> MapGenerator::getPositionInfo(sf::Vector2i pos)
{
    std::vector<std::string> result;

    int num_tiles_per_chunk = c_chunk_size * c_chunk_size;
    sf::Vector2i chunkPos = getNextChunkPosition
							(
								sf::Vector2i(static_cast<int>(pos.x), static_cast<int>(pos.y)),
								num_tiles_per_chunk
							);

    auto it = c_chunks.find(chunkPos);
    if (it == c_chunks.end() || !it->second)
    {
        result.push_back("Unknown");
        return result;
    }

    // Find world tile and biome color
    sf::Vector2i tile = worldToTile(pos);
    sf::Vector2i tileWorld = tileToWorld(tile);
    sf::Color tileColor = getBiomeColor(pos);

    for (const auto& [key, color] : m_biomes) 
	{
        if (color == tileColor)
        {
			result.push_back("Type: " + std::to_string(static_cast<int>(key)));
            break;
        }
    }

    if(result.empty())
        result.push_back("Unknown");
    else
    {
        result.push_back("X: " + std::to_string(static_cast<int>(tileWorld.x)));
        result.push_back("Y: " + std::to_string(static_cast<int>(tileWorld.y)));
    }

    return result;
}

/*
*	Return a random coord in px in the provided radius != than water
*/
sf::Vector2i MapGenerator::getLocationWithinBound(sf::Vector2i& pos, float radius)
{
	int num_tiles_per_chunk = c_chunk_size * c_chunk_size;
	sf::Vector2i chunkPos = getNextChunkPosition(pos, num_tiles_per_chunk);

	auto it = c_chunks.find(chunkPos);
	if (it == c_chunks.end() || !it->second)
	{
		return pos; // If chunk is not found return current position
	}

	sf::Color tileColor;
	sf::Vector2i random{ 0, 0 };

	while (tileColor.r == 0)
	{
		random.x = Random::get<int, int, int>(pos.x - radius, pos.x + radius);
		random.y = Random::get<int, int, int>(pos.y - radius, pos.y + radius);

		tileColor = getBiomeColor(random);
	}

	return random;
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

			sf::Color color = getBiomeColor(tileWorldPos);

			for (const auto& [element, biomeColor] : m_biomes)
			{
				if ((element == Elements::ocean || element == Elements::hill) && color == biomeColor)
				{
					auto it = closest.find(element);
					if (it == closest.end() || dist < it->second.first)
					{
						closest[element] = { dist, tileWorldPos }; // store world coords
					}
					break; // biome found → skip rest
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
	sf::Vector2i chunkPos = getNextChunkPosition(pos, c_chunk_size * c_chunk_size);

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

	sf::Color tileColor{ getBiomeColor(tile) };

	if (tileColor == m_biomes[Elements::hill])
		return 1;
	if (tileColor == m_biomes[Elements::forest])
		return 0.8;
	if (tileColor == m_biomes[Elements::sand])
		return 0.5;
	if (tileColor == m_biomes[Elements::muntain])
		return 0.5;
	if (tileColor == m_biomes[Elements::snow] || tileColor == m_biomes[Elements::ocean])
		return 0.3;

	
	return 0;
}


//Cambia il colore di una tile specifica nella mappa.
bool MapGenerator::setTileColor(const sf::Vector2i& pos, const Elements& new_element)
{
	sf::Vector2i chunkPos = getNextChunkPosition(
		sf::Vector2i(static_cast<int>(pos.x), static_cast<int>(pos.y)),
		c_chunk_size * c_chunk_size
	);

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
	return sf::Vector2i
	(
		static_cast<int>(pos.x) / m_tile_size_px,
		static_cast<int>(pos.y) / m_tile_size_px
	);
}

sf::Vector2i MapGenerator::tileToWorld(sf::Vector2i tile) const 
{
	return sf::Vector2i
	(
		tile.x * m_tile_size_px,
		tile.y * m_tile_size_px
	);
}