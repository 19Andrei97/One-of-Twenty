#pragma once

#include "../mapgen/MapGenerator.h"
#include "Components_Entities.h"

class EntityManager
{

	int								m_total_entities{ 0 };
	float&							m_delta_time;
	sf::Font&						m_font;
	std::unique_ptr<entt::registry>	m_registry;
	std::shared_ptr<MapGenerator>	m_map;
	std::shared_ptr<GameClock>		m_game_clock;
	std::mutex						m_mutex;
	BS::thread_pool<>				m_threads{ 2 };

	// Private function
	void addTextToEntityInfo(std::vector<sf::Text>& vec, std::string&& s, int size, const sf::Color& color);

public:

	bool show_vision = false;

	// CONSTRUCTOR
	EntityManager(sf::Font& font, std::shared_ptr<MapGenerator> map, std::shared_ptr<GameClock> clock, float& deltatime)
		: m_font(font)
		, m_map(map)
		, m_game_clock(clock)
		, m_delta_time(deltatime)
	{
		m_registry = std::make_unique<entt::registry>();

		// THREADS TO BE IMPLEMENTED
		//m_threads.submit_task([this] { startChunksGenerator(); });
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