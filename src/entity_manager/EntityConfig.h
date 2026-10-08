#pragma once

#include "Config.h"
#include "EntityDecision.h"
#include "GameClock.h"
#include "Jobs.h"

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

    // Population dynamics: ageing, lethal needs, health and reproduction.
    // Defaults are shipped so the simulation runs even with an older config file.
    struct Survival
    {
        // How many entities the scene spawns at the start.
        int initial_population{ 8 };

        // Hard ceiling on the population, so births cannot run away.
        int max_population{ 120 };

        // A need counter at or below this starts draining health. Fullness
        // counters (thirst/hunger) run 100 (comfortable) down to 0 (dire).
        int lethal_threshold{ 0 };

        // How many in-game hours an entity lives before dying of old age.
        // Expressed in years in the config (a year is GameTime::kHoursPerYear).
        int lifespan_hours{ GameTime::kHoursPerYear * 65 };

        // How long after a birth the same entity can give birth again. Expressed
        // in days in the config. Kept per entity, so a larger settlement grows
        // faster than a small one.
        int birth_interval_hours{ GameTime::kHoursPerDay * 300 };

        // Reproduction: an entity gives birth when it is at least this
        // comfortable (1 = every need satisfied) and its own interval has passed.
        float birth_comfort{ 0.8f };

        // Health: how fast starvation/dehydration drains it, and how fast a
        // comfortable entity recovers. Death happens when health reaches zero.
        int starvation_damage_per_hour{ 4 };
        int health_regen_per_hour{ 1 };
    };

    Survival survival{};

    // Optional per-entity-type starting job, from an "entity_types" block keyed
    // by the EntityType name. Unlisted types fall back to a sensible default
    // (human -> Builder, animal -> Idle), so the config stays backward compatible.
    std::array<Jobs::Job, kEntityTypeCount> type_jobs{}; // indexed by EntityType
    std::array<bool, kEntityTypeCount>      has_type_job{};

    // Settlement economy: how many of each job to staff and what the buildings
    // produce. Defaults ship so the game runs on an older config file.
    struct Economy
    {
        // How many of each job the settlement wants staffed, indexed by Job.
        Jobs::JobTargets job_targets{};

        // Food lost per in-game day, as a percentage of the stored food.
        int food_spoilage_percent_per_day{ 5 };

        // Cost to place a building, in goods, taken from the stockpile.
        int farm_wood_cost{ 5 };
        int workshop_wood_cost{ 5 };

        // Food each farm / workshop produces per in-game hour.
        int farm_food_per_hour{ 1 };
        int workshop_output_per_hour{ 1 };
    };

    Economy economy{};

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
        s.birth_comfort        = survival.value("birth_comfort", s.birth_comfort);
        s.starvation_damage_per_hour = survival.value("starvation_damage_per_hour", s.starvation_damage_per_hour);
        s.health_regen_per_hour      = survival.value("health_regen_per_hour", s.health_regen_per_hour);

        // Lifespan can be given directly in hours (older configs) or in years,
        // which is friendlier for a value on the scale of a human life.
        s.lifespan_hours = survival.value("lifespan_hours", s.lifespan_hours);
        if (survival.contains("lifespan_years"))
            s.lifespan_hours = survival.at("lifespan_years").get<int>() * GameTime::kHoursPerYear;

        // Same idea for the reproduction interval: days (preferred) or hours.
        s.birth_interval_hours = survival.value("birth_cooldown_hours", s.birth_interval_hours);
        if (survival.contains("birth_interval_days"))
            s.birth_interval_hours = survival.at("birth_interval_days").get<int>() * GameTime::kHoursPerDay;
    }

    // Default staffing: enough food production to feed the founders, a couple of
    // gatherers and one craftsperson. Overridden by the config's economy block.
    cfg.economy.job_targets[Jobs::index(Jobs::Job::Farmer)]     = 4;
    cfg.economy.job_targets[Jobs::index(Jobs::Job::Lumberjack)] = 2;
    cfg.economy.job_targets[Jobs::index(Jobs::Job::Miner)]      = 1;
    cfg.economy.job_targets[Jobs::index(Jobs::Job::Builder)]    = 1;

    if (js.contains("economy"))
    {
        const auto& economy = js.at("economy");
        auto& e = cfg.economy;

        e.food_spoilage_percent_per_day = economy.value("food_spoilage_percent_per_day", e.food_spoilage_percent_per_day);
        e.farm_wood_cost                = economy.value("farm_wood_cost", e.farm_wood_cost);
        e.workshop_wood_cost            = economy.value("workshop_wood_cost", e.workshop_wood_cost);
        e.farm_food_per_hour            = economy.value("farm_food_per_hour", e.farm_food_per_hour);
        e.workshop_output_per_hour      = economy.value("workshop_output_per_hour", e.workshop_output_per_hour);

        if (economy.contains("jobs"))
        {
            for (const auto& [key, value] : economy.at("jobs").items())
            {
                if (const auto job = Jobs::fromString(key))
                {
                    if (*job == Jobs::Job::Idle)
                        continue;
                    e.job_targets[Jobs::index(*job)] = value.get<int>();
                }
            }
        }
    }

    // Per-entity-type starting job. Names match the EntityType enumerators.
    if (js.contains("entity_types"))
    {
        const auto& types = js.at("entity_types");
        const auto readType = [&](const char* key, const EntityType type)
        {
            if (!types.contains(key))
                return;
            if (const auto job = Jobs::fromString(types.at(key).value("job", std::string{})))
            {
                cfg.type_jobs[static_cast<std::size_t>(type)] = *job;
                cfg.has_type_job[static_cast<std::size_t>(type)] = true;
            }
        };
        readType("Human_Generic", EntityType::Human_Generic);
        readType("Human_Farmer", EntityType::Human_Farmer);
        readType("Human_Lumberjack", EntityType::Human_Lumberjack);
        readType("Animal_Dog", EntityType::Animal_Dog);
        readType("Animal_Cat", EntityType::Animal_Cat);
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
