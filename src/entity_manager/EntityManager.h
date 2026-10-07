#pragma once

#include "../map_generator/MapGenerator.h"
#include "Components_Entities.h"
#include "EntityConfig.h"
#include "EntityDecision.h"
#include "EntityVitals.h"

#include <string>
#include <unordered_map>

class EntityManager
{

        float&                                                  m_delta_time;
        const sf::Font&                                         m_font;
        std::unique_ptr<entt::registry> m_registry;
        std::shared_ptr<MapGenerator>   m_map;
        std::shared_ptr<GameClock>              m_game_clock;
        std::mutex                                              m_mutex;

        // Tuning for needs decay and the weighted decision policy.
        EntityConfig                    m_config;
        // Consecutive decisions that produced "nothing urgent", per entity, so a
        // contented entity eventually wanders instead of standing still.
        std::unordered_map<entt::entity, int>   m_entity_idle;

        // Resources gathered by the settlement, totalled by element, plus a count
        // of completed gathers so progress is observable (HUD / tests).
        std::unordered_map<Elements, int>       m_stockpile;
        int                                     m_gathers_completed{ 0 };

        // Population bookkeeping, exposed for the HUD and tests.
        int                                     m_births{ 0 };
        int                                     m_deaths{ 0 };
        int                                     m_last_birth_hour{ 0 };
        // Last simulation minute for which survival dynamics ran. The clock can
        // advance several hours per frame (and wraps at midnight), so this is a
        // monotonic timestamp rather than the hour-of-day.
        std::int64_t                            m_last_survival_tick{ -1 };
        bool                                    m_seeded_population{ false };

        // A remembered tile worth working and what it yields.
        struct WorkTarget
        {
                sf::Vector2i pos;
                Elements     element;
        };

        // Private function
        void addTextToEntityInfo(std::vector<sf::Text>& vec, std::string&& s, int size, const sf::Color& color);

        // Nearest remembered gatherable tile, rarest material first. Empty when
        // nothing workable has been remembered yet.
        std::optional<WorkTarget> settleElements(const CMemory& memory) const;

        // Withdraw one unit of `element` from the settlement stores if any is
        // held, so eating/drinking can be gated on supply. Returns false when the
        // stores are empty (the entity then falls back to foraging the tile).
        bool consumeFromStockpile(Elements element);

        // Population dynamics, run once per in-game hour. `ageEntities` advances
        // lifespan, `killTheDying` removes entities that starved or aged out, and
        // `tryBirths` adds a newborn when the settlement is comfortable enough.
        void decayNeeds(std::int64_t hourIndex);
        void ageEntities();
        void killTheDying(int hour);
        void tryBirths(int hour);

        // Turn a decision into queued actions. Returns true if the entity is
        // already busy with the need (so the idle counter should reset).
        bool startActionFor(const EntityDecision::Need need,
                            const sf::Vector2i& pos,
                            float visionRadius,
                            const CMemory& memory,
                            CActionsQueue& queue);

public:

        bool show_vision = false;

        // CONSTRUCTOR
        EntityManager(const sf::Font& font, std::shared_ptr<MapGenerator> map, std::shared_ptr<GameClock> clock, float& deltatime,
                      const std::string& entity_file)
                : m_font(font)
                , m_map(map)
                , m_game_clock(clock)
                , m_delta_time(deltatime)
                , m_config(loadEntityConfig(entity_file))
        {
                m_registry = std::make_unique<entt::registry>();
        }

        // MAIN FUNCTIONS
        void render(sf::RenderTarget& window);
        void update();
        // Spawn at an optional world position (defaults to the origin, which is
        // what the game uses until there is a city center).
        void addEntity(const EntityType& type, const sf::Vector2i& spawn = { 0, 0 });

        // Spawn the configured starting population and enable population dynamics.
        // Kept separate from the constructor so tests can start from zero and add
        // entities explicitly.
        void seedPopulation();

        // A habitable world position to found the settlement: a land tile with
        // drinkable water and forageable food close enough to be reached. Falls
        // back to the origin when the map offers nothing better.
        sf::Vector2i findHabitableSpawn() const;

        // GETTERS
        //const EntityVec& getEntities(const EntityType& type);

        // Snapshot of the first live entity, for the HUD and for tests.
        std::optional<CBasicNeeds>  firstNeeds() const;
        std::optional<ActionTypes>  firstAction() const;
        int                         entityCount() const;

        // Settlement totals, accumulated as gathers complete.
        int stockpile(const Elements element) const;
        int totalStockpile() const;
        int gathersCompleted() const { return m_gathers_completed; }

        // Population and vitals, for the HUD and tests. `population` is the live
        // entity count; births/deaths accumulate over the run.
        int population() const { return entityCount(); }
        int births() const { return m_births; }
        int deaths() const { return m_deaths; }
        int maxPopulation() const { return m_config.survival.max_population; }
        int lifespanHours() const { return m_config.survival.lifespan_hours; }
};
