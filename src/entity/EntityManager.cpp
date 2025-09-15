
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
		--m_totalEntities;
	}

	// UPDATE ENTITIES

	m_registry->view<CTransform, CVision, CMemory, CBasicNeeds>().each([&](auto entity, CTransform& trs, CVision& vsn, CMemory& mmr, CBasicNeeds& needs)
	{
			// UPDATING NEEDS
			if (m_game_clock->getHour() != m_last_updated_hour)
			{
				needs.hunger	= std::max(0, needs.hunger - 5);
				needs.thirst	= std::max(0, needs.thirst - 10);
				needs.sleep		= std::min(needs.sleep + 2, 100);

				LOG_INFO("Hunger: {}", needs.hunger);
				LOG_INFO("Thirst: {}", needs.thirst);
				LOG_INFO("Sleep: {}",  needs.sleep);

				m_last_updated_hour = m_game_clock->getHour();
			}

			if (!needs.drinking && !needs.eating && !needs.sleeping)
			{
				if(needs.thirst < 10)
				{

					auto pos = mmr.getLocation(Elements::ocean);
					if (pos.x != 0.f)
					{
						trs.target = pos;
						trs.has_target = true;
						needs.drinking = true;

						LOG_INFO("Entity {} going to drink", (int)entity);
					}
					else
						LOG_INFO("Entity {} searching for water.", (int)entity);

				}

				if (needs.hunger < 10)
				{

					auto pos = mmr.getLocation(Elements::hill);
					if (pos.x != 0.f)
					{
						trs.target = pos;
						trs.has_target = true;
						needs.eating = true;

						LOG_INFO("Entity {} going to eat", (int)entity);
					}
					else
						LOG_INFO("Entity {} searching for food.", (int)entity);

				}

				if (needs.sleeping > 80)
				{
					//LOG_INFO("Entity {} needs to sleep", (int)entity);
				}

				// UPDATE MEMORY IF NO NEEDS
				const auto resources{ m_map->getResourcesWithinBoundary(trs.pos, vsn.radius) };
				mmr.rememberLocation(resources);
			}

			// UPDATE MOVMENT
			if (trs.has_target)
			{
				sf::Vector2f direction = trs.target - trs.pos;
				float distance = sqrt(direction.x * direction.x + direction.y * direction.y);

				if (distance > 1.f) {
					direction.x /= distance;   // normalize x
					direction.y /= distance;   // normalize y

					trs.pos.x += direction.x * trs.speed; // TO DO ADD DELTA TIME
					trs.pos.y += direction.y * trs.speed;
				}
				else
				{
					// Entity drinking.
					if (mmr.getLocation(Elements::ocean) == trs.target && needs.drinking)
					{
						needs.drinking = false;
						needs.thirst = 100;

						LOG_INFO("Entity {} finished drinking.", (int)entity);
					}
					// Entity eating.
					else if (mmr.getLocation(Elements::hill) == trs.target && needs.eating)
					{
						needs.eating = false;
						needs.hunger = 100;

						LOG_INFO("Entity {} finished eating.", (int)entity);
					}

					trs.has_target = false;
					//LOG_INFO("Target reached by entity with id {}.", (int)entity);
				}
			}
			else
			{
				trs.target = m_map->getLocationWithinBound(trs.pos, vsn.radius);
				trs.has_target = true;
			}
	});
}

void EntityManager::render(sf::RenderTarget& window)
{
	m_registry->view<CShape, CTransform, CVision>().each([&](auto entity, CShape& shape, CTransform& trs, CVision& vsn)
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

	++m_totalEntities;
}

void EntityManager::nextTarget(const EntityType& type, sf::Vector2f& targ)
{
	m_registry->view<CTransform, CType>().each([&](auto entity, CTransform& trs, CType& tp)
	{
		if (tp.type == type)
		{
			trs.target = targ;
			trs.has_target = true;
		}
	});
}