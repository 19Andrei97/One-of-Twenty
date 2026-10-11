#pragma once

#include "MapConfig.h"

#include <string>

// The choices the pre-simulation menu makes, carried into a run.
//
// The menu builds a *preview* map the player can see and tune, so the map
// generator that draws it already owns the terrain values. Rather than trusting
// that object (which a run must not share), a run captures the terrain as a
// MapConfig value plus the few non-terrain choices - seed, start time, street
// level - so a fresh world can be built from the snapshot and the menu can keep
// its own preview independent of a running game. Keeping this a plain value
// type means a run is fully described by the menu with no hidden state left on
// the scene.
struct GameSettings
	{
	// Terrain tuning for the run, snapshotted from the menu's preview.
	MapConfig map{};

	// The world seed. The menu preview and the run share this value, so the run
	// reproduces the world the player was shown.
	int seed{ 1 };

	// Starting street level: how many of the twenty political seats the player's
	// faction begins with. Cosmetic until the political layer lands, but it is
	// the kind of pre-simulation choice the menu exists to make.
	int street_level{ 1 };

	// Time of day and clock speed the run starts at.
	int start_hour{ 8 };
	int start_minute{ 0 };
	int speed_index{ 1 };

	// Entity and building catalogs for the run. Defaults are used by tools and
	// tests that build a scene without a menu.
	std::string entity_file{ "config/entity_data.json" };
	std::string buildings_file{ "config/buildings.json" };
};
