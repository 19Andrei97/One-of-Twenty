
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
	m_registry->view<CTransform>().each([](auto entity, CTransform& trs)
	{
			if (trs.has_target)
			{
				sf::Vector2f direction = trs.target - trs.pos;
				float distance = sqrt(direction.x * direction.x + direction.y * direction.y);

				if (distance > 0.5f) {
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
	});


}

void EntityManager::render(sf::RenderTarget& window)
{
	m_registry->view<CShape, CTransform>().each([&](auto entity, CShape& shape, CTransform& trs)
		{
			shape.circle.setPosition(trs.pos);
			window.draw(shape.circle);
		});
}

/// MANAGING ENTITIES //////////////////////////////////////////////////////////////

void EntityManager::addEntity(const EntityType& type)
{
	auto entity = m_registry->create();

	// TODO ADD SHAPE
	m_registry->emplace<CType>(entity, type);
	m_registry->emplace<CLifespan>(entity, 100);
	m_registry->emplace<CTransform>(entity, sf::Vector2f{ 0.0f, 0.0f }, 1.f);
	m_registry->emplace<CShape>(entity, 10, 4, sf::Color::White);

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