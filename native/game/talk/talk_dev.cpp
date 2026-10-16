// PORT dev options of the talk runtime (not original behaviour; verification only):
//   --talk SPEAKER      on the first Game tick whose floor has an NPC with that 4-char id, the player is placed 150 units
//                       in front of it, facing it, so the approach selector FUN_001a2d40 starts its talk normally.
//   --talk-name NAME    additionally plays talk NAME through FUN_001a73a0 (the VOICE LIST path) right after placing.
//                       Without --talk it is played on the 30th Game tick of the first floor (no NPC needed).
//   --talk-trace        log every talk state change, every text-window slot 3 text (rect, style, speed, scale, wrap
//                       audit: lines / widest line / bottom against the rect, same wrap rule as FUN_0015c710), the
//                       speaker name plate and the voice status (conversation sweep, tools/sweep_talks).
//   --talk-batch FILE   play the talk names of FILE (one per line) one after another through FUN_001a73a0, each to its
//                       end (or 2400 ticks), 10 ticks apart; logs "talk batch ..." lines (implies --talk-trace).
//   --trace-pos N       every N engine ticks while the Game scene runs: log tick, floor, player position / forward /
//                       HP / VT and the talk manager state (scripted playthrough verification).
//   --press-every MASK FROM TO STEP   (repeatable) scripted pad bits on every STEP-th engine tick in FROM..TO-1 (STEP 1 =
//                       held); --stick-more LX LY FROM TO (repeatable) scripted left stick like main.cpp --stick. Both
//                       are injected from the engine tick and apply to the next pad read; --shot runs only.
//   --goto X Z FROM     (repeatable, processed in order) steer the left stick toward world (X, Z) from tick FROM on, as a
//                       player would: the stick is derived from the gameplay camera axes the movement code uses
//                       (FUN_0013cf90: move = camFwd * sy + camRight * sx), full tilt; a waypoint ends within 40 units
//                       (or after 300 ticks, or when the floor changes) and the next one starts. Logged as "trace goto".
#include "game/talk/talk_wiring.hpp"

#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/camera.hpp"
#include "game/engine.hpp"
#include "game/event/event_manager.hpp"
#include "game/flag_manager.hpp"
#include "game/free_camera.hpp"
#include "game/player_move.hpp"
#include "game/progress/progression.hpp"
#include "game/game_world.hpp"
#include "game/scenes/game_scene.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "platform/input.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace baroque::game {

