#include <pch.h>

#include "Scene_Play.h"
#include "Game.h"

Scene_Play::Scene_Play(Game* game, const sf::Font& font, const nlohmann::json& data)
	: Scene(game)
{
	// IN GAME CLOCK
	LOG_DEBUG("Creating in Game Clock.");
	m_game_clock = std::make_shared<GameClock>(120.f);
	// Start mid-morning: a midnight start would put the settlement to sleep for
	// its first hours, before it has found food or water.
	m_game_clock->setTime(8, 0);
	m_game_clock->onNewDay([&]() 
		{
			LOG_INFO("New day. Passed: {}", m_game_clock->getDays());
		});

	// MAP GENERATION
	LOG_DEBUG("Creating Map Generator.");
	m_map = std::make_shared<MapGenerator>(m_currentFrame, data["map"]["file"]);

	// HUD
	LOG_DEBUG("Creating HUD.");
	m_hud = std::make_unique<Hud>(font, m_map, data["hud"]["file"], static_cast<float>(data["window"]["width"]), static_cast<float>(data["window"]["height"]));
	m_hud->init();

	// CAMERA
	LOG_DEBUG("Creating Camera.");
	m_camera = std::make_unique<Camera>(static_cast<float>(data["window"]["width"]), static_cast<float>(data["window"]["height"]));

	// ENTITIES MANAGER
	LOG_DEBUG("Creating Entities Manager.");
	m_entity_manager = std::make_unique<EntityManager>(font, m_map, m_game_clock, m_deltaTime, data["entity"]["file"]);
	m_entity_manager->seedPopulation();
}

void Scene_Play::update(float deltaTime)
{
	m_deltaTime = deltaTime;
	m_game_clock->update(m_deltaTime);

	if (!m_paused)
	{
		sMovement();
		sCollision();
	}

	refreshStats();

	++m_currentFrame;
}

void Scene_Play::refreshStats()
{
	if (!m_hud || !m_entity_manager || !m_game_clock)
		return;

	const auto day = m_game_clock->getDays();
	const auto hour = m_game_clock->getHour();

	m_hud->stats({
		"Day " + std::to_string(day) + "  " + (hour < 10 ? "0" : "") + std::to_string(hour) + ":00",
		"Population: " + std::to_string(m_entity_manager->population()) + " / " + std::to_string(m_entity_manager->maxPopulation()),
		"Births: " + std::to_string(m_entity_manager->births()),
		"Deaths: " + std::to_string(m_entity_manager->deaths()),
		"Stockpile: " + std::to_string(m_entity_manager->totalStockpile()),
		"Gathers: " + std::to_string(m_entity_manager->gathersCompleted()),
	});
}

void Scene_Play::setPaused(bool paused)
{
	m_paused = paused;
	if (m_game_clock)
		m_game_clock->pause(m_paused);
}

void Scene_Play::togglePaused()
{
	setPaused(!m_paused);
}

void Scene_Play::sMovement()
{
	if (m_entity_manager)
		m_entity_manager->update();

	if (m_camera)
	{
		if (m_camera->cInput.up)
		{
			m_camera->move(0, -m_camera->getVelocity() * m_deltaTime);
			m_current_position.y -= static_cast<int>(m_camera->getVelocity() * m_deltaTime);
		}
		else if (m_camera->cInput.down)
		{
			m_camera->move(0, m_camera->getVelocity() * m_deltaTime);
			m_current_position.y += static_cast<int>(m_camera->getVelocity() * m_deltaTime);
		}
		if (m_camera->cInput.left)
		{
			m_camera->move(-m_camera->getVelocity() * m_deltaTime, 0);
			m_current_position.x -= static_cast<int>(m_camera->getVelocity() * m_deltaTime);
		}
		else if (m_camera->cInput.right)
		{
			m_camera->move(m_camera->getVelocity() * m_deltaTime, 0);
			m_current_position.x += static_cast<int>(m_camera->getVelocity() * m_deltaTime);
		}
	}
}

void Scene_Play::sCollision()
{
        if (m_entity_manager)
                m_entity_manager->resolveCollisions();
}

void Scene_Play::sRender(sf::RenderTarget& target)
{
	if (m_camera && m_map)
	{
		target.setView(m_camera->getCamera());
		m_map->render(m_camera->getWorldBounds(), target);
	}

	if (m_entity_manager)
		m_entity_manager->render(target);

	if (m_hud)
	{
		target.setView(m_hud->getCamera());
		m_hud->render(target);
	}
}

