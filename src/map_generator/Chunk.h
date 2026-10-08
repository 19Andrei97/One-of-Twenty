#pragma once

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstddef>
#include <memory>
#include <unordered_map>

// Hash for sf::Vector2i so chunks and tiles can be keyed by tile coordinate.
struct Vector2iHash {
	std::size_t operator()(const sf::Vector2i& v) const noexcept {
		std::size_t h1 = std::hash<int>()(v.x);
		std::size_t h2 = std::hash<int>()(v.y);
		return h1 ^ (h2 << 1);
	}
};

enum class Elements
{
	very_deep_ocean = 0,
	deep_ocean,
	ocean,

	sand,
	hill,
	forest,
	mountain,
	snow,

	clay,
	iron,
	silver,

	// Placed buildings. These are written into tile_types by the economy when a
	// structure is built, so they render and persist like any other edit.
	farm,
	workshop,

	test,

	count	// number of elements; not a real element
};

// Number of entries in an array indexed by Elements.
inline constexpr std::size_t kElementCount = static_cast<std::size_t>(Elements::count);

// A square block of the world. `tile_types` is the authoritative per-tile map
// (tile space); `vertices` is only ever a mesh derived from it, so an edit made
// through MapGenerator::setTileColor stays in sync with what is drawn.
struct Chunk {
	sf::Vector2i    position;                       // top left position of chunk, in tiles
	sf::VertexArray vertices;                       // the map in vertices ready to draw
	std::unordered_map<sf::Vector2i, Elements, Vector2iHash> tile_types;
	bool unload{ true };
};

using ChunkMap = std::unordered_map<sf::Vector2i, std::shared_ptr<Chunk>, Vector2iHash>;
