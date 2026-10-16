#pragma once
// Weapon trail objects of the ACT commands Track / TrackEx / TrackEnd (see also act_props.hpp):
//   ACT +0x5c[2] trail slots: FUN_00145560 (start), FUN_001456b0 (end with a 4-tick fade), FUN_00145660 (both, from
//   the ACT reset FUN_00143bf0), FUN_00149390 (TrackEnd: FUN_001ab0e0(trail, n)).
//   Trail object (pool 0x53eb40, 0x5b0 bytes, at most 0x10 live, list 0x53d710 / 0x53d718): FUN_001aa880 create,
//   FUN_001ab170 ctor, FUN_001aa920 (callback + igVertexArray of 0x60 vertices, format 0x10005), FUN_001ab0e0 end,
//   FUN_001aaf70 point history, FUN_001aa2d0 B-spline, FUN_001aaa00 update + draw, FUN_001aa530 the draw pass of the
//   engine tick (FUN_00101bd0, after FUN_001120b0), FUN_001aa470 texture 0x11d (C_SMOKE.PNG) at boot.
//   Callback FUN_00144de0: the two points from the owner's joint (sword length / element colours of the player's
//   equipment, TrackEx start / length for the others).
#include "game/effects/effects.hpp"

#include <string>

namespace baroque::game {

struct Chr;

// ACT handlers (called from actPropCommand)
void actTrackStart(Chr& owner, int slot, int joint, int axis, int start, int length);   // orig FUN_00145560
void actTrackEnd(Chr& owner, int slot, int frames);                                       // orig FUN_00149390
void actTrackReset(Chr& owner);                                                           // orig FUN_00145660

// orig FUN_001aa530 (engine tick, after the EXI pass): every trail is updated (FUN_001aaa00) and its strip built. PORT:
// the strips are recorded here once per tick and drawn by actTrailsReplay on every presented frame (the EXI split).
void actTrailsTick();
void actTrailsReplay(const ExiView& view);
void actTrailsClear();   // PORT: scene exit (the pool is not cleared by the original; the owners are gone)
int actTrailsLive();     // PORT: diagnostics
std::string actTrailsDescribe();   // PORT: diagnostics (--trail-log): one entry per live trail

}  // namespace baroque::game
