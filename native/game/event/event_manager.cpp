// EVT event manager 0x51b620: lifecycle, file parse, camera keyframes, skip, end, screen transition.
// Cue dispatcher and per-tick jobs: event_cues.cpp. Hosts of the side systems: event_services.cpp.
#include "game/event/event_manager.hpp"

#include "audio/sfs.hpp"            // PORT (voice language): which voice archive plays
#include "data/evt_jptime.hpp"     // PORT (voice language): Japanese timing with the English text
#include "game/event/event_sound.hpp"   // PORT: voice start/end log (test runs)
#include "platform/app_host.hpp"

#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/event/event_services.hpp"
#include "game/event/event_skip_prompt.hpp"   // PORT: hold-to-skip
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/scene_manager.hpp"
#include "game/sound_stub.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/task_list.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

namespace baroque::game {

namespace {

EventManager* gMgr = nullptr;
EventHooks gHooks;
RenderLights gLights;

// Event camera task (vtable 0x4cdda0: +0xc FUN_0010da30 tick wrapper, +0x14 FUN_0016ba60 update)
struct EventCameraTask final : Task {
    EventCamera cam;
    bool pushed = false;     // linked as *0x4e7b18 (FUN_0010da00 / FUN_0010d9c0)
    // orig FUN_0010da30 (work/decompiled/0010da30.c) -> vt+0x14 FUN_0016ba60 (work/decompiled/0016ba60.c); the view
    // matrix publish (FUN_0039f1d0 into 0x4e7b30) happens in the presentation (eventCameraMatrix)
    void update() override {
        std::memcpy(cam.prev, cam.world, sizeof cam.prev);
        const EventManager& m = eventManager();
        for (int k = 0; k < 3; ++k) {
            cam.world[0][k] = m.basis[0][k];   // FUN_0016bdf0 = +0x1c
            cam.world[1][k] = m.basis[1][k];   // FUN_0016be00 = +0x28
            cam.world[2][k] = m.basis[2][k];   // FUN_0016be10 = +0x34
            cam.world[3][k] = m.eye[k];        // FUN_0016bde0 = +0x40
        }
    }
};
EventCameraTask* gCamTask = nullptr;

// Event-name tables (ELF 0x44d650 {file, startFlag string} and 0x44d2e0 {file, actor-set id}).
struct NameFlag { const char* file; int flag; };
constexpr NameFlag kFirstViewFlags[] = {
    {"E01.EVT", 656}, {"E51.EVT", 658}, {"E52.EVT", 659}, {"E53.EVT", 674}, {"E91.EVT", 3829}, {"E92.EVT", 3830},
    {"E03.EVT", 657}, {"E04.EVT", 661}, {"E05.EVT", 660}, {"E06.EVT", 662}, {"E62.EVT", 676}, {"E71.EVT", 663},
    {"E72.EVT", 664}, {"E08.EVT", 665}, {"E82.EVT", 677}, {"E09.EVT", 666}, {"E10.EVT", 668}, {"E83.EVT", 675},
    {"E11.EVT", 667}, {"E12.EVT", 669}, {"E13.EVT", 670}, {"E14.EVT", 671}, {"E15.EVT", 672}, {"E21.EVT", 3800},
    {"E22.EVT", 3801}, {"E23.EVT", 3802}, {"E24.EVT", 3803}, {"E25.EVT", 3804}, {"E26.EVT", 3805},
    {"E27.EVT", 3806}, {"E28.EVT", 3807}, {"E29.EVT", 3808}, {"E30.EVT", 3809}, {"E31.EVT", 3810},
    {"E32.EVT", 3811}, {"E33.EVT", 3812}, {"E34.EVT", 3813}, {"E35.EVT", 3814}, {"E36.EVT", 3815},
    {"E37.EVT", 3816}, {"E38.EVT", 3817}, {"E39.EVT", 3818}, {"E40.EVT", 3819}, {"E41.EVT", 3820},
    {"E42.EVT", 3821}, {"E43.EVT", 3822}, {"E44.EVT", 3823}, {"E45.EVT", 3824}, {"E46.EVT", 3825},
    {"E47.EVT", 3826}, {"E48.EVT", 3827}, {"E49.EVT", 3828}};
struct NameSet { const char* file; const char* set; };
constexpr NameSet kActorSets[] = {
    {"E01.EVT", "AE01"}, {"E02.EVT", "AE02"}, {"E03.EVT", "AE03"}, {"E04.EVT", "AE04"}, {"E05.EVT", "AE05"},
    {"E06.EVT", "AE06"}, {"E07.EVT", "AE07"}, {"E08.EVT", "AE08"}, {"E09.EVT", "AE09"}, {"E10.EVT", "AE10"},
    {"E11.EVT", "AE11"}, {"E12.EVT", "AE12"}, {"E13.EVT", "AE13"}, {"E14.EVT", "AE14"}, {"E15.EVT", "AE15"},
    {"E16.EVT", "AE16"}, {"E17.EVT", "AE17"}, {"E18.EVT", "AE18"}, {"E51.EVT", "AE02"}, {"E52.EVT", "AE02"},
    {"E53.EVT", "AE02"}, {"E71.EVT", "AE07"}, {"E72.EVT", "AE07"}, {"E62.EVT", "AE06"}, {"E82.EVT", "AE08"},
    {"E83.EVT", "AE10"}, {"E91.EVT", "AE03"}, {"E92.EVT", "AE03"}};

// orig FUN_0039ddd0 -> FUN_0039ded0: rotation of `angle` about unit `axis` (same formula and operation order as
// camera.cpp's private copy). UNVERIFIED: sin/cos from the host libm (game: FUN_002386a0 / FUN_00238400).
void axisAngle(float angle, const float axis[3], float m[3][3]) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    const float omc = 1.0f - c;
    const float zero = 0.0f;
    float t = omc * axis[0];
    m[0][0] = (zero + c) + t * axis[0];
    m[0][1] = (zero + s * axis[2]) + t * axis[1];
    m[0][2] = t * axis[2] - s * axis[1];
    t = omc * axis[1];
    m[1][0] = t * axis[0] - s * axis[2];
    m[1][1] = (zero + c) + t * axis[1];
    m[1][2] = (zero + s * axis[0]) + t * axis[2];
    t = omc * axis[2];
    m[2][0] = (zero + s * axis[1]) + t * axis[0];
    m[2][1] = t * axis[1] - s * axis[0];
    m[2][2] = (zero + c) + t * axis[2];
}
// orig FUN_003a0330: out = v * M (3x3), out may alias v
void transformVec(float out[3], const float v[3], const float m[3][3]) {
    const float x = v[0], y = v[1], z = v[2];
    out[0] = y * m[1][0] + x * m[0][0] + z * m[2][0];
    out[1] = y * m[1][1] + x * m[0][1] + z * m[2][1];
    out[2] = y * m[1][2] + x * m[0][2] + z * m[2][2];
}
// orig FUN_003a01f0 (work/decompiled/003a01f0.c): out = a x b
void cross(float out[3], const float a[3], const float b[3]) {
    const float r0 = a[1] * b[2] - a[2] * b[1];
    const float r1 = a[2] * b[0] - a[0] * b[2];
    const float r2 = a[0] * b[1] - a[1] * b[0];
    out[0] = r0;
    out[1] = r1;
    out[2] = r2;
}
// orig FUN_00206c40 (work/decompiled/00206c40.c): rotation in the XY plane
void rotateZ(float angle, float v[3]) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    const float x = v[0];
    v[0] = x * c + v[1] * -s;
    v[1] = x * s + v[1] * c;
}

