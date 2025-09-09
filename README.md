# One Of Twenty

One Of Twenty is a game project that I started to learn the basics of c++.

## Requirements

- C++17 compatible compiler (Visual Studio 2022 recommended)
- [SFML](https://www.sfml-dev.org/) (Simple and Fast Multimedia Library)

## Installation

1. Clone the repository
2. Install SFML and configure your project to include it
3. Build using Visual Studio 2022 or another C++17 compatible compiler

## Usage

- Run the game using the generated executable
- Configure options via the `config.json` file
- Modify map and entity logic in the source files

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
- FIX: info box no more working when updating map.
- Add rivers?
- Remove id for chunks?
- add possibility to increase depths and heights

### Game
- Implement a `Scene` class, pass inputs to scenes.

### Entity
- implement entt
- Implement weights-based decisions for entities.
- add ai through llama for civilization politics

---

## License

This project is distributed under the MIT license.
