

#include <pch.h>

#include "Game.h"
#include "Scene_Play.h"
#include "Scene_Menu.h"

// GAME FLOW ////////////////////////////////////////////////////////////

Game::Game(const std::string& path)
{
	m_config = loadJsonFile(path);

	// LOGGER
	Logger::init(m_config["logger"]["file"], m_config["logger"].value("level", std::string{ "debug" }));

	// WINDOW AND FRAME
	sf::State state;

	if (m_config["window"]["fullscreen"])
		state = sf::State::Fullscreen;
	else
		state = sf::State::Windowed;

	m_window.create(sf::VideoMode({ m_config["window"]["width"], m_config["window"]["height"] }), "One Of Twenty", state);
	m_window.setFramerateLimit(m_config["window"]["frames"]);

	// TEXT AND FONT
	LOG_DEBUG("Opening font file.");
	if (!m_font.openFromFile(m_config["font"]["file"])) {
		std::cerr << "Could not load font!\n";
	}

	// SCENES
	LOG_DEBUG("Registering scenes.");
	auto playScene = std::make_shared<Scene_Play>(this, m_font, m_config);
	registerScene("play", playScene);

	auto menuScene = std::make_shared<Scene_Menu>(
		this, 
		m_font, 
		static_cast<float>(m_config["window"]["width"]), 
		static_cast<float>(m_config["window"]["height"])
	);
	registerScene("menu", menuScene);

	// Start in the play scene
	changeScene("play");
}

void Game::registerScene(const std::string& name, std::shared_ptr<Scene> scene)
{
	if (scene)
		scene->setGame(this);
	m_scenes[name] = scene;
}

void Game::changeScene(const std::string& name, std::shared_ptr<Scene> scene, bool endCurrent)
{
	if (scene)
	{
		registerScene(name, scene);
	}

	auto it = m_scenes.find(name);
	if (it != m_scenes.end())
	{
		if (m_currentScene)
		{
			m_currentScene->onExit();
			if (endCurrent)
				m_currentScene->end();
		}

		m_currentSceneName = name;
		m_currentScene = it->second;
		if (m_currentScene)
			m_currentScene->onEnter();
	}
}

std::shared_ptr<Scene> Game::getScene(const std::string& name) const
{
	auto it = m_scenes.find(name);
	if (it != m_scenes.end())
		return it->second;
	return nullptr;
}

bool Game::hasScene(const std::string& name) const
{
	return m_scenes.find(name) != m_scenes.end();
}

void Game::run()
{
	while (m_running && m_window.isOpen())
	{
		m_deltaTime = m_clock.restart().asSeconds();

		// Clamp the frame delta: the first frame measures the whole startup
		// (window, fonts, map generation), and a hitch or a dragged window can be
		// seconds long. Feeding that to the clock would jump the simulation hours
		// ahead in one step, so needs decay past the point where entities can
		// react and the settlement starves.
		constexpr float kMaxDeltaSeconds{ 0.1f };
		m_deltaTime = std::min(m_deltaTime, kMaxDeltaSeconds);

		if (m_currentScene)
		{
			m_currentScene->update(m_deltaTime);
		}

		sUserInput();
		sRender();

		++m_currentFrame;
	}
}

void Game::sUserInput()
{
	while (const std::optional event = m_window.pollEvent())
	{
		if (event->is<sf::Event::Closed>())
		{
			m_running = false;
		}

		if (m_currentScene)
		{
			m_currentScene->sUserInput(*event);
		}
	}
}

void Game::sRender()
{
	m_window.clear();

	if (m_currentScene)
	{
		m_currentScene->sRender(m_window);
	}

	m_window.display();
}


