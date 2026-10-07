#pragma once

#include "Scene.h"

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

class Scene_Menu : public Scene
{
	const sf::Font&				m_font;
	std::vector<std::string>	m_options;
	size_t						m_selectedIndex{ 0 };
	sf::Text					m_titleText;
	std::vector<sf::Text>		m_optionTexts;
	sf::View					m_view;

public:
	Scene_Menu(Game* game, const sf::Font& font, float width, float height);

	void update(float deltaTime) override;
	void sRender(sf::RenderTarget& target) override;
	void sUserInput(const sf::Event& event) override;

	void selectNext();
	void selectPrev();
	void chooseSelected();

	size_t getSelectedIndex() const { return m_selectedIndex; }
	const std::vector<std::string>& getOptions() const { return m_options; }
};
