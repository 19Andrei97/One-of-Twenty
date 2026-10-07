#pragma once

#include "../map_generator/MapGenerator.h"
#include "Components_Entities.h"
#include "EntityConfig.h"
#include "EntityDecision.h"

#include <string>
#include <unordered_map>

class EntityManager
{

        int                                                             m_total_entities{ 0 };
        float&                                                  m_delta_time;
        sf::Font&                                               m_font;
        std::unique_ptr<entt::registry> m_registry;
        std::shared_ptr<MapGenerator>   m_map;
        std::shared_ptr<GameClock>              m_game_clock;
        std::mutex                                              m_mutex;
        BS::thread_pool<>                               m_threads{ 2 };

        // Tuning for needs decay and the weighted decision policy.
        EntityConfig                    m_config;
        // Consecutive decisions that produced "nothing urgent", per entity, so a
        // contented entity eventually wanders instead of standing still.
        std::unordered_map<entt::entity, int>   m_entity_idle;

        // Resources gathered by the settlement, totalled by element, plus a count
        // of completed gathers so progress is observable (HUD / tests).
        std::unordered_map<Elements, int>       m_stockpile;
        int                                     m_gathers_completed{ 0 };

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
        EntityManager(sf::Font& font, std::shared_ptr<MapGenerator> map, std::shared_ptr<GameClock> clock, float& deltatime,
                      const std::string& entity_file)
                : m_font(font)
                , m_map(map)
                , m_game_clock(clock)
                , m_delta_time(deltatime)
                , m_config(loadEntityConfig(entity_file))
        {
                m_registry = std::make_unique<entt::registry>();

                // THREADS TO BE IMPLEMENTED
                //m_threads.submit_task([this] { startChunksGenerator(); });
        }

        // MAIN FUNCTIONS
        void render(sf::RenderTarget& window);
        void update();
        // Spawn at an optional world position (defaults to the origin, which is
        // what the game uses until there is a city center).
        void addEntity(const EntityType& type, const sf::Vector2i& spawn = { 0, 0 });

        // SETTERS
        void nextTarget(const EntityType& type, sf::Vector2i& targ);

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
};
