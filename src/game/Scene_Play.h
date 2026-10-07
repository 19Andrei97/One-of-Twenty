#pragma once

#include "Scene.h"
#include "../entity_manager/EntityManager.h"
#include "../map_generator/MapGenerator.h"
#include "../camera/Camera.h"
#include "../hud/Hud.h"

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

	sf::Vector2i					m_current_position{ 0, 0 };
	float							m_deltaTime{ 0.f };
	int								m_currentFrame{ 0 };

	void sMovement();
	void sCollision();
	// Push the current population/stockpile readout into the HUD.
	void refreshStats();

public:
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
};
