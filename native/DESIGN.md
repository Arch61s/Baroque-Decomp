# Baroque native port — host design

Goal: the original PS2 game, behaviour-exact, running natively on Windows with no emulation, at 1080p–4K,
4:3 or 16:9, 60 or 120 FPS, with a modern mouse-driven free camera replacing only the camera-orientation input.

## Principles
1. **Translate, don't reconstruct.** Every gameplay, menu, AI, item, dungeon-generation and timing routine is
   translated from the recovered pseudocode and cross-checked against the R5900 disassembly. Constants are copied,
   never tuned. Where the original uses its RNG, the same LCG/sequence is used.
2. **Fixed original tick.** Game logic runs at the original rate (30 ticks/s per FUN_0011bec0; to be confirmed by the
   runtime report). Presentation runs at 60/120 FPS by interpolating transforms between the previous and current
   tick for the renderer only; logic never sees a fractional frame.
3. **Original assets only.** IGB scene graphs, PNG UI, PRM/ACT/TLK/EVT data, STER/VAG/HD-BD audio and PSS movies are
   decoded by our own loaders from `work/assets` and the disc files. No re-authored content.
4. **GS-faithful rendering.** Alchemy's PS2 render states (alpha blend equations, alpha test, depth test/write,
   texture function modulate with 0x80 = 1.0 colour/alpha scale, CLUT textures, fog, vertex colours, mip bias) are
   reproduced in GLSL on raylib/rlgl. The internal render target has the requested height; the original 640x448 raster
   is only used for 2D layout math. Widescreen extends the horizontal FOV (vertical FOV preserved) and the HUD stays
   anchored as the original 4:3 layout centred, unless the runtime report shows the HUD uses a different anchoring.
5. **Mouse camera.** The original camera controller is kept (position smoothing, wall collision, distance clamps).
   Only its yaw/pitch target is driven by mouse delta instead of the right stick/buttons; the original stick/button
   camera inputs remain available on gamepad. Cutscene/conversation cameras are untouched.
6. **Settings overlay.** A pre-game settings screen selects resolution, aspect, FPS cap, controls; stored beside the
   EXE. (Later: re-skin with original UI assets.)

## Module layout (planned)
- `platform/` window, input (keyboard/mouse/gamepad → original pad bitmask), audio output, timing.
- `alchemy/` IGB loader → in-memory scene graph; geometry/material/texture upload; skeletal animation (igSkin,
  igAnimation*, igTransformSequence) evaluated with the original interpolation rules.
- `data/` DATA.DIR/DATA.BIN reader, FDT/RDT/CLS, ACT, PRM, TLK, EVT, 2D sprite tables, fonts.
- `game/` translated scenes (BootMC, Credit, Title, Adv, Option, EvMovie, NameEntr, GameLoad, Game, Ending), the task
  list, flag manager, dungeon generator, player/enemy/item systems, camera, HUD, text.
- `audio/` STER streaming (Sony ADPCM), VAG/HD-BD sample playback with ADSR/pitch/pan, movie PCM.
- `video/` PSS demux + MPEG-2 decode (local FFmpeg libavcodec), presented with the original 4:3 framing.
- `checks/` headless regression captures (PNG) and CPU fixtures comparing translated routines against recorded
  original values (e.g. dungeon layouts for a given seed, damage formulas, menu timers).

## Build
CMake + MSVC 2022, raylib from the local source tree, static CRT. `Baroque-Native.exe --toolchain-check` is the
smoke test. Packaging copies `assets/` next to the EXE with a SHA-256 manifest.
