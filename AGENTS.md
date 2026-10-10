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
  starts — so a need with no remembered target cannot thrash the entity. Sleep is
  the only per-entity survival need, so a sleep plan is the only non-interruptible
  one; food is a settlement-level stock (see below), not an entity decision.
- `EntityManager::reloadConfig` re-reads the entity JSON *and* the building
  catalog in place; the `R` key in `Scene_Play` binds it. Add new tuning as a
  member of `EntityConfig` so reload picks it up for free.
- Buildings live in `helpers/Buildings.h`, a pure data-driven catalog parsed from
  `config/buildings.json` (`Def` cost/color/recipes/`walk_cost`, plus a
  `Settlement` block). `EntityManager` places sites by spending the catalog cost
  and writing the element into `tile_types`, then builders finish them over
  `build_hours`. `populationCapacity()` is the entity config's
  `survival.max_population` base plus each *completed housing def's*
  `population_capacity` bonus (a def with `population_capacity > 0`), so houses
  raise the cap and an incomplete site does not. `Buildings::walkCost(element, catalog)` layers the catalog override
  over `MoveCost` for both movement and pathfinding. Keep catalog lookups by
  `byId`/`byElement`, not by rebuilding maps.
- Building placement goes through one rule, `EntityManager::canBuildOn`, which
  `findBuildSite` (the planner) and `placeBuilding` (the API) both call so the
  two paths cannot diverge. It rejects the tile unless `Buildings::defaultBuildable`
  passes (dry land: never sand, ocean or lake) *and* the def's optional
  `allowed_terrain` admits it, then requires the settlement's `min_spacing_tiles`
  gap (Chebyshev, so diagonal neighbours count) from every placed building. Roads
  are exempt from the gap (a road must touch what it connects). The allow-list is
  JSON-driven, so a building restricted to a specific tile is a data edit; an
  empty list means "any dry land". Do not re-add an ad-hoc `isOcean`/occupancy
  test in `findBuildSite` or `placeBuilding`.
- `findBuildSite` scatters the site instead of filling the nearest ring: it seeds
  a radius-limited flood fill from a random point near the anchor and returns the
  nearest free tile it meets, so successive buildings differ in direction and in
  standoff distance. Keep the jitter a *fraction* of `build_radius_tiles`
  (currently `radius / 6`); a wide jitter lands farms beyond a builder's reach
  before the settlement is fed and the population starves out (an over-large
  window regressed `entities never stand in the ocean during a run`). The site is
  derived from `Random`, so a whole run's layout depends on the global MT state.
- Housing is demand-gated: `Buildings::housesWanted(heads, capacity, per_house)`
  is the pure rule (a def is housing when `population_capacity > 0`), and
  `planConstruction` only raises a house while the settlement has no free bed
  (plus a one-house buffer), so it never spends wood on capacity nobody needs.
  The rule fires at `heads >= capacity`, not `heads > capacity`: births stop *at*
  capacity, so waiting for a shortfall would deadlock growth (the settlement could
  never raise the house that lets it grow). `planConstruction` counts placed
  housing excluding the anchor, because the city center's beds are already in
  `capacity`; counting it would let the anchor satisfy the demand alone and no
  house would ever go up. The
  per-house size is the housing def's own `population_capacity` (shipped 30),
  which is also what `populationCapacity()` adds once the house is complete;
  `survival.max_population` in the entity config stays the founding base cap
  (shipped 20, so the first houses are demanded early rather than at a distant cap).
  `populationCapacity()` must add `rolled_value` for *housing defs only*: a farm's
  rolled value is its food reach, so counting it would let every farm inflate the
  cap, inviting the births that demand the next farm (the endless-farm loop).
- Food is demand-gated too: `planConstruction` raises a farm only while the
  *completed* farms' summed `rolled_value` (their reach) falls short of the people
  present AND the store is not already comfortable. `Buildings::needsFoodProducer`
  has a stock-aware overload taking `stockedFood`/`dailyPerPerson`/`reserveDays`
  (`economy.food_reserve_days`, shipped 3): a granary covering every mouth for that
  many days suppresses new farms entirely, so a fed settlement stops spending wood
  on food it has. At most one farm is ever under construction, and only completed
  reach counts, so an in-progress site cannot suppress the next farm and starve the
  settlement. Keep the store gate a pure function of the stock, not a timer.