void Scene_Play::sUserInput(const sf::Event& event)
{
	// KEYBOARD LOGIC
	if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>())
	{
		if (keyPressed->code == sf::Keyboard::Key::P)
		{
			togglePaused();
		}
		else if (keyPressed->code == sf::Keyboard::Key::Escape)
		{
			if (m_game && m_game->hasScene("menu"))
			{
				m_game->changeScene("menu");
				return;
			}
		}
		else if (keyPressed->code == sf::Keyboard::Key::Tab || keyPressed->code == sf::Keyboard::Key::H)
		{
			if (m_hud)
				m_hud->cycleLevel();
		}

		if (!m_paused)
		{
			if (m_camera)
			{
				if (keyPressed->code == sf::Keyboard::Key::W)
					m_camera->cInput.up = true;
				if (keyPressed->code == sf::Keyboard::Key::S)
					m_camera->cInput.down = true;
				if (keyPressed->code == sf::Keyboard::Key::A)
					m_camera->cInput.left = true;
				if (keyPressed->code == sf::Keyboard::Key::D)
					m_camera->cInput.right = true;
			}
			if (m_map)
			{
				if (keyPressed->code == sf::Keyboard::Key::M)
					m_map->setSeed();
				if (keyPressed->code == sf::Keyboard::Key::G)
					m_map->setDebugWireFrame(true);
			}
			if (m_entity_manager && keyPressed->code == sf::Keyboard::Key::Num1)
			{
				m_entity_manager->addEntity(EntityType::Human_Generic);
			}
		}
	}

	if (const auto* keyReleased = event.getIf<sf::Event::KeyReleased>())
	{
		if (!m_paused)
		{
			if (m_camera)
			{
				switch (keyReleased->code)
				{
				case sf::Keyboard::Key::W:
					m_camera->cInput.up = false;
					break;
				case sf::Keyboard::Key::S:
					m_camera->cInput.down = false;
					break;
				case sf::Keyboard::Key::A:
					m_camera->cInput.left = false;
					break;
				case sf::Keyboard::Key::D:
					m_camera->cInput.right = false;
					break;
				default: break;
				}
			}
			if (m_map && keyReleased->code == sf::Keyboard::Key::G)
			{
				m_map->setDebugWireFrame(false);
			}
		}
	}

	// TEXT INPUT LOGIC
	if (const auto* textEntered = event.getIf<sf::Event::TextEntered>())
	{
		if (m_hud)
			m_hud->input(*textEntered);
	}

	// MOUSE BUTTONS LOGIC
	if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>())
	{
		if (!m_paused && m_game)
		{
			switch (mousePressed->button)
			{
			case sf::Mouse::Button::Left:
			{
				auto pixel = sf::Mouse::getPosition(m_game->getWindow());

				if (m_hud)
				{
					sf::Vector2f guiPos = m_game->getWindow().mapPixelToCoords(pixel, m_hud->getCamera());
					m_hud->input(*mousePressed, guiPos);
				}

				if (m_camera && m_map)
				{
					sf::Vector2f worldPos = m_game->getWindow().mapPixelToCoords(pixel, m_camera->getCamera());
					if (m_hud)
						m_hud->infoBox(m_map->getPositionInfo(static_cast<sf::Vector2i>(worldPos)));

					// Map edit
					m_map->setTileColor(static_cast<sf::Vector2i>(worldPos), Elements::test);
				}
				break;
			}
			default: break;
			}
		}
	}

	// MOUSE CLICK RELEASED
	if (const auto* mouseReleased = event.getIf<sf::Event::MouseButtonReleased>())
	{
		if (!m_paused && m_game && m_hud)
		{
			if (mouseReleased->button == sf::Mouse::Button::Left)
			{
				auto pixel = sf::Mouse::getPosition(m_game->getWindow());
				sf::Vector2f mouseHudPos = m_game->getWindow().mapPixelToCoords(pixel, m_hud->getCamera());
				m_hud->input(*mouseReleased, mouseHudPos);
			}
		}
	}

	// MOUSE MOVING
	if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>())
	{
		if (!m_paused && m_game && m_hud)
		{
			auto pixel = sf::Mouse::getPosition(m_game->getWindow());
			sf::Vector2f mouseHudPos = m_game->getWindow().mapPixelToCoords(pixel, m_hud->getCamera());
			m_hud->input(*mouseMoved, mouseHudPos);
		}
	}

	// MOUSE WHEEL LOGIC
	if (const auto* mouseWheel = event.getIf<sf::Event::MouseWheelScrolled>())
	{
		if (!m_paused && m_camera)
		{
			if (mouseWheel->delta > 0)
				m_camera->zoomIn();
			else
				m_camera->zoomOut();
		}
	}
}
