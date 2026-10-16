#pragma once
// Shared 32x32 procedural texture DAT_0044c1a0 (FUN_00163e20 create / FUN_00164060 per-frame border animation /
// FUN_00163fc0 free). Row 0 holds an alpha ramp (red 0xc4) used as the menu title underline; rows 1..31 carry a
// marching-ants border (orange 0x44ff) used as the NAME ENTRY cursor box and FILE SELECT highlight.
namespace baroque::game::ui {
int gradientTextureId();      // DAT_0044c1a0 (-1 when not created)
void gradientCreate();        // FUN_00163e20
void gradientAnimate();       // FUN_00164060 (called every menu frame by FUN_00167d80 and once at creation)
void gradientFree();          // FUN_00163fc0
}
