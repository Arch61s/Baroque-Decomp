// PORT test driver `--autoplay FROM` (--shot runs only; not original behaviour, verification only).
//
// A scripted "player" for the long continuous playthrough (work/reports/PLAYTHROUGH_FULL_2026-10-10.md). From engine
// tick FROM on it reads what a player sees (scene, message window + highlighted choice, item screen cursor, the player
// / enemy / item positions, HP / VT, the floor's cell map) and produces ONLY pad input through the same injection
// hooks as --press / --stick (platform::injectButtons / injectStick, applied to the next pad sample). It never writes
// game state, except the optional keep-hp stand-in below (same as main.cpp --keep-hp: HP refilled + status bit 0x10000)
// which is enabled only in the descent phase after the first death + continue.
//
// Phases: RUN1 (first trip down the tower, no keep-hp, until the player dies) -> DEAD (run-end menu 0x3b: Save to
// file 1, then "Return to the title screen") -> CONTINUE (Title -> GameLoad -> file 1 -> Continue -> load) -> HUB
// (walk to the tower gate PT10, YES) -> DESCENT (keep-hp, stairs menu Save every N floors) -> floor 99 (equip the
// Angelic Rifle, shoot the angel) -> DONE.
//
// Options: --autoplay FROM, --autoplay-save-every N (default 5), --autoplay-shots DIR (milestone captures through
// main.cpp's capture hook), --autoplay-keep-hp-now (keep-hp from the start), --autoplay-max-stall T (end the run when
// the floor does not change for T ticks, default 30000), --autoplay-phase N (start phase; 4 = descent after a
// Continue run).
#include "game/talk/talk_wiring.hpp"

#include "alchemy/texture.hpp"
#include "data/tlk.hpp"

#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/engine.hpp"
#include "game/event/event_manager.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/free_camera.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/player_move.hpp"
#include "game/scene_manager.hpp"
#include "game/scenes/game_scene.hpp"
#include "game/sound_stub.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/message_box.hpp"
#include "platform/input.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace baroque::game {

bool autoplayProcessMemory(size_t* workingSet, size_t* privateBytes, unsigned long* handles, unsigned long* gdi,
                           unsigned long* user);   // autoplay_mem.cpp

namespace {

enum Phase { kRun1 = 0, kDead = 1, kContinue = 2, kHub = 3, kDescent = 4, kDone = 5 };
const char* const kPhaseName[] = {"RUN1", "DEAD", "CONTINUE", "HUB", "DESCENT", "DONE"};

constexpr uint32_t kL2 = 1, kTriangle = 0x10, kCircle = 0x20, kCross = 0x40, kSquare = 0x80, kStart = 0x800, kUp = 0x1000,
                   kRight = 0x2000, kDown = 0x4000, kLeft = 0x8000;

struct Bot {
    bool on = false;
    long from = 0;
    int phase = kRun1;
    int saveEvery = 5;
    long maxStall = 30000;
    bool keepHp = false;
    std::string shotDir;
    // shots
    std::deque<std::string> shots;
    std::string shotBuf;
    bool finished = false;
    long finishAt = -1;
    // progress
    int floor = -1;
    long floorTick = 0;
    int floorsVisited = 0;
    int deepest = -1;
    int floorsSinceSave = 0;
    bool savePending = false;       // the current stairs / run-end menu wants a save
    bool savedThisMenu = false;
    bool saveDone = false;          // 0x27 seen in the current save flow
    bool titleChosen = false;
    int deaths = 0;
    int saves = 0;
    int itemUses = 0;
    int lastLevel = -1;
    uint32_t lastStatus = 0;
    int lastBgm = -2;
    int lastInvCount = -1;
    std::string lastScene;
    bool lastEvent = false;
    int lastBoxId = 0;
    long boxOpenedAt = 0;
    long lastBoxPress = -100;
    int boxPresses = 0;
    long lastMenuPress = -100;
    bool shotThisBox = false;
    long lastMemLog = -1;
    bool deadLogged = false;
    // navigation
    float bestDist = 1e30f;
    float lastWx = 1e30f, lastWz = 1e30f;
    int still = 0, side = 0, sideTicks = 0, strikeTicks = 0;
    std::map<const void*, long> ignoreUntil;   // enemies / items skipped until tick
    const Chr* foe = nullptr;
    long foeSince = 0;
    int foeHp = 0;
    long foeHpTick = 0;
    const Chr* itemTarget = nullptr;
    long itemSince = 0;
    // item screen macro
    int macro = 0;                  // 0 none, 1 open, 2 move cursor, 3 context, 4 execute, 5 close
    int macroIdx = -1;
    uint32_t macroItemId = 0;
    long macroStart = 0, macroLast = 0;
    const char* macroWhy = "";
    long nextItemCheck = 0;
    // floor 99
    long angelShotAt = -1;
    int angelShots = 0;
    // hub route
    int hubWp = 0;
    // story mode (--autoplay-story): hub progression for the story flags 35..37 and the endings
    bool story = false;
    int macroCtx = 0;               // context-menu row the item macro executes (0 = primary, 2 = Place, 3 = Give)
    long giftTryAt = -1;
    int giftTries = 0;
    long storyLogAt = -1;
    uint32_t watchBits = 0;         // last values of the watched story flags
    bool watchInit = false;
    // stage marks
    bool clearedSeen = false;
    long eventStart = -1;
};
Bot gBot;

long now() { return long(engine().tickCount); }

void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    platform::log(std::string("autoplay t=") + std::to_string(now()) + " " + buf);
}

void shot(const char* what) {
    if (gBot.shotDir.empty()) return;
    char name[256];
    std::snprintf(name, sizeof name, "%s/t%06ld_f%03d_%s.png", gBot.shotDir.c_str(), now(), gBot.floor, what);
    gBot.shots.push_back(name);
    logf("shot %s", name);
}

void press(uint32_t mask) { platform::injectButtons(mask, 1); }

// left stick toward world direction (dx, dz) through the gameplay camera axes (FUN_0013cf90: mv = camFwd*sy + camRight*sx)
bool steerDir(float dx, float dz, float tilt = 1.0f) {
    const PlayerControl* ctl = playerCtl();
    if (ctl == nullptr || ctl->camera == nullptr) return false;
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d < 1e-3f) return false;
    dx /= d;
    dz /= d;
    float f[2] = {ctl->camera->forward[0], ctl->camera->forward[2]};
    float r[2] = {ctl->camera->right[0], ctl->camera->right[2]};
    if (freeCameraActive()) {
        float fb[3], fr[3];
        freeCameraAxes(fb, fr);
        f[0] = fb[0]; f[1] = fb[2];
        r[0] = fr[0]; r[1] = fr[2];
    }
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1]), rl = std::sqrt(r[0] * r[0] + r[1] * r[1]);
    if (fl > 0.0f) { f[0] /= fl; f[1] /= fl; }
    if (rl > 0.0f) { r[0] /= rl; r[1] /= rl; }
    float sy = dx * f[0] + dz * f[1], sx = dx * r[0] + dz * r[1];
    const float m = std::max(std::fabs(sx), std::fabs(sy));
    if (m > 0.0f) { sx /= m; sy /= m; }
    platform::injectStick(std::clamp(int(std::lround(128.0f + sx * 127.0f * tilt)), 0, 255),
                          std::clamp(int(std::lround(128.0f + sy * 127.0f * tilt)), 0, 255), 1);
    return true;
}

