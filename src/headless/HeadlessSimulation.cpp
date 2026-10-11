#include "../headless/HeadlessSimulation.h"

#include "../helpers/Config.h"
#include "../helpers/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

void HeadlessSimulation::streamAroundCenter()
{
    // Re-centre on the settlement anchor as soon as one exists, so the built-up
    // area (and the tiles the planner writes buildings into) stay loaded.
    if (const auto a = m_entities->settlementAnchor())
        m_stream_center = *a;

    const sf::IntRect view{ m_stream_center - sf::Vector2i{ m_half_extent_px, m_half_extent_px },
                            sf::Vector2i{ m_half_extent_px * 2, m_half_extent_px * 2 } };
    m_map->stream(view);
}

void HeadlessSimulation::report(std::int64_t day)
{
    if (m_last_report_day < 0)
    {
        std::printf("%-4s %-28s %8s %8s %6s %6s %6s %6s %7s %6s\n",
                    "day", "date", "pop", "cap", "food", "wood", "bldgs", "births", "deaths",
                    "gathers");
    }
    m_last_report_day = day;

    EntityManager& e = *m_entities;
    std::printf("%-4lld %-28s %8d %8d %6d %6d %6d %6d %7d %6d\n",
                static_cast<long long>(day),
                m_clock->formatDate().c_str(),
                e.population(), e.maxPopulation(),
                e.good(Goods::Good::Food), e.good(Goods::Good::Wood),
                e.completedBuildingCount(), e.births(), e.deaths(),
                e.gathersCompleted());
}

HeadlessSimulation::HeadlessSimulation(const std::string& config_path)
{
    nlohmann::json config = loadJsonFile(config_path);

    if (config.contains("logger"))
        Logger::init(config["logger"].value("file", std::string{ "logs/sim.log" }),
                     config["logger"].value("level", std::string{ "info" }));

    m_clock = std::make_shared<GameClock>();
    const auto time_cfg = config.value("time", nlohmann::json::object());
    m_clock->setSpeedIndex(time_cfg.value("speed_index", 1));
    m_clock->setTime(time_cfg.value("start_hour", 8), time_cfg.value("start_minute", 0));

    // Synchronous chunk generation: the run advances the clock far faster than
    // real time, so the real-time worker threads would never keep up. Generating
    // inline makes chunk delivery deterministic and fast.
    m_map = std::make_shared<MapGenerator>(m_frames, config["map"]["file"], /*synchronous=*/true);

    m_entities = std::make_unique<EntityManager>(
        m_font, m_map, m_clock, m_delta,
        config["entity"]["file"],
        config.value("buildings", nlohmann::json::object())
              .value("file", std::string{ "config/buildings.json" }));

    // Colour building tiles the way the catalog declares, exactly as Scene_Play
    // does, so a rendered map from a headless run matches the interactive one.
    m_map->applyBuildingColors(m_entities->buildings().colors());

    // The very first construction pass runs before chunks have streamed in, so
    // stream the neighbourhood around the eventual spawn once before the run
    // starts. Without this a settlement at high clock speed can spend its first
    // hours unable to place its city center.
    m_stream_center = m_entities->findHabitableSpawn();
}

void HeadlessSimulation::step(double frame_seconds)
{
    m_clock->update(static_cast<float>(frame_seconds));
    m_entities->update();
    m_entities->resolveCollisions();
    streamAroundCenter();
    ++m_frames;
}

void HeadlessSimulation::runFrames(int frames, double frame_seconds)
{
    m_entities->seedPopulation();

    // Load the settlement's neighbourhood before the clock starts running, so
    // the first construction pass has a chunk to write into. Synchronous
    // generation makes this immediate.
    if (m_last_report_day < 0)
    {
        streamAroundCenter();
        if (m_report_interval_days > 0)
            report(day());
    }

    for (int i = 0; i < frames; ++i)
    {
        step(frame_seconds);

        if (m_report_interval_days > 0)
        {
            const std::int64_t d = day();
            if (d >= (m_last_report_day + m_report_interval_days))
                report(d);
        }
    }
}

void HeadlessSimulation::runDays(double days, double frame_seconds)
{
    const float scale = m_clock->getTimeScale();
    if (scale <= 0.f)
    {
        LOG_ERROR("Headless run requested with a paused clock (scale {}); nothing to do.", scale);
        return;
    }

    const double minutes = days * static_cast<double>(GameTime::kMinutesPerDay);
    const double per_frame = frame_seconds * static_cast<double>(scale);
    const int frames = std::max(1, static_cast<int>(std::ceil(minutes / per_frame)));
    runFrames(frames, frame_seconds);
}

std::string HeadlessSimulation::summaryLine() const
{
    char buffer[512];
    EntityManager& e = *m_entities;
    std::snprintf(buffer, sizeof(buffer),
                  "Day %lld | pop %d/%d | food %d wood %d | buildings %d/%d | births %d deaths %d | gathers %d",
                  static_cast<long long>(day()),
                  e.population(), e.maxPopulation(),
                  e.good(Goods::Good::Food), e.good(Goods::Good::Wood),
                  e.completedBuildingCount(), e.buildingCount(),
                  e.births(), e.deaths(), e.gathersCompleted());
    return buffer;
}
