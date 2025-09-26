
#include "pch.h"

#include "Game.h"

// GAME FLOW ////////////////////////////////////////////////////////////

Game::Game(const std::string& path)
{
	std::ifstream f(path);
	nlohmann::json data = nlohmann::json::parse(f);

	// LOGGER
	Logger::init(data["logger"]["file"]);

	// WINDOW AND FRAME
	sf::State state;

	if (data["window"]["fullscreen"])
		state = sf::State::Fullscreen;
	else
		state = sf::State::Windowed;

	m_window.create(sf::VideoMode({ data["window"]["width"], data["window"]["height"] }), "One Of Twenty", state);
	m_window.setFramerateLimit(data["window"]["frames"]);

	// IN GAME CLOCK
	LOG_DEBUG("Creating in Game Clock.");
	m_game_clock = std::make_shared<GameClock>(120.f);
	m_game_clock->onNewDay([&]() 
		{
			LOG_INFO("New day. Passed: {}", m_game_clock->getDays());
		});

	// TEXT AND FONT
	LOG_DEBUG("Opening font file.");
	if (!m_font.openFromFile(data["font"]["file"])) {
		std::cerr << "Could not load font!\n";
	}

	LOG_DEBUG("Creating Text.");
	m_text = std::make_unique<sf::Text>(m_font);
	m_text->setFont(m_font);
	m_text->setCharacterSize(data["font"]["size"]);
	m_text->setFillColor(sf::Color(data["font"]["color"][0], data["font"]["color"][1], data["font"]["color"][2]));

	// MAP GENERATION
	LOG_DEBUG("Creating Map Generator.");
	m_map = std::make_shared<MapGenerator>(m_font, m_currentFrame, "src/config/map_data.json");
	m_map->setDebugNoiseView(false);

	// HUD
	LOG_DEBUG("Creating HUD.");
	m_hud = std::make_unique<Hud>(m_font, m_map, "src/config/hud_menu_data.json", data["window"]["width"], data["window"]["height"]);
	m_hud->init();

	// CAMERA
	LOG_DEBUG("Creating Camera.");
	m_camera = std::make_unique<Camera>(data["window"]["width"], data["window"]["height"]);
	m_camera->cInput = std::make_shared<CInput>();
	m_window.setView(m_camera->getCamera());

	// ENTITIES MANAGER
	LOG_DEBUG("Creating Entities Manager.");
	m_entity_manager = std::make_unique<EntityManager>(m_font, m_map, m_game_clock, m_deltaTime);
}

void Game::run()
{
	
	while (m_running)
	{
		m_deltaTime = m_clock.restart().asSeconds();
		m_game_clock->update(m_deltaTime);

		if (!m_paused)
		{
			sMovement();
			sCollision();
		}

		sUserInput();
		sRender();

		++m_currentFrame;
	}

}

void Game::setPaused()
{
	m_paused = !m_paused;

	m_game_clock->pause(m_paused);
}

// SPAWNS ////////////////////////////////////////////////////////////

void Game::spawnEntities()
{
	m_entity_manager->addEntity(EntityType::Human_Generic);
}

// SYSTEMS ////////////////////////////////////////////////////////////

void Game::sMovement()
{
	// Entities Updates
	m_entity_manager->update();


	if (m_camera->cInput->up)
	{
		m_camera->move(0, -m_camera->getVelocity() * m_deltaTime);
		m_current_position.y -= static_cast<int>(m_camera->getVelocity() * m_deltaTime);
	} else if (m_camera->cInput->down)
	{
		m_camera->move(0, m_camera->getVelocity() * m_deltaTime);
		m_current_position.y += static_cast<int>(m_camera->getVelocity() * m_deltaTime);
	}
	if (m_camera->cInput->left)
	{
		m_camera->move(-m_camera->getVelocity() * m_deltaTime, 0);
		m_current_position.x -= static_cast<int>(m_camera->getVelocity() * m_deltaTime);
	} else if (m_camera->cInput->right)
	{
		m_camera->move(m_camera->getVelocity() * m_deltaTime, 0);
		m_current_position.x += static_cast<int>(m_camera->getVelocity() * m_deltaTime);
	}
		
}

void Game::sCollision()
{

}

void Game::sRender()
{
	m_window.clear();

	m_window.setView(m_camera->getCamera());
	m_map->render(m_camera->getWorldBounds(), m_window);

	m_entity_manager->render(m_window);
	
	m_window.setView(m_hud->getCamera());
	m_hud->render(m_window);

	m_window.display();
}