// ---- cell grid path (same cell rules as main.cpp --walk-exit) ----------------------------------------------------
struct Grid {
    Dungeon* d = nullptr;
    int w = 0, h = 0;
    std::vector<int8_t> lvl;   // -1 closed, 0 ground, 1 upper
    std::vector<int8_t> rm;    // room index of room cells, else -1
    int room(int x, int z) const { return (x < 0 || z < 0 || x >= w || z >= h) ? -1 : rm[size_t(z) * size_t(w) + size_t(x)]; }
    float ox = 0.0f, oz = 0.0f;   // world position of the corner of cell (0, 0)
    bool build() {
        d = gameWorld().dungeon;
        if (d == nullptr) return false;
        const int fl = gameProgress().floor;
        ox = oz = 0.0f;
        if (fl == 0x5b) {   // the hub has a cell lookup (FUN_00124c70 offsets (+23, +20)); floor 99 walks straight
            if (d->roomCount() < 1) return false;
            ox = -0x17 * 200.0f;
            oz = -0x14 * 200.0f;
            w = d->room(0).rdt.w8 * 8;
            h = d->room(0).rdt.h8 * 8;
        } else {
            w = d->context().maxX + 2;
            h = d->context().maxY + 2;
        }
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
        lvl.assign(size_t(w) * size_t(h), -1);
        rm.assign(size_t(w) * size_t(h), -1);
        for (int z = 0; z < h; ++z)
            for (int x = 0; x < w; ++x) {
                const float pos[3] = {ox + float(x) * 200.0f + 100.0f, 0.0f, oz + float(z) * 200.0f + 100.0f};
                int cx = 0, cy = 0;
                const uint32_t* c = d->cellAtWorld(pos, &cx, &cy);
                if (c == nullptr || (*c & 0xf000u) == 0) continue;
                if ((*c & 0xf000u) == 0x2000u && (*c & 0xffu) >= 15u) continue;   // blocked door pieces 15..18
                lvl[size_t(z) * size_t(w) + size_t(x)] = (*c & 0xf00u) != 0 ? 1 : 0;
                if ((*c & 0xf000u) == 0x1000u) rm[size_t(z) * size_t(w) + size_t(x)] = int8_t(*c & 0xfu);
            }
        return true;
    }
    int level(int x, int z) const {
        if (x < 0 || z < 0 || x >= w || z >= h) return -1;
        return lvl[size_t(z) * size_t(w) + size_t(x)];
    }
    int relax = 0;   // 1: level changes anywhere, 2: also room-to-room
    bool passable(int ax, int az, int bx, int bz) const {
        const int la = level(ax, az), lb = level(bx, bz);
        if (la < 0 || lb < 0) return false;
        const int ra = room(ax, az), rb = room(bx, bz);
        if (relax < 2 && ra >= 0 && rb >= 0 && ra != rb) return false;   // two rooms side by side: a wall between them
        if (la == lb || relax >= 1) return true;
        const int lx = la == 0 ? ax : bx, lz = la == 0 ? az : bz;
        const int hx = la == 0 ? bx : ax, hz = la == 0 ? bz : az;
        const int ddx = hx - lx, ddz = hz - lz;
        return level(lx - ddx, lz - ddz) == 0 && level(lx + ddz, lz + ddx) != 0 && level(lx - ddz, lz - ddx) != 0;
    }
};
Grid gGrid;
std::vector<long long> gBlockedEdges;   // learned: cell pairs the player could not cross (walls, ledges, closed gates)
bool edgeBlocked(int a, int b) {
    const long long k1 = (long long)a * 1048576 + b, k2 = (long long)b * 1048576 + a;
    for (long long k : gBlockedEdges)
        if (k == k1 || k == k2) return true;
    return false;
}
int gLastFrom = -1, gLastTo = -1;   // the edge the last pathStep chose (player cell -> next cell)
const void* gGridFor = nullptr;
int gGridFloor = -1;

bool pathStepOnce(float px, float pz, float tx, float tz, float* wx, float* wz, int* len);
// next waypoint toward (tx, tz); len = path length in cells (-1 no path: straight line). Strict rules first, then
// relaxed (level changes anywhere, then room-to-room) when the strict grid has no path.
bool pathStep(float px, float pz, float tx, float tz, float* wx, float* wz, int* len) {
    for (int r = 0; r < 3; ++r) {
        gGrid.relax = r;
        pathStepOnce(px, pz, tx, tz, wx, wz, len);
        if (*len >= 0) break;
    }
    gGrid.relax = 0;
    return true;
}
bool pathStepOnce(float px, float pz, float tx, float tz, float* wx, float* wz, int* len) {
    *wx = tx;
    *wz = tz;
    *len = -1;
    if (gGridFor != gameWorld().dungeon || gGridFloor != gameProgress().floor) {
        gGridFor = gameWorld().dungeon;
        gGridFloor = gameProgress().floor;
        if (!gGrid.build()) gGrid.w = 0;
    }
    if (gGrid.w == 0) return true;
    const int sx = int(std::floor((px - gGrid.ox) / 200.0f)), sz = int(std::floor((pz - gGrid.oz) / 200.0f));
    const int gx = int(std::floor((tx - gGrid.ox) / 200.0f)), gz = int(std::floor((tz - gGrid.oz) / 200.0f));
    const int w = gGrid.w, h = gGrid.h;
    if (sx < 0 || sz < 0 || sx >= w || sz >= h || gx < 0 || gz < 0 || gx >= w || gz >= h) return true;
    if (sx == gx && sz == gz) { *len = 0; return true; }
    std::vector<int> prev(size_t(w) * size_t(h), -1);
    std::vector<int> q;
    const int start = sz * w + sx, goal = gz * w + gx;
    prev[size_t(start)] = start;
    q.push_back(start);
    for (size_t qi = 0; qi < q.size() && prev[size_t(goal)] < 0; ++qi) {
        const int cur = q[qi], cx = cur % w, cz = cur / w;
        static const int ddx[4] = {0, 1, 0, -1}, ddz[4] = {-1, 0, 1, 0};
        for (int k = 0; k < 4; ++k) {
            const int nx = cx + ddx[k], nz = cz + ddz[k];
            if (!(nx == gx && nz == gz) && !gGrid.passable(cx, cz, nx, nz)) continue;
            if (nx < 0 || nz < 0 || nx >= w || nz >= h) continue;
            const int n = nz * w + nx;
            if (edgeBlocked(cur, n)) continue;
            if (prev[size_t(n)] >= 0) continue;
            prev[size_t(n)] = cur;
            q.push_back(n);
        }
    }
    if (prev[size_t(goal)] < 0) return true;
    int step = goal, n = 1;
    while (prev[size_t(step)] != start) { step = prev[size_t(step)]; ++n; }
    *len = n;
    gLastFrom = start;
    gLastTo = step;
    if (step != goal) {
        *wx = gGrid.ox + float(step % w) * 200.0f + 100.0f;
        *wz = gGrid.oz + float(step / w) * 200.0f + 100.0f;
    }
    return true;
}

void resetUnstick() {
    gBot.bestDist = 1e30f;
    gBot.still = 0;
    gBot.sideTicks = 0;
    gBot.strikeTicks = 0;
}

// walk toward (wx, wz) with the unstick rule of main.cpp --walk-exit (strike 24 ticks: doors / walls open to attacks;
// then 16 ticks sideways)
void walkTo(const Chr& p, float wx, float wz) {
    float dx = wx - p.xf.m.pos[0], dz = wz - p.xf.m.pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d < 12.0f) return;
    if (std::fabs(wx - gBot.lastWx) > 1.0f || std::fabs(wz - gBot.lastWz) > 1.0f) {
        gBot.bestDist = 1e30f;
        gBot.still = 0;
        gBot.lastWx = wx;
        gBot.lastWz = wz;
    }
    if (gBot.strikeTicks > 0) {
        if (--gBot.strikeTicks == 20) press(kSquare);
        return;
    }
    if (gBot.sideTicks > 0) {
        --gBot.sideTicks;
        const float sdx = gBot.side ? -dz : dz, sdz = gBot.side ? dx : -dx;
        dx = sdx;
        dz = sdz;
        gBot.bestDist = 1e30f;
    } else if (d < gBot.bestDist - 8.0f) {
        gBot.bestDist = d;
        gBot.still = 0;
    } else if (++gBot.still >= 20) {
        gBot.still = 0;
        gBot.strikeTicks = 24;
        gBot.sideTicks = 16;
        gBot.side ^= 1;
        // the same cell edge failed three times: remember it as closed and plan around it
        static int edgeFails = 0, failFrom = -1, failTo = -1;
        if (gLastFrom == failFrom && gLastTo == failTo) ++edgeFails;
        else { failFrom = gLastFrom; failTo = gLastTo; edgeFails = 1; }
        if (edgeFails >= 3 && failFrom >= 0 && failTo >= 0 && failFrom != failTo) {
            gBlockedEdges.push_back((long long)failFrom * 1048576 + failTo);
            logf("path: edge %d -> %d blocked (stuck at (%.0f %.0f))", failFrom, failTo, double(p.xf.m.pos[0]), double(p.xf.m.pos[2]));
            edgeFails = 0;
            gLastFrom = gLastTo = -1;
        }
    }
    steerDir(dx, dz);
}

float dist2(const Chr& a, const Chr& b) {
    const float dx = a.xf.m.pos[0] - b.xf.m.pos[0], dz = a.xf.m.pos[2] - b.xf.m.pos[2];
    return std::sqrt(dx * dx + dz * dz);
}

bool ignored(const void* p) {
    auto it = gBot.ignoreUntil.find(p);
    return it != gBot.ignoreUntil.end() && now() < it->second;
}

