#pragma once

#include "Scene.h"
#include "../helpers/GameSettings.h"
#include "../map_generator/MapGenerator.h"

#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

class Game
{
	sf::RenderWindow										m_window;
	sf::Clock												m_clock;
	sf::Font												m_font;
	nlohmann::json											m_config;

	std::unordered_map<std::string, std::shared_ptr<Scene>>	m_scenes;
	std::shared_ptr<Scene>									m_currentScene;
	std::string												m_currentSceneName;

	// The choices the current/last run was started with, and the preview
	// world the menu tunes. The preview is shared with the menu so the
	// terrain sliders drive the very map that is drawn.
	GameSettings                                                                             m_settings;
	std::shared_ptr<MapGenerator>                                    m_preview_map;
	int                                                                                                             m_preview_frames{ 0 };

	float													m_deltaTime{ 0.f };
	int														m_currentFrame{ 0 };
	bool													m_running{ true };
	bool                                                                                                             m_hasRun{ false };

	void sUserInput();
	void sRender();

public:
	Game(const std::string& path);

	void run();
	void quit() { m_running = false; }
	bool isRunning() const { return m_running; }

	// Start a run from the menu's choices: build a fresh play scene from the
	// settings (a new world each time) and switch to it. The preview and the
	// menu keep the same choices so returning re-shows what was played.
	void startNewGame(const GameSettings& settings);

	// The currently selected/played settings.
	const GameSettings& getSettings() const { return m_settings; }
	bool hasRun() const { return m_hasRun; }
	std::shared_ptr<MapGenerator> getPreviewMap() const { return m_preview_map; }

	// Scene management
	void registerScene(const std::string& name, std::shared_ptr<Scene> scene);
	void changeScene(const std::string& name, std::shared_ptr<Scene> scene = nullptr, bool endCurrent = false);
	std::shared_ptr<Scene> getCurrentScene() const { return m_currentScene; }
	const std::string& getCurrentSceneName() const { return m_currentSceneName; }
	std::shared_ptr<Scene> getScene(const std::string& name) const;
	bool hasScene(const std::string& name) const;

	// Accessors
	sf::RenderWindow& getWindow() { return m_window; }
	const sf::RenderWindow& getWindow() const { return m_window; }
	sf::Font& getFont() { return m_font; }
	const sf::Font& getFont() const { return m_font; }
	float getDeltaTime() const { return m_deltaTime; }
};

