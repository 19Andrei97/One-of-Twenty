
#include <pch.h>

#include "EntityManager.h"

#include "Pathfinding.h"

#include <algorithm>

namespace
{
// Distance (world units) at which a move counts as arrived. Targets are tile
// corners on the same lattice as entity positions, so this only has to absorb
// the integer truncation left by a fractional step.
constexpr float kArriveDistance{ 1.0f };

// Does the queue already hold an action of this kind (planned or in progress)?
bool queueHasAction(const CActionsQueue& queue, const ActionTypes type)
{
    return std::any_of(queue.actions.begin(), queue.actions.end(),
                       [type](const std::shared_ptr<CAction>& action)
                       { return action && action->action_name == type; });
}

// Squared distance between two positions. Squared so comparisons stay integer
// and avoid a square root in the per-candidate selection loops.
int squaredDistance(const sf::Vector2i& a, const sf::Vector2i& b) noexcept
{
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return dx * dx + dy * dy;
}
} // namespace

void EntityManager::update()
{
        const std::int64_t now = m_game_clock->getTimestamp();
        const std::int64_t hourIndex = now / 60;

        if (m_last_survival_tick < 0)
                m_last_survival_tick = hourIndex * 60 - 60; // run the first hour too
        if (m_last_frame_minutes < 0)
                m_last_frame_minutes = now;

        // Movement is measured in in-game time, not real time: an entity covers the
        // same ground per in-game hour whatever the clock speed, so speeding the
        // clock up does not outrun the walk to water. A paused clock advances no
        // minutes, so entities freeze with it.
        const float gameHours = static_cast<float>(now - m_last_frame_minutes)
                              / static_cast<float>(GameTime::kMinutesPerHour);
        m_last_frame_minutes = now;

        // The clock can advance several in-game hours in a single frame (and wraps
        // at midnight), so step hour by hour instead of watching the hour-of-day,
        // which would skip most hours and never fire on a wrap.
        for (std::int64_t hour = m_last_survival_tick / 60 + 1; hour <= hourIndex; ++hour)
        {
                decayNeeds(hour);
                ageEntities();
                applyHealth();
                killTheDying();
                // The construction planner decides what to build and marks a site;
                // production then runs over the buildings that are complete.
                planConstruction();
                produceGoods();
                tryBirths();
        }
        m_last_survival_tick = now;

        // The daily food pass: the settlement consumes one food per person and any
        // entity that could not be fed accrues a hungry day, while a fed entity has
        // its hunger streak reset. Running it per crossed day (not per hour) keeps
        // consumption and hunger on one a-day cadence; spoilage follows on the same
        // boundary.
        const std::int64_t dayIndex = now / GameTime::kMinutesPerDay;
        if (m_last_spoilage_day < 0)
                m_last_spoilage_day = dayIndex;
        else if (dayIndex > m_last_spoilage_day)
        {
                consumeFoodDaily();
                spoilFood();
                m_last_spoilage_day = dayIndex;
        }

        // Keep the roles staffed: idle entities move to whichever job is short.
        reassignJobs();

        // REMOVE ENTITIES
        std::vector<entt::entity> toDestroy;

        m_registry->view<CLifespan>().each([&](auto entity, CLifespan& life)
        {
                if (life.remaining <= 0)
                {
                        toDestroy.push_back(entity);
                }
        });

        for (auto entity : toDestroy)
        {
                auto cause = m_death_cause.find(entity);
                m_events.record(Observability::EventKind::Death,
                                cause != m_death_cause.end() ? cause->second : Observability::EventCause::Natural,
                                now);
                m_death_cause.erase(entity);
                m_registry->destroy(entity);
                m_entity_idle.erase(entity);
        }

        if (!toDestroy.empty())
        {
                m_deaths += static_cast<int>(toDestroy.size());
                LOG_INFO("Population: {} died (population now {}).",
                         toDestroy.size(), static_cast<int>(m_registry->view<CType>().size()));
        }

        // UPDATE ENTITIES

        // Update the settlement's shared knowledge. The vision scan touches ~1k
        // tiles per entity, so only redo it when the entity steps onto a new tile:
        // the known set changes slowly and this is the dominant per-frame cost.
        // Every entity's observation feeds the same civ-level store, so a resource
        // found by one is known to all and nothing is duplicated per entity.
        m_registry->view<CTransform, CKnowledgeScan, CVision>().each([&](auto entity, auto& trs, auto& scan, auto& vision)
        {
                const sf::Vector2i tile = CoordMath::worldToTile(trs.pos, m_map->getTileSize());
                if (scan.last_scan_tile && *scan.last_scan_tile == tile)
                        return;

                scan.last_scan_tile = tile;
                const auto found = m_map->getResourcesWithinBoundary(trs.pos, vision.radius);
                m_knowledge.remember(found);
                m_knowledge.observe(tile, static_cast<int>(vision.radius / static_cast<float>(m_map->getTileSize())));
                for (const auto& [element, pos] : found)
                        recordDiscovery(element);
        });

        // Moving
        m_registry->view<CActionsQueue, CTransform, CPath>().each([&](auto entity, auto& queue, auto& trs, auto& path)
        {
                if (queue.actions.empty())
                        return;

                auto action = std::dynamic_pointer_cast<CMoving>(queue.actions.front());
                if (!action)
                        return;

                // If the action's target moved, the route is stale: drop it so the
                // step below recomputes one.
                if (!path.empty() && path.waypoints.back() != action->target)
                        path.waypoints.clear();

                // Distance budget for this frame, in pixels. Measured in in-game
                // hours so an entity covers the same ground per in-game hour whatever
                // the clock speed. The budget is spent across as many waypoints as it
                // reaches: at high speeds a frame spans several in-game hours, and
                // advancing a single waypoint per frame would leave entities crawling
                // while the clock raced ahead.
                float budget = trs.speed * gameHours;
                if (budget <= 0.f)
                        return;

                // Walk toward the next waypoint (when routing) or the action's target
                // (straight-line fallback), spending from the frame budget. Returns
                // true once the goal is reached.
                const auto stepToward = [&](const sf::Vector2i& goal) -> bool
                {
                        const sf::Vector2i toTarget = goal - trs.pos;
                        const float distance = std::sqrt(static_cast<float>(toTarget.x * toTarget.x + toTarget.y * toTarget.y));

                        if (distance <= kArriveDistance)
                        {
                                trs.pos = goal;
                                return true;
                        }

                        // Never step past the target: a frame that moves further than
                        // the remaining distance would overshoot and the entity would
                        // oscillate forever, never "arriving" to drink or eat.
                        // Catalog-aware cost, so a road speeds the step; terrain
                        // falls back to MoveCost inside Buildings::walkCost.
                        const float cost{ tileCostAt(trs.pos) };
                        const float step = std::min(budget * cost, distance);
                        if (step >= distance)
                        {
                                trs.pos = goal;
                                budget -= distance / cost;
                                return true;
                        }

                        sf::Vector2i delta{
                                static_cast<int>(std::lround(toTarget.x / distance * step)),
                                static_cast<int>(std::lround(toTarget.y / distance * step)) };
                        // Rounding a sub-pixel step can yield (0,0); nudge one pixel
                        // along the dominant axis so progress is guaranteed.
                        if (delta.x == 0 && delta.y == 0)
                        {
                                if (std::abs(toTarget.x) >= std::abs(toTarget.y))
                                        delta.x = (toTarget.x > 0) ? 1 : -1;
                                else
                                        delta.y = (toTarget.y > 0) ? 1 : -1;
                        }
                        trs.pos += delta;
                        budget -= step / cost;
                        return false;
                };

                // Chain waypoints while the frame budget lasts. The guard stops a
                // pathological route from spinning.
                for (int guard = 0; guard < 4096 && budget > 0.f; ++guard)
                {
                        if (!path.empty())
                        {
                                if (!stepToward(path.current()))
                                        break; // budget spent before reaching it

                                std::lock_guard<std::mutex> lock(m_mutex);
                                path.advance();
                                if (path.empty())
                                {
                                        queue.actions.pop_front();
                                        break;
                                }
                                continue;
                        }

                        // No route yet (or none could be found): walk straight at the
                        // target. This is also the path for a target on the entity's
                        // own tile.
                        if (stepToward(action->target))
                        {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                queue.actions.pop_front();
                        }
                        break;
                }
        });

        // Finish actions that have run their course, restoring the need they serve
        // or banking what a gather produced. The work actions (gather/build) are
        // queued before the walk, so this pass also latches their timer on the
        // first frame the entity is actually at the tile: the move is popped on
        // arrival, so a work action at the front means the entity has just arrived.
        m_registry->view<CActionsQueue, CBasicNeeds, CInventory, CTransform>().each([&](auto entity, auto& queue, auto& needs, auto& inventory, auto& trs)
        {
                if (queue.actions.empty())
                        return;

                const auto& front = queue.actions.front();
                const std::int64_t elapsed = m_game_clock->getTimestamp();
                const int tileSize = m_map->getTileSize();
                // Work must happen on the tile; a completion further than one tile
                // away is a bug (the arrival latch prevents it). Recorded so the
                // regression is testable, not just visible on screen.
                const auto offTile = [&](const sf::Vector2i& tile) {
                        return squaredDistance(tile, trs.pos) > tileSize * tileSize;
                };

                if (auto action = std::dynamic_pointer_cast<CSleeping>(front); action
                        && elapsed - action->timestamp_min > action->duration_min)
                {
                        needs.satisfySleep();
                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
                else if (auto action = std::dynamic_pointer_cast<CGather>(front); action)
                {
                        // The timer starts only once the entity has arrived at the
                        // tile, so the gather never banks a yield mid-journey.
                        if (!action->started)
                        {
                                action->started = true;
                                action->timestamp_min = elapsed;
                                return;
                        }
                        if (elapsed - action->timestamp_min <= action->duration_min)
                                return;

                        // The work must be done on the tile: if the entity has
                        // drifted off it (blocked, pushed, or re-routed part way),
                        // hold the completion until it is back on the tile instead of
                        // banking the yield from a distance. offTileWork() counts any
                        // completion further than one tile away, so it stays zero.
                        if (offTile(action->tile))
                                return;

                        // The gather produced a unit: carry it, then deposit it into
                        // the settlement stores as the good its tile yields, and
                        // tally the completed trip. A forest tile is a finite pile of
                        // wood: deplete it and, once exhausted, clear it to a hill.
                        inventory.gather();
                        const Elements yielded = action->element;
                        m_goods.add(Goods::fromElement(yielded), inventory.deposit());
                        ++m_gathers_completed;
                        if (yielded == Elements::forest)
                        {
                                const int remaining = m_map->harvestWood(action->tile);
                                m_events.record(Observability::EventKind::Gather, m_game_clock->getTimestamp(),
                                                1, remaining == 0 ? "Wood (stand cleared)" : Goods::name(Goods::Good::Wood));
                        }
                        else
                        {
                                m_events.record(Observability::EventKind::Gather, m_game_clock->getTimestamp(),
                                                1, Goods::name(Goods::fromElement(yielded)));
                        }

                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
                else if (auto action = std::dynamic_pointer_cast<CBuild>(front); action)
                {
                        // Same latch as a gather: no construction progress before
                        // the builder stands on the site.
                        if (!action->started)
                        {
                                action->started = true;
                                action->timestamp_min = elapsed;
                                return;
                        }
                        if (elapsed - action->timestamp_min <= action->duration_min)
                                return;

                        // As with a gather, hold the work until the builder is back
                        // on the site: a completion while off the tile would apply
                        // the build effect somewhere other than where it was raised.
                        if (offTile(action->tile))
                                return;

                        // Advance the site by one hour's work. A completed site's
                        // effects apply once (applyBuildEffect is idempotent on the
                        // `complete` flag) and a construction-finished event is
                        // recorded, so the run's story includes what was built.
                        if (PlacedBuilding* site = findBuildingAt(action->tile))
                        {
                                if (!site->complete)
                                {
                                        const auto& def = m_catalog.all()[site->def_index];
                                        site->work_remaining -= std::max(1, def.work_per_hour);
                                        if (site->work_remaining <= 0)
                                        {
                                                applyBuildEffect(*site);
                                                m_events.record(Observability::EventKind::Construction,
                                                                m_game_clock->getTimestamp(), 1, def.name);
                                                LOG_INFO("Built {} at ({},{}).", def.name,
                                                         site->origin.x, site->origin.y);
                                        }
                                }
                        }

                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
        });

        // Decide what each entity should do. A busy entity keeps its plan; an idle
        // one asks the weighted policy and starts the winning action.
        m_registry->view<CActionsQueue, CTransform, CBasicNeeds, CPersonality, CVision, CPath, CJob>()
                .each([&](auto entity, auto& queue, auto& trs, auto& needs, auto& personality, auto& vision, auto& path, auto& job)
        {
                int& idle = m_entity_idle[entity];

                if (!queue.actions.empty())
                {
                        idle = 0;

                        // Survival first: a busy entity drops a non-survival plan
                        // (a long gather trip, a wander) for sleep once it turns
                        // urgent, so a long trip cannot exhaust it. A plan that is
                        // already a sleep is left alone.
                        const bool busySurviving = queueHasAction(queue, ActionTypes::Sleeping);
                        if (!busySurviving)
                        {
                                const auto& front = queue.actions.front();
                                const ActionTypes current = front ? front->action_name : ActionTypes::Idle;
                                const auto urgent = EntityDecision::interruptFor(needs, personality, m_config.decision, current);

                                // Sleep needs no target (the entity lies down where
                                // it stands), so any urgent sleep is actionable. The
                                // check below only skips re-planning an action that
                                // is already on the way.
                                if (urgent && !queueHasAction(queue, EntityDecision::actionFor(*urgent)))
                                {
                                        // Plan the interruption transactionally: keep
                                        // the old plan unless the new one actually
                                        // starts the urgent need's action, so an
                                        // unreachable target does not strand the entity
                                        // with an empty queue or a cleared route.
                                        auto previous = queue.actions;
                                        std::vector<sf::Vector2i> previous_path;
                                        {
                                                std::lock_guard<std::mutex> lock(m_mutex);
                                                previous_path = path.waypoints;
                                                queue.actions.clear();
                                                path.waypoints.clear();
                                        }
                                        const ActionTypes wanted = EntityDecision::actionFor(*urgent);
                                        startActionFor(*urgent, trs.pos, vision.radius, m_knowledge, queue, path, job.job);
                                        if (queueHasAction(queue, wanted))
                                        {
                                                idle = 0;
                                        }
                                        else
                                        {
                                                std::lock_guard<std::mutex> lock(m_mutex);
                                                queue.actions = std::move(previous);
                                                path.waypoints = std::move(previous_path);
                                        }
                                }
                        }
                        return;
                }

                const EntityDecision::Need need =
                        EntityDecision::decide(needs, personality, m_config.decision, idle, job.job);

                if (need == EntityDecision::Need::None)
                {
                        ++idle;
                        return;
                }

                if (startActionFor(need, trs.pos, vision.radius, m_knowledge, queue, path, job.job))
                        idle = 0;
        });

        // The per-entity readout is not refreshed here: only the hovered entity's
        // box is drawn, so its text is built in render() on demand instead of
        // rebuilding a panel for every entity every frame.

        // Observability: sample the run on its daily cadence. Sampling here (once
        // per update) rather than per hour keeps the history one point per day
        // regardless of how many hours a single frame spans.
        m_history.sample(Observability::RunPoint{ now, entityCount(),
                                                  buildingCount(), m_goods });
}

void EntityManager::recordDiscovery(const Elements element)
{
        // A "discovery" is the settlement first noticing a resource category. The
        // key is the element itself, so a lake and a second lake are one event,
        // while food, wood and ore each record separately. Logging once per
        // category (not per entity or per tile) keeps the log readable.
        const int key = static_cast<int>(element);
        if (!m_discovered.insert(key).second)
                return;
        m_events.record(Observability::EventKind::Discovery, m_game_clock->getTimestamp(),
                        0, Resources::name(element));
}

// Translate a decision into queued actions. A need with a remembered target
// walks there first (routed around water); a need with no memory (or a wander
// decision) walks to a random land tile so the entity explores and refreshes the
// shared knowledge. Targets that no land route reaches fall back to exploring.
bool EntityManager::startActionFor(const EntityDecision::Need need,
                                   const sf::Vector2i& pos,
                                   const float visionRadius,
                                   const CivKnowledge& knowledge,
                                   CActionsQueue& queue,
                                   CPath& path,
                                   const Jobs::Job job)
{
        // Walk to a random land tile in vision, routing when possible and falling
        // back to a straight line otherwise, so exploration never stalls.
        const auto queueExplore = [&]() -> bool
        {
                sf::Vector2i from = pos;
                const int ts = m_map->getTileSize();
                // Snap to a tile corner: routes are built between tile corners, and
                // the move target must match the route's last waypoint.
                const sf::Vector2i target =
                        CoordMath::tileToWorld(CoordMath::worldToTile(m_map->getLocationWithinBound(from, visionRadius), ts), ts);
                if (!queueMoveTo(pos, target, path, queue))
                        queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, target));
                return true;
        };

        switch (need)
        {
                case EntityDecision::Need::Sleep:
                {
                        if (queueHasAction(queue, ActionTypes::Sleeping))
                                return false;

                        queue.actions.push_back(std::make_shared<CSleeping>(ActionTypes::Sleeping, m_game_clock->getTimestamp()));
                        return true;
                }

                case EntityDecision::Need::Work:
                {
                        // A builder heads to the nearest incomplete site and works
                        // it. With nothing to build it falls through to gathering, so
                        // early-game wood production is unaffected.
                        if (job == Jobs::Job::Builder && !queueHasAction(queue, ActionTypes::Building))
                        {
                                if (auto site = nearestIncompleteSite(pos))
                                {
                                        if (queueMoveTo(pos, *site, path, queue))
                                        {
                                                queue.actions.push_back(std::make_shared<CBuild>(
                                                        ActionTypes::Building, *site, m_game_clock->getTimestamp()));
                                                return true;
                                        }
                                }
                        }

                        if (queueHasAction(queue, ActionTypes::Gathering))
                                return false;

                        if (auto target = settleElements(knowledge, job, pos))
                        {
                                if (queueMoveTo(pos, target->pos, path, queue))
                                {
                                        queue.actions.push_back(std::make_shared<CGather>(ActionTypes::Gathering, target->pos, target->element,
                                                                                          m_game_clock->getTimestamp()));
                                        return true;
                                }
                        }
                        return queueExplore();
                }

                case EntityDecision::Need::Explore:
                {
                        if (queueHasAction(queue, ActionTypes::Moving))
                                return false;

                        // An explorer roams. It does not route to a distant frontier:
                        // the frontier moves as knowledge grows, so routing to it every
                        // leg both re-planned constantly and could pace in place. Walking
                        // to a random land tile within sight keeps it moving over fresh
                        // ground, and the vision scan it triggers is what actually grows
                        // the settlement's shared map.
                        return queueExplore();
                }

                case EntityDecision::Need::Wander:
                {
                        if (queueHasAction(queue, ActionTypes::Moving))
                                return false;

                        return queueExplore();
                }

                case EntityDecision::Need::None:
                default:
                        return false;
        }
}

// A* route between two world positions. Returns the world-space waypoints
// (tile corners, excluding the tile the entity already stands on), `nullopt`
// when both ends share a tile, or an empty vector when no land route exists.
std::optional<std::vector<sf::Vector2i>> EntityManager::findRoute(const sf::Vector2i& from, const sf::Vector2i& to)
{
        const int ts = m_map->getTileSize();
        const sf::Vector2i startTile = CoordMath::worldToTile(from, ts);
        const sf::Vector2i goalTile = CoordMath::worldToTile(to, ts);
        if (startTile == goalTile)
                return std::nullopt;

        // Bound the search to a window around the two ends. A window keeps the
        // scan (and its tile copy) small; a trip longer than it fails and the
        // caller falls back to a straight line, re-planning as it goes.
        constexpr int kWindow = MapGenerator::kMaxTileBlock;
        const int spanX = std::abs(startTile.x - goalTile.x);
        const int spanY = std::abs(startTile.y - goalTile.y);
        if (spanX >= kWindow || spanY >= kWindow)
                return std::vector<sf::Vector2i>{};

        const int margin = std::max(4, std::max(spanX, spanY) / 4);
        const int side = std::min(kWindow, std::max(spanX, spanY) + margin * 2 + 1);
        const sf::Vector2i topLeft{ std::min(startTile.x, goalTile.x) - margin,
                                    std::min(startTile.y, goalTile.y) - margin };

        const int blockSide = m_map->copyTileBlock(topLeft, side, m_tile_block);
        const auto& block = m_tile_block;
        // Outside the copied window is unknown; treat it as impassable so the
        // search cannot wander off the snapshot.
        const auto elementAt = [&](const sf::Vector2i& tile) -> Elements
        {
                const sf::Vector2i rel = tile - topLeft;
                if (rel.x < 0 || rel.y < 0 || rel.x >= blockSide || rel.y >= blockSide)
                        return Elements::very_deep_ocean;
                return block[static_cast<std::size_t>(rel.y) * blockSide + rel.x];
        };

        const auto path = Pathfinding::findPath(
                startTile, goalTile,
                // Catalog-aware cost so a road is cheaper to route along; terrain
                // falls back to MoveCost inside Buildings::walkCost.
                [&](const sf::Vector2i& tile) { return Buildings::walkCost(elementAt(tile), m_catalog); },
                [&](const sf::Vector2i& tile) { return !Resources::isOcean(elementAt(tile)); },
                Pathfinding::kDefaultNodeCap);
        if (path.empty())
                return std::vector<sf::Vector2i>{};

        std::vector<sf::Vector2i> waypoints;
        waypoints.reserve(path.size() - 1);
        for (std::size_t i = 1; i < path.size(); ++i)
                waypoints.push_back(CoordMath::tileToWorld(path[i], ts));
        return waypoints;
}

bool EntityManager::queueMoveTo(const sf::Vector2i& from, const sf::Vector2i& target,
                                CPath& path, CActionsQueue& queue)
{
        auto route = findRoute(from, target);
        if (!route)
        {
                // Same tile: a move with no route still lets movement finish it.
                path.waypoints.clear();
        }
        else if (route->empty())
        {
                return false; // no land route to the target
        }
        else
        {
                path.waypoints = *route;
        }

        queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, target));
        return true;
}