// ---- message windows -------------------------------------------------------------------------------------------
// the value the bot wants for the open window (0 = just confirm with Cross)
int wantedChoice(int id) {
    switch (id) {
    case 0x33: return 1;                 // tower gate "Go to Neuro Tower?" -> YES
    case 0x34: return 1;                 // "Go down?" -> YES
    case 0x35: return 2;                 // "Go to the Training Dungeon?" -> NO (tower route)
    case 0x3a:                           // stairs menu: Resume 3 / Save 4 / Return to title 5
        if (gBot.phase == kDead) return !gBot.savedThisMenu ? 4 : 5;   // after the rebirth: save, then quit to the title
        return (gBot.savePending && !gBot.savedThisMenu) ? 4 : 3;
    case 0x36: return 1;                 // hub PT12 "Go to ...?" -> YES (floor 0x5c)
    case 0x3b:                           // run-end menu
        if (gBot.phase == kDead) return !gBot.savedThisMenu ? 4 : 5;
        if (gBot.story) return !gBot.savedThisMenu ? 4 : 3;   // story: save every run end (restart point), then Resume
        return (gBot.savePending && !gBot.savedThisMenu) ? 4 : 3;
    case 0x39: return gBot.titleChosen ? 1 : 2;   // "Return to the title screen?"
    case 0x22: case 0x23: return 1;      // "Save game file N?" (new / overwrite)
    case 0x12: return 1;                 // "Load game file N?"
    case 0x42: return 0x12;              // file menu: "Continue a game."
    default: return 0;
    }
}

void handleBox() {
    ui::MessageBox& box = ui::messageBox();
    const int id = box.currentId();
    if (id != gBot.lastBoxId || gBot.boxOpenedAt == 0) {
        gBot.lastBoxId = id;
        gBot.boxOpenedAt = now();
        gBot.boxPresses = 0;
        gBot.shotThisBox = false;
        logf("message 0x%x opened (highlight %d) phase %s floor %d", id, box.highlighted(), kPhaseName[gBot.phase], gBot.floor);
    }
    if (!box.ready()) return;
    if (!gBot.shotThisBox) {
        gBot.shotThisBox = true;
        if (id == 0x34 || id == 0x3a || id == 0x3b || id == 0x39 || id == 0x33 || id == 0x27 || id == 0x14 || id == 0x22 ||
            id == 0x23) {
            char w[32];
            std::snprintf(w, sizeof w, "msg%02x", id);
            shot(w);
        }
    }
    if (now() - gBot.lastBoxPress < 10) return;
    if (id == 0x26 || id == 0x13) return;   // "Saving..." / "Loading..." progress windows
    const int want = wantedChoice(id);
    gBot.lastBoxPress = now();
    if (want == 0) {
        // information window: Cross; Circle after a few tries (file page messages close on either)
        press(gBot.boxPresses++ < 3 ? kCross : kCircle);
        if (id == 0x27) gBot.saveDone = true;
        return;
    }
    const int h = box.highlighted();
    if (h == want) {
        press(kCross);
        logf("message 0x%x choose %d", id, want);
        if (id == 0x3a || id == 0x3b) {
            if (want == 4) {
                gBot.savedThisMenu = true;
                gBot.saveDone = false;
            }
            if (want == 5) gBot.titleChosen = true;
            if (want == 3) {
                gBot.savedThisMenu = false;
                gBot.savePending = false;
            }
        }
        return;
    }
    if (id == 0x3a || id == 0x3b) press(h < want ? kDown : kUp);
    else if (id == 0x42) press(kDown);
    else press(kRight);   // YES / NO toggle (Left / Right)
}

// file select page (in-game save, or the GameLoad scene)
void handleFilePage() {
    if (now() - gBot.lastMenuPress < 20) return;
    gBot.lastMenuPress = now();
    if (scenes().isCurrent("GameLoad")) {
        press(kCross);                 // slot 1 (cursor 0) -> file menu 0x42
        return;
    }
    if (!gBot.saveDone) press(kCross);  // slot 1 -> "Save game file 1?"
    else {
        press(kCircle);                 // leave the page: back to the stairs / run-end menu
        gBot.saveDone = false;
        ++gBot.saves;
        gBot.floorsSinceSave = 0;
        gBot.savePending = false;
        logf("saved (save #%d) floor %d phase %s", gBot.saves, gBot.floor, kPhaseName[gBot.phase]);
    }
}

// ---- items ------------------------------------------------------------------------------------------------------
int itemValue(const Item& it, int cls) { return cls == kClsSD ? it.atkBase + it.atkMod : it.defBase + it.defMod; }

// pick an item action: returns the inventory index (primary context action 0 = Equip / Eat) or -1
int chooseItemUse(const Chr& p, const char** why) {
    Inventory& inv = inventory();
    const int n = inv.count;
    if (n <= 0) return -1;
    // floor 99: the Angelic Rifle in hand (story mode after E14 (flag 0x19): walk into the angel instead)
    if (gBot.floor == 99) {
        if (gBot.story) return -1;
        for (int i = 0; i < n; ++i) {
            const Item* it = inv.slots[i];
            if (it != nullptr && it->cls == kClsGN && inv.equippedSlotOfIndex(i) == -1 && inv.equipLegal(i, true, itemSystem())) {
                *why = "equip rifle";
                return i;
            }
        }
        return -1;
    }
    // better weapon / coat (never swap to the gun)
    for (int cls : {int(kClsSD), int(kClsCT)}) {
        const int slot = cls == kClsSD ? 0 : 2;
        const Item* cur = inv.equip[slot];
        int best = -1, bestV = cur != nullptr ? itemValue(*cur, cls) : -1000;
        for (int i = 0; i < n; ++i) {
            const Item* it = inv.slots[i];
            if (it == nullptr || it->cls != cls || inv.equippedSlotOfIndex(i) != -1) continue;
            if (!inv.equipLegal(i, true, itemSystem())) continue;
            const int v = itemValue(*it, cls);
            if (v > bestV) { bestV = v; best = i; }
        }
        if (best >= 0) {
            *why = cls == kClsSD ? "equip sword" : "equip coat";
            return best;
        }
    }
    // eat when VT is low (or HP low outside keep-hp); RUN1 past floor 103 plays carelessly (no eating)
    const StatusBlock& s = p.status.block;
    const bool careless = gBot.phase == kRun1 && gBot.floor >= 104;
    const bool hungry = s.vtMax > 0 && s.vt * 2 < s.vtMax;
    const bool hurt = !gBot.keepHp && s.hpMax > 0 && s.hp * 3 < s.hpMax;
    if (!careless && (hungry || hurt)) {
        for (int i = 0; i < n; ++i) {
            const Item* it = inv.slots[i];
            if (it != nullptr && (it->cls == kClsHT || it->cls == kClsMT || it->cls == kClsBN)) {
                *why = hungry ? "eat (VT low)" : "eat (HP low)";
                return i;
            }
        }
    }
    return -1;
}

void macroAbort(const char* why) {
    logf("item macro abort (%s) idx %d", why, gBot.macroIdx);
    gBot.macro = 0;
    gBot.nextItemCheck = now() + 600;
}

