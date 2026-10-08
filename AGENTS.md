# AGENTS.md

Repository notes for AI agents working on One Of Twenty.

## What this is

C++17 simulation game: SFML3 (graphics/window), EnTT (ECS), spdlog (logging),
nlohmann-json (config), BS::thread_pool (map generation workers), doctest
(tests). CMake 3.20+.

## Build and test

```bash
cmake --preset default                 # Ninja + Release, in ./build
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

- Dependencies are located with `find_package` and fall back to FetchContent
  with pinned tags (see `CMakeLists.txt`). Do not unpin them.
- `-DONE_OF_TWENTY_BUILD_TESTS=OFF` disables the test target.
- Assets under `config/` and `fonts/` are copied to `build/bin/` at configure
  time. After editing a config or font, re-run CMake configure.

### Running headlessly (CI / containers)

The game needs an X11 display; without one SFML aborts during static
initialization, before `main`. Use Xvfb:

```bash
cd build/bin && xvfb-run -a ./OneOfTwenty
```

A run that is killed by `timeout` (exit 124) is a success; check
`logs/game.log` for startup traces.

## Conventions and gotchas

- Most `.cpp` files include `<pch.h>` first; it aggregates the common headers.
  A header used directly by a test must be self-contained (include what it
  uses), because tests do not go through the pch.
- Terrain sampling lives in `generate_terrain.{h,cpp}` (`GenerateTerrain`),
  a stateless object over a `MapConfig` value type; `Chunk.h` holds `Chunk`
  and the `ChunkMap` alias. `MapGenerator` is streaming/rendering plus the
  chunk store, and delegates all sampling. Keep noise out of `MapGenerator`.
- `MapGenerator::worldToTile` / `tileToWorld` delegate to
  `helpers/CoordMath.h` (floor division so negative coordinates map to the
  correct tile). Keep them as the single source of truth for conversions.
- Load config files with `loadJsonFile()` (`helpers/Config.h`) so missing or
  malformed files raise a clear `std::runtime_error`.
- The logger level comes from `logger.level` in `config.json`; parse it with
  `Logger::levelFromString()` (`helpers/Logger.h`, a namespace), which is
  case-insensitive and throws on an unknown value.
- Queries that must match the rendered map use `getElementAtWorld`, which reads
  the loaded chunk's `tile_types` (post-edit) and only falls back to noise
  when the chunk is not loaded. `getTileCost` holds `t_mutex`, so it reads
  `tile_types` directly instead of calling the locking accessor.
- Entity pathfinding is a pure, header-only A* in `helpers/Pathfinding.h`; it
  takes cost/walkable callables so it is unit-tested without a map.
  `EntityManager::findRoute` snapshots tiles via `MapGenerator::copyTileBlock`
  (one lock for a whole block) and turns the tile path into world-space
  waypoints stored on the entity's `CPath`. Movement follows the waypoints and
  falls back to a straight line when there is no route. Water is not walkable;
  a drink target in the sea is approached from the nearest land tile.
  `Scene_Play::sCollision` calls `EntityManager::resolveCollisions`, which keeps
  entities on land and separates overlapping ones.
- Entity vision feeds one *settlement-wide* knowledge store,
  `CivKnowledge` (`helpers/Knowledge.h`, SFML/EnTT-free), not a per-entity
  memory. Every entity's scan merges into the same store, so a resource found by
  one is known to all and the store does not grow with the population. Entities
  carry only `CKnowledgeScan` (the last scanned tile, to avoid rescanning every
  frame). Knowledge keeps known resource tiles bucketed by element (nearest wins
  per query) and *coarse* explored cells capped by `max_cells`; do not replace
  the coarse cells with per-tile coverage or it grows without bound.
- The `Explorer` job (`Jobs::Job::Explorer`) maps rather than gathers:
  `EntityDecision::decide` returns `Need::Explore` for it, and
  `EntityManager::startActionFor` roams to a random land tile within sight (the
  same local step as `Wander`), never routing to a distant frontier. Do not
  re-add frontier A*: the frontier drifts as knowledge grows, so it re-planned
  every leg toward an often-unreachable target (tens of thousands of route
  attempts over a population run, almost all failing), and each attempt copied a
  ~1k-tile window. The vision scan the roaming triggers is what grows the map.
- `copyTileBlock` returns a *dense row-major* `std::vector<Elements>` and its
  clamped side; `findRoute` reads tiles by index. Keep it dense (not a hash map):
  the block copy dominates pathfinding, and a hash insert per tile was the bulk
  of the explorer regression.
- Observability lives in two pure, SFML/EnTT-free headers: `helpers/EventLog.h`
  (a bounded ring of timestamped events with cause, queried with `countOf`/
  `since`/`recent`) and `helpers/RunSummary.h` (`RunHistory` samples the
  population/stock per in-game day and summarizes them). EntityManager records
  into both; keep new gameplay events flowing through `EventLog::record`.
- A busy entity interrupts a non-survival plan when a need turns critical
  (`EntityDecision::interruptFor`, used in the decision loop). The replan is
  transactional — the old plan is restored unless the urgent action actually
  starts — so a need with no remembered target cannot thrash the entity. Keep
  survival plans (Eat/Drink/Sleep) non-interruptible by other survival needs.
- `EntityManager::reloadConfig` re-reads the entity JSON *and* the building
  catalog in place; the `R` key in `Scene_Play` binds it. Add new tuning as a
  member of `EntityConfig` so reload picks it up for free.
- Buildings live in `helpers/Buildings.h`, a pure data-driven catalog parsed from
  `config/buildings.json` (`Def` cost/color/recipes/`walk_cost`, plus a
  `Settlement` block). `EntityManager` places sites by spending the catalog cost
  and writing the element into `tile_types`, then builders finish them over
  `build_hours`. `populationCapacity()` is the entity config's
  `survival.max_population` base plus each *completed* building's
  `population_capacity` bonus, so houses raise the cap and an incomplete site
  does not. `Buildings::walkCost(element, catalog)` layers the catalog override
  over `MoveCost` for both movement and pathfinding. Keep catalog lookups by
  `byId`/`byElement`, not by rebuilding maps.
- Entity appearance is data-driven: `helpers/Appearance.h` holds a `Look`
  (shape/color/outline/size) and builds the `CShape` circle; the `appearance`
  block in `config/entity_data.json` configures it per entity type (`types`) and
  per job (`jobs`). `EntityManager::lookFor` resolves job-over-type, the spawn
  and `reassignJobs` repaint, and `reloadConfig` repaints every live entity so
  `R` shows a change at once. Keep the default (white 10-unit circle) for an
  unconfigured entry so older configs are unchanged.
- Ores use one noise field each (`m_noise_clay`/`iron`/`silver`, seeded apart)
  rather than a shared mineral field; `getResourceValue()` samples a field.
- The chunk map `c_chunks` is shared with worker threads; guard access with
  `t_mutex`. Never hold `t_mutex` while touching a `SharedContainer`
  (`tc_chunks_*`): those lock themselves, so nesting breaks the lock order.
  Prefer `LOG_TRACE` for anything on a hot path.
- Coordinates inside `MapGenerator` are tiles, not pixels. Chunk keys and
  `Chunk::position` are top-left tile coordinates; `c_chunk_tiles` is the chunk
  size in tiles. Convert to pixels only when emitting vertices, via
  `tileToWorld` / `worldToTile` (`helpers/CoordMath.h`). Noise is sampled in
  tile space so terrain does not depend on `tile_size`.
- `generateChunk(tiles_per_side, tile_position)` takes the tile position
  directly; islands, rivers and lakes are opt-in via `island.*` / `river.*` /
  `lake.*` in `config/map_data.json`. `height_range.min`/`max` remap the
  continent field before thresholds (default `[0,1]` = identity).
- Rivers are the zero crossing of a *single low-frequency Perlin* field
  (`river.freq` ~0.0035, `river.threshold` ~0.012). One octave keeps the zero
  contour a long, smooth, meandering channel; a ridged/multi-octave field
  shatters the same coverage into thousands of disconnected specks (measured:
  ~2k components vs ~140). Do not re-add octaves to the river field.
- Lakes flood a low basin (elevation between `lake.level` and the `hill`
  threshold) where the lake field peaks, and are checked *before* the beach so a
  basin reads as water, not sand. A low `lake.freq` (~0.004) with a high
  `lake.threshold` (~0.62) gives a few larger, natural basins rather than many
  small ones.
- The map palette (`elements` in `config/map_data.json`) is deliberately dark
  and desaturated (muted ocean/forest/sand, only snow is bright) so the map does
  not read as psychedelic. `height_range.min` above ~0.1 also removes the wide
  flat sandy lowlands; values at or below ~0.05 strand the seeded settlement far
  from drinkable water (the entity-pathing run test dies out).
- A chunk's `tile_types` is the authoritative per-tile map: `elementAtTile`
  only seeds it at generation, and `buildChunkVertices` derives the drawn mesh
  from it. Edit tiles through `setTileColor` (which rebuilds the mesh) rather
  than writing to `vertices` directly. Tile→chunk lookups use `chunkOf` (floors,
  matching chunk keys), not `getNextChunkPosition` (rounds up, render-only).

## CI

`.github/workflows/build.yml` builds and tests on Ubuntu 24.04 (with the SFML
apt dependencies) and Windows 2022. The Linux test step runs under `xvfb-run`
so the render test can create a GL context. A third job builds with ASan+UBSan
(`-DONE_OF_TWENTY_SANITIZE=ON`) and runs the same suite with the suppression
files in `tests/`.

`.github/workflows/game-smoke.yml` runs the real game on Xvfb, injects input and
asserts the frame changes, then exercises the browser streamer over HTTP
(`tools/ci/smoke_test.py`). `tools/stream/stream_server.py` streams an X display
to a browser for interactive play; it is X11-only and needs `python-xlib` +
`Pillow` (`tools/stream/requirements.txt`).

# Instructions for AI Agents

## Role & Conduct
- You are a Senior C++ Developer. Maintain high professional standards.
- Keep all user responses strictly concise, brief, and to the point. No fluff.

## Workflow Rules
1. **Analyze First**: Read existing code and context before making modifications.
2. **Implementation**: Write clean, modern C++ (C++17 or C++20).
3. **Automated Testing**:
   - Write or update unit/integration tests for every feature or bug fix.
   - Run tests locally to ensure zero regressions before completing tasks.
4. **Documentation Sync**:
   - Update `README.md` (To-Do list/Status section) immediately after completing tasks.
   - Update `ROADMAP.md` if milestone status changes.

## Safety & Safeguards
- **Zero Hallucination**: Do not assume missing APIs/libraries exist. Check files or build environment.
- **Atomic Commits**: Make small, incremental changes. Never rewrite whole modules unless instructed.
- **Build Checks**: Verify the project compiles without warnings or errors before marking a task complete.

## Verification notes

- Build in `build/` with `cmake --build build -j"$(nproc)"`; the tree is
  warning-clean, so treat any new warning as a failure.
- Tests need a display: `xvfb-run -a ctest --test-dir build --output-on-failure`.
  A single `unit_tests` target runs all doctest cases.
- Do not construct `MapGenerator` with a font; the constructor is
  `MapGenerator(int& frames, const std::string& map_file)`. HUD and
  `EntityManager` take a `const sf::Font&`.
- `MapConfig::biome_colors` / `thresholds` are `std::array` indexed by
  `Elements` (see `kElementCount`), not maps. Index with
  `static_cast<std::size_t>(Elements::x)`.
- `MapGenerator::getElementAtWorld` and `getResourcesWithinBoundary` take
  `t_mutex` directly and read `c_chunks`; do not call the locking accessor from
  inside either, and compute the chunk key before locking.
- Roadmap status lives in `ROADMAP.md`; completed milestones are folded into
  "Current baseline", and `README.md` mirrors the same list.

## Simulation time

- `GameClock` (`src/helpers/GameClock.h`) is a header-only clock with a 360-day
  calendar (`GameTime::kDaysPerYear`; year/month/day helpers) tracked to the
  minute. `update(realDelta)` scales by `m_timeScale` in *in-game minutes per
  real second*; speed presets run 12 min/s to 1 month/s, with `faster`/`slower`
  stepping between them and `getSpeedLabel()` for the HUD. `setTime` jumps to a
  time of day (the game starts at 08:00 so the settlement does not sleep through
  its first hours).
- The whole simulation is driven by *in-game* time, not the real frame delta.
  `EntityManager::update()` measures elapsed minutes from the clock timestamp
  (`m_last_frame_minutes`) and steps survival hour by hour; movement spends a
  `speed * gameHours` pixel budget per frame. Never reintroduce a real-time
  factor into entity movement: at high clock speeds it would starve the
  settlement before it could walk to water, and a paused clock must freeze it.
- `EntityManager::update()` may advance several in-game hours in one frame (and
  the timestamp wraps at midnight), so step `hour = last/60 + 1 .. now/60`; do
  not key survival systems off the hour-of-day.
- Population dynamics live in `EntityManager` (`decayNeeds`, `ageEntities`,
  `applyHealth`, `killTheDying`, `tryBirths`). Death is deferred to a removal
  pass that tallies `m_deaths` once, because `killTheDying` only marks entities
  and is idempotent across hour-steps. Reproduction is per entity
  (`CReproduction`) rather than a settlement-wide cooldown.
- `CHealth` is a slow resource separate from the needs: starvation drains it,
  comfort regenerates it, and death happens at zero. Keep this indirection so a
  need emptied in one coarse step does not kill a healthy entity instantly.
- `config/entity_data.json` `survival` accepts both the newer friendly units
  (`lifespan_years`, `birth_interval_days`) and the older hours keys
  (`lifespan_hours`, `birth_cooldown_hours`); keep both loadable.
- Time HUD: `Hud` owns the `time_slower`/`time_faster`/`time_pause` callbacks
  (registered in `registerDefaultCallbacks`, acting on the clock set via
  `setClock`). `Scene_Play` overrides `time_pause` so its own `m_paused` flag
  stays in step with the clock. The HUD stays interactive while paused.
