#pragma once

#include "../entity/EntityManager.h"
#include "../mapgen/MapGenerator.h"
#include "../camera/Camera.h"

#include "../hud/Hud.h"

class Game
{
	sf::RenderWindow				m_window;
	sf::Clock						m_clock;
	sf::Font						m_font;
	std::unique_ptr<sf::Text>		m_text;
	std::unique_ptr<Camera>			m_camera;
	std::unique_ptr<EntityManager>	m_entity_manager;
	std::unique_ptr<Hud>			m_hud;
	std::shared_ptr<MapGenerator>	m_map;

	sf::Vector2i					m_current_position{ 0, 0 };
	float							m_deltaTime{ 0.f };
	int								m_score{ 0 };
	int								m_currentFrame{ 0 };
	bool							m_paused = false;	// Game is paused check
	bool							m_running = true;	// Game is running check

	void setPaused();

	void sMovement();
	void sUserInput();
	void sRender();
	void sCollision();

	void spawnEntities();

public:

	Game(const std::string& path);

	void run();

};
