
#include <pch.h>

#include "EntityManager.h"

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
        m_registry->view<CActionsQueue, CTransform>().each([&](auto entity, auto& queue, auto& trs)
        {
                if (queue.actions.empty())
                        return;

                if(auto action = std::dynamic_pointer_cast<CMoving>(queue.actions.front()))
                {
                        const sf::Vector2i toTarget = action->target - trs.pos;
                        const float distance = std::sqrt(static_cast<float>(toTarget.x * toTarget.x + toTarget.y * toTarget.y));

                        // Within a pixel of the target counts as arrived; snapping keeps
                        // the integer position from oscillating around it.
                        if (distance <= kArriveDistance)
                        {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                trs.pos = action->target;
                                queue.actions.pop_front();
                        }
                        else
                        {
                                // Tile cost
                                const float cost{ m_map->getTileCost(trs.pos) };

                                // Never step past the target: a frame that moves further
                                // than the remaining distance would overshoot and the
                                // entity would oscillate forever, never "arriving" to
                                // drink or eat.
                                const float step = std::min(trs.speed * m_delta_time * cost, distance);
                                if (step >= distance)
                                {
                                        std::lock_guard<std::mutex> lock(m_mutex);
                                        trs.pos = action->target;
                                        queue.actions.pop_front();
                                }
                                else
                                {
                                        sf::Vector2i delta{
                                                static_cast<int>(std::lround(toTarget.x / distance * step)),
                                                static_cast<int>(std::lround(toTarget.y / distance * step)) };
                                        // Rounding a sub-pixel step can yield (0,0); nudge one
                                        // pixel along the dominant axis so progress is guaranteed.
                                        if (delta.x == 0 && delta.y == 0)
                                        {
                                                if (std::abs(toTarget.x) >= std::abs(toTarget.y))
                                                        delta.x = (toTarget.x > 0) ? 1 : -1;
                                                else
                                                        delta.y = (toTarget.y > 0) ? 1 : -1;
                                        }

                                        std::lock_guard<std::mutex> lock(m_mutex);
                                        trs.pos += delta;
                                }
                        }
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
        m_registry->view<CActionsQueue, CTransform, CBasicNeeds, CMemory, CPersonality, CVision>()
                .each([&](auto entity, auto& queue, auto& trs, auto& needs, auto& memory, auto& personality, auto& vision)
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

                if (startActionFor(need, trs.pos, vision.radius, memory, queue))
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
// walks there first; a need with no memory (or a wander decision) walks to a
// random reachable tile so the entity explores and refreshes its memory.
bool EntityManager::startActionFor(const EntityDecision::Need need,
                                   const sf::Vector2i& pos,
                                   const float visionRadius,
                                   const CMemory& memory,
                                   CActionsQueue& queue)
{
        const auto exploreTarget = [&]()
        {
                sf::Vector2i from = pos;
                return m_map->getLocationWithinBound(from, visionRadius);
        };

        switch (need)
        {
                case EntityDecision::Need::Thirst:
                {
                        if (queueHasAction(queue, ActionTypes::Drinking))
                                return false;

                        if (auto target = memory.findNearest(pos, MemoryKind::Water))
                        {
                                queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, *target));
                                queue.actions.push_back(std::make_shared<CDrinking>(ActionTypes::Drinking, m_game_clock->getTimestamp()));
                        }
                        else
                        {
                                // Nothing to drink is remembered yet: go look for some.
                                queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, exploreTarget()));
                        }
                        return true;
                }

                case EntityDecision::Need::Hunger:
                {
                        if (queueHasAction(queue, ActionTypes::Eating))
                                return false;

                        if (auto target = memory.findNearest(pos, MemoryKind::Food))
                        {
                                queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, *target));
                                queue.actions.push_back(std::make_shared<CEating>(ActionTypes::Eating, m_game_clock->getTimestamp()));
                        }
                        else
                        {
                                queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, exploreTarget()));
                        }
                        return true;
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
                                queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, target->pos));
                                queue.actions.push_back(std::make_shared<CGather>(ActionTypes::Gathering, target->pos, target->element,
                                                                                  m_game_clock->getTimestamp()));
                        }
                        else
                        {
                                // Nothing workable is remembered yet: explore to find some.
                                queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, exploreTarget()));
                        }
                        return true;
                }

                case EntityDecision::Need::Wander:
                {
                        if (queueHasAction(queue, ActionTypes::Moving))
                                return false;

                        queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, exploreTarget()));
                        return true;
                }

                case EntityDecision::Need::None:
                default:
                        return false;
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
        std::unordered_map<std::int64_t, Elements> cache;
        const auto elementAt = [&](const int tx, const int ty)
        {
                const std::int64_t key = (static_cast<std::int64_t>(tx) << 32) ^ static_cast<std::uint32_t>(ty);
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
