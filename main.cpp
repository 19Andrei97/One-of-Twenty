#include <SFML/Graphics.hpp>
#include "src/game_manager/Game.h"
#include <iostream>

int main()
{
	Game g("src/config/config.json");

	g.run();
}
