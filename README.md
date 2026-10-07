# One Of Twenty

One Of Twenty is a game project that I started to learn the basics of C++ while trying to create a real simulation game. (C++17)

It is a chunked, procedurally generated world rendered with SFML, with entities
driven by an EnTT entity-component-system. Map generation runs on worker
threads, and all tuning data (map, HUD, window, logger) lives in JSON config
files so it can be changed without recompiling.

## Requirements

- CMake 3.20 or newer
- A C++17 compiler (GCC, Clang, or MSVC)
- Git (dependencies are fetched at configure time when not installed)
- On Linux, the SFML system dependencies (see below)

SFML, EnTT, spdlog, nlohmann-json, thread-pool and doctest are located with
`find_package`. If a package is not installed, CMake fetches a pinned release
from GitHub automatically, so no manual dependency setup is required.

## Build

### Linux / macOS

Install the SFML system dependencies first (Debian/Ubuntu):

```bash
sudo apt-get install -y build-essential cmake ninja-build git \
    libx11-dev libxrandr-dev libxcursor-dev libxi-dev \
    libudev-dev libfreetype-dev libgl1-mesa-dev \
    libflac-dev libvorbis-dev libopenal-dev
```

Then configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Run the game from the build output directory (assets are copied there at
configure time):

```bash
./build/bin/OneOfTwenty
```

### Windows (Visual Studio)

The easiest path is the provided CMake preset:

```powershell
cmake --preset vs
cmake --build --preset vs
```

Or open the folder directly in Visual Studio and let it use `CMakePresets.json`.
`CMakeSettings.json` is also provided for the VS "Open Folder" workflow.

### Presets

`CMakePresets.json` defines `default` (Ninja/Release), `debug`, and `vs`
(Visual Studio 2022). For example:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## Testing

Unit tests use [doctest](https://github.com/doctest/doctest) and are built by
default. Disable them with `-DONE_OF_TWENTY_BUILD_TESTS=OFF`.

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The tests cover the pure, platform-independent logic: coordinate conversions,
the thread-safe `SharedContainer`, `GameClock` timekeeping, JSON config loading,
and `MapGenerator` lifetime/determinism (biome colors for a fixed seed, worker
shutdown, and the streaming render path).

CI (`.github/workflows/build.yml`) builds and tests on Linux and Windows for
every push and pull request, plus a dedicated Linux job that builds with
AddressSanitizer and UndefinedBehaviorSanitizer.

### Sanitizers

A sanitizer build is available through the `sanitize` preset (GCC/Clang only):

```bash
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize          # runs under xvfb-run; see the preset env
```

On headless machines run the test binary under Xvfb so the render test can
create a GL context: `xvfb-run -a ctest --test-dir build-asan`.
`tests/lsan.supp` and `tests/ubsan.supp` silence known third-party noise
(mesa's GL driver allocations and FastNoiseLite's intentional integer
wrapping); a genuine leak or UB in project code still fails the run.

## Controls

| Input | Action |
| --- | --- |
| `W` / `A` / `S` / `D` | Move the camera |
| Mouse wheel | Zoom in / out |
| `P` | Pause / resume |
| `M` | Re-randomize the map seed |
| `G` (hold) | Debug wireframe view |
| `1` | Spawn a generic human entity |
| Left click | Inspect a tile and show its info box |
| HUD sliders | Tune continent / warp / mineral noise |

## Architecture

| Module | Responsibility |
| --- | --- |
| `src/game` | Game loop, window, systems (`sMovement`, `sCollision`, `sRender`, `sUserInput`) |
| `src/map_generator` | Chunked procedural map: noise, chunk streaming on worker threads, rendering, tile queries |
| `src/entity_manager` | EnTT registry, entity spawning, needs/memory/action systems, rendering |
| `src/components` | ECS component and HUD widget definitions |
| `src/hud` | Data-driven HUD (buttons, sliders, input boxes) loaded from JSON |
| `src/camera` | World camera and view bounds |
| `src/helpers` | Header-only utilities: `Logger`, `GameClock`, `Random`, `SharedContainer`, `Config`, `CoordMath`, `FastNoiseLite` |
| `src/pch` | Precompiled header aggregating the common includes |

Config files live in `config/`: `config.json` (window/logger/font/map/HUD
paths), `map_data.json` (tile size, noise parameters, biomes, thresholds), and
`hud_menu_data.json` (HUD element layout and bindings).

## Libraries

- [SFML](https://www.sfml-dev.org/) (Simple and Fast Multimedia Library)
- [entt](https://github.com/skypjack/entt) (Entity Component System by Michele Caini)
- [thread-pool](https://github.com/bshoshany/thread-pool) (Thread Manager by Barak Shoshany)
- [nlohmann-json](https://github.com/nlohmann/json) (JSON parser by Niels Lohmann)
- [spdlog](https://github.com/gabime/spdlog) (Logging library)
- [doctest](https://github.com/doctest/doctest) (Unit testing framework)

## TODO

See [ROADMAP.md](ROADMAP.md) for these items organized into milestones.

### General
- [x] Re-check all objects for dynamic allocation of big objects. Removed the
  unused `sf::Text` member, the heap-allocated `Camera::cInput`, the per-tile
  color lookup in `generateChunk`, and the second (bit-packed) tile grid.
- [x] Thread-safe access to the shared chunk map is guarded by a single mutex;
  lock ordering documented and the mutex is no longer taken around the
  self-locking ready container. Revisit with a finer-grained or lock-free
  structure if contention grows.

### Components
- [x] Convert HUD components into classes. `CButton`, `CInputBox`, `CSlider`
  and `CInfoBox` now own their shapes/text and handle placement, hit testing,
  drawing and input; `Hud` just drives them.

### HUD
- Implement multiples HUD levels.
- Bind HUD elements to functions by name instead of by numeric id, so config
  and enums cannot drift apart.

### MapGenerator
- IMPORTANT: Convert all coords to be tile. Use world coords only on render
- Add rivers?
- Add possibility to increase depths and heights.
- Improve getting resources for entities.
- FIX: different noise map for each resource?
- Add option to create an island.
- Change map on entity action. CHECK setTileColor, added map for tiles
- [x] Update chunk unload to double check if no entity or changes are present.
  Added `MapGenerator::setChunkUnload()`; a pinned chunk survives streaming and
  is only evicted once released.
- Split the class into chunk store / streamer / renderer.

### Game
- Implement a `Scene` class, pass inputs to scenes.
- [x] Expose the logger level through `config.json` (key `logger.level`).

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

This project is distributed under the MIT license. See [LICENSE](LICENSE).
