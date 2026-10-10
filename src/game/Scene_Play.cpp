#include <pch.h>

#include "Scene_Play.h"
#include "Game.h"

Scene_Play::Scene_Play(Game* game, const sf::Font& font, const nlohmann::json& data)
	: Scene(game)
{
	// IN GAME CLOCK
	LOG_DEBUG("Creating in Game Clock.");
	m_game_clock = std::make_shared<GameClock>();
	const auto time_cfg = data.value("time", nlohmann::json::object());
	m_game_clock->setSpeedIndex(time_cfg.value("speed_index", 1)); // 1 hour/s
	// Start mid-morning: a midnight start would put the settlement to sleep for
	// its first hours, before it has found work or a farm.
	m_game_clock->setTime(time_cfg.value("start_hour", 8), time_cfg.value("start_minute", 0));
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
	m_hud->setClock(m_game_clock);
	m_hud->init();
	registerTimeControls();

	// CAMERA
	LOG_DEBUG("Creating Camera.");
	m_camera = std::make_unique<Camera>(static_cast<float>(data["window"]["width"]), static_cast<float>(data["window"]["height"]));

	// ENTITIES MANAGER
	LOG_DEBUG("Creating Entities Manager.");
	m_entity_manager = std::make_unique<EntityManager>(font, m_map, m_game_clock, m_deltaTime, data["entity"]["file"],
	                                                   data.value("buildings", nlohmann::json::object()).value("file", "config/buildings.json"));
	// Color building tiles with the catalog's colors, so a structure renders as the
	// JSON declares rather than a palette entry in map_data.json.
	m_map->applyBuildingColors(m_entity_manager->buildings().colors());
	m_entity_manager->seedPopulation();
}

void Scene_Play::update(float deltaTime)
{
	m_deltaTime = deltaTime;
	m_game_clock->update(m_deltaTime);

	// The camera is view-only, so it keeps moving while the simulation is
	// paused: a player can still look around a frozen world.
	sCamera();

	if (!m_paused)
	{
		sMovement();
		sCollision();
	}

	refreshStats();
	refreshTimeReadout();

	++m_currentFrame;
}

void Scene_Play::refreshStats()
{
	if (!m_hud || !m_entity_manager || !m_game_clock)
		return;

	m_hud->stats({
		m_game_clock->formatDate(),
		"Population: " + std::to_string(m_entity_manager->population()) + " / " + std::to_string(m_entity_manager->maxPopulation()),
		"Births: " + std::to_string(m_entity_manager->births()),
		"Deaths: " + std::to_string(m_entity_manager->deaths()),
		"Food: " + std::to_string(m_entity_manager->good(Goods::Good::Food))
			+ "   Wood: " + std::to_string(m_entity_manager->good(Goods::Good::Wood)),
		"Stone: " + std::to_string(m_entity_manager->good(Goods::Good::Stone))
			+ "   Clay: " + std::to_string(m_entity_manager->good(Goods::Good::Clay))
			+ "   Iron: " + std::to_string(m_entity_manager->good(Goods::Good::Iron)),
		"Planks: " + std::to_string(m_entity_manager->good(Goods::Good::Planks))
			+ "   Tools: " + std::to_string(m_entity_manager->good(Goods::Good::Tools)),
		"Buildings: " + std::to_string(m_entity_manager->completedBuildingCount())
			+ "/" + std::to_string(m_entity_manager->buildingCount()) + " built"
			+ "   Houses: " + std::to_string(m_entity_manager->countOfElement(Elements::house))
			+ "   Roads: " + std::to_string(m_entity_manager->countOfElement(Elements::road))
			+ "   " + (m_entity_manager->hasCityCenter() ? "City Center up" : "No city center"),
		"Gathers: " + std::to_string(m_entity_manager->gathersCompleted()),
		"Explored: " + std::to_string(m_entity_manager->knowledge().exploredCells())
			+ " cells   Known: " + std::to_string(m_entity_manager->knowledge().knownLocations()),
		lastEventLine(),
	});
}

std::string Scene_Play::lastEventLine() const
{
	if (!m_entity_manager)
		return {};

	// Prefer the most recent recorded event; fall back to the run's compact
	// summary so the panel always shows something current.
	const auto& events = m_entity_manager->events();
	if (!events.empty())
		return "Event: " + events.events().back().format();
	return "Run: " + m_entity_manager->runSummary().format();
}

void Scene_Play::refreshTimeReadout()
{
	if (!m_hud || !m_game_clock)
		return;

	const std::string status = m_paused ? "Paused" : m_game_clock->getSpeedLabel();
	m_hud->timeReadout({
		m_game_clock->formatDate() + "  " + m_game_clock->formatClock(),
		"Speed: " + status,
	});

	// The pause control doubles as play; keep its label in step with the state.
	m_hud->setButtonLabel("time_pause", m_paused ? "Play" : "Pause");
}

