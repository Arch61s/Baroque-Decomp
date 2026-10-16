#pragma once
// Services the event manager calls in other objects (player status, actor registry, the player's event animation
// set, the inventory's event costume, map offsets) and the event mode switch FUN_0017a0c0. Each function is a
// translation of the named original working on the port's objects.
namespace baroque::game {

struct Chr;

void eventPlayerStatusSave();                    // orig FUN_00138c50: status mask + 19 timers saved and cleared
void eventPlayerStatusRestore();                 // orig FUN_00138d00
void eventRegistryHide();                        // orig FUN_0012cb90(0x50b130): enemies hidden + frozen
void eventRegistryRestore();                     // orig FUN_0012ccf0(0x50b130)
void eventActorSetLoad(Chr& c, int slot, const char* setId);   // orig FUN_001b2620 (+ FUN_001b0f10 load callback)
void eventActorSetRelease(Chr& c, int slot);     // orig FUN_001b2730
void eventEquipOverride(int kind);               // orig FUN_001d7dc0(0x53d910, kind)
void eventEquipRestore();                        // orig FUN_001d8060(0x53d910)
float eventMapOrigin(int room, int axis);        // orig FUN_0011e6c0 (axis 0) / FUN_0011e740 (axis 2)
void eventMode(int on);                          // orig FUN_0017a0c0
int eventModeFlag();                             // uRam0053c6a8

}  // namespace baroque::game
