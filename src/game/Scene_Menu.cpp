#include <pch.h>

#include "Scene_Menu.h"
#include "Scene_Play.h"
#include "Game.h"

namespace
{
// The keyboard-navigable entries. The HUD buttons are the mouse path; these
// are the keyboard equivalent and the actions they map to.
const std::vector<std::string> kMenuOptions{
	"Start Simulation", "Randomize World", "Quit Game"
};
} // namespace

Scene_Menu::Scene_Menu(Game* game, const sf::Font& font, const nlohmann::json& data,
                       std::shared_ptr<MapGenerator> previewMap)
	: Scene(game)
	, m_font(font)
	, m_preview_map(std::move(previewMap))
	, m_titleText(font)
	, m_options(kMenuOptions)
{
	const float width = static_cast<float>(data["window"]["width"]);
	const float height = static_cast<float>(data["window"]["height"]);
	m_width = width;
	m_height = height;

	m_view.setSize(sf::Vector2f(width, height));
	m_view.setCenter(sf::Vector2f(width / 2.f, height / 2.f));

	m_titleText.setString("ONE OF TWENTY");
	m_titleText.setCharacterSize(36);
	m_titleText.setFillColor(sf::Color::White);
	m_titleText.setPosition(sf::Vector2f(width / 2.f - 140.f, 40.f));

	m_camera = std::make_unique<Camera>(static_cast<int>(width), static_cast<int>(height));

	// The interactive menu HUD: terrain sliders, presets, seed, start time,
	// street level and the Start/Quit buttons. Loaded from the menu file so the
	// in-run HUD (time controls only) stays free of the pre-simulation controls.
	const std::string menu_file = data.value("menu", nlohmann::json::object()).value("file", std::string{});
	if (!menu_file.empty())
	{
		m_hud = std::make_unique<Hud>(font, m_preview_map, menu_file, width, height);
		m_hud->init();
		// `init` installs the terrain/preset callbacks declared by the config.
		// The menu-only actions live here, next to the scene that owns them.
		m_hud->registerButtonCallback("start_simulation", [this]() {
			if (m_game)
				m_game->startNewGame(collectSettings());
		});
		m_hud->registerButtonCallback("quit_game", [this]() {
			if (m_game)
				m_game->quit();
		});
		m_hud->registerButtonCallback("street_level", [this]() {
			auto& s = m_hud->settings();
			s.street_level = (s.street_level % 3) + 1;
			m_hud->applySettings();
		});

		// Seed the settings from the config so a run built without touching a
		// control still carries the right catalogs.
		auto& settings = m_hud->settings();
		settings.entity_file = data["entity"]["file"];
		settings.buildings_file = data.value("buildings", nlohmann::json::object()).value("file", std::string{ "config/buildings.json" });
		if (m_preview_map)
		{
			settings.map = m_preview_map->getConfig();
			settings.seed = m_preview_map->getSeed();
		}
		m_hud->applySettings();
	}
}

Scene_Menu::Scene_Menu(Game* game, const sf::Font& font, float width, float height)
	: Scene(game)
	, m_font(font)
	, m_titleText(font)
	, m_options(kMenuOptions)
	, m_width(width)
	, m_height(height)
{
	m_view.setSize(sf::Vector2f(width, height));
	m_view.setCenter(sf::Vector2f(width / 2.f, height / 2.f));

	m_titleText.setString("ONE OF TWENTY");
	m_titleText.setCharacterSize(36);
	m_titleText.setFillColor(sf::Color::White);
	m_titleText.setPosition(sf::Vector2f(width / 2.f - 140.f, height / 4.f));
}

void Scene_Menu::onEnter()
{
	// Returning from a run shows the choices it was started with, so the player
	// can tweak them and go again. The first entry has no run yet, so the preview
	// keeps its own defaults.
	if (m_game && m_game->hasRun() && m_hud)
		loadSettings(m_game->getSettings());
}

void Scene_Menu::update(float /*deltaTime*/)
{
	++m_frame;
}

void Scene_Menu::selectNext()
{
	if (!m_options.empty())
		m_selectedIndex = (m_selectedIndex + 1) % m_options.size();
}

