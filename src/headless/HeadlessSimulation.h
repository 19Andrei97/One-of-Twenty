#pragma once

#include "../entity_manager/EntityManager.h"
#include "../helpers/Random.h"
#include "../map_generator/MapGenerator.h"

#include <SFML/Graphics.hpp>

#include <memory>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

// Drive the simulation with no window, no GL context and no render target.
//
// The game loop (Game/Scene_Play) exists to draw: it opens a window, owns the
// HUD and feeds a render target every frame. The simulation underneath does not
// need any of that. The two things that tied it to a render target were chunk
// streaming and the HUD; MapGenerator::stream now does the streaming without
// drawing, and the entity simulation never touched the target. What remains is
// the clock, the map, and the entity manager, advanced in a plain loop.
//
// This is the same code path Scene_Play runs (clock -> EntityManager::update ->
// resolveCollisions), just without the screen, so a run reflects the real game
// and is fast enough to run for in-game years in seconds. Use it for headless
// tests and for a CLI that reports how a run unfolds.
class HeadlessSimulation
{
    std::shared_ptr<GameClock>              m_clock;
    std::shared_ptr<MapGenerator>          m_map;
    std::unique_ptr<EntityManager>         m_entities;

    // EntityManager takes a const sf::Font& and only uses it to draw info boxes;
    // the simulation never touches it. A default (empty) font keeps the runner
    // free of a font file without affecting behaviour.
    sf::Font                               m_font;

    // EntityManager stores a float& for the real frame delta; the simulation
    // measures movement in in-game time, so this value is never read, but it
    // must outlive the manager.
    float                                  m_delta{ 1.f / 60.f };

    // The map is streamed around a point so buildings (which are written into the
    // loaded chunk) can be placed. Re-centred on the settlement anchor when it
    // appears, so a settlement that grows keeps its neighbourhood loaded.
    sf::Vector2i                           m_stream_center{ 0, 0 };
    int                                    m_half_extent_px{ 1024 };
    int                                    m_frames{ 0 };

    // Report cadence: a one-line summary every this many in-game days.
    int                                    m_report_interval_days{ 30 };
    std::int64_t                           m_last_report_day{ -1 };

    // Advance the chunk stream for the current view. Cheap once the view is
    // unchanged (the workers dedup against what is already loaded).
    void streamAroundCenter();

    // Print one line of the run summary at the given in-game day.
    void report(std::int64_t day);

public:
    // `config_path` is the game's config.json (window/font/map/entity/buildings
    // paths). The font file is read for the sf::Font only if it exists; the
    // simulation does not depend on it.
    explicit HeadlessSimulation(const std::string& config_path);

    // Advance the simulation by `days` in-game days. `frame_seconds` is the
    // simulated real time between steps (1/60 s is the game's frame budget); the
    // in-game time is driven by the clock's scale, exactly as in the game.
    void runDays(double days, double frame_seconds = 1.0 / 60.0);

    // Advance in raw game-loop frames (used for fine control and tests).
    void runFrames(int frames, double frame_seconds = 1.0 / 60.0);

    // --- configuration (call before run*) -------------------------------------

    // Terrain seed. Applied to the map before the population is seeded.
    void setSeed(int seed) { m_map->setSeed(seed); m_map->setNoises(); }

    // In-game minutes per real second (see GameClock speed presets).
    void setTimeScale(float minutes_per_second) { m_clock->setTimeScale(minutes_per_second); }
    void setStartHour(int hour, int minute = 0) { m_clock->setTime(hour, minute); }

    // Override the config's founding population.
    void setPopulation(int count) { m_entities->seedPopulation(count); }

    // Seed the shared RNG so a run is repeatable (placement and wander decisions
    // draw from it). Without this the run varies from process to process.
    void setRandomSeed(unsigned seed) { Random::mt.seed(seed); }

    // How much of the world to keep streamed around the settlement, in pixels.
    void setStreamHalfExtent(int px) { m_half_extent_px = std::max(64, px); }

    // Print a summary line every this many in-game days (0 disables).
    void setReportIntervalDays(int days) { m_report_interval_days = std::max(0, days); }

    // --- state ----------------------------------------------------------------

    // Seed the configured starting population now (run* also does this once).
    void seedPopulation() { m_entities->seedPopulation(); }

    // The settlement anchor once a city center exists; the stream re-centres on
    // it so the built-up area stays loaded as the settlement grows.
    std::optional<sf::Vector2i> anchor() const { return m_entities->settlementAnchor(); }

    // Advance one frame: clock, entities, collisions, stream. Exactly the order
    // Scene_Play::update uses for the simulation systems.
    void step(double frame_seconds = 1.0 / 60.0);

    // Current in-game day (0 at the start).
    std::int64_t day() const { return m_clock->getTimestamp() / GameTime::kMinutesPerDay; }

    int population() const { return m_entities->population(); }
    int births() const { return m_entities->births(); }
    int deaths() const { return m_entities->deaths(); }
    int completedBuildings() const { return m_entities->completedBuildingCount(); }
    int buildings() const { return m_entities->buildingCount(); }
    int food() const { return m_entities->good(Goods::Good::Food); }
    int wood() const { return m_entities->good(Goods::Good::Wood); }
    int gathers() const { return m_entities->gathersCompleted(); }

    GameClock& clock() { return *m_clock; }
    EntityManager& entities() { return *m_entities; }
    MapGenerator& map() { return *m_map; }

    // A compact one-line summary of the run so far.
    std::string summaryLine() const;
};