void Game::sUserInput()
{
	while (const std::optional event = m_window.pollEvent())
	{
		if (event->is<sf::Event::Closed>())
		{
			m_running = false;
		}

		// KEYBOARD LOGIC
		if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>())
		{
			if (keyPressed->code == sf::Keyboard::Key::P)
			{
				setPaused();
			}

			if (!m_paused)
			{
				if (keyPressed->code == sf::Keyboard::Key::W)
					m_camera->cInput->up = true;
				if (keyPressed->code == sf::Keyboard::Key::S)
					m_camera->cInput->down = true;
				if (keyPressed->code == sf::Keyboard::Key::A)
					m_camera->cInput->left = true;
				if (keyPressed->code == sf::Keyboard::Key::D)
					m_camera->cInput->right = true;
				if (keyPressed->code == sf::Keyboard::Key::M)
					m_map->setSeed();
				if (keyPressed->code == sf::Keyboard::Key::G)
					m_map->setDebugWireFrame(true);
				if (keyPressed->code == sf::Keyboard::Key::Num1)
					m_entity_manager->addEntity(EntityType::Human_Generic);
			}
		}

		if (const auto* keyReleased = event->getIf<sf::Event::KeyReleased>())
		{
			if (!m_paused)
			{
				switch (keyReleased->code)
				{
				case sf::Keyboard::Key::W:
					m_camera->cInput->up = false;
					break;
				case sf::Keyboard::Key::S:
					m_camera->cInput->down = false;
					break;
				case sf::Keyboard::Key::A:
					m_camera->cInput->left = false;
					break;
				case sf::Keyboard::Key::D:
					m_camera->cInput->right = false;
					break;
				case sf::Keyboard::Key::G:
					m_map->setDebugWireFrame(false);
					break;
						
				default: break;
				}
			}
		}

		// TEXT INPUT LOGIC
		if (const auto* textEntered = event->getIf<sf::Event::TextEntered>())
		{
			m_hud->input(*textEntered);
		}

		// MOUSE BUTTONS LOGIC
		if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>())
		{
			if (!m_paused)
			{
				switch (mousePressed->button)
				{
				case sf::Mouse::Button::Left:
				{
					auto pixel = sf::Mouse::getPosition(m_window);

					// GUI coords
					sf::Vector2f guiPos = m_window.mapPixelToCoords(pixel, m_hud->getCamera());
					m_hud->input(*mousePressed, guiPos);

					// World coords
					sf::Vector2f worldPos = m_window.mapPixelToCoords(pixel, m_camera->getCamera());
					m_hud->infoBox(m_map->getPositionInfo(worldPos));

					// TESTING ENTITY MOVING
					//m_entity_manager->nextTarget(EntityType::Human_Generic, worldPos);

					break;
				}

				case sf::Mouse::Button::Right:
				{
					// Right click logic
					break;
				}

				default: break;
				}
			}
		}
		
		// MOUSE CLICK RELEASED
		if (const auto* mousereleased = event->getIf<sf::Event::MouseButtonReleased>())
		{
			if (!m_paused)
			{
				switch (mousereleased->button)
				{
				case sf::Mouse::Button::Left:
				{
					auto pixel = sf::Mouse::getPosition(m_window);
					sf::Vector2f mouseWorldPos = m_window.mapPixelToCoords(pixel, m_camera->getCamera());
					sf::Vector2f mouseHudPos = m_window.mapPixelToCoords(pixel, m_hud->getCamera());

					m_hud->input(*mousereleased, mouseHudPos);
				}
				}
			}
		}

		// MOUSE MOVING
		if (const auto* mousemoved = event->getIf<sf::Event::MouseMoved>())
		{
			if (!m_paused)
			{
				auto pixel = sf::Mouse::getPosition(m_window);
				sf::Vector2f mouseWorldPos = m_window.mapPixelToCoords(pixel, m_camera->getCamera());
				sf::Vector2f mouseHudPos = m_window.mapPixelToCoords(pixel, m_hud->getCamera());

				m_hud->input(*mousemoved, mouseHudPos);
			}
		}

		// MOUSE WHEEL LOGIC
		if (const auto* mouseWheel = event->getIf<sf::Event::MouseWheelScrolled>())
		{
			if (!m_paused)
			{
				if (mouseWheel->delta > 0)
					m_camera->zoomIn();
				else
					m_camera->zoomOut();
			}
		}
	}

}

