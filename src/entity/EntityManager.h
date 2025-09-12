#pragma once

#include "../mapgen/MapGenerator.h"
#include "Components_Entities.h"

class EntityManager
{
	std::unique_ptr<entt::registry>	m_registry;
	std::shared_ptr<MapGenerator>	m_map;
	int								m_totalEntities{ 0 };

public:

	bool show_vision = true;

	// CONSTRUCTOR
	EntityManager(std::shared_ptr<MapGenerator> map)
		: m_map(map)
	{
		m_registry = std::make_unique<entt::registry>();
	}

	// MAIN FUNCTIONS
	void render(sf::RenderTarget& window);
	void update();
	void addEntity(const EntityType& type);

	// SETTERS
	void nextTarget(const EntityType& type, sf::Vector2f& targ);

	// GETTERS
	//const EntityVec& getEntities(const EntityType& type);
};