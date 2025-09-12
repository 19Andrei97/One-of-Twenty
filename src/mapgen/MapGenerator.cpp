#include "pch.h"

#include "MapGenerator.h"

/*
*	Generate a chunk of terrain based on height and width in tiles. Position is the top left of the chunk
*/
std::shared_ptr<MapGenerator::Chunk>& MapGenerator::generateChunk(const int height, const int width, const sf::Vector2i& position)
{
	auto chunk		= std::make_shared<Chunk>();
	chunk->position = position;
	chunk->vertices.setPrimitiveType(sf::PrimitiveType::Triangles);

	// Step 1: store colors of all tiles
	std::vector<std::vector<sf::Color>> colors(m_chunkSize * m_chunkSize, std::vector<sf::Color>(m_chunkSize * m_chunkSize));

	for (int ty = 0; ty < m_chunkSize * m_chunkSize; ty += m_tile_size_px) 
	{
		for (int tx = 0; tx < m_chunkSize * m_chunkSize; tx += m_tile_size_px) 
		{
			float worldX = position.x + tx;
			float worldY = position.y + ty;

			colors[ty / m_tile_size_px][tx / m_tile_size_px] = getBiomeColor(worldX, worldY);
		}
	}

	int tiles_per_side{ m_chunkSize * m_chunkSize / m_tile_size_px };

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

	m_chunks[position] = chunk;

	return chunk;
}

/*
*	Decide which color for the biome.
*/
sf::Color MapGenerator::getBiomeColor(float worldX, float worldY) {

	// Generate noise and wrap for natural environment
	float warpX = worldX + m_warpNoise.GetNoise(worldX, worldY) * 100.0f;
	float warpY = worldY + m_warpNoise.GetNoise(worldX, worldY) * 100.0f;
	float continent = (m_continentNoise.GetNoise(warpX * m_cont_multiplier, warpY * m_cont_multiplier) + 1.0f) * 0.5f;

	// Generate mineral noise
	float mineral = (m_mineralNoise.GetNoise(warpX * m_mineral_multiplier, warpY * m_mineral_multiplier) + 1.0f) * 0.5f;

	// --- OCEAN ---

	if (continent < m_thresholds["very_deep_ocean"]) return m_biomes["very_deep_ocean"];
	if (continent < m_thresholds["deep_ocean"]) return m_biomes["deep_ocean"];
	if (continent < m_thresholds["ocean"]) return m_biomes["ocean"];
	if (continent < m_thresholds["sand"]) return m_biomes["sand"];

	// --- CONTINENT ---
	if (continent < m_thresholds["hills"])
	{
		if (mineral > m_thresholds["clay"])
			return m_biomes["clay"];

		return m_biomes["hills"];
	}

	if (continent < m_thresholds["forest"])
	{
		if (mineral > m_thresholds["iron"])
			return m_biomes["iron"];

		return m_biomes["forest"];
	}
		
		
	if (continent < m_thresholds["mountain"])
	{
		if (mineral > m_thresholds["silver"])
			return m_biomes["silver"];

		return m_biomes["mountain"];
	}
		
		
	return m_biomes["snow"];
}

