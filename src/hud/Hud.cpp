

#include <pch.h>

#include "Hud.h"

Hud::Hud(sf::Font& font, std::shared_ptr<MapGenerator> map, const std::string& file, float window_x, float window_y)
	: m_font(font)
	, m_map(map)
	, m_file(file)
{
	m_camera.setSize(sf::Vector2f(window_x, window_y));
	m_camera.setCenter(sf::Vector2f(window_x / 2.f, window_y / 2.f));
}

void Hud::registerDefaultCallbacks()
{
	// Map actions
	registerButtonCallback("random", [this]() {
		if (m_map)
		{
			m_map->setSeed();
			m_map->m_reset = true;
		}
	});

	registerButtonCallback("random_seed", [this]() {
		if (m_map)
		{
			m_map->setSeed();
			m_map->m_reset = true;
		}
	});

	registerButtonCallback("cycle_hud", [this]() {
		cycleLevel();
	});

	registerButtonCallback("cycle_hud_level", [this]() {
		cycleLevel();
	});

	registerButtonCallback("next_hud_level", [this]() {
		nextLevel();
	});

	// Sliders
	registerSliderCallback("continent_frequency", [this](float val) {
		if (m_map)
		{
			m_map->setContFreq(val);
			m_map->m_reset = true;
		}
	});
	registerSliderCallback("cont_freq", [this](float val) {
		if (m_map)
		{
			m_map->setContFreq(val);
			m_map->m_reset = true;
		}
	});

	registerSliderCallback("continent_multiplier", [this](float val) {
		if (m_map)
		{
			m_map->setContMult(val);
			m_map->m_reset = true;
		}
	});
	registerSliderCallback("cont_multiplier", [this](float val) {
		if (m_map)
		{
			m_map->setContMult(val);
			m_map->m_reset = true;
		}
	});
	registerSliderCallback("cont_mult", [this](float val) {
		if (m_map)
		{
			m_map->setContMult(val);
			m_map->m_reset = true;
		}
	});

	registerSliderCallback("warp_frequency", [this](float val) {
		if (m_map)
		{
			m_map->setWarpFreq(val);
			m_map->m_reset = true;
		}
	});
	registerSliderCallback("warp_freq", [this](float val) {
		if (m_map)
		{
			m_map->setWarpFreq(val);
			m_map->m_reset = true;
		}
	});

	registerSliderCallback("mineral_frequency", [this](float val) {
		if (m_map)
		{
			m_map->setMineralFreq(val);
			m_map->m_reset = true;
		}
	});
	registerSliderCallback("mineral_freq", [this](float val) {
		if (m_map)
		{
			m_map->setMineralFreq(val);
			m_map->m_reset = true;
		}
	});

	registerSliderCallback("mineral_multiplier", [this](float val) {
		if (m_map)
		{
			m_map->setMineralMult(val);
			m_map->m_reset = true;
		}
	});
	registerSliderCallback("mineral_mult", [this](float val) {
		if (m_map)
		{
			m_map->setMineralMult(val);
			m_map->m_reset = true;
		}
	});
}

