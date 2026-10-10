#pragma once

#include "Appearance.h"
#include "Config.h"
#include "EntityDecision.h"
#include "GameClock.h"
#include "Jobs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>

// Per-entity simulation tuning, loaded from config/entity_data.json.
//
// Kept separate from MapConfig so entity behaviour can be rebalanced without
// touching terrain, and loaded through loadJsonFile so a missing or malformed
// file raises a clear std::runtime_error instead of failing silently.
struct EntityConfig
{
    // How fast fatigue rises per in-game hour. Food is no longer a per-entity
    // need: the settlement consumes one food per person at the end of each day.
    int sleep_gain_per_hour{ 2 };

    // Population dynamics: ageing, lethal hunger, health and reproduction.
    // Defaults are shipped so the simulation runs even with an older config file.
    struct Survival
    {
        // How many entities the scene spawns at the start.
        int initial_population{ 8 };

        // Hard ceiling on the population, so births cannot run away.
        int max_population{ 120 };

        // Consecutive days an entity may end without food before its health
        // starts draining. A single missed meal is harmless; a run of them kills.
        int lethal_days_without_food{ 3 };

        // Food each entity consumes at the end of an in-game day. The settlement
        // stock is drawn down by this much per head.
        int food_per_person_per_day{ 1 };

        // How many in-game hours an entity lives before dying of old age.
        // Expressed in years in the config (a year is GameTime::kHoursPerYear).
        int lifespan_hours{ GameTime::kHoursPerYear * 65 };

        // How long after a birth the same entity can give birth again. Expressed
        // in days in the config. Kept per entity, so a larger settlement grows
        // faster than a small one.
        int birth_interval_hours{ GameTime::kHoursPerDay * 300 };

        // Reproduction: an entity gives birth when it is at least this
        // comfortable (1 = fully rested) and its own interval has passed.
        float birth_comfort{ 0.8f };

        // Health: how fast hunger drains it, and how fast a rested entity
        // recovers. Death happens when health reaches zero.
        int starvation_damage_per_hour{ 4 };
        int health_regen_per_hour{ 1 };
    };

    Survival survival{};

    // Optional per-entity-type starting job, from an "entity_types" block keyed
    // by the EntityType name. Unlisted types fall back to a sensible default
    // (human -> Builder, animal -> Idle), so the config stays backward compatible.
    std::array<Jobs::Job, kEntityTypeCount> type_jobs{}; // indexed by EntityType
    std::array<bool, kEntityTypeCount>      has_type_job{};

    // Per-entity-type and per-job appearance, from an optional top-level
    // "appearance" block in the entity file (each keyed by the EntityType name
    // and the job name). A missing entry keeps the shipped default look, so the
    // block is fully backward compatible.
    std::array<Appearance::Look, kEntityTypeCount> type_looks{}; // indexed by EntityType
    std::array<Appearance::Look, Jobs::kJobCount>  job_looks{};  // indexed by Jobs::Job
    // Whether an entry was configured at all, so the resolver can fall back
    // instead of guessing from a value that happens to equal the default.
    std::array<bool, Jobs::kJobCount> has_job_look{};

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

    // Shared civilization knowledge: the store is one per settlement (not per
    // entity), so this only tunes how coarse the explored map is and how large it
    // may grow. Defaults ship so older configs load unchanged.
    struct Knowledge
    {
        // Side, in tiles, of one explored cell. Coverage is bucketed so the store
        // does not grow one entry per tile the settlement ever sees.
        int cell_size{ 8 };
        // Hard cap on explored cells, so a long run cannot grow the store without
        // bound.
        std::size_t max_cells{ 65536 };
    };

    Knowledge knowledge{};

    // Weighted decision policy (thresholds, biases, idle tolerance).
    EntityDecision::Config decision{};
};

