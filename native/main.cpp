// Baroque native host. `main` (FUN_00100730) initialises the subsystems, registers the scenes (FUN_00100100) and
// runs the scene manager (FUN_00206570) from "BootMC..". The original loop is blocking with one engine tick per
// displayed frame at 29.97 Hz (RUNTIME.md §1.4); the port steps the scene manager at that rate and presents every
// host frame (the 2-D layer and movies; 3-D comes with the Game scene).
//
// Options: --toolchain-check, --shot OUT.png TICKS (run TICKS ticks headless-ish and save a 1440x1080 frame),
//          --press-start-at N (scripted START press on tick N, for --shot verification),
//          --press MASK TICK (repeatable; scripted pad bits on tick TICK, for --shot verification).
//          PORT mouse-menu tests (repeatable, UI raster coordinates 0..511 x 0..447, applied on tick TICK):
//          --mouse-at X Y TICK (move), --click-at X Y TICK (move + left click), --rclick-at TICK (right click),
//          --wheel-at N TICK (N wheel notches, positive = up). --shot frames show the cursor as a red marker.
//          --menu-at MODE TICK replaces the open menu page by page MODE (e.g. 10 = GAME CONFIG, reachable only in-game).
//          PORT dev starts: --flag N / --clear-flag N (repeatable; set / clear game flag N at start-up and again at the
//          end of the first Game init, after the new-game wipe), --ctl-flag MASK (OR into the player-control word
//          0x50b620 at that point, e.g. 0x100000 = killed by C10A -> E53 on floor 95), --start-scene NAME (8-char scene name,
//          e.g. "Option..", instead of "BootMC.."), --event N (event E%02d via FUN_001d1d30 on the first Game tick),
//          --run-end TICK (the HP-0 run end FUN_0011ce60 on that tick: post-run menu, save, rebirth),
//          --kill TICK (player HP set to 0 on that tick: the real death state DD01 -> FUN_001cd9d0 -> FUN_001cdcd0),
//          --status MASK TICK (status bits MASK inflicted on the player on that tick through FUN_00138660, e.g. 0x10 Lust),
//          --warp-to X Y Z TICK (player moved to X Y Z on that tick, e.g. next to an exit object).
//          PORT test options: --stick LX LY FROM TO (left stick raw values on ticks FROM..TO-1, repeatable),
//          --hold MASK FROM TO (pad bits held on ticks FROM..TO-1, repeatable; e.g. 0x800 = hold-to-skip test),
//          --shot-also PATH TICK (extra capture at TICK in the same --shot run, repeatable),
//          --mouse-look DX DY TICK (captured-cursor mouse motion in window pixels after tick TICK, repeatable),
//          --free-camera (the PORT free mouse camera in a --shot run; implied by --mouse-look, else the original one),
//          --walk-to X Z FROM TO (stick steers the player toward world X,Z on ticks FROM..TO-1, repeatable),
//          --walk-exit FROM TO (same, along a cell BFS path to the floor's PT00 / PT14 exit),
//          --keep-hp (player HP refilled + invincible status bit 0x10000 held; route tests on deep floors),
//          --hp-max N (base max HP and HP set to N once the player exists; HUD gauge tests).
//          PORT dev option: --give ID (repeatable; 4-char item id, e.g. SD05, added to the inventory on the first Game
//          tick), --give-identified (the --give items start identified).
//          PORT (dev): --restore-state PATH (one-time inventory / stats / position edits applied in memory on top of a
//          normally loaded game; never writes the save file; directives in devApplyRestoreState).
//          PORT determinism tests (PRESENTATION_PLAN.md I5): --present-per-tick K (--shot mode: after every tick present
//          K discarded frames at alpha k/K, k = 1..K, into an off-screen target), --logic-hash PATH (one line per tick:
//          tick, FNV-1a hash of every Chr pos/axes/scale/hp/act/actor combiner states + RNG state + event frame).
//          PORT presentation tests: --shot-alpha A (the --shot / --shot-also captures are presented at alpha A between
//          the last two ticks instead of 1), --aspect W H (capture size, default 1440 1080), --port-set KEY=VALUE
//          (repeatable; overrides a baroque_port.ini setting for this run, e.g. hud=classic, max_aspect=1.7778).
//          PORT settings file <exe>/baroque_port.ini (game/present/present_config.hpp): fps cap, interpolation, HUD
//          anchoring, max aspect. Written with the defaults on the first windowed run.
//          PORT packaging (platform/app_host.*, work/reports/PACKAGING.md): every run writes <exe>/baroque.log (previous
//          run -> baroque.old.log). Baroque.exe (GUI copy of Baroque-Native.exe) attaches to the parent console for
//          "--" options when its std handles are not redirected; --console opens a console window. Missing
//          <exe>/assets or <exe>/disc -> first-start setup from an .iso next to the EXE / dropped on it;
//          --setup-data PATH.iso extracts and exits.
//          PORT window focus tests (work/reports/PC_SETTINGS.md "Window focus"): --simulate-unfocus TICK FRAMES (the
//          window loses the focus at tick TICK for FRAMES host frames, then gets it back and a click inside follows 15
//          frames later; the same handler as a live focus change, and the mouse-look capture runs as in a live game),
//          --simulate-minimize TICK FRAMES (the same, minimised: nothing is presented), --window-test TICK (from tick
//          TICK: borderless, windowed, resize 960x540 and back on the hidden window; flags logged to baroque.log).
#include "audio/sound_system.hpp"
#include "game/character/ai.hpp"
#include "game/character/character.hpp"
#include "game/character/combat.hpp"
#include "game/effects/effects.hpp"
#include "game/effects/effects_gl.hpp"
#include "game/engine.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/event_sound.hpp"
#include "game/flag_manager.hpp"
#include "game/free_camera.hpp"    // PORT (dev): --restore-state camera yaw
#include "game/game_world.hpp"
#include "game/movie_service.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/camera.hpp"        // PORT: --walk-to reads the camera basis
#include "game/player_move.hpp"   // PORT: --walk-to (PlayerControl::camera)
#include "game/dungeon.hpp"       // PORT: --walk-exit (cells, exit placement)
#include "game/ui/message_box.hpp" // PORT: --walk-to / --walk-exit pause while a message window is open
#include "game/hud/hud_api.hpp"
#include "game/present/present_config.hpp"
#include "game/present/present_layout.hpp"
#include "game/render2d.hpp"
#include "game/save_manager.hpp"
#include "game/scene_manager.hpp"
#include "game/scenes/front_scenes.hpp"
#include "game/scenes/game_scene.hpp"
#include "game/sound_listener.hpp"
#include "game/talk/talk_wiring.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include "platform/app_host.hpp"   // PORT (packaging): log file, console attach, first-start data setup
#include "platform/controls.hpp"
#include "platform/input.hpp"
#include "platform/platform.hpp"
#include "video/movie_player.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace baroque;

namespace {

constexpr double kTickSeconds = 1001.0 / 30000.0;   // 29.97 Hz (one displayed frame of the original)

void present(int w, int h, float alpha) {
    ClearBackground(BLACK);
    if (w <= 0 || h <= 0) return;   // PORT (window focus): minimised window (0x0 framebuffer): no targets of size 0
    if (const video::MoviePlayer* m = game::movieActive()) {
        m->draw(w, h);
        return;
    }
    if (game::gameRender3D(w, h, alpha)) {   // 3-D pass when the Game scene is active (FUN_001075a0)
        game::hudDrawWindowOverlay(w, h, alpha);   // PORT: keyboard + mouse gun reticle (game/hud/gun_reticle.cpp)
        game::renderer2d().drawPillars(w, h);   // PORT: max-aspect cap (the 3-D pass covers the whole window)
    }
    game::renderer2d().drawSprites(w, h);
}

// PORT test option --logic-hash: FNV-1a 64 over the logic state of one tick (PRESENTATION_PLAN.md I5). The
// presentation must not change any of it, whatever the number of presented frames per tick.
struct LogicHash {
    uint64_t h = 0xcbf29ce484222325ull;
    void bytes(const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x100000001b3ull; }
    }
    template <class T> void v(const T& x) { bytes(&x, sizeof x); }
};
// anim: the combiner states the ACT logic reads; cache: the evaluation cache words (+0x48 / +0x50) and _new
void hashActor(LogicHash& lh, LogicHash& cache, const game::ActorAnim& a) {
    lh.v(a.currentClip());
    lh.v(a.currentTimeNs());
    lh.v(a.speed());
    cache.v(a.combiner().cacheTime());
    cache.v(a.combiner().cacheValid());
    for (const auto& s : a.combiner().states()) {
        cache.v(s->isNew);
        lh.v(s->clip); lh.v(s->combineMode); lh.v(s->transitionMode); lh.v(s->status);
        lh.v(s->blendRatio); lh.v(s->localTime); lh.v(s->timeScale); lh.v(s->timeBias); lh.v(s->blendStartTime);
        lh.v(s->blendStartRatio); lh.v(s->blendRatioRange); lh.v(s->blendDuration); lh.v(s->animationStartTime);
    }
}
void writeLogicHash(std::FILE* f) {
    LogicHash all, anim, cache;
    int n = 0;
    for (const game::Chr* c : game::characters().list()) {
        ++n;
        all.v(c->id); all.v(c->type); all.v(c->taskFlags);
        all.v(c->xf.scale); all.v(c->xf.m.pos); all.v(c->xf.m.right); all.v(c->xf.m.up); all.v(c->xf.m.fwd);
        all.v(c->status.block.hp);
        const char* act = c->act.currentName();
        if (act != nullptr) all.bytes(act, std::strlen(act));
        all.v(c->act.flags());
        hashActor(anim, cache, c->model.anim);
        for (const game::ChrModelPart& p : c->model.parts)
            if (p.actor) hashActor(anim, cache, p.actor->anim);
    }
    const uint64_t body = all.h;   // ids, transforms, hp, actions
    all.v(anim.h);
    all.v(cache.h);
    const uint32_t rng = game::gameWorld().rng.state;
    all.v(rng);
    const int32_t evFrame = game::eventActive() ? game::eventManager().frame : -1;
    all.v(evFrame);
    std::fprintf(f, "%llu %016llx body=%016llx anim=%016llx cache=%016llx rng=%08x ev=%d chr=%d\n",
                 static_cast<unsigned long long>(game::engine().tickCount), static_cast<unsigned long long>(all.h),
                 static_cast<unsigned long long>(body), static_cast<unsigned long long>(anim.h),
                 static_cast<unsigned long long>(cache.h), rng, evFrame, n);
}

