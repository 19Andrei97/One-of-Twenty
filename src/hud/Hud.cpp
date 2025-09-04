#include "Hud.h"

void Hud::init()
{
	using json = nlohmann::json;

	std::ifstream f(m_file);
	json data = json::parse(f);

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
				buttons.back()->onClick = [this]() {
					m_map.setSeed();
					m_map.m_reset = true;
					};
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
				sliders.back()->onChange = [this](float val)
					{
						m_map.setContFreq(val);
						m_map.m_reset = true;
					};
				break;

			case Function::Slider::ContMult:
				sliders.back()->onChange = [this](float val)
					{
						m_map.setContMult(val);
						m_map.m_reset = true;
					};
				break;

			case Function::Slider::WarpFreq:
				sliders.back()->onChange = [this](float val)
					{
						m_map.setWarpFreq(val);
						m_map.m_reset = true;
					};
				break;

			case Function::Slider::MineralFreq:
				sliders.back()->onChange = [this](float val)
					{
						m_map.setMineralMult(val);
						m_map.m_reset = true;
					};
				break;

			case Function::Slider::MineralMult:
				sliders.back()->onChange = [this](float val)
					{
						m_map.setMineralFreq(val);
						m_map.m_reset = true;
					};
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
	sf::Vector2f viewCenter = m_camera.getCenter();
	sf::Vector2f viewSize = m_camera.getSize();

	for (auto& b : buttons)
	{
		sf::Vector2f screenPos
		(
			viewCenter.x - viewSize.x / 2.f + b->m_pos.x,
			viewCenter.y - viewSize.y / 2.f + b->m_pos.y
		);

		// Set shape position
		b->rect.setPosition(screenPos);

		// Set text position
		sf::FloatRect rectBounds = b->rect.getGlobalBounds();
		sf::FloatRect textBounds = b->text->getLocalBounds();

		float x = rectBounds.position.x + (rectBounds.size.x / 2.f) - (textBounds.size.x / 2.f) - textBounds.position.x;
		float y = rectBounds.position.y + (rectBounds.size.y / 2.f) - (textBounds.size.y / 2.f) - textBounds.position.y;

		b->text->setPosition({ x, y });

		// Draw
		window.draw(b->rect);
		window.draw(*(b->text));
	}

	for (auto& i : inputs)
	{
		sf::Vector2f screenPos(
			viewCenter.x - viewSize.x / 2.f + i->pos.x,
			viewCenter.y - viewSize.y / 2.f + i->pos.y
		);

		// Set shape position
		i->rect.setPosition(screenPos);

		// Set text position
		sf::FloatRect rectBounds = i->rect.getGlobalBounds();
		sf::FloatRect textBounds = i->text->getLocalBounds();

		float x = rectBounds.position.x + (rectBounds.size.x / 2.f) - (textBounds.size.x / 2.f) - textBounds.position.x;
		float y = rectBounds.position.y + (rectBounds.size.y / 2.f) - (textBounds.size.y / 2.f) - textBounds.position.y;

		i->text->setPosition({ x, y });

		// Draw
		window.draw(i->rect);
		window.draw(*(i->text));
	}

	for (auto& s : sliders)
	{
		sf::Vector2f screenPos(
			viewCenter.x - viewSize.x / 2.f + s->pos.x,
			viewCenter.y - viewSize.y / 2.f + s->pos.y
		);

		// Set bar shape position
		s->bar.setPosition(screenPos);

		// Set text position
		s->text->setPosition({ screenPos.x, screenPos.y - s->pos.x });

		// Update handle
		float ratio = (s->value - s->minValue) / (s->maxValue - s->minValue);
		float x = s->bar.getPosition().x + ratio * s->bar.getSize().x;
		float y = s->bar.getPosition().y + s->bar.getSize().y / 2.f;
		s->handle.setPosition(sf::Vector2f(x, y));

		// Draw
		window.draw(s->bar);
		window.draw(s->handle);
		window.draw(*(s->text));
	}

	// RENDER INFO BOX
	{
		if (info_box && info_box->m_text.size() >= 1)
		{
			window.draw(info_box->m_rect);

			int text_space{ 0 };
			for (auto& text : info_box->m_text)
			{
				text->setPosition({ info_box->m_pos.x + 20, info_box->m_pos.y + 20 + text_space });
				window.draw(*(text));

				text_space += 20;
			}
		}
	}

}

bool Hud::checkClick(const std::unique_ptr<CButton>& obj, const sf::Vector2f& mouse_pos)
{
	if (obj->rect.getGlobalBounds().contains(mouse_pos))
	{
		if (obj->onClick) obj->onClick();
		return true;
	}

	return false;
}

bool Hud::checkClick(const std::unique_ptr<CInputBox>& obj, const sf::Vector2f& mouse_pos)
{
	if (obj->rect.getGlobalBounds().contains(mouse_pos))
	{
		obj->active = true;
		obj->inputString.clear();
		obj->text->setString("");
		return true;
	}

	return false;
}

bool Hud::checkClick(const std::unique_ptr<CSlider>& obj, const sf::Vector2f& mouse_pos)
{
	if (obj->handle.getGlobalBounds().contains(mouse_pos) || obj->bar.getGlobalBounds().contains(mouse_pos))
		obj->active = true;

	return false;
}

void Hud::writing(const std::unique_ptr<CInputBox>& obj, const sf::Event::TextEntered& textEvent)
{
	if (!obj->active) return;

	if (textEvent.unicode == 8) // Backspace
	{
		if (!obj->inputString.empty())
		{
			obj->inputString.pop_back();
			obj->text->setString(obj->inputString.empty() ? obj->placeholder : obj->inputString);
		}
	}
	else if (textEvent.unicode == 13) // Enter
	{
		if (!obj->inputString.empty())
		{
			if (obj->onEnter) obj->onEnter(std::stof(obj->inputString));
			obj->inputString.clear();
			obj->text->setString(obj->placeholder);
			obj->active = false; // unfocuse input after enter
		}
	}
	else if (textEvent.unicode >= '0' && textEvent.unicode <= '9' || textEvent.unicode == '.') // Numbers and dot
	{
		obj->inputString += static_cast<char>(textEvent.unicode);
		obj->text->setString(obj->inputString);
	}
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
		if(i->active)
			writing(i, event);
	}
}

// Moused Pressed
void Hud::input(const sf::Event::MouseButtonPressed& event, sf::Vector2f& mouse_position)
{
	switch (event.button)
	{
	case sf::Mouse::Button::Left:
	{
		for (auto& b : buttons)
			checkClick(b, mouse_position);

		for (auto& i : inputs)
			checkClick(i, mouse_position);

		for (auto& s : sliders)
			checkClick(s, mouse_position);

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
			s->active = false;

		break;
	}

	default: break;
	}
}

// Mouse Moved
void Hud::input(const sf::Event::MouseMoved& event, sf::Vector2f& mouse_position)
{
	for (auto& s : sliders)
	{
		if (s->active)
		{
			float left = s->bar.getPosition().x;
			float right = left + s->bar.getSize().x;
			float clampedX = std::max(left, std::min(mouse_position.x, right));

			float ratio = (clampedX - left) / s->bar.getSize().x;
			s->value = s->minValue + ratio * (s->maxValue - s->minValue);

			if (s->onChange) s->onChange(s->value);
		}
	}
}
