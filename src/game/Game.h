#pragma once

#include "Scene.h"

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

	float													m_deltaTime{ 0.f };
	int														m_currentFrame{ 0 };
	bool													m_running{ true };

	void sUserInput();
	void sRender();

public:
	Game(const std::string& path);

	void run();
	void quit() { m_running = false; }
	bool isRunning() const { return m_running; }

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
	const nlohmann::json& getConfig() const { return m_config; }
	float getDeltaTime() const { return m_deltaTime; }
	int getCurrentFrame() const { return m_currentFrame; }
};

