#pragma once

#include "Scene.h"
#include "../camera/Camera.h"
#include "../hud/Hud.h"
#include "../map_generator/MapGenerator.h"
#include "../helpers/GameSettings.h"

#include <SFML/Graphics.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// The pre-simulation menu. It owns a *preview* map the player can see and tune
// (terrain sliders, presets, seed, start time, street level) and a HUD that
// hosts those controls. Starting a run captures the preview's config into a
// GameSettings and asks the game to build a fresh Scene_Play from it, so the
// world the player previewed is the world they play.
class Scene_Menu : public Scene
{
	const sf::Font&				m_font;
	// The preview world. Owned by Game and shared here so the tuners drive the
	// same generator the render draws.
	std::shared_ptr<MapGenerator>	m_preview_map;
	std::unique_ptr<Camera>		m_camera;
	std::unique_ptr<Hud>		m_hud;

	sf::Text				m_titleText;
	sf::View				m_view;

	// Keyboard-navigable entries, kept beside the HUD buttons so the menu works
	// with the keyboard as well as the mouse.
	std::vector<std::string>	m_options;
	size_t					m_selectedIndex{ 0 };

	int					m_frame{ 0 };

	float					m_width{ 0.f };
	float					m_height{ 0.f };

public:
	// Full menu: hosts the interactive HUD from config and shares the game's
	// preview generator.
	Scene_Menu(Game* game, const sf::Font& font, const nlohmann::json& config,
	           std::shared_ptr<MapGenerator> previewMap);

	// Lightweight constructor for tests: no preview map and no HUD, only the
	// keyboard navigation behaviour.
	Scene_Menu(Game* game, const sf::Font& font, float width, float height);

	void onEnter() override;
	void update(float deltaTime) override;
	void sRender(sf::RenderTarget& target) override;
	void sUserInput(const sf::Event& event) override;

	void selectNext();
	void selectPrev();
	void chooseSelected();

	// Collect the current choices into a run: the preview's terrain plus the
	// HUD's seed/time/street-level settings.
	GameSettings collectSettings() const;
	// Push a settings object into the HUD controls (silently) and re-seed the
	// preview. Used when a run returns to the menu so the menu shows what was
	// last played.
	void loadSettings(const GameSettings& settings);

	size_t getSelectedIndex() const { return m_selectedIndex; }
	const std::vector<std::string>& getOptions() const { return m_options; }
	Hud* getHud() const { return m_hud.get(); }
	std::shared_ptr<MapGenerator> getPreviewMap() const { return m_preview_map; }
};