void Hud::init()
{
	using json = nlohmann::json;

	registerDefaultCallbacks();

	if (m_file.empty())
		return;

	json data = loadJsonFile(m_file);

	for (const auto& [key, value] : data.items())
	{
		int level = value.value("level", 0);
		m_maxLevel = std::max(m_maxLevel, level);

		std::string funcName;
		if (value.contains("function") && value["function"].is_string())
		{
			funcName = value["function"].get<std::string>();
		}

		if (value["type"] == "button")
		{
			auto btn = std::make_unique<CButton>(
				static_cast<float>(value["width"]), 
				static_cast<float>(value["height"]),
				sf::Vector2f{ static_cast<float>(value["position"]["x"]), static_cast<float>(value["position"]["y"]) },
				m_font, 
				value["label"],
				sf::Color(value["color_fill"][0], value["color_fill"][1], value["color_fill"][2]),
				sf::Color(value["outline"][0], value["outline"][1], value["outline"][2]),
				static_cast<float>(value["thickness"]),
				static_cast<float>(value["text_size"])
			);

			if (!funcName.empty())
			{
				auto it = m_buttonCallbacks.find(funcName);
				if (it != m_buttonCallbacks.end() && it->second)
				{
					btn->setOnClick(it->second);
				}
			}

			buttons.push_back({ std::move(btn), level, funcName });
		}
		else if (value["type"] == "slider")
		{
			auto sld = std::make_unique<CSlider>(
				static_cast<float>(value["width"]),
				static_cast<float>(value["height"]),
				sf::Vector2f{ static_cast<float>(value["position"]["x"]), static_cast<float>(value["position"]["y"]) },
				static_cast<float>(value["minimum_value"]),
				static_cast<float>(value["maximum_value"]),
				m_font,
				value["label"],
				sf::Color(value["bar_color"][0], value["bar_color"][1], value["bar_color"][2]),
				sf::Color(value["handle_color"][0], value["handle_color"][1], value["handle_color"][2])
			);

			if (!funcName.empty())
			{
				auto it = m_sliderCallbacks.find(funcName);
				if (it != m_sliderCallbacks.end() && it->second)
				{
					sld->setOnChange(it->second);
				}
			}

			sliders.push_back({ std::move(sld), level, funcName });
		}
		else if (value["type"] == "input")
		{
			auto inp = std::make_unique<CInputBox>(
				static_cast<float>(value["width"]),
				static_cast<float>(value["height"]),
				sf::Vector2f{ static_cast<float>(value["position"]["x"]), static_cast<float>(value["position"]["y"]) },
				m_font,
				value.value("placeholder", "Enter number..."),
				sf::Color(value["color_fill"][0], value["color_fill"][1], value["color_fill"][2]),
				sf::Color(value["outline"][0], value["outline"][1], value["outline"][2]),
				static_cast<float>(value.value("thickness", 2.f)),
				static_cast<unsigned int>(value.value("text_size", 16u))
			);

			if (!funcName.empty())
			{
				auto it = m_inputCallbacks.find(funcName);
				if (it != m_inputCallbacks.end() && it->second)
				{
					inp->setOnEnter(it->second);
				}
			}

			inputs.push_back({ std::move(inp), level, funcName });
		}
	}
}

void Hud::registerButtonCallback(const std::string& name, ButtonCallback callback)
{
	m_buttonCallbacks[name] = callback;
	for (auto& b : buttons)
	{
		if (b.functionName == name && b.widget)
		{
			b.widget->setOnClick(callback);
		}
	}
}

void Hud::registerSliderCallback(const std::string& name, SliderCallback callback)
{
	m_sliderCallbacks[name] = callback;
	for (auto& s : sliders)
	{
		if (s.functionName == name && s.widget)
		{
			s.widget->setOnChange(callback);
		}
	}
}

void Hud::registerInputCallback(const std::string& name, InputCallback callback)
{
	m_inputCallbacks[name] = callback;
	for (auto& i : inputs)
	{
		if (i.functionName == name && i.widget)
		{
			i.widget->setOnEnter(callback);
		}
	}
}

bool Hud::hasButtonCallback(const std::string& name) const
{
	return m_buttonCallbacks.find(name) != m_buttonCallbacks.end();
}

bool Hud::hasSliderCallback(const std::string& name) const
{
	return m_sliderCallbacks.find(name) != m_sliderCallbacks.end();
}

bool Hud::hasInputCallback(const std::string& name) const
{
	return m_inputCallbacks.find(name) != m_inputCallbacks.end();
}

void Hud::nextLevel()
{
	if (m_currentLevel < m_maxLevel)
		++m_currentLevel;
}

