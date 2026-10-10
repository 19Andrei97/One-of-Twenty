// A long-horizon soak harness (built as `sim_1000days`, not part of the
// unit_tests suite): run the real settlement simulation for ~1000 in-game days
// and report growth plus the village's shape. Run it by hand under xvfb:
//   cd build/bin && xvfb-run -a ./sim_1000days
#include <pch.h>

#include "EntityManager.h"
#include "GameClock.h"
#include "Logger.h"
#include "MapGenerator.h"
#include "MapStream.h"
#include "Random.h"
#include "Resources.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{
std::string configPath(const char* rel)
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/" + rel;
#else
    return std::string(rel);
#endif
}

struct BuildingTile
{
    sf::Vector2i tile;
    Elements     element;
};

const char* elementName(const Elements e)
{
    switch (e)
    {
        case Elements::farm:        return "farm";
        case Elements::workshop:    return "workshop";
        case Elements::house:       return "house";
        case Elements::city_center: return "city_center";
        case Elements::road:        return "road";
        default:                    return "?";
    }
}
} // namespace

int main()
{
    Logger::init("/tmp/sim1000.log", "info");
    Random::mt.seed(20241007);

    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, configPath("config/map_data.json"));
    map->setSeed(20241007);
    map->setNoises();

    // 120 in-game minutes per real second, stepped 0.5 s per update => exactly one
    // in-game hour per update, the cadence the e2e tests use and the game's own
    // hourly survival systems expect.
    auto clock = std::make_shared<GameClock>(120.f);
    clock->setTime(8, 0);

    float delta = 0.5f;
    EntityManager em(font, map, clock, delta, configPath("config/entity_data.json"),
                     configPath("config/buildings.json"));
    em.seedPopulation();

    // Stream the neighbourhood in once (the game loop renders each frame); the
    // settlement stays within build_radius (24 tiles) of the anchor, well inside
    // this 512px window, so chunks stay loaded for every later setTileColor.
    const sf::Vector2i spawn = em.findHabitableSpawn();
    if (!TestSupport::primeChunks(*map, frames, spawn, 640))
    {
        std::printf("HARNESS: no display / no chunks; aborting\n");
        return 2;
    }

    const int tileSize = map->getTileSize();
    const std::int64_t kTargetDays = 1000;
    const std::int64_t kTargetMinutes = kTargetDays * GameTime::kMinutesPerDay;

    std::int64_t lastReportDay = -1;
    while (clock->getTimestamp() < kTargetMinutes)
    {
        clock->update(delta);
        em.update();
        em.resolveCollisions();

        const std::int64_t day = clock->getTimestamp() / GameTime::kMinutesPerDay;
        if (day / 100 != lastReportDay / 100)
        {
            lastReportDay = day;
            std::printf("day %4lld  pop=%3d/%3d  births=%4d deaths=%4d  food=%5d wood=%5d  "
                        "bld=%3d done=%3d (house=%d farm=%d road=%d) foodProd=%d gathers=%d\n",
                        (long long)day, em.population(), em.populationCapacity(),
                        em.births(), em.deaths(), em.good(Goods::Good::Food),
                        em.good(Goods::Good::Wood), em.buildingCount(), em.completedBuildingCount(),
                        em.countOfElement(Elements::house), em.countOfElement(Elements::farm),
                        em.countOfElement(Elements::road), em.foodProduced(), em.gathersCompleted());
            std::fflush(stdout);
        }
    }

    // ---- Shape: scan the loaded neighbourhood for building tiles -------------
    const sf::Vector2i anchor = em.settlementAnchor().value_or(spawn);
    const sf::Vector2i anchorTile = CoordMath::worldToTile(anchor, tileSize);
    const int scanTiles = 400; // generous: covers the whole 24-tile build radius many times

    std::vector<BuildingTile> buildings;
    std::map<int, int> counts;
    for (int dy = -scanTiles; dy <= scanTiles; ++dy)
        for (int dx = -scanTiles; dx <= scanTiles; ++dx)
        {
            const sf::Vector2i tile = anchorTile + sf::Vector2i{ dx, dy };
            const sf::Vector2i world = CoordMath::tileToWorld(tile, tileSize);
            const Elements e = map->getElementAtWorld(world);
            switch (e)
            {
                case Elements::farm:
                case Elements::workshop:
                case Elements::house:
                case Elements::city_center:
                case Elements::road:
                    buildings.push_back({ tile, e });
                    ++counts[static_cast<int>(e)];
                    break;
                default:
                    break;
            }
        }

    std::printf("\n===== 1000-day report =====\n");
    std::printf("anchor tile: (%d, %d)\n", anchorTile.x, anchorTile.y);
    std::printf("run summary: %s\n", em.runSummary().format().c_str());
    std::printf("population: %d  capacity: %d  births: %d  deaths: %d\n",
                em.population(), em.populationCapacity(), em.births(), em.deaths());
    std::printf("knowledge: explored=%zu known=%zu\n",
                em.knowledge().exploredCells(), em.knowledge().knownLocations());
    std::printf("buildings scanned: %zu\n", buildings.size());
    for (const auto& [k, v] : counts)
        std::printf("  %-12s %d\n", elementName(static_cast<Elements>(k)), v);

    // Non-road buildings only for shape (roads are the connective paths).
    std::vector<sf::Vector2i> structs;
    for (const auto& b : buildings)
        if (b.element != Elements::road)
            structs.push_back(b.tile);

    if (!structs.empty())
    {
        double sx = 0, sy = 0;
        for (const auto& t : structs) { sx += t.x; sy += t.y; }
        const double cx = sx / structs.size(), cy = sy / structs.size();

        std::vector<double> radii;
        radii.reserve(structs.size());
        for (const auto& t : structs)
            radii.push_back(std::hypot(t.x - cx, t.y - cy));
        std::sort(radii.begin(), radii.end());

        const double meanR = std::accumulate(radii.begin(), radii.end(), 0.0) / radii.size();
        double var = 0;
        for (double r : radii) var += (r - meanR) * (r - meanR);
        const double sd = std::sqrt(var / radii.size());

        // Nearest-neighbour distances: too-tight means a fused block, too-loose
        // means scattered specks. Natural villages sit between.
        std::vector<double> nn;
        nn.reserve(structs.size());
        int minCheb = 1 << 30;
        for (std::size_t i = 0; i < structs.size(); ++i)
        {
            double best = 1e18;
            for (std::size_t j = 0; j < structs.size(); ++j)
            {
                if (i == j) continue;
                const double d = std::hypot(structs[i].x - structs[j].x, structs[i].y - structs[j].y);
                best = std::min(best, d);
                minCheb = std::min(minCheb,
                    std::max(std::abs(structs[i].x - structs[j].x), std::abs(structs[i].y - structs[j].y)));
            }
            nn.push_back(best);
        }
        std::sort(nn.begin(), nn.end());

        // Radial deciles, so the spread is visible as a shape rather than one number.
        std::printf("centroid: (%.1f, %.1f) relative to anchor (%.1f, %.1f) tiles\n",
                    cx, cy, cx - anchorTile.x, cy - anchorTile.y);
        std::printf("radius: mean=%.1f sd=%.1f min=%.1f p50=%.1f p90=%.1f max=%.1f tiles\n",
                    meanR, sd, radii.front(), radii[radii.size() / 2],
                    radii[radii.size() * 9 / 10], radii.back());
        std::printf("nearest-neighbour: min=%.1f p25=%.1f p50=%.1f p90=%.1f max=%.1f tiles\n",
                    nn.front(), nn[nn.size() / 4], nn[nn.size() / 2],
                    nn[nn.size() * 9 / 10], nn.back());
        std::printf("min Chebyshev gap between any two structures: %d tiles (rule >= 2)\n", minCheb);

        // Radial histogram in 4-tile bands.
        std::printf("radial histogram (tiles from centroid):\n");
        const int band = 4;
        std::map<int, int> hist;
        for (double r : radii)
            ++hist[static_cast<int>(r) / band];
        for (const auto& [b, n] : hist)
            std::printf("  [%2d-%2d) %s%d\n", b * band, b * band + band,
                        std::string(static_cast<std::size_t>(n), '#').c_str(), 0);

        // A crude ASCII map: structures as letters in a 60x30 window around the
        // centroid, so the layout is visible at a glance.
        std::printf("layout (rows: north->south; 'C'c center, 'H' house, 'F' farm, 'W' workshop):\n");
        const int halfW = 30, halfH = 15;
        for (int y = -halfH; y <= halfH; ++y)
        {
            std::string line;
            for (int x = -halfW; x <= halfW; ++x)
            {
                char c = '.';
                for (const auto& b : buildings)
                    if (b.tile == sf::Vector2i{ anchorTile.x + x, anchorTile.y + y })
                    {
                        c = b.element == Elements::city_center ? 'C'
                          : b.element == Elements::house       ? 'H'
                          : b.element == Elements::farm        ? 'F'
                          : b.element == Elements::workshop    ? 'W'
                          : '+';
                        break;
                    }
                line.push_back(c);
            }
            std::printf("  %s\n", line.c_str());
        }
    }

    std::printf("===== end =====\n");
    return 0;
}
