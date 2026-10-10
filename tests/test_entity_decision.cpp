#include "EntityDecision.h"
#include "EntityConfig.h"
#include "EntityManager.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>

// EntityDecision is a pure policy over CBasicNeeds + CPersonality, so these
// tests never build a map, a registry or a clock.

namespace
{
EntityDecision::Config defaultConfig()
{
    return EntityDecision::Config{};
}

// A personality with every trait pinned, so tests are not at the mercy of the
// random values CPersonality generates on construction.
CPersonality personalityWith(int brave, int greedy, int calm, int loyal = 50)
{
    CPersonality p;
    p.traits[static_cast<int>(PersonalityTrait::Brave)] = brave;
    p.traits[static_cast<int>(PersonalityTrait::Greedy)] = greedy;
    p.traits[static_cast<int>(PersonalityTrait::Calm)] = calm;
    p.traits[static_cast<int>(PersonalityTrait::Loyal)] = loyal;
    return p;
}

std::string entityConfigPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
    return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/entity_data.json";
#else
    return "config/entity_data.json";
#endif
}

// First gatherable tile near the origin, as a world position. Terrain near the
// origin is not guaranteed to be workable, so the gather test seeds its entity
// on one found by scanning outward.
std::optional<sf::Vector2i> findGatherableTile(MapGenerator& map, int maxRing = 80)
{
    const int tileSize = map.getTileSize();
    for (int ring = 0; ring <= maxRing; ++ring)
    {
        for (int dx = -ring; dx <= ring; ++dx)
        {
            for (int dy = -ring; dy <= ring; ++dy)
            {
                if (std::max(std::abs(dx), std::abs(dy)) != ring)
                    continue;
                const sf::Vector2i tile{ dx, dy };
                const sf::Vector2i world{ tile.x * tileSize, tile.y * tileSize };
                if (Resources::isGatherable(map.getElementAtWorld(world)))
                    return world;
            }
        }
    }
    return std::nullopt;
}
} // namespace

TEST_CASE("urgency inverts fullness needs and maps fatigue straight through")
{
    // Fullness: 100 is comfortable, 0 is dire.
    CHECK(EntityDecision::urgency(100, true) == doctest::Approx(0.f));
    CHECK(EntityDecision::urgency(75, true) == doctest::Approx(0.f));
    CHECK(EntityDecision::urgency(50, true) == doctest::Approx(0.f));
    CHECK(EntityDecision::urgency(25, true) == doctest::Approx(0.5f));
    CHECK(EntityDecision::urgency(0, true) == doctest::Approx(1.f));

    // Fatigue: 0 is rested, 100 is exhausted.
    CHECK(EntityDecision::urgency(0, false) == doctest::Approx(0.f));
    CHECK(EntityDecision::urgency(50, false) == doctest::Approx(0.f));
    CHECK(EntityDecision::urgency(75, false) == doctest::Approx(0.5f));
    CHECK(EntityDecision::urgency(100, false) == doctest::Approx(1.f));

    // Out-of-range values are clamped rather than producing garbage.
    CHECK(EntityDecision::urgency(-40, false) == doctest::Approx(0.f));
    CHECK(EntityDecision::urgency(500, false) == doctest::Approx(1.f));
}

TEST_CASE("urgency is zero across the comfortable half")
{
    for (int v = 50; v <= 100; ++v)
        CHECK(EntityDecision::urgency(v, true) == doctest::Approx(0.f));
    for (int v = 0; v <= 50; ++v)
        CHECK(EntityDecision::urgency(v, false) == doctest::Approx(0.f));
}

TEST_CASE("personality maps onto a 0.5x..1.5x multiplier")
{
    CHECK(EntityDecision::personalityFactor(0) == doctest::Approx(0.5f));
    CHECK(EntityDecision::personalityFactor(50) == doctest::Approx(1.0f));
    CHECK(EntityDecision::personalityFactor(100) == doctest::Approx(1.5f));
    CHECK(EntityDecision::personalityFactor(-10) == doctest::Approx(0.5f));
    CHECK(EntityDecision::personalityFactor(999) == doctest::Approx(1.5f));
}

TEST_CASE("biasedUrgency scales and clamps to one")
{
    CHECK(EntityDecision::biasedUrgency(0.5f, 1.f) == doctest::Approx(0.5f));
    CHECK(EntityDecision::biasedUrgency(0.5f, 2.f) == doctest::Approx(1.f));
    CHECK(EntityDecision::biasedUrgency(0.5f, 0.5f) == doctest::Approx(0.25f));
}

