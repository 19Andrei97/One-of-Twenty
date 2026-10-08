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
| `[` / `]` | Slower / faster clock |
| `H` / `Tab` | Cycle HUD level |
| `R` | Reload `entity_data.json` tuning at runtime |
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

One Of Twenty is a political life-sim: you play **one of the twenty who govern**,
gaining and losing political power through your decisions, events and
geopolitical conditions, starting with little power and only your faction's few
allies — while a **civilization simulation never stops** around you and grows far
larger than the governing twenty. The current focus is that civilization
simulation; the political layer is built once it behaves.

Because the population grows without bound, nothing may cost O(pawns²): pawns
have a **social station and importance** (a per-pawn value), not pairwise
relationships, and detail follows importance (notables are simulated as
individuals, the masses in bulk).

See [ROADMAP.md](ROADMAP.md) for the ordered milestones. The engine core, the
survival loop and pathfinding are folded into the baseline; milestones 1
(settlement economy and jobs) and 2 (observability and tuning) are complete and
the active work starts at milestone 3 (social station and importance).

### Next milestones
- [x] Settlement economy and jobs (entity-type roles, recipes/production, food as
  a resource, stockpile HUD, placeable buildings).
- [x] Observability and tuning (event log, run summaries, runtime config reload,
  interrupt a busy entity when a need turns critical).
- [ ] Social station and importance (a per-pawn station, divergent personalities,
  detail-by-importance) — the active milestone, and the basis for politics.
- [ ] Threats and defense (wildlife, combat, walls) — gives survival a reason.
- [ ] The wider world (several civilizations, an abstract neighbor model, trade
  and rivalry, evolving geopolitical conditions).
- [ ] City center and institutional growth (a settlement anchor, a sphere of
  effect, investing in the commons).
- [ ] Performance and scale (indexed resource queries, spatial entity index,
  budgeted vision scan, headless benchmark in CI).
- [ ] Social capital and influence — the political simulation (power, factions,
  earned/lost influence, an in-simulation vote among the governing twenty).
- [ ] Events and the player's agency (a player-controlled entity, decisions that
  scale with power and allies, an event system, a political UI, opinion model).
- [ ] Presentation and UX (day/night lighting, sprites and camera follow,
  minimap and stats overlay, real menu, audio).

### On the side
- [x] **Survival and population dynamics** (implemented). Kept here for now
  rather than as an active milestone: entities age and die, starvation and
  dehydration drain health while a comfortable entity recovers, and comfortable
  adults reproduce. Follow-up work (illness/weather, shelter/housing, survival
  interrupts) is listed in [ROADMAP.md](ROADMAP.md).

### Parked
- [ ] Persistence and save/load (serialize seed/clock/entities/social station/
  stockpile/influence/factions/edits, versioned saves, round-trip tests).
  Deferred until the station and wider-world milestones settle.

### Deferred
- [ ] AI politics via an LLM (speeches, negotiation, justification behind a
  mockable interface), once factions, events and influence exist as data.

### Completed

Retired from the roadmap; kept here as a record.

- [x] **Observability and tuning.** A bounded event log (`helpers/EventLog.h`)
  records births, deaths with cause, gathers and discoveries, stamped with the
  in-game minute and queryable in tests; a `RunHistory` (`helpers/RunSummary.h`)
  samples the population and stores once per in-game day and summarizes them for
  a baseline. A busy entity interrupts a non-survival plan when a need turns
  critical (`EntityDecision::interruptFor`), and `entity_data.json` reloads at
  runtime with `R`.
- [x] **Settlement economy and jobs.** Entity types are bound to jobs
  (`helpers/Jobs.h`, `config/entity_data.json` `entity_types` block, with
  defaults for older configs). Gathers bank into a per-good stockpile
  (`helpers/Goods.h`) that eating and daily spoilage draw on; the shortage rule
  reassigns idle entities to understaffed jobs (`helpers/Economy.h`); farms grow
  food and workshops run recipes; and buildings are placed with `setTileColor`
  so they render and persist. A stats panel shows the stockpile, buildings and
  gather count.
- [x] **Buildings catalog and construction.** `helpers/Buildings.h` is a pure,
  data-driven catalog (`config/buildings.json`) of what a building costs, looks
  like, produces and how it changes movement, so adding one is a JSON entry plus
  an `Elements` value. `EntityManager` plans sites (spending the catalog cost and
  writing the tile) and builders complete them over `build_hours`; a finished
  house raises the population cap above the config's `max_population`, and the
  city center is an anchor building. The catalog's `walk_cost` layers over
  `MoveCost` (a road is faster), and `R` reloads the catalog alongside the entity
  config.
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
  removed the dead `Scene_Play::spawnEntities`; and refresh the shared knowledge
  only when an entity enters a new tile, which cuts the per-frame vision scan from
  every entity every frame to roughly once per tile traversed.
- [x] **Map depth.** Tile-space coordinates end to end; `tile_types` is
  authoritative and edits rebuild the chunk mesh; island/river/height-range
  options; one noise field per resource; `getElementAtWorld` for post-edit
  queries; split sampling into `GenerateTerrain` + `MapConfig` + `Chunk.h`.
- [x] **Entities.** `CBasicNeeds` clamps with config-driven decay; the
  settlement's shared map knowledge (`helpers/Knowledge.h`) finds the nearest
  water/food and replaces the old per-entity memory, so what one entity sees is
  known to all and the store does not grow with the population; `EntityDecision`
  scales need urgency by personality and config bias; vision feeds that store;
  `MoveCost` gives every element a positive cost; `CGather`/`CInventory` bank
  work into a settlement stockpile.
- [x] **Exploration.** A settlement-wide `CivKnowledge` tracks known resource
  tiles (bucketed by element) and explored coarse cells, capped so a long run
  stays bounded. The `Explorer` job (`Jobs::Job::Explorer`) returns
  `Need::Explore` and roams to fresh land tiles within sight; the vision scan it
  triggers is what grows the shared map, without routing to a distant frontier
  that would re-plan every leg. The HUD reports explored cells and known
  locations. Knowledge tuning (`cell_size`, `max_cells`) lives in
  `config/entity_data.json`.
- [x] **Game structure and HUD.** Abstract `Scene` with `Scene_Play` and
  `Scene_Menu`; multiple HUD levels with layered/exact modes; HUD callbacks
  bound by name instead of numeric id.
- [x] **Time management.** `GameClock` keeps a 360-day calendar (year/month/day)
  and runs from 12 min/s up to 1 month/s, with pause/resume and a speed label.
  A compact top-right HUD panel shows the date, clock and speed next to
  slower/pause/faster buttons, and the simulation is driven by in-game time so a
  fast clock never outruns the walk to water and pausing freezes the world.

---

## License

This project is distributed under the MIT license. See [LICENSE](LICENSE).
