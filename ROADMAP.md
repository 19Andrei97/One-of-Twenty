# One Of Twenty — Roadmap

This roadmap turns the `TODO` list in [README.md](README.md) into ordered
milestones. It reflects what the code does today and what comes next; keep the
two in sync when a milestone changes status.

## Legend

- `[x]` done — implemented and covered by tests or CI
- `[ ]` planned — not started
- `[~]` in progress — partially implemented or unverified

## What the game is

One Of Twenty is a political life-sim on top of a living-world simulation.

- You control **one person** in a **democracy of twenty** — you are one of the
  twenty who govern, while the civilization beneath you grows far larger. You
  hold political power, and you gain or lose it through the decisions you take,
  the events that befall the settlement, and the geopolitical conditions around
  it.
- Decisions have a **small impact at the start**: you begin with little
  political power and only the few allies your faction gives you. Influence is
  earned, not given.
- Underneath, a **civilization simulation never stops**. Your settlement grows;
  other civilizations grow alongside it; the world changes and adapts on its
  own, whether or not you act.

**Current focus: the civilization simulation.** The political layer is the
destination, but it is only worth building once the civilization it operates on
behaves believably — the economy, the wider world, and the different social
stations people hold. So the schedule below builds the simulation first and
brings the political game in once the society is alive.

**Scale is a design constraint, not an afterthought.** The population grows
without bound, so nothing may cost O(pawns²). Relationships *between* individual
pawns are therefore out of scope: what matters is a pawn's **social station and
importance** — a per-pawn attribute that scales, and that the political layer
later draws on. Detail follows importance: notables are simulated as
individuals, the masses are aggregated.

## Current baseline

The engine core and the survival loop are in place:

- Window, game loop, scenes (`Scene_Play`, `Scene_Menu`), camera, and an
  EnTT-based ECS (`src/game`, `src/camera`, `src/entity_manager`).
- Chunked procedural map streamed on a `BS::thread_pool`, with noise
  continents/warp/minerals, ridged highland detail, islands, lake basins and
  per-resource fields; sampling lives in a stateless `GenerateTerrain` over a
  `MapConfig` value type (`src/map_generator`).
- Tile-space coordinates end to end (`helpers/CoordMath.h`); `tile_types` is the
  authoritative per-tile map, so edits made through `setTileColor` agree with
  what is drawn and what entities query.
- Data-driven HUD (buttons, sliders, input boxes) loaded from JSON, with named
  callbacks, multiple levels and a persistent stats panel (`src/hud`,
  `config/hud_menu_data.json`).
- Weighted, personality-scaled entity decisions over needs and a settlement-wide
  shared map knowledge store (`helpers/Knowledge.h`), with terrain-cost movement
  and a gather-to-stockpile work loop (`src/entity_manager`). What one entity
  observes is known to all, so the store does not grow with the population.
- **Exploration.** An `Explorer` job roams to fresh land tiles while everyone
  else gathers; its vision scan extends the settlement's explored cells, and the
  HUD reports how much of the map the civilization has seen.
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
  old age; a food shortfall costs an unfed entity a day of hunger and three
  hungry days in a row drain `CHealth` (a slow resource separate from the needs)
  while a comfortable, rested entity recovers; sleep is the only per-entity
  survival need; food is eaten from the settlement stockpile once per person per
  day; comfortable adults reproduce on their own timer (`CReproduction`); and
  population/vital stats reach the HUD. Movement and the survival systems both
  run on in-game time, so the clock speed never outruns the walk to work and
  pausing freezes the settlement. The seeded population founds itself on a
  habitable site with wood to gather and land to farm in reach, so a run produces
  a story instead of a static crowd.