// drive the item screen: Triangle, cursor to the row, Cross (context, action 0), Cross (execute), close
void runMacro() {
    ItemScreen& is = itemScreen();
    const long t = now();
    if (t - gBot.macroStart > 400) {
        if (is.isOpen() && t - gBot.macroLast >= 8) {
            gBot.macroLast = t;
            press(is.contextOpen() ? kCircle : kTriangle);
            return;
        }
        if (!is.isOpen()) macroAbort("timeout");
        return;
    }
    if (t - gBot.macroLast < 6) return;
    switch (gBot.macro) {
    case 1:   // open
        if (is.isOpen()) {
            if (is.ready()) gBot.macro = 2;
            return;
        }
        press(kTriangle);
        gBot.macroLast = t;
        return;
    case 2: {   // cursor to the row
        if (!is.ready()) return;
        Inventory& inv = inventory();
        if (gBot.macroIdx >= inv.count || inv.slots[gBot.macroIdx] == nullptr || inv.slots[gBot.macroIdx]->id != gBot.macroItemId) {
            gBot.macro = 5;
            return;
        }
        if (is.cursor() == gBot.macroIdx) {
            gBot.macro = 3;
            return;
        }
        const int wantPage = gBot.macroIdx / 10;
        if (is.listPage() != wantPage) press(wantPage > is.listPage() ? kRight : kLeft);
        else press(kDown);
        gBot.macroLast = t;
        return;
    }
    case 3:   // context menu
        if (is.contextOpen()) {
            gBot.macro = 4;
            return;
        }
        press(kCross);
        gBot.macroLast = t;
        return;
    case 4:   // execute the primary action (context action 0)
        if (!is.contextOpen()) {
            gBot.macro = 5;
            return;
        }
        if (is.contextAction() != gBot.macroCtx) {
            press(gBot.macroCtx == 0 ? kUp : kDown);   // the context cursor wraps over its 4 rows
            gBot.macroLast = t;
            return;
        }
        {
            const Item* it = gBot.macroIdx < inventory().count ? inventory().slots[gBot.macroIdx] : nullptr;
            char id[5] = {};
            if (it != nullptr) std::memcpy(id, &it->id, 4);
            logf("item use: %s idx %d id %s cls %d (%s) context row %d", gBot.macroWhy, gBot.macroIdx, id, it ? it->cls : -1,
                 it ? itemDisplayName(*it, itemSystem()) : "?", gBot.macroCtx);
        }
        press(kCross);
        ++gBot.itemUses;
        gBot.macroLast = t;
        gBot.macro = 5;
        return;
    case 5:   // close
        if (!is.isOpen()) {
            gBot.macro = 0;
            gBot.nextItemCheck = t + 30;
            shot("item_done");
            return;
        }
        if (!is.ready()) return;
        press(is.contextOpen() ? kCircle : kTriangle);
        gBot.macroLast = t;
        return;
    default:
        gBot.macro = 0;
    }
}

// ---- dungeon behaviour --------------------------------------------------------------------------------------------
const Placement* exitPlacement() {
    Dungeon* d = gameWorld().dungeon;
    if (d == nullptr) return nullptr;
    const Placement* e = nullptr;
    for (const Placement& pl : d->placements())
        if (pl.kind == Placement::Kind::Stairs) e = &pl;
    return e;
}

Chr* findChrPrefix(const char* pfx, int type) {
    for (Chr* c : characters().list()) {
        if (c == nullptr || (c->taskFlags & 0x10000000u) != 0) continue;
        if (type >= 0 && c->type != type) continue;
        if (std::strncmp(c->idString().c_str(), pfx, std::strlen(pfx)) == 0) return c;
    }
    return nullptr;
}

void angelTick(Chr& p) {
    // the Angelic Rifle (GN00) must be the weapon: chooseItemUse equips it on floor 99
    Chr* angel = findChrPrefix("C19", 1);
    if (angel == nullptr) angel = findChrPrefix("C19", -1);
    if (angel == nullptr) {
        const Placement* e = exitPlacement();
        if (e != nullptr) walkTo(p, e->x, e->z);
        return;
    }
    const float dx = angel->xf.m.pos[0] - p.xf.m.pos[0], dz = angel->xf.m.pos[2] - p.xf.m.pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (inventory().weaponClass(itemSystem()) != 3) {
        if (d < 450.0f) steerDir(-dx, -dz);   // keep away while equipping
        return;
    }
    if (d > 650.0f) {
        float wx, wz;
        int len;
        pathStep(p.xf.m.pos[0], p.xf.m.pos[2], angel->xf.m.pos[0], angel->xf.m.pos[2], &wx, &wz, &len);
        walkTo(p, wx, wz);
        return;
    }
    if (d < 380.0f) {
        steerDir(-dx, -dz);
        return;
    }
    // face the angel, then fire (Square = AT01 with the gun, FUN_0013c4e0)
    const float fx = p.xf.m.fwd[0], fz = p.xf.m.fwd[2];
    const float facing = (fx * dx + fz * dz) / d;
    if (facing < 0.995f) {
        steerDir(dx, dz, 0.35f);
        return;
    }
    if (gBot.angelShotAt < 0 || now() - gBot.angelShotAt > 90) {
        gBot.angelShotAt = now();
        ++gBot.angelShots;
        press(kSquare);
        logf("angel: fire #%d dist %.0f facing %.3f", gBot.angelShots, double(d), double(facing));
        shot("angel_fire");
    }
}

// ---- story mode (--autoplay-story) -----------------------------------------------------------------------------------
// What the original needs for the tower redirects of FUN_0011a710 (table 0x48c7e0) to move past floor 116, worked out
// from the TLK / EVT scripts and the talk tables (work/reports/PLAYTHROUGH_STORY_2026-10-10.md):
//   flag 34 -> 35: IS01 (after-event FUN_001d2360 of E06 / E62 on floor 0x5c, reached by the hub exit PT12) given to
//                  C02A on floor 115 (talk C02_V401 / C02_V421, kind 2 category 0) -> EV03_02A / EV02_02A -> E82 / E08
//                  -> chained E09 (op 180) sets 35, clears 34.
//   flag 35 -> 36: positional talk C22B_V004 (kind 3: map 30 room 2, zone 4 = local (1000, 1500) r 250) on floor 120
//                  sets 0x18 / 36, clears 35; EV01_22A -> E13 -> after-event FUN_001d24c0 gives IS04 (only when C22B is
//                  spawned, RDT cond flag 201); with 0x18 set IS04 also lies in that room on later visits.
//   flag 36 -> 37: IS04 given to C03A on floor 128 (talk C03_V826, kind 2 category 6) sets 0x19 / 0x1c / 37, clears 36;
//                  EV02_03A -> E14.
//   flag 37: floor 140 -> 99; approaching the angel with flag 0x19 -> FUN_0011d120 -> E15 -> "Ending..".
constexpr uint32_t kIdIS01 = itemId("IS01"), kIdIS04 = itemId("IS04"), kIdGN00s = itemId("GN00");
bool hasItem(uint32_t id) { return inventory().hasId(id); }

Chr* itemEntity(uint32_t id) {
    for (Chr* c : characters().list())
        if (c != nullptr && c->type == 3 && c->item != nullptr && (c->taskFlags & 0x10000000u) == 0 && c->item->id == id) return c;
    return nullptr;
}

const Placement* stagePlacement(const char* tag) {
    Dungeon* d = gameWorld().dungeon;
    if (d == nullptr) return nullptr;
    uint32_t t = 0;
    std::memcpy(&t, tag, 4);
    for (const Placement& pl : d->placements())
        if (pl.kind == Placement::Kind::Stairs && pl.tag == t) return &pl;
    return nullptr;
}

const Room* roomOfMap(int mapId) {
    Dungeon* d = gameWorld().dungeon;
    if (d == nullptr) return nullptr;
    for (int i = 0; i < d->roomCount() && i < 16; ++i)
        if (d->room(i).placed() && d->room(i).rdt.roomId == mapId) return &d->room(i);
    return nullptr;
}

// walk to (x, z) along the cell grid (straight line when no path)
void goTo(Chr& p, float x, float z) {
    float wx, wz;
    int len;
    pathStep(p.xf.m.pos[0], p.xf.m.pos[2], x, z, &wx, &wz, &len);
    if (len <= 0) { wx = x; wz = z; }
    walkTo(p, wx, wz);
}

void watchFlags() {
    static const uint32_t kWatch[] = {0x5, 0xf, 0x10, 0x11, 0x12, 0x16, 0x17, 0x18, 0x19, 0x1c, 0x1d, 0x1e, 0x21, 0x22,
                                      0x23, 0x24, 0x25, 0x58, 0xc6, 0xc9, 0x2c8, 0x2c9, 0x2ca, 0x3bd, 0x3c4};
    uint32_t bits = 0;
    for (size_t i = 0; i < sizeof kWatch / sizeof kWatch[0]; ++i)
        if (flags().test(kWatch[i])) bits |= 1u << i;
    if (gBot.watchInit && bits == gBot.watchBits) return;
    std::string on;
    for (size_t i = 0; i < sizeof kWatch / sizeof kWatch[0]; ++i) {
        const bool now1 = ((bits >> i) & 1u) != 0, was = ((gBot.watchBits >> i) & 1u) != 0;
        if (gBot.watchInit && now1 == was) continue;
        if (!gBot.watchInit && !now1) continue;
        char b[24];
        std::snprintf(b, sizeof b, " %s0x%x", now1 ? "+" : "-", kWatch[i]);
        on += b;
    }
    logf("FLAGS%s%s (floor %d)", gBot.watchInit ? "" : " initial:", on.c_str(), gBot.floor);
    gBot.watchBits = bits;
    gBot.watchInit = true;
}