void Hud::prevLevel()
{
	if (m_currentLevel > 0)
		--m_currentLevel;
}

void Hud::cycleLevel()
{
	if (m_maxLevel <= 0)
	{
		m_currentLevel = 0;
		return;
	}
	m_currentLevel = (m_currentLevel + 1) % (m_maxLevel + 1);
}

bool Hud::isLevelVisible(int level) const
{
	if (m_levelMode == HudLevelMode::Exact)
		return level == m_currentLevel;
	return level <= m_currentLevel;
}

size_t Hud::getVisibleButtonCount() const
{
	size_t count = 0;
	for (const auto& b : buttons)
	{
		if (isLevelVisible(b.level))
			++count;
	}
	return count;
}

size_t Hud::getVisibleSliderCount() const
{
	size_t count = 0;
	for (const auto& s : sliders)
	{
		if (isLevelVisible(s.level))
			++count;
	}
	return count;
}

size_t Hud::getVisibleInputCount() const
{
	size_t count = 0;
	for (const auto& i : inputs)
	{
		if (isLevelVisible(i.level))
			++count;
	}
	return count;
}

void Hud::render(sf::RenderTarget& window)
{
	// Widgets position themselves relative to the top-left of the HUD view.
	const sf::Vector2f viewOrigin = m_camera.getCenter() - m_camera.getSize() / 2.f;

	for (auto& b : buttons)
	{
		if (isLevelVisible(b.level) && b.widget)
			b.widget->draw(window, viewOrigin);
	}

	for (auto& i : inputs)
	{
		if (isLevelVisible(i.level) && i.widget)
			i.widget->draw(window, viewOrigin);
	}

	for (auto& s : sliders)
	{
		if (isLevelVisible(s.level) && s.widget)
			s.widget->draw(window, viewOrigin);
	}

	if (info_box && isLevelVisible(0))
		info_box->draw(window);
}

// ACCESSORIES

void Hud::infoBox(std::vector<std::string> info)
{
	info_box = std::make_unique<CInfoBox>
		(
			200.f,
			200.f,
			m_font,
			info,
			m_camera
		);	
}

// INPUTS

// Text Entered
void Hud::input(const sf::Event::TextEntered& event)
{
	for (auto& i : inputs)
	{
		if (isLevelVisible(i.level) && i.widget)
			i.widget->handleText(event);
	}
}

// Mouse Pressed
void Hud::input(const sf::Event::MouseButtonPressed& event, const sf::Vector2f& mouse_position)
{
	switch (event.button)
	{
	case sf::Mouse::Button::Left:
	{
		const sf::Vector2f viewOrigin = m_camera.getCenter() - m_camera.getSize() / 2.f;

		for (auto& b : buttons)
		{
			if (isLevelVisible(b.level) && b.widget && b.widget->contains(mouse_position, viewOrigin))
				b.widget->activate();
		}

		for (auto& i : inputs)
		{
			if (isLevelVisible(i.level) && i.widget && i.widget->contains(mouse_position, viewOrigin))
				i.widget->activate();
		}

		for (auto& s : sliders)
		{
			if (isLevelVisible(s.level) && s.widget && s.widget->contains(mouse_position, viewOrigin))
				s.widget->beginDrag();
		}

		break;
	}

	default: break;
	}
}

// Mouse Released
void Hud::input(const sf::Event::MouseButtonReleased& event, const sf::Vector2f& mouse_position)
{
	switch (event.button)
	{
	case sf::Mouse::Button::Left:
	{
		for (auto& s : sliders)
		{
			if (s.widget)
				s.widget->endDrag();
		}

		break;
	}

	default: break;
	}
}

// Mouse Moved
void Hud::input(const sf::Event::MouseMoved& event, const sf::Vector2f& mouse_position)
{
	for (auto& s : sliders)
	{
		if (s.widget && s.widget->isActive())
			s.widget->dragTo(mouse_position);
	}
}