TEST_CASE("computeUrgencies combines need state with the governing trait")
{
    const auto cfg = defaultConfig();

    SUBCASE("comfortable entity has no survival urgency but is ready to work")
    {
        CBasicNeeds needs; // rested
        const auto u = EntityDecision::computeUrgencies(needs, personalityWith(50, 50, 50), cfg);
        CHECK(u.sleep == doctest::Approx(0.f));
        // Fully rested, so the society drive is at its strongest.
        CHECK(u.work == doctest::Approx(1.f));
    }

    SUBCASE("higher calm acts sooner on sleep")
    {
        CBasicNeeds needs;
        needs.sleep = 75; // urgency 0.5 before personality

        const auto calm = EntityDecision::computeUrgencies(needs, personalityWith(50, 50, 100), cfg);
        const auto restless = EntityDecision::computeUrgencies(needs, personalityWith(50, 50, 0), cfg);

        CHECK(calm.sleep == doctest::Approx(0.75f));
        CHECK(restless.sleep == doctest::Approx(0.25f));
        CHECK(calm.sleep > restless.sleep);
    }

    SUBCASE("sleep leans on Calm and work leans on Loyalty")
    {
        CBasicNeeds needs;
        needs.sleep = 100; // max fatigue

        const auto u = EntityDecision::computeUrgencies(needs, personalityWith(50, 50, 100, 100), cfg);
        CHECK(u.sleep == doctest::Approx(1.0f * 1.5f)); // max, clamped to 1
        CHECK(u.work == doctest::Approx(0.f));          // no comfort to work from
    }
}

TEST_CASE("strongestNeed respects the sleep threshold")
{
    EntityDecision::Config cfg; // sleep threshold 0.2

    EntityDecision::Urgencies u;
    u.sleep = 0.19f;
    CHECK(EntityDecision::strongestNeed(u, cfg) == EntityDecision::Need::None);

    u.sleep = 0.2f;
    CHECK(EntityDecision::strongestNeed(u, cfg) == EntityDecision::Need::Sleep);

    u.sleep = 0.9f;
    CHECK(EntityDecision::strongestNeed(u, cfg) == EntityDecision::Need::Sleep);
}

TEST_CASE("decide returns sleep, work, then idles and wanders")
{
    auto cfg = defaultConfig();
    const auto personality = personalityWith(50, 50, 50);

    SUBCASE("an exhausted entity sleeps")
    {
        CBasicNeeds needs;
        needs.sleep = 100;
        CHECK(EntityDecision::decide(needs, personality, cfg, 0) == EntityDecision::Need::Sleep);
    }

    SUBCASE("a rested entity works for the settlement")
    {
        CBasicNeeds needs; // fully rested
        CHECK(EntityDecision::decide(needs, personality, cfg, 0) == EntityDecision::Need::Work);
    }

    SUBCASE("an explorer explores instead of gathering when work is the winning drive")
    {
        CBasicNeeds needs; // rested, so work wins
        CHECK(EntityDecision::decide(needs, personality, cfg, 0, Jobs::Job::Explorer) == EntityDecision::Need::Explore);

        // Survival still outranks exploration.
        needs.sleep = 100;
        CHECK(EntityDecision::decide(needs, personality, cfg, 0, Jobs::Job::Explorer) == EntityDecision::Need::Sleep);
    }

    SUBCASE("work is gated off while a need presses, and an entity too uneasy to work idles")
    {
        // Threshold above what work can ever reach: isolates the idle/wander
        // fallback from the work behaviour.
        cfg.work.threshold = 2.0f;
        cfg.idle_tolerance = 3;

        CBasicNeeds needs; // rested, but work is gated off

        CHECK(EntityDecision::decide(needs, personality, cfg, 0) == EntityDecision::Need::None);
        CHECK(EntityDecision::decide(needs, personality, cfg, 2) == EntityDecision::Need::None);
        CHECK(EntityDecision::decide(needs, personality, cfg, 3) == EntityDecision::Need::Wander);
        CHECK(EntityDecision::decide(needs, personality, cfg, 99) == EntityDecision::Need::Wander);

        // A critical need outranks work.
        needs.sleep = 100;
        cfg.work.threshold = 0.f;
        CHECK(EntityDecision::decide(needs, personality, cfg, 0) == EntityDecision::Need::Sleep);
    }

    SUBCASE("loyalty raises the work drive")
    {
        CBasicNeeds needs;
        const auto loyal = EntityDecision::computeUrgencies(needs, personalityWith(50, 50, 50, 100), cfg);
        const auto indifferent = EntityDecision::computeUrgencies(needs, personalityWith(50, 50, 50, 0), cfg);
        CHECK(loyal.work == doctest::Approx(1.5f));
        CHECK(indifferent.work == doctest::Approx(0.5f));
    }
}