void startMacro(int idx, int ctx, const char* why) {
    gBot.macro = 1;
    gBot.macroIdx = idx;
    gBot.macroItemId = inventory().slots[idx]->id;
    gBot.macroStart = gBot.macroLast = now();
    gBot.macroWhy = why;
    gBot.macroCtx = ctx;
    logf("item macro start: %s idx %d context row %d", why, idx, ctx);
}

int inventoryIndex(uint32_t id) {
    for (int i = 0; i < inventory().count; ++i)
        if (inventory().slots[i] != nullptr && inventory().slots[i]->id == id) return i;
    return -1;
}

// a full inventory and a story item to pick up: put down (Place, context row 2) the first unequipped weapon / coat /
// wing / box / liquid that is not a story item
bool makeRoom(const char* why) {
    if (inventory().count < Inventory::kMaxItems) return false;
    for (int i = 0; i < inventory().count; ++i) {
        const Item* it = inventory().slots[i];
        if (it == nullptr || inventory().equippedSlotOfIndex(i) != -1) continue;
        if (it->cls == kClsSD || it->cls == kClsCT || it->cls == kClsWG || it->cls == kClsBX || it->cls == kClsLQ) {
            startMacro(i, 2, why);
            return true;
        }
    }
    return false;
}

// hand a story item to an NPC: walk up to it (FUN_0012c520 range 100 minus both radii, cone cosf(30)), then the item
// screen Give (context row 3, FUN_001d8d60 -> TH07 -> FUN_001d91a0) -> talk selector FUN_001a3320 category 2
bool giveTo(Chr& p, const char* npcId, uint32_t itemId4) {
    Chr* npc = findChrPrefix(npcId, 1);
    if (npc == nullptr) {
        if (now() % 600 == 0) logf("story: %s not on this floor", npcId);
        return false;
    }
    const int idx = inventoryIndex(itemId4);
    if (idx < 0) return false;
    const float dx = npc->xf.m.pos[0] - p.xf.m.pos[0], dz = npc->xf.m.pos[2] - p.xf.m.pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    const float reach = npc->shape.radius + p.shape.radius + 70.0f;
    if (now() % 300 == 0)
        logf("story: give to %s at (%.0f %.0f) dist %.0f reach %.0f", npcId, double(npc->xf.m.pos[0]), double(npc->xf.m.pos[2]),
             double(d), double(reach));
    if (d > reach) {
        goTo(p, npc->xf.m.pos[0], npc->xf.m.pos[2]);
        return true;
    }
    const float facing = (p.xf.m.fwd[0] * dx + p.xf.m.fwd[2] * dz) / std::max(d, 1.0f);
    if (facing < 0.9f) {
        steerDir(dx, dz, 0.3f);
        return true;
    }
    if (gBot.giftTryAt >= 0 && now() - gBot.giftTryAt < 240) return true;   // wait for the TH07 action / talk
    gBot.giftTryAt = now();
    ++gBot.giftTries;
    shot("give");
    static char why[48];
    std::snprintf(why, sizeof why, "give to %s (try %d)", npcId, gBot.giftTries);
    startMacro(idx, 3, why);
    return true;
}

// stand in front of an NPC (approach talk FUN_001a2d40: within 300 and the NPC's forward toward the player, dot > 0.68)
bool talkFront(Chr& p, const char* npcId) {
    Chr* npc = findChrPrefix(npcId, 1);
    if (npc == nullptr) return false;
    const float tx = npc->xf.m.pos[0] + npc->xf.m.fwd[0] * 170.0f, tz = npc->xf.m.pos[2] + npc->xf.m.fwd[2] * 170.0f;
    const float dx = tx - p.xf.m.pos[0], dz = tz - p.xf.m.pos[2];
    if (now() % 300 == 0) logf("story: talk in front of %s, spot (%.0f %.0f) dist %.0f", npcId, double(tx), double(tz),
                               double(std::sqrt(dx * dx + dz * dz)));
    if (std::sqrt(dx * dx + dz * dz) > 50.0f) goTo(p, tx, tz);
    return true;
}

// sword hits on an NPC (talk selector FUN_001a3320 category 1: the hit record's attacker is the player)
bool hitNpc(Chr& p, const char* npcId) {
    Chr* npc = findChrPrefix(npcId, 1);
    if (npc == nullptr) return false;
    const float dx = npc->xf.m.pos[0] - p.xf.m.pos[0], dz = npc->xf.m.pos[2] - p.xf.m.pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    const float reach = npc->shape.radius + p.shape.radius + 60.0f;
    if (now() % 300 == 0) logf("story: hit %s dist %.0f", npcId, double(d));
    if (d > reach) {
        goTo(p, npc->xf.m.pos[0], npc->xf.m.pos[2]);
        return true;
    }
    const float facing = (p.xf.m.fwd[0] * dx + p.xf.m.fwd[2] * dz) / std::max(d, 1.0f);
    if (facing < 0.95f) {
        steerDir(dx, dz, 0.3f);
        return true;
    }
    if (now() % 60 == 0) {
        press(kSquare);
        logf("story: Square at %s", npcId);
    }
    return true;
}

// story goals inside the tower; true when it drove the player this tick
bool storyDungeon(Chr& p) {
    if (!gBot.story) return false;
    const int fl = gBot.floor;
    FlagManager& f = flags();
    // floor 115: IS01 -> C02A (C02A is spawned until flag 0x11)
    if (fl == 115 && f.test(0x22) && !f.test(0x11) && hasItem(kIdIS01)) return giveTo(p, "C02A", kIdIS01);
    // floor 115: flag 6 (approach talks C02_V402 / V404 / V414; with 0x3c6 from E71 / E72 -> 0x3c7 -> flag 8 at the
    // run reset FUN_00178b30, which spawns C13B in the hub)
    if (fl == 115 && !f.test(6) && !f.test(0x10) && !f.test(8)) {
        if (talkFront(p, "C02A")) return true;
    }
    // floor 120: the C22B zone talk, then the IS04 lying in the map-30 room
    if (fl == 120 && f.test(0x23) && !f.test(0x18)) {
        const Room* r = roomOfMap(30);
        if (r == nullptr) {
            if (now() % 600 == 0) logf("story: floor 120 has no map-30 room");
            return false;
        }
        const float x = r->originX() + 1000.0f, z = r->originZ() + 1500.0f;
        if (now() % 300 == 0)
            logf("story: C22B zone (%.0f %.0f), player (%.0f %.0f)", double(x), double(z), double(p.xf.m.pos[0]), double(p.xf.m.pos[2]));
        goTo(p, x, z);
        return true;
    }
    if ((fl == 120 || fl == 128) && !hasItem(kIdIS04) && !f.test(0x19)) {
        if (Chr* is4 = itemEntity(kIdIS04)) {
            if (makeRoom("make room for IS04")) return true;
            if (now() % 300 == 0) logf("story: IS04 at (%.0f %.0f)", double(is4->xf.m.pos[0]), double(is4->xf.m.pos[2]));
            goTo(p, is4->xf.m.pos[0], is4->xf.m.pos[2]);
            return true;
        }
    }
    // floor 128: IS04 -> C03A (spawned until flag 0x19)
    if (fl == 128 && f.test(0x24) && !f.test(0x19) && hasItem(kIdIS04)) return giveTo(p, "C03A", kIdIS04);
    return false;
}

// floor 99 without the rifle (or after E14): walk into the angel (TypeId state FUN_001b3030: within 200 and moving
// toward it for more than 30 ticks -> FUN_0011d120)
void angelApproach(Chr& p) {
    Chr* angel = findChrPrefix("C19", 1);
    if (angel == nullptr) angel = findChrPrefix("C19", -1);
    if (angel == nullptr) return;
    const float dx = angel->xf.m.pos[0] - p.xf.m.pos[0], dz = angel->xf.m.pos[2] - p.xf.m.pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (now() % 150 == 0) logf("angel: approach dist %.0f", double(d));
    if (d > 300.0f) {
        goTo(p, angel->xf.m.pos[0], angel->xf.m.pos[2]);
        return;
    }
    steerDir(dx, dz);
}

