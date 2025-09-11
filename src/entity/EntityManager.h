#pragma once

#include "Components_Entities.h"

class EntityManager
{
	std::unique_ptr<entt::registry>	m_registry;
	int								m_totalEntities{ 0 };

public:

	// CONSTRUCTOR
	EntityManager() 
	{
		m_registry = std::make_unique<entt::registry>();
	}

	// MAIN FUNCTIONS
	void render(sf::RenderTarget& window);
	void update();
	void addEntity(const EntityType& type);

	// GETTERS
	//const EntityVec& getEntities(const EntityType& type);
};