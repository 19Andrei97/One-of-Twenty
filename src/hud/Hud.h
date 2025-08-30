#pragma once

#include "Components.h"
#include "json.h"
#include "../mapgen/MapGenerator.h"
#include <fstream>
#include <memory>

namespace Function
{
	enum Button
	{
		Random = 0
	};

	enum Slider
	{
		ContFreq = 0,
		WarpFreq,
		TerrMult,
		ContMult
	};
}


class Hud
{
	sf::Font&			m_font;
	std::string			m_file;
	MapGenerator&		m_map;

	// VECTORS ELEMENTS
	std::vector<std::unique_ptr<CButton>>		buttons;
	std::vector<std::unique_ptr<CInputBox>>		inputs;
	std::vector<std::unique_ptr<CSlider>>		sliders;

public:

	// CONSTRUCTOR & INITIATOR
	Hud(sf::Font& font, MapGenerator& map, const std::string& file)
		: m_font(font)
		, m_map(map)
		, m_file(file)
	{
	}
	void init();

	// SYSTEMS
	void render(sf::RenderTarget& window, const sf::View& view);
	bool checkClick(const std::unique_ptr<CButton>& obj, const sf::Vector2f& mouse_pos);
	bool checkClick(const std::unique_ptr<CInputBox>& obj, const sf::Vector2f& mouse_pos);
	bool checkClick(const std::unique_ptr<CSlider>& obj, const sf::Vector2f& mouse_pos);
	void writing(const std::unique_ptr<CInputBox>& obj, const sf::Event::TextEntered& textEvent);

	// INPUTS
	void input(const sf::Event::TextEntered& event);
	void input(const sf::Event::MouseButtonPressed& event, sf::Vector2f& mouse_position);
	void input(const sf::Event::MouseButtonReleased& event, sf::Vector2f& mouse_position);
	void input(const sf::Event::MouseMoved& event, sf::Vector2f& mouse_position);
};