# One Of Twenty — Roadmap

This roadmap turns the `TODO` list in [README.md](README.md) into ordered
milestones. It reflects what the code does today and what comes next; keep the
two in sync when a milestone changes status.

## Legend

- `[x]` done — implemented and covered by tests or CI
- `[ ]` planned — not started
- `[~]` in progress — partially implemented or unverified

## Current baseline

The engine core is in place:

- Window, game loop, camera, and an EnTT-based ECS (`src/game`, `src/camera`,
  `src/entity_manager`).
- Chunked procedural map streamed on a `BS::thread_pool`, with noise
  continents/warp/minerals and a `SharedContainer` hand-off queue
  (`src/map_generator`).
- Data-driven HUD (buttons, sliders, input boxes) loaded from JSON
  (`src/hud`, `config/hud_menu_data.json`).
- JSON config loading with clear errors, logging via spdlog, a `GameClock`
  driving day/time, and pure helpers (`src/helpers`).
- Unit tests (doctest) for coordinate math, `SharedContainer`, `GameClock`,
  config loading, and `MapGenerator` lifetime/determinism.
- CI on Linux + Windows plus an ASan/UBSan job (`.github/workflows/build.yml`).

---

## Milestone 1 — Technical debt and correctness

Stabilize the existing code before adding features.

- [x] Re-check all objects for dynamic allocation of big objects; remove
      unnecessary heap churn (`General`). Removed the unused `sf::Text` member,
      made `Camera::cInput` a value, and replaced the per-tile color lookup and
      the second tile grid in `MapGenerator::generateChunk`.
- [x] Revisit thread-safe access to the shared chunk map: keep the single
      `t_mutex` for now, but document the lock ordering and stop taking it while
      touching the self-locking ready container (`General`).
- [x] Convert HUD components (`CButton`, `CSlider`, `CInputBox`, `CInfoBox`)
      into proper classes with encapsulated state (`Components`). Shapes and
      text are now private; placement, hit testing, drawing and input live on
      the widget.
- [x] Update chunk unload to double-check that no entity or pending change
      still references the chunk before eviction (`MapGenerator`). Added
      `setChunkUnload()` and a pin check before erasing; covered by tests.
- [x] Expose the logger level through `config.json` instead of hardcoding it
      (`Game`). Added `logger.level` (parsed case-insensitively, unknown values
      throw) and covered it with tests.

**Done when:** the build is warning-clean, sanitizer CI stays green, and no
behavior changes are visible in-game.

## Milestone 2 — Map generation depth

Grow the world from "flat terrain with resources" into a richer, queryable map.
The work is ordered so that correctness lands before new scenery, and the
structural split is last (it is refactoring, not new behavior).

### 2a. Correctness first
- [x] IMPORTANT: convert all coordinates to tile space; use world coordinates
      only at render time. `MapGenerator::worldToTile` / `tileToWorld`
      (`helpers/CoordMath.h`) stay the single source of truth. Chunks are now
      keyed by tile position and noise is sampled in tile space (`MapGenerator`).
- [x] Support changing the map on entity action. `tile_types` is the
      authoritative per-tile map; the chunk mesh is rebuilt from it on edit, so
      rendering and `getPositionInfo` agree. Queries use `chunkOf` (floored) so
      the edited chunk is actually the one found (`MapGenerator`).

### 2b. Generation options (driven by `config/map_data.json`)
- [x] Add an option to generate an island (`MapGenerator`). `island.enabled` /
      `island.falloff` apply a radial falloff to the continent field.
- [x] Add rivers (`MapGenerator`). `river.enabled` / `river.freq` /
      `river.threshold` carve a dedicated noise field into land tiles.
- [x] Add configurable depth and height ranges (`MapGenerator`). `height_range.min`/`max` remap the continent field before thresholds; the default `[0,1]` is an identity transform.
- [ ] Use a distinct noise map per resource so deposits don't overlap
      (`MapGenerator`).

### 2c. Queries and entity interaction
- [ ] Improve how entities query resources from the map (`MapGenerator`).

### 2d. Structure (largest, do last)
- [ ] Split `MapGenerator` into chunk store / streamer / renderer
      (`MapGenerator`).

**Done when:** the new generation options are driven by `config/map_data.json`,
`MapGenerator` tests cover each option, and streaming still shuts down cleanly.

## Milestone 3 — Entities and simulation

Deepen the ECS simulation from generic humans to purposeful agents.

- [ ] Improve the `CVision` debug circle (`Entity`).
- [ ] Improve `CMemory` — currently only remembers water and hill (`Entity`).
- [ ] Improve `CBasicNeeds` (thirst / hunger / sleep) (`Entity`).
- [ ] Improve tile cost calculation used for pathing/decisions (`Entity`).
- [ ] Implement weights-based decisions for entities, using `CPersonality`
      traits (currently generated but unused) (`Entity`).
- [ ] Provide actions that advance society (`Entity`).

**Done when:** entities choose actions from weighted needs/memory rather than
fixed logic, and the new decision code is unit-testable and tested.

## Milestone 4 — Game structure and HUD

Give the game room to hold multiple screens and menus.

- [ ] Implement a `Scene` class and route inputs through scenes (`Game`).
- [ ] Implement multiple HUD levels (`HUD`).
- [ ] Bind HUD elements to functions by name instead of numeric id, so config
      and enums cannot drift apart (`HUD`).

**Done when:** the HUD layout in `config/hud_menu_data.json` drives named
callbacks, and the `Function` enums in `src/hud/Hud.h` no longer encode ids.

## Milestone 5 — Civilization

The long-term goal: a living society on the generated world.

- [ ] Add a city center (`Entity`).
- [ ] Add AI for civilization politics via llama (`Entity`).

**Done when:** a city center anchors settlement behavior and politics can run
against an LLM backend behind an interface that is mockable in tests.

---

## Tracking

- Update this file when a milestone's status changes.
- Update the `TODO` section in [README.md](README.md) as items complete.
