#pragma once

#include "../map_generator/MapGenerator.h"
#include "Components_Entities.h"
#include "EntityConfig.h"
#include "EntityDecision.h"
#include "EntityVitals.h"

#include "../helpers/Buildings.h"
#include "../helpers/EventLog.h"
#include "../helpers/Knowledge.h"
#include "../helpers/RunSummary.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Default catalog path for the EntityManager constructor. Tests define
// ONE_OF_TWENTY_SOURCE_DIR, so they find the real catalog regardless of the
// working directory; the game passes the path from config.json explicitly.
[[nodiscard]] inline std::string defaultBuildingsPath()
{
#ifdef ONE_OF_TWENTY_SOURCE_DIR
        return std::string(ONE_OF_TWENTY_SOURCE_DIR) + "/config/buildings.json";
#else
        return "config/buildings.json";
#endif
}

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
        // Where the tuning was loaded from, so it can be reloaded at runtime
        // without a rebuild.
        std::string                     m_config_path;
        // Consecutive decisions that produced "nothing urgent", per entity, so a
        // contented entity eventually wanders instead of standing still.
        std::unordered_map<entt::entity, int>   m_entity_idle;

        // The settlement's shared map knowledge: one store for the whole
        // civilization, fed by every entity's vision. Replaces the per-entity
        // memory map, so its size does not grow with the population.
        CivKnowledge                            m_knowledge;

        // Observability. `m_events` is the run's story (births, deaths with
        // cause, gathers, discoveries, production); `m_history` samples the
        // population and stores on in-game days so a run's shape can be compared
        // against a baseline. `m_death_cause` carries the cause decided when an
        // entity is marked dead to the removal pass that records the event, and
        // `m_discovered` logs each resource the settlement first finds once.
        Observability::EventLog                 m_events{ 1024 };
        Observability::RunHistory               m_history;
        std::unordered_map<entt::entity, Observability::EventCause> m_death_cause;
        std::unordered_set<int>                 m_discovered;

        // Settlement stores, plus a count of completed gathers so progress is
        // observable (HUD / tests). Goods are the economy's currency: raw gathers
        // and crafted production both land here, and food/spoilage draw from it.
        Goods::Stock                            m_goods;
        int                                     m_gathers_completed{ 0 };
        // Work (gather/build) that completed while the entity was more than one
        // tile from its target. The arrival latch should make this impossible; it
        // exists so a regression that resumes plan-time timing is caught by a test
        // rather than only visible on screen.
        int                                     m_off_tile_work{ 0 };
        int                                     m_food_produced{ 0 };

        // The building catalog (behaviour, cost, color per building) and its
        // settlement-wide tuning, loaded from config/buildings.json. Data-driven:
        // a new building is a JSON entry plus one Elements value.
        Buildings::Catalog                      m_catalog;
        Buildings::Settlement                   m_settlement;
        std::string                             m_buildings_path;

        // A placed building: which catalog definition it is, where it stands and
        // how much construction work remains. `complete` is false while builders
        // are still working it; once true its effects apply. The list is what the
        // hourly production pass and the capacity/effects queries iterate, so the
        // terrain never has to be rescanned for structures.
        struct PlacedBuilding
        {
                std::size_t  def_index{ 0 };    // index into m_catalog.all()
                sf::Vector2i origin;            // world position (tile corner)
                int          work_remaining{ 0 };
                bool         complete{ false };
                // The size rolled when the site was started: people housed for a
                // house, people fed for a food producer. Zero for anything else.
                int          rolled_value{ 0 };
        };
        std::vector<PlacedBuilding>             m_buildings;

        // The settlement's anchor (the city center), in world coordinates, once a
        // city center is complete. Used by the construction planner's radius and
        // as the newborn spawn. Empty until then.
        std::optional<sf::Vector2i>             m_anchor;

        // Last in-game hour the construction planner ran, so it fires once per
        // hour rather than every frame.
        std::int64_t                            m_last_plan_hour{ -1 };

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

        // Scratch tile block reused by findRoute so routing does not allocate per
        // call. A dense row-major buffer (index y * width + x) over the copied
        // window, so a tile read is an indexed load rather than a hash lookup.
        std::vector<Elements> m_tile_block;

        // The world position the pointer is currently over, set by the scene every
        // frame before entities are drawn. The per-entity info box is drawn only
        // for the entity under it, so the map is not covered by a wall of
        // readouts. Empty until the scene supplies a position.
        std::optional<sf::Vector2i>             m_hover_world;

        // Private function
        void addTextToEntityInfo(std::vector<sf::Text>& vec, std::string&& s, int size, const sf::Color& color);

        // Starting role for a freshly spawned entity: the configured job for its
        // type when given, otherwise a sensible default (humans build, animals
        // idle). The shortage rule can reassign it afterwards.
        Jobs::Job defaultJobFor(const EntityType& type) const;

        // Resolved appearance for an entity type and its current job. The job
        // look takes precedence (it personalizes a profession), falling back to
        // the type look and then the shipped default, so any of the three may be
        // left unconfigured.
        Appearance::Look lookFor(const EntityType& type, Jobs::Job job) const;

        // Movement cost of the tile under a world position, consulting the
        // catalog so a road speeds movement while terrain is unchanged.
        float tileCostAt(const sf::Vector2i& worldPos) const
        {
                return Buildings::walkCost(m_map->getElementAtWorld(worldPos), m_catalog);
        }

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
        std::optional<WorkTarget> settleElements(const CivKnowledge& knowledge, Jobs::Job job,
                                                 const sf::Vector2i& from) const;

        // Reassign idle entities to the most understaffed job, so the settlement
        // responds to shortages. Called when an entity completes a job assignment
        // cycle, not every frame.
        void reassignJobs();

        // Run the settlement's production once per in-game hour: farms grow food
        // (once wood is available), workshops turn raw goods into crafted ones,
        // and stored food spoils once per day.
        void produceGoods();
        void spoilFood();

        // The construction planner, run once per in-game hour: decide the next
        // building (city center first, then by priority/max_count/affordability),
        // find a valid site within the build radius, spend its cost and mark it a
        // site. Roads are laid separately as paths between completed buildings.
        void planConstruction();

        // Find a land tile for a new building near the anchor: dry land (never
        // sand or water), matching the def's `allowed_terrain` when it sets one,
        // and far enough from every placed building to leave a gap. The search
        // starts from a random point near the anchor (so successive sites scatter
        // rather than marching around the centre) and scans outward to the first
        // free tile. Returns nullopt when the radius offers nothing buildable.
        std::optional<sf::Vector2i> findBuildSite(const Buildings::Def& def) const;

        // The one rule for where a def may stand: its tile is dry land the def
        // allows, and it keeps the settlement's minimum gap from every placed
        // building. Shared by the planner and the placement API so both agree.
        bool canBuildOn(const Buildings::Def& def, const sf::Vector2i& worldPos) const;

        // The nearest incomplete construction site (world distance), or nullopt
        // when everything is built. Builders walk to it and apply work.
        std::optional<sf::Vector2i> nearestIncompleteSite(const sf::Vector2i& from) const;

        // The placed building occupying a world position, or nullptr.
        PlacedBuilding* findBuildingAt(const sf::Vector2i& worldPos);

        // Apply a finished building's effects: record the anchor for the city
        // center, log production recipes etc. Capacity is computed from completed
        // houses on demand, so it needs no stored effect.
        void applyBuildEffect(PlacedBuilding& building);

        // How many placed buildings use a given def index (for max_count checks).
        int countOf(std::size_t def_index) const;

        // Roll a building's size once, at construction: a food producer's reach,
        // or a house's capacity, from the def's range; a fixed value stays itself.
        static int rollBuildValue(const Buildings::Def& def);

        // Population dynamics, run once per in-game hour. `ageEntities` advances
        // lifespan, `applyHealth` drains/restores health from a food shortfall, and
        // `killTheDying` removes entities whose health or lifespan ran out.
        // `consumeFoodDaily` runs once per in-game day: it draws one food per
        // person from the store and charges each unfed entity a day of hunger.
        void decayNeeds(std::int64_t hourIndex);
        void ageEntities();
        void applyHealth();
        void killTheDying();
        void consumeFoodDaily();
        void tryBirths();

        // Log a resource the first time the settlement remembers it, so a run
        // records its discoveries without spamming one event per entity per tile.
        void recordDiscovery(Elements element);

        // Turn a decision into queued actions. Returns true if the entity is
        // already busy with the need (so the idle counter should reset).
        bool startActionFor(const EntityDecision::Need need,
                            const sf::Vector2i& pos,
                            float visionRadius,
                            const CivKnowledge& knowledge,
                            CActionsQueue& queue,
                            CPath& path,
                            Jobs::Job job);

