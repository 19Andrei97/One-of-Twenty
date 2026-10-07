#pragma once

#include "Config.h"
#include "EntityDecision.h"

#include <nlohmann/json.hpp>

// Per-entity simulation tuning, loaded from config/entity_data.json.
//
// Kept separate from MapConfig so entity behaviour can be rebalanced without
// touching terrain, and loaded through loadJsonFile so a missing or malformed
// file raises a clear std::runtime_error instead of failing silently.
struct EntityConfig
{
    // How fast the raw need counters move per in-game hour.
    int hunger_decay_per_hour{ 3 };
    int thirst_decay_per_hour{ 5 };
    int sleep_gain_per_hour{ 2 };

    // Population dynamics: ageing, lethal needs and reproduction. Defaults are
    // shipped so the simulation runs even with an older config file.
    struct Survival
    {
        // How many entities the scene spawns at the start.
        int initial_population{ 8 };

        // Hard ceiling on the population, so births cannot run away.
        int max_population{ 40 };

        // A need counter at or below this kills the entity. Fullness counters
        // (thirst/hunger) run 100 (comfortable) down to 0 (dire), so 0 means
        // "dies of starvation/dehydration".
        int lethal_threshold{ 0 };

        // How many in-game hours an entity lives before dying of old age.
        int lifespan_hours{ 720 }; // 30 in-game days

        // Reproduction: an entity gives birth when it is at least this
        // comfortable (1 = every need satisfied) and enough hours have passed
        // since the settlement's last birth.
        float birth_comfort{ 0.8f };
        int birth_cooldown_hours{ 48 };
    };

    Survival survival{};

    // Weighted decision policy (thresholds, biases, idle tolerance).
    EntityDecision::Config decision{};

    // Convenience: the per-hour decay for each need, in thirst/hunger/sleep
    // order. Positive values mean "move toward urgent".
    [[nodiscard]] int decayPerHour(EntityDecision::Need need) const noexcept
    {
        switch (need)
        {
            case EntityDecision::Need::Thirst: return thirst_decay_per_hour;
            case EntityDecision::Need::Hunger: return hunger_decay_per_hour;
            case EntityDecision::Need::Sleep:  return sleep_gain_per_hour;
            default:                           return 0;
        }
    }
};

// Build an EntityConfig from a parsed entity file. Throws std::runtime_error
// when the file cannot be read (loadJsonFile does that) or a key is malformed.
inline EntityConfig loadEntityConfig(const std::string& path)
{
    const nlohmann::json js = loadJsonFile(path);

    EntityConfig cfg;

    const auto& needs = js.at("needs");
    cfg.hunger_decay_per_hour = needs.at("hunger_decay_per_hour").get<int>();
    cfg.thirst_decay_per_hour = needs.at("thirst_decay_per_hour").get<int>();
    cfg.sleep_gain_per_hour = needs.at("sleep_gain_per_hour").get<int>();

    const auto& decision = js.at("decision");
    cfg.decision.idle_tolerance = decision.value("idle_tolerance", cfg.decision.idle_tolerance);

    if (js.contains("survival"))
    {
        const auto& survival = js.at("survival");
        auto& s = cfg.survival;
        s.initial_population   = survival.value("initial_population", s.initial_population);
        s.max_population       = survival.value("max_population", s.max_population);
        s.lethal_threshold     = survival.value("lethal_threshold", s.lethal_threshold);
        s.lifespan_hours       = survival.value("lifespan_hours", s.lifespan_hours);
        s.birth_comfort        = survival.value("birth_comfort", s.birth_comfort);
        s.birth_cooldown_hours = survival.value("birth_cooldown_hours", s.birth_cooldown_hours);
    }

    const auto readNeed = [&decision](const char* key, EntityDecision::Config::Need& out)
    {
        const auto& node = decision.at(key);
        out.threshold = node.at("threshold").get<float>();
        out.bias = node.value("bias", out.bias);
    };
    readNeed("thirst", cfg.decision.thirst);
    readNeed("hunger", cfg.decision.hunger);
    readNeed("sleep", cfg.decision.sleep);

    if (decision.contains("work"))
        readNeed("work", cfg.decision.work);

    return cfg;
}
