

#include <pch.h>

#include "Hud.h"

Hud::Hud(const sf::Font& font, std::shared_ptr<MapGenerator> map, const std::string& file, float window_x, float window_y)
	: m_font(font)
	, m_map(map)
	, m_file(file)
{
	m_camera.setSize(sf::Vector2f(window_x, window_y));
	m_camera.setCenter(sf::Vector2f(window_x / 2.f, window_y / 2.f));
}

void Hud::registerDefaultCallbacks()
{
        // Only the names the shipped config actually uses are registered; an
        // unknown name in the config simply binds no callback.

        // Changing a noise parameter invalidates the loaded chunks, so mark the
        // map for a reset as well.
        const auto onMapChanged = [this]()
        {
                if (m_map)
                        m_map->m_reset = true;
        };

        // Map actions
        registerButtonCallback("random", [this, onMapChanged]() {
                if (m_map)
                        m_map->setSeed();
                onMapChanged();
        });

        // Terrain presets. Each one drives every terrain slider at once so a player
        // gets a recognisable world in one click, then tweaks individual sliders.
        // setSliderValue fires each slider's own callback, which writes the live
        // config and marks the map for a reset, so no reset is issued here.
        const auto applyPreset = [this](const std::vector<std::pair<std::string, float>>& values) {
                for (const auto& [name, value] : values)
                        setSliderValue(name, value);
        };
        registerButtonCallback("preset_earth", [applyPreset]() {
                applyPreset({
                        { "land_amount", 0.35f }, { "continent_size", 0.0015f }, { "coast_roughness", 0.60f },
                        { "mountain_height", 0.45f }, { "mountain_scale", 0.012f },
                        { "temperature", 0.50f }, { "rainfall", 0.50f }, { "snow_line", 0.90f },
                        { "lake_level", 0.30f }, { "lake_size", 0.62f }, { "ore_richness", 0.55f },
                });
        });
        registerButtonCallback("preset_archipelago", [applyPreset]() {
                applyPreset({
                        { "land_amount", 0.32f }, { "continent_size", 0.0035f }, { "coast_roughness", 0.75f },
                        { "mountain_height", 0.35f }, { "mountain_scale", 0.020f },
                        { "temperature", 0.58f }, { "rainfall", 0.62f }, { "snow_line", 0.95f },
                        { "lake_level", 0.32f }, { "lake_size", 0.62f }, { "ore_richness", 0.50f },
                });
        });
        registerButtonCallback("preset_pangaea", [applyPreset]() {
                applyPreset({
                        { "land_amount", 0.46f }, { "continent_size", 0.0007f }, { "coast_roughness", 0.45f },
                        { "mountain_height", 0.60f }, { "mountain_scale", 0.008f },
                        { "temperature", 0.48f }, { "rainfall", 0.38f }, { "snow_line", 0.85f },
                        { "lake_level", 0.28f }, { "lake_size", 0.58f }, { "ore_richness", 0.60f },
                });
        });

        // Time management. These act on the clock when one is attached; the scene
        // overrides time_pause to keep its own pause state in step.
        registerButtonCallback("time_slower", [this]() {
                if (m_clock)
                        m_clock->slower();
        });
        registerButtonCallback("time_faster", [this]() {
                if (m_clock)
                        m_clock->faster();
        });
        registerButtonCallback("time_pause", [this]() {
                if (m_clock)
                        m_clock->togglePause();
        });

        registerButtonCallback("cycle_hud", [this]() {
                cycleLevel();
        });

        // Terrain sliders. Each one maps a player-facing name to a single config
        // value; changing one marks the map for a reset.
        registerSliderCallback("land_amount", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setLandAmount(val);
                onMapChanged();
        });

        registerSliderCallback("continent_size", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setContinentSize(val);
                onMapChanged();
        });

        registerSliderCallback("coast_roughness", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setCoastRoughness(val);
                onMapChanged();
        });

        registerSliderCallback("mountain_height", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setMountainHeight(val);
                onMapChanged();
        });

        registerSliderCallback("mountain_scale", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setMountainScale(val);
                onMapChanged();
        });

        registerSliderCallback("temperature", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setTemperature(val);
                onMapChanged();
        });

        registerSliderCallback("rainfall", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setRainfall(val);
                onMapChanged();
        });

        registerSliderCallback("snow_line", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setSnowLine(val);
                onMapChanged();
        });

        registerSliderCallback("lake_level", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setLakeLevel(val);
                onMapChanged();
        });

        registerSliderCallback("lake_size", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setLakeSize(val);
                onMapChanged();
        });

        registerSliderCallback("ore_richness", [this, onMapChanged](float val) {
                if (m_map)
                        m_map->setOreRichness(val);
                onMapChanged();
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
			const float minValue = static_cast<float>(value["minimum_value"]);
			const float maxValue = static_cast<float>(value["maximum_value"]);
			// An explicit `value` starts the handle at the config's actual setting
			// instead of the middle of the range, so the UI matches the generated map.
			const float initial = value.value("value", (minValue + maxValue) / 2.f);

			auto sld = std::make_unique<CSlider>(
				static_cast<float>(value["width"]),
				static_cast<float>(value["height"]),
				sf::Vector2f{ static_cast<float>(value["position"]["x"]), static_cast<float>(value["position"]["y"]) },
				minValue,
				maxValue,
				m_font,
				value["label"],
				sf::Color(value["bar_color"][0], value["bar_color"][1], value["bar_color"][2]),
				sf::Color(value["handle_color"][0], value["handle_color"][1], value["handle_color"][2]),
				value.value("description", std::string{}),
				initial
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

	if (m_stats && isLevelVisible(0))
		m_stats->draw(window);

	if (m_time_panel && isLevelVisible(0))
		m_time_panel->draw(window);
}

// ACCESSORIES

void Hud::infoBox(std::vector<std::string> info)
{
	// A compact selection readout pinned to the bottom-right, clear of the
	// top-left map controls, the top-right clock and the bottom-left stats.
	// Refreshed in place so a repeated click does not reallocate the panel.
	if (!info_box)
	{
		constexpr float kWidth{ 220.f };
		constexpr float kMargin{ 16.f };
		const float x = m_camera.getSize().x - kWidth - kMargin;
		info_box = std::make_unique<CInfoBox>(kWidth, 0.f, m_font, std::vector<std::string>{}, m_camera,
		                                      sf::Color(0, 0, 0, 160), sf::Color::White, 16U, true, x, kMargin);
	}

	info_box->setLines(info);
}

void Hud::stats(const std::vector<std::string>& lines)
{
	// Refresh the persistent panel in place; rebuild only on the first call.
	// Pinned to the bottom-left, clear of the top-left map controls and the
	// bottom-right selection box.
	constexpr float kMargin{ 16.f };
	if (!m_stats)
		m_stats = std::make_unique<CInfoBox>(240.f, 0.f, m_font, std::vector<std::string>{}, m_camera,
		                                     sf::Color(0, 0, 0, 128), sf::Color::White, 16U, true, kMargin, kMargin);

	m_stats->setLines(lines);
}

void Hud::timeReadout(const std::vector<std::string>& lines)
{
	// A compact panel pinned to the top-right, clear of the settlement stats on
	// the left and the selection info box at the bottom.
	constexpr float kWidth{ 260.f };
	constexpr float kMargin{ 16.f };
	const float x = m_camera.getSize().x - kWidth - kMargin;

	if (!m_time_panel)
		m_time_panel = std::make_unique<CInfoBox>(kWidth, 0.f, m_font, std::vector<std::string>{}, m_camera,
		                                          sf::Color(0, 0, 0, 160), sf::Color::White, 16U, false, x, kMargin);

	m_time_panel->setLines(lines);
}

void Hud::setButtonLabel(const std::string& functionName, const std::string& label)
{
	for (auto& b : buttons)
	{
		if (b.functionName == functionName && b.widget)
			b.widget->setLabel(label);
	}
}

bool Hud::setSliderValue(const std::string& functionName, float value)
{
	bool found = false;
	for (auto& s : sliders)
	{
		if (s.functionName == functionName && s.widget)
		{
			s.widget->setValue(value);
			found = true;
		}
	}
	return found;
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

