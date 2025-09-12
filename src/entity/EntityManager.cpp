
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

	// Moving
	m_registry->view<CTransform, CVision>().each([&](auto entity, CTransform& trs, CVision& vsn)
	{
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
					trs.has_target = false;
					std::cout << "Target reached by " << (int)entity << "\n";
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