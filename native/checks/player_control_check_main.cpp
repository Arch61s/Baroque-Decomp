// Baroque-PlayerControlCheck: headless run of the translated player controller FUN_0013b0d0 (game/character/
// player_control.cpp) with a hand-built player Chr (C00A, DATA/ACTION/C00A.ACT) and scripted pad input.
// Verifies per tick the ACT action chosen (WT01_H1 idle, MV01_H1 run, MV02_H1 walk, MV02/MV03 strafe set, AT12_H1
// attack, AT18/AT13/AT06 special by hidden stat 2, AT05_H1 charge after 8 held ticks, WT02/WT03 idle variants) and
// the move velocity magnitudes (12 / 3 / 6 units per tick, x1.5 at hidden stat 0 = 255), lock-on target selection,
// first-person presets and the dead / Confusion gates. The ACT runner is not ticked: the check ends actions by
// calling ActRunner::stop() where an action would end. Prints a line per tick; exits 1 on any failure.
//
// character.cpp is not linked (written separately); the few Chr functions the controller needs are stubbed below.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "alchemy/ps2_float.hpp"
#include "data/act.hpp"
#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/character/player_control.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "platform/platform.hpp"

using namespace baroque;
using namespace baroque::game;

// ---- stubs for character.cpp (not linked into this check) ----
namespace baroque::game {
ChrManager& characters() {
    static ChrManager m;
    return m;
}
Chr* ChrManager::create(const char* id, bool addToList) {
    auto c = std::make_unique<Chr>();
    uint32_t v = 0;
    for (int i = 0; i < 4 && id[i] != '\0'; ++i) v |= uint32_t(uint8_t(id[i])) << (8 * i);
    c->id = v;
    c->serial = nextSerial_++;
    c->phys.shape = &c->shape;
    c->phys.pos = &c->xf.m.pos;
    Chr* raw = c.get();
    owned_.push_back(std::move(c));
    if (addToList) list_.push_back(raw);
    return raw;
}
int statusHpMaxTotal(const StatusObj& s) { return s.block.hpMax; }   // check stub: base HP max only
}  // namespace baroque::game