namespace {
std::string gSpeaker;      // --talk
std::string gName;         // --talk-name
int gWait = 0;
bool gDone = false;
int gTraceEvery = 0;       // --trace-pos
std::vector<uint32_t> gTraceFlags;   // --trace-flag N (repeatable): flag states appended to the trace line
struct PressEvery { uint32_t mask; long from, to, step; };
struct StickMore { int lx, ly; long from, to; };
std::vector<PressEvery> gPressEvery;   // --press-every
std::vector<StickMore> gStickMore;     // --stick-more
struct Goto { float x, z; long from; long started = -1; bool done = false; int floor = -1; };
std::vector<Goto> gGoto;               // --goto
bool gShotRun = false;
const void* gTraceDungeon = nullptr;   // --trace-pos: placements dumped once per loaded floor
int gTraceFloor = -1;

// --talk-trace / --talk-batch (PORT, conversation sweep)
bool gTalkTrace = false;
std::vector<std::string> gBatch;       // --talk-batch names
size_t gBatchNext = 0;
long gBatchStart = -1;                 // tick the current batch talk was started (-1 = none running)
long gBatchGapUntil = 0;
bool gBatchDone = false;
struct TraceSnap {
    int state = -1, active = -1, talkIndex = -2, nameIndex = -2, layout = -1, voiceStatus = -1, cat = -2;
    std::string text;
    int textStyle = -1;
    long gateT = -1;
};
TraceSnap gSnap;

// The wrap rule of FUN_0015c710 / FUN_0015cac0 over the whole slot text (every character visible): number of lines,
// widest drawn line, and how far the last line reaches (y for style 0, x for the rotated style 1).
void auditSlot(int slot, int& lines, float& widest, float& reach) {
    ui::HelpBar& hb = ui::helpBar();
    ui::TextRenderer& t = ui::text();
    const float saveW = t.halfW, saveH = t.halfH;
    const int saveP = t.proportional;
    t.halfW = hb.scaleW(slot) * 8.0f;
    t.halfH = hb.scaleH(slot) * 9.0f;
    t.proportional = hb.proportional(slot);
    const char* src = hb.slotText(slot);
    const int len = static_cast<int>(std::strlen(src));
    const bool rot = hb.style(slot) == 1;
    const float limit = rot ? hb.h(slot) : hb.w(slot);
    const float step = ui::language() == 1 ? hb.scaleH(slot) * 9.0f * 2.0f : hb.scaleH(slot) * 9.0f * 2.0f * 0.85f;
    char line[130] = {};
    int bufLen = 0;
    float width = 0.0f;
    lines = 0;
    widest = 0.0f;
    for (int idx = 0; idx < len + 1; ++idx) {
        line[bufLen] = src[idx];
        line[bufLen + 1] = '\0';
        bool flush = false;
        if (src[idx] != '\n' && src[idx] != '\0') {
            if (((idx + 1) & 1) == 0) {
                if (limit < width && src[idx + 1] != '\n') flush = true;
                else ++bufLen;
            } else ++bufLen;
        } else flush = true;
        if (flush) {
            ++lines;
            widest = std::max(widest, t.measure("%s\n", line));
            bufLen = 0;
        }
        width = t.measure("%s\n", line);
        if (bufLen >= 126) break;
    }
    reach = (rot ? hb.x(slot) : hb.y(slot)) + float(lines) * step;
    t.halfW = saveW;
    t.halfH = saveH;
    t.proportional = saveP;
}

std::string oneLine(const char* s) {
    std::string r;
    for (; *s != '\0'; ++s) r += *s == '\n' ? '|' : *s;
    return r;
}

void talkTraceTick() {
    TalkManager& m = talkManager();
    TalkWorld* w = talkWorld();
    if (w == nullptr) return;
    ui::HelpBar& hb = ui::helpBar();
    const long t = long(engine().tickCount);
    char buf[512];
    const int vs = w->voiceStatus();
    if (m.state != gSnap.state || m.active != gSnap.active || m.talkIndex != gSnap.talkIndex || m.category != gSnap.cat) {
        std::snprintf(buf, sizeof buf, "talk trace t=%ld state=%d active=%d idx=%d file=%s cat=%d spk=%.4s frame=%d noVoice=%d voice=%d vs=%d",
                      t, m.state, m.active, m.talkIndex, m.talkIndex >= 0 ? talkFileName(m.talkIndex) : "-", m.category,
                      reinterpret_cast<const char*>(&m.speaker), m.frame, m.noVoice, m.voiceId, vs);
        platform::log(buf);
    }
    if (m.active != 0 && (t % 60 == 0 || t - gSnap.gateT >= 60 || gSnap.gateT < 0)) {   // FUN_001a1dc0 gates while a talk is up
        gSnap.gateT = t;
        std::snprintf(buf, sizeof buf,
                      "talk gates t=%ld item=%d menu=%d paused=%d msg=%d floor&1=%u event=%d mode=%d cooldown=%d enabled=%d player=%d hp=%d ctl=%08x frame=%d volume=%.2f alpha3=%.2f facing=%d",
                      t, int(w->itemScreenOpen()), int(w->systemMenuOpen()), int(w->enginePaused()), int(w->messageBoxOpen()),
                      w->floorFlags() & 1u, int(w->eventRunning()), w->eventMode(), m.cooldown, m.enabled,
                      int(w->player() != nullptr), w->player() != nullptr ? w->actorHp(w->player()) : -1, w->playerControlFlags(),
                      m.frame, double(m.volume), double(hb.alpha(3)), m.facing);
        platform::log(buf);
    }
    if (vs != gSnap.voiceStatus && m.active != 0) {
        std::snprintf(buf, sizeof buf, "talk trace t=%ld voiceStatus %d -> %d frame=%d", t, gSnap.voiceStatus, vs, m.frame);
        platform::log(buf);
    }
    if (m.nameIndex != gSnap.nameIndex) {
        std::snprintf(buf, sizeof buf, "talk trace t=%ld name=%d \"%s\"", t, m.nameIndex,
                      m.nameIndex >= 0 ? ui::str(m.nameIndex + 0x16d) : "");
        platform::log(buf);
    }
    if (m.layout != gSnap.layout) {
        std::snprintf(buf, sizeof buf, "talk trace t=%ld layout=%d", t, m.layout);
        platform::log(buf);
    }
    const std::string txt = hb.slotText(3);
    if (txt != gSnap.text || hb.style(3) != gSnap.textStyle) {
        if (txt.empty()) {
            std::snprintf(buf, sizeof buf, "talk text t=%ld frame=%d cleared", t, m.frame);
            platform::log(buf);
        } else {
            int lines = 0;
            float widest = 0.0f, reach = 0.0f;
            auditSlot(3, lines, widest, reach);
            std::snprintf(buf, sizeof buf,
                          "talk text t=%ld frame=%d rect=(%.0f %.0f %.0f %.0f) style=%d speed=%d scale=(%.2f %.2f) prop=%d col=%06x lines=%d widest=%.0f reach=%.0f text=",
                          t, m.frame, double(hb.x(3)), double(hb.y(3)), double(hb.w(3)), double(hb.h(3)), hb.style(3), hb.speed(3),
                          double(hb.scaleW(3)), double(hb.scaleH(3)), hb.proportional(3), hb.color(3) & 0xffffffu, lines,
                          double(widest), double(reach));
            platform::log(std::string(buf) + oneLine(txt.c_str()));
        }
    }
    gSnap.state = m.state;
    gSnap.active = m.active;
    gSnap.talkIndex = m.talkIndex;
    gSnap.cat = m.category;
    gSnap.nameIndex = m.nameIndex;
    gSnap.layout = m.layout;
    gSnap.voiceStatus = vs;
    gSnap.text = txt;
    gSnap.textStyle = hb.style(3);
}

void talkBatchTick() {
    if (gBatch.empty() || gBatchDone || !gameActive() || gameWorld().dungeon == nullptr) return;
    TalkManager& m = talkManager();
    const long t = long(engine().tickCount);
    char buf[256];
    if (Chr* p = characters().player()) p->status.block.vt = p->status.block.vtMax;   // keep the VT talks (FUN_001a5650) out
    if (gBatchStart >= 0) {
        const std::string& name = gBatch[gBatchNext - 1];
        const bool other = m.active != 0 && m.talkIndex >= 0 && name != talkFileName(m.talkIndex);
        if (m.active == 0 || other || t - gBatchStart > 2400) {
            std::snprintf(buf, sizeof buf, "talk batch end %s ticks=%ld result=%s", name.c_str(), t - gBatchStart,
                          m.active == 0 ? "ended" : other ? "REPLACED" : "TIMEOUT");
            platform::log(buf);
            if (m.active != 0) m.end();
            gBatchStart = -1;
            gBatchGapUntil = t + 10;
        }
        return;
    }
    static long firstSeen = -1;
    if (firstSeen < 0) firstSeen = t;
    if (t < firstSeen + 30 || t < gBatchGapUntil) return;   // let the floor finish its first ticks
    static long eventSince = -1;                             // an event a talk started (ACT Event / E91): let it finish
    if (eventActive() && (eventSince < 0 || t - eventSince < 6000)) {
        if (eventSince < 0) {
            eventSince = t;
            platform::log("talk batch waiting for event t=" + std::to_string(t));
        }
        return;
    }
    if (eventSince >= 0) platform::log("talk batch event over after " + std::to_string(t - eventSince) + " ticks");
    eventSince = -1;
    if (gBatchNext >= gBatch.size()) {
        gBatchDone = true;
        platform::log("talk batch done");
        return;
    }
    const std::string& name = gBatch[gBatchNext++];
    std::snprintf(buf, sizeof buf, "talk batch start %s t=%ld", name.c_str(), t);
    platform::log(buf);
    m.playByName(name.c_str());                                    // FUN_001a73a0(0x53c6e0, name)
    gBatchStart = t;
}
}  // namespace

