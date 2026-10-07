#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>

class Game;

class Scene
{
protected:
	Game* m_game = nullptr;
	bool  m_hasEnded = false;
	bool  m_paused = false;

public:
	Scene() = default;
	explicit Scene(Game* game) : m_game(game) {}
	virtual ~Scene() = default;

	virtual void update(float deltaTime) = 0;
	virtual void sRender(sf::RenderTarget& target) = 0;
	virtual void sUserInput(const sf::Event& event) = 0;

	virtual void onEnter() {}
	virtual void onExit() {}

	void setGame(Game* game) { m_game = game; }
	Game* getGame() const { return m_game; }

	bool hasEnded() const { return m_hasEnded; }
	void end() { m_hasEnded = true; }

	bool isPaused() const { return m_paused; }
	virtual void setPaused(bool paused) { m_paused = paused; }
	virtual void togglePaused() { setPaused(!m_paused); }
};
