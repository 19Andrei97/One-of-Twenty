# One Of Twenty — Roadmap

This roadmap turns the `TODO` list in [README.md](README.md) into ordered
milestones. It reflects what the code does today and what comes next; keep the
two in sync when a milestone changes status.

## Legend

- `[x]` done — implemented and covered by tests or CI
- `[ ]` planned — not started
- `[~]` in progress — partially implemented or unverified

## Current baseline

The engine core and the survival loop are in place:

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
  callbacks, multiple levels and a persistent stats panel (`src/hud`,
  `config/hud_menu_data.json`).
- Weighted, personality-scaled entity decisions over needs and memory, with
  terrain-cost movement and a gather-to-stockpile work loop (`src/entity_manager`).
- **Pathfinding and collision.** Entities route over the terrain cost map with a
  pure A* (`helpers/Pathfinding.h`) instead of walking in a straight line, so
  they go around oceans and prefer cheap ground. `Scene_Play::sCollision` keeps
  the settlement out of the sea and separates overlapping entities, and
  unreachable targets fall back to exploring rather than stalling.
- JSON config loading with clear errors, logging via spdlog, a `GameClock`
  driving day/time, and pure helpers (`src/helpers`).
- **Survival and population dynamics.** Entities age (`CLifespan`) and die of
  old age; thirst/hunger become lethal once an entity is already struggling;
  eating and drinking draw on the settlement stockpile; the settlement
  reproduces when it is comfortable; and population/vital stats reach the HUD.
  The seeded population founds itself on a habitable coastal site with water
  and forage in reach, so a run produces a story instead of a static crowd.
- Unit tests (doctest) across coordinates, `SharedContainer`, `GameClock`,
  config, map lifetime/determinism, decisions, move cost, HUD, scenes and
  survival/population.
- CI on Linux + Windows plus an ASan/UBSan job (`.github/workflows/build.yml`)
  and a real-game smoke test (`.github/workflows/game-smoke.yml`).

---

## Deferred — Civilization

The long-term goal, parked until the simulation loop below is solid. Revisit
once the settlement economy exists.

- [ ] Add a city center (`Entity`).
- [ ] Add AI for civilization politics via llama (`Entity`).

**Done when:** a city center anchors settlement behavior and politics can run
against an LLM backend behind an interface that is mockable in tests.

---

## Parked — Persistence and save/load

Set aside for now: the simulation still changes too quickly for a save format to
be worth freezing. Pick this up once the economy and jobs below have settled.

- [ ] Serialize world state: seed, clock, entities, stockpile, and edited tiles.
- [ ] Load it back and reconstruct an equivalent simulation.
- [ ] Keep edits as a log over the seed so a save stays small and deterministic.
- [ ] Add a version field and reject incompatible saves with a clear error.
- [ ] Unit-test round-trip: save, load, and compare state and determinism.

**Done when:** a saved game reloads to the same population, stockpile and map.

---

## Milestones

Ordered by dependency: each milestone makes the next one possible. Milestone 1
is complete; the rest are planned.

### Milestone 1 — Pathfinding and collision

Entities still walk in a straight line to their target and ignore terrain, so
they cross oceans and mountains. Make movement physical.

- [x] Implement a grid path (A* or a flow field) over the `MoveCost` map so
      entities route around water and prefer cheap ground.
- [x] Follow the path in the movement system instead of aiming at the target
      directly; keep `CMoving` as the intent, add the route behind it.
- [x] Implement `Scene_Play::sCollision` (currently empty): keep entities out of
      water and separate overlapping entities.
- [x] Make unreachable targets fail gracefully (re-plan, then re-decide).
- [x] Unit-test pathfinding on a small cost grid, including a case where the
      straight line is blocked and the path detours.

**Done when:** a target across water is reached only via a land route, and paths
are tested without a renderer.

**Follow-up (moved to Milestone 6).** A vision-radius trip takes several
in-game hours at the default `timeScale` of 120, and an entity committed to a
non-survival action does not re-prioritize when a need turns critical, so the
starting settlement can lose members on its first day. This is a balance
problem, not a pathing one: drinking and eating do fire (needs reset on
arrival). Address it with the tuning work below.

### Milestone 2 — Settlement economy and jobs

Turn "gather the nearest resource" into production with roles and buildings.

- [ ] Give entity types behavior: `Human_Farmer`, `Human_Lumberjack` (and the
      animal types) are defined but unused; bind jobs to them.
- [ ] Add recipes / production chains that convert raw stock (wood, stone, clay,
      iron, silver) into goods, so different resources matter.
- [ ] Make food a real resource: foraging, farms and spoilage, so hunger is
      supplied by production rather than the current tile fallback.
- [ ] Add a stockpile HUD panel showing counts and rates over time.
- [ ] Add placeable buildings on tiles, reusing `setTileColor` and the chunk
      mesh rebuild so structures render and persist in the world.
- [ ] Unit-test the economy: a recipe consumes inputs and produces outputs, and
      job assignment responds to shortages.

**Done when:** a settlement produces a surplus from specialized jobs, visible in
the HUD and verified by tests.

### Milestone 3 — Survival depth: health, illness and shelter

The survival loop is binary today (alive until a need hits zero). Give it
gradients and recovery so a run has texture.

- [ ] Add health as a slow resource separate from the needs, damaged by
      starvation/dehydration and restored by eating and resting.
- [ ] Add shelter/housing: a bed to sleep in and a home tile, so sleep and
      reproduction depend on more than raw comfort.
- [ ] Add illness/weather events that drain health and spread between close
      entities, with a simple cure (herbs/medicine) to counter them.
- [ ] Unit-test the health curve: starving lowers health, feeding restores it,
      and a sheltered entity recovers faster than an exposed one.

**Done when:** an entity can be sick-but-alive and recover, and the HUD shows
health alongside the needs.

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

### Milestone 5 — Performance and scale

Prepare for hundreds of entities without frame drops.

- [ ] Index tile resource queries (e.g. per-chunk resource lists) instead of
      rescanning the vision square for every entity every frame.
- [ ] Add a spatial index for entity lookups (neighbour queries, collision).
- [ ] Update entity memory incrementally rather than rebuilding it per frame.
- [ ] Add a headless benchmark (entities x ticks) to CI so regressions show up.
- [ ] Set and test a target: e.g. 500 entities at 60 fps on the CI machine.

**Done when:** the benchmark target is met and guarded by a test.

### Milestone 6 — Observability and tuning

The simulation is hard to balance by eye. Make its behaviour measurable.

- [ ] Add a lightweight event log (births, deaths, gathers, discoveries) with
      cause and in-game timestamp, queryable in tests.
- [ ] Record a run's population/stockpile over time and expose a headless
      summary, so a balance change can be compared against a baseline.
- [ ] Support reloading `entity_data.json` at runtime so tuning does not need a
      rebuild.
- [ ] Let a busy entity interrupt its current action when a survival need turns
      critical, so a long trip does not kill it (see the Milestone 1 follow-up).
- [ ] Unit-test that a given config produces the expected steady-state
      population band over a fixed number of simulated days.

**Done when:** a balance change can be justified by numbers rather than by
watching the window.

---

## Tracking

- Update this file when a milestone's status changes.
- Update the `TODO` section in [README.md](README.md) as items complete.
