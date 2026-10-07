#include <pch.h>

#include "Scene_Menu.h"
#include "Scene_Play.h"
#include "Game.h"

Scene_Menu::Scene_Menu(Game* game, const sf::Font& font, float width, float height)
	: Scene(game)
	, m_font(font)
	, m_titleText(font)
{
	m_view.setSize(sf::Vector2f(width, height));
	m_view.setCenter(sf::Vector2f(width / 2.f, height / 2.f));

	m_titleText.setString("ONE OF TWENTY");
	m_titleText.setCharacterSize(36);
	m_titleText.setFillColor(sf::Color::White);
	m_titleText.setPosition(sf::Vector2f(width / 2.f - 140.f, height / 4.f));

	m_options = { "Play / Resume", "Toggle HUD Level", "Quit Game" };

	for (size_t i = 0; i < m_options.size(); ++i)
	{
		sf::Text text(font);
		text.setString(m_options[i]);
		text.setCharacterSize(22);
		text.setFillColor(i == 0 ? sf::Color::Yellow : sf::Color::White);
		text.setPosition(sf::Vector2f(width / 2.f - 100.f, height / 2.f + static_cast<float>(i * 40)));
		m_optionTexts.push_back(text);
	}
}

void Scene_Menu::update(float /*deltaTime*/)
{
	for (size_t i = 0; i < m_optionTexts.size(); ++i)
	{
		m_optionTexts[i].setFillColor(i == m_selectedIndex ? sf::Color::Yellow : sf::Color::White);
	}
}

void Scene_Menu::selectNext()
{
	if (!m_options.empty())
		m_selectedIndex = (m_selectedIndex + 1) % m_options.size();
}

void Scene_Menu::selectPrev()
{
	if (!m_options.empty())
		m_selectedIndex = (m_selectedIndex + m_options.size() - 1) % m_options.size();
}

void Scene_Menu::chooseSelected()
{
	if (m_selectedIndex == 0)
	{
		if (m_game)
			m_game->changeScene("play");
	}
	else if (m_selectedIndex == 1)
	{
		if (m_game)
		{
			if (auto playScene = std::dynamic_pointer_cast<Scene_Play>(m_game->getScene("play")))
			{
				if (playScene->getHud())
					playScene->getHud()->cycleLevel();
			}
		}
	}
	else if (m_selectedIndex == 2)
	{
		if (m_game)
			m_game->quit();
	}
}

void Scene_Menu::sRender(sf::RenderTarget& target)
{
	target.setView(m_view);
	target.draw(m_titleText);

	for (const auto& text : m_optionTexts)
	{
		target.draw(text);
	}
}

void Scene_Menu::sUserInput(const sf::Event& event)
{
	if (const auto* keyPressed = event.getIf<sf::Event::KeyPressed>())
	{
		if (keyPressed->code == sf::Keyboard::Key::W || keyPressed->code == sf::Keyboard::Key::Up)
		{
			selectPrev();
		}
		else if (keyPressed->code == sf::Keyboard::Key::S || keyPressed->code == sf::Keyboard::Key::Down)
		{
			selectNext();
		}
		else if (keyPressed->code == sf::Keyboard::Key::Enter || keyPressed->code == sf::Keyboard::Key::Space)
		{
			chooseSelected();
		}
		else if (keyPressed->code == sf::Keyboard::Key::Escape)
		{
			if (m_game && m_game->hasScene("play"))
				m_game->changeScene("play");
		}
	}
}
