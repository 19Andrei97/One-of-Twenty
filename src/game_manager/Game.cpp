
#include "pch.h"

#include "Game.h"

// GAME FLOW ////////////////////////////////////////////////////////////

Game::Game(const std::string& config)
{
	init(config);
}

void Game::init(const std::string& path)
{
	std::ifstream f(path);
	nlohmann::json data = nlohmann::json::parse(f);

	// WINDOW AND FRAME
	sf::State state;

	if (data["window"]["fullscreen"])
		state = sf::State::Fullscreen;
	else
		state = sf::State::Windowed;

	m_window.create(sf::VideoMode({ data["window"]["width"], data["window"]["height"] }), "One Of Twenty", state);
	m_window.setFramerateLimit(data["window"]["frames"]);

	// TEXT AND FONT
	if (!m_font.openFromFile(data["font"]["file"])) {
		std::cerr << "Could not load font!\n";
	}

	m_text = std::make_unique<sf::Text>(m_font);
	m_text->setFont(m_font);
	m_text->setCharacterSize(data["font"]["size"]);
	m_text->setFillColor(sf::Color(data["font"]["color"][0], data["font"]["color"][1], data["font"]["color"][2]));
	m_text->setPosition({ 20.f, 20.f });
	m_text->setString("Hello");

	// MAP GENERATION
	m_map = std::make_unique<MapGenerator>(m_font, m_currentFrame, "src/config/map_data.json");
	m_map->setDebugNoiseView(false);

	// HUD
	m_hud = std::make_unique<Hud>(m_font, *m_map, "src/config/hud_menu_data.json", data["window"]["width"], data["window"]["height"]);
	m_hud->init();

	// CAMERA
	m_camera.setCamera(data["window"]["width"], data["window"]["height"]);
	m_camera.cInput		= std::make_shared<CInput>();
	m_window.setView(m_camera.getCamera());
}

void Game::run()
{
	
	while (m_running)
	{
		m_deltaTime = m_clock.restart().asSeconds();

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
}

// SPAWNS ////////////////////////////////////////////////////////////

void Game::spawnPlayer()
{

}

// SYSTEMS ////////////////////////////////////////////////////////////

void Game::sMovement()
{
	if (m_camera.cInput->up)
	{
		m_camera.move(0, -m_camera.getVelocity() * m_deltaTime);
		m_current_position.y -= static_cast<int>(m_camera.getVelocity() * m_deltaTime);
	} else if (m_camera.cInput->down)
	{
		m_camera.move(0, m_camera.getVelocity() * m_deltaTime);
		m_current_position.y += static_cast<int>(m_camera.getVelocity() * m_deltaTime);
	}
	if (m_camera.cInput->left)
	{
		m_camera.move(-m_camera.getVelocity() * m_deltaTime, 0);
		m_current_position.x -= static_cast<int>(m_camera.getVelocity() * m_deltaTime);
	} else if (m_camera.cInput->right)
	{
		m_camera.move(m_camera.getVelocity() * m_deltaTime, 0);
		m_current_position.x += static_cast<int>(m_camera.getVelocity() * m_deltaTime);
	}
		
}

void Game::sCollision()
{

}

void Game::sRender()
{
	m_window.clear();

	m_window.setView(m_camera.getCamera());
	m_map->render(m_camera.getWorldBounds(), m_window);
	
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
					m_camera.cInput->up = true;
				if (keyPressed->code == sf::Keyboard::Key::S)
					m_camera.cInput->down = true;
				if (keyPressed->code == sf::Keyboard::Key::A)
					m_camera.cInput->left = true;
				if (keyPressed->code == sf::Keyboard::Key::D)
					m_camera.cInput->right = true;
				if (keyPressed->code == sf::Keyboard::Key::M)
					m_map->setSeed();
				if (keyPressed->code == sf::Keyboard::Key::G)
					m_map->setDebugWireFrame(true);
			}
		}

		if (const auto* keyReleased = event->getIf<sf::Event::KeyReleased>())
		{
			if (!m_paused)
			{
				switch (keyReleased->code)
				{
				case sf::Keyboard::Key::W:
					m_camera.cInput->up = false;
					break;
				case sf::Keyboard::Key::S:
					m_camera.cInput->down = false;
					break;
				case sf::Keyboard::Key::A:
					m_camera.cInput->left = false;
					break;
				case sf::Keyboard::Key::D:
					m_camera.cInput->right = false;
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
					sf::Vector2f worldPos = m_window.mapPixelToCoords(pixel, m_camera.getCamera());
					m_hud->infoBox(m_map->getPositionInfo(worldPos));

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
					sf::Vector2f mouseWorldPos = m_window.mapPixelToCoords(pixel, m_camera.getCamera());
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
				sf::Vector2f mouseWorldPos = m_window.mapPixelToCoords(pixel, m_camera.getCamera());
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
					m_camera.zoomIn();
				else
					m_camera.zoomOut();
			}
		}
	}

}

