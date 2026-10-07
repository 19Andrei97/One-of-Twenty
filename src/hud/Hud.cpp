
#include <pch.h>

#include "Hud.h"

void Hud::init()
{
	using json = nlohmann::json;

	json data = loadJsonFile(m_file);

	for (const auto& [key, value] : data.items())
	{

		if (value["type"] == "button")
		{
			// CREATE BUTTON ELEMENT
			buttons.push_back(std::make_unique<CButton>(
				static_cast<float>(value["width"]), 
				static_cast<float>(value["height"]),
				sf::Vector2f{ static_cast<float>(value["position"]["x"]), static_cast<float>(value["position"]["y"]) },
				m_font, 
				value["label"],
				sf::Color(value["color_fill"][0], value["color_fill"][1], value["color_fill"][2]),
				sf::Color(value["outline"][0], value["outline"][1], value["outline"][2]),
				static_cast<float>(value["thickness"]),
				static_cast<float>(value["text_size"])
			));

			// DECIDE WHICH FUNCTION
			switch (static_cast<int>(value["function"]))
			{
			case Function::Button::Random:
				buttons.back()->setOnClick([this]() {
					m_map->setSeed();
					m_map->m_reset = true;
					});
				break;
			}

		}
		else if (value["type"] == "slider")
		{
			// CREATE SLIDER ELEMENT
			sliders.push_back(std::make_unique<CSlider>(
				static_cast<float>(value["width"]),
				static_cast<float>(value["height"]),
				sf::Vector2f{ static_cast<float>(value["position"]["x"]), static_cast<float>(value["position"]["y"]) },
				static_cast<float>(value["minimum_value"]),
				static_cast<float>(value["maximum_value"]),
				m_font,
				value["label"],
				sf::Color(value["bar_color"][0], value["bar_color"][1], value["bar_color"][2]),
				sf::Color(value["handle_color"][0], value["handle_color"][1], value["handle_color"][2])
			));

			// DECIDE WHICH FUNCTION
			switch (static_cast<int>(value["function"]))
			{

			case Function::Slider::ContFreq:
				sliders.back()->setOnChange([this](float val)
					{
						m_map->setContFreq(val);
						m_map->m_reset = true;
					});
				break;

			case Function::Slider::ContMult:
				sliders.back()->setOnChange([this](float val)
					{
						m_map->setContMult(val);
						m_map->m_reset = true;
					});
				break;

			case Function::Slider::WarpFreq:
				sliders.back()->setOnChange([this](float val)
					{
						m_map->setWarpFreq(val);
						m_map->m_reset = true;
					});
				break;

			case Function::Slider::MineralFreq:
				sliders.back()->setOnChange([this](float val)
					{
						m_map->setMineralFreq(val);
						m_map->m_reset = true;
					});
				break;

			case Function::Slider::MineralMult:
				sliders.back()->setOnChange([this](float val)
					{
						m_map->setMineralMult(val);
						m_map->m_reset = true;
					});
				break;

			}
		}

		else if (value["type"] == "input")
		{

		}
	}

}

void Hud::render(sf::RenderTarget& window)
{
	// Widgets position themselves relative to the top-left of the HUD view.
	const sf::Vector2f viewOrigin = m_camera.getCenter() - m_camera.getSize() / 2.f;

	for (auto& b : buttons)
		b->draw(window, viewOrigin);

	for (auto& i : inputs)
		i->draw(window, viewOrigin);

	for (auto& s : sliders)
		s->draw(window, viewOrigin);

	if (info_box)
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
		i->handleText(event);
}

// Moused Pressed
void Hud::input(const sf::Event::MouseButtonPressed& event, sf::Vector2f& mouse_position)
{
	switch (event.button)
	{
	case sf::Mouse::Button::Left:
	{
		const sf::Vector2f viewOrigin = m_camera.getCenter() - m_camera.getSize() / 2.f;

		for (auto& b : buttons)
		{
			if (b->contains(mouse_position, viewOrigin))
				b->activate();
		}

		for (auto& i : inputs)
		{
			if (i->contains(mouse_position, viewOrigin))
				i->activate();
		}

		for (auto& s : sliders)
		{
			if (s->contains(mouse_position, viewOrigin))
				s->beginDrag();
		}

		break;
	}

	default: break;
	}
}

// Mouse Released
void Hud::input(const sf::Event::MouseButtonReleased& event, sf::Vector2f& mouse_position)
{
	switch (event.button)
	{
	case sf::Mouse::Button::Left:
	{
		for (auto& s : sliders)
			s->endDrag();

		break;
	}

	default: break;
	}
}

// Mouse Moved
void Hud::input(const sf::Event::MouseMoved& event, sf::Vector2f& mouse_position)
{
	for (auto& s : sliders)
		s->dragTo(mouse_position);
}
