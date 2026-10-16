# Native code conventions (read before writing any file under native/)

Language: C++20, MSVC 2022, warnings at /W4 must be clean. Stdlib + raylib/rlgl only (no new dependencies).
Build: `native/CMakeLists.txt` includes every `native/cmake/*.cmake`; each module adds its sources with
`target_sources(Baroque-Native PRIVATE ...)` in its own cmake file. Never edit another module's cmake file.

Directory layout
- `native/platform/`  window, input, audio output, timing, file access (asset root resolution).
- `native/alchemy/`   IGB loader, scene graph, animation, GS-faithful rendering.
- `native/data/`      DATA.DIR/DATA.BIN reader and every game data format (FDT/RDT/CLS, ACT, PRM, TLK, EVT, EXI, AISEQ, 2D tables).
- `native/game/`      translated game logic (scenes, task list, dungeon, player, enemies, items, camera, HUD, text).
- `native/audio/`, `native/video/`, `native/checks/`.
- Headers `.hpp`, sources `.cpp`, one namespace per directory: `baroque::platform`, `baroque::alchemy`, `baroque::data`,
  `baroque::game`, `baroque::audio`, `baroque::video`.

Translation rules (fidelity is the requirement)
1. Every translated function carries a comment with its original address and the Ghidra file it came from:
   `// orig FUN_00123dc0 (work/decompiled/00123dc0.c)`. Keep the original control flow and integer/float widths;
   PS2 `float` math stays `float`. Keep original constants verbatim (hex where the original used hex).
2. Original RNG: the game LCG (`state = state*0x19660d + 0x3c6ef35f; rand(n) = (uint32_t)(((uint64_t)state*(n+1))>>32)`)
   lives in `game/rng.hpp` as `baroque::game::Rng`. Never use std::rand or <random> in game logic.
3. Memory layout matters only where the original relies on it (saves, file formats). Otherwise use plain structs with
   descriptive names, but document the original offset of each field (`int hp; // +0xc`).
4. Time is in original ticks (30 per second unless the runtime report says otherwise). No wall-clock in logic.
5. Nothing is invented: if a behaviour is unknown, implement what the pseudocode shows and add
   `// UNVERIFIED:` with what is uncertain. Do not add gameplay features, balance changes or shortcuts.
6. Data loaders assert the format exactly like the Python tools (`tools/*.py`) and must accept every file in
   `work/assets`. Provide a `--<module>-check` command-line option in `native/checks/` that runs the loader over
   all files and prints counts; it must match the Python summary JSON.
7. Keep functions small enough to review; split translated giant functions by original labels/switch cases, keeping a
   top-level function with the original structure.
8. Asset path: `baroque::platform::assetRoot()` returns `<exe>/assets` if present else `BAROQUE_ASSET_ROOT`
   (`work/assets`; release packages are built with BAROQUE_DEV_ASSET_FALLBACK=OFF -> `<exe>/assets` only). Disc files
   outside DATA.BIN (MOVIE_EN, SOUND_EN) come from `platform::discRoot()` = assetRoot().parent_path()/disc. Get the
   EXE folder from `platform::exeDir()` (wide-path safe). Paths inside use the original `DATA/...` names; packs resolve through `data::Archive`
   which mirrors the DATA.DIR tree (floor `F%03d` packs are consulted before the root tree, matching FUN_0010cf60).
9. Logging via `baroque::platform::log(...)`; no printf in game code. No exceptions across module boundaries; loaders
   return `std::optional`/error codes but `--check` modes may throw to fail fast.
