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

`.github/workflows/game-smoke.yml` additionally runs the *real* game on a
virtual display, injects input and checks the frame changes, then exercises the
browser streamer over HTTP (`tools/ci/smoke_test.py`). It uploads the captured
frames as an artifact. See `tools/stream/README.md`.

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

## Running in a browser (remote display)

There is no WebAssembly build, so the game does not run inside a browser tab.
Instead, run it on a headless X display and stream that display to a browser
with `tools/stream/stream_server.py`, forwarding keyboard/mouse through XTEST:

```bash
python3 -m pip install -r tools/stream/requirements.txt
Xvfb :99 -screen 0 1280x720x24 -nolisten tcp &
cd build/bin && DISPLAY=:99 ./OneOfTwenty &
python3 tools/stream/stream_server.py --display :99 --port 12000
```

Open `http://<host>:12000/`, click the picture to take control, and use the
normal keys (WASD, `M`, `G`, `1`, `P`, left click). Add `--password` when the
port is reachable by others. Full details and limits: `tools/stream/README.md`.

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

See [ROADMAP.md](ROADMAP.md) for the ordered milestones. The engine core, the
survival loop and pathfinding are folded into the baseline; the new work starts
at milestone 2 of the roadmap.

### Next milestones
- [ ] Settlement economy and jobs (entity-type roles, recipes/production, food as
  a resource, stockpile HUD, placeable buildings).
- [ ] Survival depth (health, illness/weather, shelter/housing).
- [ ] Presentation and UX (day/night lighting, sprites and camera follow,
  minimap and stats overlay, real menu, audio).
- [ ] Performance and scale (indexed resource queries, spatial entity index,
  incremental memory, headless benchmark in CI).
- [ ] Observability and tuning (event log, run summaries, runtime config reload,
  interrupt a busy entity when a need turns critical).

### Parked
- [ ] Persistence and save/load (serialize seed/clock/entities/stockpile/edits,
  versioned saves, round-trip tests). Deferred until the economy settles.

### Deferred
- [ ] Add a city center.
- [ ] Add AI through llama for civilization politics.

### Completed

Folded into the baseline; kept here as a record. See the roadmap for the new
work.

- [x] **Survival and population dynamics.** Entities age via `CLifespan` and die
  of old age; thirst/hunger turn lethal once an entity is already struggling;
  eating/drinking draw on the stockpile; the settlement reproduces when
  comfortable; population and vitals reach the HUD. Movement no longer overshoots
  its target, so entities actually arrive to drink and eat, and the seeded
  population founds itself on a habitable coastal site (rivers enabled).
- [x] **Pathfinding and collision.** A pure A* (`helpers/Pathfinding.h`) routes
  entities over the `MoveCost` map, so they go around water and prefer cheap
  ground; movement follows the route (`CPath`) and falls back to a straight line
  for short or unreachable hops. `Scene_Play::sCollision` keeps the settlement
  out of the sea and separates overlapping entities, and the game loop clamps the
  frame delta so a slow frame cannot jump the clock hours ahead.
- [x] **Technical debt.** Removed unnecessary heap use (unused `sf::Text`
  member, heap `Camera::cInput`, per-tile color lookup, second tile grid);
  documented the chunk-map lock order and stopped taking the mutex around the
  self-locking ready container; converted HUD widgets into classes; added
  `setChunkUnload()` with a pin check; exposed `logger.level` in `config.json`;
  removed the dead `Scene_Play::spawnEntities`; and refreshed entity memory only
  when an entity enters a new tile, which cuts the per-frame vision scan from
  every entity every frame to roughly once per tile traversed.
- [x] **Map depth.** Tile-space coordinates end to end; `tile_types` is
  authoritative and edits rebuild the chunk mesh; island/river/height-range
  options; one noise field per resource; `getElementAtWorld` for post-edit
  queries; split sampling into `GenerateTerrain` + `MapConfig` + `Chunk.h`.
- [x] **Entities.** `CBasicNeeds` clamps with config-driven decay; `CMemory`
  finds the nearest water/food; `EntityDecision` scales need urgency by
  personality and config bias; vision drives memory and exploration; `MoveCost`
  gives every element a positive cost; `CGather`/`CInventory` bank work into a
  settlement stockpile.
- [x] **Game structure and HUD.** Abstract `Scene` with `Scene_Play` and
  `Scene_Menu`; multiple HUD levels with layered/exact modes; HUD callbacks
  bound by name instead of numeric id.

---

## License

This project is distributed under the MIT license. See [LICENSE](LICENSE).