void EntityManager::resolveCollisions()
{
        const int ts = m_map->getTileSize();

        // Keep entities out of the ocean. An entity that ends up on a water tile
        // (pushed there, or spawned in a corner) steps back to the nearest land
        // tile in its 8-neighbourhood.
        m_registry->view<CTransform>().each([&](auto, CTransform& trs)
        {
                const sf::Vector2i tile = CoordMath::worldToTile(trs.pos, ts);
                const sf::Vector2i center = CoordMath::tileToWorld(tile, ts);
                if (!Resources::isOcean(m_map->getElementAtWorld(center)))
                        return;

                const sf::Vector2i offset = trs.pos - center;
                static constexpr sf::Vector2i kAround[8]{ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                                                          { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
                for (const auto& d : kAround)
                {
                        const sf::Vector2i candidate = center + sf::Vector2i{ d.x * ts, d.y * ts };
                        if (!Resources::isOcean(m_map->getElementAtWorld(candidate)))
                        {
                                trs.pos = candidate + offset;
                                return;
                        }
                }
        });

        // Separate overlapping entities with a symmetric push, but keep a worker
        // anchored to the tile it is working: a gather/build action is performed
        // on that tile, so separation must not shove the entity off it (a
        // neighbour absorbs the whole push instead of half).
        constexpr int kMinSeparation = 12;
        // The tile an entity is currently working, if its front action is a
        // gather or build. Such an entity does not move during separation.
        const auto workAnchor = [&](entt::entity entity) -> std::optional<sf::Vector2i>
        {
                const CActionsQueue* queue = m_registry->try_get<CActionsQueue>(entity);
                if (!queue || queue->actions.empty())
                        return std::nullopt;
                const auto& front = queue->actions.front();
                if (auto gather = std::dynamic_pointer_cast<CGather>(front))
                        return gather->tile;
                if (auto build = std::dynamic_pointer_cast<CBuild>(front))
                        return build->tile;
                return std::nullopt;
        };

        std::vector<std::pair<entt::entity, sf::Vector2i>> positions;
        std::vector<std::optional<sf::Vector2i>> anchors;
        m_registry->view<CTransform>().each([&](auto entity, CTransform& trs)
        {
                positions.emplace_back(entity, trs.pos);
                anchors.push_back(workAnchor(entity));
        });

        for (std::size_t i = 0; i < positions.size(); ++i)
        {
                for (std::size_t j = i + 1; j < positions.size(); ++j)
                {
                        const sf::Vector2i d = positions[j].second - positions[i].second;
                        const int dist2 = d.x * d.x + d.y * d.y;
                        if (dist2 >= kMinSeparation * kMinSeparation || dist2 == 0)
                                continue;

                        const float dist = std::sqrt(static_cast<float>(dist2));
                        const float overlap = static_cast<float>(kMinSeparation) - dist;
                        const sf::Vector2i unit{
                                static_cast<int>(std::lround(d.x / dist)),
                                static_cast<int>(std::lround(d.y / dist)) };
                        // A worker stays put; its neighbour takes the full push so
                        // the pair still separates. Two workers of the same tile are
                        // both anchored and simply remain.
                        if (anchors[i] && anchors[j])
                                continue;
                        if (anchors[i])
                        {
                                positions[j].second += sf::Vector2i{
                                        static_cast<int>(std::lround(unit.x * overlap)),
                                        static_cast<int>(std::lround(unit.y * overlap)) };
                        }
                        else if (anchors[j])
                        {
                                positions[i].second -= sf::Vector2i{
                                        static_cast<int>(std::lround(unit.x * overlap)),
                                        static_cast<int>(std::lround(unit.y * overlap)) };
                        }
                        else
                        {
                                const float push = overlap * 0.5f;
                                const sf::Vector2i shift{
                                        static_cast<int>(std::lround(d.x / dist * push)),
                                        static_cast<int>(std::lround(d.y / dist * push)) };
                                positions[i].second -= shift;
                                positions[j].second += shift;
                        }
                }
        }

        // Write back, but never shove an entity into the sea.
        for (auto& [entity, pos] : positions)
        {
                auto& trs = m_registry->get<CTransform>(entity);
                if (Resources::isOcean(m_map->getElementAtWorld(pos)))
                        continue;
                trs.pos = pos;
        }

        // De-stack: the push above can be blocked by water, leaving entities on
        // the same spot. Fan each duplicate out to an adjacent land tile that no
        // already-placed entity occupies, so the settlement does not render as a
        // single dot and no two entities share a position. An anchored worker is
        // never fanned out: it has to stay on the tile it is working.
        static constexpr sf::Vector2i kAround[8]{ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                                                  { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
        std::vector<sf::Vector2i> placed;
        placed.reserve(positions.size());
        for (std::size_t k = 0; k < positions.size(); ++k)
        {
                auto& trs = m_registry->get<CTransform>(positions[k].first);
                const bool duplicate = std::find(placed.begin(), placed.end(), trs.pos) != placed.end();
                if (duplicate && !anchors[k])
                {
                        const sf::Vector2i tile = CoordMath::worldToTile(trs.pos, ts);
                        for (const auto& d : kAround)
                        {
                                const sf::Vector2i candidate = CoordMath::tileToWorld(tile + d, ts);
                                if (Resources::isOcean(m_map->getElementAtWorld(candidate)))
                                        continue;
                                if (std::find(placed.begin(), placed.end(), candidate) != placed.end())
                                        continue;
                                trs.pos = candidate;
                                break;
                        }
                }
                placed.push_back(trs.pos);
        }
}

// Economy ////////////////////////////////////////////////////////////////////////

void EntityManager::produceGoods()
{
        // Every completed building with recipes runs once per hour. A farm grows
        // food from pure labour; a workshop turns raw goods into crafted ones.
        // The element switch is gone: behaviour comes from the catalog def, so a
        // new producer is a JSON entry.
        for (auto& building : m_buildings)
        {
                if (!building.complete)
                        continue;
                const auto& def = m_catalog.all()[building.def_index];
                if (!def.hasRecipes())
                        continue;

                // A food producer's size is the reach rolled when its site was
                // started (the 40-80 band), not the recipe's per-hour amount: it
                // grows `rolled_value` food a day, credited a 24th per hour with
                // the remainder carried, so the daily total is exact. A def with no
                // rolled reach (a unit-test farm) keeps the recipe's own output.
                if (def.feeds_population > 0 && building.rolled_value > 0)
                {
                        building.food_progress += building.rolled_value;
                        const int produced = building.food_progress / GameTime::kHoursPerDay;
                        building.food_progress %= GameTime::kHoursPerDay;
                        if (produced > 0)
                        {
                                m_goods.add(Goods::Good::Food, produced);
                                m_food_produced += produced;
                                m_events.record(Observability::EventKind::Production,
                                                m_game_clock->getTimestamp(), produced, def.name);
                        }
                        continue;
                }

                // A recipe with no input yields per the configured output; anything
                // else is capped by the shipped per-hour production tuning.
                const int before = m_goods.count(def.recipes.front().output_good);
                if (Buildings::produceOnce(m_goods, def))
                {
                        const int produced = m_goods.count(def.recipes.front().output_good) - before;
                        if (def.recipes.front().output_good == Goods::Good::Food && produced > 0)
                                m_food_produced += produced;
                        m_events.record(Observability::EventKind::Production, m_game_clock->getTimestamp(),
                                        produced, def.name);
                }
        }
}

void EntityManager::spoilFood()
{
        Goods::spoil(m_goods, m_config.economy.food_spoilage_percent_per_day);
}

std::optional<sf::Vector2i> EntityManager::nearestIncompleteSite(const sf::Vector2i& from) const
{
        std::optional<sf::Vector2i> best;
        int bestDist = 0;
        for (const auto& building : m_buildings)
        {
                if (building.complete)
                        continue;
                const int dist = squaredDistance(building.origin, from);
                if (!best || dist < bestDist)
                {
                        best = building.origin;
                        bestDist = dist;
                }
        }
        return best;
}

EntityManager::PlacedBuilding* EntityManager::findBuildingAt(const sf::Vector2i& worldPos)
{
        for (auto& building : m_buildings)
                if (building.origin == worldPos)
                        return &building;
        return nullptr;
}

int EntityManager::countOf(const std::size_t def_index) const
{
        int total = 0;
        for (const auto& building : m_buildings)
                if (building.def_index == def_index)
                        ++total;
        return total;
}

int EntityManager::completedBuildingCount() const
{
        int total = 0;
        for (const auto& building : m_buildings)
                if (building.complete)
                        ++total;
        return total;
}

int EntityManager::countOfElement(const Elements element) const
{
        int total = 0;
        for (const auto& building : m_buildings)
                if (m_catalog.all()[building.def_index].element == element)
                        ++total;
        return total;
}

int EntityManager::populationCapacity() const
{
        // The base cap is the entity config's `survival.max_population` (so the
        // existing tuning and its reload test still govern growth); only *housing*
        // raises it, i.e. a completed def that adds population capacity. A food
        // producer's rolled value is its food reach, not beds, so counting it here
        // would let every farm invite the births that demand the next farm.
        int capacity = m_config.survival.max_population;
        for (const auto& building : m_buildings)
        {
                if (!building.complete)
                        continue;
                if (m_catalog.all()[building.def_index].population_capacity > 0)
                        capacity += building.rolled_value;
        }
        return capacity;
}

int EntityManager::rollBuildValue(const Buildings::Def& def)
{
        // Roll the size once, when the site starts, so it sticks for the building's
        // whole life (a reload cannot re-roll it). A food producer's reach wins
        // when it has one, otherwise a house's capacity; both accept a range (a
        // `_max`) or a single value. Anything else rolls to zero.
        const auto roll = [](const int lo, const int hi)
        {
                if (hi <= lo)
                        return lo;
                return Random::get(lo, hi);
        };
        if (def.feeds_population > 0)
                return roll(def.feeds_population, def.feeds_population_max);
        if (def.population_capacity > 0)
                return roll(def.population_capacity, def.population_capacity_max);
        return 0;
}

bool EntityManager::hasCityCenter() const
{
        return m_anchor.has_value();
}

std::optional<sf::Vector2i> EntityManager::nextBuildSite(const std::string& building_id) const
{
        const Buildings::Def* def = m_catalog.byId(building_id);
        if (!def)
                return std::nullopt;
        return findBuildSite(*def);
}

void EntityManager::applyBuildEffect(PlacedBuilding& building)
{
        if (building.complete)
                return;

        building.complete = true;
        const auto& def = m_catalog.all()[building.def_index];
        if (def.is_anchor)
                m_anchor = building.origin;
}

std::optional<sf::Vector2i> EntityManager::findBuildSite(const Buildings::Def& def) const
{
        // The anchor (city center) seeds the search; before it exists, the
        // settlement spawns its first building near where the founders stand.
        const sf::Vector2i center = m_anchor.value_or(findHabitableSpawn());
        const int ts = m_map->getTileSize();
        const sf::Vector2i centerTile = CoordMath::worldToTile(center, ts);
        const int radius = std::max(1, m_settlement.build_radius_tiles);

        // Start from a random point near the centre, not the centre itself: the
        // outward scan then meets its first free tile in a random direction and at
        // a random standoff, so successive sites scatter instead of filling the
        // same tidy ring at one fixed distance.
        const int jitter = std::max(1, radius / 6);
        const sf::Vector2i seed = centerTile + sf::Vector2i{ Random::get(-jitter, jitter),
                                                            Random::get(-jitter, jitter) };

        // Radius-limited flood fill outward from the seed. It returns the nearest
        // free tile in whatever direction the seed fell, so two sites are not the
        // same standoff distance from the centre in a tidy ring.
        // Bound the fill to the whole radius window, so a valid tile is never
        // missed just because the frontier grew large before reaching it.
        const int window = 2 * radius + 1;
        const int maxVisit = window * window;
        std::vector<sf::Vector2i> frontier{ seed };
        std::vector<sf::Vector2i> visited{ seed };

        for (std::size_t i = 0; i < frontier.size() && static_cast<int>(i) < maxVisit; ++i)
        {
                const sf::Vector2i tile = frontier[i];
                if (std::max(std::abs(tile.x - centerTile.x), std::abs(tile.y - centerTile.y)) > radius)
                        continue;

                const sf::Vector2i world = CoordMath::tileToWorld(tile, ts);
                if (canBuildOn(def, world))
                        return world;

                for (const sf::Vector2i step : { sf::Vector2i{ 1, 0 }, sf::Vector2i{ -1, 0 },
                                                 sf::Vector2i{ 0, 1 }, sf::Vector2i{ 0, -1 } })
                {
                        const sf::Vector2i next = tile + step;
                        if (std::find(visited.begin(), visited.end(), next) == visited.end())
                        {
                                visited.push_back(next);
                                frontier.push_back(next);
                        }
                }
        }

        return std::nullopt;
}

bool EntityManager::canBuildOn(const Buildings::Def& def, const sf::Vector2i& worldPos) const
{
        const int ts = m_map->getTileSize();

        // Only dry land suits a building: no ocean, no lake and no beach sand.
        // The def may narrow this further (a future mine that must sit on a
        // mountain), so both gates must pass.
        const Elements element = m_map->getElementAtWorld(worldPos);
        if (!Buildings::defaultBuildable(element) || !def.allowsTerrain(element))
                return false;

        // Leave a gap between buildings so the settlement does not fuse into one
        // solid block. Measured in tiles with the Chebyshev metric, so diagonal
        // neighbours are separated too. Roads are exempt: a road's whole purpose
        // is to touch the buildings it connects.
        if (!Resources::isRoad(def.element))
        {
                const int minSpacing = std::max(1, m_settlement.min_spacing_tiles);
                const sf::Vector2i tile = CoordMath::worldToTile(worldPos, ts);
                for (const auto& building : m_buildings)
                {
                        const sf::Vector2i otherTile = CoordMath::worldToTile(building.origin, ts);
                        if (std::max(std::abs(tile.x - otherTile.x), std::abs(tile.y - otherTile.y)) < minSpacing)
                                return false;
                }
        }
        return true;
}

void EntityManager::planConstruction()
{
        const std::int64_t hour = m_game_clock->getTimestamp() / GameTime::kMinutesPerHour;
        if (hour == m_last_plan_hour)
                return;
        m_last_plan_hour = hour;

        // Do not over-build: cap the number of sites under construction at once.
        int pending = 0;
        for (const auto& building : m_buildings)
                if (!building.complete)
                        ++pending;
        if (pending >= m_settlement.max_concurrent_sites)
                return;

        // The people the settlement could house right now (its base cap plus every
        // completed building) and the heads it must house. The gap decides whether
        // another house is worth its wood. Food is sized to the people actually
        // present, since they are the ones that eat: one farm's rolled reach
        // already covers a small, growing settlement, and more are added only as
        // the population outgrows what the built farms claim to feed.
        const int capacity = populationCapacity();
        const int heads = entityCount();

        const Buildings::Def* chosen = nullptr;
        if (!hasCityCenter() && countOf(m_catalog.indexOf(m_catalog.byId("city_center"))) == 0)
                chosen = m_catalog.byId("city_center");

        if (!chosen)
        {
                // Count the placed housing (started or done) so several housing
                // defs share one budget; sum the reach of every *completed* food
                // producer, and note whether one is already under construction.
                // The anchor's beds are already in `capacity`, so it is not counted
                // here (else the city center would satisfy the housing demand on
                // its own and no house would ever be raised). Counting completed
                // reach means the planner raises another farm only while the ones
                // actually standing do not feed the people, and the one-pending cap
                // stops it from queuing several farms that would all be redundant
                // once the first completes.
                int placedHousing = 0;
                int completedFeeds = 0;
                int pendingProducers = 0;
                for (const auto& building : m_buildings)
                {
                        const auto& def = m_catalog.all()[building.def_index];
                        if (def.population_capacity > 0 && !def.is_anchor)
                                ++placedHousing;
                        if (def.feeds_population > 0)
                        {
                                if (building.complete)
                                        completedFeeds += building.rolled_value;
                                else
                                        ++pendingProducers;
                        }
                }

                for (const auto& def : m_catalog.all())
                {
                        if (def.is_anchor)
                                continue; // handled above
                        const std::size_t index = m_catalog.indexOf(&def);
                        if (def.max_count > 0 && countOf(index) >= def.max_count)
                                continue;
                        if (def.costs.empty() && def.recipes.empty())
                                continue; // nothing to build toward (a pure anchor)
                        // Finish the anchor before raising houses: until it is up the
                        // people have no bed, so a house would claim the wood the
                        // settlement needs for the shelter (and its first food).
                        if (!hasCityCenter() && def.population_capacity > 0)
                                continue;
                        // Do not keep raising houses the settlement does not need: a
                        // def is housing when it adds population capacity, and the
                        // planner stops once the beds cover the people (plus a small
                        // buffer). A new housing def is recognized by its own
                        // `population_capacity`, so this stays data-only.
                        if (def.population_capacity > 0
                                && placedHousing >= Buildings::housesWanted(heads, capacity, def.population_capacity))
                                continue;
                        // Likewise for food: raise a farm only while the completed
                        // producers do not feed the people *and* the store is not
                        // already comfortable, but never have more than one under
                        // construction at a time, so a small settlement keeps a
                        // single farm while it grows and stops once the granary
                        // covers the settlement for the configured reserve.
                        if (def.feeds_population > 0
                                && (pendingProducers >= 1
                                        || !Buildings::needsFoodProducer(
                                                heads, completedFeeds,
                                                m_goods.count(Goods::Good::Food),
                                                m_config.survival.food_per_person_per_day,
                                                m_config.economy.food_reserve_days)))
                                continue;
                        if (!Buildings::affordable(m_goods, def))
                                continue;
                        if (!chosen || def.priority < chosen->priority)
                                chosen = &def;
                }
        }

        if (!chosen)
                return;

        const auto site = findBuildSite(*chosen);
        if (!site)
                return;

        if (!placeBuilding(chosen->id, *site))
                return;

        m_events.record(Observability::EventKind::Construction,
                        m_game_clock->getTimestamp(), 1, chosen->name);
}

bool EntityManager::placeBuilding(const std::string& building_id, const sf::Vector2i& worldPos)
{
        const Buildings::Def* def = m_catalog.byId(building_id);
        if (!def)
                return false;

        // Do not stack a new structure on an existing one, and keep the one rule
        // for placement (dry land the def allows, minimum gap) in charge here too,
        // so a manual or test placement cannot break the layout rules the planner
        // follows. Terrain is checked before cost: an invalid tile is refused
        // whether or not the stock could pay for it.
        if (findBuildingAt(worldPos))
                return false;

        if (!canBuildOn(*def, worldPos))
                return false;

        if (!Buildings::affordable(m_goods, *def))
                return false;

        if (!m_map->setTileColor(worldPos, def->element))
                return false;

        Buildings::spend(m_goods, *def);
        m_buildings.push_back(PlacedBuilding{
                m_catalog.indexOf(def), worldPos,
                std::max(1, def->build_hours) * std::max(1, def->work_per_hour), false,
                rollBuildValue(*def), 0 });
        LOG_INFO("Construction started: {} at ({},{}).", def->name, worldPos.x, worldPos.y);
        return true;
}

bool EntityManager::completeBuilding(const sf::Vector2i& worldPos)
{
        if (PlacedBuilding* site = findBuildingAt(worldPos))
        {
                applyBuildEffect(*site);
                return true;
        }
        return false;
}

int EntityManager::stockpile(const Elements element) const
{
        return m_goods.count(Goods::fromElement(element));
}

int EntityManager::totalStockpile() const
{
        return m_goods.total();
}

// Population dynamics ////////////////////////////////////////////////////////////

void EntityManager::decayNeeds(const std::int64_t hourIndex)
{
        m_registry->view<CBasicNeeds>().each([&](auto, CBasicNeeds& needs)
        {
                if (needs.last_update == hourIndex)
                        return;

                needs.applyHourlyDecay(m_config.sleep_gain_per_hour);
                needs.last_update = static_cast<int>(hourIndex);
        });
}

void EntityManager::ageEntities()
{
        m_registry->view<CLifespan>().each([&](auto entity, CLifespan& life)
        {
                --life.remaining;
        });
}

void EntityManager::applyHealth()
{
        const auto& survival = m_config.survival;

        m_registry->view<CBasicNeeds, CHealth>().each([&](auto, CBasicNeeds& needs, CHealth& health)
        {
                const int delta = EntityVitals::healthChange(needs,
                                                             survival.lethal_days_without_food,
                                                             survival.starvation_damage_per_hour,
                                                             survival.health_regen_per_hour);
                if (delta > 0)
                        health.heal(delta);
                else if (delta < 0)
                        health.damage(-delta);
        });
}

void EntityManager::consumeFoodDaily()
{
        const auto& survival = m_config.survival;

        // The settlement eats from its store: one food per person per day. When
        // the store runs short the food is rationed entity by entity, so the ones
        // that go without each accrue a day of hunger.
        int available = m_goods.count(Goods::Good::Food);
        const int perPerson = std::max(0, survival.food_per_person_per_day);
        int fed = 0;

        m_registry->view<CBasicNeeds>().each([&](auto, CBasicNeeds& needs)
        {
                if (perPerson == 0 || available >= perPerson)
                {
                        available -= perPerson;
                        needs.days_without_food = 0;
                        ++fed;
                }
                else
                {
                        ++needs.days_without_food;
                }
        });

        // Draw what was actually eaten out of the store in one write.
        const int eaten = fed * perPerson;
        if (eaten > 0)
                m_goods.take(Goods::Good::Food, eaten);
}

void EntityManager::killTheDying()
{
        // Mark the dead; the removal pass below destroys them and tallies the
        // deaths. Marking is idempotent and deliberately does not count here: a
        // single frame can step several hours, so counting per hour-step would
        // charge one entity's death to every hour it was already marked. The
        // cause is decided here (needs are still readable) and carried to the
        // removal pass that records the event.
        m_registry->view<CLifespan, CHealth, CBasicNeeds>()
                .each([&](auto entity, CLifespan& life, CHealth& health, const CBasicNeeds& needs)
        {
                if (!EntityVitals::isAged(life) && health.isAlive())
                        return;

                const bool aged = EntityVitals::isAged(life);
                life.remaining = 0;
                if (m_death_cause.find(entity) != m_death_cause.end())
                        return;

                Observability::EventCause cause = Observability::EventCause::Natural;
                if (aged && !EntityVitals::isStarving(needs, m_config.survival.lethal_days_without_food))
                        cause = Observability::EventCause::Aged;
                else if (EntityVitals::isStarving(needs, m_config.survival.lethal_days_without_food))
                        cause = Observability::EventCause::Starved;
                m_death_cause[entity] = cause;
        });
}

void EntityManager::tryBirths()
{
        const auto& survival = m_config.survival;

        const int population = static_cast<int>(m_registry->view<CType>().size());
        if (population == 0)
                return;

        const int capacity = populationCapacity();

        // Every entity carries its own reproduction timer, so growth scales with
        // the number of comfortable adults instead of one settlement-wide
        // cooldown. Collect the births first: creating entities mid-view would
        // invalidate the iteration.
        std::vector<sf::Vector2i> spawns;

        m_registry->view<CReproduction, CBasicNeeds, CTransform>()
                .each([&](auto, CReproduction& repro, CBasicNeeds& needs, CTransform& trs)
        {
                if (repro.cooldown_hours > 0)
                {
                        --repro.cooldown_hours;
                        return;
                }

                if (population + static_cast<int>(spawns.size()) >= capacity)
                        return;

                if (EntityVitals::comfort(needs) < survival.birth_comfort)
                        return;

                repro.cooldown_hours = survival.birth_interval_hours;
                spawns.push_back(trs.pos);
        });

        for (const auto& spawn : spawns)
        {
                addEntity(EntityType::Human_Generic, spawn);
                ++m_births;
                m_events.record(Observability::EventKind::Birth, m_game_clock->getTimestamp(), 1);
        }

        if (!spawns.empty())
        {
                LOG_INFO("Population: {} born (population now {}).",
                         spawns.size(), population + static_cast<int>(spawns.size()));
        }
}

void EntityManager::render(sf::RenderTarget& window)
{
        // ENTITIES
        m_registry->view<CShape, CTransform, CVision>().each([&](auto entity, auto& shape, auto& trs, auto& vsn)
        {
                shape.circle.setPosition(static_cast<sf::Vector2f>(trs.pos));
                window.draw(shape.circle);

                // THIS IS JUST FOR TESTING AND NEEDS TO BE IMPROVED
                if (show_vision)
                {
                        sf::CircleShape circle(vsn.radius);
                        circle.setFillColor({ 255, 255, 255, 100 });
                        circle.setPosition(static_cast<sf::Vector2f>(trs.pos));
                        circle.setOrigin({ vsn.radius, vsn.radius });
                        window.draw(circle);
                }
        });

        // INFO BOXES: only the entity under the pointer shows a readout, so the map
        // stays clear. The lines are built here (not per frame in update) so the
        // hovered entity's panel reflects the current state the moment it is drawn.
        if (!m_hover_world)
                return;

        const std::optional<entt::entity> hovered = entityAtWorld(*m_hover_world);
        if (!hovered)
                return;

        m_registry->view<CTransform, CEntityInfo>().each([&](auto entity, auto& trs, auto& info)
        {
                if (entity != *hovered)
                        return;

                const CBasicNeeds* needs = m_registry->try_get<CBasicNeeds>(entity);
                const CHealth* health = m_registry->try_get<CHealth>(entity);
                const CJob* job = m_registry->try_get<CJob>(entity);
                const CActionsQueue* queue = m_registry->try_get<CActionsQueue>(entity);

                static const std::array<std::pair<ActionTypes, const char*>, 5> kActions{ {
                        { ActionTypes::Moving,    "Moving." },
                        { ActionTypes::Sleeping,  "Sleeping." },
                        { ActionTypes::Gathering, "Gathering." },
                        { ActionTypes::Building,  "Building." },
                        { ActionTypes::Idle,      "Idle." },
                } };

                std::string status = "Idle.";
                if (queue && !queue->actions.empty() && queue->actions.front())
                {
                        for (const auto& [action, label] : kActions)
                                if (queue->actions.front()->action_name == action)
                                        status = label;
                }

                const std::vector<std::string> lines{
                        "Sleep: " + std::to_string(needs ? needs->sleep : 0),
                        "Health: " + std::to_string(health ? health->value : 0),
                        "Hungry: " + std::to_string(needs ? needs->days_without_food : 0) + "d",
                        "Job: " + Jobs::name(job ? job->job : Jobs::Job::Idle),
                        status,
                };

                if (info.text.size() != lines.size())
                {
                        info.text.clear();
                        for (const auto& line : lines)
                                addTextToEntityInfo(info.text, std::string(line), info.size, info.text_color);
                }
                else
                {
                        for (std::size_t i = 0; i < lines.size(); ++i)
                                info.text[i].setString(lines[i]);
                }

                const float padding    = 5.f;
                const float lineHeight = static_cast<float>(info.size) + 2.f;

                float boxWidth = 0.f;
                for (auto& text : info.text)
                        boxWidth = std::max(boxWidth, text.getLocalBounds().size.x);

                const float boxHeight = lineHeight * static_cast<float>(info.text.size()) + padding * 2;

                info.shape.setSize(sf::Vector2f{ boxWidth + padding * 2, boxHeight });
                info.shape.setPosition(sf::Vector2f{
                        trs.pos.x - info.shape.getSize().x / 2.f,
                        trs.pos.y - info.shape.getSize().y - padding
                        });

                window.draw(info.shape);

                int i = 0;
                for (auto& text : info.text)
                {
                        text.setPosition(sf::Vector2f{
                                info.shape.getPosition().x + padding,
                                info.shape.getPosition().y + padding + i * lineHeight
                                });
                        window.draw(text);
                        ++i;
                }
        });
}

/// MANAGING ENTITIES //////////////////////////////////////////////////////////////

entt::entity EntityManager::addEntity(const EntityType& type, const sf::Vector2i& spawn)
{
        auto entity = m_registry->create();

        const Jobs::Job job = defaultJobFor(type);

        m_registry->emplace<CType>(entity, type);
        m_registry->emplace<CJob>(entity, job);
        m_registry->emplace<CLifespan>(entity, m_config.survival.lifespan_hours);
        m_registry->emplace<CTransform>(entity, spawn, 100.f);
        m_registry->emplace<CShape>(entity, lookFor(type, job));
        m_registry->emplace<CVision>(entity);
        m_registry->emplace<CKnowledgeScan>(entity);
        m_registry->emplace<CBasicNeeds>(entity);
        m_registry->emplace<CHealth>(entity);
        m_registry->emplace<CPersonality>(entity);
        m_registry->emplace<CActionsQueue>(entity);
        m_registry->emplace<CPath>(entity);
        m_registry->emplace<CInventory>(entity);
        m_registry->emplace<CEntityInfo>(entity, 60, 40);
        // A newborn (or a fresh founder) waits a full interval before its first
        // child, so reproduction is paced rather than immediate.
        m_registry->emplace<CReproduction>(entity, m_config.survival.birth_interval_hours);

        m_entity_idle[entity] = 0;
        return entity;
}

void EntityManager::seedPopulation()
{
        seedPopulation(m_config.survival.initial_population);
}

void EntityManager::seedPopulation(const int count)
{
        if (m_seeded_population)
                return;

        m_seeded_population = true;
        const sf::Vector2i spawn = findHabitableSpawn();
        const int founders = std::max(0, count);
        LOG_INFO("Seeding population of {} at ({},{}).", founders, spawn.x, spawn.y);
        for (int i = 0; i < founders; ++i)
        {
                const entt::entity entity = addEntity(EntityType::Human_Generic, spawn);

                // Spread the founders across a range of ages and reproduction
                // timers so the settlement does not age or breed in lockstep: the
                // first dies at 40% of a lifespan, the last at 100%.
                auto& life = m_registry->get<CLifespan>(entity);
                life.remaining = m_config.survival.lifespan_hours * (4 + 6 * i / std::max(1, founders)) / 10;

                auto& repro = m_registry->get<CReproduction>(entity);
                repro.cooldown_hours = m_config.survival.birth_interval_hours * (i + 1) / std::max(1, founders);
        }
}

sf::Vector2i EntityManager::findHabitableSpawn() const
{
        // The world origin is inland on most seeds, far from any water, so a
        // settlement founded there starves before it can find a drink. Find the
        // nearest coast, then a land tile with both water and forage inside an
        // entity's vision, so the settlement is self-sufficient.
        const int tileSize = m_map->getTileSize();
        const float reach = CVision{}.radius;
        const int reachTiles = static_cast<int>(reach / static_cast<float>(tileSize));

        // Cache tile elements: the search touches the same neighbourhood many
        // times and every uncached read locks the chunk map.
        std::unordered_map<std::uint64_t, Elements> cache;
        const auto elementAt = [&](const int tx, const int ty)
        {
                // Combine the two signed tile coordinates into one key. Build it
                // unsigned: shifting a negative signed value left is undefined
                // behaviour (UBSan flags it). The high 32 bits hold tx, the low 32
                // hold ty, both via their bit pattern.
                const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tx)) << 32)
                                        | static_cast<std::uint32_t>(ty);
                const auto it = cache.find(key);
                if (it != cache.end())
                        return it->second;
                const Elements e = m_map->getElementAtWorld(sf::Vector2i(tx * tileSize, ty * tileSize));
                cache.emplace(key, e);
                return e;
        };

        // Nearest land tile to the origin that has a gatherable (forest for wood,
        // hill for stone) within an entity's vision. Wood gates the first farm, so
        // a settlement founded away from any gatherable would starve before it
        // could build. Falls back to the nearest land tile when none qualifies, so
        // the search always returns a walkable spot.
        const auto gatherableWithinReach = [&](const int cx, const int cy)
        {
                for (int ox = -reachTiles; ox <= reachTiles; ++ox)
                        for (int oy = -reachTiles; oy <= reachTiles; ++oy)
                        {
                                if (std::hypot(static_cast<float>(ox), static_cast<float>(oy)) * tileSize > reach)
                                        continue;
                                if (Resources::isGatherable(elementAt(cx + ox, cy + oy)))
                                        return true;
                        }
                return false;
        };

        sf::Vector2i bestLand{ 0, 0 };
        float bestLandDist = 0.f;
        sf::Vector2i bestWorkLand{ 0, 0 };
        float bestWorkDist = 0.f;
        constexpr int kMaxRing = 256;
        for (int ring = 0; ring <= kMaxRing; ++ring)
        {
                for (int dx = -ring; dx <= ring; ++dx)
                        for (int dy = -ring; dy <= ring; ++dy)
                        {
                                if (std::max(std::abs(dx), std::abs(dy)) != ring)
                                        continue;
                                const float dist = std::hypot(static_cast<float>(dx), static_cast<float>(dy));
                                if (Resources::isOcean(elementAt(dx, dy)))
                                        continue;

                                if (bestLandDist == 0.f)
                                {
                                        bestLandDist = dist;
                                        bestLand = sf::Vector2i{ dx, dy };
                                }
                                if (bestWorkDist == 0.f && gatherableWithinReach(dx, dy))
                                {
                                        bestWorkDist = dist;
                                        bestWorkLand = sf::Vector2i{ dx, dy };
                                }
                        }
                if (bestWorkDist > 0.f)
                        break;
        }

        const sf::Vector2i chosen = (bestWorkDist > 0.f) ? bestWorkLand
                                 : (bestLandDist > 0.f) ? bestLand : sf::Vector2i{ 0, 0 };
        return sf::Vector2i{ chosen.x * tileSize, chosen.y * tileSize };
}

Jobs::Job EntityManager::defaultJobFor(const EntityType& type) const
{
        const std::size_t index = static_cast<std::size_t>(type);
        if (index < kEntityTypeCount && m_config.has_type_job[index])
                return m_config.type_jobs[index];

        // Fall back by species: humans build, animals idle. A type the config did
        // not mention still gets a useful role.
        switch (type)
        {
                case EntityType::Animal_Dog:
                case EntityType::Animal_Cat:
                        return Jobs::Job::Idle;
                default:
                        return Jobs::Job::Builder;
        }
}

Appearance::Look EntityManager::lookFor(const EntityType& type, const Jobs::Job job) const
{
        // A configured job look personalizes the profession and wins; otherwise
        // the type's look applies; otherwise the default (white 10-unit circle).
        if (job != Jobs::Job::Idle && m_config.has_job_look[Jobs::index(job)])
                return m_config.job_looks[Jobs::index(job)];
        return m_config.type_looks[static_cast<std::size_t>(type)];
}

// HELPER FUNCTION
void EntityManager::addTextToEntityInfo(std::vector<sf::Text>& vec, std::string&& s, int size, const sf::Color& color)
{
        vec.emplace_back(m_font, s);
        vec.back().setCharacterSize(size);
        vec.back().setFillColor(color);
}

std::optional<entt::entity> EntityManager::entityAtWorld(const sf::Vector2i& worldPos) const
{
        // A generous grab radius: the drawn body is small (a ~10-unit circle), so a
        // pixel-exact hit would be hard to land with the mouse. `kHoverRadius` is
        // the pick radius in world units, kept a little larger than the body.
        constexpr float kHoverRadius{ 14.f };
        const float limit2 = kHoverRadius * kHoverRadius;

        std::optional<entt::entity> hit;
        float best2 = limit2;
        m_registry->view<CTransform>().each([&](auto entity, const CTransform& trs)
        {
                const float dx = static_cast<float>(trs.pos.x - worldPos.x);
                const float dy = static_cast<float>(trs.pos.y - worldPos.y);
                const float d2 = dx * dx + dy * dy;
                if (d2 <= best2)
                {
                        best2 = d2;
                        hit = entity;
                }
        });
        return hit;
}

/// GETTERS ////////////////////////////////////////////////////////////////////////

std::optional<CBasicNeeds> EntityManager::firstNeeds() const
{
        auto view = m_registry->view<CBasicNeeds>();
        if (view.begin() == view.end())
                return std::nullopt;
        return view.get<CBasicNeeds>(*view.begin());
}

std::optional<int> EntityManager::firstHealth() const
{
        auto view = m_registry->view<CHealth>();
        if (view.begin() == view.end())
                return std::nullopt;
        return view.get<CHealth>(*view.begin()).value;
}

std::optional<ActionTypes> EntityManager::firstAction() const
{
        auto view = m_registry->view<CActionsQueue>();
        if (view.begin() == view.end())
                return std::nullopt;

        const auto& queue = view.get<CActionsQueue>(*view.begin());
        if (queue.actions.empty() || !queue.actions.front())
                return std::nullopt;
        return queue.actions.front()->action_name;
}

int EntityManager::entityCount() const
{
        return static_cast<int>(m_registry->view<CType>().size());
}

std::vector<sf::Vector2i> EntityManager::entityPositions() const
{
        std::vector<sf::Vector2i> positions;
        m_registry->view<CTransform>().each([&](auto, const CTransform& trs)
        {
                positions.push_back(trs.pos);
        });
        return positions;
}

CBasicNeeds* EntityManager::needsOf(const entt::entity entity)
{
        return m_registry->valid(entity) ? m_registry->try_get<CBasicNeeds>(entity) : nullptr;
}

CActionsQueue* EntityManager::actionsOf(const entt::entity entity)
{
        return m_registry->valid(entity) ? m_registry->try_get<CActionsQueue>(entity) : nullptr;
}

const CActionsQueue* EntityManager::actionsOf(const entt::entity entity) const
{
        return m_registry->valid(entity) ? m_registry->try_get<CActionsQueue>(entity) : nullptr;
}

std::vector<entt::entity> EntityManager::entityHandles() const
{
        std::vector<entt::entity> handles;
        m_registry->view<CType>().each([&](auto entity, const CType&)
        {
                handles.push_back(entity);
        });
        return handles;
}

std::optional<EntityManager::WorkTarget> EntityManager::settleElements(const CivKnowledge& knowledge, const Jobs::Job job, const sf::Vector2i& from) const
{
        // Gather the known tiles, then pick the best one for *this job*: lowest
        // preference rank wins (see Goods::jobPreference), and ties go to the
        // closest tile. Idle entities fall back to rarest-material-first so they
        // still contribute. The knowledge is the settlement's, so an entity may
        // work a resource another one found.
        std::optional<WorkTarget> best;
        int bestRank = 0;
        int bestDist = 0;

        const auto consider = [&](const Elements element, const sf::Vector2i& pos, const int rank)
        {
                if (rank < 0)
                        return;
                const int dist = squaredDistance(pos, from);
                if (!best || rank < bestRank || (rank == bestRank && dist < bestDist))
                {
                        best = WorkTarget{ pos, element };
                        bestRank = rank;
                        bestDist = dist;
                }
        };

        if (job == Jobs::Job::Idle)
        {
                static constexpr std::array<Elements, 5> kPriority{
                        Elements::silver, Elements::iron, Elements::clay,
                        Elements::forest, Elements::hill,
                };
                for (std::size_t i = 0; i < kPriority.size(); ++i)
                {
                        const auto pos = knowledge.location(kPriority[i], from);
                        if (pos)
                                return WorkTarget{ *pos, kPriority[i] };
                }
                return std::nullopt;
        }

        for (const auto& [element, positions] : knowledge.tiles())
                for (const auto& pos : positions)
                        consider(element, pos, Goods::jobPreference(job, element));

        return best;
}

std::array<int, Jobs::kJobCount> EntityManager::jobCounts() const
{
        std::array<int, Jobs::kJobCount> counts{};
        m_registry->view<CJob>().each([&](auto, const CJob& job)
        {
                counts[Jobs::index(job.job)] += 1;
        });
        return counts;
}

Jobs::Job EntityManager::firstJob() const
{
        auto view = m_registry->view<CJob>();
        if (view.begin() == view.end())
                return Jobs::Job::Idle;
        return view.get<CJob>(*view.begin()).job;
}

void EntityManager::reassignJobs()
{
        // Only entities that have no plan are candidates, so a worker mid-trip is
        // not yanked off it. The most understaffed job wins each free entity.
        const auto counts = jobCounts();
        std::array<int, Jobs::kJobCount> projected = counts;

        m_registry->view<CJob, CActionsQueue, CType, CShape>().each(
                [&](auto, CJob& job, const CActionsQueue& queue, const CType& type, CShape& shape)
        {
                if (!queue.actions.empty())
                        return;

                const Jobs::Job want = Economy::assignJob(m_config.economy.job_targets, projected);
                if (want != Jobs::Job::Idle && want != job.job)
                {
                        projected[Jobs::index(job.job)] -= 1;
                        projected[Jobs::index(want)] += 1;
                        job.job = want;
                        // Repaint the entity so its look follows its profession.
                        shape.circle = lookFor(type.type, want).makeShape();
                }
        });
}
