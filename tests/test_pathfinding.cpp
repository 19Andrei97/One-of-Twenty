#include "Chunk.h"
#include "Pathfinding.h"

#include <doctest/doctest.h>

#include <map>
#include <string>

// Pathfinding is a pure template over a cost/walkability grid, so these cases
// run on small hand-written maps with no MapGenerator, registry or renderer.

namespace
{
// A tiny ASCII grid: '.' is cheap ground, '#' is blocked water, 'x' is rough
// ground (cost 3). Coordinates are (column, row) = (x, y).
struct Grid
{
    std::map<std::pair<int, int>, char> cells;

    static Grid parse(const std::vector<std::string>& rows)
    {
        Grid g;
        for (int y = 0; y < static_cast<int>(rows.size()); ++y)
            for (int x = 0; x < static_cast<int>(rows[y].size()); ++x)
                g.cells[{ x, y }] = rows[y][x];
        return g;
    }

    char at(const sf::Vector2i& tile) const
    {
        const auto it = cells.find({ tile.x, tile.y });
        return (it != cells.end()) ? it->second : '#';
    }

    float cost(const sf::Vector2i& tile) const
    {
        return (at(tile) == 'x') ? 3.f : 1.f;
    }

    bool walkable(const sf::Vector2i& tile) const
    {
        return at(tile) != '#';
    }
};

std::vector<sf::Vector2i> solve(const Grid& grid, const sf::Vector2i& start, const sf::Vector2i& goal)
{
    return Pathfinding::findPath(start, goal,
                                 [&](const sf::Vector2i& t) { return grid.cost(t); },
                                 [&](const sf::Vector2i& t) { return grid.walkable(t); });
}

// Whether every consecutive pair of tiles is adjacent (including diagonally).
bool isContiguous(const std::vector<sf::Vector2i>& path)
{
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const int dx = std::abs(path[i].x - path[i - 1].x);
        const int dy = std::abs(path[i].y - path[i - 1].y);
        if (dx > 1 || dy > 1 || (dx == 0 && dy == 0))
            return false;
    }
    return true;
}
} // namespace

TEST_CASE("path is empty when the goal is blocked")
{
    const Grid grid = Grid::parse({
        "....",
        "..#.",
        "....",
    });

    CHECK(solve(grid, { 0, 0 }, { 2, 1 }).empty());
}

TEST_CASE("path from a tile to itself is a single step")
{
    const Grid grid = Grid::parse({ "...." });
    const auto path = solve(grid, { 1, 0 }, { 1, 0 });
    REQUIRE(path.size() == 1);
    CHECK(path.front() == sf::Vector2i{ 1, 0 });
}

TEST_CASE("straight line across open ground")
{
    const Grid grid = Grid::parse({ "......" });
    const auto path = solve(grid, { 0, 0 }, { 5, 0 });
    REQUIRE_FALSE(path.empty());
    CHECK(path.front() == sf::Vector2i{ 0, 0 });
    CHECK(path.back() == sf::Vector2i{ 5, 0 });
    CHECK(path.size() == 6);
    CHECK(isContiguous(path));
}

TEST_CASE("a wall forces a detour around the blocked straight line")
{
    // A vertical wall of water down the middle columns. The straight line along
    // row 1 from (0,1) to (4,1) is blocked, so the path must detour through the
    // open top or bottom row.
    const Grid grid = Grid::parse({
        ".....",
        ".#.#.",
        ".#.#.",
        ".....",
    });

    const auto path = solve(grid, { 0, 1 }, { 4, 1 });
    REQUIRE_FALSE(path.empty());
    CHECK(path.front() == sf::Vector2i{ 0, 1 });
    CHECK(path.back() == sf::Vector2i{ 4, 1 });
    CHECK(isContiguous(path));

    // The direct route would be length 4; a detour is strictly longer.
    CHECK(path.size() > 4);
    for (const auto& tile : path)
        CHECK(grid.walkable(tile));
}

TEST_CASE("the route prefers cheap ground over rough ground")
{
    // The straight line is short but rough (cost 3 per tile); the detour along
    // the top is longer but cheap. A* must pick the cheap route.
    const Grid grid = Grid::parse({
        "..........",
        ".xxxxxxxx.",
        "..........",
    });

    const auto path = solve(grid, { 0, 1 }, { 9, 1 });
    REQUIRE_FALSE(path.empty());
    for (const auto& tile : path)
        CHECK(grid.at(tile) != 'x');
}

TEST_CASE("no diagonal slip between two blocked tiles")
{
    // (1,0) and (0,1) are blocked, so the corner between (0,0) and (1,1) must
    // not be cut.
    const Grid grid = Grid::parse({
        ".#",
        "#.",
    });

    CHECK(solve(grid, { 0, 0 }, { 1, 1 }).empty());
}

TEST_CASE("unreachable goal returns an empty path")
{
    // The goal sits on an island ringed by water.
    const Grid grid = Grid::parse({
        "........",
        "..####..",
        "..#..#..",
        "..####..",
        "........",
    });

    CHECK(solve(grid, { 0, 0 }, { 3, 2 }).empty());
}

TEST_CASE("the node cap bounds the search")
{
    // A large open grid with a tiny budget cannot reach a far goal.
    std::vector<std::string> rows(50, std::string(50, '.'));
    const Grid grid = Grid::parse(rows);

    const auto path = Pathfinding::findPath(sf::Vector2i{ 0, 0 }, sf::Vector2i{ 49, 49 },
                                            [&](const sf::Vector2i& t) { return grid.cost(t); },
                                            [&](const sf::Vector2i& t) { return grid.walkable(t); },
                                            /* nodeCap */ 10);
    CHECK(path.empty());
}

TEST_CASE("the returned path is a valid walk with a finite length")
{
    const Grid grid = Grid::parse({
        "..........",
        "..##..#...",
        "..#...#...",
        "..#...##..",
        "..........",
    });

    const auto path = solve(grid, { 0, 0 }, { 9, 4 });
    REQUIRE_FALSE(path.empty());
    CHECK(path.front() == sf::Vector2i{ 0, 0 });
    CHECK(path.back() == sf::Vector2i{ 9, 4 });
    CHECK(isContiguous(path));
    for (const auto& tile : path)
        CHECK(grid.walkable(tile));
}
