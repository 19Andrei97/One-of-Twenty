
#include "pch.h"

#include "EntityManager.h"

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

	// Random Target
	m_registry->view<CActionsQueue, CTransform, CVision>().each([&](auto entity, auto& queue, auto& trs, auto& vision)
	{
		if (queue.actions.empty())
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, m_map->getLocationWithinBound(trs.pos, vision.radius)));
		}
	});

	// Needs system
	m_registry->view<CActionsQueue, CBasicNeeds, CMemory>().each([&](auto entity, auto& queue, auto& needs, auto& memory)
	{
		if (queue.actions.empty())
			return;
		
		// PERFORMING ACTION NEEDS
		if (auto action = std::dynamic_pointer_cast<CEating>(queue.actions.front()))
		{
			if (m_game_clock->getTimestamp() - action->timestamp_min > action->duration_min)
			{
				needs.hunger = 100;

				std::lock_guard<std::mutex> lock(m_mutex);
				queue.actions.pop_front();
			}
		}
		else if(auto action = std::dynamic_pointer_cast<CDrinking>(queue.actions.front()))
		{
			if (m_game_clock->getTimestamp() - action->timestamp_min > action->duration_min)
			{
				needs.thirst = 100;

				std::lock_guard<std::mutex> lock(m_mutex);
				queue.actions.pop_front();
			}
		}
		else if (auto action = std::dynamic_pointer_cast<CSleeping>(queue.actions.front()))
		{
			if (m_game_clock->getTimestamp() - action->timestamp_min > action->duration_min)
			{
				needs.sleep = 0;

				std::lock_guard<std::mutex> lock(m_mutex);
				queue.actions.pop_front();
			}
		}

		// UPDATING NEEDS
		if (m_game_clock->getHour() != needs.last_update)
		{

			needs.hunger = std::max(0, needs.hunger - 3);
			needs.thirst = std::max(0, needs.thirst - 5);
			needs.sleep = std::min(needs.sleep + 2, 100);

			needs.last_update = m_game_clock->getHour();
		}

		// Checking if thirsty
		if (needs.thirst < 10)
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			// Checking if action is present
			auto it = std::find_if(queue.actions.begin(), queue.actions.end(),
				[](const std::shared_ptr<CAction>& act)
				{
					return dynamic_cast<CDrinking*>(act.get()) != nullptr;
				});

			if (it == queue.actions.end())
			{ 
				if (auto pos = memory.getLocation(Elements::ocean))
				{
					LOG_DEBUG("[MEMORY] Water {} {}", pos->x, pos->y);

					queue.actions.push_back(std::make_shared<CMoving>( ActionTypes::Moving, *pos));
					queue.actions.push_back(std::make_shared<CDrinking>(ActionTypes::Drinking, m_game_clock->getTimestamp()));
				}
			}
		}

		// Checking if hungry
		if (needs.hunger < 10)
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			// Checking if action is present
			auto it = std::find_if(queue.actions.begin(), queue.actions.end(),
				[](const std::shared_ptr<CAction>& act)
				{
					return dynamic_cast<CEating*>(act.get()) != nullptr;
				});

			if (it == queue.actions.end())
			{
				if (auto pos = memory.getLocation(Elements::hill))
				{
					LOG_DEBUG("[MEMORY] Food {} {}", pos->x, pos->y);

					queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, *pos));
					queue.actions.push_back(std::make_shared<CEating>(ActionTypes::Eating, m_game_clock->getTimestamp()));
				}
			}
		}

		// Checking if need sleeping
		if (needs.sleep > 90)
		{
			std::lock_guard<std::mutex> lock(m_mutex);

			// Checking if action is present
			auto it = std::find_if(queue.actions.begin(), queue.actions.end(),
				[](const std::shared_ptr<CAction>& act)
				{
					return dynamic_cast<CSleeping*>(act.get()) != nullptr;
				});

			if (it == queue.actions.end())
			{
				queue.actions.push_back(std::make_shared<CSleeping>(ActionTypes::Sleeping, m_game_clock->getTimestamp()));
			}
		}
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

				default:
					info.text[3].setString("Idle.");
				}
			}
	});

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

		float padding		= 5.f;
		float lineHeight	= static_cast<float>(info.size) + 2.f;
		float boxWidth		= 0.f;

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

void EntityManager::addEntity(const EntityType& type)
{
	auto entity = m_registry->create();

	m_registry->emplace<CType>(entity, type);
	m_registry->emplace<CLifespan>(entity, 100);
	m_registry->emplace<CTransform>(entity, sf::Vector2i{ 0, 0 }, 100.f);
	m_registry->emplace<CShape>(entity, 10, 4, sf::Color::White);
	m_registry->emplace<CVision>(entity);
	m_registry->emplace<CMemory>(entity);
	m_registry->emplace<CBasicNeeds>(entity);
	m_registry->emplace<CActionsQueue>(entity);
	m_registry->emplace<CEntityInfo>(entity, 60, 40);

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