void Scene_Play::registerTimeControls()
{
	if (!m_hud || !m_game_clock)
		return;

	// Slower/faster act on the clock directly (bound by the HUD). Pause is
	// overridden here so the scene's own paused flag, which also gates
	// movement and collision, stays in step with the clock.
	m_hud->registerButtonCallback("time_pause", [this]() { togglePaused(); });
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
}

void Scene_Play::sCamera()
{
	if (!m_camera)
		return;

	const float step = m_camera->getVelocity() * m_deltaTime;
	if (m_camera->cInput.up)
	{
		m_camera->move(0, -step);
		m_current_position.y -= static_cast<int>(step);
	}
	else if (m_camera->cInput.down)
	{
		m_camera->move(0, step);
		m_current_position.y += static_cast<int>(step);
	}
	if (m_camera->cInput.left)
	{
		m_camera->move(-step, 0);
		m_current_position.x -= static_cast<int>(step);
	}
	else if (m_camera->cInput.right)
	{
		m_camera->move(step, 0);
		m_current_position.x += static_cast<int>(step);
	}
}

void Scene_Play::sCollision()
{
        if (m_entity_manager)
                m_entity_manager->resolveCollisions();
}

void Scene_Play::updateHover()
{
	if (!m_entity_manager || !m_camera || !m_game)
	{
		if (m_entity_manager)
			m_entity_manager->clearHoverWorld();
		return;
	}

	const sf::Vector2f worldPos =
		m_game->getWindow().mapPixelToCoords(m_mouse_pixel, m_camera->getCamera());
	m_entity_manager->setHoverWorld(static_cast<sf::Vector2i>(worldPos));
}

void Scene_Play::sRender(sf::RenderTarget& target)
{
	// The pointer's world position is resolved once per frame, right before the
	// entities are drawn, so the hover readout tracks the cursor and the camera.
	updateHover();

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
		if (keyPressed->code == sf::Keyboard::Key::P || keyPressed->code == sf::Keyboard::Key::Space)
		{
			togglePaused();
		}
		else if (keyPressed->code == sf::Keyboard::Key::LBracket)
		{
			if (m_game_clock)
				m_game_clock->slower();
		}
		else if (keyPressed->code == sf::Keyboard::Key::RBracket)
		{
			if (m_game_clock)
				m_game_clock->faster();
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
		else if (keyPressed->code == sf::Keyboard::Key::R)
		{
			// Reload the tuning without a rebuild. A malformed file keeps the
			// previous config and logs the error rather than losing it.
			if (m_entity_manager)
			{
				try { m_entity_manager->reloadConfig(); LOG_INFO("Reloaded entity config."); }
				catch (const std::exception& e) { LOG_ERROR("Reload failed: {}", e.what()); }
			}
		}

		// Camera keys are view-only, so they register even while paused.
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

		if (!m_paused)
		{
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
		// Release camera keys even while paused, so a key held across a pause does
		// not stick down.
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

		if (!m_paused)
		{
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
		if (m_game)
		{
			switch (mousePressed->button)
			{
			case sf::Mouse::Button::Left:
			{
				auto pixel = sf::Mouse::getPosition(m_game->getWindow());
				m_mouse_pixel = pixel;

				// The HUD stays live while paused, so its Pause/Play and speed
				// controls can always be clicked.
				if (m_hud)
				{
					sf::Vector2f guiPos = m_game->getWindow().mapPixelToCoords(pixel, m_hud->getCamera());
					m_hud->input(*mousePressed, guiPos);
				}

				// Left click is UI-only for now: it must not edit the map.
				break;
			}
			default: break;
			}
		}
	}

	// MOUSE CLICK RELEASED
	if (const auto* mouseReleased = event.getIf<sf::Event::MouseButtonReleased>())
	{
		if (m_game && m_hud)
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
		if (m_game)
		{
			auto pixel = sf::Mouse::getPosition(m_game->getWindow());
			m_mouse_pixel = pixel;

			if (m_hud)
			{
				sf::Vector2f mouseHudPos = m_game->getWindow().mapPixelToCoords(pixel, m_hud->getCamera());
				m_hud->input(*mouseMoved, mouseHudPos);
			}

			// Tile info follows the pointer, so the readout always shows whatever
			// tile the cursor is over. Kept live while paused since the camera can
			// still be panned.
			if (m_camera && m_map)
			{
				sf::Vector2f worldPos = m_game->getWindow().mapPixelToCoords(pixel, m_camera->getCamera());
				if (m_hud)
					m_hud->infoBox(m_map->getPositionInfo(static_cast<sf::Vector2i>(worldPos)));
			}
		}
	}

	// MOUSE WHEEL LOGIC
	if (const auto* mouseWheel = event.getIf<sf::Event::MouseWheelScrolled>())
	{
		if (m_camera)
		{
			m_mouse_pixel = sf::Mouse::getPosition(m_game->getWindow());
			if (mouseWheel->delta > 0)
				m_camera->zoomIn();
			else
				m_camera->zoomOut();
		}
	}
}