- A farm *yields* its rolled reach: `produceGoods` credits a completed food
  producer (`feeds_population > 0`) its `rolled_value` food per in-game day, a
  24th per hourly step with the remainder carried in `PlacedBuilding::food_progress`
  so the daily total is exact for any reach. Do not go back to running the farm's
  recipe once an hour: that flat 24/day starved any settlement past ~20 people and
  made the planner raise farms that could not close the gap. A def with no rolled
  reach (a unit-test farm) still falls back to `Buildings::produceOnce`, so the
  pure-recipe path is unchanged for it.
- `Def::hasRecipes()` (recipes not empty) is what marks a workshop-like producer;
  a farm carries a recipe too but is labour, so distinguish the two by
  `feeds_population`, not by the element. Workshops have no demand signal (nothing
  consumes planks/tools yet), so they are bounded by `max_count` in the catalog
  (shipped 4); without a cap they spam the village.
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
- Terrain is a *layered* pipeline, sampled in `GenerateTerrain::sampleAt`:
  domain warp -> continent fBm -> ridged mountain belts -> height remap ->
  island falloff -> elevation bands -> lakes -> climate biomes. Keep
  the stages in that order; each later stage assumes the earlier ones.
  `MapConfig` now carries the tuning as flat, player-named fields
  (`land_amount`, `continent_size`, `coast_roughness`, `mountain_height`,
  `mountain_scale`, `temperature`, `rainfall`, `snow_line`,
  `lake_level`, `lake_size`, `ore_richness`), each parsed from a
  top-level key in `config/map_data.json` and each backed by one HUD slider.
- The ridged-detail fractal in FastNoiseLite already returns `[-1,1]`; only its
  positive half may be added as uplift (clamp at 0). Remapping it (`*2-1`) or
  adding the negative half drives whole regions below sea level and drowns the
  map - the failure looks like a world that is ~99% ocean with a speck of land.
- `land_amount` is the sea level and dominates the land fraction; the shipped
  config uses `0.35` for a roughly 60/40 ocean/land split. `height_range.min`/
  `max` remap the continent field before thresholds (default `[0,1]` =
  identity); raising `min` above ~0.1 removes the flat sandy lowlands. The
  shipped config uses `min = 0.0` for wide lowlands, so the world origin is
  often a tiny coastal islet with almost no forage. `EntityManager::findHabitableSpawn`
  therefore founds the settlement at the nearest land tile that has a
  *gatherable* (forest for wood, hill for stone) within an entity's vision, and
  falls back to the nearest land tile when none qualifies. Wood gates the first
  farm, which is the only food source, so a settlement founded away from any
  gatherable would starve before it could build. Water is no longer a need, so
  the spawn no longer weighs it.
- Rivers were removed: the single-frequency zero-crossing field produced
  disconnected cyan specks that read as unnatural, so the map no longer carves
  river channels. Do not re-add a river element or its noise field without a
  real flow/erosion model.
- Lakes flood a low basin (elevation between `lake_level` and the `hill`
  threshold) where the lake field peaks, and are checked *before* the beach so a
  basin reads as water, not sand. A low lake frequency with a high `lake_size`
  (~0.62) gives a few larger, natural basins rather than many small ones.
- Climate is separate from elevation: `temperature` falls off with latitude
  (`|tile.y|`, reaching the pole band by ~6000 tiles) and `moisture` is its own
  field, both shifted by their sliders. `classifyLand` only re-classifies *land*
  (forest vs hill by a moisture/temperature "lushness"), and snow needs both a
  high `snow_line` crossing and cold temperature, so the poles read as bare rock
  and only the cold peaks turn white.
- Ore is one noise field per resource (clay/iron/silver seeded apart) sampled at
  the warped coordinate. Each has a fixed base cutoff tuned to a few percent of
  its land band (`hill`/`forest`/`mountain`); `ore_richness` shifts all three
  together, so a single slider is the whole ore control. Do not route ore
  cutoffs through the `heights` array - that array is elevation bands only.