void dungeonTick(Chr& p) {
    const long t = now();
    // item screen use
    if (gBot.macro != 0) {
        runMacro();
        return;
    }
    if (t >= gBot.nextItemCheck) {
        gBot.nextItemCheck = t + 90;
        const char* why = "";
        const int idx = chooseItemUse(p, &why);
        if (idx >= 0) {
            gBot.macro = 1;
            gBot.macroIdx = idx;
            gBot.macroItemId = inventory().slots[idx]->id;
            gBot.macroStart = gBot.macroLast = t;
            gBot.macroWhy = why;
            gBot.macroCtx = 0;
            logf("item macro start: %s idx %d", why, idx);
            return;
        }
    }
    if (gBot.floor == 99) {
        if (gBot.story) angelApproach(p);   // story: every branch of FUN_0011d120 (0x60 / E11 / E15) is wanted
        else angelTick(p);
        return;
    }
    // combat: nearest live enemy within 320 (same level)
    Chr* best = nullptr;
    float bestD = 320.0f;
    for (Chr* c : characters().list()) {
        if (c == nullptr || c->type != 2 || (c->taskFlags & 0x10000000u) != 0 || c->status.block.hp <= 0) continue;
        if (std::fabs(c->xf.m.pos[1] - p.xf.m.pos[1]) > 180.0f || ignored(c)) continue;
        const float d = dist2(*c, p);
        if (d < bestD) { bestD = d; best = c; }
    }
    if (best != nullptr) {
        if (best != gBot.foe) {
            gBot.foe = best;
            gBot.foeSince = t;
            gBot.foeHp = best->status.block.hp;
            gBot.foeHpTick = t;
        }
        if (best->status.block.hp < gBot.foeHp) {
            gBot.foeHp = best->status.block.hp;
            gBot.foeHpTick = t;
        }
        if (t - gBot.foeHpTick > 900) {   // no damage dealt for 30 s: leave it
            logf("combat: give up on %s hp %d", best->idString().c_str(), best->status.block.hp);
            gBot.ignoreUntil[best] = t + 1800;
            gBot.foe = nullptr;
        } else {
            press(kL2);   // lock-on held (0x4e65f8)
            const float dx = best->xf.m.pos[0] - p.xf.m.pos[0], dz = best->xf.m.pos[2] - p.xf.m.pos[2];
            if (bestD > 130.0f) {
                if (t % 10 == 0) press(kSquare);
                steerDir(dx, dz);
            } else {
                if (t % 9 == 0) press(kSquare);
                else if (t % 9 >= 4 && t % 9 <= 5) steerDir(dx, dz, 0.4f);
            }
            resetUnstick();
            return;
        }
    }
    gBot.foe = nullptr;
    if (storyDungeon(p)) return;
    // items lying near the route (inventory not full)
    if (inventory().count < Inventory::kMaxItems) {
        Chr* it = nullptr;
        int itLen = 1 << 30;
        float iwx = 0, iwz = 0;
        for (Chr* c : characters().list()) {
            if (c == nullptr || c->type != 3 || c->item == nullptr || (c->taskFlags & 0x10000000u) != 0 || ignored(c)) continue;
            if (std::fabs(c->xf.m.pos[1] - p.xf.m.pos[1]) > 120.0f) continue;
            if (dist2(*c, p) > 2400.0f) continue;
            float wx, wz;
            int len;
            pathStep(p.xf.m.pos[0], p.xf.m.pos[2], c->xf.m.pos[0], c->xf.m.pos[2], &wx, &wz, &len);
            if (len < 0 || len > 14) continue;
            if (len < itLen) { itLen = len; it = c; iwx = wx; iwz = wz; }
        }
        if (it != nullptr) {
            if (it != gBot.itemTarget) {
                gBot.itemTarget = it;
                gBot.itemSince = t;
            }
            if (t - gBot.itemSince > 450) {
                logf("item: give up on %s", it->idString().c_str());
                gBot.ignoreUntil[it] = t + 1000000;
                gBot.itemTarget = nullptr;
            } else {
                if (itLen <= 1) walkTo(p, it->xf.m.pos[0], it->xf.m.pos[2]);
                else walkTo(p, iwx, iwz);
                return;
            }
        }
    }
    gBot.itemTarget = nullptr;
    // the exit (PT00 / PT14)
    const Placement* e = exitPlacement();
    const int fid = gBot.floor;
    if (e == nullptr && (fid == 0x8b || fid == 0xd8 || fid == 0xde || fid == 0xe6) && gameWorld().dungeon != nullptr) {
        // hole floors (FUN_0011c220) have no stairs: FUN_0013b0d0 starts event 0x32 (E50 -> descend menu) when the
        // player stands in the room of kind ' ' (RDT byte 0) at local x 1100..1300, local z < 1400, moving toward -z
        // for more than 30 ticks: walk to local (1200, 1300), then hold the stick toward -z
        Dungeon& d = *gameWorld().dungeon;
        for (int i = 0; i < d.roomCount() && i < 16; ++i) {
            if (d.room(i).rdt.roomId != 0x20) continue;
            const float ox = float(int(d.room(i).posX) * 0x640 + 100), oz = float(int(d.room(i).posY) * 0x640 + 100);
            const float lx = p.xf.m.pos[0] - ox, lz = p.xf.m.pos[2] - oz;
            const uint32_t cell = p.cell != nullptr ? *p.cell : 0u;
            const bool inRoom = (cell & 0xf000u) == 0x1000u && int(cell & 0xfu) == i;
            if (t % 300 == 150)
                logf("hole floor: room %d origin (%.0f %.0f) local (%.0f %.0f) in room %d", i, double(ox), double(oz), double(lx),
                     double(lz), int(inRoom));
            if (inRoom && lx > 1120.0f && lx < 1280.0f && lz < 1400.0f) {
                steerDir(0.0f, -1.0f);
                return;
            }
            float wx, wz;
            int len;
            pathStep(p.xf.m.pos[0], p.xf.m.pos[2], ox + 1200.0f, oz + 1300.0f, &wx, &wz, &len);
            if (len <= 0) { wx = ox + 1200.0f; wz = oz + 1300.0f; }
            walkTo(p, wx, wz);
            return;
        }
    }
    if (e == nullptr) {
        // arena floors without stairs: the warp pad O7xC moves the player onto the boss spawner O0?Q; once its children
        // are dead FUN_001c03e0 removes the spawner and (floor 0x8c) creates the stage object PT00 at room 0 + (800, 1000)
        const bool spawnerLeft = findChrPrefix("O0", -1) != nullptr && std::strchr(findChrPrefix("O0", -1)->idString().c_str(), 'Q');
        Chr* q = nullptr;
        for (Chr* c : characters().list())
            if (c != nullptr && (c->taskFlags & 0x10000000u) == 0 && c->idString().size() == 4 && c->idString()[0] == 'O' &&
                c->idString()[3] == 'Q')
                q = c;
        (void)spawnerLeft;
        if (q == nullptr && gBot.floor == 0x8c && gameWorld().dungeon != nullptr) {
            const Room& r0 = gameWorld().dungeon->room(0);
            const float ex = float(r0.posX * 0x640 + 100) + 800.0f, ez = float(r0.posY * 0x640 + 100) + 1000.0f;
            if (t % 300 == 150) logf("arena cleared: walking to the stage PT00 at (%.0f %.0f)", double(ex), double(ez));
            float wx, wz;
            int len;
            pathStep(p.xf.m.pos[0], p.xf.m.pos[2], ex, ez, &wx, &wz, &len);
            if (len <= 0) { wx = ex; wz = ez; }
            walkTo(p, wx, wz);
            return;
        }
        Chr* pad = findChrPrefix("O7", -1);
        if (pad == nullptr) pad = findChrPrefix("O8", -1);
        if (pad == nullptr) pad = findChrPrefix("O9", -1);
        if (pad != nullptr) {
            if (t % 300 == 150) logf("arena: walking to warp pad %s", pad->idString().c_str());
            float wx, wz;
            int len;
            pathStep(p.xf.m.pos[0], p.xf.m.pos[2], pad->xf.m.pos[0], pad->xf.m.pos[2], &wx, &wz, &len);
            if (len <= 0) { wx = pad->xf.m.pos[0]; wz = pad->xf.m.pos[2]; }
            walkTo(p, wx, wz);
        } else if (t % 300 == 150) {
            logf("no exit and no warp pad on floor %d", gBot.floor);
        }
        return;
    }
    float wx, wz;
    int len;
    pathStep(p.xf.m.pos[0], p.xf.m.pos[2], e->x, e->z, &wx, &wz, &len);
    if (len == 0) { wx = e->x; wz = e->z; }
    if (t % 300 == 150) {
        const char* a = p.act.currentName();
        logf("exit (%.0f %.0f) path len %d next (%.0f %.0f) still %d strike %d side %d state %d act %s ctl %x cam fwd (%.2f %.2f %.2f)",
             double(e->x), double(e->z), len, double(wx), double(wz), gBot.still, gBot.strikeTicks, gBot.sideTicks, int(p.sm.cur),
             a ? a : "-", playerCtl() ? playerCtl()->flags : 0u, playerCtl() && playerCtl()->camera ? double(playerCtl()->camera->forward[0]) : 0.0,
             playerCtl() && playerCtl()->camera ? double(playerCtl()->camera->forward[1]) : 0.0,
             playerCtl() && playerCtl()->camera ? double(playerCtl()->camera->forward[2]) : 0.0);
    }
    walkTo(p, wx, wz);
}

