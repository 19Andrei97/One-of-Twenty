# One Of Twenty

One Of Twenty is a game project that I started to learn the basics of c++ while providing a something more than just a test.

## Requirements

- C++17 compatible compiler (Visual Studio 2022 recommended)
- [SFML](https://www.sfml-dev.org/) (Simple and Fast Multimedia Library)
- [entt](https://github.com/skypjack/entt) (Entity Component System by Michele Caini)

## Installation

1. Clone the repository
2. Install SFML and configure your project to include it
3. Install entt, you can follow the how to in their github project.
4. Build using Visual Studio 2022 or another C++17 compatible compiler

## Project Structure

- `src/mapgen/MapGenerator.*` : Map generation and management
- `src/hud/Hud.*` : HUD and user interface management
- `Entity.*` and `EntityManager.*` : Entity management
- `Game.*` : Main game logic and loop
- `main.cpp` : Entry point

## TODO

### General
- Re-check all objects for dynamic allocation of big objects.
- Add logger.

### MapGenerator
- Add rivers?
- add possibility to increase depths and heights

### Game
- Implement a `Scene` class, pass inputs to scenes.

### Entity
- Add ability to move.
- Add needs.
- Implement weights-based decisions for entities.
- add ai through llama for civilization politics

---

## License

This project is distributed under the MIT license.