void talkDevOptions(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--talk") == 0 && std::strlen(argv[i + 1]) == 4) gSpeaker = argv[i + 1];
        if (std::strcmp(argv[i], "--talk-name") == 0) gName = argv[i + 1];
        if (std::strcmp(argv[i], "--talk-batch") == 0) {
            std::ifstream in(argv[i + 1]);
            for (std::string ln; std::getline(in, ln);) {
                while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
                if (!ln.empty()) gBatch.push_back(ln);
            }
            gTalkTrace = true;
        }
        if (std::strcmp(argv[i], "--trace-pos") == 0) gTraceEvery = std::atoi(argv[i + 1]);
        if (std::strcmp(argv[i], "--trace-flag") == 0) gTraceFlags.push_back(uint32_t(std::strtoul(argv[i + 1], nullptr, 0)));
        if (std::strcmp(argv[i], "--shot") == 0) gShotRun = true;
        if (std::strcmp(argv[i], "--talk-trace") == 0) gTalkTrace = true;
        if (std::strcmp(argv[i], "--press-every") == 0 && i + 4 < argc)
            gPressEvery.push_back({uint32_t(std::strtoul(argv[i + 1], nullptr, 0)), std::atol(argv[i + 2]), std::atol(argv[i + 3]),
                                   std::max(1L, std::atol(argv[i + 4]))});
        if (std::strcmp(argv[i], "--goto") == 0 && i + 3 < argc)
            gGoto.push_back({float(std::atof(argv[i + 1])), float(std::atof(argv[i + 2])), std::atol(argv[i + 3])});
        if (std::strcmp(argv[i], "--stick-more") == 0 && i + 4 < argc)
            gStickMore.push_back({std::atoi(argv[i + 1]), std::atoi(argv[i + 2]), std::atol(argv[i + 3]), std::atol(argv[i + 4])});
    }
    autoplayOptions(argc, argv);   // PORT: --autoplay (autoplay_dev.cpp)
}