- The map palette (`elements` in `config/map_data.json`) is deliberately dark
  and desaturated (muted ocean/forest/sand, only snow is bright) so the map does
  not read as psychedelic.
- Terrain HUD sliders are *tabbed across levels* in `config/hud_menu_data.json`
  (Land+Mountains on level 0, Climate on 1, Water+Ore on 2); `H`/`Tab` cycles
  levels. Each slider has a `description` drawn under it and an explicit `value`
  so the handle starts at the config's actual setting. `CSlider` takes the
  description and initial value as its last constructor arguments.
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
- `CHealth` is a slow resource separate from the needs: a food shortfall drains
  it, a rested entity regenerates it, and death happens at zero. Keep this
  indirection so a missed meal does not kill a healthy entity instantly.
- Food is a *settlement-level* resource, not a per-entity need: `consumeFoodDaily`
  draws `food_per_person_per_day` per head once per in-game day; each entity left
  unfed accrues `days_without_food`, and `lethal_days_without_food` consecutive
  days start draining health. Sleep (`sleep_gain_per_hour`) is the only per-entity
  need. Farms are the *only* food source (`Resources::isFood` is farm-only);
  wild forest/hill yield wood/stone via gathers, never food.
- A forest tile is a finite wood pile (`Chunk::tree_wood`, sized at generation
  from `tree_wood_min`/`tree_wood_max` via `GenerateTerrain::treeAmountAtTile`);
  `MapGenerator::harvestWood` decrements it and clears the tile to a hill at zero,
  rebuilding the mesh. Gathers credit `Goods::fromElement`, so a farm yields food
  and a forest wood with no special case in the gather handler.
- Work must be done *on* the tile. `CGather`/`CBuild` are queued before the walk,
  so their timer (`timestamp_min`) is stamped on arrival, not at plan time, via a
  `started` latch in the finish-action pass: the move is popped on arrival, so a
  work action at the front means the entity just arrived. Do not time work from
  the queue stamp again — a plan-timed action finished its hour mid-journey and
  banked the yield (or advanced the site) while the entity was still walking, up
  to a tile or more away. `EntityManager::offTileWork()` counts completions more
  than a tile from the target and is asserted zero in tests, so a resumed
  plan-time regression fails loudly instead of only showing on screen.
- `EntityManager::resolveCollisions` anchors an entity whose front action is a
  gather/build to its tile: separation pushes only the neighbour, and the de-stack
  pass never fans a working entity out. Without this, the collision push shoved
  workers off the tile while they were gathering/building, so the completion (and
  the site's finished building) appeared a tile or more away.
- EntityManager writes a building into a tile with `setTileColor`, which only
  succeeds once that tile's chunk is loaded. A test that drives the real update
  loop must stream chunks (render the view) or construction silently fails and the
  settlement starves; see `tests/MapStream.h`.
- `config/entity_data.json` `survival` accepts both the newer friendly units
  (`lifespan_years`, `birth_interval_days`) and the older hours keys
  (`lifespan_hours`, `birth_cooldown_hours`); keep both loadable.
- Time HUD: `Hud` owns the `time_slower`/`time_faster`/`time_pause` callbacks
  (registered in `registerDefaultCallbacks`, acting on the clock set via
  `setClock`). `Scene_Play` overrides `time_pause` so its own `m_paused` flag
  stays in step with the clock. The HUD stays interactive while paused.
- View-only input is *not* gated on `m_paused`: the camera keys, the mouse wheel
  zoom and the tile readout all run without the `!m_paused` guard, and
  `Scene_Play::sCamera()` is called from `update()` outside the paused branch, so
  a frozen world can still be panned and inspected. Only the simulation systems
  (`sMovement`, `sCollision`, `M`/`G`/`1` keys) stay paused. Camera key
  *releases* are also outside the guard, so a key held across a pause does not
  stick down.
- The per-entity readout is hover-only: `Scene_Play` resolves the pointer's world
  position once per frame (`updateHover`, called from `sRender`) and hands it to
  `EntityManager::setHoverWorld`; `render` draws an info box only for
  `entityAtWorld()`'s hit (nearest body within a grab radius), and builds that
  box's lines on demand rather than a text panel per entity every frame.
