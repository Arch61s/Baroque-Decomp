#pragma once
// Status set FUN_00138850 (character status object vtable 0x4cdb40) and its per-status helpers: the flag of a
// player status FUN_00137220, the message FUN_00137540 and the visual set-up FUN_00137d40. Callers: the flag hook
// FUN_00178890 (flags 0x810 / 0x80f / 0x80d -> player status 0x10 / 0x20 / 4) and the save-record read FUN_00139b40.
#include <cstdint>
namespace baroque::game {
struct StatusObj;

// orig FUN_00138850: sets every requested status bit (0..0x12) without a resist roll; returns the OR of the requested
// bits. The instant effects FUN_00138020 run for the result plus the HP / VT-only bits 0x300000 of `mask`.
uint32_t statusSet(StatusObj& s, uint32_t mask);
void statusSetPlayerFlag(int i);                       // orig FUN_00137220 (player only)
void statusSetMessage(int i, bool isPlayer);           // orig FUN_00137540
void statusVisualOn(StatusObj& s, int i);              // orig FUN_00137d40

// Defined in combat.cpp (status module):
void statusSetDuration(StatusObj& s, int i);           // orig FUN_00137aa0
void statusInstantEffects(StatusObj& s, uint32_t bits);   // orig FUN_00138020
}  // namespace baroque::game
