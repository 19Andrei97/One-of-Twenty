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
- JSON config loading with clear errors, logging via spdlog, and pure helpers
  (`src/helpers`). A `GameClock` drives a 360-day calendar (year/month/day),
  tracks time to the minute, and runs at a selectable speed from 12 min/s up
  to 1 month/s, with pause/resume and a speed label for the HUD.
- **Time management HUD.** A compact top-right panel shows the date, clock
  and speed, with slower/pause/faster buttons wired to the clock, so a run
  can be watched slowly or fast-forwarded over a lifetime.
- **Survival and population dynamics.** Entities age (`CLifespan`) and die of
  old age; starvation and dehydration drain `CHealth` (a slow resource separate
  from the needs) while a comfortable entity recovers; eating and drinking draw
  on the settlement stockpile; comfortable adults reproduce on their own timer
  (`CReproduction`); and population/vital stats reach the HUD. Movement and the
  survival systems both run on in-game time, so the clock speed never outruns
  the walk to water and pausing freezes the settlement. The seeded population
  founds itself on a habitable coastal site with water and forage in reach, so a
  run produces a story instead of a static crowd.
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

## On the side — Survival depth

Implemented and kept out of the active sequence for now: the survival loop runs
(entities age and die, needs turn lethal once an entity is already struggling,
the settlement reproduces). The follow-ups below stay parked until the economy
and observability milestones land, because tuning them needs measurement.

- [x] Add health as a slow resource separate from the needs, damaged by
      starvation/dehydration and restored by eating and resting.
- [ ] Add shelter/housing: a bed to sleep in and a home tile, so sleep and
      reproduction depend on more than raw comfort.
- [ ] Add illness/weather events that drain health and spread between close
      entities, with a simple cure (herbs/medicine) to counter them.
- [x] Unit-test the health curve: starving lowers health and a comfortable
      entity recovers it.

**Done when:** an entity can be sick-but-alive and recover, and the HUD shows
health alongside the needs.

---

## Milestones

Ordered by dependency: each milestone makes the next one possible. Pathfinding
and collision is complete and folded into the baseline above.

### Milestone 1 — Settlement economy and jobs

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

### Milestone 2 — Observability and tuning

The simulation is hard to balance by eye, and the first day already shows it: a
vision-radius trip costs several in-game hours at the default `timeScale` of 120,
and an entity committed to a non-survival action does not re-prioritize when a
need turns critical, so the starting settlement can lose members before it
settles. Make behaviour measurable before adding more systems.

- [ ] Add a lightweight event log (births, deaths, gathers, discoveries) with
      cause and in-game timestamp, queryable in tests.
- [ ] Record a run's population/stockpile over time and expose a headless
      summary, so a balance change can be compared against a baseline.
- [ ] Support reloading `entity_data.json` at runtime so tuning does not need a
      rebuild.
- [ ] Let a busy entity interrupt its current action when a survival need turns
      critical, so a long trip does not kill it.
- [ ] Unit-test that a given config produces the expected steady-state
      population band over a fixed number of simulated days.

**Done when:** a balance change can be justified by numbers rather than by
watching the window, and a fresh settlement survives its first day.

### Milestone 3 — Threats and defense

Survival has no antagonist yet. Add pressure so the settlement has a reason to
grow, store and build.

- [ ] Spawn hostile wildlife (the unused animal entity types) that hunts or raids
      the settlement.
- [ ] Add combat: entities can fight, flee or be wounded.
- [ ] Add walls/defenses as placeable tiles, reusing the building path from
      Milestone 1.
- [ ] Make threats scale with time or population so the early game stays calm.
- [ ] Unit-test a raid: attackers damage defenders, and a wall blocks a path.

**Done when:** an unattended settlement can be harmed, and a defended one can
repel the attack.

### Milestone 4 — Social bonds and society

The premise is "one of twenty": people, not units. Give the population
relationships that shape behavior.

- [ ] Track kinship and relationships (parent/child, partners, friends) formed
      by proximity and shared work.
- [ ] Let relationships bias decisions: help, share food, follow, or avoid.
- [ ] Add roles and leadership so a settlement can organize, not just survive.
- [ ] Drive reproduction and child-rearing from relationships rather than a
      settlement-wide comfort check.
- [ ] Unit-test that a relationship changes a decision and that a child inherits
      a parent link.

**Done when:** individuals have relationships that visibly change what they do.

### Milestone 5 — Performance and scale

Prepare for hundreds of entities (more people, wildlife and buildings) without
frame drops. Memory refresh is already per-tile rather than per-frame; the rest
of the scan is still repeated.

- [ ] Index tile resource queries (e.g. per-chunk resource lists) instead of
      rescanning the vision square for every entity.
- [ ] Add a spatial index for entity lookups (neighbour queries, collision).
- [ ] Add a headless benchmark (entities x ticks) to CI so regressions show up.
- [ ] Set and test a target: e.g. 500 entities at 60 fps on the CI machine.

**Done when:** the benchmark target is met and guarded by a test.

### Milestone 6 — Presentation and UX

Make the simulation legible and pleasant to watch, once the simulation is worth
watching.

- [ ] Drive a day/night tint and lighting from `GameClock`.
- [ ] Replace the debug circles with sprites/animations and add a camera that
      can follow an entity.
- [ ] Add a minimap and a stats overlay (population, stockpile, clock).
- [ ] Turn `Scene_Menu` into a real front end: new game (seed/options), load,
      settings, quit; add a pause overlay.
- [ ] Optional: SFML audio for ambience and events.

**Done when:** the game communicates its own state without debug overlays, and
the menu can start and configure a run.

---

## Tracking

- Update this file when a milestone's status changes.
- Update the `TODO` section in [README.md](README.md) as items complete.