// PORT test option --walk-exit: the world point to steer toward this tick on the way to the floor's exit placement
// (Dungeon placement of kind Stairs = the PT00 / PT14 stage object). BFS over the 200-unit world cells: non-void
// cells, blocked-door pieces (object ids 15..18, FUN_00125fc0) are walls. Verification only, no game state touched.
bool walkExitTarget(float* outX, float* outZ) {
    game::Dungeon* d = game::gameWorld().dungeon;
    const game::Chr* p = game::characters().player();
    if (d == nullptr || p == nullptr) return false;
    const game::Placement* exit = nullptr;
    for (const game::Placement& pl : d->placements())
        if (pl.kind == game::Placement::Kind::Stairs) exit = &pl;
    if (exit == nullptr) return false;
    const int w = d->context().maxX + 2, h = d->context().maxY + 2;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
    auto open = [&](int gx, int gz) {
        if (gx < 0 || gz < 0 || gx >= w || gz >= h) return false;
        const float pos[3] = {float(gx) * 200.0f + 100.0f, 0.0f, float(gz) * 200.0f + 100.0f};
        int cx = 0, cy = 0;
        const uint32_t* c = d->cellAtWorld(pos, &cx, &cy);
        if (c == nullptr || (*c & 0xf000u) == 0) return false;
        return !((*c & 0xf000u) == 0x2000u && (*c & 0xffu) >= 15u);
    };
    auto level = [&](int gx, int gz) -> int {   // -1 void, else 0 ground / 1 upper (cell bits 8-11)
        if (!open(gx, gz)) return -1;
        const float pos[3] = {float(gx) * 200.0f + 100.0f, 0.0f, float(gz) * 200.0f + 100.0f};
        int cx = 0, cy = 0;
        const uint32_t* c = d->cellAtWorld(pos, &cx, &cy);
        return (*c & 0xf00u) != 0 ? 1 : 0;
    };
    // a level change is a ramp: the ground cell L next to the upper cell H continues to ground on the far side and
    // has no ground cell beside it (the ramp is walled by the upper floor or void laterally)
    auto passable = [&](int ax, int az, int bx, int bz) {
        const int la = level(ax, az), lb = level(bx, bz);
        if (la < 0 || lb < 0) return false;
        if (la == lb) return true;
        const int lx = la == 0 ? ax : bx, lz = la == 0 ? az : bz;
        const int hx = la == 0 ? bx : ax, hz = la == 0 ? bz : az;
        const int ddx = hx - lx, ddz = hz - lz;
        return level(lx - ddx, lz - ddz) == 0 && level(lx + ddz, lz + ddx) != 0 && level(lx - ddz, lz - ddx) != 0;
    };
    const int sx = int(p->xf.m.pos[0] / 200.0f), sz = int(p->xf.m.pos[2] / 200.0f);
    const int tx = int(exit->x / 200.0f), tz = int(exit->z / 200.0f);
    static const game::Dungeon* dumped = nullptr;   // one cell map per floor on stderr (verification aid)
    if (dumped != d) {
        dumped = d;
        std::fprintf(stderr, "walk-exit map %dx%d (. floor ^ upper, digit = attr bits 4-7, o/O piece, D blocked, S exit, P player)\n", w, h);
        for (int gz = 0; gz < h; ++gz) {
            std::string row;
            for (int gx = 0; gx < w; ++gx) {
                const float pos[3] = {float(gx) * 200.0f + 100.0f, 0.0f, float(gz) * 200.0f + 100.0f};
                int cx = 0, cy = 0;
                const uint32_t* c = d->cellAtWorld(pos, &cx, &cy);
                char ch = ' ';
                if (gx == tx && gz == tz) ch = 'S';
                else if (gx == sx && gz == sz) ch = 'P';
                else if (c != nullptr && (*c & 0xf000u) == 0x1000u)
                    ch = (*c & 0xf0u) != 0 ? char('0' + ((*c >> 4) & 0xf)) : ((*c & 0xf00u) ? '^' : '.');
                else if (c != nullptr && (*c & 0xf000u) == 0x2000u)
                    ch = (*c & 0xffu) >= 15u ? 'D' : ((*c & 0xf00u) ? 'O' : 'o');
                row += ch;
            }
            std::fprintf(stderr, "%3d %s\n", gz, row.c_str());
        }
    }
    if (sx == tx && sz == tz) { *outX = exit->x; *outZ = exit->z; return true; }
    std::vector<int> prev(size_t(w) * size_t(h), -1);
    std::vector<int> q;
    const int start = sz * w + sx, goal = tz * w + tx;
    if (start < 0 || start >= w * h || goal < 0 || goal >= w * h) return false;
    prev[size_t(start)] = start;
    q.push_back(start);
    for (size_t qi = 0; qi < q.size() && prev[size_t(goal)] < 0; ++qi) {
        const int cur = q[qi], cx = cur % w, cz = cur / w;
        const int ddx[4] = {0, 1, 0, -1}, ddz[4] = {-1, 0, 1, 0};
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + ddx[k], nz = cz + ddz[k];
            if (!(nx == tx && nz == tz) && !passable(cx, cz, nx, nz)) continue;
            const int n = nz * w + nx;
            if (prev[size_t(n)] >= 0) continue;
            prev[size_t(n)] = cur;
            q.push_back(n);
        }
    }
    if (prev[size_t(goal)] < 0) { *outX = exit->x; *outZ = exit->z; return true; }   // no path: straight line
    int step = goal;
    while (prev[size_t(step)] != start) step = prev[size_t(step)];
    if (step == goal) { *outX = exit->x; *outZ = exit->z; return true; }
    *outX = float(step % w) * 200.0f + 100.0f;
    *outZ = float(step / w) * 200.0f + 100.0f;
    return true;
}

// PORT (dev): --restore-state PATH. One-time edits applied on top of a normally loaded game (e.g. FILE SELECT ->
// Continue -> file 2), to recreate a play state that was never saved. Nothing is written to the save file: the
// edits live in memory only until the player saves through the game's own menus. Text file, one directive per line
// ('#' comments):
//   floor N            apply only once the Game runs on floor id N (default: the first Game floor)
//   remove ID          remove the first unequipped inventory item with that 4-char id
//   give ID [COUNT]    add an identified item (optionally setting its count / charges)
//   count ID N         set the count of the first inventory item with that id
//   vt full | hp full  refill VT / HP to their maximum
//   exp N              set the experience points
//   start hole-walkway place the player at the start of the walkway in the room of kind ' ' (no-stairs floors)
// Returns true once applied (or on a file error: logged, not retried).
bool devApplyRestoreState(const char* path) {
    using namespace baroque;
    static std::vector<std::string> lines;
    static bool loaded = false;
    static int floorWanted = -1;
    static int settle = 0;
    if (!loaded) {
        loaded = true;
        std::ifstream f(path);
        if (!f) { platform::log(std::string("--restore-state: cannot open ") + path); return true; }
        std::string s;
        while (std::getline(f, s)) {
            if (const size_t h = s.find('#'); h != std::string::npos) s.resize(h);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
            size_t b = 0;
            while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
            if (b < s.size()) lines.push_back(s.substr(b));
        }
        for (const std::string& l : lines)
            if (l.rfind("floor ", 0) == 0) floorWanted = std::atoi(l.c_str() + 6);
    }
    game::Chr* p = game::characters().player();
    if (p == nullptr || p->status.block.hp <= 0) return false;
    if (floorWanted >= 0 && game::hudWorld().floorId != floorWanted) return false;
    if (++settle < 30) return false;   // let the floor init (inventory restore, equipment) finish first
    auto tagOf = [](const char* s) {
        return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24;
    };
    game::Inventory& inv = game::inventory();
    game::ItemSystem& sys = game::itemSystem();
    auto find = [&](uint32_t id, bool unequippedOnly) {
        for (int i = 0; i < inv.count; ++i)
            if (inv.slots[i] != nullptr && inv.slots[i]->id == id && (!unequippedOnly || inv.equippedSlotOfIndex(i) == -1)) return i;
        return -1;
    };
    for (const std::string& l : lines) {
        std::istringstream ls(l);
        std::string c, a;
        int n = -1;
        int got = 0;
        if (ls >> c) ++got;
        if (ls >> a) ++got;
        if (ls >> n) ++got;
        const char* arg = a.c_str();
        bool ok = true;
        if (c == "floor") {
        } else if (c == "remove" && a.size() == 4) {
            const int i = find(tagOf(arg), true);
            game::Item* it = i >= 0 ? inv.remove(i, sys) : nullptr;
            if (it != nullptr) game::gameAdapter().freeItem(it);
            ok = it != nullptr;
        } else if (c == "give" && a.size() == 4) {
            ok = game::hudDevGiveItem(tagOf(arg), true);
            if (ok && got == 3 && inv.count > 0) inv.slots[inv.count - 1]->count = n;
        } else if (c == "count" && a.size() == 4 && got == 3) {
            const int i = find(tagOf(arg), false);
            if (i >= 0) inv.slots[i]->count = n;
            ok = i >= 0;
        } else if (c == "vt" && a == "full") {
            p->status.block.vt = p->status.block.vtMax;
            game::hudSetVt(p->status.block.vt);
        } else if (c == "hp" && a == "full") {
            p->status.block.hp = p->status.block.hpMax;
            game::hudSetHp(p->status.block.hp);
        } else if (c == "exp" && got >= 2) {
            p->status.block.exp = std::atoi(arg);
        } else if (c == "start" && a == "hole-walkway") {
            ok = false;
            if (const game::Dungeon* d = game::gameWorld().dungeon)
                for (int r = 0; r < d->roomCount(); ++r)
                    if (d->room(r).rdt.roomId == 0x20) {
                        p->xf.m.pos[0] = float(int(d->room(r).posX) * 0x640 + 100) + 1200.0f;
                        p->xf.m.pos[2] = float(int(d->room(r).posY) * 0x640 + 100) + 1800.0f;
                        game::freeCamera().yaw = 0.0f;   // free mouse camera behind, looking down the walkway (-z)
                        ok = true;
                        break;
                    }
        } else {
            ok = false;
        }
        platform::log("--restore-state: " + l + (ok ? "" : "  (FAILED)"));
    }
    inv.refreshFlags(sys);
    std::string dump = "--restore-state: inventory";
    for (int i = 0; i < inv.count; ++i) {
        const uint32_t id = inv.slots[i]->id;
        dump += ' ';
        for (int k = 0; k < 4; ++k) dump += char((id >> (8 * k)) & 0xff);
        if (inv.equippedSlotOfIndex(i) != -1) dump += '*';
        dump += "x" + std::to_string(inv.slots[i]->count);
    }
    platform::log(dump + ", HP " + std::to_string(p->status.block.hp) + "/" + std::to_string(p->status.block.hpMax) + ", VT " +
                  std::to_string(p->status.block.vt) + "/" + std::to_string(p->status.block.vtMax) + ", level " +
                  std::to_string(p->status.block.level) + ", exp " + std::to_string(p->status.block.exp));
    return true;
}

}  // namespace

