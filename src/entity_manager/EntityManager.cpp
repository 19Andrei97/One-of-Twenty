
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
} // namespace

void EntityManager::update()
{
        const std::int64_t now = m_game_clock->getTimestamp();
        const std::int64_t hourIndex = now / 60;

        if (m_last_survival_tick < 0)
                m_last_survival_tick = hourIndex * 60 - 60; // run the first hour too

        // The clock can advance several in-game hours in a single frame (and wraps
        // at midnight), so step hour by hour instead of watching the hour-of-day,
        // which would skip most hours and never fire on a wrap.
        for (std::int64_t hour = m_last_survival_tick / 60 + 1; hour <= hourIndex; ++hour)
        {
                const int hourOfDay = static_cast<int>(hour % 24);
                decayNeeds(hour);
                ageEntities();
                killTheDying(hourOfDay);
                tryBirths(hour);
        }
        m_last_survival_tick = now;

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
                m_registry->destroy(entity);
                m_entity_idle.erase(entity);
        }

        // UPDATE ENTITIES

        // Update Memory (candidate for another thread?)
        m_registry->view<CTransform, CMemory, CVision>().each([&](auto entity, auto& trs, auto& memory, auto& vision)
        {
                memory.rememberLocation(m_map->getResourcesWithinBoundary(trs.pos, vision.radius));
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

                // The point to walk toward: the next waypoint when routing, else the
                // action's target (straight-line fallback).
                const auto stepToward = [&](const sf::Vector2i& goal)
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
                        const float cost{ m_map->getTileCost(trs.pos) };
                        const float step = std::min(trs.speed * m_delta_time * cost, distance);
                        if (step >= distance)
                        {
                                trs.pos = goal;
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
                        return false;
                };

                // Prefer the routed waypoint. If the waypoint is reached, advance the
                // path; when the final waypoint is reached the entity stands on the
                // target tile and the move action is done.
                if (!path.empty())
                {
                        if (stepToward(path.current()))
                        {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                path.advance();
                                if (path.empty())
                                        queue.actions.pop_front();
                        }
                        return;
                }

                // No route yet (or one could not be found): walk straight at the
                // target. This is also the path for a target on the entity's own tile.
                if (stepToward(action->target))
                {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
        });

        // Finish actions that have run their course, restoring the need they serve
        // or banking what a gather produced.
        m_registry->view<CActionsQueue, CBasicNeeds, CInventory>().each([&](auto entity, auto& queue, auto& needs, auto& inventory)
        {
                if (queue.actions.empty())
                        return;

                const auto& front = queue.actions.front();
                const std::int64_t elapsed = m_game_clock->getTimestamp();

                if (auto action = std::dynamic_pointer_cast<CEating>(front); action
                        && elapsed - action->timestamp_min > action->duration_min)
                {
                        // Prefer drawing from the settlement stores; when they are
                        // empty the entity foraged the tile directly, so the meal
                        // still satisfies hunger.
                        consumeFromStockpile(Elements::forest) || consumeFromStockpile(Elements::hill);
                        needs.satisfy(1);
                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
                else if (auto action = std::dynamic_pointer_cast<CDrinking>(front); action
                        && elapsed - action->timestamp_min > action->duration_min)
                {
                        consumeFromStockpile(Elements::ocean);
                        needs.satisfy(0);
                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
                else if (auto action = std::dynamic_pointer_cast<CSleeping>(front); action
                        && elapsed - action->timestamp_min > action->duration_min)
                {
                        needs.satisfy(2);
                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
                else if (auto action = std::dynamic_pointer_cast<CGather>(front); action
                        && elapsed - action->timestamp_min > action->duration_min)
                {
                        // The gather produced a unit: carry it, then deposit it into
                        // the settlement stockpile and tally the completed trip.
                        inventory.gather();
                        m_stockpile[action->element] += inventory.deposit();
                        ++m_gathers_completed;

                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
        });

        // Decide what each entity should do. A busy entity keeps its plan; an idle
        // one asks the weighted policy and starts the winning action.
        m_registry->view<CActionsQueue, CTransform, CBasicNeeds, CMemory, CPersonality, CVision, CPath>()
                .each([&](auto entity, auto& queue, auto& trs, auto& needs, auto& memory, auto& personality, auto& vision, auto& path)
        {
                int& idle = m_entity_idle[entity];

                if (!queue.actions.empty())
                {
                        idle = 0;
                        return;
                }

                const EntityDecision::Need need =
                        EntityDecision::decide(needs, personality, m_config.decision, idle);

                if (need == EntityDecision::Need::None)
                {
                        ++idle;
                        return;
                }

                if (startActionFor(need, trs.pos, vision.radius, memory, queue, path))
                        idle = 0;
        });

        // Update Entity info box
        m_registry->view<CActionsQueue, CBasicNeeds, CEntityInfo>().each([&](auto entity, auto& queue, auto& needs, auto& info)
        {
                        if (info.text.empty())
                        {
                                // Hunger
                                addTextToEntityInfo(info.text, "Hunger: " + std::to_string(needs.hunger), info.size, info.text_color);

                                // Thirst
                                addTextToEntityInfo(info.text, "Thirst: " + std::to_string(needs.thirst), info.size, info.text_color);

                                // Sleep
                                addTextToEntityInfo(info.text, "Sleep: " + std::to_string(needs.sleep), info.size, info.text_color);
                        }
                        else
                        {
                                info.text[0].setString("Hunger: " + std::to_string(needs.hunger));
                                info.text[1].setString("Thirst: " + std::to_string(needs.thirst));
                                info.text[2].setString("Sleep: " + std::to_string(needs.sleep));

                                // Update current action
                                if(info.text.size() < 4)
                                        addTextToEntityInfo(info.text, "Idle.", info.size, info.text_color);

                                if (queue.actions.empty())
                                {
                                        info.text[3].setString("Idle.");
                                        return;
                                }

                                switch (static_cast<int>(queue.actions.front()->action_name))
                                {
                                case static_cast<int>(ActionTypes::Moving):
                                        info.text[3].setString("Moving.");
                                        break;
                                case static_cast<int>(ActionTypes::Eating):
                                        info.text[3].setString("Eating.");
                                        break;
                                case static_cast<int>(ActionTypes::Sleeping):
                                        info.text[3].setString("Sleeping.");
                                        break;
                                case static_cast<int>(ActionTypes::Drinking):
                                        info.text[3].setString("Drinking.");
                                        break;
                                case static_cast<int>(ActionTypes::Gathering):
                                        info.text[3].setString("Gathering.");
                                        break;

                                default:
                                        info.text[3].setString("Idle.");
                                }
                        }
        });

}

// Translate a decision into queued actions. A need with a remembered target
// walks there first (routed around water); a need with no memory (or a wander
// decision) walks to a random land tile so the entity explores and refreshes its
// memory. Targets that no land route reaches fall back to exploring.
bool EntityManager::startActionFor(const EntityDecision::Need need,
                                   const sf::Vector2i& pos,
                                   const float visionRadius,
                                   const CMemory& memory,
                                   CActionsQueue& queue,
                                   CPath& path)
{
        // Water is not walkable, so a drink target is approached from the nearest
        // land tile. The closest water tile can itself be ringed by ocean, so
        // expand outward instead of only checking its 8 neighbours, otherwise the
        // route fails and the entity wanders instead of drinking.
        const auto approachLand = [&](const sf::Vector2i& target) -> sf::Vector2i
        {
                if (!Resources::isOcean(m_map->getElementAtWorld(target)))
                        return target;
                const int ts = m_map->getTileSize();
                const sf::Vector2i targetTile = CoordMath::worldToTile(target, ts);
                for (int ring = 1; ring <= 6; ++ring)
                {
                        for (int dx = -ring; dx <= ring; ++dx)
                                for (int dy = -ring; dy <= ring; ++dy)
                                {
                                        if (std::max(std::abs(dx), std::abs(dy)) != ring)
                                                continue;
                                        const sf::Vector2i candidate =
                                                CoordMath::tileToWorld(targetTile + sf::Vector2i{ dx, dy }, ts);
                                        if (!Resources::isOcean(m_map->getElementAtWorld(candidate)))
                                                return candidate;
                                }
                }
                return target;
        };

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
                case EntityDecision::Need::Thirst:
                {
                        if (queueHasAction(queue, ActionTypes::Drinking))
                                return false;

                        if (auto target = memory.findNearest(pos, MemoryKind::Water))
                        {
                                if (queueMoveTo(pos, approachLand(*target), path, queue))
                                {
                                        queue.actions.push_back(std::make_shared<CDrinking>(ActionTypes::Drinking, m_game_clock->getTimestamp()));
                                        return true;
                                }
                        }
                        // Nothing drinkable is reachable: go look for some.
                        return queueExplore();
                }

                case EntityDecision::Need::Hunger:
                {
                        if (queueHasAction(queue, ActionTypes::Eating))
                                return false;

                        if (auto target = memory.findNearest(pos, MemoryKind::Food))
                        {
                                if (queueMoveTo(pos, *target, path, queue))
                                {
                                        queue.actions.push_back(std::make_shared<CEating>(ActionTypes::Eating, m_game_clock->getTimestamp()));
                                        return true;
                                }
                        }
                        return queueExplore();
                }

                case EntityDecision::Need::Sleep:
                {
                        if (queueHasAction(queue, ActionTypes::Sleeping))
                                return false;

                        queue.actions.push_back(std::make_shared<CSleeping>(ActionTypes::Sleeping, m_game_clock->getTimestamp()));
                        return true;
                }

                case EntityDecision::Need::Work:
                {
                        if (queueHasAction(queue, ActionTypes::Gathering))
                                return false;

                        if (auto target = settleElements(memory))
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

        m_map->copyTileBlock(topLeft, side, m_tile_block);
        const auto& block = m_tile_block;
        // Outside the copied window is unknown; treat it as impassable so the
        // search cannot wander off the snapshot.
        const auto elementAt = [&](const sf::Vector2i& tile) -> Elements
        {
                const auto it = block.find(tile - topLeft);
                return (it != block.end()) ? it->second : Elements::very_deep_ocean;
        };

        const auto path = Pathfinding::findPath(
                startTile, goalTile,
                [&](const sf::Vector2i& tile) { return MoveCost::moveCost(elementAt(tile)); },
                [&](const sf::Vector2i& tile) { return !Resources::isOcean(elementAt(tile)); });
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

        // Separate overlapping entities with a symmetric push.
        constexpr int kMinSeparation = 12;
        std::vector<std::pair<entt::entity, sf::Vector2i>> positions;
        m_registry->view<CTransform>().each([&](auto entity, CTransform& trs)
        {
                positions.emplace_back(entity, trs.pos);
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
                        const float push = (kMinSeparation - dist) * 0.5f;
                        const sf::Vector2i shift{
                                static_cast<int>(std::lround(d.x / dist * push)),
                                static_cast<int>(std::lround(d.y / dist * push)) };
                        positions[i].second -= shift;
                        positions[j].second += shift;
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
        // single dot and no two entities share a position.
        static constexpr sf::Vector2i kAround[8]{ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                                                  { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
        std::vector<sf::Vector2i> placed;
        placed.reserve(positions.size());
        for (const auto& [entity, pos] : positions)
        {
                auto& trs = m_registry->get<CTransform>(entity);
                const bool duplicate = std::find(placed.begin(), placed.end(), trs.pos) != placed.end();
                if (duplicate)
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

bool EntityManager::consumeFromStockpile(Elements element)
{
        const auto it = m_stockpile.find(element);
        if (it == m_stockpile.end() || it->second <= 0)
                return false;

        --it->second;
        return true;
}

// Population dynamics ////////////////////////////////////////////////////////////

void EntityManager::decayNeeds(const std::int64_t hourIndex)
{
        m_registry->view<CBasicNeeds>().each([&](auto, CBasicNeeds& needs)
        {
                if (needs.last_update == hourIndex)
                        return;

                needs.applyHourlyDecay(m_config.thirst_decay_per_hour,
                                       m_config.hunger_decay_per_hour,
                                       m_config.sleep_gain_per_hour);
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

void EntityManager::killTheDying(const int hour)
{
        std::vector<entt::entity> dying;

        m_registry->view<CLifespan, CBasicNeeds>().each([&](auto entity, CLifespan& life, CBasicNeeds& needs)
        {
                const bool starved = EntityVitals::isStarving(needs, m_config.survival.lethal_threshold);
                const bool aged = EntityVitals::isAged(life);
                // A comfortable entity cannot die from a need that was emptied in a
                // single catch-up step; it only dies once it was already struggling.
                if ((starved && !needs.healthy()) || aged)
                {
                        life.remaining = 0; // so the existing removal pass destroys it
                        dying.push_back(entity);
                }
        });

        // Count a death once per entity: the removal pass destroys them next, so
        // this list is not revisited.
        m_deaths += static_cast<int>(dying.size());
        if (!dying.empty())
        {
                LOG_INFO("Population: {} died at hour {} (population now {}).",
                         dying.size(), hour, static_cast<int>(m_registry->view<CType>().size() - dying.size()));
        }
}

void EntityManager::tryBirths(const int hour)
{
        const auto& survival = m_config.survival;

        const int population = static_cast<int>(m_registry->view<CType>().size());
        if (population == 0 || population >= survival.max_population)
                return;

        if (hour - m_last_birth_hour < survival.birth_cooldown_hours)
                return;

        // A birth needs at least one comfortable parent; that is the "settlement
        // comfort" gate. Find the most comfortable entity and, if it clears the
        // bar, add a newborn at its position.
        entt::entity parent = entt::null;
        float best = -1.f;
        sf::Vector2i spawn{ 0, 0 };

        m_registry->view<CTransform, CBasicNeeds>().each([&](auto entity, CTransform& trs, CBasicNeeds& needs)
        {
                const float c = EntityVitals::comfort(needs);
                if (c > best)
                {
                        best = c;
                        parent = entity;
                        spawn = trs.pos;
                }
        });

        if (parent == entt::null || best < survival.birth_comfort)
                return;

        addEntity(EntityType::Human_Generic, spawn);
        ++m_births;
        m_last_birth_hour = hour;
        LOG_INFO("Population: a child was born at hour {} (population now {}).", hour, population + 1);
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

        // INFO BOXES
        m_registry->view<CTransform, CEntityInfo>().each([&](auto entity, auto& trs, auto& info)
        {
                if (info.text.empty())
                        return;

                float padding           = 5.f;
                float lineHeight        = static_cast<float>(info.size) + 2.f;
                float boxWidth          = 0.f;

                // Find the widest text line
                for (auto& text : info.text)
                        boxWidth = std::max(boxWidth, text.getLocalBounds().size.x);

                float boxHeight = lineHeight * info.text.size() + padding * 2;

                // Resize the box to fit text width + padding
                info.shape.setSize(sf::Vector2f{ boxWidth + padding * 2, boxHeight });

                // Position the box right above the entity
                info.shape.setPosition(sf::Vector2f{
                        trs.pos.x - info.shape.getSize().x / 2.f,
                        trs.pos.y - info.shape.getSize().y - padding
                        });

                window.draw(info.shape);

                // Draw each text line inside the box
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

void EntityManager::addEntity(const EntityType& type, const sf::Vector2i& spawn)
{
        auto entity = m_registry->create();

        m_registry->emplace<CType>(entity, type);
        m_registry->emplace<CLifespan>(entity, m_config.survival.lifespan_hours);
        m_registry->emplace<CTransform>(entity, spawn, 100.f);
        m_registry->emplace<CShape>(entity, 10, 4, sf::Color::White);
        m_registry->emplace<CVision>(entity);
        m_registry->emplace<CMemory>(entity);
        m_registry->emplace<CBasicNeeds>(entity);
        m_registry->emplace<CPersonality>(entity);
        m_registry->emplace<CActionsQueue>(entity);
        m_registry->emplace<CPath>(entity);
        m_registry->emplace<CInventory>(entity);
        m_registry->emplace<CEntityInfo>(entity, 60, 40);

        m_entity_idle[entity] = 0;
}

void EntityManager::seedPopulation()
{
        if (m_seeded_population)
                return;

        m_seeded_population = true;
        const sf::Vector2i spawn = findHabitableSpawn();
        LOG_INFO("Seeding population of {} at ({},{}).", m_config.survival.initial_population, spawn.x, spawn.y);
        for (int i = 0; i < m_config.survival.initial_population; ++i)
                addEntity(EntityType::Human_Generic, spawn);
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

        // Nearest water tile, expanding ring by ring. Stop at the first ring that
        // holds any water: it is the closest coast, and later rings are farther.
        std::optional<sf::Vector2i> coast;
        constexpr int kMaxRing = 256;
        for (int ring = 0; ring <= kMaxRing && !coast; ++ring)
        {
                for (int dx = -ring; dx <= ring && !coast; ++dx)
                        for (int dy = -ring; dy <= ring && !coast; ++dy)
                        {
                                if (std::max(std::abs(dx), std::abs(dy)) != ring)
                                        continue;
                                if (Resources::isWater(elementAt(dx, dy)))
                                        coast = sf::Vector2i{ dx, dy };
                        }
        }
        if (!coast)
                return { 0, 0 };

        // Near that coast, prefer the closest land tile that also has forage
        // within reach. Fall back to the closest land tile if none does, so the
        // settlement can at least drink.
        const auto foodWithinReach = [&](const int cx, const int cy)
        {
                for (int ox = -reachTiles; ox <= reachTiles; ++ox)
                        for (int oy = -reachTiles; oy <= reachTiles; ++oy)
                        {
                                if (std::hypot(static_cast<float>(ox), static_cast<float>(oy)) * tileSize > reach)
                                        continue;
                                if (Resources::isFood(elementAt(cx + ox, cy + oy)))
                                        return true;
                        }
                return false;
        };

        sf::Vector2i bestLand{ 0, 0 };
        float bestLandDist = 0.f;
        sf::Vector2i bestFoodLand{ 0, 0 };
        float bestFoodDist = 0.f;
        for (int ox = -reachTiles; ox <= reachTiles; ++ox)
        {
                for (int oy = -reachTiles; oy <= reachTiles; ++oy)
                {
                        const float dist = std::hypot(static_cast<float>(ox), static_cast<float>(oy)) * tileSize;
                        if (dist < 1.f || dist > reach - tileSize)
                                continue;
                        const int tx = coast->x + ox;
                        const int ty = coast->y + oy;
                        if (Resources::isWater(elementAt(tx, ty)))
                                continue;

                        if (bestLandDist == 0.f || dist < bestLandDist)
                        {
                                bestLandDist = dist;
                                bestLand = sf::Vector2i{ tx, ty };
                        }
                        if ((bestFoodDist == 0.f || dist < bestFoodDist) && foodWithinReach(tx, ty))
                        {
                                bestFoodDist = dist;
                                bestFoodLand = sf::Vector2i{ tx, ty };
                        }
                }
        }

        const sf::Vector2i chosen = (bestFoodDist > 0.f) ? bestFoodLand : bestLand;
        return sf::Vector2i{ chosen.x * tileSize, chosen.y * tileSize };
}

// HELPER FUNCTION
void EntityManager::addTextToEntityInfo(std::vector<sf::Text>& vec, std::string&& s, int size, const sf::Color& color)
{
        vec.emplace_back(m_font, s);
        vec.back().setCharacterSize(size);
        vec.back().setFillColor(color);
}

/// GETTERS ////////////////////////////////////////////////////////////////////////

std::optional<CBasicNeeds> EntityManager::firstNeeds() const
{
        auto view = m_registry->view<CBasicNeeds>();
        if (view.begin() == view.end())
                return std::nullopt;
        return view.get<CBasicNeeds>(*view.begin());
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

std::optional<EntityManager::WorkTarget> EntityManager::settleElements(const CMemory& memory) const
{
        // Rarer, more useful materials first, so an entity that could work any of
        // several remembered tiles prefers the scarcer one.
        static constexpr std::array<Elements, 5> kPriority{
                Elements::silver, Elements::iron, Elements::clay,
                Elements::forest, Elements::hill,
        };

        for (const Elements element : kPriority)
        {
                const auto pos = memory.getLocation(element);
                if (pos)
                        return WorkTarget{ *pos, element };
        }
        return std::nullopt;
}

int EntityManager::stockpile(const Elements element) const
{
        const auto it = m_stockpile.find(element);
        return (it != m_stockpile.end()) ? it->second : 0;
}

int EntityManager::totalStockpile() const
{
        int total = 0;
        for (const auto& [element, amount] : m_stockpile)
                total += amount;
        return total;
}