namespace {

int g_failures = 0;
int g_tick = 0;
uint32_t g_prevHeld = 0;

void fail(const std::string& what) {
    ++g_failures;
    std::printf("  FAIL tick %d: %s\n", g_tick, what.c_str());
}

float len3(const Vec3f& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

std::string curName(const Chr& c) {
    const char* n = c.act.currentName();
    return n != nullptr ? std::string(n) : std::string("(none)");
}

// One controller tick with the given pad state.
void tick(PlayerControl& ctl, Chr& pc, uint32_t held, float lx = 0.0f, float ly = 0.0f, float lmag = 0.0f) {
    PadPort& p = pad().port[0];
    p.held = held;
    p.pressed = held & ~g_prevHeld;
    p.released = ~held & g_prevHeld;
    p.repeat = p.pressed;
    p.lstick[0] = lx;
    p.lstick[1] = ly;
    p.lstick[2] = lmag;
    p.rstick[0] = p.rstick[1] = p.rstick[2] = 0.0f;
    g_prevHeld = held;
    playerControl(ctl, pc);
    std::printf("tick %3d held=%05x stick=(%5.2f,%5.2f,%4.2f)  action=%-8s |moveVel|=%7.3f fwd=(%6.3f,%6.3f,%6.3f) "
                "flags=%05x combo=%d charge=%08x target=%s\n",
                g_tick, held, lx, ly, lmag, curName(pc).c_str(), len3(pc.phys.moveVel), pc.xf.m.fwd[0],
                pc.xf.m.fwd[1], pc.xf.m.fwd[2], ctl.flags, ctl.combo, ctl.counter, ctl.target ? "yes" : "no");
    ++g_tick;
}

void expectAction(const Chr& pc, const char* name) {
    if (curName(pc) != name) fail("action " + curName(pc) + " != expected " + name);
}
void expectSpeed(const Chr& pc, float v) {
    const float l = len3(pc.phys.moveVel);
    if (std::fabs(l - v) > 1e-3f) fail("|moveVel| " + std::to_string(l) + " != expected " + std::to_string(v));
}

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    alchemy::ps2::Ps2FloatScope fpu;
    buttonDefaults();          // FUN_00100f10: attack 0x40 (cross), special 0x80 (square), lock-on 0x01 (L2)
    options().camera = 0;      // third person

    const auto actPath = platform::assetRoot() / "DATA" / "ACTION" / "C00A.ACT";
    const std::vector<uint8_t> bytes = readFile(actPath);
    if (bytes.empty()) {
        std::printf("FAIL: cannot read %s\n", actPath.string().c_str());
        return 1;
    }

    ChrManager& mgr = characters();
    Chr& pc = *mgr.create("C00A", true);
    mgr.setPlayer(&pc);
    pc.type = kChrPlayerType;
    pc.actFile = data::parseActOrThrow(data::Bytes(bytes.data(), bytes.size()));
    pc.actLoaded = true;
    pc.shape.radius = 20.0f;
    pc.status.owner = &pc;
    pc.status.block.hpMax = 100;
    pc.status.block.hp = 100;
    pc.model.anim.init(&pc.model.clips, nullptr);
    PlayerControl ctl;
    pc.act.init(&pc.actFile, &pc.model.anim, &pc.phys, &ctl, &pc.status.block.statusMask);
    std::printf("C00A.ACT: %zu actions\n", pc.actFile.actions.size());
    bool hasAt12 = false;
    for (const auto& a : pc.actFile.actions) hasAt12 = hasAt12 || a.name == "AT12_H1";
    if (!hasAt12) fail("C00A.ACT has no AT12_H1");

    // Player at the origin facing +x; camera behind a +z-facing reference (camera forward = (0,0,-1), right = (-1,0,0)
    // after FUN_0013e130 with player forward +z).
    CameraWorld camWorld;
    camWorld.playerPos[0] = camWorld.playerPos[1] = camWorld.playerPos[2] = 0.0f;
    EngineProjection engine;
    Camera cam(camWorld, engine, CameraHooks{});
    ctl.camera = &cam;
    snapForward(pc.xf.m, Vec3f{1.0f, 0.0f, 0.0f});
    std::printf("camera forward=(%.3f,%.3f,%.3f) right=(%.3f,%.3f,%.3f)\n", cam.forward[0], cam.forward[1],
                cam.forward[2], cam.right[0], cam.right[1], cam.right[2]);

    // 1. idle
    std::printf("-- idle\n");
    for (int i = 0; i < 3; ++i) {
        tick(ctl, pc, 0);
        expectAction(pc, "WT01_H1");
        expectSpeed(pc, 0.0f);
    }
    // 2. stick fully up: run toward -camera forward = +z, turning from +x at FUN_00128fc0(45 deg, scaled)
    std::printf("-- run (stick up, mag 1.0)\n");
    for (int i = 0; i < 12; ++i) {
        tick(ctl, pc, 0x10000, 0.0f, -1.0f, 1.0f);
        expectAction(pc, "MV01_H1");
        expectSpeed(pc, 12.0f);
        if ((ctl.flags & 0x10) == 0) fail("flags 0x10 (moved) not set");
    }
    if (pc.xf.m.fwd[2] < 0.99f) fail("player did not turn to +z");
    // 3. walk
    std::printf("-- walk (mag 0.5)\n");
    for (int i = 0; i < 3; ++i) {
        tick(ctl, pc, 0, 0.0f, -1.0f, 0.5f);
        expectAction(pc, "MV02_H1");
        expectSpeed(pc, 3.0f);
    }
    // 4. hidden stat 0 = 255: speed x (255/255 * 0.5 + 1)
    std::printf("-- run with hidden[0] = 255\n");
    pc.status.block.hidden[0] = 255;
    tick(ctl, pc, 0x10000, 0.0f, -1.0f, 1.0f);
    expectAction(pc, "MV01_H1");
    expectSpeed(pc, 18.0f);
    pc.status.block.hidden[0] = 0;
    // 5. D-pad: strafe set (moving = -1), base 6
    std::printf("-- d-pad up / down\n");
    tick(ctl, pc, 0x1000);
    expectAction(pc, "MV02_H1");
    expectSpeed(pc, 6.0f);
    tick(ctl, pc, 0x4000);
    expectAction(pc, "MV03_H1");
    expectSpeed(pc, 6.0f);
    tick(ctl, pc, 0x2000);   // right: +camRight = (-1,0,0); player fwd +z, right = up x fwd = (+1,0,0) -> MV04
    expectAction(pc, "MV04_H1");
    expectSpeed(pc, 6.0f);

    // 6. lock-on (L2 held) with an enemy at 200 units (distance minus radii = 160 <= 300)
    std::printf("-- lock-on\n");
    Chr& enemy = *mgr.create("E01A", true);
    enemy.type = kChrEnemy;
    enemy.shape.radius = 20.0f;
    enemy.status.block.hp = 10;
    enemy.status.block.hpMax = 10;
    enemy.xf.m.pos = {0.0f, 0.0f, 200.0f};
    for (int i = 0; i < 3; ++i) {
        tick(ctl, pc, 0x1 | 0x10000, 0.0f, -1.0f, 1.0f);
        if (ctl.target != &enemy) fail("lock-on target not the enemy");
        if (ctl.lock == 0) fail("lock word not set");
        if (!ctl.lockMarker.live) fail("lock marker not created");
        expectAction(pc, "MV02_H1");   // strafe set, moving forward
        expectSpeed(pc, 6.0f);
    }
    if (std::fabs(ctl.lockMarker.pos[2] - 200.0f) > 1e-3f) fail("marker not at the target");
    tick(ctl, pc, 0);   // L2 released -> target dropped, marker removed
    if (ctl.target != nullptr || ctl.lock != 0 || ctl.lockMarker.live) fail("lock-on not released");
    expectAction(pc, "WT01_H1");
    enemy.xf.m.pos = {0.0f, 0.0f, 400.0f};   // 400 - 40 = 360 > 300
    tick(ctl, pc, 0x1);
    if (ctl.target != nullptr) fail("target beyond 300 units selected");
    enemy.taskFlags |= 0x10000000u;

    // 7. attack (cross 0x40 = slot 0x4e65c8)
    std::printf("-- attack (cross)\n");
    tick(ctl, pc, 0);
    expectAction(pc, "WT01_H1");
    tick(ctl, pc, 0x40);
    expectAction(pc, "AT12_H1");
    expectSpeed(pc, 0.0f);
    if (ctl.attacked != 1 || ctl.combo != 0 || ctl.counter != 0) fail("attack start state");
    for (int i = 0; i < 8; ++i) {
        tick(ctl, pc, 0x10000, 0.0f, -1.0f, 1.0f);   // stick while attacking: turn only, no move
        expectAction(pc, "AT12_H1");
        expectSpeed(pc, 0.0f);
    }
    if (ctl.combo != 6) fail("combo did not cap at 6");
    pc.act.stop();   // the action ends
    tick(ctl, pc, 0);
    expectAction(pc, "WT01_H1");

    // 8. special (square 0x80 = slot 0x4e65d0): tier by hidden stat 2, charge to AT05 after 8 held ticks
    std::printf("-- special (square) and charge\n");
    tick(ctl, pc, 0x80);
    expectAction(pc, "AT18_H1");
    for (int i = 1; i <= 8; ++i) {
        tick(ctl, pc, 0x80);
        if (i < 8) {
            expectAction(pc, "AT18_H1");
            if (ctl.counter != uint32_t(i)) fail("charge counter " + std::to_string(ctl.counter));
        }
    }
    expectAction(pc, "AT05_H1");
    if (ctl.counter != 0x10000000u) fail("charge state not 0x10000000");
    tick(ctl, pc, 0x80);
    expectAction(pc, "AT05_H1");
    tick(ctl, pc, 0);   // release while charging-move -> FUN_0013cf90 (idle)
    expectAction(pc, "WT01_H1");
    pc.act.stop();
    tick(ctl, pc, 0);
    pc.status.block.hidden[2] = 0x46;
    tick(ctl, pc, 0x80);
    expectAction(pc, "AT13_H1");
    pc.act.stop();
    tick(ctl, pc, 0);
    pc.status.block.hidden[2] = 0x8c;
    tick(ctl, pc, 0x80);
    expectAction(pc, "AT06_H1");
    pc.act.stop();
    pc.status.block.hidden[2] = 0;
    tick(ctl, pc, 0);

    // 9. Confusion: the attack button is ignored; idle uses WT03 (status mask & 0x3f)
    std::printf("-- confusion\n");
    pc.status.block.statusMask = 2;
    tick(ctl, pc, 0x40);
    expectAction(pc, "WT03_H1");
    if (ctl.attacked != 1) fail("attacked flag changed");
    pc.status.block.statusMask = 0;
    // 10. HP below 30 %
    std::printf("-- low HP\n");
    pc.status.block.hp = 20;
    tick(ctl, pc, 0);
    expectAction(pc, "WT02_H1");
    pc.status.block.hp = 100;

    // 11. first-person option
    std::printf("-- first person\n");
    options().camera = 4;
    tick(ctl, pc, 0x10000, 0.0f, -1.0f, 1.0f);
    if ((ctl.flags & 0x20) == 0) fail("first-person flag not set");
    if (cam.tun.dist != 10.0f || cam.tun.height != 120.0f) fail("first-person camera preset");
    expectAction(pc, "MV01_H1");
    expectSpeed(pc, 12.0f);
    options().camera = 0;
    tick(ctl, pc, 0);
    if ((ctl.flags & 0x20) != 0) fail("first-person flag not cleared");
    if (cam.tun.dist != 300.0f || cam.tun.height != 185.0f) fail("dungeon camera preset");
    expectAction(pc, "WT01_H1");

    // 12. dead: no input
    std::printf("-- dead\n");
    pc.status.block.hp = 0;
    tick(ctl, pc, 0x10000 | 0x40, 0.0f, -1.0f, 1.0f);
    expectAction(pc, "WT01_H1");
    expectSpeed(pc, 0.0f);

    std::printf("%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
