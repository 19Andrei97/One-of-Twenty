# One Of Twenty

One Of Twenty is a game project that I started to learn the basics of c++ while trying to create a real simulation game. (C++17)

## Libraries

- [SFML](https://www.sfml-dev.org/) (Simple and Fast Multimedia Library)
- [entt](https://github.com/skypjack/entt) (Entity Component System by Michele Caini)
- [thread-pool](https://github.com/bshoshany/thread-pool) (Thread Manager by Barak Shoshany)
- [nlohmann-json](https://github.com/nlohmann/json) (JSON parser by Niels Lohmann)
- [spdlog](https://github.com/gabime/spdlog) (Logging library)

## Installation

1. Clone the repository.
2. Open the folder in VS.
3. Compile (it will install missing libraries).
4. Run.

## TODO

### General
- Re-check all objects for dynamic allocation of big objects.

### Components
- Convert HUD components into classes.

### HUD
- Implement multiples HUD levels.

### MapGenerator
- IMPORTANT: Convert all coords to be tile. Use world coords only on render
- Add rivers?
- Add possibility to increase depths and heights.
- Improve getting resources for entities.
- FIX: different noise map for each resource?
- Add option to create an island.
- Change map on entity action. CHECK setTileColor, added map for tiles
- Update chunk unload to double check if no entity or changes are present.

### Game
- Implement a `Scene` class, pass inputs to scenes.

### Entity
- Improve CVision component debug circle.
- Improve CMemory component (currently remembers only water and hill).
- ADD city center.
- Provide actions to advance society.
- Improve Tile Cost calculation.
- Improve CBasicNeeds.
- Implement weights-based decisions for entities.
- Add ai through llama for civilization politics.

---

## License

This project is distributed under the MIT license.