// Hub goals for the story (the gate J019 in front of PT12 is spawned until flag 0xe, RDT MO50_00 cond -14):
//   flag 8 (C13B spawned): hit C13B twice (C13B_V006 -> 9, C13B_V007 -> 0xa, it dies); IS00 then lies at its spot
//   (cond 10) from the next hub load; IS00 handed to C10A (C10A_V018, category 0x1d) -> 0xd -> 0xe at the next run
//   reset. A run is ended in the hub by standing still (VT 0, then HP 0 -> FUN_001cdcd0 -> FUN_0011ce60).
enum HubGoal { kGoPT10, kGoPT12, kHitC13B, kPickIS00, kGiveC10A, kEndRun };
const char* const kHubGoalName[] = {"PT10", "PT12", "hit C13B", "pick IS00", "give IS00 to C10A", "end the run (idle)"};
int hubGoal() {
    FlagManager& f = flags();
    const uint32_t kIS00 = itemId("IS00");
    if (!f.test(0xe)) {
        if (hasItem(kIS00) && !f.test(0xd) && findChrPrefix("C10A", 1) != nullptr) return kGiveC10A;
        if ((f.test(0xa) || f.test(0xc)) && !f.test(0xd) && itemEntity(kIS00) != nullptr) return kPickIS00;
        if (f.test(8) && !f.test(0xa) && !f.test(0xc) && findChrPrefix("C13B", 1) != nullptr) return kHitC13B;
        if (f.test(0xd) || f.test(0xa) || f.test(0xc)) return kEndRun;
    }
    if (f.test(0x22) && !hasItem(kIdIS01) && stagePlacement("PT12") != nullptr && f.test(0xe)) return kGoPT12;
    return kGoPT10;
}

void hubTick(Chr& p) {
    static int lastGoal = -1;
    const int goal = gBot.story ? hubGoal() : kGoPT10;
    if (goal != lastGoal) {
        logf("hub goal: %s", kHubGoalName[goal]);
        lastGoal = goal;
    }
    if (goal == kHitC13B) { hitNpc(p, "C13B"); return; }
    if (goal == kPickIS00) {
        if (Chr* it = itemEntity(itemId("IS00"))) goTo(p, it->xf.m.pos[0], it->xf.m.pos[2]);
        return;
    }
    if (goal == kGiveC10A) { giveTo(p, "C10A", itemId("IS00")); return; }
    if (goal == kEndRun) return;
    // story: the hub exit PT12 (-780, 2700) to floor 0x5c while the IS01 errand is open. Placed by FUN_001222d0 unless
    // flag 0x2c8 (IS01 carried, FUN_001d5ef0) or 0x12 (E09 seen); its message 0x36 is answered YES.
    if (goal == kGoPT12) {
        if (const Placement* pt12 = stagePlacement("PT12")) {
            if (now() % 300 == 0)
                logf("hub: to PT12 (%.0f %.0f), player (%.0f %.0f)", double(pt12->x), double(pt12->z), double(p.xf.m.pos[0]),
                     double(p.xf.m.pos[2]));
            goTo(p, pt12->x, pt12->z);
            return;
        }
    }
    // route from the hub start to the tower gate PT10 (z < -2100 at x -400, PLAYTHROUGH_A stage 3)
    static const float wp[][2] = {{-600.0f, 600.0f}, {-400.0f, -900.0f}, {-400.0f, -2400.0f}};
    const int n = int(sizeof wp / sizeof wp[0]);
    if (gBot.hubWp >= n) return;
    const float dx = wp[gBot.hubWp][0] - p.xf.m.pos[0], dz = wp[gBot.hubWp][1] - p.xf.m.pos[2];
    if (std::sqrt(dx * dx + dz * dz) < 60.0f) {
        logf("hub: waypoint %d reached", gBot.hubWp);
        ++gBot.hubWp;
        resetUnstick();
        return;
    }
    walkTo(p, wp[gBot.hubWp][0], wp[gBot.hubWp][1]);
}

void memLog() {
    size_t ws = 0, pb = 0;
    unsigned long h = 0, gdi = 0, user = 0;
    autoplayProcessMemory(&ws, &pb, &h, &gdi, &user);
    int tex = 0, meshes = 0;
    alchemy::gpuLiveCounts(&tex, &meshes);
    logf("mem ws=%.1fMB private=%.1fMB handles=%lu gdi=%lu user=%lu chrs=%zu inv=%d floor=%d gltex=%d glmesh=%d",
         double(ws) / 1048576.0, double(pb) / 1048576.0, h, gdi, user, characters().list().size(), inventory().count, gBot.floor,
         tex, meshes);
}

void observe() {
    const long t = now();
    // scene changes
    const std::string sc = sceneNameString(scenes().current());
    if (sc != gBot.lastScene) {
        logf("scene %s -> %s", gBot.lastScene.c_str(), sc.c_str());
        gBot.lastScene = sc;
        if (sc.rfind("Title", 0) == 0) shot("title");
        if (sc.rfind("GameLoad", 0) == 0) shot("gameload");
    }
    if (gBot.lastMemLog < 0 || t - gBot.lastMemLog >= 5000) {
        gBot.lastMemLog = t;
        memLog();
    }
    const int bgm = sound().bgmEntry();
    if (bgm != gBot.lastBgm) {
        logf("bgm entry %d -> %d", gBot.lastBgm, bgm);
        gBot.lastBgm = bgm;
    }
    const bool ev = eventActive();
    {
        static int lastTalk = -2;
        const int ti = talkManager().active != 0 ? talkManager().talkIndex : -1;
        if (ti != lastTalk) {
            if (ti >= 0 && ti < data::kTalkTableCount)
                logf("TALK %s (idx %d, speaker %s, category %d)", data::talkTable()[ti].file, ti, data::talkTable()[ti].speaker,
                     talkManager().category);
            lastTalk = ti;
        }
    }
    if (ev != gBot.lastEvent) {
        gBot.lastEvent = ev;
        logf("event %s (floor %d, trigger %d)", ev ? "start" : "end", gBot.floor, eventManager().triggerId);
        if (ev) shot("event_start");
        gBot.eventStart = ev ? t : -1;
    }
    if (ev && gBot.eventStart >= 0 && (t - gBot.eventStart == 240 || t - gBot.eventStart == 900)) shot("event_mid");
    if (!gameActive()) return;
    if (gBot.story) watchFlags();
    const int fl = gameProgress().floor;
    if (fl != gBot.floor) {
        logf("FLOOR %d -> %d (phase %s, level %d)", gBot.floor, fl, kPhaseName[gBot.phase],
             characters().player() ? characters().player()->status.block.level : -1);
        if (gBot.floor >= 100 && fl >= 100 && fl != gBot.floor) gBot.floorsSinceSave++;
        const int prevFl = gBot.floor;
        gBot.floor = fl;
        gBot.floorTick = t;
        ++gBot.floorsVisited;
        if (fl >= 100 && fl > gBot.deepest) gBot.deepest = fl;
        if (fl == 99) gBot.deepest = std::max(gBot.deepest, 141);
        if (const Placement* e = exitPlacement()) {
            char tg[5] = {};
            std::memcpy(tg, &e->tag, 4);
            logf("floor %d exit %s at (%.0f %.0f %.0f)", fl, tg, double(e->x), double(e->y), double(e->z));
        } else {
            logf("floor %d: no exit placement", fl);
        }
        gBot.ignoreUntil.clear();
        gBlockedEdges.clear();
        gBot.foe = nullptr;
        gBot.itemTarget = nullptr;
        gBot.angelShotAt = -1;
        gBot.hubWp = 0;
        resetUnstick();
        if (gBot.floorsSinceSave >= gBot.saveEvery || (gBot.phase == kRun1 && gBot.saves == 0 && fl >= 101))
            gBot.savePending = true;
        if (gBot.phase == kDescent && prevFl == 99 && fl != 99 && !gBot.story) {
            gBot.clearedSeen = true;
            logf("ANGEL FLOOR LEFT -> floor %d (run end after the angel)", fl);
        }
        if (fl == 0x5f && gBot.phase == kDescent && !gBot.story) {
            gBot.clearedSeen = true;
            logf("DUNGEON CLEARED -> floor 0x5f (new cycle)");
        }
    }
    if (t == gBot.floorTick + 45) {
        char w[32];
        std::snprintf(w, sizeof w, "arrive");
        shot(w);
    }
    if (const Chr* p = characters().player()) {
        const StatusBlock& s = p->status.block;
        if (s.level != gBot.lastLevel) {
            if (gBot.lastLevel >= 0) logf("LEVEL %d -> %d (hp max %d)", gBot.lastLevel, s.level, s.hpMax);
            gBot.lastLevel = s.level;
        }
        const uint32_t st = s.statusMask & ~0x10000u;
        if (st != gBot.lastStatus) {
            logf("status mask 0x%x -> 0x%x", gBot.lastStatus, st);
            gBot.lastStatus = st;
        }
        if (inventory().count != gBot.lastInvCount) {
            std::string ids;
            for (int i = 0; i < inventory().count; ++i)
                if (const Item* it = inventory().slots[i]) {
                    char tg[5] = {};
                    std::memcpy(tg, &it->id, 4);
                    ids += tg;
                    ids += inventory().equippedSlotOfIndex(i) >= 0 ? "* " : " ";
                }
            logf("inventory: %s", ids.c_str());
            logf("inventory %d -> %d%s", gBot.lastInvCount, inventory().count,
                 inventory().count >= Inventory::kMaxItems ? " (FULL)" : "");
            gBot.lastInvCount = inventory().count;
        }
        if (s.hp <= 0 && !gBot.deadLogged && (p->sm.cur == StateId::DeathA || p->sm.cur == StateId::DeathB)) {
            gBot.deadLogged = true;
            ++gBot.deaths;
            logf("DEATH #%d on floor %d (level %d)", gBot.deaths, gBot.floor, s.level);
            shot("death");
            if (gBot.phase == kRun1) {
                gBot.phase = kDead;
                gBot.savedThisMenu = false;
                gBot.titleChosen = false;
            }
        }
        if (s.hp > 0) gBot.deadLogged = false;
        if (t % 300 == 0)
            logf("pos (%.0f %.0f %.0f) hp %d/%d vt %d/%d lv %d foe %s item %s macro %d", double(p->xf.m.pos[0]), double(p->xf.m.pos[1]),
                 double(p->xf.m.pos[2]), s.hp, s.hpMax, s.vt, s.vtMax, s.level, gBot.foe ? gBot.foe->idString().c_str() : "-",
                 gBot.itemTarget ? gBot.itemTarget->idString().c_str() : "-", gBot.macro);
    }
}

}  // namespace

