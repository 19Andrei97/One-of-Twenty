#pragma once

#include "Entity.h"
#include "EntityManager.h"
#include "src/mapgen/MapGenerator.h"
#include "Camera.h"
#include "Components.h"
#include "json.h"
#include "src/hud/Hud.h"

#include <SFML/Graphics.hpp>

class Game
{
	sf::RenderWindow				m_window;
	sf::Clock						m_clock;
	sf::Font						m_font;
	std::unique_ptr<sf::Text>		m_text;
	Camera							m_camera;
	std::unique_ptr<MapGenerator>	m_map;
	std::unique_ptr<Hud>			m_hud;

	sf::Vector2i					m_current_position{ 0, 0 };
	float							m_deltaTime{ 0.f };
	int								m_score{ 0 };
	int								m_currentFrame{ 0 };
	bool							m_paused = false;	// Game is paused check
	bool							m_running = true;	// Game is running check

	std::shared_ptr<Entity> m_player;

	void init(const std::string& config);
	void setPaused();

	void sMovement();
	void sUserInput();
	void sRender();
	void sCollision();

	void spawnPlayer();

public:

	Game(const std::string& config);

	void run();

};
