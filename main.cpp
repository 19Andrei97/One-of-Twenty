
#include "pch.h"

#include "src/game_manager/Game.h"

int main()
{
	Game g("src/config/config.json");

	g.run();
}