void autoplayOptions(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--autoplay") == 0 && i + 1 < argc) {
            gBot.on = true;
            gBot.from = std::atol(argv[i + 1]);
        }
        if (std::strcmp(argv[i], "--autoplay-save-every") == 0 && i + 1 < argc) gBot.saveEvery = std::max(1, std::atoi(argv[i + 1]));
        if (std::strcmp(argv[i], "--autoplay-shots") == 0 && i + 1 < argc) gBot.shotDir = argv[i + 1];
        if (std::strcmp(argv[i], "--autoplay-keep-hp-now") == 0) gBot.keepHp = true;
        if (std::strcmp(argv[i], "--autoplay-max-stall") == 0 && i + 1 < argc) gBot.maxStall = std::atol(argv[i + 1]);
        if (std::strcmp(argv[i], "--autoplay-phase") == 0 && i + 1 < argc) gBot.phase = std::clamp(std::atoi(argv[i + 1]), 0, 4);
        if (std::strcmp(argv[i], "--autoplay-story") == 0) gBot.story = true;
    }
    if (gBot.on) logf("enabled from tick %ld, save every %d floors, shots %s", gBot.from, gBot.saveEvery, gBot.shotDir.c_str());
}

const char* autoplayNextShot() {
    if (gBot.shots.empty()) return nullptr;
    gBot.shotBuf = gBot.shots.front();
    gBot.shots.pop_front();
    return gBot.shotBuf.c_str();
}

bool autoplayFinished() { return gBot.finished; }

void autoplayTick() {
    if (!gBot.on || gBot.finished) return;
    const long t = now() + 1;   // injected input applies to the next pad read
    if (t < gBot.from) return;
    observe();
    if (gBot.keepHp && gameActive() && !(gBot.story && (gBot.floor == 0x5b || gBot.floor == 0x5c))) {   // PORT stand-in for a skilled player (same as main.cpp --keep-hp)
        if (Chr* p = characters().player(); p != nullptr && 0 < p->status.block.hp) {
            p->status.block.hp = p->status.block.hpMax;
            p->status.block.statusMask |= 0x10000u;
        }
    }
    if (gBot.finishAt >= 0 && now() >= gBot.finishAt) {
        logf("FINISHED: phase %s deepest floor %d floors visited %d deaths %d saves %d item uses %d angel shots %d", kPhaseName[gBot.phase],
             gBot.deepest, gBot.floorsVisited, gBot.deaths, gBot.saves, gBot.itemUses, gBot.angelShots);
        memLog();
        shot("final");
        gBot.finished = true;
        return;
    }
    if (gBot.clearedSeen && gBot.finishAt < 0) {
        gBot.phase = kDone;
        gBot.finishAt = now() + 900;
    }
    if (gameActive() && now() - gBot.floorTick > gBot.maxStall && gBot.finishAt < 0) {
        logf("STALL: floor %d unchanged for %ld ticks", gBot.floor, gBot.maxStall);
        shot("stall");
        gBot.finishAt = now() + 2;
    }
    // modal UI first
    if (ui::messageBox().isOpen()) {
        handleBox();
        return;
    }
    gBot.lastBoxId = 0;
    gBot.boxOpenedAt = 0;
    if (ui::menu().isOpen()) {
        handleFilePage();
        return;
    }
    // scenes outside the Game
    if (scenes().isCurrent("Title...")) {
        if (gBot.phase == kDead || gBot.phase == kContinue) {
            if (gBot.phase == kDead) {
                gBot.phase = kContinue;
                logf("phase CONTINUE");
            }
            if (t % 40 == 0) press(kStart);
        }
        return;
    }
    if (scenes().isCurrent("GameLoad")) return;   // its file page is handled above (menu open)
    if (!gameActive()) return;
    Chr* p = characters().player();
    if (p == nullptr || p->status.block.hp <= 0) return;
    if (gBot.macro != 0 && !eventActive()) {   // the item screen pauses the engine: drive it first
        runMacro();
        return;
    }
    if (itemScreen().isOpen()) {   // opened by something else: close it
        if (t % 20 == 0) press(itemScreen().contextOpen() ? kCircle : kTriangle);
        return;
    }
    if (eventActive() || fade().frames != 0 || engine().paused()) return;
    if (talkManager().active != 0) {
        if (talkManager().state == 1 && t % 30 == 0) press(kCross);   // talk choice: first answer
        return;
    }
    if (gBot.phase == kContinue) {
        gBot.phase = gBot.floor == 0x5b ? kHub : kDescent;
        if (gBot.phase == kDescent) gBot.keepHp = true;   // continued mid-tower: the descent phase
        gBot.titleChosen = false;
        gBot.savedThisMenu = false;
        logf("continued: floor %d -> phase %s", gBot.floor, kPhaseName[gBot.phase]);
        shot("continued");
    }
    if (gBot.phase == kHub && gBot.floor >= 100) {
        gBot.phase = kDescent;
        gBot.keepHp = true;
        gBot.floorsSinceSave = 0;
        logf("phase DESCENT (keep-hp on)");
    }
    if (gBot.floor == 0x5b) {
        if (gBot.phase == kHub || gBot.phase == kDescent || gBot.phase == kRun1 || gBot.phase == kDead) hubTick(*p);
        return;
    }
    if (gBot.floor == 0x5c && gBot.story) {
        // floor 0x5c with flag 0xf: FUN_001229d0 places IS01 at (0, 100, 0) and the stage PT13 linked to it (FUN_0017bb30:
        // picking it up starts the warp chain back to the hub); without 0xf the event E06 / E62 runs on arrival
        if (Chr* is1 = itemEntity(kIdIS01)) {
            if (t % 300 == 0) logf("0x5c: walking to IS01 (%.0f %.0f)", double(is1->xf.m.pos[0]), double(is1->xf.m.pos[2]));
            walkTo(*p, is1->xf.m.pos[0], is1->xf.m.pos[2]);
        }
        return;
    }
    if (gBot.floor >= 100 || gBot.floor == 99) dungeonTick(*p);
}

}  // namespace baroque::game