void Scene_Menu::selectPrev()
{
	if (!m_options.empty())
		m_selectedIndex = (m_selectedIndex + m_options.size() - 1) % m_options.size();
}

GameSettings Scene_Menu::collectSettings() const
{
	GameSettings settings;
	if (m_hud)
		settings = m_hud->settings();

	// The preview map is authoritative for terrain and seed: the tuners drive it
	// directly, and its live config is exactly what the player saw.
	if (m_preview_map)
	{
		settings.map = m_preview_map->getConfig();
		settings.seed = m_preview_map->getSeed();
	}

	return settings;
}

void Scene_Menu::loadSettings(const GameSettings& settings)
{
	if (m_hud)
	{
		m_hud->settings() = settings;
		m_hud->applySettings();
	}
	if (m_preview_map && settings.seed != m_preview_map->getSeed())
	{
		m_preview_map->setSeed(settings.seed);
		m_preview_map->m_reset = true;
	}
}

void Scene_Menu::chooseSelected()
{
	if (!m_game)
		return;

	switch (m_selectedIndex)
	{
	case 0: // Start Simulation
		m_game->startNewGame(collectSettings());
		break;
	case 1: // Randomize World
		if (m_preview_map)
		{
			m_preview_map->setSeed();
			m_preview_map->m_reset = true;
		}
		break;
	case 2: // Quit Game
		m_game->quit();
		break;
	default: break;
	}
}

void Scene_Menu::sRender(sf::RenderTarget& target)
{
	// The preview world first, then the controls over it, so the player tunes
	// the terrain while seeing the change.
	if (m_preview_map && m_camera)
	{
		target.setView(m_camera->getCamera());
		m_preview_map->render(m_camera->getWorldBounds(), target);
	}

	target.setView(m_view);
	if (m_hud)
		m_hud->render(target);

	target.draw(m_titleText);
}

void Scene_Menu::sUserInput(const sf::Event& event)
{
	if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>())
	{
		if (keyPressed->code == sf::Keyboard::Key::W || keyPressed->code == sf::Keyboard::Key::Up)
		{
			selectPrev();
		}
		else if (keyPressed->code == sf::Keyboard::Key::S || keyPressed->code == sf::Keyboard::Key::Down)
		{
			selectNext();
		}
		else if (keyPressed->code == sf::Keyboard::Key::Enter || keyPressed->code == sf::Keyboard::Key::Space)
		{
			chooseSelected();
		}
		else if (keyPressed->code == sf::Keyboard::Key::R)
		{
			if (m_preview_map)
			{
				m_preview_map->setSeed();
				m_preview_map->m_reset = true;
			}
		}
		else if (keyPressed->code == sf::Keyboard::Key::Escape)
		{
			if (m_game)
				m_game->quit();
		}
	}

	// The HUD handles the mouse path; it exists only in the full menu.
	if (!m_hud || !m_game)
		return;

	auto& window = m_game->getWindow();

	if (const auto* mousePressed = event.getIf<sf::Event::MouseButtonPressed>())
	{
		if (mousePressed->button == sf::Mouse::Button::Left)
		{
			const auto pixel = sf::Mouse::getPosition(window);
			const sf::Vector2f guiPos = ViewMath::worldFromPixel(window, pixel, m_hud->getCamera());
			m_hud->input(*mousePressed, guiPos);
		}
	}

	if (const auto* mouseReleased = event.getIf<sf::Event::MouseButtonReleased>())
	{
		if (mouseReleased->button == sf::Mouse::Button::Left)
		{
			const auto pixel = sf::Mouse::getPosition(window);
			const sf::Vector2f guiPos = ViewMath::worldFromPixel(window, pixel, m_hud->getCamera());
			m_hud->input(*mouseReleased, guiPos);
		}
	}

	if (const auto* mouseMoved = event.getIf<sf::Event::MouseMoved>())
	{
		const auto pixel = sf::Mouse::getPosition(window);
		const sf::Vector2f guiPos = ViewMath::worldFromPixel(window, pixel, m_hud->getCamera());
		m_hud->input(*mouseMoved, guiPos);
	}

	if (const auto* textEntered = event.getIf<sf::Event::TextEntered>())
	{
		m_hud->input(*textEntered);
	}
}