std::vector<uint8_t> readAsset(const std::string& rel) {
    std::ifstream in(platform::assetRoot() / rel, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
int32_t le32(const std::vector<uint8_t>& d, size_t off) {
    if (off + 4 > d.size()) return 0;
    int32_t v;
    std::memcpy(&v, d.data() + off, 4);
    return v;
}

}  // namespace

EventManager& eventManager() {
    if (gMgr == nullptr) gMgr = new EventManager();
    return *gMgr;
}
EventHooks& eventHooks() { return gHooks; }
RenderLights& renderLights() { return gLights; }
bool eventActive() { return gMgr != nullptr && gMgr->active != 0; }

// orig FUN_00108a30 (work/decompiled/00108a30.c): slot 0 for the scene light, else the first free of 1..4
int RenderLights::alloc(int secondary) {
    if (secondary == 0) {
        if (slot[0].used == 0) {
            slot[0].used = 1;
            slot[0].enabled = 0;   // node vt+0x84 (argument dropped by Ghidra) UNVERIFIED
            return 0;
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            if (slot[i].used == 0) {
                slot[i].used = 1;
                slot[i].enabled = 0;   // UNVERIFIED as above
                return i;
            }
        }
    }
    return -1;
}
// orig FUN_00108ae0 (work/decompiled/00108ae0.c)
void RenderLights::release(int s) {
    if (s < 0 || s > 4) return;   // PORT: the original indexes without a check
    slot[s].used = 0;
    slot[s].enabled = 0;
}

// orig FUN_0016d300 (work/decompiled/0016d300.c): 1 while the event's start flag is clear (first viewing)
bool eventFirstView(const char* fileName) {
    for (const NameFlag& e : kFirstViewFlags)
        if (std::strcmp(e.file, fileName) == 0) return !flags().test(static_cast<uint32_t>(e.flag));   // atoi + FUN_00178a80
    return false;
}

// orig FUN_001d1d30 (work/decompiled/001d1d30.c)
void eventTrigger(int n) {
    EventManager& m = eventManager();
    m.triggerId = n;                                         // uRam0053d840
    char name[16];
    std::snprintf(name, sizeof name, "E%02d.EVT", n);        // 0x463d70
    m.start(name);
    // player state request (factory LAB_001d1de0 = the Event state, prio 2) when no state is held or prio < 3
    if (Chr* p = characters().player()) {
        if (gHooks.playerEventState) gHooks.playerEventState(*p);
        // UNVERIFIED: the player's Event state (vtable 0x4ced70) is not translated in the character module; the
        // request is left to the hook (an untranslated state would never pop).
    }
    fade().flags &= 0xfffffffdu;                             // uRam004e7890 &= ~2
}

// orig FUN_0016bbf0 (work/decompiled/0016bbf0.c)
void EventManager::reset() {
    active = 0;
    camera = nullptr;
    mapOfs[0] = mapOfs[1] = mapOfs[2] = 0.0f;
    angles[0] = angles[1] = angles[2] = 0.0f;
    frame = 0;
    keyIndex = 0;
    cueIndex = 0;
    lastKey = 0;
    lastCue = 0;
    pc = 0;
    letterTimer = -1;
    letterState = 0;
    transFrames = -1;
    transAlpha = 0.0f;
    transStep = 0.0f;
    letterTop = nullptr;
    letterBottom = nullptr;
    colorQuad = nullptr;
    quadStep = 0;
    instanceFilter = -1;
    equipOverride = 0;
    savedFog[0] = savedFog[1] = 0.0f;
    savedFar = 0.0f;
    savedFov = 0.0f;
    actorSet = 0;
    light = -1;
    lightDriftFrames = 0;
    lightDir[0] = lightDir[1] = lightDir[2] = 0.0f;
    keyAdvanced = 0;
    transCaptured = 0;                                       // FUN_00326280(+0x209cc, 0)
    transRequest = 0;
    seStopTime = 2;
    chained = -1;
    ticks = 0;
    skippable = 1;
    skipCountdown = -1;
    firstView = 1;
}

// orig FUN_0016c6d0 (work/decompiled/0016c6d0.c)
void EventManager::start(const char* fileName) {
    if (active != 0 || letterTop != nullptr) return;
    talkManager().cameraRelease();                           // FUN_001a2cd0(0x53c6e0)
    Chr* pl = characters().player();
    if (pl == nullptr) return;                               // PORT: the original dereferences iRam0050b144
    bool dead = false;
    if (pl->type == 0 || pl->type == 2) dead = pl->status.block.hp == 0;
    if (dead) return;
    if (pl->actLoaded) pl->act.stop();                       // FUN_00143bc0
    pl->phys.moveVel = {0.0f, 0.0f, 0.0f};                   // phys +0x7c..+0x84
    eventPlayerStatusSave();                                 // FUN_00138c50
    if (gHooks.effectsKillFinished) gHooks.effectsKillFinished();   // FUN_00112600
    reset();                                                 // FUN_0016bbf0
    ++camCutSerial;                                          // PORT: presentation cut marker
    if (gHooks.padRumbleStop) gHooks.padRumbleStop();        // FUN_0020c540(0x4e6280, 0)
    ui::helpBar().clear(-1);                                 // FUN_0015c3f0(0x5180f0, -1)
    messageLog().clearCentre();                              // FUN_0015ce70
    messageLog().clear();                                    // FUN_0015d0a0
    eventRegistryHide();                                     // FUN_0012cb90(0x50b130)
    if (gHooks.mapHide) gHooks.mapHide();                    // FUN_00126fb0(0x50a350)
    sound().setMenuDuck(1);                                  // FUN_001161d0(0x4e9790, 1)
    for (EvtActorJob& j : jobs) j = EvtActorJob{};
    for (EvtEffectSlot& e : effects) e = EvtEffectSlot{};
    const LightSlot& ml = gLights.slot[gLights.mainSlot];
    for (int k = 0; k < 3; ++k) savedLightDir[k] = ml.dir[k];        // FUN_00108c90
    for (int k = 0; k < 4; ++k) savedLightColor[k] = ml.color[k];    // FUN_00108cc0
    for (int k = 0; k < 4; ++k) savedLightAmbient[k] = ml.ambient[k];   // FUN_00108cf0
    savedFov = gHooks.fov ? gHooks.fov() : 0.0f;             // iRam004e66cc
    savedFar = gHooks.farZ ? gHooks.farZ() : 0.0f;           // iRam004e66d4
    if (gHooks.fog) gHooks.fog(&savedFog[0], &savedFog[1]);  // *(0x4e790c) +0x10 / +0x14
    ui::helpBar().clear(-1);
    messageLog().clearCentre();
    messageLog().clear();
    // FUN_00178670: camera task from the pool (FUN_00132b20) -> FUN_0016b920: base ctor, then the manager's basis and
    // eye are taken from the active camera (*0x4e7b18 +0x18 rows)
    gCamTask = new EventCameraTask();
    {
        float w[4][4];
        if (gHooks.activeCameraWorld && gHooks.activeCameraWorld(w)) {
            for (int k = 0; k < 3; ++k) {
                basis[0][k] = w[0][k];
                basis[1][k] = w[1][k];
                basis[2][k] = w[2][k];
                eye[k] = w[3][k];
            }
        }
    }
    gCamTask->pushed = true;                                 // FUN_0010da00
    camera = &gCamTask->cam;
    tasks().count += 1;                                      // iRam004e661c++
    {   // FUN_00207450(task, 0x4e6614, 0x4e6618): link at the head of the engine task list
        Task* t = gCamTask;
        if ((t->flags & 0x10000000u) == 0) {
            t->prev = nullptr;
            t->next = tasks().head;
            if (tasks().head == nullptr) tasks().tail = t;
            else tasks().head->prev = t;
            tasks().head = t;
            t->slot = 0;
        }
        t->flags = (t->flags & 0xcfffffffu) | 1u;
    }
    gCamTask->update();                                      // PORT: the task has not run yet; the copy is what the
    std::memcpy(gCamTask->cam.prev, gCamTask->cam.world, sizeof gCamTask->cam.prev);   // ctor left in +0x18
    load(fileName);                                          // FUN_0016be20
    cameraAccel();                                           // FUN_0016f250
    lookAtInit();                                            // FUN_0016f1c0
    for (const NameSet& e : kActorSets) {
        if (std::strcmp(e.file, fileName) == 0) {
            actorSet = 1;
            eventActorSetLoad(*pl, 1, e.set);                // FUN_001b2620(player, 1, id)
        }
    }
    if (gHooks.seStop) gHooks.seStop(0xfffe, 2);             // FUN_00115900 = FUN_00115820(snd, 0xfffe, 2)
    active = 1;
    keyAdvanced = 0;
    firstView = eventFirstView(fileName) ? 1 : 0;            // FUN_0016d300
    requestTransition(0.0f, 5);                              // FUN_00177cc0(0, 0x51b620, 5)
    platform::log(std::string("event: start ") + fileName);
}

// orig FUN_00177c50 (work/decompiled/00177c50.c). UNVERIFIED: acos from the host libm (game: FUN_00238848).
float EventManager::angleOf(float a, float b, float c, float d) {
    const float v = a * c + b * d;
    if (d <= 0.0f) return std::acos(v) * 57.32484f;
    return 360.0f - std::acos(v) * 57.32484f;
}

// orig FUN_0016be20 (work/decompiled/0016be20.c)
bool EventManager::load(const char* fileName) {
    if (camera == nullptr) return false;
    const std::string rel = std::string(ui::language() == 1 ? "DATA/EVENT/" : "DATA/EVENT_EN/") + fileName;   // 0x44d120 / 0x44d130
    std::vector<uint8_t> d = readAsset(rel);                 // iRam0053f814 vt+0xc / +0x14 / +0x10
    if (d.size() < 0x80c) {
        platform::log("event: cannot read " + rel);
        return false;
    }
    // PORT (voice language): with the Japanese voices (Japan disc SOUND_JP, or SOUND_EN of an undub) the English scripts
    // talk over and cut off the longer Japanese lines. Run the Japanese script of the same event (DATA/EVENT, also
    // on the USA disc) with the English text pages swapped in (data/evt_jptime.*); the English script stays
    // unchanged for the English voices and whenever the two scripts do not line up.
    // Test runs can keep the English timing with the environment variable BAROQUE_EVT_ENGLISH_TIMING (comparison).
    char* keepEn = nullptr;
    size_t keepEnLen = 0;
    const bool forceEnglish = !platform::playerRun() && _dupenv_s(&keepEn, &keepEnLen, "BAROQUE_EVT_ENGLISH_TIMING") == 0 &&
                              keepEn != nullptr;
    std::free(keepEn);
    if (!forceEnglish && ui::language() != 1 && audio::japaneseVoicesActive()) {
        const std::vector<uint8_t> jp = readAsset(std::string("DATA/EVENT/") + fileName);
        std::string why = "no DATA/EVENT script";
        std::optional<std::vector<uint8_t>> merged;
        if (jp.size() >= 0x80c) merged = data::buildJapaneseTimedEvt(d, jp, &why);
        if (merged) {
            d = std::move(*merged);
            platform::log(std::string("event: ") + fileName + " uses the Japanese timing (Japanese voices)");
        } else {
            platform::log(std::string("event: ") + fileName + " keeps the English timing: " + why);
        }
    }
    std::memcpy(text, d.data(), 0x800);
    uint32_t mapId = static_cast<uint32_t>(le32(d, 0x800));
    mapIndex = static_cast<int32_t>(mapId);
    if (mapId != 0xffffffffu) {
        // 0x50a640[16]: room objects; UNVERIFIED: only the rooms of the floor are scanned (the original reads all
        // sixteen pointers)
        const int n = gHooks.mapIdCount ? gHooks.mapIdCount() : 0;
        for (int i = 0; i < 16 && i < n; ++i) {
            if (gHooks.mapRoomId && static_cast<uint32_t>(gHooks.mapRoomId(i)) == mapId) {
                mapIndex = i;
                const float z = eventMapOrigin(i, 2);        // FUN_0011e740
                const float x = eventMapOrigin(i, 0);        // FUN_0011e6c0
                mapOfs[0] = x;
                mapOfs[1] = 0.0f;
                mapOfs[2] = z;
                break;
            }
        }
    }
    count = le32(d, 0x804) + 1;
    for (EvtCamKey& k : keys) k = EvtCamKey{};
    for (EvtCueBlock& c : cues) c = EvtCueBlock{};
    keys[0].frame = 0;
    keys[0].pos[0] = eye[0];
    keys[0].pos[1] = eye[1];
    keys[0].pos[2] = eye[2];
    keys[0].lookValid = 0;
    float f = angleOf(-1.0f, 0.0f, basis[0][0], basis[0][2]);
    keys[0].rot[1] = f;
    angles[1] = f;
    float r0;
    if (90.0f < f) {
        if (f < 270.0f) r0 = angleOf(1.0f, 0.0f, basis[1][1], -basis[1][2]);
        else r0 = angleOf(1.0f, 0.0f, basis[1][1], basis[1][2]);
    } else {
        r0 = angleOf(1.0f, 0.0f, basis[1][1], basis[1][2]);
    }
    keys[0].rot[0] = r0;
    angles[0] = r0;
    const float r2 = angleOf(basis[1][0], basis[1][1], basis[0][0], basis[0][1]);
    keys[0].rot[2] = r2;
    angles[2] = r2;
    keys[0].rotDir[0] = keys[0].rotDir[1] = keys[0].rotDir[2] = 0;

    const size_t nWords = (d.size() - 0x808) / 4;
    auto word = [&](size_t i) -> int32_t { return i < nWords ? le32(d, 0x808 + 4 * i) : 0; };
    auto key = [&](int slot) -> EvtCamKey* {
        // UNVERIFIED: writes before the first op 5 (slot -1) land in the text buffer tail in the original; dropped
        if (slot < 0 || slot >= 300) return nullptr;
        return &keys[static_cast<size_t>(slot)];
    };
    bool originFromFile = true;   // bVar2
    int32_t id = 0;               // iVar11
    int slot = -1;                // iVar14
    int cue = -1;                 // iVar12
    size_t wp = 0;                // iVar15
    uint32_t guard = 0;           // uVar13
    keyEnd = 0;
    if (word(0) != 0xc9) {
        for (;;) {
            const int32_t op = word(wp);
            if (op == 1) {
                id = word(wp + 1);
                wp += 2;
            } else if (op == 5) {
                int next = slot + 1;
                if (next == 0) {
                    if (id != 0) {
                        originFromFile = false;
                        next = slot + 2;
                        if (299 < slot + 2) { end(); return false; }   // FUN_0016cbf0
                    }
                }
                slot = next;
                wp += 1;
                if (EvtCamKey* k = key(slot)) {
                    k->frame = id;
                    keyEnd = static_cast<uint32_t>(slot + 1);
                }
            } else if (op == 7) {
                if (EvtCamKey* k = key(slot)) {
                    k->pos[0] = mapOfs[0] + static_cast<float>(word(wp + 1));
                    k->pos[1] = mapOfs[1] + static_cast<float>(word(wp + 2));
                    k->pos[2] = mapOfs[2] + static_cast<float>(word(wp + 3));
                }
                wp += 4;
            } else if (op == 6) {
                if (EvtCamKey* k = key(slot)) k->mode = word(wp + 1);
                wp += 2;
            } else if (op == 8) {
                if (EvtCamKey* k = key(slot)) {
                    k->lookValid = 1;
                    k->look[0] = mapOfs[0] + static_cast<float>(word(wp + 1));
                    k->look[1] = mapOfs[1] + static_cast<float>(word(wp + 2));
                    k->look[2] = mapOfs[2] + static_cast<float>(word(wp + 3));
                }
                wp += 4;
            } else if (op == 0xf) {
                if (EvtCamKey* k = key(slot)) {
                    k->orbit = word(wp + 1);
                    k->orbitPlane = word(wp + 2);
                }
                wp += 3;
            } else if (op == 9 || op == 10 || op == 0xb) {
                if (EvtCamKey* k = key(slot)) k->rot[op - 9] = static_cast<float>(static_cast<uint32_t>(word(wp + 1)));
                wp += 2;
            } else if (op == 0xc || op == 0xd || op == 0xe) {
                if (EvtCamKey* k = key(slot)) k->rotDir[op - 0xc] = word(wp + 1);
                wp += 2;
            } else if (op == 0x10) {
                const int32_t w4 = word(wp + 4);
                if (static_cast<float>(w4) != 0.0f) {
                    if (EvtCamKey* k = key(slot)) {
                        k->spline[0] = mapOfs[0] + static_cast<float>(word(wp + 1));
                        k->spline[1] = mapOfs[1] + static_cast<float>(word(wp + 2));
                        k->spline[2] = mapOfs[2] + static_cast<float>(word(wp + 3));
                        k->spline[3] = static_cast<float>(w4);
                    }
                }
                wp += 5;
            } else if (op == 0x14) {
                ++cue;
                wp += 1;
                if (399 < cue) { end(); return false; }
                for (size_t i = 0; i < 64; ++i) cues[static_cast<size_t>(cue)].words[i] = word(wp + i);
                wp += 64;
                cues[static_cast<size_t>(cue)].frame = id;
            }
            // any other word: wp unchanged; the original spins until the 0x8000 guard
            ++guard;
            if (0x8000 < guard || word(wp) == 0xc9) break;
        }
    }
    if (originFromFile) {
        angles[0] = keys[0].rot[0];
        angles[1] = keys[0].rot[1];
        angles[2] = keys[0].rot[2];
        basisFromAngles(angles[0], angles[1], angles[2]);    // FUN_0016f430
        eye[0] = keys[0].pos[0];
        eye[1] = keys[0].pos[1];
        eye[2] = keys[0].pos[2];
    }
    lastKey = slot;
    lastCue = cue;
    return true;                                             // FUN_0020ef70 frees the file
}

// orig FUN_0016f430 (work/decompiled/0016f430.c). 0x4bc478 = (0,1,0): the static initialiser FUN_004cba80 stores
// DAT_004bc478 = 0, DAT_004bc47c = 0x3f800000, DAT_004bc480 = 0.
void EventManager::basisFromAngles(float x, float y, float z) {
    float* right = basis[0];
    float* up = basis[1];
    float* fwd = basis[2];
    right[0] = -1.0f; right[1] = 0.0f; right[2] = 0.0f;
    up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
    fwd[0] = 0.0f; fwd[1] = 0.0f; fwd[2] = -1.0f;
    float m[3][3];
    if (y != 0.0f) {
        static const float kUp[3] = {0.0f, 1.0f, 0.0f};
        axisAngle(-y * 0.017453292f, kUp, m);
        transformVec(fwd, fwd, m);
        transformVec(right, right, m);
    }
    if (x != 0.0f) {
        const float ax[3] = {right[0], right[1], right[2]};
        axisAngle(x * 0.017453292f, ax, m);
        transformVec(up, up, m);
        transformVec(fwd, fwd, m);
    }
    if (z != 0.0f) {
        const float ax[3] = {fwd[0], fwd[1], fwd[2]};
        axisAngle(z * 0.017453292f, ax, m);
        transformVec(up, up, m);
        transformVec(right, right, m);
    }
    float l = 1.0f / std::sqrt(fwd[2] * fwd[2] + fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    fwd[0] *= l; fwd[1] *= l; fwd[2] *= l;
    cross(right, up, fwd);
    l = 1.0f / std::sqrt(right[2] * right[2] + right[0] * right[0] + right[1] * right[1]);
    right[0] *= l; right[1] *= l; right[2] *= l;
    cross(up, fwd, right);
    l = 1.0f / std::sqrt(up[2] * up[2] + up[0] * up[0] + up[1] * up[1]);
    up[0] *= l; up[1] *= l; up[2] *= l;
}

// orig FUN_0016f250 (work/decompiled/0016f250.c)
void EventManager::cameraAccel() {
    const EvtCamKey& k = keys[static_cast<size_t>(keyIndex)];
    const EvtCamKey& n = keys[static_cast<size_t>(keyIndex) + 1];
    const float f = static_cast<float>(n.frame - k.frame);
    const float f2 = f * f;
    for (int i = 0; i < 3; ++i) camAcc[i] = ((n.pos[i] - k.pos[i]) * 2.0f) / f2;
    if (k.mode == 3) {
        for (int i = 0; i < 3; ++i) camVel[i] = camAcc[i] * f;
    } else {
        camVel[0] = camVel[1] = camVel[2] = 0.0f;
    }
}

// orig FUN_0016f1c0 (work/decompiled/0016f1c0.c)
void EventManager::lookAtInit() {
    const EvtCamKey& k = keys[static_cast<size_t>(keyIndex)];
    lookCur[0] = k.look[0];
    lookCur[1] = k.look[1];
    lookCur[2] = k.look[2];
}

// orig FUN_0016f070 (work/decompiled/0016f070.c)
void EventManager::cameraSpline(float dur, int t) {
    const EvtCamKey& k = keys[static_cast<size_t>(keyIndex)];
    const EvtCamKey& n = keys[static_cast<size_t>(keyIndex) + 1];
    const float f = ((dur / 2.0f - static_cast<float>(t)) / dur) * 2.0f;
    eye[0] = eye[0] + (f * (k.spline[3] / 100.0f) * (k.spline[0] - (k.pos[0] + n.pos[0]) / 2.0f)) / dur;
    eye[1] = eye[1] + (f * (k.spline[3] / 100.0f) * (k.spline[1] - (k.pos[1] + n.pos[1]) / 2.0f)) / dur;
    eye[2] = eye[2] + (f * (k.spline[3] / 100.0f) * (k.spline[2] - (k.pos[2] + n.pos[2]) / 2.0f)) / dur;
}

// orig FUN_0016dde0 (work/decompiled/0016dde0.c)
void EventManager::cameraInterp(float dur) {
    const EvtCamKey& k = keys[static_cast<size_t>(keyIndex)];
    const EvtCamKey& n = keys[static_cast<size_t>(keyIndex) + 1];
    const int t = frame - k.frame;
    if (n.frame - k.frame == 1) {
        ++camCutSerial;                                      // PORT: presentation cut marker (1-frame segment)
        eye[0] = n.pos[0];
        eye[1] = n.pos[1];
        eye[2] = n.pos[2];
    } else if (k.orbit != 0) {
        float a = (static_cast<float>(t) / dur) * 180.0f;
        if (k.orbit == 1) a = -a;
        const float hz = (k.pos[2] - n.pos[2]) / 2.0f;
        const float hx = (k.pos[0] - n.pos[0]) / 2.0f;
        const float hy = (k.pos[1] - n.pos[1]) / 2.0f;
        float v[3] = {hx, hy, hz};
        if (k.orbitPlane == 0) {
            rotateY(a * 0.017453292f, v);                    // FUN_00206be0
            eye[0] = v[0] + n.pos[0] + hx;
            eye[1] = eye[1] + (n.pos[1] - k.pos[1]) / dur;
            eye[2] = v[2] + n.pos[2] + hz;
        } else {
            rotateZ(a * 0.017453292f, v);                    // FUN_00206c40
            eye[0] = eye[0] + (n.pos[0] - k.pos[0]) / dur;
            eye[1] = v[1] + n.pos[1] + hy;
            eye[2] = v[2] + n.pos[2] + hz;
        }
    } else if (k.mode != 0) {
        if (k.mode == 1) {
            if (static_cast<float>(t) < dur / 2.0f) {
                for (int i = 0; i < 3; ++i) camVel[i] = camAcc[i] * 2.0f + camVel[i] + 0.0f;
            } else {
                for (int i = 0; i < 3; ++i) camVel[i] = (camVel[i] + 0.0f) - camAcc[i] * 2.0f;
            }
        } else if (k.mode == 2) {
            for (int i = 0; i < 3; ++i) camVel[i] = camVel[i] + camAcc[i];
        } else if (k.mode == 3) {
            for (int i = 0; i < 3; ++i) camVel[i] = camVel[i] - camAcc[i];
        }
        for (int i = 0; i < 3; ++i) eye[i] = eye[i] + camVel[i];
        if (k.spline[3] != 0.0f && 0 < frame && 0 < t) cameraSpline(dur, t);
    } else {
        for (int i = 0; i < 3; ++i) eye[i] = eye[i] + (n.pos[i] - k.pos[i]) / dur;
        if (k.spline[3] != 0.0f && 0 < frame && 0 < t) cameraSpline(dur, t);
    }
    // LAB_0016e558: rotation
    if (n.frame - k.frame == 1) {
        ++camCutSerial;                                      // PORT: presentation cut marker (1-frame segment)
        angles[0] = n.rot[0];
        angles[1] = n.rot[1];
        angles[2] = n.rot[2];
    } else {
        const float u = static_cast<float>(t) / dur;
        for (int ax = 0; ax < 3; ++ax) {
            float d = 0.0f;
            const float r = k.rot[ax];
            const float nr = n.rot[ax];
            if (k.rotDir[ax] == 0) {
                if (r < nr) d = (nr - r) / dur;
                else if (nr < r) d = ((360.0f - r) + nr) / dur;
            } else {
                if (r < nr) d = -((360.0f - nr) + r) / dur;
                else if (nr < r) d = (ax == 0) ? (nr - r) / dur : -(r - nr) / dur;
            }
            float& a = angles[ax];
            if (k.mode == 0) a = a + d;
            else if (k.mode == 2) a = d * u * 2.0f + a + 0.0f;
            else if (k.mode == 3) a = d * (2.0f - u * 2.0f) + a + 0.0f;
            else if (k.mode == 1) {
                if (u <= 0.5f) a = d * u * 4.0f + a + 0.0f;
                else a = d * (4.0f - u * 4.0f) + a + 0.0f;
            }
            if (0.0f < d) {
                if (360.0f < a) a = a - 360.0f;
            } else if (a < 0.0f) {
                a = a + 360.0f;
            }
        }
    }
    // LAB_0016ece0: look-at
    if (k.lookValid == 1 && n.lookValid == 1) {
        for (int i = 0; i < 3; ++i) lookCur[i] = lookCur[i] + (n.look[i] - k.look[i]) / dur;
    }
    bool aim = false;
    if (k.lookValid == 1 && (n.frame != frame + 1 || n.lookValid != 0)) aim = true;
    else if (k.lookValid == 0 && n.frame == frame + 1 && n.lookValid == 1) aim = true;
    if (aim) {   // LAB_0016eea8
        float v[3] = {eye[0] - lookCur[0], eye[1] - lookCur[1], eye[2] - lookCur[2]};
        normalize3(v);                                       // FUN_00206ab0
        float* right = basis[0];
        float* up = basis[1];
        float* fwd = basis[2];
        right[0] = -1.0f; right[1] = 0.0f; right[2] = 0.0f;
        up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
        fwd[0] = v[0]; fwd[1] = v[1]; fwd[2] = v[2];
        float l = 1.0f / std::sqrt(fwd[2] * fwd[2] + fwd[0] * fwd[0] + fwd[1] * fwd[1]);
        fwd[0] *= l; fwd[1] *= l; fwd[2] *= l;
        cross(right, up, fwd);
        right[1] = 0.0f;
        l = 1.0f / std::sqrt(right[2] * right[2] + right[0] * right[0] + 0.0f);
        right[0] *= l; right[1] *= l; right[2] *= l;
        cross(up, fwd, right);
        l = 1.0f / std::sqrt(up[2] * up[2] + up[0] * up[0] + up[1] * up[1]);
        up[0] *= l; up[1] *= l; up[2] *= l;
    } else {
        basisFromAngles(angles[0], angles[1], angles[2]);    // FUN_0016f430
    }
}

// orig FUN_0016dbf0 (work/decompiled/0016dbf0.c)
void EventManager::step() {
    const EvtCamKey& k = keys[static_cast<size_t>(keyIndex)];
    const EvtCamKey& n = keys[static_cast<size_t>(keyIndex) + 1];
    const int endF = n.frame;
    const int startF = k.frame;
    if (frame + 1 == endF && k.lookValid != 1 && n.lookValid == 1) {
        lookCur[0] = n.look[0];
        lookCur[1] = n.look[1];
        lookCur[2] = n.look[2];
    }
    cameraInterp(static_cast<float>(endF - startF));         // FUN_0016dde0
    if (!platform::playerRun()) {                            // PORT (test log): voice end frames
        static int lastVoice = 0;
        const int v = eventVoiceStatus();
        if (lastVoice != 0 && v == 0) platform::log("event: voice end frame " + std::to_string(frame));
        lastVoice = v;
    }
    if (runCues(false)) {                                    // FUN_00175e30(mgr, 0)
        frame = frame + 1;
        const uint32_t ki = static_cast<uint32_t>(keyIndex);
        if (keys[ki + 1].frame <= frame && ki < static_cast<uint32_t>(count) - 1u && ki + 2 < keys.size()) {
            keyIndex = static_cast<int32_t>(ki + 1);
            cameraAccel();                                   // FUN_0016f250
            keyAdvanced = 1;
        }
        const uint32_t ci = static_cast<uint32_t>(cueIndex);
        if (cues[ci + 1].frame <= frame && ci < static_cast<uint32_t>(count) - 1u && ci + 2 < cues.size()) {
            cueIndex = static_cast<int32_t>(ci + 1);
            pc = 0;
        }
    }
}

// orig FUN_0016d3b0 (work/decompiled/0016d3b0.c)
void EventManager::tick(bool paused) {
    ticks = ticks + 1;
    ui::SpriteLayer& sl = ui::sprites();
    if (letterTop != nullptr) {
        if (letterBottom != nullptr &&
            (!scenes().isCurrent("Game....") || hudItemScreenOpen() || ui::menu().isOpen())) {   // 0x44d158, iRam00517b68
            sl.destroyRef(letterTop);
            sl.destroyRef(letterBottom);
        }
    }
    if (active == 0 && colorQuad != nullptr) sl.destroyRef(colorQuad);
    if (paused) return;
    if (letterTop != nullptr && letterBottom != nullptr) {
        if (letterState == 1 && -1 < letterTimer) {
            letterTop->cy = letterTop->cy + 1.6f;
            letterBottom->cy = letterBottom->cy - 2.1333334f;
            const int t = letterTimer;
            letterTimer = t - 1;
            if (t - 1 < 0) letterState = 2;
        } else if (letterState == 3 && -1 < letterTimer) {
            letterTop->cy = letterTop->cy - 1.6f;
            letterBottom->cy = letterBottom->cy + 2.1333334f;
            const int t = letterTimer;
            letterTimer = t - 1;
            if (t - 1 < 0) {
                sl.destroyRef(letterTop);
                sl.destroyRef(letterBottom);
                letterState = 0;
            }
        }
    }
    if (active != 0 && talkManager().active == 0) {          // iRam0053c6e8: no conversation running
        float v[3];
        if (lightDir[0] == 0.0f && lightDir[1] == 0.0f && lightDir[2] == 0.0f) {
            v[0] = -basis[2][0];
            v[1] = -basis[2][1];
            v[2] = -basis[2][2];
        } else {
            v[0] = lightDir[0];
            v[1] = lightDir[1];
            v[2] = lightDir[2];
        }
        LightSlot& ml = gLights.slot[gLights.mainSlot];      // FUN_00108b70(0x4e78e0, uRam0050a2cc, v)
        ml.dir[0] = v[0];
        ml.dir[1] = v[1];
        ml.dir[2] = v[2];
        bool loaded = true;
        for (Chr* c : characters().list()) {
            if ((c->taskFlags & 0x10000000u) != 0) continue;
            if (static_cast<uint32_t>(c->type) < 3u && !c->model.loaded()) {   // (**(chr+0x2c)+0x2c) & 0x80000000
                loaded = false;
                break;
            }
        }
        if (skipCountdown == -1 && loaded) step();           // FUN_0016dbf0
        skipTick();                                          // FUN_0016d260
    }
}

void eventEngineTick(bool paused) {
    if (gMgr != nullptr) gMgr->tick(paused);
    eventSkipPromptTick(gMgr, paused);   // PORT: hold-to-skip + prompt (event_skip_prompt.cpp)
    // the HUD / item-screen readers of iRam0051b620 and uRam0053c6a8 (ItemWorldState mirrors, written by their owner)
    hudWorld().eventRunning = eventActive();
    hudWorld().talkBusy = eventModeFlag() != 0;
}

// orig FUN_0016d1a0 (work/decompiled/0016d1a0.c)
void EventManager::skip(bool force) {
    if (skipCountdown == -1 && 0x3b < ticks && (force || (firstView == 0 && skippable != 0))) {
        skipCountdown = 0x1e;
        fade().set(0x80000000u, 0, 0);
        fade().set(0x80000000u, 0x23, 0);
    }
}

// orig FUN_0016d260 (work/decompiled/0016d260.c)
void EventManager::skipTick() {
    if (0 < skipCountdown) skipCountdown = skipCountdown - 1;
    if (skipCountdown == 0) {
        skipCountdown = -1;
        cueIndex = lastCue;
        pc = 0;
        runCues(true);                                       // FUN_00175e30(mgr, 1)
        end();                                               // FUN_0016cbf0
    }
}

// orig FUN_00177cc0 (work/decompiled/00177cc0.c)
void EventManager::requestTransition(float offset, int frames) {
    transRequest = 1;
    transRequestFrames = frames;
    transOffset = offset;
}

// orig FUN_00178600 (work/decompiled/00178600.c): texture release (+0x1f0), geometry release, frames -1
void EventManager::transitionRelease() {
    transCaptured = 0;
    transFrames = -1;
}

// orig FUN_0016cbf0 (work/decompiled/0016cbf0.c)
void EventManager::end() {
    if (camera == nullptr || active == 0) return;
    active = 0;
    if (gCamTask != nullptr) {                               // FUN_0010d9c0: pop *0x4e7b18, FUN_00207350
        gCamTask->pushed = false;
        gCamTask->flags |= 0x30000000u;                      // removed (and freed) by the next task-list walk
        gCamTask = nullptr;
    }
    camera = nullptr;
    ui::helpBar().clear(-1);                                 // FUN_0015c3f0(0x5180f0, -1)
    eventRegistryRestore();                                  // FUN_0012ccf0(0x50b130)
    if (letterState != 0 && letterState != 3) {
        letterTimer = 0x1e;
        letterState = 3;
    }
    if (equipOverride != 0) {
        eventEquipRestore();                                 // FUN_00177c40 -> FUN_001d8060(0x53d910)
        equipOverride = 0;
    }
    LightSlot& ml = gLights.slot[gLights.mainSlot];
    for (int k = 0; k < 3; ++k) ml.dir[k] = savedLightDir[k];          // FUN_00108b70
    for (int k = 0; k < 4; ++k) ml.color[k] = savedLightColor[k];      // FUN_00108ba0
    for (int k = 0; k < 4; ++k) ml.ambient[k] = savedLightAmbient[k];  // FUN_00108bd0
    if (gHooks.setFog) gHooks.setFog(savedFog[0], savedFog[1]);
    if (gHooks.setProjection) gHooks.setProjection(savedFar, savedFov);   // FUN_00101b60(+0x209b0, +0x209b4)
    for (EvtEffectSlot& e : effects) {
        if (e.handle != 0 && gHooks.effectAlive && gHooks.effectAlive(e.handle)) {   // FUN_00112500
            if (gHooks.effectKill) gHooks.effectKill(e.handle);                      // FUN_00112530
        }
    }
    if (light != -1) gLights.release(light);                 // FUN_00108ae0 (light kept in +0x20d1c)
    if (0 < transFrames) {
        transFrames = -1;
        transitionRelease();                                 // FUN_00178600
    }
    if (gHooks.padRumbleStop) gHooks.padRumbleStop();        // FUN_0020c540(0x4e6280, 0)
    if (gHooks.seStop) gHooks.seStop(0xfffe, 2);             // FUN_00115820(snd, 0xfffe, 2)
    if (!platform::playerRun() && eventVoiceStatus() != 0)  // PORT (test log)
        platform::log("event: end frame " + std::to_string(frame) + " stops a voice that is still playing");
    if (gHooks.voiceStop) gHooks.voiceStop(0xfffe, 2);       // FUN_00115bb0(snd, 0xfffe, 2)
    // uRam004e9c38 = 8: UNVERIFIED (sound-side word, not modelled)
    if (actorSet != 0) {
        actorSet = 0;
        if (Chr* p = characters().player()) eventActorSetRelease(*p, 1);   // FUN_001b2730(player, 1)
    }
    sound().setMenuDuck(0);                                  // FUN_001161d0(snd, 0)
    eventPlayerStatusRestore();                              // FUN_00138d00
    if (gHooks.gameCameraReset) gHooks.gameCameraReset();    // FUN_0013f0b0(uRam0050b624)
    platform::log("event: end");
    if (chained != -1) {
        ui::SpriteLayer& sl = ui::sprites();
        if (letterTop != nullptr) {
            sl.destroyRef(letterTop);
            sl.destroyRef(letterBottom);
        }
        sl.destroyRef(colorQuad);
        char name[0x40] = {};
        std::snprintf(name, sizeof name, "E%02d.EVT", chained);   // 0x44d148
        start(name);                                         // FUN_0016c6d0
        fade().set(0x80000000u, 0, 0);
        fade().set(0, 0x1e, 0);
    }
}

// ---------------------------------------------------------------------------------------------------- presentation
bool eventCameraMatrix(float out[4][4], float alpha) {
    if (gCamTask == nullptr || !gCamTask->pushed) return false;
    const EventCamera& c = gCamTask->cam;
    for (int r = 0; r < 4; ++r) {
        for (int k = 0; k < 3; ++k) out[r][k] = c.prev[r][k] + (c.world[r][k] - c.prev[r][k]) * alpha;
        out[r][3] = r == 3 ? 1.0f : 0.0f;
    }
    return true;
}
bool eventCameraIsActive() { return gCamTask != nullptr && gCamTask->pushed; }
bool eventTransitionCapturePending() { return gMgr != nullptr && gMgr->transRequest != 0; }
// orig FUN_00177cf0 (work/decompiled/00177cf0.c), state part (the 256x256 capture itself is the presentation's)
void eventTransitionCaptured() {
    EventManager& m = eventManager();
    if (m.transRequest == 0) return;
    m.transFrames = m.transRequestFrames;
    m.transAlpha = 1.0f;
    m.transStep = m.transAlpha / static_cast<float>(m.transFrames);
    m.transCaptured = 1;
    m.transRequest = 0;
}
float eventTransitionAlpha(float* offset) {
    if (gMgr == nullptr || gMgr->transFrames <= 0) return 0.0f;
    if (offset != nullptr) *offset = gMgr->transOffset;
    return gMgr->transAlpha;
}
// orig FUN_00178280 (work/decompiled/00178280.c), state part: alpha -= step, frames--, release at 0
void eventTransitionAdvance() {
    if (gMgr == nullptr || gMgr->transFrames <= 0) return;
    gMgr->transAlpha = gMgr->transAlpha - gMgr->transStep;
    gMgr->transFrames = gMgr->transFrames - 1;
    if (gMgr->transFrames < 1) gMgr->transitionRelease();
}

}  // namespace baroque::game
