#pragma once
// Movie struct 0x540930 as used by the scenes (SCENES.md 0.9): +4 index into table 0x46dec0, +8 skippable flag (all four original callers FUN_00119720 / FUN_0011dbc0 x2 / FUN_002062c0 store 1),
// +0xc volume word. FUN_00213730 (alloc) + FUN_00213840 (blocking play) + FUN_002137b0 (free).
// The original blocks inside the scene's Update; the port plays across host frames: begin() opens the file,
// update() advances it (START skips when skippable, FUN_00213990), and the host draws movieActive() instead of
// the sprite layer while it plays.
namespace baroque::video { class MoviePlayer; }

namespace baroque::game {

const char* movieFileName(int index);                 // table 0x46dec0 ("OPENING.PSS", ...)
int movieVolumePercent(int bgmSetting);               // FUN_00119720 volume math: bgm*10 clamped 0..100
bool movieBegin(int index, int skippable, int volumePercent);   // FUN_00213730 + start of FUN_00213840
bool movieUpdate();                                   // one host frame; returns true while playing
void movieEnd();                                      // FUN_002137b0
const video::MoviePlayer* movieActive();              // non-null while a movie is open

}  // namespace baroque::game
