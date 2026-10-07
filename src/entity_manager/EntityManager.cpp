
#include <pch.h>

#include "EntityManager.h"

#include <algorithm>

namespace
{
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
                --m_total_entities;
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
                        sf::Vector2i direction = action->target - trs.pos;
                        float distance = sqrt(direction.x * direction.x + direction.y * direction.y);

                        if (distance > 0.5f)
                        {
                                // Normalizing
                                direction.x /= distance;
                                direction.y /= distance;

                                // Tile cost
                                float cost{ m_map->getTileCost(trs.pos) };

                                // Updating position
                                std::lock_guard<std::mutex> lock(m_mutex);
                                trs.pos.x += direction.x * trs.speed * m_delta_time * cost;
                                trs.pos.y += direction.y * trs.speed * m_delta_time * cost;
                        }
                        else
                        {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                queue.actions.pop_front();
                        }
                }
        });

        // Hourly needs decay
        const int hour = m_game_clock->getHour();
        m_registry->view<CBasicNeeds, CPersonality>().each([&](auto entity, auto& needs, auto&)
        {
                if (hour == needs.last_update)
                        return;

                needs.applyHourlyDecay(m_config.thirst_decay_per_hour,
                                       m_config.hunger_decay_per_hour,
                                       m_config.sleep_gain_per_hour);
                needs.last_update = hour;
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
                        needs.satisfy(1);
                        std::lock_guard<std::mutex> lock(m_mutex);
                        queue.actions.pop_front();
                }
                else if (auto action = std::dynamic_pointer_cast<CDrinking>(front); action
                        && elapsed - action->timestamp_min > action->duration_min)
                {
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
        m_registry->emplace<CLifespan>(entity, 100);
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
        ++m_total_entities;
}

// Add a moving action with assosciated target position.
void EntityManager::nextTarget(const EntityType& type, sf::Vector2i& target)
{
        m_registry->view<CActionsQueue, CTransform, CType>().each([&](auto entity, auto& queue, auto& trs, auto& tp)
        {

                if (tp.type == type)
                        queue.actions.push_back(std::make_shared<CMoving>( ActionTypes::Moving, target ));

        });
}

// HELPER FUNCTION
void EntityManager::addTextToEntityInfo(std::vector<sf::Text>& vec, std::string&& s, int size, const sf::Color& color)
{
        vec.emplace_back(sf::Text{ m_font });
        vec.back().setString(s);
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
