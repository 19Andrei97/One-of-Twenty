#include "EntityManager.h"

EntityManager::EntityManager() {}

void EntityManager::update()
{
	for (auto& e : m_entitiesToAdd)
	{
		m_entities.push_back(e);
		m_entityMap[e->type()].push_back(e);
	}

	m_entitiesToAdd.clear();

	removeDeadEntities(m_entities);

	for (auto& [tag, entityVec] : m_entityMap)
	{

		removeDeadEntities(entityVec);

	}
}

/// MANAGING ENTITIES //////////////////////////////////////////////////////////////

void EntityManager::removeDeadEntities(EntityVec& vec)
{
	vec.erase(
		std::remove_if(vec.begin(), vec.end(),
			[](const std::shared_ptr<Entity>& e) {
				return !e->isAlive();
			}),
		vec.end()
	);
}

std::shared_ptr<Entity> EntityManager::addEntity(const int type)
{
	auto entity = std::shared_ptr<Entity>(new Entity(++m_totalEntities, type));

	m_entitiesToAdd.push_back(entity);

	return entity;
}

/// GETTING ENTITIES //////////////////////////////////////////////////////////////

const EntityVec& EntityManager::getEntities()
{
	return m_entities;
}

const EntityVec& EntityManager::getEntities(const int type)
{
	static EntityVec emptyVec; // restituito se il tag non esiste

	auto it = m_entityMap.find(type);
	if (it != m_entityMap.end()) {
		return it->second;
	}
	return emptyVec;
}