/*
*	Starts another thread. Check view boundaries and generate chunks if needed.
*/
void MapGenerator::startWorker() {
	while (m_running) {
		sf::Vector2i alignedPos = m_cameraPos.load();
		sf::Vector2i viewSize = m_viewSize.load();
		int num_tiles_per_chunk = m_chunkSize * m_chunkSize;

		// Find missing chunks
		int generated = 0;
		for (int y = alignedPos.y - num_tiles_per_chunk;
			y < alignedPos.y + viewSize.y + num_tiles_per_chunk;
			y += num_tiles_per_chunk)
		{
			for (int x = alignedPos.x - num_tiles_per_chunk;
				x < alignedPos.x + viewSize.x + num_tiles_per_chunk;
				x += num_tiles_per_chunk)
			{
				if (generated >= maxChunksPerCycle) break;

				sf::Vector2i chunkPos(x, y);

				{ // lock to check m_chunks safely
					std::lock_guard<std::mutex> lock(m_chunksMutex);
					if (m_chunks.find(chunkPos) != m_chunks.end()) continue;
				}

				// Create the chunk directly in this thread
				auto chunk = generateChunk(m_chunkSize, m_chunkSize, chunkPos);
				m_readyChunks.push(chunk);
				++generated;
			}
		}

		// Sleep a bit so we’re not burning CPU
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
	int num_tiles_per_chunk = m_chunkSize * m_chunkSize;
	sf::Vector2i chunk_alligned_position = getNextChunkPosition(viewBounds.position, num_tiles_per_chunk);

	// UPDATE in case of changes, only every 20 frames
	if (m_reset && i_frames % 20 == 0)
	{
		m_reset = false;

		setNoises();
		print();

		std::lock_guard<std::mutex> lock(m_chunksMutex);
		m_chunks.clear();
	}
	
	// Send camera data to worker
	m_cameraPos.store(chunk_alligned_position);
	m_viewSize.store(viewBounds.size);

	// Pull ready chunks from worker
	while (true) 
	{
		auto optChunk = m_readyChunks.tryPop();
		if (!optChunk.has_value()) break; // No more chunks ready

		auto& chunk = *optChunk; // Dereference optional
		{
			std::lock_guard<std::mutex> lock(m_chunksMutex);
			m_chunks[chunk->position] = std::move(chunk);
		}
	}

	// Calculate visible chunks
	std::vector<std::shared_ptr<Chunk>> visibleChunks;
	{
		std::lock_guard<std::mutex> lock(m_chunksMutex);

		for (auto it = m_chunks.begin(); it != m_chunks.end(); ) 
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

				if (dx > (viewBounds.size.x / num_tiles_per_chunk) / 2 + m_unloadMargin ||
					dy > (viewBounds.size.y / num_tiles_per_chunk) / 2 + m_unloadMargin) 
				{
					// Too far — unload it
					it = m_chunks.erase(it);
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

		if (d_noise_val)
		{
			for (auto& text : chunk->d_noise) 
			{
				window.draw(*text);
			}
		}
	}

	//std::cout << m_chunks.size() << '\n';
}

/*
*	Re-set seed and frequency for FastNoiseLite obj
*/
void MapGenerator::setNoises()
{
	m_continentNoise.SetSeed(m_seed);
	m_continentNoise.SetFrequency(m_cont_freq);
	
	m_warpNoise.SetSeed(m_seed);
	m_warpNoise.SetFrequency(m_warp_freq);

	m_mineralNoise.SetSeed(m_seed);
	m_mineralNoise.SetFrequency(m_mineral_freq);
}

/*
*	Return the information at the requested map position.
*/
std::vector<std::string> MapGenerator::getPositionInfo(sf::Vector2f pos)
{
    std::vector<std::string> result;

    int num_tiles_per_chunk = m_chunkSize * m_chunkSize;
    sf::Vector2i chunkPos = getNextChunkPosition
							(
								sf::Vector2i(static_cast<int>(pos.x), static_cast<int>(pos.y)),
								num_tiles_per_chunk
							);

    auto it = m_chunks.find(chunkPos);
    if (it == m_chunks.end() || !it->second)
    {
        result.push_back("Unknown");
        return result;
    }

    // Calcola la posizione della tile e il colore del bioma
    sf::Vector2i tile = worldToTile(pos);
    sf::Vector2f tileWorld = tileToWorld(tile);
    sf::Color tileColor = getBiomeColor(pos.x, pos.y);

    for (const auto& [key, color] : m_biomes) 
	{
        if (color == tileColor)
        {
            result.push_back("Type: " + key);
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
*	Return a random coord in the provided radius != than water
*/
sf::Vector2f MapGenerator::getLocationWithinBound(sf::Vector2f& pos, float radius)
{
	int num_tiles_per_chunk = m_chunkSize * m_chunkSize;
	sf::Vector2i chunkPos = getNextChunkPosition
	(
		sf::Vector2i(static_cast<int>(pos.x), static_cast<int>(pos.y)),
		num_tiles_per_chunk
	);

	auto it = m_chunks.find(chunkPos);
	if (it == m_chunks.end() || !it->second)
	{
		return pos; // If chunk is not found return current position
	}

	sf::Color tileColor;
	float random_x;
	float random_y;
	while (tileColor.r == 0)
	{
		random_x = Random::get(static_cast<int>(pos.x - radius), static_cast<int>(pos.x + radius));
		random_y = Random::get(static_cast<int>(pos.y - radius), static_cast<int>(pos.y + radius));

		tileColor = getBiomeColor(random_x, random_y);
	}

	return sf::Vector2f{ random_x , random_y };
}

/*
*	Translate coordinates
*/
sf::Vector2i MapGenerator::worldToTile(sf::Vector2f pos) const 
{
	return sf::Vector2i
	(
		static_cast<int>(pos.x) / m_tile_size_px,
		static_cast<int>(pos.y) / m_tile_size_px
	);
}

sf::Vector2f MapGenerator::tileToWorld(sf::Vector2i tile) const 
{
	return sf::Vector2f
	(
		tile.x * m_tile_size_px,
		tile.y * m_tile_size_px
	);
}