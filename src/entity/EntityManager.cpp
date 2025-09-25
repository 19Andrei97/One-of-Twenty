
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

	m_registry->view<CActionsQueue, CTransform>().each([&](auto entity, auto& queue, auto& trs)
	{
		if (queue.actions.empty())
			return;

		if(auto action = std::dynamic_pointer_cast<CMoving>(queue.actions.front()))
		{
			sf::Vector2f direction = action->target - trs.pos;
			float distance = sqrt(direction.x * direction.x + direction.y * direction.y);

			if (distance > 1.f) 
			{
				direction.x /= distance;   // normalize x
				direction.y /= distance;   // normalize y

				trs.pos.x += direction.x * trs.speed; // TO DO ADD DELTA TIME
				trs.pos.y += direction.y * trs.speed;
			}
			else
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				queue.actions.pop_front();
			}
		}
	});

	m_registry->view<CActionsQueue, CTransform, CVision>().each([&](auto entity, auto& queue, auto& trs, auto& vision)
	{
		// GET RANDOM TARGET IF NO ACTION
		if (queue.actions.empty())
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, m_map->getLocationWithinBound(trs.pos, vision.radius)));
		}
	});

	m_registry->view<CTransform, CMemory, CVision>().each([&](auto entity, auto& trs, auto& memory, auto& vision)
	{
		// UPDATE MEMORY
		memory.rememberLocation(m_map->getResourcesWithinBoundary(trs.pos, vision.radius));
	});

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

				LOG_INFO("Entity {} finished eating.", (int)entity);
			}
		}
		else if(auto action = std::dynamic_pointer_cast<CDrinking>(queue.actions.front()))
		{
			if (m_game_clock->getTimestamp() - action->timestamp_min > action->duration_min)
			{
				needs.thirst = 100;

				std::lock_guard<std::mutex> lock(m_mutex);
				queue.actions.pop_front();

				LOG_INFO("Entity {} finished drinking.", (int)entity);
			}
		}

		// UPDATING NEEDS
		if (m_game_clock->getHour() != m_last_updated_hour)
		{

			needs.hunger = std::max(0, needs.hunger - 5);
			needs.thirst = std::max(0, needs.thirst - 10);
			needs.sleep = std::min(needs.sleep + 2, 100);

			//LOG_INFO("Hunger: {}", needs.hunger);
			//LOG_INFO("Thirst: {}", needs.thirst);
			//LOG_INFO("Sleep: {}", needs.sleep);

			m_last_updated_hour = m_game_clock->getHour();
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
				LOG_INFO("Entity {} is thirsty.", (int)entity);

				auto pos = memory.getLocation(Elements::ocean);
				if (pos.x != 0.f)
				{
					queue.actions.push_back(std::make_shared<CMoving>( ActionTypes::Moving, pos ));
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
				LOG_INFO("Entity {} is hungry.", (int)entity);

				auto pos = memory.getLocation(Elements::hill);
				if (pos.x != 0.f)
				{
					queue.actions.push_back(std::make_shared<CMoving>(ActionTypes::Moving, pos));
					queue.actions.push_back(std::make_shared<CEating>(ActionTypes::Eating, m_game_clock->getTimestamp()));
				}
			}
		}
	});

	m_registry->view<CBasicNeeds, CEntityInfo>().each([&](auto entity, auto& needs, auto& info)
	{
			if (info.text.empty())
			{
				// Hunger
				info.text.push_back(sf::Text{ m_font });
				info.text.back().setString("Hunger: " + std::to_string(needs.hunger));
				info.text.back().setCharacterSize(info.size);
				info.text.back().setFillColor(info.text_color);

				// Thirst
				info.text.push_back(sf::Text{ m_font });
				info.text.back().setString("Thirst: " + std::to_string(needs.thirst));
				info.text.back().setCharacterSize(info.size);
				info.text.back().setFillColor(info.text_color);

				// Sleep
				info.text.push_back(sf::Text{ m_font });
				info.text.back().setString("Sleep: " + std::to_string(needs.sleep));
				info.text.back().setCharacterSize(info.size);
				info.text.back().setFillColor(info.text_color);
			}
			else
			{
				info.text[0].setString("Hunger: " + std::to_string(needs.hunger));
				info.text[1].setString("Thirst: " + std::to_string(needs.thirst));
				info.text[2].setString("Sleep: " + std::to_string(needs.sleep));
			}

	});

}

void EntityManager::render(sf::RenderTarget& window)
{
	// ENTITIES
	m_registry->view<CShape, CTransform, CVision>().each([&](auto entity, auto& shape, auto& trs, auto& vsn)
	{
		shape.circle.setPosition(trs.pos);
		window.draw(shape.circle);

		// THIS IS JUST FOR TESTING AND NEEDS TO BE IMPROVED
		if (show_vision)
		{
			sf::CircleShape circle(vsn.radius);
			circle.setFillColor({ 255, 255, 255, 100 });
			circle.setPosition(trs.pos);
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
	m_registry->emplace<CTransform>(entity, sf::Vector2f{ 0.0f, 0.0f }, 1.f);
	m_registry->emplace<CShape>(entity, 10, 4, sf::Color::White);
	m_registry->emplace<CVision>(entity);
	m_registry->emplace<CMemory>(entity);
	m_registry->emplace<CBasicNeeds>(entity);
	m_registry->emplace<CActionsQueue>(entity);
	m_registry->emplace<CEntityInfo>(entity, 60, 40);

	++m_total_entities;
}

void EntityManager::nextTarget(const EntityType& type, sf::Vector2f& target)
{
	m_registry->view<CActionsQueue, CTransform, CType>().each([&](auto entity, auto& queue, auto& trs, auto& tp)
	{

		if (tp.type == type)
			queue.actions.push_back(std::make_shared<CMoving>( ActionTypes::Moving, target ));

	});
}