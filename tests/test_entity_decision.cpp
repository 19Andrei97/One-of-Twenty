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
CPersonality personalityWith(int brave, int greedy, int calm)
{
    CPersonality p;
    p.traits[static_cast<int>(PersonalityTrait::Brave)] = brave;
    p.traits[static_cast<int>(PersonalityTrait::Greedy)] = greedy;
    p.traits[static_cast<int>(PersonalityTrait::Calm)] = calm;
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

    SUBCASE("comfortable entity has no urgency")
    {
        CBasicNeeds needs; // thirst/hunger 100, sleep 0
        const auto u = EntityDecision::computeUrgencies(needs, personalityWith(50, 50, 50), cfg);
        CHECK(u.thirst == doctest::Approx(0.f));
        CHECK(u.hunger == doctest::Approx(0.f));
        CHECK(u.sleep == doctest::Approx(0.f));
    }

    SUBCASE("higher traits act sooner")
    {
        CBasicNeeds needs;
        needs.thirst = 25; // urgency 0.5 before personality

        const auto brave = EntityDecision::computeUrgencies(needs, personalityWith(100, 50, 50), cfg);
        const auto timid = EntityDecision::computeUrgencies(needs, personalityWith(0, 50, 50), cfg);

        CHECK(brave.thirst == doctest::Approx(0.75f));
        CHECK(timid.thirst == doctest::Approx(0.25f));
        CHECK(brave.thirst > timid.thirst);
    }

    SUBCASE("each need uses its own trait")
    {
        CBasicNeeds needs;
        needs.thirst = 0;  // Brave
        needs.hunger = 0;  // Greedy
        needs.sleep = 100; // Calm

        const auto u = EntityDecision::computeUrgencies(needs, personalityWith(100, 50, 0), cfg);
        CHECK(u.thirst == doctest::Approx(1.0f * 1.5f)); // max, clamped to 1
        CHECK(u.hunger == doctest::Approx(1.0f * 1.0f));
        CHECK(u.sleep == doctest::Approx(1.0f * 0.5f));
    }
}

TEST_CASE("strongestNeed respects thresholds and a deterministic tie-break")
{
    EntityDecision::Config cfg;

    SUBCASE("below threshold selects nothing")
    {
        EntityDecision::Urgencies u;
        u.thirst = 0.1f;
        u.hunger = 0.15f;
        u.sleep = 0.19f;
        CHECK(EntityDecision::strongestNeed(u, cfg) == EntityDecision::Need::None);
    }

    SUBCASE("highest wins")
    {
        EntityDecision::Urgencies u;
        u.thirst = 0.3f;
        u.hunger = 0.9f;
        u.sleep = 0.4f;
        CHECK(EntityDecision::strongestNeed(u, cfg) == EntityDecision::Need::Hunger);
    }

    SUBCASE("ties resolve thirst > hunger > sleep")
    {
        EntityDecision::Urgencies u;
        u.thirst = 0.8f;
        u.hunger = 0.8f;
        u.sleep = 0.8f;
        CHECK(EntityDecision::strongestNeed(u, cfg) == EntityDecision::Need::Thirst);

        u.thirst = 0.f;
        CHECK(EntityDecision::strongestNeed(u, cfg) == EntityDecision::Need::Hunger);

        u.hunger = 0.f;
        CHECK(EntityDecision::strongestNeed(u, cfg) == EntityDecision::Need::Sleep);
    }
}

TEST_CASE("decide returns needs, idles, then wanders")
{
    auto cfg = defaultConfig();
    const auto personality = personalityWith(50, 50, 50);

    SUBCASE("an urgent need is chosen immediately")
    {
        CBasicNeeds needs;
        needs.thirst = 0;
        CHECK(EntityDecision::decide(needs, personality, cfg, 0) == EntityDecision::Need::Thirst);
    }

    SUBCASE("a contented entity idles until the tolerance is spent")
    {
        CBasicNeeds needs;
        cfg.idle_tolerance = 3;

        CHECK(EntityDecision::decide(needs, personality, cfg, 0) == EntityDecision::Need::None);
        CHECK(EntityDecision::decide(needs, personality, cfg, 2) == EntityDecision::Need::None);
        CHECK(EntityDecision::decide(needs, personality, cfg, 3) == EntityDecision::Need::Wander);
        CHECK(EntityDecision::decide(needs, personality, cfg, 99) == EntityDecision::Need::Wander);
    }
}

TEST_CASE("actionFor maps needs to their satisfying action")
{
    CHECK(EntityDecision::actionFor(EntityDecision::Need::Thirst) == ActionTypes::Drinking);
    CHECK(EntityDecision::actionFor(EntityDecision::Need::Hunger) == ActionTypes::Eating);
    CHECK(EntityDecision::actionFor(EntityDecision::Need::Sleep) == ActionTypes::Sleeping);
    CHECK(EntityDecision::actionFor(EntityDecision::Need::Wander) == ActionTypes::Moving);
    CHECK(EntityDecision::actionFor(EntityDecision::Need::None) == ActionTypes::Moving);
}

TEST_CASE("loadEntityConfig reads the shipped entity file")
{
    const EntityConfig cfg = loadEntityConfig(entityConfigPath());

    CHECK(cfg.hunger_decay_per_hour == 3);
    CHECK(cfg.thirst_decay_per_hour == 5);
    CHECK(cfg.sleep_gain_per_hour == 2);
    CHECK(cfg.decision.idle_tolerance == 3);
    CHECK(cfg.decision.thirst.threshold == doctest::Approx(0.2f));
    CHECK(cfg.decision.hunger.bias == doctest::Approx(1.0f));

    CHECK(cfg.decayPerHour(EntityDecision::Need::Thirst) == 5);
    CHECK(cfg.decayPerHour(EntityDecision::Need::Hunger) == 3);
    CHECK(cfg.decayPerHour(EntityDecision::Need::Sleep) == 2);
    CHECK(cfg.decayPerHour(EntityDecision::Need::None) == 0);
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
    auto map = std::make_shared<MapGenerator>(font, frames, std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/map_data.json");
    auto clock = std::make_shared<GameClock>(60.f);

    float delta = 1.f / 60.f;
    EntityManager entities(font, map, clock, delta, entityConfigPath());

    CHECK(entities.entityCount() == 0);

    entities.addEntity(EntityType::Human_Generic);
    CHECK(entities.entityCount() == 1);

    const auto initialNeeds = entities.firstNeeds();
    REQUIRE(initialNeeds.has_value());
    CHECK(initialNeeds->thirst == 100);
    CHECK(initialNeeds->hunger == 100);
    CHECK(initialNeeds->sleep == 0);

    // Phase 1: a few in-game hours, ticking the clock a frame at a time like the
    // game loop does and running the systems once per hour. Decay should be
    // clearly visible before any need is urgent enough to trip an action.
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
    CHECK(midNeeds->hunger < initialNeeds->hunger);
    CHECK(midNeeds->thirst < initialNeeds->thirst);
    CHECK(midNeeds->sleep > initialNeeds->sleep);

    // Phase 2: keep going until a need crosses its threshold and the entity
    // starts acting. Thirst falls fastest, so it should trip first.
    bool acted = false;
    for (int hour = 0; hour < 40 && !acted; ++hour)
    {
        advanceHour();
        acted = entities.firstAction().has_value();
    }
    CHECK(acted);
}

