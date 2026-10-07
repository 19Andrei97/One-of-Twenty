# One Of Twenty — Roadmap

This roadmap turns the `TODO` list in [README.md](README.md) into ordered
milestones. It reflects what the code does today and what comes next; keep the
two in sync when a milestone changes status.

## Legend

- `[x]` done — implemented and covered by tests or CI
- `[ ]` planned — not started
- `[~]` in progress — partially implemented or unverified

## Current baseline

Milestones 1–4 (technical debt, map depth, entities/simulation, scenes/HUD) are
complete and folded into the baseline below. The engine core is in place:

- Window, game loop, scenes (`Scene_Play`, `Scene_Menu`), camera, and an
  EnTT-based ECS (`src/game`, `src/camera`, `src/entity_manager`).
- Chunked procedural map streamed on a `BS::thread_pool`, with noise
  continents/warp/minerals, islands, rivers and per-resource fields; sampling
  lives in a stateless `GenerateTerrain` over a `MapConfig` value type
  (`src/map_generator`).
- Tile-space coordinates end to end (`helpers/CoordMath.h`); `tile_types` is the
  authoritative per-tile map, so edits made through `setTileColor` agree with
  what is drawn and what entities query.
- Data-driven HUD (buttons, sliders, input boxes) loaded from JSON, with named
  callbacks and multiple levels (`src/hud`, `config/hud_menu_data.json`).
- Weighted, personality-scaled entity decisions over needs and memory, with
  terrain-cost movement and a gather-to-stockpile work loop (`src/entity_manager`).
- JSON config loading with clear errors, logging via spdlog, a `GameClock`
  driving day/time, and pure helpers (`src/helpers`).
- Unit tests (doctest) across coordinates, `SharedContainer`, `GameClock`,
  config, map lifetime/determinism, decisions, move cost, HUD and scenes.
- CI on Linux + Windows plus an ASan/UBSan job (`.github/workflows/build.yml`)
  and a real-game smoke test (`.github/workflows/game-smoke.yml`).

---

## Deferred — Civilization

The long-term goal, parked until the simulation loop below is solid. Revisit
once survival, pathfinding and the settlement economy exist.

- [ ] Add a city center (`Entity`).
- [ ] Add AI for civilization politics via llama (`Entity`).

**Done when:** a city center anchors settlement behavior and politics can run
against an LLM backend behind an interface that is mockable in tests.

---

## Proposed milestones

Ordered by dependency: each milestone makes the next one possible. The first
two are the highest value because they close the core simulation loop.

### Milestone 1 — Survival and population dynamics

Today entities never die from needs, never reproduce, and nothing consumes the
stockpile, so population is static. Finish the loop so a run produces a story.

- [ ] Actually age entities: `CLifespan` is emplaced but never decremented, so
      nothing ever reaches the removal path. Drive it from the clock.
- [ ] Make needs lethal: at zero thirst/hunger (and full sleep debt) an entity
      dies, is removed, and the settlement population drops.
- [ ] Consume the stockpile: eating/drinking should draw from stored units when
      available instead of always succeeding, so gathering has a purpose.
- [ ] Add reproduction or immigration so population can recover, gated on
      settlement comfort (food/water stock and housing).
- [ ] Expose population and vital stats (births, deaths, stock) for tests and
      the HUD.
- [ ] Unit-test the loop: a starved entity dies, a supplied settlement grows,
      and the stockpile drains when it is eaten.

**Done when:** running the game for several simulated days changes the
population, and the rules are covered by deterministic unit tests.

### Milestone 2 — Pathfinding and collision

Entities walk in a straight line to their target and ignore terrain, so they
cross oceans and mountains. Make movement physical.

- [ ] Implement a grid path (A* or a flow field) over the `MoveCost` map so
      entities route around water and prefer cheap ground.
- [ ] Follow the path in the movement system instead of aiming at the target
      directly; keep `CMoving` as the intent, add the route behind it.
- [ ] Implement `Scene_Play::sCollision` (currently empty): keep entities out of
      water and separate overlapping entities.
- [ ] Make unreachable targets fail gracefully (re-plan, then re-decide).
- [ ] Unit-test pathfinding on a small cost grid, including a case where the
      straight line is blocked and the path detours.

**Done when:** a target across water is reached only via a land route, and paths
are tested without a renderer.

### Milestone 3 — Settlement economy and jobs

Turn "gather the nearest resource" into production with roles and buildings.

- [ ] Give entity types behavior: `Human_Farmer`, `Human_Lumberjack` (and the
      animal types) are defined but unused; bind jobs to them.
- [ ] Add recipes / production chains that convert raw stock (wood, stone, clay,
      iron, silver) into goods, so different resources matter.
- [ ] Add a stockpile HUD panel showing counts and rates over time.
- [ ] Add placeable buildings on tiles, reusing `setTileColor` and the chunk
      mesh rebuild so structures render and persist in the world.
- [ ] Unit-test the economy: a recipe consumes inputs and produces outputs, and
      job assignment responds to shortages.

**Done when:** a settlement produces a surplus from specialized jobs, visible in
the HUD and verified by tests.

### Milestone 4 — Presentation and UX

Make the simulation legible and pleasant to watch.

- [ ] Drive a day/night tint and lighting from `GameClock`.
- [ ] Replace the debug circles with sprites/animations and add a camera that
      can follow an entity.
- [ ] Add a minimap and a stats overlay (population, stockpile, clock).
- [ ] Turn `Scene_Menu` into a real front end: new game (seed/options), load,
      settings, quit; add a pause overlay.
- [ ] Optional: SFML audio for ambience and events.

**Done when:** the game communicates its own state without debug overlays, and
the menu can start and configure a run.

### Milestone 5 — Persistence and save/load

Nothing survives a restart today; a run cannot be resumed or shared.

- [ ] Serialize world state: seed, clock, entities, stockpile, and edited tiles.
- [ ] Load it back and reconstruct an equivalent simulation.
- [ ] Keep edits as a log over the seed so a save stays small and deterministic.
- [ ] Add a version field and reject incompatible saves with a clear error.
- [ ] Unit-test round-trip: save, load, and compare state and determinism.

**Done when:** a saved game reloads to the same population, stockpile and map.

### Milestone 6 — Performance and scale

Prepare for hundreds of entities without frame drops.

- [ ] Index tile resource queries (e.g. per-chunk resource lists) instead of
      rescanning the vision square for every entity every frame.
- [ ] Add a spatial index for entity lookups (neighbour queries, collision).
- [ ] Update entity memory incrementally rather than rebuilding it per frame.
- [ ] Add a headless benchmark (entities x ticks) to CI so regressions show up.
- [ ] Set and test a target: e.g. 500 entities at 60 fps on the CI machine.

**Done when:** the benchmark target is met and guarded by a test.

---

## Tracking

- Update this file when a milestone's status changes.
- Update the `TODO` section in [README.md](README.md) as items complete.
