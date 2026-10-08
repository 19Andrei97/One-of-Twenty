#pragma once

#include "../map_generator/MapGenerator.h"
#include "Components_Entities.h"
#include "EntityConfig.h"
#include "EntityDecision.h"
#include "EntityVitals.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

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

        // Settlement stores, plus a count of completed gathers so progress is
        // observable (HUD / tests). Goods are the economy's currency: raw gathers
        // and crafted production both land here, and eating/spoilage draw from it.
        Goods::Stock                            m_goods;
        int                                     m_gathers_completed{ 0 };
        int                                     m_food_produced{ 0 };

        // A placed building: the tile it occupies and the element written into the
        // map for it. The list is what the hourly production pass iterates, so it
        // does not have to rescan the terrain for structures.
        struct BuildingSite
        {
                sf::Vector2i pos;       // world position (tile corner)
                Elements     element;   // Elements::farm / Elements::workshop
        };
        std::vector<BuildingSite>               m_buildings;

        // Last in-game day for which spoilage ran, so it fires exactly once per
        // day even when a frame advances several hours.
        std::int64_t                            m_last_spoilage_day{ -1 };

        // Population bookkeeping, exposed for the HUD and tests.
        int                                     m_births{ 0 };
        int                                     m_deaths{ 0 };
        // Last simulation minute for which survival dynamics ran. The clock can
        // advance several hours per frame (and wraps at midnight), so this is a
        // monotonic timestamp rather than the hour-of-day.
        std::int64_t                            m_last_survival_tick{ -1 };
        // Timestamp (in in-game minutes) at the previous update. Movement consumes
        // the game-time elapsed since then rather than the real frame delta, so an
        // entity walks the same in-game distance whether the clock runs at a
        // minute or a month per second, and a paused clock freezes it too.
        std::int64_t                            m_last_frame_minutes{ -1 };
        bool                                    m_seeded_population{ false };

        // A remembered tile worth working and what it yields.
        struct WorkTarget
        {
                sf::Vector2i pos;
                Elements     element;
        };

        // Scratch tile block reused by findRoute so routing does not allocate a
        // map per call.
        std::unordered_map<sf::Vector2i, Elements, Vector2iHash> m_tile_block;

        // Private function
        void addTextToEntityInfo(std::vector<sf::Text>& vec, std::string&& s, int size, const sf::Color& color);

        // Starting role for a freshly spawned entity: the configured job for its
        // type when given, otherwise a sensible default (humans build, animals
        // idle). The shortage rule can reassign it afterwards.
        Jobs::Job defaultJobFor(const EntityType& type) const;

        // A* route from one world position to another. Returns the world-space
        // waypoints (tile corners, excluding the tile the entity already stands
        // on), `nullopt` when already on the target tile, or an empty vector when
        // no land route exists. The search is bounded to a window around the two
        // ends, so a very long trip falls back to a straight line and re-plans.
        std::optional<std::vector<sf::Vector2i>> findRoute(const sf::Vector2i& from, const sf::Vector2i& to);

        // Queue a move to `target`, computing a route and storing it on `path`.
        // Returns false (and queues nothing) when no land route exists.
        bool queueMoveTo(const sf::Vector2i& from, const sf::Vector2i& target,
                         CPath& path, CActionsQueue& queue);

        // Nearest remembered gatherable tile this job can work, in the job's
        // preference order. Empty when nothing workable has been remembered yet.
        std::optional<WorkTarget> settleElements(const CMemory& memory, Jobs::Job job) const;

        // Reassign idle entities to the most understaffed job, so the settlement
        // responds to shortages. Called when an entity completes a job assignment
        // cycle, not every frame.
        void reassignJobs();

        // Run the settlement's production once per in-game hour: farms grow food
        // (once wood is available), workshops turn raw goods into crafted ones,
        // and stored food spoils once per day.
        void produceGoods();
        void spoilFood();

        // Withdraw one unit of `element` from the settlement stores if any is
        // held, so eating/drinking can be gated on supply. Returns false when the
        // stores are empty (the entity then falls back to foraging the tile).
        bool consumeFromStockpile(Elements element);

        // Population dynamics, run once per in-game hour. `ageEntities` advances
        // lifespan, `applyHealth` drains/restores health from the needs,
        // `killTheDying` removes entities whose health or lifespan ran out, and
        // `tryBirths` adds newborns from comfortable adults.
        void decayNeeds(std::int64_t hourIndex);
        void ageEntities();
        void applyHealth();
        void killTheDying();
        void tryBirths();

        // Turn a decision into queued actions. Returns true if the entity is
        // already busy with the need (so the idle counter should reset).
        bool startActionFor(const EntityDecision::Need need,
                            const sf::Vector2i& pos,
                            float visionRadius,
                            const CMemory& memory,
                            CActionsQueue& queue,
                            CPath& path,
                            Jobs::Job job);

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
        // what the game uses until there is a city center). Returns the new
        // entity so callers can tune it (e.g. stagger founders' ages).
        entt::entity addEntity(const EntityType& type, const sf::Vector2i& spawn = { 0, 0 });

        // Push entities out of the ocean and apart from each other. Called once
        // per frame from Scene_Play::sCollision.
        void resolveCollisions();

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
        std::optional<int>          firstHealth() const;
        std::optional<ActionTypes>  firstAction() const;
        int                         entityCount() const;
        // World positions of every live entity, for tests that need to reason
        // about where the settlement actually stands.
        std::vector<sf::Vector2i>   entityPositions() const;

        // Settlement totals, accumulated as gathers complete.
        int stockpile(const Elements element) const;
        int totalStockpile() const;
        int gathersCompleted() const { return m_gathers_completed; }

        // The economy's stores, for the HUD and tests.
        int good(const Goods::Good which) const { return m_goods.count(which); }
        const Goods::Stock& goods() const { return m_goods; }
        int foodProduced() const { return m_food_produced; }
        int buildingCount() const { return static_cast<int>(m_buildings.size()); }

        // Current job staffing, indexed by Jobs::Job, for the HUD.
        std::array<int, Jobs::kJobCount> jobCounts() const;
        Jobs::Job firstJob() const;

        // Build a structure on a tile: spend the wood cost and write the element
        // into the map so it renders and can be worked. Returns false (placing
        // nothing) when the spot is occupied by another building or the stock is
        // short.
        bool placeBuilding(Economy::Building building, const sf::Vector2i& worldPos);

        // Population and vitals, for the HUD and tests. `population` is the live
        // entity count; births/deaths accumulate over the run.
        int population() const { return entityCount(); }
        int births() const { return m_births; }
        int deaths() const { return m_deaths; }
        int maxPopulation() const { return m_config.survival.max_population; }
        int lifespanHours() const { return m_config.survival.lifespan_hours; }
        int birthIntervalHours() const { return m_config.survival.birth_interval_hours; }
};