static bool gShotWindow = false;   // PORT: --shot-window
static long gShotWindowMs = -1;    // PORT: --shot-window-ms (capture after wall-clock ms; movies freeze the tick count)

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--toolchain-check") == 0) {
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(640, 448, "Baroque native host");
        std::printf("toolchain ok\n");
        CloseWindow();
        return 0;
    }
    {
        int exitCode = 0;   // PORT (packaging): baroque.log, console for "--" options, data setup (PACKAGING.md)
        if (!platform::initApp(argc, argv, &exitCode)) return exitCode;
    }
    const char* shotPath = nullptr;
    long shotTicks = 0;
    long pressStartAt = -1;
    struct ScriptedPress { uint32_t mask; long tick; } scripted[64] = {};
    int scriptedCount = 0;
    struct ScriptedHold { uint32_t mask; long from, to; } holdScript[16] = {};   // PORT: --hold MASK FROM TO (test option)
    int holdScriptCount = 0;
    int devFloor = -1;   // --floor N: port-only developer start straight into the Game scene
    long long devSeed = -1;
    // PORT: scripted mouse events for --shot verification of the mouse menu layer (kind 0 move, 1 left, 2 right, 3 wheel)
    struct ScriptedMouse { int kind; float x, y; long tick; } mouseScript[512] = {};
    int mouseScriptCount = 0;
    int menuAtMode = -1;
    long menuAtTick = -1;
    std::vector<uint32_t> devFlags;          // PORT: --flag N
    static uint32_t devCtlFlags = 0;         // PORT: --ctl-flag MASK (player-control word 0x50b620 bits, e.g. 0x100000 -> E53)
    const char* devStartScene = nullptr;     // PORT: --start-scene NAME
    int devEvent = -1;                       // PORT: --event N (FUN_001d1d30(N) once the Game scene runs)
    long devRunEnd = -1;                     // PORT: --run-end TICK (the HP-0 run end FUN_0011ce60 on that tick)
    long devKill = -1;                       // PORT: --kill TICK (player HP = 0 on that tick -> the real death state)
    struct DevStatus { uint32_t mask; long tick; } devStatus{0, -1};   // PORT: --status MASK TICK (FUN_00138660 on the player)
    struct DevWarp { float x, y, z; long tick; } devWarp{0.0f, 0.0f, 0.0f, -1};   // PORT: --warp-to X Y Z TICK
    bool devKeepHp = false;                  // PORT: --keep-hp (test: HP refilled, invincible bit 0x10000 held)
    int devHpMax = -1;                       // PORT: --hp-max N (test: base max HP and HP = N once the player exists)
    std::vector<uint32_t> devGive;           // PORT: --give ID (repeatable)
    bool devGiveIdentified = false;          // PORT: --give-identified
    const char* devRestoreState = nullptr;   // PORT (dev): --restore-state PATH (applied once, see devApplyRestoreState)
    // PORT: --stick LX LY FROM TO (repeatable): scripted left stick raw values (0..255, 128 = centre) on ticks
    // FROM..TO-1; --shot-also PATH TICK (repeatable): an extra capture at TICK in the same --shot run
    struct ScriptedStick { int lx, ly; long from, to; } stickScript[32] = {};
    int stickScriptCount = 0;
    struct ExtraShot { const char* path; long tick; } extraShots[256] = {};
    int extraShotCount = 0;
    // PORT: --mouse-look DX DY TICK (repeatable): a captured-cursor mouse motion of DX, DY window pixels after tick TICK
    // (through platform::mouseLookDegrees -> game::gameMouseLook, as the live mouse-look block below)
    struct ScriptedLook { float dx, dy; long tick, to; } lookScript[32] = {};   // to: --mouse-look-span end (exclusive)
    int lookScriptCount = 0;
    struct ScriptedDevice { bool gamepad; long tick; } deviceScript[16] = {};
    int deviceScriptCount = 0;
    bool arrowCursor = false;   // PORT (media captures): --arrow-cursor
    // PORT: --walk-to X Z FROM TO (repeatable): on ticks FROM..TO-1 the left stick steers the player toward world
    // (X, Z) through the current camera basis (inverse of the FUN_0013cf90 stick mapping); released within 12 units
    struct ScriptedWalk { float x, z; long from, to; bool exit; } walkScript[32] = {};   // exit: --walk-exit FROM TO
    int walkScriptCount = 0;
    int presentPerTick = 0;                  // PORT: --present-per-tick K (determinism test)
    const char* logicHashPath = nullptr;     // PORT: --logic-hash PATH (determinism test)
    float shotAlpha = 1.0f;                  // PORT: --shot-alpha A
    int shotW = 1440, shotH = 1080;          // PORT: --aspect W H (capture size)
    std::vector<const char*> portSets;       // PORT: --port-set KEY=VALUE
    // PORT (window focus) test options: --simulate-unfocus / --simulate-minimize TICK FRAMES, --window-test TICK
    struct SimFocus { long tick = -1, frames = 0; bool minimize = false; int phase = 0; long left = 0; } simFocus;
    long windowTestTick = -1;
    for (int i = 1; i < argc; ++i) {
        if ((std::strcmp(argv[i], "--simulate-unfocus") == 0 || std::strcmp(argv[i], "--simulate-minimize") == 0) && i + 2 < argc) {
            simFocus.tick = std::atol(argv[i + 1]);
            simFocus.frames = std::max(1L, std::atol(argv[i + 2]));
            simFocus.minimize = std::strcmp(argv[i], "--simulate-minimize") == 0;
        }
        if (std::strcmp(argv[i], "--window-test") == 0 && i + 1 < argc) windowTestTick = std::atol(argv[i + 1]);
        if (std::strcmp(argv[i], "--shot-alpha") == 0 && i + 1 < argc) shotAlpha = float(std::atof(argv[i + 1]));
        if (std::strcmp(argv[i], "--aspect") == 0 && i + 2 < argc) {
            shotW = std::max(16, std::atoi(argv[i + 1]));
            shotH = std::max(16, std::atoi(argv[i + 2]));
        }
        if (std::strcmp(argv[i], "--port-set") == 0 && i + 1 < argc) portSets.push_back(argv[i + 1]);
        if (std::strcmp(argv[i], "--present-per-tick") == 0 && i + 1 < argc) presentPerTick = std::atoi(argv[i + 1]);
        if (std::strcmp(argv[i], "--logic-hash") == 0 && i + 1 < argc) logicHashPath = argv[i + 1];
        if (std::strcmp(argv[i], "--shot") == 0 && i + 2 < argc) {
            shotPath = argv[i + 1];
            shotTicks = std::atol(argv[i + 2]);
        }
        // PORT test: like --shot, but the hidden window gets the normal monitor-fitted size and the frame is drawn through
        // the real BeginDrawing/present path and read back from the window's framebuffer (verifies window/DPI handling)
        if (std::strcmp(argv[i], "--shot-window-ms") == 0 && i + 2 < argc) {
            shotPath = argv[i + 1];
            gShotWindowMs = std::atol(argv[i + 2]);
            shotTicks = 0x7fffffff;
            gShotWindow = true;
        }
        if (std::strcmp(argv[i], "--shot-window") == 0 && i + 2 < argc) {
            shotPath = argv[i + 1];
            shotTicks = std::atol(argv[i + 2]);
            gShotWindow = true;
        }
        if (std::strcmp(argv[i], "--press-start-at") == 0 && i + 1 < argc) pressStartAt = std::atol(argv[i + 1]);
        if (std::strcmp(argv[i], "--stick") == 0 && i + 4 < argc && stickScriptCount < 32) {   // PORT: test option
            stickScript[stickScriptCount++] = {std::atoi(argv[i + 1]), std::atoi(argv[i + 2]), std::atol(argv[i + 3]), std::atol(argv[i + 4])};
        }
        if (std::strcmp(argv[i], "--walk-to") == 0 && i + 4 < argc && walkScriptCount < 32) {   // PORT: test option
            walkScript[walkScriptCount++] = {float(std::atof(argv[i + 1])), float(std::atof(argv[i + 2])),
                                             std::atol(argv[i + 3]), std::atol(argv[i + 4]), false};
        }
        if (std::strcmp(argv[i], "--walk-exit") == 0 && i + 2 < argc && walkScriptCount < 32) {  // PORT: test option
            walkScript[walkScriptCount++] = {0.0f, 0.0f, std::atol(argv[i + 1]), std::atol(argv[i + 2]), true};
        }
        if (std::strcmp(argv[i], "--shot-also") == 0 && i + 2 < argc && extraShotCount < 256) {   // PORT: test option
            extraShots[extraShotCount++] = {argv[i + 1], std::atol(argv[i + 2])};
        }
        if (std::strcmp(argv[i], "--mouse-look") == 0 && i + 3 < argc && lookScriptCount < 32) {   // PORT: test option
            lookScript[lookScriptCount++] = {float(std::atof(argv[i + 1])), float(std::atof(argv[i + 2])), std::atol(argv[i + 3]), std::atol(argv[i + 3]) + 1};
        }
        // PORT (media captures): --mouse-look-span DX DY FROM TO = the same motion on every tick FROM..TO-1
        if (std::strcmp(argv[i], "--mouse-look-span") == 0 && i + 4 < argc && lookScriptCount < 32) {
            lookScript[lookScriptCount++] = {float(std::atof(argv[i + 1])), float(std::atof(argv[i + 2])), std::atol(argv[i + 3]), std::atol(argv[i + 4])};
        }
        if (std::strcmp(argv[i], "--arrow-cursor") == 0) arrowCursor = true;
        // PORT (media captures): --input-device gamepad|keyboard TICK = the last-used input device from tick TICK
        if (std::strcmp(argv[i], "--input-device") == 0 && i + 2 < argc && deviceScriptCount < 16) {
            deviceScript[deviceScriptCount++] = {std::strcmp(argv[i + 1], "gamepad") == 0, std::atol(argv[i + 2])};
        }
        // --press MASK TICK (repeatable): scripted pad bits (SCE_PAD layout, hex or decimal) on tick TICK
        if (std::strcmp(argv[i], "--hold") == 0 && i + 3 < argc && holdScriptCount < 16) {   // PORT: held pad bits on ticks FROM..TO-1
            holdScript[holdScriptCount++] = {static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 0)), std::atol(argv[i + 2]), std::atol(argv[i + 3])};
        }
        if (std::strcmp(argv[i], "--press") == 0 && i + 2 < argc && scriptedCount < 64) {
            scripted[scriptedCount].mask = static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 0));
            scripted[scriptedCount].tick = std::atol(argv[i + 2]);
            ++scriptedCount;
        }
        if (std::strcmp(argv[i], "--floor") == 0 && i + 1 < argc) devFloor = std::atoi(argv[i + 1]);
        if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) devSeed = std::strtoll(argv[i + 1], nullptr, 0);   // PORT: fixed RNG seed
        if (std::strcmp(argv[i], "--flag") == 0 && i + 1 < argc) devFlags.push_back(uint32_t(std::strtoul(argv[i + 1], nullptr, 0)));
        if (std::strcmp(argv[i], "--clear-flag") == 0 && i + 1 < argc)   // PORT: dev option (bit 31 = clear)
            devFlags.push_back(0x80000000u | uint32_t(std::strtoul(argv[i + 1], nullptr, 0)));
        if (std::strcmp(argv[i], "--ctl-flag") == 0 && i + 1 < argc)     // PORT: dev option, OR into uRam0050b620
            devCtlFlags |= uint32_t(std::strtoul(argv[i + 1], nullptr, 0));
        if (std::strcmp(argv[i], "--start-scene") == 0 && i + 1 < argc && std::strlen(argv[i + 1]) == 8) devStartScene = argv[i + 1];
        if (std::strcmp(argv[i], "--event") == 0 && i + 1 < argc) devEvent = std::atoi(argv[i + 1]);   // PORT: dev option
        if (std::strcmp(argv[i], "--run-end") == 0 && i + 1 < argc) devRunEnd = std::atol(argv[i + 1]); // PORT: dev option
        if (std::strcmp(argv[i], "--kill") == 0 && i + 1 < argc) devKill = std::atol(argv[i + 1]);      // PORT: dev option
        if (std::strcmp(argv[i], "--status") == 0 && i + 2 < argc)                                       // PORT: dev option
            devStatus = {static_cast<uint32_t>(std::strtoul(argv[i + 1], nullptr, 0)), std::atol(argv[i + 2])};
        if (std::strcmp(argv[i], "--warp-to") == 0 && i + 4 < argc)                                      // PORT: dev option
            devWarp = {float(std::atof(argv[i + 1])), float(std::atof(argv[i + 2])), float(std::atof(argv[i + 3])), std::atol(argv[i + 4])};
        if (std::strcmp(argv[i], "--keep-hp") == 0) devKeepHp = true;                                   // PORT: test option
        if (std::strcmp(argv[i], "--hp-max") == 0 && i + 1 < argc) devHpMax = std::atoi(argv[i + 1]);  // PORT: test option
        if (std::strcmp(argv[i], "--give") == 0 && i + 1 < argc && std::strlen(argv[i + 1]) == 4) {   // PORT: dev option
            const unsigned char* c = reinterpret_cast<const unsigned char*>(argv[i + 1]);
            devGive.push_back(uint32_t(c[0]) | uint32_t(c[1]) << 8 | uint32_t(c[2]) << 16 | uint32_t(c[3]) << 24);
        }
        if (std::strcmp(argv[i], "--give-identified") == 0) devGiveIdentified = true;   // PORT: dev option
        if (std::strcmp(argv[i], "--restore-state") == 0 && i + 1 < argc) devRestoreState = argv[i + 1];   // PORT (dev)
        if (std::strcmp(argv[i], "--menu-at") == 0 && i + 2 < argc) {   // PORT: test option (see the header comment)
            menuAtMode = std::atoi(argv[i + 1]);
            menuAtTick = std::atol(argv[i + 2]);
        }
        if (mouseScriptCount < 512) {   // PORT: mouse-menu test options (see the header comment)
            ScriptedMouse& m = mouseScript[mouseScriptCount];
            const bool move = std::strcmp(argv[i], "--mouse-at") == 0, click = std::strcmp(argv[i], "--click-at") == 0;
            if ((move || click) && i + 3 < argc) {
                m = {click ? 1 : 0, float(std::atof(argv[i + 1])), float(std::atof(argv[i + 2])), std::atol(argv[i + 3])};
                ++mouseScriptCount;
            } else if (std::strcmp(argv[i], "--rclick-at") == 0 && i + 1 < argc) {
                m = {2, 0.0f, 0.0f, std::atol(argv[i + 1])};
                ++mouseScriptCount;
            } else if (std::strcmp(argv[i], "--wheel-at") == 0 && i + 2 < argc) {
                m = {3, float(std::atof(argv[i + 1])), 0.0f, std::atol(argv[i + 2])};
                ++mouseScriptCount;
            }
        }
    }
    // PORT: free mouse camera in --shot runs: off (original camera, scripted pad tests unchanged) unless --free-camera
    // or a --mouse-look is given
    if (shotPath != nullptr) {
        bool freeCam = lookScriptCount > 0;
        for (int i = 1; i < argc; ++i) freeCam = freeCam || std::strcmp(argv[i], "--free-camera") == 0;
        platform::setFreeCameraOverride(freeCam ? 1 : 2);
    }

    SetTraceLogLevel(LOG_WARNING);
    // PORT: presentation settings <exe>/baroque_port.ini (fps cap, interpolation, HUD anchoring, max aspect); the
    // defaults file is written on the first windowed run. --port-set overrides single values for this run.
    game::present::loadPortSettings(fs::path(GetApplicationDirectory()) / "baroque_port.ini", shotPath == nullptr);
    for (const char* kv : portSets) {
        const char* eq = std::strchr(kv, '=');
        if (eq == nullptr || !game::present::applyPortSetting(std::string_view(kv, size_t(eq - kv)), eq + 1))
            platform::log(std::string("--port-set: ignored ") + kv);
    }
    const game::present::PortSettings& portCfg = game::present::portSettings();
    game::setScreenLayoutProvider(&game::present::presentLayout);   // PORT widescreen layout of the 3-D / 2-D passes
    // PORT: --shot runs headless (hidden window) so scripted verification never pops a window on the desktop
    const bool vsync = shotPath == nullptr && game::present::wantsVsync(portCfg);
    // PORT: no FLAG_WINDOW_HIGHDPI. With it, raylib scales its own draw calls by the monitor content scale
    // (screenScale) while our passes work in framebuffer pixels, so at 200 % Windows scaling the layers no longer line
    // up. Without it GLFW (per-monitor DPI aware) gives a window whose size == framebuffer size, in physical pixels.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | (vsync ? FLAG_VSYNC_HINT : 0u) | FLAG_MSAA_4X_HINT |
                   (shotPath != nullptr ? FLAG_WINDOW_HIDDEN : 0u));
    InitWindow(1440, 1080, "BAROQUE");
    // PORT (window focus): raylib would block inside EndDrawing while minimised (glfwWaitEvents), so the loop never saw
    // the focus loss; main.cpp pauses and stops presenting itself instead (PC_SETTINGS.md "Window focus")
    if (IsWindowReady()) SetWindowState(FLAG_WINDOW_ALWAYS_RUN);
    if (!IsWindowReady()) {   // PORT (packaging): no OpenGL 3.3 context (driver / remote desktop) -> tell the player
        platform::showError("Baroque", "Could not open the game window (OpenGL 3.3 is required)."
                            " Please update your graphics driver. Details are in baroque.log.", platform::playerRun());
        return 1;
    }
    if (shotPath == nullptr || gShotWindow) {
        // PORT: initial window fits the monitor (largest window of the configured window_aspect, clamped to
        // [4:3, max_aspect], within ~85 % of its height / 90 % of its width), centred; --shot keeps the fixed
        // 1440x1080 capture size.
        const int mon = GetCurrentMonitor();
        const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
        if (mw > 0 && mh > 0) {
            const float wa = std::clamp(portCfg.windowAspect, 4.0f / 3.0f, std::max(portCfg.maxAspect, 4.0f / 3.0f));
            int h = std::min(1080, mh * 85 / 100);
            int w = int(float(h) * wa + 0.5f);
            if (w > mw * 9 / 10) { w = mw * 9 / 10; h = int(float(w) / wa + 0.5f); }
            SetWindowSize(w, h);
            const Vector2 mp = GetMonitorPosition(mon);
            SetWindowPosition(int(mp.x) + (mw - w) / 2, int(mp.y) + (mh - h) / 2);
        }
    }
    if (shotPath == nullptr) SetTargetFPS(game::present::fpsTarget(portCfg));   // PORT: fps cap (0 = vsync / uncapped)
    if (shotPath == nullptr) game::present::applyPortDisplay();   // PORT: window = borderless (PC SETTINGS page)
    SetExitKey(KEY_NULL);   // Esc is not a PS2 button; close the window to quit
    // PORT: mouse menu layer. Unscaled cursor = framebuffer pixels (Windows: GLFW window coordinates are pixels), the
    // space present() lays the 4:3 picture out in; the layer's pad bits join every pad sample through the poll hook.
    SetMouseScale(1.0f, 1.0f);
    platform::setPollHook(&game::ui::mouse::padPoll);
    InitAudioDevice();
    audio::setEnglishVoiceArchive(portCfg.englishVoice);   // PORT (voice language): SOUND_EN_DUB when set up
    // FUN_00114f30(1,1,1): sound manager 0x4e9790 over BRQSOUND.SFS; ticked by the engine (FUN_001150b0)
    if (audio::SoundSystem* snd = audio::installSoundSystem(true)) {
        game::engine().soundFrame = [snd] { snd->tick(); };
        game::eventBindSoundSystem(snd);                       // event manager sound entries (FUN_00115470 ..)
        game::soundListenerInstall(snd);                       // FUN_001150b0 listener (camera / event) + area test
    }

    // FUN_00100730 subsystem init (front-end subset)
    game::ui::setLanguage(0);                                  // SLUS (EN) build: uRam004e9768 = 0
    game::ui::textures().init();
    game::ui::fontsLoad();                                     // FUN_00114ca0 (TEXASCII + FONT textures)
    game::ui::text().init();                                   // FUN_001129c0
    game::saveManager().init();                                // FUN_001169b0
    game::saveManager().setDirectory(fs::path(GetApplicationDirectory()) / "save");
    game::renderer2d().init();
    game::registerFrontScenes();                               // FUN_00100100
    game::registerGameScene();
    game::talkInstallGame();                                   // FUN_001a1dc0 talk task after the event tick
    game::talkDevOptions(argc, argv);                          // PORT: dev option --talk SPEAKER
    {
        // FUN_0022f950 (sceCdReadClock) -> 0x53eb20 = day | hour << 8 | minute << 16 | second << 24 (BCD bytes)
        const std::time_t now = std::time(nullptr);
        std::tm lt{};
        localtime_s(&lt, &now);
        auto bcd = [](int v) { return static_cast<uint32_t>(((v / 10) << 4) | (v % 10)); };
        game::gameWorld().rng.seed(bcd(lt.tm_mday) | bcd(lt.tm_hour) << 8 | bcd(lt.tm_min) << 16 | bcd(lt.tm_sec) << 24);
        if (devSeed >= 0) game::gameWorld().rng.seed(static_cast<uint32_t>(devSeed));
    }
    {
        std::string aiErr;                                     // FUN_001ed200(0) in the boot init FUN_00100730
        if (!game::aiLoad(platform::assetRoot(), &aiErr)) platform::log("ai: " + aiErr);
    }
    // FUN_00111e30(ctx, 0x4e7b30) in FUN_00100730: EXI manager + BQ.EXI (FUN_001120c0); BQ.PNG atlas for the renderer
    if (!game::effects().init()) platform::log("effects: BQ.EXI not loaded");
    if (!game::effectsRenderer().init()) platform::log("effects: renderer init failed");
    auto applyDevFlag = [](uint32_t f) {                       // PORT: dev options --flag / --clear-flag
        if ((f & 0x80000000u) != 0) game::flags().clear(f & 0x7fffffffu);
        else game::flags().set(f);
    };
    for (uint32_t f : devFlags) applyDevFlag(f);
    if (!devFlags.empty() || devCtlFlags != 0) {
        // PORT: --flag again at the end of the first Game init (gameInitProgress, after the new-game wipe
        // FUN_00178ad0), before the floor's story-event checks FUN_001229d0 run, so flag-gated events can be tested.
        static std::vector<uint32_t> pendingDevFlags;
        pendingDevFlags = devFlags;
        game::progressHooks().devAfterInit = [] {
            for (uint32_t f : pendingDevFlags) {
                if ((f & 0x80000000u) != 0) game::flags().clear(f & 0x7fffffffu);
                else game::flags().set(f);
            }
            pendingDevFlags.clear();
            if (game::PlayerControl* pc = game::playerCtl()) pc->flags |= devCtlFlags;   // read by FUN_001229d0 (0x5f)
            devCtlFlags = 0;
        };
        devFlags.clear();
    }
    if (devStartScene != nullptr) {
        game::scenes().start(game::sceneName(devStartScene));  // PORT: dev option --start-scene
    } else if (devFloor >= 0) {
        game::front().newGame = 1;                             // as after NameEntr: DAT_00469700 = 1, slot -1
        game::front().loadSlot = -1;
        game::optionDefaults();
        game::gameRequestFloor(devFloor, 0);
        game::scenes().start(game::sceneName("Game...."));
    } else {
        game::scenes().start(game::sceneName("BootMC.."));     // FUN_00206570(0x53ea80, "BootMC..")
    }
    bool mouseLocked = false;
    // PORT (cursor through scene changes / movies): heldCapture = the mouse-look capture is kept although the Game
    // scene is not running this frame (Game -> Game floor reload, Game <-> Ending story movie); its first live frame
    // drops the mouse delta. movieCursorHidden = the free cursor is hidden (HideCursor) while a movie plays.
    bool heldCapture = false, movieCursorHidden = false;
    auto captureCursor = [&] {
        DisableCursor();
        mouseLocked = true;
        movieCursorHidden = false;   // DisableCursor replaces the hidden mode
        platform::setMouseLookCaptured(true);
    };
    auto releaseCursor = [&] {
        EnableCursor();
        mouseLocked = false;
        heldCapture = false;
        movieCursorHidden = false;   // EnableCursor shows the cursor
        platform::setMouseLookCaptured(false);
    };
    bool userReleased = false;   // PORT: sticky cursor release (controls().releaseCursorKey)
    bool scriptInside = false;   // PORT: scripted cursor state (--shot mouse tests)
    float scriptX = 0.0f, scriptY = 0.0f;
    Vector2 lastMouse = {-1.0f, -1.0f};
    // PORT (window focus, PC_SETTINGS.md "Window focus"): winFocused = the window has the keyboard focus (live: GLFW
    // focus and not minimised; --shot: the --simulate-* script). focusHold is set when the focus is lost: the cursor is
    // not captured again until the window has the focus back and the player clicks inside it or presses a game key.
    bool winFocused = true, winMinimized = false, focusHold = false, focusMuted = false, simClick = false;
    long minimizedFrames = 0;
    const bool simFocusOn = simFocus.tick >= 0;
    // PORT (cursor test log): cursor capture / release / hide changes, in --simulate-* runs or with BAROQUE_TRACE_CURSOR
    bool cursorTrace = simFocusOn;
    {
        char* env = nullptr;
        size_t envLen = 0;
        if (_dupenv_s(&env, &envLen, "BAROQUE_TRACE_CURSOR") == 0 && env != nullptr) cursorTrace = true;
        std::free(env);
    }
    auto logCursor = [&](const char* what) {
        if (!cursorTrace) return;
        platform::log(std::string("cursor: ") + what + " (tick " + std::to_string(long(game::engine().tickCount)) + ", scene '" +
                      game::sceneNameString(game::scenes().current()) + "'" + (IsCursorHidden() ? ", hidden)" : ", visible)"));
    };
    auto onFocusChange = [&](bool focused) {
        const std::string at = " (tick " + std::to_string(long(game::engine().tickCount)) + ")";
        if (!focused) {
            const bool wasCaptured = mouseLocked;
            if (mouseLocked) releaseCursor();   // never keep the cursor clipped / hidden
            if (movieCursorHidden) { ShowCursor(); movieCursorHidden = false; }   // PORT: nor hidden for a movie
            focusHold = true;
            platform::setInputBlocked(true, false);   // PORT: no background gamepad
            platform::log("focus: lost" + at + ": cursor " + (wasCaptured ? "released" : "already free") +
                          (IsCursorHidden() ? " (STILL HIDDEN)" : "") + ", mouse look stopped, input blocked" +
                          ", muted, paused" + (winMinimized ? std::string(", minimised") : std::string()));
        } else {
            platform::setInputBlocked(false);
            platform::swallowMouseUntilRelease();   // the click that activates the window is no game / menu click
            if (shotPath == nullptr) game::present::refitBorderless();
            platform::log("focus: regained" + at + ": cursor stays free until a click inside the window or a game key");
        }
    };

    std::FILE* logicHash = nullptr;   // PORT: --logic-hash
    if (logicHashPath != nullptr && fopen_s(&logicHash, logicHashPath, "w") != 0) logicHash = nullptr;
    RenderTexture2D perTickTarget{};  // PORT: --present-per-tick off-screen target
    if (shotPath != nullptr && presentPerTick > 0) perTickTarget = LoadRenderTexture(720, 540);

    double acc = 0.0;
    auto last = std::chrono::steady_clock::now();
    while (!WindowShouldClose() && !game::scenes().finished()) {
        const auto now = std::chrono::steady_clock::now();
        {   // PORT (window focus): focus / minimise state of this host frame -> onFocusChange on a change
            bool focused = true, minimized = false;
            simClick = false;
            if (shotPath == nullptr) {
                minimized = IsWindowMinimized();
                focused = IsWindowFocused() && !minimized;
            } else if (simFocusOn) {   // --simulate-unfocus / --simulate-minimize
                if (simFocus.phase == 0 && long(game::engine().tickCount) >= simFocus.tick) { simFocus.phase = 1; simFocus.left = simFocus.frames; }
                if (simFocus.phase == 1) {
                    if (simFocus.left-- > 0) { focused = false; minimized = simFocus.minimize; }
                    else { simFocus.phase = 2; simFocus.left = 15; }
                }
                if (simFocus.phase == 2 && simFocus.left-- <= 0) {
                    simFocus.phase = 3;
                    simClick = true;
                    platform::log("focus: simulated left click inside the window (tick " + std::to_string(long(game::engine().tickCount)) + ")");
                }
            }
            winMinimized = minimized;
            if (focused != winFocused) { winFocused = focused; onFocusChange(focused); }
            if (!minimized && minimizedFrames > 0) {
                platform::log("focus: restored after " + std::to_string(minimizedFrames) + " minimised frames (nothing presented)");
                minimizedFrames = 0;
            }
        }
        // PORT (window focus): Alt+Enter toggles windowed <-> borderless (saved to baroque_port.ini); Enter is START, so
        // it is swallowed until released
        if (shotPath == nullptr && winFocused && (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) &&
            (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
            platform::swallowKeyUntilRelease(IsKeyPressed(KEY_ENTER) ? KEY_ENTER : KEY_KP_ENTER);
            game::present::PortSettings& ps = game::present::portSettings();
            ps.borderless = !ps.borderless;
            game::present::applyPortDisplay();
            game::present::savePortSettings();
        }
        // PORT (window focus): the logic stops the moment another window is active or the window is
        // minimised (no setting). A movie stops with it (its clock is the audio the device consumed). Muted while paused.
        const bool focusPaused = !winFocused || winMinimized;
        if (const bool mute = focusPaused; mute != focusMuted) {
            SetMasterVolume(mute ? 0.0f : 1.0f);
            focusMuted = mute;
        }
        if (!focusPaused) acc += std::chrono::duration<double>(now - last).count();
        last = now;
        if (acc > 0.25) acc = 0.25;
        if (shotPath != nullptr) acc = focusPaused ? 0.0 : kTickSeconds;   // deterministic: one tick per host frame
        // port-only: free mouse look while the Game scene runs (cursor captured; F1 releases it).
        // PORT: while a menu page or message window is open the cursor is released for the mouse menu layer.
        // PORT (window focus): a focus loss releases the cursor (onFocusChange); it is captured again only when the
        // window has the focus back and the player clicks inside it or presses a game key (in menus too)
        const bool clickInside = winFocused && (shotPath == nullptr ? IsCursorOnScreen() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) : simClick);
        if (focusHold && winFocused && (clickInside || platform::anyGameInputPressed())) {
            focusHold = false;
            platform::log(std::string("focus: capture allowed again after ") + (clickInside ? "a click" : "a game key") +
                          " (tick " + std::to_string(long(game::engine().tickCount)) + ")");
        }
        // PORT (cursor through scene changes / movies): the capture also covers the frames in which the Game scene is
        // not running but the game is still on its way between story scenes: between Exit and the next Init when the
        // next scene is Game or Ending (floor reloads and event warps are Game -> Game, a story movie is Game -> Ending
        // -> Game, a new / loaded game enters Game) and the whole Ending scene (its movies). These frames used to fall
        // into the release branch below, so the OS cursor popped up -- and stayed up through the synchronous floor
        // load -- at event / movie starts. Any other scene (Title, Option, GameLoad, ...) releases the cursor as before.
        const game::SceneManager& sm = game::scenes();
        const game::SceneName sceneTarget = sm.initPending() ? sm.next() : sm.current();
        const bool liveGame = game::gameActive();
        const bool storyTransition = !liveGame && (sceneTarget == game::sceneName("Game....") || sceneTarget == game::sceneName("Ending.."));
        if ((liveGame || storyTransition) && (shotPath == nullptr || simFocusOn) && !game::ui::mouse::wantsCursor()) {
            // PORT: the release key (controls.ini, default F1) toggles a sticky release; a left click inside the
            // window (or the key again) captures the cursor again. The capturing click is not an attack.
            if (winFocused && IsKeyPressed(platform::controls().releaseCursorKey)) {
                if (focusHold) { focusHold = false; userReleased = false; }
                else userReleased = !userReleased;
            } else if (userReleased && clickInside) {
                userReleased = false;
                platform::swallowMouseUntilRelease();
            }
            if (!mouseLocked && clickInside) platform::swallowMouseUntilRelease();
            if (userReleased || focusHold || !winFocused) {
                if (mouseLocked) { releaseCursor(); logCursor("released (F1 / focus)"); }
            } else {
                bool justLocked = false;
                if (!mouseLocked) {
                    captureCursor();
                    justLocked = true;
                    logCursor(liveGame ? "captured" : "captured (scene change / movie)");
                    if (simFocusOn)   // PORT (window focus) test log
                        platform::log("focus: cursor captured (tick " + std::to_string(long(game::engine().tickCount)) +
                                      (IsCursorHidden() ? ", hidden" : ", NOT hidden") + "); this frame's mouse delta is dropped");
                }
                const Vector2 md = GetMouseDelta();
                if (!liveGame) {
                    // no mouse look while the Game scene is not running; a left click still skips a movie (the free
                    // cursor's movie click, mouse_menu consumeMovieClick)
                    if (!heldCapture) { heldCapture = true; logCursor("held through a scene change / movie"); }
                    const bool click = shotPath == nullptr && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !platform::mouseSwallowed();
                    if (click && game::movieActive() != nullptr) game::ui::mouse::hostFrame(false, 0.0f, 0.0f, false, true, false, 0.0f);
                } else {
                    if (heldCapture) {   // first live frame after a held scene change: no jump from the load frames
                        heldCapture = false;
                        justLocked = true;
                        logCursor("hold ended, Game running");
                    }
                    if (!justLocked) {   // PORT: no jump from the re-centre
                        float yaw = 0.0f, pitch = 0.0f;
                        platform::mouseLookDegrees(md.x, md.y, &yaw, &pitch);
                        game::gameMouseLook(yaw, pitch);
                    }
                }
            }
        } else if (mouseLocked) {
            releaseCursor();
            logCursor(game::ui::mouse::wantsCursor() ? "released (a menu wants it)" : "released (scene change)");
        }
        // PORT: a movie with a free cursor (title attract movie, EVENT LIST movies, or a story movie reached with the
        // cursor released) hides the OS cursor while it plays; a click still skips it. Shown again when the movie ends,
        // a menu wants the cursor or the window loses the focus.
        if (!mouseLocked && (shotPath == nullptr || simFocusOn)) {
            const bool hide = game::movieActive() != nullptr && winFocused && !game::ui::mouse::wantsCursor();
            if (hide != movieCursorHidden) {
                if (hide) HideCursor(); else ShowCursor();
                movieCursorHidden = hide;
                logCursor(hide ? "free cursor hidden (movie)" : "free cursor shown (movie over)");
            }
        }
        platform::inputHostFrame();   // PORT: tap latching + wheel queue (CONTROLS_NATIVE.md), once per host frame
        // PORT: mouse menu layer input for this presented frame (window pixels -> 512x448 UI raster)
        game::ui::mouse::setEnabled(!mouseLocked);
        if (!mouseLocked) {
            float ux = 0.0f, uy = 0.0f, wheel = 0.0f;
            bool inside = false, moved = false, left = false, right = false;
            if (shotPath == nullptr) {
                const Vector2 mp = GetMousePosition();
                moved = mp.x != lastMouse.x || mp.y != lastMouse.y;
                lastMouse = mp;
                inside = IsCursorOnScreen() && game::ui::mouse::uiFromWindow(mp.x, mp.y, GetRenderWidth(), GetRenderHeight(), &ux, &uy);
                left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
                right = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
                wheel = GetMouseWheelMove();
                // PORT (window focus): no hover / clicks while another window is active; the activating click is
                // swallowed (platform::swallowMouseUntilRelease)
                if (!winFocused) { inside = moved = false; wheel = 0.0f; }
                if (!winFocused || platform::mouseSwallowed()) left = right = false;
            } else {
                for (int s = 0; s < mouseScriptCount; ++s) {
                    const ScriptedMouse& m = mouseScript[s];
                    if (long(game::engine().tickCount) != m.tick) continue;
                    if (m.kind <= 1) { moved = moved || !scriptInside || scriptX != m.x || scriptY != m.y; scriptInside = true; scriptX = m.x; scriptY = m.y; }
                    if (m.kind == 1) left = true;
                    if (m.kind == 2) right = true;
                    if (m.kind == 3) wheel += m.x;
                }
                inside = scriptInside;
                ux = scriptX;
                uy = scriptY;
            }
            game::ui::mouse::hostFrame(inside, ux, uy, moved, left, right, wheel);
        }
        const auto tickStart = std::chrono::steady_clock::now();
        int ticksThisFrame = 0;
        while (acc >= kTickSeconds) {
            acc -= kTickSeconds;
            ++ticksThisFrame;
            // FUN_00112c60(0x4e7e90): the original frees the text glyphs near the end of FUN_00101bd0, after the frame's
            // render. PORT (AUDIT_FRONTEND F3): the host releases them here, after every frame that showed them and
            // before the next tick, so glyphs a scene draws before its engine tick survive to the presentation and
            // any number of presented frames per tick shows the same text.
            game::ui::text().endOfTick();
            game::scenes().step();
            if (logicHash != nullptr) writeLogicHash(logicHash);   // PORT: --logic-hash
            if (devEvent >= 0 && game::gameActive() && !game::eventActive()) {   // PORT: dev option --event N
                game::eventTrigger(devEvent);
                devEvent = -1;
            }
            if (!devGive.empty() && game::gameActive()) {   // PORT: dev option --give
                for (uint32_t id : devGive)
                    if (!game::hudDevGiveItem(id, devGiveIdentified)) platform::log("--give: item refused");
                devGive.clear();
            }
            if (devRestoreState != nullptr && game::gameActive() && devApplyRestoreState(devRestoreState))   // PORT (dev)
                devRestoreState = nullptr;
            if (devRunEnd >= 0 && long(game::engine().tickCount) == devRunEnd && game::gameActive()) {   // PORT: --run-end
                game::onStairsExit(game::gameProgress());   // FUN_0011ce60 as reached from FUN_001cdcd0 (death fade done)
                devRunEnd = -1;
            }
            if (devHpMax > 0 && game::gameActive()) {   // PORT: --hp-max N (HUD gauge tests at a high max HP)
                if (game::Chr* p = game::characters().player(); p != nullptr && 0 < p->status.block.hp) {
                    p->status.block.hpMax = devHpMax;   // status +0x0 (FUN_001343f0 adds items, clamps to 999)
                    p->status.block.hp = devHpMax;
                    game::hudSetHp(devHpMax);           // as FUN_001349e0 -> FUN_00151ae0 would
                    devHpMax = -1;
                }
            }
            if (devKeepHp && game::gameActive()) {   // PORT: --keep-hp (route tests on deep floors at level 1)
                // deep-floor enemies one-shot a level-1 player: HP refilled and the invincible status bit 0x10000
                // (FUN_00136580 guard branch: no damage) held
                if (game::Chr* p = game::characters().player(); p != nullptr && 0 < p->status.block.hp) {
                    p->status.block.hp = p->status.block.hpMax;
                    p->status.block.statusMask |= 0x10000u;
                }
            }
            if (devWarp.tick >= 0 && long(game::engine().tickCount) == devWarp.tick && game::gameActive()) {   // PORT: --warp-to
                if (game::Chr* p = game::characters().player()) {
                    p->xf.m.pos[0] = devWarp.x;
                    p->xf.m.pos[1] = devWarp.y;
                    p->xf.m.pos[2] = devWarp.z;
                }
                devWarp.tick = -1;
            }
            if (devKill >= 0 && long(game::engine().tickCount) == devKill && game::gameActive()) {   // PORT: --kill
                if (game::Chr* p = game::characters().player()) p->status.block.hp = 0;   // FUN_00129c60 -> FUN_0012a5e0
                devKill = -1;
            }
            if (devStatus.tick >= 0 && long(game::engine().tickCount) == devStatus.tick && game::gameActive()) {   // PORT: --status
                if (game::Chr* p = game::characters().player()) game::inflictStatus(p->status, devStatus.mask);
                devStatus.tick = -1;
            }
            if (std::getenv("BAROQUE_TRACE_PLAYER") != nullptr && game::gameActive()) {   // PORT: debug trace
                if (const game::Chr* p = game::characters().player()) {
                    const char* a = p->act.currentName();
                    std::printf("trace t%lu state %d act %s hp %d anim %d (M069 %d M005 %d) states %d\n",
                                static_cast<unsigned long>(game::engine().tickCount), static_cast<int>(p->sm.cur), a ? a : "-",
                                p->status.block.hp, p->model.anim.currentClip(), p->model.anim.findClip("M069"),
                                p->model.anim.findClip("M005"), static_cast<int>(p->model.anim.combiner().states().size()));
                    std::printf("trace pos %.1f %.1f %.1f\n", p->xf.m.pos[0], p->xf.m.pos[1], p->xf.m.pos[2]);
                }
            }
            if (std::getenv("BAROQUE_TRACE_CHRS") != nullptr && game::gameActive() && game::engine().tickCount % 30 == 0) {   // PORT: debug trace
                for (const game::Chr* c : game::characters().list())
                    if (c != nullptr && c->type < 3) {
                        const char* a = c->act.currentName();
                        std::printf("chrs t%lu %s type %d pos %.1f %.1f %.1f lod %d/%d act %s states", static_cast<unsigned long>(game::engine().tickCount),
                                    c->idString().c_str(), c->type, c->xf.m.pos[0], c->xf.m.pos[1], c->xf.m.pos[2],
                                    int(c->model.lodDraw), int(c->model.actorFlag10), a ? a : "-");
                        for (const auto& s : c->model.anim.combiner().states())
                            std::printf(" [%s m%d st%d r%.2f t%lld]", s->clip >= 0 && size_t(s->clip) < c->model.clips.size() ? c->model.clips[size_t(s->clip)]->name.c_str() : "?",
                                        s->combineMode, s->status, s->blendRatio, static_cast<long long>(s->localTime / 1000000));
                        std::printf("\n");
                    }
            }
            if (shotPath != nullptr && long(game::engine().tickCount) == pressStartAt) {
                platform::injectButtons(0x800u, 1);   // scripted START (verification only)
            }
            for (int s = 0; s < scriptedCount; ++s)
                if (shotPath != nullptr && long(game::engine().tickCount) == scripted[s].tick) platform::injectButtons(scripted[s].mask, 1);
            for (int s = 0; s < holdScriptCount; ++s) {   // PORT: --hold (verification only)
                const long t = long(game::engine().tickCount);
                if (shotPath != nullptr && holdScript[s].from <= t && t < holdScript[s].to) platform::injectButtons(holdScript[s].mask, 1);
            }
            for (int s = 0; s < stickScriptCount; ++s) {   // PORT: --stick (verification only)
                const long t = long(game::engine().tickCount);
                if (shotPath != nullptr && stickScript[s].from <= t && t < stickScript[s].to)
                    platform::injectStick(stickScript[s].lx, stickScript[s].ly, 1);
            }
            for (int s = 0; s < lookScriptCount; ++s) {   // PORT: --mouse-look (verification only)
                const long lt = long(game::engine().tickCount);
                if (shotPath == nullptr || lt < lookScript[s].tick || lt >= lookScript[s].to || !game::gameActive()) continue;
                float yaw = 0.0f, pitch = 0.0f;
                platform::mouseLookDegrees(lookScript[s].dx, lookScript[s].dy, &yaw, &pitch);
                game::gameMouseLook(yaw, pitch);
            }
            for (int s = 0; s < deviceScriptCount; ++s)   // PORT: --input-device (media captures)
                if (shotPath != nullptr && long(game::engine().tickCount) == deviceScript[s].tick)
                    platform::setLastInputGamepad(deviceScript[s].gamepad);
            for (int s = 0; s < walkScriptCount; ++s) {   // PORT: --walk-to (verification only)
                const long t = long(game::engine().tickCount);
                if (shotPath == nullptr || t < walkScript[s].from || walkScript[s].to <= t || !game::gameActive()) continue;
                if (game::ui::messageBox().isOpen()) continue;   // leave menus / prompts to --press
                const game::Chr* p = game::characters().player();
                const game::PlayerControl* ctl = game::playerCtl();
                if (p == nullptr || ctl == nullptr || ctl->camera == nullptr) continue;
                float wx = walkScript[s].x, wz = walkScript[s].z;
                if (walkScript[s].exit && !walkExitTarget(&wx, &wz)) continue;
                if (t % 60 == 30) std::fprintf(stderr, "walk t=%ld target (%.0f, %.0f)\n", t, wx, wz);
                float dx = wx - p->xf.m.pos[0], dz = wz - p->xf.m.pos[2];
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d < 12.0f) continue;
                dx /= d;
                dz /= d;
                {   // unstick: the distance to the target has not shrunk by 8 for 20 ticks -> stand and strike for 24
                    // ticks (doors O??N / walls O??M open to attacks), then 16 ticks sideways (side alternates)
                    static float best = 1e30f, lastWx = 0.0f, lastWz = 0.0f;
                    static int still = 0, side = 0, sideTicks = 0, strikeTicks = 0;
                    if (wx != lastWx || wz != lastWz) { best = 1e30f; still = 0; lastWx = wx; lastWz = wz; }
                    if (strikeTicks > 0) {
                        if (--strikeTicks == 20) platform::injectButtons(0x40u, 1);
                        continue;   // stick released while striking
                    }
                    if (sideTicks > 0) {
                        --sideTicks;
                        const float sdx = side ? -dz : dz, sdz = side ? dx : -dx;
                        dx = sdx;
                        dz = sdz;
                        best = 1e30f;
                    } else if (d < best - 8.0f) {
                        best = d;
                        still = 0;
                    } else if (++still >= 20) {
                        still = 0;
                        strikeTicks = 24;
                        sideTicks = 16;
                        side ^= 1;
                    }
                }
                // mv = fwd * sy + right * sx (fwd = camera +0x70 flattened, toward the eye; right = camera +0x58)
                float fx = ctl->camera->forward[0], fz = ctl->camera->forward[2];
                const float fl = std::sqrt(fx * fx + fz * fz);
                if (fl > 0.0f) { fx /= fl; fz /= fl; }
                const float rx = ctl->camera->right[0], rz = ctl->camera->right[2];
                float sy = dx * fx + dz * fz, sx = dx * rx + dz * rz;
                const float m = std::fmax(std::fabs(sx), std::fabs(sy));
                if (m > 0.0f) { sx /= m; sy /= m; }
                platform::injectStick(int(std::lround(128.0f + sx * 127.0f)), int(std::lround(128.0f + sy * 127.0f)), 1);
            }
            if (shotPath != nullptr && long(game::engine().tickCount) == menuAtTick && game::ui::menu().isOpen()) {
                game::ui::menu().close();     // PORT: --menu-at (verification only)
                game::ui::menu().open(menuAtMode);
            }
        }
        if (perTickTarget.id != 0) {   // PORT: --present-per-tick K: K discarded frames at alpha k/K
            for (int k = 1; k <= presentPerTick; ++k) {
                BeginTextureMode(perTickTarget);
                present(720, 540, float(k) / float(presentPerTick));
                EndTextureMode();
            }
        }
        for (const char* ap = shotPath != nullptr ? game::autoplayNextShot() : nullptr; ap != nullptr; ap = game::autoplayNextShot()) {
            RenderTexture2D rt = LoadRenderTexture(shotW, shotH);   // PORT: --autoplay milestone captures (talk_wiring.hpp)
            BeginTextureMode(rt);
            present(shotW, shotH, shotAlpha);
            EndTextureMode();
            Image img = LoadImageFromTexture(rt.texture);
            ImageFlipVertical(&img);
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            ExportImage(img, ap);
            UnloadImage(img);
            UnloadRenderTexture(rt);
        }
        if (shotPath != nullptr && game::autoplayFinished()) shotTicks = long(game::engine().tickCount);   // PORT: --autoplay end
        // PORT: mark the scripted cursor on a --shot / --shot-also capture (mouse menu tests, media captures)
        auto drawShotCursor = [&]() {
            float mx = 0.0f, my = 0.0f;   // PORT: mark the scripted cursor (mouse menu tests)
            if (mouseScriptCount > 0 && game::ui::mouse::cursor(&mx, &my)) {
                const game::ScreenLayout lay = game::screenLayout(shotW, shotH);
                const game::Viewport43 vp{lay.boxX, lay.boxY, lay.boxW, lay.boxH};
                const float px = (mx + 0.5f) * float(vp.w) / 512.0f + float(vp.x), py = (my + 0.5f) * float(vp.h) / 448.0f + float(vp.y);
                if (arrowCursor) {   // PORT (media captures): an ordinary arrow pointer instead of the test marker
                    const float s = float(shotH) / 540.0f;
                    const Vector2 tip{px, py}, l{px, py + 21.0f * s}, r{px + 15.0f * s, py + 15.0f * s};
                    const Vector2 t0{px + 5.0f * s, py + 16.0f * s}, t1{px + 9.0f * s, py + 14.0f * s};
                    const Vector2 t2{px + 13.0f * s, py + 24.0f * s}, t3{px + 9.0f * s, py + 26.0f * s};
                    DrawTriangle(tip, l, r, WHITE);
                    DrawTriangle(t0, t2, t1, WHITE);
                    DrawTriangle(t0, t3, t2, WHITE);
                    DrawTriangleLines(tip, l, r, BLACK);
                    DrawLineV(t0, t3, BLACK);
                    DrawLineV(t3, t2, BLACK);
                    DrawLineV(t2, t1, BLACK);
                } else {
                    DrawCircleLines(int(px), int(py), 10.0f, RED);
                    DrawLine(int(px) - 16, int(py), int(px) + 16, int(py), RED);
                    DrawLine(int(px), int(py) - 16, int(px), int(py) + 16, RED);
                }
            }
        };
        for (int s = 0; s < extraShotCount; ++s) {   // PORT: --shot-also (verification only)
            if (shotPath == nullptr || long(game::engine().tickCount) != extraShots[s].tick) continue;
            RenderTexture2D rt = LoadRenderTexture(shotW, shotH);
            BeginTextureMode(rt);
            present(shotW, shotH, shotAlpha);
            drawShotCursor();
            EndTextureMode();
            Image img = LoadImageFromTexture(rt.texture);
            ImageFlipVertical(&img);
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);   // the window ignores the target's alpha; so does the capture
            ExportImage(img, extraShots[s].path);
            UnloadImage(img);
            UnloadRenderTexture(rt);
        }
        if (windowTestTick >= 0 && shotPath != nullptr && long(game::engine().tickCount) >= windowTestTick) {
            // PORT (window focus) test --window-test: the window-mode code on the hidden window, one step every 5 host
            // frames (GLFW reports the new size by the next poll); the settings file is not written
            static int wtFrame = 0;
            const int f = wtFrame++;
            auto state = [](const char* what) {
                const int mon = GetCurrentMonitor();
                const Vector2 wp = GetWindowPosition(), mp = GetMonitorPosition(mon);
                platform::log(std::string("window-test: ") + what + ": window " + std::to_string(GetScreenWidth()) + "x" +
                              std::to_string(GetScreenHeight()) + " at " + std::to_string(int(wp.x)) + "," + std::to_string(int(wp.y)) +
                              ", render " + std::to_string(GetRenderWidth()) + "x" + std::to_string(GetRenderHeight()) +
                              ", monitor " + std::to_string(mon) + " " + std::to_string(GetMonitorWidth(mon)) + "x" +
                              std::to_string(GetMonitorHeight(mon)) + " at " + std::to_string(int(mp.x)) + "," + std::to_string(int(mp.y)) +
                              ", topmost " + (IsWindowState(FLAG_WINDOW_TOPMOST) ? "YES" : "no") + ", undecorated " +
                              (IsWindowState(FLAG_WINDOW_UNDECORATED) ? "yes" : "no") + ", raylib-borderless " +
                              (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "YES" : "no") + ", ours " +
                              (game::present::borderlessActive() ? "borderless" : "windowed"));
            };
            auto capture = [](const std::string& path) {   // the real BeginDrawing / present path, read back
                BeginDrawing();
                present(GetRenderWidth(), GetRenderHeight(), 1.0f);
                Image img{};
                img.data = rlReadScreenPixels(GetRenderWidth(), GetRenderHeight());
                img.width = GetRenderWidth();
                img.height = GetRenderHeight();
                img.mipmaps = 1;
                img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                EndDrawing();
                ExportImage(img, path.c_str());
                UnloadImage(img);
            };
            const std::string base = std::string(shotPath).substr(0, std::string(shotPath).rfind('.'));
            if (f == 0) {
                state("start");
                game::present::portSettings().borderless = true;
                game::present::applyWindowMode();
            } else if (f == 5) {
                state("borderless");
                capture(base + "_borderless.png");
                game::present::portSettings().borderless = false;
                game::present::applyWindowMode();
            } else if (f == 10) {
                state("windowed again");
                SetWindowSize(960, 540);
            } else if (f == 15) {
                state("resized 960x540");
                capture(base + "_960x540.png");
                // (raylib's ToggleBorderlessWindowed is not called here: its glfwFocusWindow would activate the hidden
                // test window and take the focus from the desktop)
                SetWindowSize(1440, 1080);
            } else if (f == 20) {
                state("resized 1440x1080");
                capture(base + "_1440x1080.png");
            }
        }
        static const auto gShotStart = std::chrono::steady_clock::now();
        const bool msDue = gShotWindowMs >= 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gShotStart).count() >= gShotWindowMs;
        if (gShotWindow && (long(game::engine().tickCount) >= shotTicks || msDue)) {
            BeginDrawing();
            present(GetRenderWidth(), GetRenderHeight(), 1.0f);
            // read exactly the framebuffer (LoadImageFromScreen scales by the monitor DPI factor)
            const int fbw = GetRenderWidth(), fbh = GetRenderHeight();
            Image img{};
            img.data = rlReadScreenPixels(fbw, fbh);
            img.width = fbw;
            img.height = fbh;
            img.mipmaps = 1;
            img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            EndDrawing();
            ExportImage(img, shotPath);
            platform::log("shot-window: " + std::to_string(GetScreenWidth()) + "x" + std::to_string(GetScreenHeight()) +
                          " render " + std::to_string(GetRenderWidth()) + "x" + std::to_string(GetRenderHeight()));
            UnloadImage(img);
            break;
        }
        if (shotPath != nullptr && long(game::engine().tickCount) >= shotTicks) {
            RenderTexture2D rt = LoadRenderTexture(shotW, shotH);
            BeginTextureMode(rt);
            present(shotW, shotH, shotAlpha);
            drawShotCursor();
            EndTextureMode();
            Image img = LoadImageFromTexture(rt.texture);
            ImageFlipVertical(&img);
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);   // the window ignores the target's alpha; so does the capture
            ExportImage(img, shotPath);
            UnloadImage(img);
            UnloadRenderTexture(rt);
            break;
        }
        const auto drawStart = std::chrono::steady_clock::now();
        BeginDrawing();   // PORT (window focus): also while minimised (EndDrawing polls the window events)
        if (!winMinimized) present(GetRenderWidth(), GetRenderHeight(), float(acc / kTickSeconds));
        else ++minimizedFrames;
        const auto drawEnd = std::chrono::steady_clock::now();
        EndDrawing();
        // PORT (window focus): a minimised window (swap returns at once) or a paused background game must not spin a
        // core at full speed
        if (shotPath == nullptr && (winMinimized || focusPaused)) WaitTime(winMinimized ? 0.05 : 0.03);
        {   // PORT: BAROQUE_DEBUG_PERF=1 logs frame pacing every 2 s (frame, tick work, draw work, worst frame)
            static const bool perf = [] {
                char* v = nullptr;
                size_t n = 0;
                const bool on = _dupenv_s(&v, &n, "BAROQUE_DEBUG_PERF") == 0 && v != nullptr;
                std::free(v);
                return on;
            }();
            static double sumFrame = 0, sumTick = 0, sumDraw = 0, worst = 0;
            static int frames = 0, ticks = 0;
            static auto windowStart = std::chrono::steady_clock::now();
            if (perf) {
                const auto end = std::chrono::steady_clock::now();
                const double f = std::chrono::duration<double, std::milli>(end - now).count();
                sumFrame += f;
                sumTick += std::chrono::duration<double, std::milli>(drawStart - tickStart).count();
                sumDraw += std::chrono::duration<double, std::milli>(drawEnd - drawStart).count();
                if (worst < f) worst = f;
                ++frames;
                ticks += ticksThisFrame;
                if (std::chrono::duration<double>(end - windowStart).count() >= 2.0) {
                    char buf[256];
                    std::snprintf(buf, sizeof buf, "perf: %d frames (%.1f fps) %d ticks | frame %.2f ms, ticks %.2f ms, draw %.2f ms, worst %.1f ms",
                                  frames, frames / std::chrono::duration<double>(end - windowStart).count(), ticks,
                                  sumFrame / frames, sumTick / frames, sumDraw / frames, worst);
                    platform::log(buf);
                    sumFrame = sumTick = sumDraw = worst = 0;
                    frames = ticks = 0;
                    windowStart = end;
                }
            }
        }
    }
    if (logicHash != nullptr) std::fclose(logicHash);
    if (perTickTarget.id != 0) UnloadRenderTexture(perTickTarget);
    game::movieEnd();
    game::engine().soundFrame = nullptr;
    audio::uninstallSoundSystem();
    game::renderer2d().shutdown();
    game::ui::textures().shutdown();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