TEST_CASE("actionFor maps needs to their satisfying action")
{
    CHECK(EntityDecision::actionFor(EntityDecision::Need::Sleep) == ActionTypes::Sleeping);
    CHECK(EntityDecision::actionFor(EntityDecision::Need::Work) == ActionTypes::Gathering);
    CHECK(EntityDecision::actionFor(EntityDecision::Need::Wander) == ActionTypes::Moving);
    CHECK(EntityDecision::actionFor(EntityDecision::Need::None) == ActionTypes::Moving);
}

TEST_CASE("loadEntityConfig reads the shipped entity file")
{
    const EntityConfig cfg = loadEntityConfig(entityConfigPath());

    CHECK(cfg.sleep_gain_per_hour == 2);
    CHECK(cfg.decision.idle_tolerance == 3);
    CHECK(cfg.decision.sleep.threshold == doctest::Approx(0.2f));
    CHECK(cfg.decision.work.bias == doctest::Approx(1.0f));
}

TEST_CASE("loadEntityConfig rejects a missing file")
{
    CHECK_THROWS_AS(loadEntityConfig("config/does_not_exist_entity.json"), std::runtime_error);
}

TEST_CASE("EntityManager drives decay and action selection end to end")
{
    // Integration: exercises the real MapGenerator (worker threads and all) and
    // the real update loop. The game only spawns entities on a keypress, so the
    // test adds one directly to drive the systems through several in-game hours.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());

    CHECK(entities.entityCount() == 0);

    entities.addEntity(EntityType::Human_Generic);
    CHECK(entities.entityCount() == 1);

    const auto initialNeeds = entities.firstNeeds();
    REQUIRE(initialNeeds.has_value());
    CHECK(initialNeeds->sleep == 0);
    CHECK(initialNeeds->days_without_food == 0);

    // Phase 1: a few in-game hours, ticking the clock a frame at a time like the
    // game loop does and running the systems once per hour. Fatigue should be
    // clearly visible before it is urgent enough to trip an action.
    const auto advanceHour = [&]()
    {
        for (int step = 0; step < 60; ++step)
            clock->update(delta);
        entities.update();
    };

    for (int hour = 0; hour < 6; ++hour)
        advanceHour();

    const auto midNeeds = entities.firstNeeds();
    REQUIRE(midNeeds.has_value());
    CHECK(midNeeds->sleep > initialNeeds->sleep);

    // Phase 2: keep going until the need crosses its threshold and the entity
    // starts acting. Fatigue rises steadily, so it should trip eventually.
    bool acted = false;
    for (int hour = 0; hour < 40 && !acted; ++hour)
    {
        advanceHour();
        acted = entities.firstAction().has_value();
    }
    CHECK(acted);
}

TEST_CASE("a comfortable entity works and the settlement accumulates resources")
{
    // End to end: a spawns-near-origin entity with no urgent need should take up
    // work, and completed gathers should show up in the settlement stockpile.
    sf::Font font;
    int frames = 0;
    auto map = std::make_shared<MapGenerator>(frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());

    const auto spawn = findGatherableTile(*map);
    REQUIRE(spawn.has_value());
    entities.addEntity(EntityType::Human_Generic, *spawn);

    const auto advanceHour = [&]()
    {
        for (int step = 0; step < 60; ++step)
            clock->update(delta);
        entities.update();
    };

    // Long window: survival needs and exploration interleave, so give the entity
    // plenty of in-game time to find a workable tile and finish a gather.
    bool gathered = false;
    for (int hour = 0; hour < 600 && !gathered; ++hour)
    {
        advanceHour();
        gathered = entities.gathersCompleted() > 0;
    }

    CHECK(gathered);
    CHECK(entities.gathersCompleted() >= 1);
    CHECK(entities.totalStockpile() >= 1);
}