// Runs inside the talk hook of the engine tick (talk_wiring.cpp), after the talk task.
void talkDevTickInternal() {
    if (gShotRun) autoplayTick();   // PORT: --autoplay (autoplay_dev.cpp)
    if (gShotRun) {   // PORT: --press-every / --stick-more (next pad read)
        const long t = long(engine().tickCount) + 1;
        uint32_t mask = 0;
        for (const PressEvery& p : gPressEvery)
            if (p.from <= t && t < p.to && (t - p.from) % p.step == 0) mask |= p.mask;
        if (mask != 0) platform::injectButtons(mask, 1);
        for (const StickMore& m : gStickMore)
            if (m.from <= t && t < m.to) platform::injectStick(m.lx, m.ly, 1);
        for (Goto& g : gGoto) {   // PORT: --goto (first unfinished waypoint only)
            if (g.done) continue;
            if (t < g.from) break;
            const Chr* p = characters().player();
            const PlayerControl* ctl = playerCtl();
            if (p == nullptr || ctl == nullptr || ctl->camera == nullptr || !gameActive()) break;
            if (g.started < 0) { g.started = t; g.floor = gameProgress().floor; }
            if (g.floor != gameProgress().floor) {   // the floor changed under this waypoint: drop it
                g.done = true;
                platform::log("trace goto dropped (floor changed)");
                continue;
            }
            const float dx = g.x - p->xf.m.pos[0], dz = g.z - p->xf.m.pos[2];
            const float d = std::sqrt(dx * dx + dz * dz);
            char buf[160];
            if (d < 40.0f || t - g.started > 300) {
                g.done = true;
                std::snprintf(buf, sizeof buf, "trace goto (%.0f %.0f) %s at t=%ld pos=(%.0f %.0f)", double(g.x), double(g.z),
                              d < 40.0f ? "reached" : "TIMEOUT", t, double(p->xf.m.pos[0]), double(p->xf.m.pos[2]));
                platform::log(buf);
                continue;
            }
            float f[2] = {ctl->camera->forward[0], ctl->camera->forward[2]};
            float r[2] = {ctl->camera->right[0], ctl->camera->right[2]};
            if (freeCameraActive()) {   // the movement code reads the free camera's axes while it is active
                float fb[3], fr[3];
                freeCameraAxes(fb, fr);
                f[0] = fb[0]; f[1] = fb[2];
                r[0] = fr[0]; r[1] = fr[2];
            }
            const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1]), rl = std::sqrt(r[0] * r[0] + r[1] * r[1]);
            if (fl > 0.0f) { f[0] /= fl; f[1] /= fl; }
            if (rl > 0.0f) { r[0] /= rl; r[1] /= rl; }
            const float sy = (dx * f[0] + dz * f[1]) / d, sx = (dx * r[0] + dz * r[1]) / d;
            const float m = std::max(std::fabs(sx), std::fabs(sy));
            const float k = m > 0.0f ? 1.0f / m : 0.0f;   // push to the square's edge: full tilt (magnitude >= 1)
            const int lx = std::clamp(int(std::lround(128.0f + sx * k * 127.0f)), 0, 255);
            const int ly = std::clamp(int(std::lround(128.0f + sy * k * 127.0f)), 0, 255);
            platform::injectStick(lx, ly, 1);
            break;
        }
    }
    if (gTraceEvery > 0 && gameActive() && gameWorld().dungeon != nullptr &&
        (gTraceDungeon != gameWorld().dungeon || gTraceFloor != gameWorld().dungeon->floorId())) {
        gTraceDungeon = gameWorld().dungeon;
        gTraceFloor = gameWorld().dungeon->floorId();   // PORT: --trace-pos placement dump (stairs / spawns / items)
        for (const Placement& pl : gameWorld().dungeon->placements()) {
            if (pl.kind == Placement::Kind::ExtraModel) continue;
            char tg[5] = {char(pl.tag & 0xff), char((pl.tag >> 8) & 0xff), char((pl.tag >> 16) & 0xff), char(pl.tag >> 24), 0};
            char buf[160];
            std::snprintf(buf, sizeof buf, "trace place kind=%d tag=%s pos=(%.0f %.0f %.0f) room=%d", int(pl.kind), tg,
                          double(pl.x), double(pl.y), double(pl.z), pl.room);
            platform::log(buf);
        }
        // cell map of generated floors (FUN_00124c70 per 200-unit cell): x right, z down; ' ' none, '0'..'f' room cell
        // (0x1000 | room), 'c' other cell kinds, 'S' stairs cell, 'P' player start
        Dungeon& d = *gameWorld().dungeon;
        const FloorContext& cx = d.context();
        if (cx.maxX > cx.minX && cx.maxY > cx.minY) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "trace map cells x %d..%d z %d..%d (cell = 200 units)", cx.minX * 8, cx.maxX * 8 + 7,
                          cx.minY * 8, cx.maxY * 8 + 7);
            platform::log(buf);
            const int psx = d.player().valid ? int(d.player().x) / 200 : -9999, psz = d.player().valid ? int(d.player().z) / 200 : -9999;
            for (int z = cx.minY * 8; z <= cx.maxY * 8 + 7; ++z) {
                std::string row = "trace map ";
                char num[8];
                std::snprintf(num, sizeof num, "%4d ", z);
                row += num;
                for (int x = cx.minX * 8; x <= cx.maxX * 8 + 7 && row.size() < 400; ++x) {
                    const float pos[3] = {x * 200.0f + 100.0f, 0.0f, z * 200.0f + 100.0f};
                    int ccx = 0, ccy = 0;
                    const uint32_t* c = d.cellAtWorld(pos, &ccx, &ccy);
                    char ch = ' ';
                    if (c != nullptr && *c != 0) {
                        const uint32_t v = *c;
                        ch = (v & 0xf000u) == 0x1000u ? "0123456789abcdef"[v & 0xfu] : 'c';
                    }
                    if (x == cx.stairsCellX && z == cx.stairsCellZ) ch = 'S';
                    if (x == psx && z == psz) ch = 'P';
                    row += ch;
                }
                platform::log(row);
            }
        }
    }
    if (gTraceEvery > 0 && gameActive() && engine().tickCount % uint64_t(gTraceEvery) == 0) {   // PORT: --trace-pos
        if (const Chr* p = characters().player()) {
            char buf[256];
            std::snprintf(buf, sizeof buf, "trace t=%llu floor=%d pos=(%.0f %.0f %.0f) fwd=(%.2f %.2f) hp=%d/%d vt=%d/%d talk=%d active=%d cat=%d spk=%.4s",
                          static_cast<unsigned long long>(engine().tickCount), gameProgress().floor,
                          double(p->xf.m.pos[0]), double(p->xf.m.pos[1]), double(p->xf.m.pos[2]),
                          double(p->xf.m.fwd[0]), double(p->xf.m.fwd[2]), p->status.block.hp, p->status.block.hpMax,
                          p->status.block.vt, p->status.block.vtMax, talkManager().state,
                          talkManager().active, talkManager().category, reinterpret_cast<const char*>(&talkManager().speaker));
            std::string line = buf;
            for (uint32_t f : gTraceFlags) line += " f" + std::to_string(f) + "=" + (flags().test(f) ? "1" : "0");
            platform::log(line);
            for (const Chr* c : characters().list()) {   // enemies within 800 units
                if (c == nullptr || c->type != 2) continue;
                const float dx = c->xf.m.pos[0] - p->xf.m.pos[0], dz = c->xf.m.pos[2] - p->xf.m.pos[2];
                if (dx * dx + dz * dz > 800.0f * 800.0f) continue;
                std::snprintf(buf, sizeof buf, "trace   enemy %s pos=(%.0f %.0f %.0f) hp=%d/%d", c->idString().c_str(),
                              double(c->xf.m.pos[0]), double(c->xf.m.pos[1]), double(c->xf.m.pos[2]), c->status.block.hp,
                              c->status.block.hpMax);
                platform::log(buf);
            }
            for (const Chr* c : characters().list()) {   // NPCs within 800 units (approach talk FUN_001a2d40 geometry)
                if (c == nullptr || c->type != 1) continue;
                const float dx = p->xf.m.pos[0] - c->xf.m.pos[0], dz = p->xf.m.pos[2] - c->xf.m.pos[2];
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d > 8000.0f) continue;
                const float dot = d > 0.0f ? (dx * c->xf.m.fwd[0] + dz * c->xf.m.fwd[2]) / d : 0.0f;
                std::snprintf(buf, sizeof buf, "trace   npc %s pos=(%.0f %.0f %.0f) fwd=(%.2f %.2f) dist=%.0f npcFacesPlayer=%.2f",
                              c->idString().c_str(), double(c->xf.m.pos[0]), double(c->xf.m.pos[1]), double(c->xf.m.pos[2]),
                              double(c->xf.m.fwd[0]), double(c->xf.m.fwd[2]), double(d), double(dot));
                platform::log(buf);
            }
        }
    }
    if (gTalkTrace && gameActive()) talkTraceTick();   // PORT: --talk-trace (state after this tick's talk task)
    talkBatchTick();                                   // PORT: --talk-batch
    if (gSpeaker.empty() && !gName.empty() && !gDone && gameActive() && gameWorld().dungeon != nullptr) {
        if (++gWait >= 30) {                           // PORT: --talk-name without --talk
            gDone = true;
            platform::log("talk dev: playing " + gName);
            talkManager().playByName(gName.c_str());   // FUN_001a73a0(0x53c6e0, name)
        }
        return;
    }
    if (gSpeaker.empty() || gDone || !gameActive() || gameWorld().dungeon == nullptr) return;
    Chr* pl = characters().player();
    if (pl == nullptr) return;
    if (++gWait < 30) return;   // let the floor finish its first ticks (player landing, NPC activation)
    Chr* npc = nullptr;
    for (Chr* c : characters().list()) {
        if (c->type == 1 && c->idString() == gSpeaker) {
            npc = c;
            break;
        }
    }
    if (npc == nullptr) {
        if (gWait == 30) platform::log("talk dev: no NPC " + gSpeaker + " on this floor");
        return;
    }
    gDone = true;
    const Vec3f& p = npc->xf.m.pos;
    const Vec3f& f = npc->xf.m.fwd;
    pl->xf.m.pos = {p[0] + f[0] * 150.0f, p[1], p[2] + f[2] * 150.0f};
    pl->prevPos = pl->xf.m.pos;
    // face the NPC: forward = -npc forward (XZ), right = up x forward
    pl->xf.m.fwd = {-f[0], 0.0f, -f[2]};
    const float l = std::sqrt(pl->xf.m.fwd[0] * pl->xf.m.fwd[0] + pl->xf.m.fwd[2] * pl->xf.m.fwd[2]);
    if (l > 0.0f) {
        pl->xf.m.fwd[0] /= l;
        pl->xf.m.fwd[2] /= l;
    }
    pl->xf.m.up = {0.0f, 1.0f, 0.0f};
    pl->xf.m.right = {pl->xf.m.fwd[2], 0.0f, -pl->xf.m.fwd[0]};
    platform::log("talk dev: player placed in front of " + gSpeaker);
    if (!gName.empty()) talkManager().playByName(gName.c_str());           // FUN_001a73a0(0x53c6e0, name)
}

}  // namespace baroque::game
