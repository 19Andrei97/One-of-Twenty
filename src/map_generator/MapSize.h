#pragma once

#include <SFML/System/Vector2.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

// Selectable world sizes. The world is a finite square of chunks, so a run has a
// bounded map instead of streaming forever.
//
// The counts are chosen so each step feels like a real jump, not a nudge, while
// staying square (a square world keeps the four directions symmetric and the
// spawn search cheap). One chunk is chunk_tile_size tiles (32 shipped), so a
// side of N chunks is N * 32 tiles, or N * 32 * tile_size pixels.
//
//   Normal    64 x 64 chunks =  2,048 x  2,048 tiles  (~2.0k x 2.0k tiles)
//   Big      128 x 128 chunks =  4,096 x  4,096 tiles  (~4.1k x 4.1k tiles)
//   Enormous 256 x 256 chunks =  8,192 x  8,192 tiles  (~8.2k x 8.2k tiles)
//
// The shipped default (Normal) is deliberately generous: terrain features span
// thousands of tiles (the climate gradient reaches the pole by ~6000 tiles), so a
// world much smaller than 2048 tiles would clip the climate bands and cramp
// exploration. Normal (±1024) and Big (±2048) stay within the temperate band;
// Enormous (±4096) reaches toward the poles. Even a full square of Enormous
// chunks (~65k) is more than a player will visit, and generation is
// demand-driven (only the view is ever generated), so the count only bounds the
// world, it does not pre-generate it.
enum class MapSize
{
        Normal = 0,
        Big,
        Enormous,
        count
};

// Side length in chunks of each size. Indexed by MapSize; keep the order in step
// with the enum. A "square" world has this many chunks on both axes.
inline constexpr int kMapSizeChunks[] = { 64, 128, 256 };

inline constexpr int kMapSizeCount = static_cast<int>(MapSize::count);

// The number of chunks on one side for a size, clamped to the known sizes.
[[nodiscard]] inline int mapSizeChunks(const MapSize size) noexcept
{
        const int index = std::clamp(static_cast<int>(size), 0, kMapSizeCount - 1);
        return kMapSizeChunks[index];
}

// Total chunks in the world for a size (side * side).
[[nodiscard]] inline int mapSizeChunkCount(const MapSize size) noexcept
{
        const int side = mapSizeChunks(size);
        return side * side;
}

// Player-facing name, for the HUD and logs.
[[nodiscard]] inline const char* mapSizeName(const MapSize size) noexcept
{
        switch (size)
        {
                case MapSize::Normal:   return "Normal";
                case MapSize::Big:      return "Big";
                case MapSize::Enormous: return "Enormous";
                default:                return "Normal";
        }
}

// Parse a size from its name (case-insensitive). Throws std::invalid_argument on
// an unknown value so a typo in the config fails loudly instead of silently
// falling back to a different world than the file asked for.
[[nodiscard]] inline MapSize mapSizeFromString(const std::string& name)
{
        std::string lower;
        lower.reserve(name.size());
        for (const char c : name)
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        if (lower == "normal")   return MapSize::Normal;
        if (lower == "big")      return MapSize::Big;
        if (lower == "enormous") return MapSize::Enormous;

        throw std::invalid_argument("Unknown map size: '" + name + "'");
}

// The world rectangle in tile coordinates, centred on the tile origin (0, 0).
// World tile (0, 0) is the world's centre, so the map grows symmetrically around
// the player's starting neighbourhood. The rect is `side` chunks square.
[[nodiscard]] inline sf::IntRect worldTileBounds(const MapSize size, const int chunk_tile_size) noexcept
{
        const int side = mapSizeChunks(size) * std::max(1, chunk_tile_size);
        const int half = side / 2;
        return sf::IntRect(sf::Vector2i{ -half, -half }, sf::Vector2i{ side, side });
}

// The world rectangle in pixel (world) coordinates. Converted through the tile
// bounds so the two always agree; used to clamp the camera and to decide whether
// a streamed view still touches the world.
[[nodiscard]] inline sf::IntRect worldPixelBounds(const MapSize size, const int chunk_tile_size,
                                                  const int tile_size_px) noexcept
{
        const sf::IntRect tiles = worldTileBounds(size, chunk_tile_size);
        return sf::IntRect(tiles.position * tile_size_px, tiles.size * tile_size_px);
}