// Build an EntityConfig from a parsed entity file. Throws std::runtime_error
// when the file cannot be read (loadJsonFile does that) or a key is malformed.
inline EntityConfig loadEntityConfig(const std::string& path)
{
    const nlohmann::json js = loadJsonFile(path);

    EntityConfig cfg;

    const auto& needs = js.at("needs");
    cfg.sleep_gain_per_hour = needs.at("sleep_gain_per_hour").get<int>();

    const auto& decision = js.at("decision");
    cfg.decision.idle_tolerance = decision.value("idle_tolerance", cfg.decision.idle_tolerance);

    if (js.contains("survival"))
    {
        const auto& survival = js.at("survival");
        auto& s = cfg.survival;
        s.initial_population   = survival.value("initial_population", s.initial_population);
        s.max_population       = survival.value("max_population", s.max_population);
        s.lethal_days_without_food = survival.value("lethal_days_without_food", s.lethal_days_without_food);
        s.food_per_person_per_day  = survival.value("food_per_person_per_day", s.food_per_person_per_day);
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
    cfg.economy.job_targets[Jobs::index(Jobs::Job::Explorer)]   = 0;

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

    // Shared knowledge tuning. Optional: older configs keep the defaults.
    if (js.contains("knowledge"))
    {
        const auto& knowledge = js.at("knowledge");
        cfg.knowledge.cell_size = knowledge.value("cell_size", cfg.knowledge.cell_size);
        cfg.knowledge.max_cells = knowledge.value("max_cells", cfg.knowledge.max_cells);
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
        readType("Human_Explorer", EntityType::Human_Explorer);
        readType("Animal_Dog", EntityType::Animal_Dog);
        readType("Animal_Cat", EntityType::Animal_Cat);
    }

    const auto readNeed = [&decision](const char* key, EntityDecision::Config::Need& out)
    {
        const auto& node = decision.at(key);
        out.threshold = node.at("threshold").get<float>();
        out.bias = node.value("bias", out.bias);
    };
    readNeed("sleep", cfg.decision.sleep);

    if (decision.contains("work"))
        readNeed("work", cfg.decision.work);

    // Appearance: optional "appearance" block with per-entity-type ("types") and
    // per-job ("jobs") looks. Each value may set shape, color, outline,
    // outline_thickness, radius and points; unset fields keep the default look.
    if (js.contains("appearance"))
    {
        const auto& appearance = js.at("appearance");

        const auto readColor = [](const nlohmann::json& node) -> std::optional<sf::Color>
        {
            if (node.is_string())
            {
                const std::string name = node.get<std::string>();
                if (name == "white")  return sf::Color::White;
                if (name == "black")  return sf::Color::Black;
                if (name == "red")    return sf::Color::Red;
                if (name == "green")  return sf::Color::Green;
                if (name == "blue")   return sf::Color::Blue;
                if (name == "yellow") return sf::Color::Yellow;
                if (name == "cyan")   return sf::Color::Cyan;
                if (name == "magenta") return sf::Color::Magenta;
                return std::nullopt;
            }
            if (node.is_array() && node.size() >= 3)
            {
                const auto channel = [](const nlohmann::json& v)
                {
                    return static_cast<std::uint8_t>(std::clamp(v.get<int>(), 0, 255));
                };
                const auto alpha = node.size() >= 4 ? channel(node.at(3)) : static_cast<std::uint8_t>(255);
                return sf::Color{ channel(node.at(0)), channel(node.at(1)), channel(node.at(2)), alpha };
            }
            return std::nullopt;
        };

        const auto readLook = [&readColor](const nlohmann::json& node, Appearance::Look& out)
        {
            if (node.contains("shape"))
                if (const auto shape = Appearance::shapeFromString(node.at("shape").get<std::string>()))
                    out.shape = *shape;
            if (node.contains("color"))
                if (const auto color = readColor(node.at("color")))
                    out.fill = *color;
            if (node.contains("outline_color"))
                if (const auto color = readColor(node.at("outline_color")))
                    out.outline = *color;
            out.outline_thickness = node.value("outline_thickness", out.outline_thickness);
            out.radius            = node.value("radius", out.radius);
            out.points            = node.value("points", out.points);
        };

        if (appearance.contains("types"))
        {
            const auto& types = appearance.at("types");
            const auto readTypeLook = [&](const char* key, const EntityType type)
            {
                if (types.contains(key))
                    readLook(types.at(key), cfg.type_looks[static_cast<std::size_t>(type)]);
            };
            readTypeLook("Human_Generic", EntityType::Human_Generic);
            readTypeLook("Human_Farmer", EntityType::Human_Farmer);
            readTypeLook("Human_Lumberjack", EntityType::Human_Lumberjack);
            readTypeLook("Human_Explorer", EntityType::Human_Explorer);
            readTypeLook("Animal_Dog", EntityType::Animal_Dog);
            readTypeLook("Animal_Cat", EntityType::Animal_Cat);
        }

        if (appearance.contains("jobs"))
        {
            for (const auto& [key, value] : appearance.at("jobs").items())
                if (const auto job = Jobs::fromString(key))
                {
                    readLook(value, cfg.job_looks[Jobs::index(*job)]);
                    cfg.has_job_look[Jobs::index(*job)] = true;
                }
        }
    }

    return cfg;
}