public:

        bool show_vision = false;

        // CONSTRUCTOR
        // The buildings file is a defaulted argument so existing call sites (and
        // tests) keep compiling while still allowing the scene to pass the path
        // from config.json.
        EntityManager(const sf::Font& font, std::shared_ptr<MapGenerator> map, std::shared_ptr<GameClock> clock, float& deltatime,
                      const std::string& entity_file,
                      const std::string& buildings_file = defaultBuildingsPath())
                : m_font(font)
                , m_map(map)
                , m_game_clock(clock)
                , m_delta_time(deltatime)
                , m_config(loadEntityConfig(entity_file))
                , m_config_path(entity_file)
                , m_catalog(Buildings::loadCatalog(buildings_file))
                , m_settlement(Buildings::loadSettlement(buildings_file))
                , m_buildings_path(buildings_file)
        {
                // Size the shared knowledge store from config. Cell size is fixed at
                // construction: reloading the config does not rebuild the explored
                // map, because re-bucketing it mid-run would discard what is known.
                m_knowledge = CivKnowledge(m_config.knowledge.cell_size, m_config.knowledge.max_cells);
                m_registry = std::make_unique<entt::registry>();
        }

        // Re-read the entity config from disk so tuning does not need a rebuild.
        // Existing entities keep the components they already have; only the rules
        // consulted next (decay, decisions, survival, economy targets) change.
        // Throws std::runtime_error if the file is missing or malformed, so the
        // caller can keep the previous tuning rather than lose it silently. The
        // building catalog is re-read too, so new buildings/costs need no rebuild.
        void reloadConfig()
        {
                m_config = loadEntityConfig(m_config_path);
                m_catalog = Buildings::loadCatalog(m_buildings_path);
                m_settlement = Buildings::loadSettlement(m_buildings_path);

                // Repaint live entities so a look change is visible immediately,
                // without a restart.
                m_registry->view<CType, CJob, CShape>().each(
                        [&](auto, const CType& type, const CJob& job, CShape& shape)
                        {
                                shape.circle = lookFor(type.type, job.job).makeShape();
                        });
        }

        // The settlement's construction plan for this hour: cost and start a site.
        // Exposed so tests can drive the planner deterministically.
        void planConstructionForTest() { planConstruction(); }

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

        // Direct component access for tooling and tests: the entity returned by
        // addEntity can be driven without going through a full update. Returns
        // nullptr when the entity is dead or does not have the component.
        CBasicNeeds* needsOf(entt::entity entity);
        CActionsQueue* actionsOf(entt::entity entity);
        const CActionsQueue* actionsOf(entt::entity entity) const;
        // The settlement's shared knowledge, for the HUD and tests.
        const CivKnowledge& knowledge() const { return m_knowledge; }
        // Handles of every live entity, for tooling and tests.
        std::vector<entt::entity> entityHandles() const;

        // The raw registry, for tooling and tests that need a component the
        // convenience accessors do not cover (e.g. an entity's CShape).
        entt::registry& registry() { return *m_registry; }
        const entt::registry& registry() const { return *m_registry; }

        // Settlement totals, accumulated as gathers complete.
        int stockpile(const Elements element) const;
        int totalStockpile() const;
        int gathersCompleted() const { return m_gathers_completed; }
        // Completed gathers/builds that happened away from their target tile. A
        // well-behaved run reports zero; used to guard the arrival latch.
        int offTileWork() const { return m_off_tile_work; }

        // The world position the pointer is over, set by the scene on mouse move.
        // The per-entity readout is drawn only for the entity under it.
        void setHoverWorld(const sf::Vector2i& worldPos) { m_hover_world = worldPos; }
        void clearHoverWorld() { m_hover_world.reset(); }

        // The entity whose body contains a world position (nearest wins when
        // bodies overlap), or nullopt when the pointer is over empty ground. Uses
        // a fixed pick radius a little larger than the drawn body, so a hover is
        // easy to land. Public so the hover hit test can be checked without a
        // window.
        std::optional<entt::entity> entityAtWorld(const sf::Vector2i& worldPos) const;

        // The economy's stores, for the HUD and tests.
        int good(const Goods::Good which) const { return m_goods.count(which); }
        const Goods::Stock& goods() const { return m_goods; }
        // Credit the stock directly, so a test (or a future gift/trade effect) can
        // seed a store without routing through a gather.
        void addGoods(const Goods::Good which, const int amount) { m_goods.add(which, amount); }
        int foodProduced() const { return m_food_produced; }
        int buildingCount() const { return static_cast<int>(m_buildings.size()); }
        int completedBuildingCount() const;
        // The building catalog, for the scene (to color the map) and tests.
        const Buildings::Catalog& buildings() const { return m_catalog; }
        // How many of each building element exist (placed, complete or not), for
        // the HUD readout.
        int countOfElement(Elements element) const;

        // The population cap: the entity config's `survival.max_population` base
        // plus the size each completed building rolled when it was started (the
        // city center's people, houses' people), so a built settlement outgrows
        // its founding cap.
        int populationCapacity() const;
        // The site the planner would pick for `building_id` right now, or nullopt
        // when the def is unknown or no tile in the radius is free. Exposed so a
        // test can exercise the site search (and its scatter) directly.
        std::optional<sf::Vector2i> nextBuildSite(const std::string& building_id) const;
        // Whether a city center has been completed; the settlement's anchor.
        bool hasCityCenter() const;
        // The settlement anchor (city-center origin) once it exists.
        std::optional<sf::Vector2i> settlementAnchor() const { return m_anchor; }

        // Current job staffing, indexed by Jobs::Job, for the HUD.
        std::array<int, Jobs::kJobCount> jobCounts() const;
        Jobs::Job firstJob() const;

        // Start a construction site for a catalog building on a tile: spend its
        // cost, write the element into the map so it renders, and push an
        // incomplete PlacedBuilding. Returns false (placing nothing) when the def
        // is unknown, the spot is occupied, or the stock is short.
        bool placeBuilding(const std::string& building_id, const sf::Vector2i& worldPos);
        // Complete a site immediately (used by tests and effects), applying its
        // effects. Returns false when there is no site there.
        bool completeBuilding(const sf::Vector2i& worldPos);

        // Population and vitals, for the HUD and tests. `population` is the live
        // entity count; births/deaths accumulate over the run.
        int population() const { return entityCount(); }
        int births() const { return m_births; }
        int deaths() const { return m_deaths; }
        // Backwards-compatible name: the cap is now computed from the settlement's
        // base capacity and its completed houses, not a flat config constant.
        int maxPopulation() const { return populationCapacity(); }
        int lifespanHours() const { return m_config.survival.lifespan_hours; }
        int birthIntervalHours() const { return m_config.survival.birth_interval_hours; }

        // Observability, for the HUD and tests.
        const Observability::EventLog& events() const { return m_events; }
        const Observability::RunHistory& history() const { return m_history; }
        // Summary of the samples taken so far (peak/trough population, days).
        Observability::RunSummary runSummary() const
        {
                Observability::RunSummary summary = m_history.summarize();
                summary.births = m_births;
                summary.deaths = m_deaths;
                summary.gathers = m_gathers_completed;
                return summary;
        }
        int lastEventMinute() const
        {
                return m_events.empty() ? 0
                     : static_cast<int>(m_events.events().back().timestamp_min);
        }
};
