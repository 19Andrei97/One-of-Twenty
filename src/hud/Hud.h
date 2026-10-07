#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../components/Components_HUD.h"
#include "../map_generator/MapGenerator.h"

enum class HudLevelMode
{
	Layered, // Elements with level <= currentLevel are visible
	Exact    // Only elements with level == currentLevel are visible
};

class Hud
{
public:
	using ButtonCallback = std::function<void()>;
	using SliderCallback = std::function<void(float)>;
	using InputCallback  = std::function<void(float)>;

	struct ButtonItem
	{
		std::unique_ptr<CButton> widget;
		int level{ 0 };
		std::string functionName;
	};

	struct SliderItem
	{
		std::unique_ptr<CSlider> widget;
		int level{ 0 };
		std::string functionName;
	};

	struct InputItem
	{
		std::unique_ptr<CInputBox> widget;
		int level{ 0 };
		std::string functionName;
	};

private:
	sf::Font&						m_font;
	std::string						m_file;
	sf::View						m_camera;
	std::shared_ptr<MapGenerator>	m_map;

	int								m_currentLevel{ 0 };
	int								m_maxLevel{ 0 };
	HudLevelMode					m_levelMode{ HudLevelMode::Layered };

	// ELEMENTS
	std::vector<ButtonItem>			buttons;
	std::vector<InputItem>			inputs;
	std::vector<SliderItem>			sliders;
	std::unique_ptr<CInfoBox>		info_box;

	std::unordered_map<std::string, ButtonCallback> m_buttonCallbacks;
	std::unordered_map<std::string, SliderCallback> m_sliderCallbacks;
	std::unordered_map<std::string, InputCallback>  m_inputCallbacks;

	void registerDefaultCallbacks();

public:

	// CONSTRUCTOR & INITIATOR
	Hud(sf::Font& font, std::shared_ptr<MapGenerator> map, const std::string& file, float window_x, float window_y);

	void init();

	// GETTERS & SETTERS
	const sf::View& getCamera() const { return m_camera; }
	sf::View& getCamera() { return m_camera; }

	int getLevel() const { return m_currentLevel; }
	void setLevel(int level) { m_currentLevel = std::max(0, level); }
	int getMaxLevel() const { return m_maxLevel; }
	void setLevelMode(HudLevelMode mode) { m_levelMode = mode; }
	HudLevelMode getLevelMode() const { return m_levelMode; }

	void nextLevel();
	void prevLevel();
	void cycleLevel();

	bool isLevelVisible(int level) const;

	// CALLBACK REGISTRATION
	void registerButtonCallback(const std::string& name, ButtonCallback callback);
	void registerSliderCallback(const std::string& name, SliderCallback callback);
	void registerInputCallback(const std::string& name, InputCallback callback);

	bool hasButtonCallback(const std::string& name) const;
	bool hasSliderCallback(const std::string& name) const;
	bool hasInputCallback(const std::string& name) const;

	// ELEMENT ACCESSORS
	const std::vector<ButtonItem>& getButtons() const { return buttons; }
	const std::vector<SliderItem>& getSliders() const { return sliders; }
	const std::vector<InputItem>& getInputs() const { return inputs; }

	size_t getButtonCount() const { return buttons.size(); }
	size_t getSliderCount() const { return sliders.size(); }
	size_t getInputCount() const { return inputs.size(); }

	size_t getVisibleButtonCount() const;
	size_t getVisibleSliderCount() const;
	size_t getVisibleInputCount() const;

	// SYSTEMS
	void render(sf::RenderTarget& window);

	// HUD ACCESSORIES
	void infoBox(std::vector<std::string> info);

	// INPUTS
	void input(const sf::Event::TextEntered& event);
	void input(const sf::Event::MouseButtonPressed& event, const sf::Vector2f& mouse_position);
	void input(const sf::Event::MouseButtonReleased& event, const sf::Vector2f& mouse_position);
	void input(const sf::Event::MouseMoved& event, const sf::Vector2f& mouse_position);
};