- **Settlement economy and jobs.** Entities have roles (`helpers/Jobs.h`) bound
  to their type through the `entity_types` block, so farmers, lumberjacks,
  miners, builders and explorers pursue their role (explorers map the frontier
  rather than gather); gathers bank into a
  per-good stockpile (`helpers/Goods.h`), the shortage rule reassigns idle
  entities to understaffed jobs (`helpers/Economy.h`), farms and workshops run
  recipes, food spoils daily, and food is consumed from the store once per person
  per day. Buildings are placed
  with `setTileColor` so they render and persist: sites only stand on dry land
  with a settlement-wide gap, the site search scatters around the anchor rather
  than following a fixed ring, a building's size (house capacity, farm reach) is
  rolled once at construction from a JSON range, only housing raises the
  population cap (a farm's roll is food reach, not beds), a farm yields that reach
  (its rolled number of food per day, not a flat hourly rate), housing is only
  raised while the settlement has no free bed (the anchor's beds aside), and farms
  only while the completed farms do not yet feed the people present and the store
  is not already comfortable (`economy.food_reserve_days`). Workshops, whose output
  nothing consumes yet, are capped by `max_count`. Work (gather/build) is
  applied on the target tile: the timer starts on arrival and a completion is
  withheld while the entity is off the tile. A stats panel shows the
  stockpile, buildings and gather count.
- **Observability and tuning.** A bounded event log records a run's story
  (births, deaths with cause, gathers, discoveries) stamped with the in-game
  minute; a `RunHistory` samples the population and stores once per in-game day
  and summarizes them (`helpers/EventLog.h`, `helpers/RunSummary.h`). A busy
  entity now interrupts a non-survival plan when a need turns critical, and the
  entity config can be reloaded at runtime (`R`) so tuning needs no rebuild.
- **Entity appearance is data-driven.** A profession's (and a type's) look —
  shape, fill, outline and size — comes from the `appearance` block in
  `config/entity_data.json` (`helpers/Appearance.h`), resolved per entity and
  repainted when a job changes or the config reloads, so a modder can recolour
  and reshape professions without a rebuild. Unconfigured entities keep the
  default white circle, so older configs are unchanged.
- Unit tests (doctest) across coordinates, `SharedContainer`, `GameClock`,
  config, map lifetime/determinism, decisions, move cost, appearance, HUD,
  scenes, survival/population, the economy/jobs and observability (event log,
  run history, interruption, runtime reload).
- CI on Linux + Windows plus an ASan/UBSan job (`.github/workflows/build.yml`)
  and a real-game smoke test (`.github/workflows/game-smoke.yml`).

---

## Milestones

Ordered by dependency. The schedule builds the civilization simulation one layer
at a time; the political game (Milestone 8 onward) sits on top of it. Milestones
1 (settlement economy) and 2 (observability and tuning) are complete and folded
into the baseline.

### Milestone 1 — Settlement economy and jobs

Turn "gather the nearest resource" into production with roles and buildings.
**Complete.**

- [x] Give entity types behavior: `Human_Farmer`, `Human_Lumberjack` (and the
      animal types) are bound to jobs through the `entity_types` block in
      `entity_data.json`, with sensible defaults (human -> Builder, animal ->
      Idle) so unlisted or older configs still load.
- [x] Add recipes / production chains that convert raw stock (wood, stone, clay,
      iron, silver) into goods, so different resources matter (`helpers/Goods.h`,
      `helpers/Economy.h`).
- [x] Make food a real resource: farms and spoilage, so hunger is supplied by
      production rather than a tile fallback. Farms are the only food source;
      wild forest/hill yields wood/stone, and a forest tile holds a finite pile
      of wood that becomes a hill once cleared.
- [~] Add a stockpile HUD panel showing counts and rates over time — the panel
      shows per-good counts, buildings and gathers; rates over time are still to
      come.
- [x] Add placeable buildings on tiles, reusing `setTileColor` and the chunk
      mesh rebuild so structures render and persist in the world.
- [x] Unit-test the economy: a recipe consumes inputs and produces outputs, and
      job assignment responds to shortages.

**Done when:** a settlement produces a surplus from specialized jobs, visible in
the HUD and verified by tests.

### Milestone 2 — Observability and tuning

**Complete.** The simulation is hard to balance by eye, so behaviour is now
measurable before more systems land.

- [x] Add a lightweight event log (births, deaths, gathers, discoveries) with
      cause and in-game timestamp, queryable in tests
      (`helpers/EventLog.h`; a bounded ring, with `countOf`/`since`/`recent`).
- [x] Record a run's population/stockpile over time and expose a headless
      summary, so a balance change can be compared against a baseline
      (`helpers/RunSummary.h`).
- [x] Support reloading `entity_data.json` at runtime so tuning needs no rebuild
      (`EntityManager::reloadConfig`, bound to `R`).
- [x] Let a busy entity interrupt its current action when a survival need turns
      critical, so a long trip does not kill it
      (`EntityDecision::interruptFor`, transactional so it never thrashes).
- [x] Unit-test that a given config produces the expected steady-state
      population band over a fixed number of simulated days
      (`tests/test_observability.cpp`).

**Done when:** a balance change can be justified by numbers rather than by
watching the window, and a fresh settlement survives its first day.

### Milestone 3 — Social station and importance

*This is the active milestone.* People are not units: they differ in **social
station and importance**, an attribute the political layer will draw on. This is
deliberately *not* a relationship system — pairwise ties are O(pawns²) and the
population grows without bound. A pawn's station is a single per-pawn value, so
it stays cheap at any scale. Detail follows importance: a notable is simulated as
an individual, the masses are aggregated.

- [ ] Add a per-pawn `social station` / `importance` value, independent of raw
      wealth, that can rise and fall over a life.
- [ ] Drive station from what already exists: role, wealth, age, and personal
      achievements (discoveries, work done) — no new per-pair state.
- [ ] Let station bias behavior in cheap, local ways (who leads a task, who
      speaks for the group), not through pairwise checks.
- [ ] Use station to decide **simulation detail**: keep notables fully simulated
      and aggregate the crowd, so the pawn count can grow without cost blowing
      up.
- [ ] Make `CPersonality` diverge and stay stable, so two people with the same
      needs are still different characters.
- [ ] Seed a starting station spread from `entity_data.json` and surface it in
      the HUD.
- [ ] Unit-test that station changes with wealth/achievement and that a run with
      many pawns stays linear in cost.

**Done when:** pawns have a social station that visibly differentiates them and
scales to a large population without per-pair work.

### Milestone 4 — Threats and defense

Survival has no antagonist yet. Pressure gives the settlement a reason to grow,
store and build — and later gives politics something to argue about.

- [ ] Spawn hostile wildlife (the unused animal entity types) that hunts or raids
      the settlement.
- [ ] Add combat: entities can fight, flee or be wounded.
- [ ] Add walls/defenses as placeable tiles, reusing the building path from
      Milestone 1.
- [ ] Make threats scale with time or population so the early game stays calm.
- [ ] Unit-test a raid: attackers damage defenders, and a wall blocks a path.

**Done when:** an unattended settlement can be harmed, and a defended one can
repel the attack.

### Milestone 5 — The wider world

The game is not one settlement. Add the neighbours and the conditions the
politics will react to: a world of several civilizations growing, trading and
competing.

- [ ] Support several settlements/civilizations, each with its own stockpile,
      jobs and knowledge, seeded apart on the map.
- [ ] Add an abstract off-map civilization model (population, wealth, relations)
      so neighbours grow even where the player is not looking, without simulating
      every pawn.
- [ ] Add inter-civilization contact: trade, migration, rivalry, and shifting
      relations.
- [ ] Add world/geopolitical conditions (resources, distance, pressure) that
      evolve over time.
- [ ] Unit-test that two civilizations interact: a trade transfers stock, a
      rivalry worsens relations.

**Done when:** more than one civilization exists and their relations change over
a run.

### Milestone 6 — City center and institutional growth

Power needs a seat. Add the institution that later becomes the arena of
democratic politics.

- [~] Add a city center as a placeable structure and a settlement anchor — it is
      a catalog building (`is_anchor`, `max_count` 1) that sets `settlementAnchor`
      and drives the HUD, but the planner does not yet build it automatically and
      growth does not yet wait on it.
- [ ] Give a city center a sphere of effect (storage, defense, administration)
      and let a settlement grow into it.
- [ ] Let entities invest work or goods in the commons, building a shared
      settlement identity.
- [ ] Unit-test that a city center changes settlement behavior (e.g. storage
      capacity or a defense bonus).

**Done when:** a city center anchors settlement behavior and the settlement can
invest in itself.

### Milestone 7 — Performance and scale

The population grows without bound, so the simulation must stay cheap as it
grows — ideally linear in pawns, never per-pair.

- [ ] Index tile resource queries (e.g. per-chunk resource lists) instead of
      rescanning the vision square for every entity.
- [ ] Add a spatial index for entity lookups (neighbour queries, collision).
- [ ] Push the knowledge/vision scan off the per-tile path where it still costs,
      and budget it across frames.
- [ ] Aggregate the crowd: simulate notables as individuals and the masses in
      bulk, so pawn count can grow far past what full per-pawn updates allow.
- [ ] Add a headless benchmark (civilizations x entities x ticks) to CI so
      regressions show up.
- [ ] Set and test a target: e.g. 500 entities at 60 fps on the CI machine.

**Done when:** the benchmark target is met and guarded by a test.

### Milestone 8 — Social capital and influence (the political simulation)

The heart of the design: power inside the governing twenty. One person, one
vote, and influence that must be earned. Politics is driven by the **social
station** of Milestone 3, not by pairwise ties, so it stays cheap as the
civilization grows. This milestone is playable *without* the player — the
simulation generates the political game.

- [ ] Add a **power**/**influence** resource on entities, distinct from wealth
      and from social station.
- [ ] Add **factions**: groups with shared interests, built from station, role
      and the wider-world conditions rather than per-pair relationships.
- [ ] Make power **earned and lost**: decisions, favours, alliances and events
      shift an entity's influence, starting from a small base and a few faction
      allies.
- [ ] Add an in-simulation **decision/vote process** (a council or assembly of
      the twenty) so the governing group can actually decide something.
- [ ] Let entities **act politically**: campaign, trade favours, form coalitions,
      oppose rivals.
- [ ] Seed starting political power and faction allies from `entity_data.json`.
- [ ] Unit-test the loop: a decision moves influence, and a faction's votes
      change an outcome.

**Done when:** an unattended simulation produces a believable power struggle
among the governing twenty, with influence shifting from decisions and factions.

### Milestone 9 — Events and the player's agency

Now hand the player the reins: one person among the twenty, with leverage that
starts small.

- [ ] Add a **player-controlled entity** in the same simulation (its decisions
      come from input, not AI).
- [ ] Add **decisions the player can take**, with small early effects scaled by
      political power and allies.
- [ ] Add an **event system** (local, settlement, and later geopolitical) that
      calls for decisions and moves the world.
- [ ] Give the player a **political UI**: standing, allies, factions, and the
      pending decision or vote.
- [ ] Add an **opinion/reaction model**: the other nineteen respond to what the
      player does.
- [ ] Unit-test a decision: the player's choice changes influence and a
      faction's stance.

**Done when:** the player can make a decision that visibly moves influence and
eventually the settlement, starting from a place of little power.

### Milestone 10 — Presentation and UX

Make the simulation legible and the politics readable, once both are worth
watching.

- [ ] Drive a day/night tint and lighting from `GameClock`.
- [ ] Replace the debug circles with sprites/animations and add a camera that
      can follow an entity.
- [ ] Add a minimap of the known world and a stats overlay (population,
      stockpile, clock, standing).
- [ ] Turn `Scene_Menu` into a real front end: new game (seed/options), load,
      settings, quit; add a pause overlay.
- [ ] Optional: SFML audio for ambience and events.

**Done when:** the game communicates its own state without debug overlays, and
the menu can start and configure a run.

---

## On the side — Survival depth

The survival loop runs (entities age and die, needs turn lethal once an entity is
already struggling, the settlement reproduces). The follow-ups below stay parked
until the society and economy milestones land, because tuning them needs the
world to be stable first.

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

## Parked — Persistence and save/load

Set aside for now: the simulation still changes too quickly for a save format to
be worth freezing. Pick this up once the society and the wider-world milestones
have settled — the political game needs a save far more than the survival loop
does.

- [ ] Serialize world state: seed, clock, entities, social station, stockpile,
      influence, factions, and edited tiles.
- [ ] Load it back and reconstruct an equivalent simulation.
- [ ] Keep edits as a log over the seed so a save stays small and deterministic.
- [ ] Add a version field and reject incompatible saves with a clear error.
- [ ] Unit-test round-trip: save, load, and compare state and determinism.

**Done when:** a saved game reloads to the same population, power structure and
map.

---

## Deferred — AI politics via LLM

A later layer, not a foundation. Once factions, events and influence exist as
data, an LLM can drive the *words* of politics — speeches, negotiations,
justifications — behind an interface that is mockable in tests, so the
simulation stays deterministic and cheap.

- [ ] Define a politics interface (propose, argue, justify) with a scripted
      default implementation.
- [ ] Add an LLM backend (llama) behind that interface, with clear fallbacks.
- [ ] Unit-test against the mock, so CI never needs a model.

**Done when:** politics can run against either a mock or an LLM backend without
changing the simulation.

---

## Tracking

- Update this file when a milestone's status changes.
- Update the `TODO` section in [README.md](README.md) as items complete.
