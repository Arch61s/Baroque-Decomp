// Player controller, translated from FUN_0013b0d0 and its callees (see player_control.hpp, PLAYER.md §3).
// Strings and constants read from the ELF (python -I tools/disasm.py, read_bytes/word):
//   0x44b120.. "_S1" "_S2" "_S3" "_G1" "_H1" (FUN_0013a4b0, player_move.cpp actionSuffix)
//   0x44b148 "MV", 0x44b158 "DM", 0x44b198 "TH", 0x44b1a0 "DD", 0x44b1b8 "AT", 0x44b1a8 "E16", 0x44b1b0 "E19",
//   0x44b1c0 "AT12", 0x44b1c8 "AT05", 0x44b1d0 "AT01", 0x44b1d8 "AT18", 0x44b1e0 "AT13", 0x44b1e8 "AT06",
//   0x44b1f8 "M079", 0x44b070 "ST13" (FUN_00134820), 0x44b190 = 50.0f (0x42480000), 0x44b1f0 = 3.0f (0x40400000).
#include "game/character/player_control.hpp"

#include "data/prm.hpp"
#include "game/aim_assist.hpp"
#include "platform/platform.hpp"    // PORT: keyboard + mouse attack direction / aim assist (hooks empty on a pad)
#include "game/camera.hpp"
#include "game/flag_manager.hpp"
#include "game/free_camera.hpp"   // PORT: free mouse camera axes
#include "game/game_world.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <string>

#pragma fenv_access(on)

