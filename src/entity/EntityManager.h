#pragma once


#include "Entity.h"
#include <vector>
#include <map>

using EntityVec = std::vector<std::shared_ptr<Entity>>;
using EntityMap = std::map<int, EntityVec>;

class EntityManager
{
	EntityVec	m_entities;
	EntityVec	m_entitiesToAdd;
	EntityMap	m_entityMap;
	size_t		m_totalEntities = 0;

	void removeDeadEntities(EntityVec& vec);

public:

	EntityManager();

	void update();

	std::shared_ptr<Entity> addEntity(const int type);

	const EntityVec& getEntities();
	const EntityVec& getEntities(const int type);
};