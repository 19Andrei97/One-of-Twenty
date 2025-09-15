# One Of Twenty

One Of Twenty is a game project that I started to learn the basics of c++ while trying to create a real simulation game.

## Requirements

- C++17 compatible compiler (Visual Studio 2022 recommended)
- [SFML](https://www.sfml-dev.org/) (Simple and Fast Multimedia Library)
- [entt](https://github.com/skypjack/entt) (Entity Component System by Michele Caini)

## Installation

1. Clone the repository
2. Install SFML and configure your project to include it
3. Install entt, you can follow the how to in their github project.
4. Build using Visual Studio 2022 or another C++17 compatible compiler.

## TODO

### General
- Re-check all objects for dynamic allocation of big objects.
- Implement thread handler class.
- Check into bin files.

### MapGenerator
- Add rivers?
- add possibility to increase depths and heights.
- Improve getting resources for entities.
- FIX: different noise map for each resource?
- Add option to create an island.

### Game
- Implement a `Scene` class, pass inputs to scenes.

### Entity
- Improve CVision component debug circle.
- Improve CMemory component
- Add cost for moving depending on tile.
- Add needs.
- Implement weights-based decisions for entities.
- add ai through llama for civilization politics

---

## License

This project is distributed under the MIT license.