namespace baroque::game {

// ------------------------------------------------------------------------------------------------------------
// Hooks into subsystems that are not translated yet. Defaults = the original's result with that subsystem empty.
// ------------------------------------------------------------------------------------------------------------
PlayerControlServices& playerControlServices() {
    static PlayerControlServices s;
    return s;
}

namespace hooks {

namespace {
const PlayerControlServices& svc() { return playerControlServices(); }
}  // namespace

int eventState() { return svc().eventState ? svc().eventState() : 0; }                // iRam0051b620
int floorId() { return svc().floorId ? svc().floorId() : 0; }                        // iRam0050a2a8
int fadeFramesLeft() { return svc().fadeFramesLeft ? svc().fadeFramesLeft() : 0; }   // iRam004e7898
// UNVERIFIED: subsystem not translated yet (message dialog 0x53bf88).
bool dialogShown() { return false; }
Rng& gameRng() { return gameWorld().rng; }   // 0x53eb20
void startFloorEvent(int arg) { if (svc().startFloorEvent) svc().startFloorEvent(arg); }   // FUN_001d1d30
// talk/event object 0x53c6e0 (talkCameraAxes through the services; unset in the PlayerControlCheck)
bool eventCameraActive() {
    float f[3], r[3];
    return svc().eventCameraAxes && svc().eventCameraAxes(f, r);
}
Vec3f eventCameraForward() {
    float f[3] = {0, 0, 0}, r[3];
    if (svc().eventCameraAxes) svc().eventCameraAxes(f, r);
    return {f[0], f[1], f[2]};
}
Vec3f eventCameraRight() {
    float f[3], r[3] = {0, 0, 0};
    if (svc().eventCameraAxes) svc().eventCameraAxes(f, r);
    return {r[0], r[1], r[2]};
}

// room table 0x50a640: **(room + 4) = RDT byte 0; FUN_0011e6c0 / FUN_0011e740 room origin (0 off generated floors)
char roomKind(int i) { return svc().roomKind ? svc().roomKind(i) : 0; }
float roomOriginX(int i) { return svc().roomOriginX ? svc().roomOriginX(i) : 0.0f; }
float roomOriginZ(int i) { return svc().roomOriginZ ? svc().roomOriginZ(i) : 0.0f; }

// floor map 0x50a350
bool floorMapShown() { return svc().floorMapShown ? svc().floorMapShown() : false; }   // iRam0050a3f8 != 0
void floorMapShow() { if (svc().floorMapShow) svc().floorMapShow(); }                   // FUN_00126cc0(0x50a350)
void floorMapHide() { if (svc().floorMapHide) svc().floorMapHide(); }                   // FUN_00126fb0(0x50a350)
bool floorMapCorner() { return svc().floorMapCorner ? svc().floorMapCorner() : false; }  // PORT
void floorMapSetCorner(bool on) { if (svc().floorMapSetCorner) svc().floorMapSetCorner(on); }   // PORT

// inventory 0x53d910 / item screen 0x517b60. Unwired = empty inventory: count 0, no weapon (-1), no equipment bonus
// (FUN_00134310 returns its base), no selected slot.
void inventoryTick() { if (svc().inventoryTick) svc().inventoryTick(); }                  // FUN_001d9b60(0x53d910)
int inventoryCount() { return svc().inventoryCount ? svc().inventoryCount() : 0; }       // iRam0053d910
const void* inventoryItem(int slot) { return svc().inventoryItem ? svc().inventoryItem(slot) : nullptr; }
int weaponClass() { return svc().weaponClass ? svc().weaponClass() : -1; }               // FUN_001d7d10(0x53d910)
bool gunHasAmmo() { return svc().gunHasAmmo ? svc().gunHasAmmo() : false; }             // *(iRam0053d9b4 + 0x6c)
int equipKindBonus(int base, int kind) { return svc().equipKindBonus ? svc().equipKindBonus(base, kind) : base; }
int itemThrowCheck(int slot) { return svc().itemThrowCheck ? svc().itemThrowCheck(slot) : 0; }   // FUN_001d7c80
int itemSlotSelected() { return svc().itemSlotSelected ? svc().itemSlotSelected() : -1; }       // FUN_001573f0
void setItemSlotSelected(int slot) { if (svc().setItemSlotSelected) svc().setItemSlotSelected(slot); }   // FUN_00157400
void itemSlotPrev() { if (svc().itemSlotStep) svc().itemSlotStep(false); }               // FUN_00157410(0x517b60)
void itemSlotNext() { if (svc().itemSlotStep) svc().itemSlotStep(true); }                // FUN_001575f0(0x517b60)

// UNVERIFIED: subsystem not translated yet (held item models FUN_001b28e0 slots).
void heldItemModelShow(Chr&, int) {}
void heldItemModelHide(Chr&, int, int) {}
int aiQuery(const Chr& chr, int key) { return svc().aiQuery ? svc().aiQuery(chr, key) : 0; }   // FUN_001f1730

// UNVERIFIED: subsystem not translated yet (3-D sprite lists 0x4e6a00). The original allocations succeed in
// practice; the controller's state (FUN_0013a140 "aiming", the marker) depends on a non-null sprite, so the default
// reports success and the sprite data lives in PlayerControl (lockMarker) for the renderer to draw.
bool lockMarkerAlloc() { return true; }
void lockMarkerFree() {}
bool aimMarkerAlloc() { return true; }
void aimMarkerFree() {}
// UNVERIFIED: subsystem not translated yet (FUN_0013a150 trajectory preview: item speed FUN_001d5850, physics probe
// FUN_00141950, 60 sprite points 0x50b240 / colours 0x50b510).
void aimTrajectory(const void*, const Vec3f&, const Vec3f&, const Vec3f&) {}

}  // namespace hooks

namespace {

// Current action name as the original reads it: 8 bytes, NUL padded (FUN_00143d30(act, -1) -> ACTN +8).
struct ActName {
    char c[8] = {};
};
ActName actName(const char* s) {
    ActName n;
    if (s != nullptr)
        for (int i = 0; i < 8 && s[i] != '\0'; ++i) n.c[i] = s[i];
    return n;
}
bool head2(const ActName& n, const char* two) { return n.c[0] == two[0] && n.c[1] == two[1]; }   // *(u16*)name
bool word0(const ActName& n, const char* four) { return std::memcmp(n.c, four, 4) == 0; }       // *(u32*)name
bool word1(const ActName& n, const char* four) { return std::memcmp(n.c + 4, four, 4) == 0; }   // *(u32*)(name+4)

constexpr uint32_t id3(char a, char b, char c) { return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16; }
constexpr uint32_t kIdE16 = id3('E', '1', '6');   // DAT_0044b1a8
constexpr uint32_t kIdE19 = id3('E', '1', '9');   // DAT_0044b1b0
constexpr uint32_t kBrandST13 = id3('S', 'T', '1') | uint32_t('3') << 24;   // DAT_0044b070

// Base name (4 chars) + FUN_0013a4b0 suffix.
std::string withSuffix(const char* base4) { return std::string(base4) + actionSuffix(hooks::weaponClass()); }

// The inline "dead" test: type 0 (player) or 2 (enemy) with status HP 0.
bool chrDead(const Chr& c) {
    bool v = true;
    if (c.type != 0 && c.type != 2) v = false;
    if (v) return c.status.block.hp == 0;
    return false;
}

void zeroMoveVel(Chr& chr) { chr.phys.moveVel = {0.0f, 0.0f, 0.0f}; }   // phys +0x7c/+0x80/+0x84 = 0

// Inline block (LAB_0013b460 / LAB_0013b5bc / LAB_0013bb90): unlink the lock-on marker sprite and free it.
void removeLockMarker(PlayerControl& ctl) {
    if (!ctl.lockMarker.live) return;
    hooks::lockMarkerFree();
    ctl.lockMarker = ControlSprite{};
}

// FUN_0013cf90 / FUN_0013d810 adapters.
MoveInputs moveInputs(const PlayerControl& ctl, Chr& chr) {
    MoveInputs in;
    const PadPort& p = pad().port[0];
    in.stickX = p.lstick[0];   // 0x4e62d0
    in.stickY = p.lstick[1];   // 0x4e62d4
    in.stickMag = p.lstick[2]; // 0x4e62d8
    in.held = p.held;          // 0x4e6284
    if (ctl.camera != nullptr) {   // *(ctl+4) +0x70 / +0x58
        in.camFwd = {ctl.camera->forward[0], ctl.camera->forward[1], ctl.camera->forward[2]};
        in.camRight = {ctl.camera->right[0], ctl.camera->right[1], ctl.camera->right[2]};
    }
    if (freeCameraActive()) {   // PORT: keyboard / mouse play moves relative to the free mouse camera (free_camera.hpp)
        float back[3], right[3];
        freeCameraAxes(back, right);
        in.camFwd = {back[0], back[1], back[2]};
        in.camRight = {right[0], right[1], right[2]};
    }
    in.eventCamera = hooks::eventCameraActive();
    if (in.eventCamera) {
        in.eventFwd = hooks::eventCameraForward();
        in.eventRight = hooks::eventCameraRight();
    }
    const StatusBlock& b = chr.status.block;
    in.statusMask = b.statusMask;
    in.hidden0 = b.hidden[0];
    in.hp = b.hp;
    in.maxHp = statusHpMaxTotal(chr.status);   // FUN_001343f0
    in.currentAction = chr.act.currentName();
    in.weaponClass = hooks::weaponClass();
    return in;
}
MoveOutputs moveOutputs(Chr& chr) {
    MoveOutputs out;
    out.requestAction = [&chr](const std::string& name8) { chr.act.request(name8); };   // FUN_00139f30
    return out;
}
// PORT (game/aim_assist.hpp): in an attack's turn window, keyboard + mouse play with camera-relative attacks steers
// toward the direction chosen at the attack start instead of the WASD direction. Only the stick direction fed to
// FUN_0013cf90 changes (in the stick basis it uses: move = flat(camFwd) * y + camRight * x); whether it turns, and
// how, stays the original's. Never taken on a pad (the hook answers false unless the free mouse camera is active).
void attackTurnWindowPort(const PlayerControl& ctl, const Chr& chr, MoveInputs& in) {
    const AimAssistHooks& h = aimAssistHooks();
    if (h.turnWindowDir == nullptr || !(0.0f < in.stickMag) || in.eventCamera) return;
    float d[3];
    if (!h.turnWindowDir(ctl, chr, d)) return;
    Vec3f f{in.camFwd[0], 0.0f, in.camFwd[2]};
    normalize(f);
    in.stickY = d[0] * f[0] + d[2] * f[2];
    in.stickX = d[0] * in.camRight[0] + d[2] * in.camRight[2];
}
void movement(PlayerControl& ctl, Chr& chr, bool turnOnly) {   // FUN_0013cf90(ctl, chr, turnOnly)
    MoveInputs in = moveInputs(ctl, chr);
    if (turnOnly) attackTurnWindowPort(ctl, chr, in);   // PORT: keyboard + mouse only
    playerMovement(ctl, chr.xf.m, chr.phys, in, moveOutputs(chr), hooks::gameRng(), turnOnly);
}
void idle(Chr& chr, const PlayerControl& ctl) {   // FUN_0013d810(ctl, chr)
    playerIdle(chr.phys, moveInputs(ctl, chr), moveOutputs(chr));
}

// orig FUN_001a6420 (work/decompiled/001a6420.c): talk-object gate on the floor id and story flags.
bool talkFlagGate() {
    if (hooks::floorId() == 0x5b) return false;
    const FlagManager& f = flags();
    if (f.test(0x2f5)) return true;
    if (!f.test(0x2f6) && !f.test(0x2f7) && !f.test(0x397) && !f.test(0x398) && !f.test(0x399) && !f.test(0x39a) &&
        !f.test(0x39b) && !f.test(0x39c) && !f.test(0x39d))
        return false;
    return true;
}
// orig FUN_001a9ba0 (work/decompiled/001a9ba0.c)
void talkFlagSpecial() {
    if (talkFlagGate()) flags().set(0xe4d);   // FUN_00178890(0x53c3b0, 0xe4d)
}
// orig FUN_001a9fb0 (work/decompiled/001a9fb0.c)
void talkFlagMap() {
    if (talkFlagGate()) flags().set(0xe4e);   // FUN_00178890(0x53c3b0, 0xe4e)
}

// orig FUN_0011c220 (work/decompiled/0011c220.c): floors with the edge-hole check.
bool holeFloor() {
    const int f = hooks::floorId();
    bool b2 = true;
    bool b1 = true;
    if (f != 0x8b && f != 0xd8) b1 = false;
    if (!b1 && f != 0xde) b2 = false;
    return b2 || f == 0xe6;
}

}  // namespace

// orig FUN_0013a140 (work/decompiled/0013a140.c)
bool playerAiming(const PlayerControl& ctl) { return ctl.aimMarker; }

// orig FUN_0013a060 (work/decompiled/0013a060.c)
void playerAimMarkerFree(PlayerControl& ctl) {
    if (ctl.aimMarker) {
        hooks::aimMarkerFree();
        ctl.aimMarker = false;
    }
}

// orig FUN_00139f80 (work/decompiled/00139f80.c)
void playerAimMarkerCreate(PlayerControl& ctl) {
    if (!ctl.aimMarker) ctl.aimMarker = hooks::aimMarkerAlloc();
}

// orig FUN_00134820 (work/decompiled/00134820.c; branch structure checked in the disassembly at 0x134820)
int statusSpecialTier(const StatusObj& s) {
    const int w = hooks::weaponClass();   // FUN_001d7d10(0x53d910)
    if (w == 3) return 0;
    const int h2 = s.block.hidden[2];     // *(*(block +0x58) + 8)
    int v = 0;
    if (w == -1) {
        if (h2 >= 0x8c) v = 2;
        else if (h2 >= 0x46) v = 1;
        else return 0;
    } else {
        v = 0x5f < h2 ? 1 : 0;
        if (s.block.brand == kBrandST13) v = v + 1;   // brand ST13
        v = hooks::equipKindBonus(v, 6);              // FUN_00134310(v, 6)
    }
    if (!(v < 3)) v = 2;
    return v;
}

// orig FUN_0012c270 (work/decompiled/0012c270.c) + FUN_0012c2e0 (work/decompiled/0012c2e0.c)
float chrDistance(const Chr& a, const Chr& b) {
    const Vec3f& pa = a.xf.m.pos;
    const Vec3f& pb = b.xf.m.pos;
    float d = 0.0f;
    const float dz = pa[2] - pb[2];
    const float dy = pa[1] - pb[1];
    const float dx = pa[0] - pb[0];
    const float s = dz * dz + dx * dx + dy * dy;
    if (s != 0.0f) d = std::sqrt(s);
    if (uint32_t(a.type) < 3) d = d - a.shape.radius;   // *(*(phys +0x5c) + 4)
    if (uint32_t(b.type) < 3) d = d - b.shape.radius;
    if (d < 0.0f) d = 0.0f;
    return d;
}

// orig FUN_0012c360 (work/decompiled/0012c360.c). FUN_001280d0/FUN_00128080 map types 0..2 to list 0 (0x50a7b0).
// UNVERIFIED: list 0x50a7b0 membership/order is taken as the character task list filtered to types 0..2.
Chr* chrNearestOfType(float maxDist, const Chr& from, int type) {
    int list = -1;
    if (type == 4) list = 2;
    else if (type == 3) list = 1;
    else if (type == 2 || type == 1 || type == 0) list = 0;
    Chr* best = nullptr;
    float bestD = -1.0f;
    if (list == 0) {
        for (Chr* c : characters().list()) {
            if (c == nullptr || uint32_t(c->type) >= 3) continue;
            if (type != c->type) continue;
            if (list == 0 && chrDead(*c)) continue;
            if ((c->taskFlags & 0x10000000u) == 0) {
                const float d = chrDistance(*c, from);
                if (bestD < 0.0f || d < bestD) {
                    bestD = d;
                    best = c;
                }
            }
        }
    }
    if (0.0f <= maxDist && maxDist < bestD) best = nullptr;
    return best;
}

// orig FUN_0013c4e0 (work/decompiled/0013c4e0.c)
int playerAttackStart(PlayerControl& ctl, Chr& chr) {
    const uint32_t pressed = pad().port[0].pressed;   // uRam004e6288
    const ButtonConfig& bc = buttons();
    // KEY ASSIGN rows (str 0x5c/0x5d via getters 0x15d7a0/0x15d7b0): 0x4e65d0 = "Attack" (default Square: the
    // AT18/AT13/AT06 combo, charge on hold), 0x4e65c8 = "Special Attack" (default Cross: AT12, the kick bare-handed).
    // The local names below are historical: `attack` holds the Special Attack slot, `special` the Attack slot.
    const uint32_t attack = bc.slot[1];               // 0x4e65c8
    const uint32_t special = bc.slot[2];              // 0x4e65d0
    if ((pressed & (special | attack)) == 0) return 0;
    if ((chr.status.block.statusMask & 2) != 0) {    // Confusion
        if ((pressed & attack) != 0) return 0;
        if (hooks::gameRng().rand(1) != 0) return 0;  // FUN_002071e0(0x53eb20, 1)
    }
    ctl.attacked = 1;
    ctl.combo = 0;
    ctl.counter = 0;
    zeroMoveVel(chr);
    const int w = hooks::weaponClass();               // FUN_001d7d10(0x53d910)
    const char* name;
    if ((pressed & special) == 0) {
        if (w == 3) return 0;
        name = "AT12";                                // DAT_0044b1c0
    } else if (w == 3) {
        if (!hooks::gunHasAmmo()) return 0;
        name = "AT01";                                // DAT_0044b1d0
    } else {
        counters().add(0xb, 1);                       // FUN_00179570(0x53c5c0, 0xb, 1)
        talkFlagSpecial();                            // FUN_001a9ba0(0x53c6e0)
        const int v = statusSpecialTier(chr.status);  // FUN_00134820
        if (v == 1) name = "AT13";                    // DAT_0044b1e0
        else if (v == 0) name = "AT18";               // DAT_0044b1d8
        else name = "AT06";                           // DAT_0044b1e8
    }
    // PORT (game/aim_assist.hpp): keyboard + mouse only - the attack is certain to start here; turn onto the camera
    // direction / assisted target with FUN_00128fc0 before the action starts. No-op with a gamepad.
    if (aimAssistHooks().attackStart != nullptr) aimAssistHooks().attackStart(ctl, chr, w);
    chr.act.start(withSuffix(name));                  // FUN_0013a4b0 + FUN_00143aa0
    return 1;
}

// orig FUN_0013c2c0 (work/decompiled/0013c2c0.c)
void playerChargeHold(PlayerControl& ctl, Chr& chr) {
    const uint32_t held = pad().port[0].held;         // uRam004e6284
    const uint32_t special = buttons().slot[2];       // 0x4e65d0
    if ((chr.status.block.statusMask & 2) != 0 || (ctl.counter & 0xa0000000u) != 0) return;
    const ActName cur = actName(chr.act.currentName());   // the original dereferences without a null test
    if (word0(cur, "AT05")) {                         // DAT_0044b1c8
        if (ctl.counter == 0x10000000u && (chr.act.flags() & 0x10000u) != 0) ctl.counter = 0;
        if ((held & special) == 0) {
            if (ctl.counter == 0x10000000u) {
                movement(ctl, chr, false);
            } else {
                ctl.counter = ctl.counter | 0x20000000u;
                if ((chr.act.flags() & 0x10000u) == 0) chr.model.anim.setSpeed(1.0f);   // FUN_0010ad90(1.0, actor)
            }
        } else if (ctl.counter != 0x10000000u) {
            const int32_t v = int32_t(ctl.counter) + 1;
            ctl.counter = uint32_t(v);
            if (200 < v) ctl.counter = 200;
        }
    } else if ((held & special) == 0) {
        ctl.counter = 0x80000000u;
    } else {
        const int32_t v = int32_t(ctl.counter) + 1;
        ctl.counter = uint32_t(v);
        if (7 < v) {
            ctl.counter = 0x10000000u;
            chr.act.start(withSuffix("AT05"));        // FUN_0013a4b0 + FUN_00143aa0
        }
    }
}

namespace {

// FUN_0013c790 tail: aim yaw/pitch from the left stick (normal view: yaw or pitch; first person: pitch only).
void aimPitchFromStick(PlayerControl& ctl, float ay, float sy, float mag) {
    float d = ((ay - 0.5f) / 0.5f) * (5.0f * mag);   // mul order as at 0x13cb64..0x13cb78
    if (sy < 0.0f) {
        const float v = ctl.aimPitch + d;
        ctl.aimPitch = v;
        if (!(v <= 60.0f)) ctl.aimPitch = std::bit_cast<float>(0x42700000u);    // 60.0
    } else {
        const float v = ctl.aimPitch - d;
        ctl.aimPitch = v;
        if (v < -60.0f) ctl.aimPitch = std::bit_cast<float>(0xc2700000u);       // -60.0
    }
}

// FUN_0013c790 tail: trajectory preview and aim animation ping-pong.
void aimPreview(PlayerControl& ctl, Chr& chr, const void* item) {
    const ChrTransform& xf = chr.xf.m;
    // FUN_003a0330(&start, {-8.416, 115.208, 42.576}, model node +0x20 matrix) + position.
    // UNVERIFIED: the model node matrix (**(chr+0x2c) +0x10 +0x20) is taken as the character's rotation rows.
    const Vec3f local{std::bit_cast<float>(0xc106a7f0u), std::bit_cast<float>(0x42e66a7fu),
                      std::bit_cast<float>(0x422a4dd3u)};
    float node[12] = {xf.right[0], xf.right[1], xf.right[2], 0.0f, xf.up[0], xf.up[1], xf.up[2], 0.0f,
                      xf.fwd[0],   xf.fwd[1],   xf.fwd[2],   0.0f};
    Vec3f start = mulVec3(local, node);
    start[0] = start[0] + xf.pos[0];
    start[1] = start[1] + xf.pos[1];
    start[2] = start[2] + xf.pos[2];
    float m[12] = {};
    axisAngle(m, (ctl.aimPitch * std::bit_cast<float>(0x40490fdbu)) / 180.0f, xf.right);   // FUN_0039ddd0
    ctl.aimDir = mulVec3(xf.fwd, m);                                                       // FUN_003a0330
    const float k = std::bit_cast<float>(0x40400000u);   // DAT_0044b1f0 = 3.0
    const Vec3f side{xf.right[0] * k, xf.right[1] * k, xf.right[2] * k};
    hooks::aimTrajectory(item, start, ctl.aimDir, side);  // FUN_0013a150
    ActorAnim& anim = chr.model.anim;
    if (ctl.aimPhase == 1) {
        const float v = ctl.aimSpeed - 0.05f;   // 0x3d4ccccd
        ctl.aimSpeed = v;
        if (v < -0.4f) ctl.aimSpeed = std::bit_cast<float>(0xbecccccdu);
        const int frame = int(anim.currentTimeNs() / 0x1fca055);   // FUN_00244f38(FUN_0010aaf0(actor), 0x1fca055)
        if (float(frame) < 6.0f) ctl.aimPhase = ctl.aimPhase - 1;
    } else if (ctl.aimPhase == 0) {
        const float v = ctl.aimSpeed + 0.05f;
        ctl.aimSpeed = v;
        if (0.4f < v) ctl.aimSpeed = std::bit_cast<float>(0x3ecccccdu);
        const int frame = int(anim.currentTimeNs() / 0x1fca055);
        if (6.5f < float(frame)) ctl.aimPhase = ctl.aimPhase + 1;
    }
    anim.setSpeed(ctl.aimSpeed);   // FUN_0010ad90(+0x38, actor)
}

}  // namespace

// orig FUN_0013c790 (work/decompiled/0013c790.c). L1/R1 test `extraout_a1` = pressed (0x4e6288 is still in a1:
// loaded at 0x13c834, FUN_0013a140 does not touch it).
int playerThrowAim(PlayerControl& ctl, Chr& chr) {
    if (ctl.inputLock != 0) return 0;
    const int slot = hooks::itemSlotSelected();       // FUN_001573f0(0x517b60)
    const PadPort& p = pad().port[0];
    const ButtonConfig& bc = buttons();
    if (slot < 0) {
        if (!playerAiming(ctl) && (ctl.flags & 2) == 0) return 0;
        ctl.flags = ctl.flags & 0xfffffffdu;
        idle(chr, ctl);
        return 1;
    }
    if ((p.pressed & 0x20) != 0) {                    // circle
        if (!playerAiming(ctl)) return 0;
        if ((ctl.flags & 2) == 0) {
            ctl.flags = ctl.flags | 2;
            idle(chr, ctl);
            return 1;
        }
        return 0;
    }
    if ((p.held & bc.slot[5]) == 0) {                 // throw button (0x4e65e8) not held
        if ((ctl.flags & 2) != 0) {
            ctl.flags = ctl.flags & 0xfffffffdu;
            return 0;
        }
        if (!playerAiming(ctl)) {
            if ((p.pressed & bc.slot[6]) == 0) {      // 0x4e65f0
                if ((p.pressed & bc.slot[4]) != 0) hooks::itemSlotNext();   // 0x4e65e0 -> FUN_001575f0
            } else {
                hooks::itemSlotPrev();                // FUN_00157410
            }
            return 0;
        }
        // Released while aiming: request the ThrowLock state (LAB_0013cf80) at priority 0.
        StateMachine& sm = chr.sm;
        if ((sm.curFactory != StateId::None || sm.next != StateId::None || sm.popRequested) && 0 < sm.priority)
            return 1;
        sm.next = StateId::ThrowLock;   // +0x18
        sm.popRequested = false;
        sm.arg = 0;                     // +0x08 UNVERIFIED: the original stores the Chr* here; the port's states
                                        //       reach their owner through the Chr that holds the machine
        sm.priority = 0;                // +0x0c
        sm.priorityCopy = 0;            // +0x10
        return 1;
    }
    if ((ctl.flags & 2) != 0) return 0;
    if (hooks::itemThrowCheck(slot) != -1) return 0;  // FUN_001d7c80(0x53d910, slot)
    if (!playerAiming(ctl)) {
        playerAimMarkerCreate(ctl);   // FUN_00139f80
        ctl.aimPitch = 0.0f;
        ctl.aimDir = chr.xf.m.fwd;    // xf +0x30..+0x38
        int clip = chr.model.anim.findClip("M079");   // strcmp over the animation list (DAT_0044b1f8)
        if (clip < 0) clip = 0;                       // not found: index 0 (iVar16 = 0)
        chr.model.anim.change(0.25f, 0.5f, clip, 4);  // FUN_0010a8e0(0x3e800000, 0x3f000000, actor, clip, 4)
        ctl.aimPhase = 0;
        ctl.aimSpeed = 0.0f;
    }
    hooks::heldItemModelHide(chr, 0, 0);              // FUN_001b2980(chr, 0, 0)
    hooks::heldItemModelHide(chr, 1, 0);
    const void* item = hooks::inventoryItem(slot);    // *(0x53d914 + slot*4)
    if (chr.act.currentName() != nullptr) {
        chr.act.stop();                               // FUN_00143bc0
        zeroMoveVel(chr);
    }
    const float sx = p.lstick[0], sy = p.lstick[1], mag = p.lstick[2];   // 0x4e62d0 / d4 / d8
    if ((ctl.flags & 0x20) == 0) {
        if (0.0f < mag) {
            const float ax = sx < 0.0f ? -sx : sx;
            const float ay = sy < 0.0f ? -sy : sy;
            if (0.5f < ax) {
                float r = ((ax - 0.5f) / 0.5f) * (std::bit_cast<float>(0x3e32b8c3u) * mag);   // 0.17453294
                if (0.0f <= sx) r = -r;
                rotateAboutUp(chr.xf.m, r);           // FUN_00128ae0(r, chr)
            } else if (0.5f < ay) {
                aimPitchFromStick(ctl, ay, sy, mag);
            }
        }
    } else if (0.0f < mag) {
        const float ay = sy < 0.0f ? -sy : sy;
        if (0.5f < ay) aimPitchFromStick(ctl, ay, sy, mag);
    }
    aimPreview(ctl, chr, item);
    return 1;
}

namespace {

// FUN_0013b0d0: first-person option change (FUN_0015dcb0 vs the +0x48 cache).
void firstPersonSync(PlayerControl& ctl, Chr& chr) {
    const uint32_t opt = (options().camera & 4) != 0 ? 1u : 0u;   // FUN_0015dcb0: uRam0051af68 & 4
    if (ctl.fpCache == opt) return;
    ctl.fpCache = opt;
    if (opt == 0) {
        const char* cur = chr.act.currentName();
        if (cur != nullptr && head2(actName(cur), "MV")) chr.act.stop();   // DAT_0044b148, FUN_00143bc0
        zeroMoveVel(chr);
        ctl.flags = ctl.flags & 0xffffffdfu;
        if (ctl.camera != nullptr) {
            if (hooks::floorId() == 99 || hooks::floorId() == 0x5b)   // FUN_0011c100(0x50a2a0): floor id == 99
                ctl.camera->applyPreset(kCameraPresetHub);             // (30, 145, 400, -6)
            else
                ctl.camera->applyPreset(kCameraPresetDungeon);         // (50, 185, 300, -15)
        }
    } else {
        ctl.flags = ctl.flags | 0x20;
        if (ctl.camera != nullptr) ctl.camera->applyPreset(kCameraPresetFirstPerson);   // (1, 120, 10, -2)
    }
}

// FUN_0013b0d0: floor-edge hole check. Returns true when the fall event was started (the original returns).
bool holeCheck(PlayerControl& ctl, Chr& chr) {
    if (!holeFloor()) {
        ctl.holeTicks = 0;
        return false;
    }
    if (chr.cell == nullptr) return false;
    const uint32_t cell = *chr.cell;
    const int room = (cell & 0xf000) == 0x1000 ? int(cell & 0xf) : -1;
    if (room < 0 || hooks::roomKind(room) != ' ') return false;
    const float dx = chr.xf.m.pos[0] - hooks::roomOriginX(room);   // FUN_0011e6c0
    const float oz = hooks::roomOriginZ(room);                     // FUN_0011e740
    const float dz = chr.xf.m.pos[2] - oz;
    // PORT: reaching the end of the walkway is enough -- standing there for kHoleAssistTicks with any
    // movement held (or none) starts the descent too, so a sideways slide or a released key no longer resets it.
    // The original's push test below is unchanged. A once-a-second log line helps diagnose a failed attempt.
    {
        constexpr int kHoleAssistTicks = 10;
        static int logTick = 0;
        if (++logTick >= 30) {
            logTick = 0;
            platform::log("hole: room ' ' local x " + std::to_string(int(dx)) + " z " + std::to_string(int(dz)) +
                          " moveVel z " + std::to_string(chr.phys.moveVel[2]) + " ticks " + std::to_string(ctl.holeTicks));
        }
        const bool atEnd = 1050.0f < dx && dx < 1350.0f && dz < 1450.0f;
        if (atEnd) {
            ++ctl.holeAssistTicks;
            if (ctl.holeAssistTicks > kHoleAssistTicks) {
                ctl.holeAssistTicks = 0;
                ctl.holeTicks = 0;
                hooks::startFloorEvent(0x32);   // FUN_001d1d30(0x32), as below
                return true;
            }
        } else {
            ctl.holeAssistTicks = 0;
        }
    }
    if (dx <= 1100.0f || 1300.0f <= dx || 1400.0f <= dz || 0.0f <= chr.phys.moveVel[2]) {
        ctl.holeTicks = 0;
    } else {
        ctl.holeTicks = ctl.holeTicks + 1;
        if (0x1e < ctl.holeTicks) {
            ctl.holeTicks = 0;
            hooks::startFloorEvent(0x32);   // FUN_001d1d30(0x32)
            return true;
        }
    }
    return false;
}

// FUN_0013b0d0: item slot clamp (FUN_001573f0 / FUN_00157400).
void itemSlotClamp() {
    hooks::inventoryTick();                 // FUN_001d9b60(0x53d910)
    const int count = hooks::inventoryCount();
    int sel = hooks::itemSlotSelected();
    if (count == 0) sel = -1;
    else if (sel < 0) sel = 0;
    else if (count <= sel) sel = count - 1;
    hooks::setItemSlotSelected(sel);
}

// FUN_0013b0d0: SELECT toggles the floor map; the map re-opens while flagged.
// PORT: a second press moves the shown map into the bottom-left corner (mini map); the third hides it.
void floorMapInput(PlayerControl& ctl) {
    if ((pad().port[0].pressed & 0x100) != 0) {
        if (!hooks::floorMapShown()) {
            hooks::floorMapSetCorner(false);
            hooks::floorMapShow();          // FUN_00126cc0(0x50a350)
            talkFlagMap();                  // FUN_001a9fb0(0x53c6e0)
            ctl.mapShown = 1;
        } else if (!hooks::floorMapCorner()) {
            hooks::floorMapSetCorner(true);
        } else {
            hooks::floorMapSetCorner(false);
            hooks::floorMapHide();          // FUN_00126fb0(0x50a350)
            ctl.mapShown = 0;
        }
    }
    if (ctl.mapShown != 0 && !hooks::floorMapShown() && hooks::fadeFramesLeft() == 0 && !hooks::dialogShown()) {
        if (hooks::floorId() == 0x5b) ctl.mapShown = 0;
        else hooks::floorMapShow();
    }
}

// FUN_0013b0d0: lock-on target keep / select (held & 0x4e65f8).
void lockOnSelect(PlayerControl& ctl, Chr& chr, uint32_t held) {
    if ((held & buttons().slot[7]) == 0) {
        setLockTarget(ctl, nullptr);
        return;
    }
    Chr* t = ctl.target;
    if (t != nullptr) {
        bool found = false;
        for (Chr* c : characters().list()) {   // task list 0x50b134, entries with +0x10 & 0x10000000 skipped
            if (c == nullptr || (c->taskFlags & 0x10000000u) != 0) continue;
            if (c == t && ctl.targetSerial == c->serial) {
                found = true;
                break;
            }
        }
        bool keep = false;
        if (found && (t->taskFlags & 0x10000000u) == 0) {
            if (!chrDead(*t) && chrDistance(chr, *t) <= 300.0f) keep = true;   // 0x43960000
        }
        if (!keep) setLockTarget(ctl, nullptr);
    }
    Chr* enemy = chrNearestOfType(300.0f, chr, 2);   // FUN_0012c360(300.0, 0x50b130, chr, 2)
    Chr* npc = chrNearestOfType(300.0f, chr, 1);     // FUN_0012c360(300.0, 0x50b130, chr, 1)
    Chr* pick = enemy;
    if (enemy == nullptr || npc == nullptr) {
        if (enemy == nullptr && npc != nullptr) pick = npc;
    } else {
        const float de = chrDistance(chr, *enemy);
        const float dn = chrDistance(chr, *npc);
        if (dn <= de) pick = npc;
    }
    if (pick == nullptr) {
        setLockTarget(ctl, nullptr);
    } else if (ctl.target == nullptr) {
        setLockTarget(ctl, pick);
        ctl.targetSerial = pick->serial;
    }
}

// FUN_0013b0d0 LAB_0013bb90 (target set): direction to the target and the marker sprite.
void lockOnMarker(PlayerControl& ctl, const Chr& chr, Vec3f& toTarget) {
    const Chr& t = *ctl.target;
    toTarget = {t.xf.m.pos[0] - chr.xf.m.pos[0], 0.0f, t.xf.m.pos[2] - chr.xf.m.pos[2]};
    normalize(toTarget);   // FUN_00206ab0
    if (!ctl.lockMarker.live) {
        ctl.lockMarker.live = hooks::lockMarkerAlloc();
        if (ctl.lockMarker.live) {
            ctl.lockMarker.color = 0xff00ff00u;   // sprite +0x30
            ctl.lockMarker.flags3a = uint16_t(ctl.lockMarker.flags3a | 1);
        }
    }
    float h = 0.0f;   // FUN_00133560(id) +0xa0
    const data::PrmCharFile* prm = characters().prm;
    const data::PrmChar* rec = prm != nullptr ? data::findPrmChar(*prm, t.id) : nullptr;
    if (rec != nullptr) h = rec->height;
    // UNVERIFIED: a missing PRMCHAR record dereferences null in the original; the port uses height 0.
    float x = t.xf.m.pos[0], y = t.xf.m.pos[1], z = t.xf.m.pos[2];
    const uint32_t id = t.id & 0xffffffu;
    if (id == kIdE16) {
        const float k = std::bit_cast<float>(0x42480000u);   // DAT_0044b190 = 50.0
        h = 180.0f;
        x = x + t.xf.m.fwd[0] * k;
        y = y + t.xf.m.fwd[1] * k;
        z = z + t.xf.m.fwd[2] * k;
    } else if (id == kIdE19) {
        h = hooks::aiQuery(t, 0x1000008) == 0 ? 180.0f : 50.0f;   // FUN_001f1730(ai, 0x1000008)
    }
    const float bob = ctl.markerBob - 2.0f;
    if (ctl.lockMarker.live) ctl.lockMarker.pos = {x, y + h + ctl.markerBob, z};
    ctl.markerBob = bob;
    if (bob < -20.0f) ctl.markerBob = 0.0f;
    ctl.markerSpin = ctl.markerSpin + 30.0f;
    if (360.0f <= ctl.markerSpin) ctl.markerSpin = ctl.markerSpin - 360.0f;
    if (ctl.lockMarker.live) ctl.lockMarker.rotDeg = {0.0f, ctl.markerSpin, 0.0f};   // FUN_00105240
}

void faceTarget(Chr& chr, const Vec3f& toTarget) {
    turnToward(90.0f, chr.xf.m, toTarget, true);   // FUN_00128fc0(0x42b40000, chr, toTarget, 1)
}

// FUN_0013b0d0 tail, normal view (flags & 0x20 == 0).
void normalView(PlayerControl& ctl, Chr& chr, const char* curp, const Vec3f& toTarget) {
    if (curp != nullptr) {
        const ActName cur = actName(curp);
        if (head2(cur, "AT")) {
            if (word1(cur, "_G1")) return;   // DAT_0044b138 ("_G1\0" at name +4)
            if (ctl.combo < 6) {
                ctl.combo = ctl.combo + 1;
                if (ctl.target == nullptr) movement(ctl, chr, true);
                else faceTarget(chr, toTarget);
            }
            if (word0(cur, "AT12")) return;   // DAT_0044b1c0
            playerChargeHold(ctl, chr);
            return;
        }
    }
    if (ctl.target != nullptr) faceTarget(chr, toTarget);
    if (playerThrowAim(ctl, chr) == 1) return;
    if (playerAttackStart(ctl, chr) == 1) return;
    movement(ctl, chr, false);
}

// FUN_0013b0d0 tail, first-person view (flags & 0x20).
void firstPersonView(PlayerControl& ctl, Chr& chr, const char* curp, const Vec3f& toTarget) {
    if (curp != nullptr) {
        const ActName cur = actName(curp);
        if (head2(cur, "AT")) {
            if (word1(cur, "_G1")) {
                if (ctl.target != nullptr) faceTarget(chr, toTarget);
                movement(ctl, chr, false);
                return;
            }
            if (ctl.combo < 6) ctl.combo = ctl.combo + 1;
            if (!word0(cur, "AT12")) playerChargeHold(ctl, chr);
            if (ctl.target != nullptr) faceTarget(chr, toTarget);
            movement(ctl, chr, false);
            return;
        }
    }
    if (ctl.target != nullptr) faceTarget(chr, toTarget);
    if (playerThrowAim(ctl, chr) == 1) return;
    playerAttackStart(ctl, chr);   // result ignored in first person
    movement(ctl, chr, false);
}

}  // namespace

// orig FUN_0013b0d0 (work/decompiled/0013b0d0.c)
void playerControl(PlayerControl& ctl, Chr& chr) {
    ctl.flags = ctl.flags & 0xffffffefu;
    const StateMachine& sm = chr.sm;
    bool stagger = false;
    if (sm.next == StateId::None && !sm.popRequested) stagger = sm.curFactory == StateId::HitStagger;   // LAB_0012a490
    if (stagger) ctl.inputLock = 2;
    else if (ctl.inputLock != 0) ctl.inputLock = ctl.inputLock - 1;
    firstPersonSync(ctl, chr);

    if (hooks::eventState() == 0 && (ctl.flags & 0x10000) == 0 && !chrDead(chr) && ctl.camera != nullptr) {
        // FUN_0013d970(ctl): the camera reads these controller words live in the original.
        if (ctl.camera->world != nullptr) {
            ctl.camera->world->controlFlags = ctl.flags;
            ctl.camera->world->controlRotLock = int32_t(ctl.lock);
            ctl.camera->world->holdingObject = playerAiming(ctl);
        }
        ctl.camera->inputFromPad(pad().port[0]);
    }

    const char* curp = chr.act.currentName();   // FUN_00143d30(act, -1)
    if (curp != nullptr) {
        const ActName cur = actName(curp);
        if (playerAiming(ctl)) {
            playerAimMarkerFree(ctl);              // FUN_0013a060
            if (head2(cur, "TH")) return;
            hooks::heldItemModelShow(chr, 0);      // FUN_001b2930(chr, 0)
            hooks::heldItemModelShow(chr, 1);
        }
        if (head2(cur, "DM") || head2(cur, "DD") || head2(cur, "TH")) {
            removeLockMarker(ctl);
            return;
        }
    }
    if (hooks::eventState() == 1 || (ctl.flags & 0x10000) != 0 || chrDead(chr)) {
        removeLockMarker(ctl);
        return;
    }

    if (holeCheck(ctl, chr)) return;
    itemSlotClamp();
    const uint32_t held = pad().port[0].held;   // uRam004e6284 (read before the map input, as the original)
    floorMapInput(ctl);
    lockOnSelect(ctl, chr, held);
    Vec3f toTarget{0.0f, 0.0f, 0.0f};
    if (ctl.target == nullptr) removeLockMarker(ctl);
    else lockOnMarker(ctl, chr, toTarget);

    if ((ctl.flags & 0x20) == 0) normalView(ctl, chr, curp, toTarget);
    else firstPersonView(ctl, chr, curp, toTarget);
}

}  // namespace baroque::game
