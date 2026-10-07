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
  directly; islands and rivers are opt-in via `island.*` / `river.*` in
  `config/map_data.json` and default to off. `height_range.min`/`max` remap
  the continent field before thresholds (default `[0,1]` = identity).
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
