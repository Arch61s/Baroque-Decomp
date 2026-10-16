#pragma once
// Player controller FUN_0013b0d0(0x50b620, chr) and its callees (PLAYER.md §3; work/reports/PLAYER_CONTROL_NATIVE.md).
// The entry point `playerControl(PlayerControl&, Chr&)` is declared in character.hpp.
//
// Every call into a subsystem that is not translated yet goes through a function in `hooks` below. The default
// implementations (player_control.cpp) return the value the original produces when that subsystem holds nothing
// (empty inventory, no event, no map, ...) or do nothing; each is marked `UNVERIFIED: subsystem not translated yet`.
// When the subsystem lands, replace the hook body with the call into it.
#include "game/character/character.hpp"
#include "game/rng.hpp"

#include <cstdint>
#include <functional>

namespace baroque::game {

// orig FUN_0013c4e0 (attack start). Returns 1 when an attack action was started.
int playerAttackStart(PlayerControl& ctl, Chr& chr);
// orig FUN_0013c2c0 (charge / special hold while in an "AT" action)
void playerChargeHold(PlayerControl& ctl, Chr& chr);
// orig FUN_0013c790 (throw aim / item slot scroll). Returns 1 when it consumed the tick.
int playerThrowAim(PlayerControl& ctl, Chr& chr);
// orig FUN_0013a140: the throw-trajectory sprite 0x50b600 exists ("aiming").
bool playerAiming(const PlayerControl& ctl);
// orig FUN_0013a060: free the throw-trajectory sprite 0x50b600.
void playerAimMarkerFree(PlayerControl& ctl);
// orig FUN_00139f80: create the throw-trajectory sprite 0x50b600 if absent.
void playerAimMarkerCreate(PlayerControl& ctl);
// orig FUN_00134820: special-attack tier 0/1/2 (selects AT18/AT13/AT06).
int statusSpecialTier(const StatusObj& s);
// orig FUN_0012c270 (+ FUN_0012c2e0): distance between two characters minus both collision radii (types < 3), >= 0.
float chrDistance(const Chr& a, const Chr& b);
// orig FUN_0012c360: nearest live character of `type` to `from`; nullptr when none or farther than maxDist (>= 0).
Chr* chrNearestOfType(float maxDist, const Chr& from, int type);

// Services behind the hooks, installed by the full game build (player_control_wiring.cpp, Baroque-Native only);
// unset members keep the empty-subsystem default (the PlayerControlCheck links the controller alone).
struct PlayerControlServices {
    std::function<int()> eventState;                    // iRam0051b620 (1 = event running)
    std::function<void(int)> startFloorEvent;           // FUN_001d1d30
    std::function<int()> floorId;                       // iRam0050a2a8
    std::function<int()> fadeFramesLeft;                // iRam004e7898
    std::function<char(int)> roomKind;                  // **(0x50a640[i] + 4)
    std::function<float(int)> roomOriginX, roomOriginZ; // FUN_0011e6c0 / FUN_0011e740
    std::function<bool()> floorMapShown;                // iRam0050a3f8 != 0
    std::function<void()> floorMapShow, floorMapHide;   // FUN_00126cc0 / FUN_00126fb0
    std::function<bool()> floorMapCorner;               // PORT: map in the bottom-left corner
    std::function<void(bool)> floorMapSetCorner;        // PORT
    std::function<void()> inventoryTick;                // FUN_001d9b60(0x53d910)
    std::function<int()> inventoryCount;                // iRam0053d910
    std::function<const void*(int)> inventoryItem;      // *(0x53d914 + slot*4)
    std::function<int()> weaponClass;                   // FUN_001d7d10
    std::function<bool()> gunHasAmmo;                   // *(iRam0053d9b4 + 0x6c) != 0
    std::function<int(int, int)> equipKindBonus;        // FUN_00134310
    std::function<int(int)> itemThrowCheck;             // FUN_001d7c80
    std::function<int()> itemSlotSelected;              // FUN_001573f0
    std::function<void(int)> setItemSlotSelected;       // FUN_00157400
    std::function<void(bool next)> itemSlotStep;        // FUN_001575f0 / FUN_00157410
    std::function<int(const Chr&, int)> aiQuery;        // FUN_001f1730
    std::function<bool(float fwd[3], float right[3])> eventCameraAxes;   // 0x53c6e8 && 0x53c720: FUN_001a1d90 / 1d80
};
PlayerControlServices& playerControlServices();

namespace hooks {

// ---- scene / event / floor state ----
int eventState();                   // iRam0051b620 (event manager 0x51b620 state word; 1 = event running)
int floorId();                      // iRam0050a2a8 (Game scene object 0x50a2a0 +8)
int fadeFramesLeft();               // iRam004e7898 (fade 0x4e7890 +8; 0 = fade finished)
bool dialogShown();                 // iRam0053bf88 != 0 (message dialog shown)
Rng& gameRng();                     // RNG state 0x53eb20 (FUN_002071e0)
void startFloorEvent(int arg);      // FUN_001d1d30(arg): event start (0x32 = fall to the next floor)
// Event camera axes: 0x53c6e8 != 0 && 0x53c720 != 0, FUN_001a1d90 / FUN_001a1d80 (talk/event object 0x53c6e0)
bool eventCameraActive();
Vec3f eventCameraForward();
Vec3f eventCameraRight();

// ---- rooms (0x50a640 table) ----
char roomKind(int roomIndex);       // **(0x50a640[i] + 4): first byte of the room's RDT data
float roomOriginX(int roomIndex);   // FUN_0011e6c0
float roomOriginZ(int roomIndex);   // FUN_0011e740

// ---- floor map (0x50a350) ----
bool floorMapShown();               // iRam0050a3f8 != 0 (map sprite at floor context +0xa8)
void floorMapShow();                // FUN_00126cc0(0x50a350)
void floorMapHide();                // FUN_00126fb0(0x50a350)
bool floorMapCorner();              // PORT: map shown as the bottom-left mini map
void floorMapSetCorner(bool on);    // PORT

// ---- inventory 0x53d910 / item screen 0x517b60 ----
void inventoryTick();               // FUN_001d9b60(0x53d910)
int inventoryCount();               // iRam0053d910 (inventory +0: item count)
const void* inventoryItem(int slot);// *(0x53d914 + slot*4)
int weaponClass();                  // FUN_001d7d10(0x53d910): 0/1/2 sword kinds, 3 gun, -1 none
bool gunHasAmmo();                  // *(iRam0053d9b4 + 0x6c) != 0 (equipped gun's ammo)
int equipKindBonus(int base, int kind);   // FUN_00134310(base, kind): base + equipped items' kind bonuses
int itemThrowCheck(int slot);       // FUN_001d7c80(0x53d910, slot) (-1 = the item can be thrown)
int itemSlotSelected();             // FUN_001573f0(0x517b60): item screen +0x28
void setItemSlotSelected(int slot); // FUN_00157400(0x517b60, slot)
void itemSlotPrev();                // FUN_00157410(0x517b60)
void itemSlotNext();                // FUN_001575f0(0x517b60)

// ---- character model / AI ----
void heldItemModelShow(Chr& chr, int hand);            // FUN_001b2930(chr, hand)
void heldItemModelHide(Chr& chr, int hand, int flag);  // FUN_001b2980(chr, hand, flag)
int aiQuery(const Chr& chr, int key);                  // FUN_001f1730(*(chr comp +0x14) AI, key)

// ---- sprites (texture 0x129 sprite list 0x4e6a00) ----
bool lockMarkerAlloc();             // FUN_00107170/FUN_00107130/FUN_00113c60/FUN_00114c30/FUN_00107070 +
                                    // FUN_001051d0(sprite, 0x44b160, 0, 0, 4, 0x50b680); true = non-null sprite
void lockMarkerFree();              // unlink from 0x4e6a04/0x4e6a0c/0x4e6a14 lists + FUN_001053a0
bool aimMarkerAlloc();              // FUN_00139f80 chain + FUN_001051d0(sprite, 0x50b240, 0, 0x50b510, 0x3c, 0)
void aimMarkerFree();               // FUN_0013a060 unlink + FUN_001053a0
void aimTrajectory(const void* item, const Vec3f& start, const Vec3f& dir, const Vec3f& side);   // FUN_0013a150

}  // namespace hooks

}  // namespace baroque::game
