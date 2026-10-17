#pragma once

#include "Scene.h"
#include "../entity_manager/EntityManager.h"
#include "../map_generator/MapGenerator.h"
#include "../camera/Camera.h"
#include "../hud/Hud.h"
#include "../helpers/GameSettings.h"

#include <SFML/Graphics.hpp>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

class Scene_Play : public Scene
{
	std::unique_ptr<Camera>			m_camera;
	std::unique_ptr<EntityManager>	m_entity_manager;
	std::unique_ptr<Hud>			m_hud;
	std::shared_ptr<GameClock>		m_game_clock;
	std::shared_ptr<MapGenerator>	m_map;

	float							m_deltaTime{ 0.f };
	int								m_currentFrame{ 0 };

	// The choices this run was started with (terrain, seed, time, street
	// level). Kept so the run is self-describing.
	GameSettings                                    m_settings;

	// Shared construction for both constructors.
	void init(const sf::Font& font, const GameSettings& settings, const nlohmann::json& config);

	// Latest pointer position in window pixels, refreshed on move/press/wheel so
	// the hover hit test has a position to work from even before the first move
	// event of a frame.
	sf::Vector2i				m_mouse_pixel{ 0, 0 };

	// Resolve the world position under the pointer and hand it to the entity
	// manager, which draws a readout only for the entity under it.
	void updateHover();

	void sMovement();
	void sCollision();
	// Pan the camera from the keyboard. Kept separate from sMovement so the
	// camera still moves while the simulation is paused.
	void sCamera();
	// Push the current population/stockpile readout into the HUD.
	void refreshStats();
	// The most recent event (or the run summary when none yet), for the HUD.
	std::string lastEventLine() const;
	// Push the clock readout (date, time, speed) into the HUD.
	void refreshTimeReadout();
	// Bind the HUD's time-management buttons to the clock.
	void registerTimeControls();

public:
	// Build a run from the menu's captured settings. The world is rebuilt
	// from settings.map rather than sharing the menu's preview, so leaving and
	// re-entering a run never carries hidden state across.
	Scene_Play(Game* game, const sf::Font& font, const GameSettings& settings, const nlohmann::json& config);

	// Convenience constructor for tools and tests that only have the config:
	// it derives the settings from config["map"]["file"], config["time"] and
	// the entity/building paths.
	Scene_Play(Game* game, const sf::Font& font, const nlohmann::json& config);

	void update(float deltaTime) override;
	void sRender(sf::RenderTarget& target) override;
	void sUserInput(const sf::Event& event) override;

	void setPaused(bool paused) override;
	void togglePaused() override;

	Camera* getCamera() const { return m_camera.get(); }
	EntityManager* getEntityManager() const { return m_entity_manager.get(); }
	Hud* getHud() const { return m_hud.get(); }
	std::shared_ptr<GameClock> getGameClock() const { return m_game_clock; }
	std::shared_ptr<MapGenerator> getMap() const { return m_map; }
	const GameSettings& getSettings() const { return m_settings; }
};
