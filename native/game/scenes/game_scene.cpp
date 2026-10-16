// Game scene (see game_scene.hpp).
#include "game/scenes/game_scene.hpp"

#include "alchemy/scene_render.hpp"
#include "audio/sound_system.hpp"
#include "data/archive.hpp"
#include "data/prm.hpp"
#include "game/camera.hpp"
#include "game/character/act_effects.hpp"
#include "game/character/act_props.hpp"
#include "game/character/act_trail.hpp"
#include "game/character/character.hpp"
#include "game/character/combat.hpp"
#include "game/character/model_fx.hpp"
#include "game/character/player_control.hpp"
#include "game/character/status_set.hpp"
#include "game/dungeon.hpp"
#include "game/engine.hpp"
#include "game/effects/effects.hpp"
#include "game/effects/effects_gl.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/event_services.hpp"
#include "game/event/event_sound.hpp"
#include "game/event/stage_script.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/floor_world.hpp"
#include "game/free_camera.hpp"     // PORT: free mouse camera
#include "game/free_camera_view.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/hud/pause.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/item.hpp"
#include "game/items/item_world.hpp"
#include "game/objects/objects.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/player_move.hpp"
#include "game/progress/player_progress.hpp"
#include "game/render2d.hpp"
#include "game/progress/progression.hpp"
#include "game/progress/save_record.hpp"
#include "game/post/post_render.hpp"
#include "game/post/screen_effects.hpp"
#include "game/post/shadow_pass.hpp"
#include "game/present/present_config.hpp"
#include "game/present/present_draw.hpp"
#include "game/present/present_layout.hpp"
#include "game/present/present_state.hpp"
#include "game/save_manager.hpp"
#include "game/rng.hpp"
#include "game/respawn.hpp"
#include "game/scenes/front_scenes.hpp"
#include "game/sound_stub.hpp"
#include "game/task_list.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/message_box.hpp"   // PORT: free camera input gate
#include "game/ui/texture_registry.hpp"
#include "platform/controls.hpp"     // PORT: free mouse camera mode
#include "platform/platform.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>

namespace baroque::game {

namespace {

GameEntry gEntry;

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Game scene object 0x50a2a0 (ctor FUN_0011a940). The progression fields (+4 flags, +8 floor, +0xc previous floor,
// +0x10 requested floor, +0x1c / +0x20 run-reset masks, +0x24.. play clock) are the GameProgress the progression
// module works on (game/progress/progression.hpp).
struct GameObj : GameProgress {
    GameObj() { floor = -1; prevFloor = -1; requestedFloor = 0; }
    int bgm = -1;                // +0x14
    int bgmDelay = 0;            // +0x18
    uint32_t clockFrames = 0;    // +0x28
};

struct GameRuntime {
    GameObj obj;
    std::unique_ptr<data::Archive> archive;
    std::unique_ptr<Dungeon> dungeon;
    std::unique_ptr<FloorWorld> world;
    data::PrmCharFile prm;
    PlayerControl ctl;                       // 0x50b620
    CameraWorld camWorld;
    EngineProjection proj;
    std::unique_ptr<Camera> cam;             // camera task (*0x4e7b18)
    float camPrev[4][4] = {};
    alchemy::SceneRenderer renderer;
    bool rendererReady = false;
    float fogNear = 755.0f, fogFar = 1500.0f;   // FUN_0011b810: fog attr +0x10 / +0x14
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // 0x4e7914..0x4e7920 (renderer +0x34): clear and fog colour
    int emitterHandles[0x20];                // floor context 0x50a350 +0x24: RDT emitter effect handles (-1 = none)
    int emitterRoom = -1;                    // floor context +0xa4: room whose emitters run
    bool active = false;                     // between Init and Exit
    bool floorReady = false;
    // player-control words of 0x50b620 that FUN_0013aaa0 uses beyond the port's PlayerControl struct
    int beatTimer = 0x3c;                    // 0x50b660 (+0x40) heartbeat countdown (0x3c at every floor Init)
    int beatPeriod = 0x3c;                   // 0x50b664 (+0x44) heartbeat period
    int lowHpTimer = 0;                      // 0x50b670 (+0x50) low-HP countdown
    int townAmbient = 0;                     // iRam0050a688 (FUN_0011edf0, floor 0x5b)
    int gameOverPhase = 0;                   // player death continuation PTMF (0x463c90 -> 0x463cb8 -> null)
    bool worldHidden = false;                // FUN_0011ed90: floor scene root flags 1 | 2 cleared
    // PORT: screen transition as stepped by renderTickPass (FUN_00177cf0 / FUN_00178280 state), read by the presentation
    uint32_t transCaptureSerial = 0;         // ++ on the tick FUN_00177cf0 took the capture (presentation grabs once)
    float transDrawAlpha = 0.0f;             // quad alpha FUN_00178280 draws this tick (before its alpha -= step)
    float transDrawOffset = 0.0f;            // +0x209d8 quad offset of this tick
    GameRuntime() { for (int& h : emitterHandles) h = -1; }
};
GameRuntime* gRt = nullptr;
GameRuntime& rt() {
    if (gRt == nullptr) gRt = new GameRuntime();
    return *gRt;
}

// uRam0050a610 as the game task reads it: the Dungeon's flags with the bits FUN_0011edf0 / the item code change
uint32_t floorFlagWord(const GameRuntime& r) {
    if (!r.dungeon) return 0;
    const ItemWorldState& w = hudWorld();
    return (r.dungeon->floorFlags() & ~w.clearedFloorFlags) | w.portFloorFlags;
}

// The inline "dead" test on a character: type 0 (player) or 2 (enemy) with status HP 0.
bool chrIsDead(const Chr& c) {
    bool v = true;
    if (c.type != 0 && c.type != 2) v = false;
    if (v) return c.status.block.hp == 0;
    return false;
}

// Services FUN_0013b000 drives (engine 0x4e6640, effects, sound manager 0x4e9790, event manager 0x51b620)
struct GamePauseHooks final : PauseHooks {
    void setEnginePaused(bool on) override {      // uRam004e6640 bit 0
        if (on) engine().flags |= 1u;
        else engine().flags &= ~1u;
    }
    void freezeEffects(bool on) override { if (eventHooks().effectsPause) eventHooks().effectsPause(on); }   // FUN_00112620
    void stopAllSe() override { sound().stopSe(0xfffe, 2); }   // FUN_00115900 = FUN_00115820(snd, 0xfffe, 2)
    void voicePause(bool on) override {                         // FUN_00115c50(snd, on) (bound by eventBindSoundSystem)
        if (eventHooks().voicePause) eventHooks().voicePause(on ? 1 : 0);
    }
    void bgmVolumeTarget(float target, int ticks) override { sound().setBgmFadeTarget(target, ticks); }   // FUN_00115f00
    void skipEvent() override { eventManager().skip(false); }   // FUN_0016d1a0(0x51b620, 0)
};
GamePauseHooks gPauseHooks;

PauseEnv pauseEnv() {
    PauseEnv e;
    e.enginePaused = engine().paused();          // FUN_00102020(0x4e6640)
    e.eventRunning = eventActive();              // iRam0051b620
    e.systemMenuOpen = hudReadEnvironment().systemMenuOpen;   // FUN_00167d70(0x51b140)
    e.itemScreenOpen = hudItemScreenOpen();      // iRam00517b68
    return e;
}

// orig FUN_0013aaa0 (work/decompiled/0013aaa0.c): pause (START), item screen open / input, heartbeat SE, idle flag.
// Called by the game task FUN_0011d850 before the character walk, also while paused.
void playerMenuInput(GameRuntime& r) {
    PlayerControl& ctl = r.ctl;                  // 0x50b620
    const uint32_t ff = floorFlagWord(r);        // uRam0050a610
    if ((ff & 1u) == 0 && (ff & 2u) == 0) return;
    if (fade().frames != 0) return;              // iRam004e7898
    if (stageWarpPause() == 1) return;           // uRam0053c6b0 == 1 (PT13 / PT14 warp chain, FUN_0017c0c0)
    const PadPort& pp = pad().port[0];
    if ((ctl.flags & 1u) == 0) {
        pauseInput(pp.pressed, pauseEnv(), gPauseHooks);   // FUN_0013af80(0x50b620)
        if (engine().paused()) return;                    // FUN_00102020(0x4e6640) == 1
        if (eventActive()) return;                        // iRam0051b620 == 1
    }
    Chr* pl = characters().player();             // iRam0050b144
    if ((ctl.flags & 1u) != 0) {
        hudItemScreenInput(pp.pressed, pp.repeat);   // FUN_00157a60(0x517b60)
        if (!hudItemScreenOpen()) {                  // iRam00517b68 == 0
            ctl.flags = ctl.flags & 0xfffffffeu;
            pauseToggle(false, pauseEnv(), gPauseHooks);   // FUN_0013b000(0x50b620, 0)
        }
    } else if (pl != nullptr && !chrIsDead(*pl)) {   // PORT: null player guard (the original reads iRam0050b144)
        const char* name = pl->act.currentName();    // FUN_00143d30(*(chr +0x2c) +0x10, -1)
        auto is2 = [](const char* n, char a, char b) { return n != nullptr && n[0] == a && n[1] == b; };
        if (name != nullptr && ctl.inputLock == 0 && (is2(name, 'M', 'V') || is2(name, 'W', 'T'))) {   // 0x44b148 / 0x44b150
            hudItemBagRequestTick(false, false);     // FUN_0015c070(0x517b60); iRam0053c6e8 (event camera) not modelled
            hudItemScreenInput(pp.pressed, pp.repeat);   // FUN_00157a60(0x517b60)
            if (hudItemScreenOpen()) {                   // iRam00517b68 == 1
                ctl.flags = ctl.flags | 1u;
                pauseToggle(true, pauseEnv(), gPauseHooks);   // FUN_0013b000(0x50b620, 1) [a1 = 1 at 0x13aca4]
                if (isScenarioFloor(r.obj.floor)) flags().set(0x61);   // FUN_0011c2c0(0x50a2a0, floor) -> FUN_00178890
            }
        }
        // heartbeat: period +0x44 grows by 2 per beat to 0x1e.. (then 0x3c) while unhurt; HitStagger, "DM" or HP below
        // 30 % of the maximum (FUN_001343f0) reset it to 10. SE 0x18a at period 0x3c, else 0x18b (volume 0.6).
        const bool lowHp = static_cast<float>(pl->status.block.hp) < static_cast<float>(statusHpMax(pl->status)) * 0.3f;
        const StateMachine& sm = pl->sm;
        const bool stagger = sm.next == StateId::None && !sm.popRequested && sm.curFactory == StateId::HitStagger;   // LAB_0012a490
        const bool damaged = is2(name, 'D', 'M');   // 0x44b158
        if (r.beatTimer == 0) {
            if (damaged || stagger || lowHp) {
                r.beatTimer = 10;
                r.beatPeriod = 10;
            } else {
                const int p = r.beatPeriod + 2;
                r.beatPeriod = p;
                if (0x1e < p) r.beatPeriod = 0x3c;
                r.beatTimer = r.beatPeriod;
            }
            if (r.beatPeriod == 0x3c) sound().playSe(0.6f, 0x18a);   // FUN_00115390(0x3f19999a, 0x4e9790, 0x18a)
            else sound().playSe(0.6f, 0x18b);
        } else {
            r.beatTimer = r.beatTimer - 1;
            if ((damaged || stagger) && 10 < static_cast<uint32_t>(r.beatTimer)) {
                r.beatTimer = 10;
                r.beatPeriod = 10;
            }
        }
        if (lowHp) {                                 // LAB_0013aedc
            if (r.lowHpTimer == 0) r.lowHpTimer = 0x14;
            else r.lowHpTimer = r.lowHpTimer - 1;
        }
    }
    // LAB_0013af10: any held button or stick clears the idle flag (FUN_001793b0(0x53c3b0, 0x480) = bit 0x480 + 0xaf9)
    if (pp.held != 0 || 0.0f < pp.lstick[2] || 0.0f < pp.rstick[2]) flags().clear(0x480 + 0xaf9);
}

// FUN_0011f6c0 (work/decompiled/0011f6c0.c) emitter part: stop every RDT emitter effect, ctx +0xa4 = -1
void emittersStop(GameRuntime& r) {
    for (int& h : r.emitterHandles) {
        if (h != -1) {
            effects().stop(h);                   // FUN_00112560(handle)
            h = -1;
        }
    }
    r.emitterRoom = -1;
}

// FUN_0011edf0 room-change block: the emitters of the room the player entered (RDT emitter records {effectId, x, y,
// z, rotY, scale}) are started with FUN_00112190(0, id, pos, (0, rotY, 0, 1), (s, s, s, 1)); room-relative on
// generated floors (FUN_0011c0a0), absolute on the fixed maps.
void emitterRoomChange(GameRuntime& r, int room) {
    for (int& h : r.emitterHandles) {
        if (h != -1) {
            effects().stop(h);                   // FUN_00112560(handle)
            h = -1;
        }
    }
    const Room& rm = r.dungeon->room(room);      // *(room * 4 + 0x50a640)
    const uint32_t n = rm.rdt.emitterCount;      // RDT +5
    for (uint32_t i = 0; i < n && i < 0x20; ++i) {
        const data::RdtEmitter& e = rm.rdt.emitters[i];
        float pos[4];
        if (isGeneratedFloor(r.obj.floor)) {
            pos[0] = e.x + static_cast<float>(rm.posX) * 8.0f * 200.0f + 100.0f;
            pos[2] = e.z + static_cast<float>(rm.posY) * 8.0f * 200.0f + 100.0f;
        } else {
            pos[0] = e.x;
            pos[2] = e.z;
        }
        pos[1] = e.y;
        pos[3] = 1.0f;
        const float rot[4] = {0.0f, e.rotYDeg, 0.0f, 1.0f};
        const float scale[4] = {e.scale, e.scale, e.scale, 1.0f};
        r.emitterHandles[i] = effects().spawn(0.0f, static_cast<int>(e.effectId), pos, rot, scale);   // FUN_00112190
    }
    r.emitterRoom = room;                        // ctx +0xa4
}

// orig FUN_0011edf0 (work/decompiled/0011edf0.c), floor context 0x50a350 per-tick part of the game task
void floorContextTick(GameRuntime& r) {
    ItemWorldState& w = hudWorld();
    w.clearedFloorFlags |= 8u;                   // uRam0050a610 &= ~8
    // FUN_0040caf0 / FUN_0040ca60: not translated
    hudMapTick();                                // FUN_00126380(0x50a350)
    chrRoomLights();                             // FUN_0011f160(0x50a350)
    const uint32_t ff = floorFlagWord(r);
    if ((ff & 1u) == 0 || !r.dungeon) return;
    Chr* pl = characters().player();
    if (pl != nullptr && pl->cell != nullptr) {
        if ((ff & 0x100000u) == 0) {
            // the stairs room (ctx +0x10) entered: target marker on the map, warp items aim there
            const int stairsRoom = r.dungeon->context().stairsRoom;
            if (stairsRoom != -1) {
                int room = -1;
                if ((*pl->cell & 0xf000u) == 0x1000u) room = static_cast<int>(*pl->cell & 0xfu);
                if (room == stairsRoom) w.portFloorFlags |= 0x100000u;
            }
        }
        int room = -1;                           // the player's room (cell type 0x1000), else -1
        if ((*pl->cell & 0xf000u) == 0x1000u) room = static_cast<int>(*pl->cell & 0xfu);
        if (-1 < room && r.emitterRoom != room) emitterRoomChange(r, room);
    }
    if (r.obj.floor == 0x5b) {                   // town ambience
        if (r.townAmbient == 0) {
            r.townAmbient = static_cast<int>(gameWorld().rng.rand(0x5a)) + 0x1c2;   // FUN_002071e0(0x53eb20, 0x5a)
        } else {
            r.townAmbient = r.townAmbient - 1;
            if (r.townAmbient == 0 && !eventActive() && !engine().paused())   // iRam0051b620 == 0, FUN_00102020 == 0
                sound().playSe(1.0f, 0x2d);      // FUN_00115390(1.0, 0x4e9790, 0x2d)
        }
    }
}

// Player death continuation of the death state (PTMF 0x463c90 = FUN_001cd9d0, then 0x463cb8 = FUN_001cdcd0). The
// state object lives in the character module, which flags the point the PTMF is reached (StateMachine::gameOver);
// the continuation is run from here, right after the character walk that ran the state.
void gameOverTick(GameRuntime& r) {
    Chr* pl = characters().player();
    if (pl == nullptr || !pl->sm.gameOver) {
        r.gameOverPhase = 0;
        return;
    }
    if (r.gameOverPhase == 0) {
        // orig FUN_001cd9d0 (work/decompiled/001cd9d0.c)
        // the ST05 revival branch (brand "ST05" -> "ST09") runs in the character module's death state, which then
        // never raises gameOver (OBJECTS_NATIVE.md)
        r.ctl.flags |= 0x10000u;                             // uRam0050b620 |= 0x10000
        fade().set(0, 0, 0);                                 // FUN_00106c00(0x4e7890, 0, 0, 0)
        fade().set(0xff000000u, 0x96, 1);
        r.gameOverPhase = 1;                                 // PTMF 0x463cb8
        // FUN_00115020(0x4e9790, 0) (work/decompiled/00115020.c): BGM fade-out over 0x10 (FUN_00115e10) and
        // uRam00540490 = uRam00540498 = 0 = audio::SoundSystem::sceneChangeStop(0)
        if (auto* m = dynamic_cast<audio::GameSoundManager*>(&sound())) m->system().sceneChangeStop(0);
        if (pl->status.block.vt == 0) flags().set(0x32e);
        return;
    }
    if (r.gameOverPhase == 1) {
        // orig FUN_001cdcd0 (work/decompiled/001cdcd0.c)
        if (fade().frames != 0) return;                      // iRam004e7898
        if ((playerCtl()->flags & 0x20u) != 0) modelFadeEnd(pl->model);   // first person: FUN_0010a130
        r.gameOverPhase = 2;                                 // +8.. = uRam0055fdc8.. (null PTMF)
        onStairsExit(r.obj);                                 // FUN_0011ce60 (named after PROGRESS_NATIVE.md; it is the
                                                             // HP-0 death flow, see EVENTS_NATIVE.md)
    }
}

// EXI pass view of the active camera (*0x4e7b18): 0x4e7b30 = inverse of the camera +0x18 matrix (FUN_0010da30),
// projection = engine fov/far at the 512x448 picture, fog *0x4e790c. PORT: `aspect` is 4:3 for the tick's update /
// recording (it feeds the UV-mode-8 screen copy of the original raster) and the presented aspect for the replay, so the
// particles line up with the Hor+ scene (PRESENTATION_PLAN.md V7).
ExiView effectView(const GameRuntime& r, const float world[4][4], float aspect = 4.0f / 3.0f) {
    alchemy::Mat4 camM = alchemy::Mat4::identity();
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 3; ++k) camM.m[size_t(row * 4 + k)] = world[row][k];
    alchemy::Mat4 view;
    if (!alchemy::invert(camM, view)) view = alchemy::Mat4::identity();
    const alchemy::Mat4 proj = present::presentProjection(r.proj.fovDeg, r.proj.nearZ, r.proj.farZ, aspect);
    ExiView v;
    for (int i = 0; i < 16; ++i) {
        v.view.m[i] = view.m[size_t(i)];
        v.projection.m[i] = proj.m[size_t(i)];
    }
    v.fogStart = r.fogNear;
    v.fogEnd = r.fogFar;
    return v;
}

// FUN_001120b0 (EXI pass of FUN_00101bd0, after the 3-D scene of the tick): update + recording, replayed by the
// presentation (effects_gl.hpp PORT split).
void effectTickPass(GameRuntime& r) {
    if (!r.cam || !r.floorReady) return;
    float w[4][4];
    if (!eventCameraMatrix(w, 1.0f)) std::memcpy(w, r.cam->camWorld, sizeof w);
    effectsTick(effectView(r, w));
}

// PORT split of the tick's render pass (PRESENTATION_PLAN.md §2.1). The original renders inside the engine tick
// (FUN_0011bec0 -> FUN_00101bd0): FUN_001075a0 evaluates every actor at the tick clock (FUN_003c5550, which runs the
// combiner update FUN_003c48c0 when its cache is stale) and the event manager's screen transition steps once per frame
// (FUN_00177cf0 capture state, FUN_00178280 alpha -= step / frames--). Those are state changes, so the port runs them
// here once per tick; the presentation (gameRender3D) only reads the results and may run any number of times per tick.
void renderTickPass(GameRuntime& r) {
    // FUN_001075a0 posing at the tick clock (no presentation clock shift): body, separate part actors (the player
    // head: FUN_00108d20 + pose), objects, class extras (E19 shell)
    for (Chr* c : characters().list()) {
        if ((c->taskFlags & 0x10000000u) != 0) continue;
        chrPose(*c);
    }
    // FUN_00177cf0 (row a) then FUN_00178280 (row j). As the port's previous presentation-side stepping: the capture
    // tick draws the full-alpha quad without stepping; every later tick draws the current alpha, then steps it.
    const bool captured = eventTransitionCapturePending();
    if (captured) {
        eventTransitionCaptured();
        ++r.transCaptureSerial;                  // the presentation grabs the picture on its next frame
    }
    float offset = 0.0f;
    r.transDrawAlpha = eventTransitionAlpha(&offset);
    r.transDrawOffset = offset;
    if (!captured) eventTransitionAdvance();
    // PORT (PRESENTATION_PLAN.md 2.2): snapshot of what this tick's 3-D pass shows -- the active camera *0x4e7b18
    // (event camera task, talk camera, else the gameplay camera) and every actor (game/present). Read-only.
    present::CamSnap cs;
    float w[4][4];
    if (eventCameraMatrix(w, 1.0f)) {
        cs.valid = true;
        cs.source = present::kCamEvent;
        cs.cutSerial = eventManager().camCutSerial;
    } else if (talkCameraMatrix(w)) {
        cs.valid = true;
        cs.source = present::kCamTalk;
    } else if (r.cam) {
        std::memcpy(w, r.cam->camWorld, sizeof w);
        cs.valid = true;
        cs.source = present::kCamGameplay;
        cs.cutSerial = r.cam->cutSerial;
    }
    if (cs.valid) std::memcpy(cs.world, w, sizeof w);
    {   // FUN_001075a0 skin swaps: pre-pass restore, FUN_00108d20 / FUN_001092a0 distance LOD at the tick eye
        // (inverse of 0x4e796c = inverse(cam +0x18): the camera position row; identity without a camera)
        const float eye[3] = {cs.valid ? w[3][0] : 0.0f, cs.valid ? w[3][1] : 0.0f, cs.valid ? w[3][2] : 0.0f};
        chrSkinLodTick(eye, post::shadowPass().enabled != 0);
    }
    // FUN_0013e340's light write is overwritten later in the tick by FUN_001a2110 while the conversation camera is up
    // (main light = -speaker forward), also on the push tick that still shows the gameplay camera
    cs.lightFollowsCamera = r.camWorld.lightFollowsCamera && talkManager().camera == nullptr;
    present::presenter().capture(cs, engine().tickCount);
}

// PORT: the presenter's frame clock for the igTransform inputs (alchemy::setSceneTimeSource) while the presentation
// draws between two ticks; the logic clock source (ActorAnim::clock) is restored after the 3-D pass
int64_t gPresentSceneClock = 0;
int64_t presentSceneClock() { return gPresentSceneClock; }

// PORT (PRESENTATION_PLAN.md V5): Chr::prevPos is the presentation's interpolation start (never read by logic). The
// unpaused walk refreshes it before each chrUpdate; ticks that skip the walk (event mode, pause) refresh it here so a
// character that does not move this tick is drawn still instead of between a stale position and its current one.
void refreshPrevPos() {
    for (Chr* c : characters().list())
        if ((c->taskFlags & 0x10000000u) == 0) c->prevPos = c->xf.m.pos;
}

// Game task 0x50a270: update FUN_0011d850 (work/decompiled/0011d850.c)
struct GameTask : Task {
    void update() override {
        GameRuntime& r = rt();
        // FUN_0013a990(0x50b620) pad vibration: not translated
        if (eventModeFlag() == 1) {                          // iRam0053c6a8
            refreshPrevPos();                                // PORT (V5): presentation marker only
            stageTickAll();                                  // FUN_0017a050
            return;
        }
        playerMenuInput(r);                  // FUN_0013aaa0(0x50b620)
        if (!engine().paused()) {            // FUN_00102020(0x4e6640) == 0
            characterWalk();
            stageTickAll();                  // FUN_0017a050
            gameOverTick(r);                 // PORT: the death state's PTMF continuation (see gameOverTick)
        } else {
            refreshPrevPos();                // PORT (V5): presentation marker only
        }
        floorContextTick(r);                 // FUN_0011edf0(0x50a350)
        // PORT: FUN_001144f0 / FUN_00114880(0x4e9750) (loading sprite while (0x50a610 & 3) == 0, freed after): the port
        // loads the floor synchronously, so the floor is ready on the first tick and the sprite is never shown
    }
    // FUN_0011d850 unpaused part: character list walk, hit manager
    void characterWalk() {
        if (!eventActive()) respawnTick(rt().dungeon.get(), rt().obj.floor);   // FUN_0012c880(0x50b130) (iRam0051b620 == 0)
        chrReap();                           // removed characters are unlinked during the walk (FUN_00207380)
        // by index: entities (spawners, helpers, items) are linked to the list during the walk
        for (size_t i = 0; i < characters().list().size(); ++i) {
            Chr* c = characters().list()[i];
            if ((c->taskFlags & 0x10000000u) != 0) continue;
            c->prevPos = c->xf.m.pos;
            chrUpdate(*c);                   // vt+0xc = FUN_00129a30
        }
        if (!eventActive()) hitManager().tick();   // FUN_0014e1b0(0x50c780) when iRam0051b620 == 0
        static const bool traceAi = [] {     // port diagnostics: BAROQUE_DEBUG_AI=1 traces enemies every 60 ticks
            char* v = nullptr;
            size_t n = 0;
            const bool on = _dupenv_s(&v, &n, "BAROQUE_DEBUG_AI") == 0 && v != nullptr;
            std::free(v);
            return on;
        }();
        if (traceAi && engine().tickCount % 60 == 0) {
            if (Chr* pl = characters().player()) {
                char pb[320];
                std::snprintf(pb, sizeof pb, "ai t=%llu PLAYER hp=%d/%d act=%s state=%d pos=(%.0f,%.0f,%.0f) stick=(%.2f,%.2f,%.2f) held=%x",
                              static_cast<unsigned long long>(engine().tickCount),
                              pl->status.block.hp, pl->status.block.hpMax, pl->act.currentName() ? pl->act.currentName() : "-",
                              static_cast<int>(pl->sm.cur), pl->xf.m.pos[0], pl->xf.m.pos[1], pl->xf.m.pos[2],
                              pad().port[0].lstick[0], pad().port[0].lstick[1], pad().port[0].lstick[2], pad().port[0].held);
                platform::log(pb);
            }
            for (const Chr* c : characters().list()) {
                if (c == characters().player()) continue;
                const char* act = c->act.currentName();
                char buf[256];
                std::snprintf(buf, sizeof buf, "ai t=%llu %s pos=(%.0f,%.0f,%.0f) act=%s flags=%08x tgt=%d dest=(%.0f,%.0f,%.0f) v=%u,%u,%u,%u",
                              static_cast<unsigned long long>(engine().tickCount), c->idString().c_str(), c->xf.m.pos[0], c->xf.m.pos[1], c->xf.m.pos[2],
                              act ? act : "-", c->ai.flags, c->ai.target != nullptr, c->ai.dest[0], c->ai.dest[1],
                              c->ai.dest[2], c->ai.v[0], c->ai.v[1], c->ai.v[2], c->ai.v[3]);
                platform::log(buf);
            }
        }
    }
};

// PORT (free mouse camera, CAMERA_NATIVE.md 7): the free camera takes mouse / camera-key input only when the original
// camera would take stick input. FUN_0013d970 (right stick) runs from the player controller FUN_0013b0d0 only while the
// game task walks the characters: engine unpaused (FUN_00102020; START pause FUN_0013b000, the item screen and
// conversations / prompts in event mode FUN_0017a0c0 all set the pause bit), no event mode (0x53c6a8), no event
// (0x51b620), ctl & 0x10000 clear and the player alive; FUN_0013e340 also turns only unpaused. Menu pages, message
// windows, the system menu and the item screen are tested explicitly as well, so nothing behind a menu moves.
bool freeCameraTakesInput(const GameRuntime& r) {
    if (!r.cam || !freeCamera().active) return false;
    if (engine().paused()) return false;                         // FUN_00102020(0x4e6640)
    if (eventModeFlag() != 0 || eventActive()) return false;     // uRam0053c6a8, iRam0051b620
    if ((r.ctl.flags & 0x10001u) != 0) return false;             // uRam0050b620: 0x10000 no input, 1 item screen
    const Chr* p = characters().player();
    if (p == nullptr || chrIsDead(*p)) return false;
    if (hudItemScreenOpen() || hudReadEnvironment().systemMenuOpen) return false;   // iRam00517b68, FUN_00167d70
    if (ui::menu().isOpen() || ui::messageBox().isOpen()) return false;
    return true;
}

// Camera task (vtable 0x4cdb70): update FUN_0010da30 -> FUN_0013e340
struct CameraTask : Task {
    void update() override {
        GameRuntime& r = rt();
        talkCameraPublish();   // the talk camera task's FUN_0010da30 in the same task-list walk (one view per walk)
        if (!r.cam) return;
        std::memcpy(r.camPrev, r.cam->camWorld, sizeof r.camPrev);
        if (Chr* p = characters().player()) {
            for (int k = 0; k < 3; ++k) {
                r.camWorld.playerPos[k] = p->xf.m.pos[size_t(k)];
                r.camWorld.playerForward[k] = p->xf.m.fwd[size_t(k)];
            }
        }
        r.camWorld.controlFlags = r.ctl.flags;
        r.camWorld.controlRotLock = int32_t(r.ctl.lock);
        r.camWorld.paused = engine().paused();
        r.camWorld.eventState = eventActive() ? 1 : 0;      // *(int*)0x51b620
        r.cam->isActiveCamera = !eventCameraIsActive() && !talkCameraActive();     // *0x4e7b18 == this
        r.camWorld.gameActive = (r.dungeon && (r.dungeon->floorFlags() & 1u) != 0);
        // FUN_0013e340 tail: the main light direction follows the camera (-forward) on generated floors
        // (FUN_0011c0a0(0x50a2a0) == 1) and on the town floor 0x5b (iRam0050a2a8)
        r.camWorld.lightFollowsCamera = isGeneratedFloor(r.obj.floor) || r.obj.floor == 0x5b;
        r.cam->tick();
        freeCameraTick(r);   // PORT
    }
    // PORT (free_camera.hpp): mode switch once per tick, after the original camera. The free camera replaces the
    // gameplay camera only (event / conversation cameras keep priority) and never in first person. It starts from the
    // original camera's direction and re-syncs whenever the original places itself (init / reset: floor start, R3).
    static void freeCameraTick(GameRuntime& r) {
        FreeCamera& fc = freeCamera();
        const bool want = platform::freeCameraEnabled() && (r.ctl.flags & 0x20) == 0;
        if (!want) {
            fc.active = false;
            fc.synced = false;
            return;
        }
        if (!fc.synced || fc.syncSerial != r.cam->cutSerial) freeCameraSync(r.cam->dir, r.cam->cutSerial);
        fc.active = r.cam->isActiveCamera;
        // keyboard camera keys (right stick bindings, default I J K L) turn the free camera too
        const PadPort& pp = pad().port[0];
        if (fc.active && pp.rstick[2] > 0.0f && freeCameraTakesInput(r)) freeCameraLook(4.0f * pp.rstick[0] * pp.rstick[2], 3.0f * pp.rstick[1] * pp.rstick[2]);
    }
};
GameTask gGameTask;
CameraTask gCameraTask;

// orig FUN_0011d360
void setBgm(GameObj& g, int id) {
    if (id != g.bgm) {
        g.bgm = id;
        g.bgmDelay = id != -1 ? 1 : 0;
    }
}

// orig FUN_0011d2a0: BGM by room type
void bgmByRoomType(GameObj& g, int type) {
    int id;
    switch (type) {
    case 0x32: id = 3; break;
    case 9: id = 0xb; break;
    case 99: id = 0x11; break;
    case 7: id = 10; break;
    case 6: id = 9; break;
    case 5: case 4: id = 8; break;
    case 8: case 3: id = 7; break;
    case 2: id = 6; break;
    case 1: id = 5; break;
    case 0: id = 4; break;
    default: id = -1; break;
    }
    setBgm(g, id);
}

// orig FUN_0011d3f0: BGM from the room the player stands in (cell type 0x1000 -> room index)
void pickBgm(GameRuntime& r) {
    Chr* p = characters().player();
    if (p == nullptr || p->cell == nullptr || !r.dungeon) return;
    const uint32_t v = *p->cell;
    const int room = (v & 0xf000u) == 0x1000u ? int(v & 0xf) : -1;
    if (room < 0 || room >= r.dungeon->roomCount()) return;
    const int mapSet = r.dungeon->fdt().mapSet;          // *pcRam0050a620
    const int type = int(r.dungeon->room(room).rdt.roomId);   // **(room+4): RDT byte 0 (room id)
    GameObj& g = r.obj;
    if ((mapSet == 1 && type == 0x1e) || (mapSet == 5 && type == 0x1e) || (mapSet == 4 && type == 0x1e) ||
        (mapSet == 8 && type == 0x28) || (mapSet == 3 && type == 0x28))
        setBgm(g, 0xe);
    else if (mapSet == 2 && type == 0x1e)
        setBgm(g, 0xf);
    else if ((mapSet == 4 && type == 0x1f) || (mapSet == 5 && type == 0x1f) || (mapSet == 6 && type == 0x1f))
        setBgm(g, 0xd);
    else if (mapSet == 2 && type == 0x1f)
        setBgm(g, 0x10);
    else
        bgmByRoomType(g, mapSet);
}

// orig FUN_0011d390
void startBgm(GameObj& g) {
    if (g.bgmDelay != 0 && --g.bgmDelay == 0) {
        sound().playBgm(g.bgm, 0x10, 1);
        flags().set(uint32_t(g.bgm + 0x39e));
    }
}

// FUN_00108b10(0x4e78e0, Game +0x2c, 1), FUN_00108b70 (direction), FUN_00108ba0 (colour), FUN_00108bd0 (ambient),
// FUN_00108c00 (specular, all floor setups pass 0) on the render list's main light slot. IGB map geometry carries
// baked vertex colours and no light attrs and the port's 3-D renderer does not light, so the values are kept in the
// renderer light slots (the event manager saves / restores them; the camera task rewrites the direction).
// UNVERIFIED: the direction's 4th word is uninitialised stack in all three setups (0 here).
void renderListMainLight(const float col[4], const float amb[4]) {
    LightSlot& l = renderLights().slot[renderLights().mainSlot];
    l.enabled = 1;
    const float dir[4] = {-1.0f, -1.0f, -1.0f, 0.0f};   // 0xbf800000 x3
    for (int k = 0; k < 4; ++k) {
        l.dir[k] = dir[k];
        l.color[k] = col[k];
        l.ambient[k] = amb[k];
        l.specular[k] = 0.0f;
    }
}

// Post-pass levels of the floor setups: FUN_0010e010(a, b, 0x4e7b80, frames) bloom, FUN_0010ed60(v, 0x4e7b80, frames)
// distance blur, FUN_0010fb00(alpha, 0x4e7b80, frames) film noise, through the same hooks the event cues 0x50 / 0x55 /
// 0x4b use (installed by installGameHooks: game/post/screen_effects.hpp).
void floorPostPasses(float bloomA, float bloomB, float effect3, float noise) {
    EventHooks& h = eventHooks();
    if (h.bloom) h.bloom(bloomA, bloomB, 0);
    if (h.screenEffect3) h.screenEffect3(effect3, 0);
    if (h.noise) h.noise(noise, 0);
}

void setClearColor(GameRuntime& r, float cr, float cg, float cb, float ca) {   // uRam004e7914..uRam004e7920
    r.clearColor[0] = cr;
    r.clearColor[1] = cg;
    r.clearColor[2] = cb;
    r.clearColor[3] = ca;
}

// orig FUN_0011b3e0 (work/decompiled/0011b3e0.c): town floor 0x5b
void floorSetupTown(GameRuntime& r) {
    const float col[4] = {0.75f, 0.65f, 0.55f, 1.0f};    // 0x3f400000, 0x3f266666, 0x3f0ccccd, 0x3f800000
    const float amb[4] = {0.35f, 0.3f, 0.25f, 1.0f};     // 0x3eb33333, 0x3e99999a, 0x3e800000, 0x3f800000
    renderListMainLight(col, amb);
    setProjection(r.proj, 14000.0f, 45.0f);              // FUN_00101b60(0x465ac000, 0x42340000, 0x4e6640)
    if ((r.ctl.flags & 0x20) == 0) r.cam->applyPreset(kCameraPresetHub);   // (30, 145, 400, -6)
    else r.cam->applyPreset(kCameraPresetFirstPerson);                     // (1, 120, 10, -2)
    setClearColor(r, 0.2f, 0.05f, 0.025f, 1.0f);         // 0x3e4ccccd, 0x3d4ccccd, 0x3ccccccd, 0x3f800000
    r.fogNear = 200.0f;                                  // *(iRam004e790c + 0x10) = 0x43480000
    r.fogFar = 3000.0f;                                  // +0x14 = 0x453b8000
    floorPostPasses(0.5f, 0.5f, 1100.0f, 0.12f);         // 0x3f000000 x2, 0x44898000, 0x3df5c28f
}

// orig FUN_0011b620 (work/decompiled/0011b620.c): outer-world floor 0x60 (no first-person branch)
void floorSetup60(GameRuntime& r) {
    const float col[4] = {0.75f, 0.65f, 0.55f, 1.0f};
    const float amb[4] = {0.35f, 0.3f, 0.25f, 1.0f};
    renderListMainLight(col, amb);
    setProjection(r.proj, 70000.0f, 48.0f);              // FUN_00101b60(0x4788b800, 0x42400000, 0x4e6640)
    r.cam->applyPreset(CameraTunables{50.0f, 185.0f, 300.0f, -15.0f});   // 0x42480000, 0x43390000, 0x43960000, 0xc1700000
    setClearColor(r, 0.2f, 0.05f, 0.025f, 1.0f);
    r.fogNear = 10000.0f;                                // 0x461c4000
    r.fogFar = 100000.0f;                                // 0x47c35000
    floorPostPasses(0.5f, 0.5f, 1100.0f, 0.12f);
}

// orig FUN_0011b810 (work/decompiled/0011b810.c): dungeon / event floors: lights, projection, camera preset, fog,
// bloom, noise
void floorSetupDungeon(GameRuntime& r) {
    const float col[4] = {0.75f, 0.75f, 0.75f, 1.0f};    // 0x3f400000 x3, 0x3f800000
    const float amb[4] = {0.25f, 0.25f, 0.25f, 1.0f};    // 0x3e800000 x3, 0x3f800000
    renderListMainLight(col, amb);
    if (isFloor99(r.obj.floor)) {
        setProjection(r.proj, 1500.0f, 45.0f);                   // 0x44bb8000, 0x42340000
        if ((r.ctl.flags & 0x20) == 0) r.cam->applyPreset(kCameraPresetHub);   // (30, 145, 400, -6)
        else r.cam->applyPreset(kCameraPresetFirstPerson);
    } else {
        setProjection(r.proj, 1500.0f, 48.0f);                   // 0x44bb8000, 0x42400000
        if ((r.ctl.flags & 0x20) == 0) r.cam->applyPreset(CameraTunables{50.0f, 185.0f, 300.0f, -15.0f});
        else r.cam->applyPreset(kCameraPresetFirstPerson);
    }
    setClearColor(r, 0.0f, 0.0f, 0.0f, 1.0f);           // uRam004e7914.. = 0, 0, 0, 0x3f800000
    r.fogNear = 755.0f;   // 0x443cc000
    r.fogFar = 1500.0f;   // 0x44bb8000
    floorPostPasses(0.5f, 0.5f, 1100.0f, 0.12f);
}

// orig FUN_0011d5e0 (work/decompiled/0011d5e0.c): reverb per floor, FUN_00115f40(0x4e9790, type, volume)
void floorReverb(const GameObj& g) {
    auto reverb = [](int type, int vol) { sound().setReverb(type, vol); };   // FUN_00115f40(0x4e9790, type, vol)
    const int id = g.floor;
    if (id == 0x5c) reverb(2, 0x3000);
    else if (id == 0x5d) reverb(1, 0x1000);
    else if (id == 0x5e) reverb(1, 0x800);
    else if (id == 0x5f) reverb(1, 0x3000);
    else if (id == 0x60) reverb(5, 0x2000);
    else {
        if (isRandomDungeon(id) && isFloor99(id)) {     // FUN_0011c290 then FUN_0011c100
            reverb(5, 0x4000);
            return;
        }
        if (id == 0x5b) reverb(4, 0x2000);
        else if (!isScenarioFloor(id)) {
            if (isRandomDungeon(id) || isSecondDungeon(id)) reverb(3, floorNumber(id) * 400 + 0x1800);
        } else {
            reverb(3, floorNumber(id) * 800 + 0x1000);
        }
    }
}

// Per-floor flags block of FUN_0011a9f0
void floorFlags(GameObj& g) {
    FlagManager& f = flags();
    const int id = g.floor;
    if (id == 0x5b) {
        f.set(0x3d6);
    } else if (id == 0x5c) {
        f.set(0x2e);
        f.set(0x3d7);
    } else if (isFloor99(id)) {
        f.set(0x3ed);
    } else if (isRandomDungeon(id)) {
        f.set(0x4f6);
        if (f.test(5)) {
            const int n = floorNumber(id);
            if (n == 0xd) f.set(0x2a);
            else if (n == 4) f.set(0x29);
            else if (n == 3) f.set(0x28);
            else if (n == 2) {
                if (f.test(0x26)) f.clear(0x26);
                else f.set(0x26);
                f.set(0x27);
            } else if (n == 0) {
                f.set(0x20);
                counters().add(0x1a, 1);     // FUN_00179570(0x53c5c0, 0x1a, 1)
                if (f.test(0x16)) f.setGuarded(0x45a);
                f.setGuarded(0x45f);
                if (f.test(0x25)) f.setGuarded(0x467);
            }
        }
        // table 0x48c960: {floor number, flag} pairs, first match
        static const uint32_t kTowerFlags[23][2] = {{0, 0x3d8}, {1, 0x3d9}, {2, 0x3da}, {3, 0x3db}, {4, 0x3dc}, {5, 0x3dd},
            {6, 0x3de}, {7, 0x3df}, {8, 0x3e0}, {10, 0x3e1}, {11, 0x3e2}, {12, 0x3e3}, {14, 0x3e4}, {15, 0x3e5},
            {16, 0x3e6}, {18, 0x3e7}, {20, 0x3e8}, {25, 0x3e9}, {28, 0x3ea}, {29, 0x3eb}, {39, 0x3ec}, {30, 0x3ef},
            {22, 0x3f0}};
        const uint32_t n = uint32_t(floorNumber(id));
        for (const auto& e : kTowerFlags)
            if (e[0] == n) { f.set(e[1]); break; }
    } else if (isScenarioFloor(id)) {
        if (isScenarioA(id)) f.set(uint32_t(floorNumber(id) + 0x2fc));
        else if (isScenarioB(id)) f.set(uint32_t(floorNumber(id) + 0x303));
        else if (isScenarioC(id)) f.set(uint32_t(floorNumber(id) + 0x30a));
    }
    else if (isSecondDungeon(id) && f.test(0x397)) {
        // table 0x48ca20: {floor number, flag}
        static const uint32_t kSecondFlags[5][2] = {{2, 0x34}, {5, 0x35}, {10, 0x36}, {15, 0x37}, {99, 0x38}};
        const uint32_t n = uint32_t(floorNumber(id));
        for (const auto& e : kSecondFlags)
            if (e[0] == n) { f.set(e[1]); break; }
    }
}

// FUN_0012ce70(0x50b1a0, tag, 1) and the spawn functions' transform writes. Every tag goes through the factory
// (ChrManager::createTag): characters, item entities (type 3) and dungeon objects (type 4, game/objects); a null
// result is the original's failed creation. Item entities mirror the transform writes into their Item, and after
// the routine's last write (the rotation) the item is placed (FUN_001d5620, as FUN_00120f70 / FUN_00121c50 do for
// item ids). "PT" tags are the stage objects of the exits (FUN_00179f60, game/event/stage_script): their handle is
// tracked so the transform writes reach StageObject::pos.
std::vector<StageObject*> gStageSpawns;
EntityHooks entityHooks() {
    EntityHooks h;
    gStageSpawns.clear();
    h.create = [](uint32_t tag) -> void* {
        if ((tag & 0xffffu) == 0x5450u) {   // "PT"
            char cmd[5] = {static_cast<char>(tag & 0xff), static_cast<char>((tag >> 8) & 0xff),
                           static_cast<char>((tag >> 16) & 0xff), static_cast<char>((tag >> 24) & 0xff), 0};
            StageObject* o = stageCreate(cmd);
            if (o != nullptr) gStageSpawns.push_back(o);
            else platform::log(std::string("stage: spawn dropped ") + cmd + " (stageCreate returned null)");   // PORT diagnostics
            return o;
        }
        return characters().createTag(tag, true);
    };
    auto stage = [](void* e) -> StageObject* {
        for (StageObject* o : gStageSpawns)
            if (o == e) return o;
        return nullptr;
    };
    auto sync = [](Chr* c) { if (c->item != nullptr) itemEntitySyncFromChr(*c); };
    auto place = [](Chr* c) { if (c->item != nullptr) itemEntityPlace(*c); };
    h.setPos = [sync, stage](void* e, const float* p) {
        if (StageObject* o = stage(e)) {
            o->pos[0] = p[0];
            o->pos[1] = p[1];
            o->pos[2] = p[2];
            return;
        }
        Chr* c = static_cast<Chr*>(e);
        c->xf.m.pos = {p[0], p[1], p[2]};
        c->prevPos = c->xf.m.pos;
        sync(c);
    };
    h.resetAxes = [sync, stage](void* e) {
        if (stage(e) != nullptr) return;
        Chr* c = static_cast<Chr*>(e);
        resetAxes(c->xf.m);
        sync(c);
    };
    h.rotateWorldUp = [sync, place, stage](void* e, float rad) {
        if (stage(e) != nullptr) return;
        Chr* c = static_cast<Chr*>(e);
        rotateAboutAxis(c->xf.m, rad, upVector());
        sync(c);
        place(c);
    };
    h.rotateUp = [sync, place, stage](void* e, float rad) {
        if (stage(e) != nullptr) return;
        Chr* c = static_cast<Chr*>(e);
        rotateAboutUp(c->xf.m, rad);
        sync(c);
        place(c);
    };
    h.entityId = [stage](void* e) -> uint32_t {                 // entity +0x1c (the item's id for item entities)
        if (stage(e) != nullptr) return 0;
        const Chr* c = static_cast<Chr*>(e);
        return c->item != nullptr ? c->item->id : c->id;
    };
    h.entityFlags = [stage](void* e) -> uint32_t {              // entity +0x70 (Item::flags)
        if (stage(e) != nullptr) return 0;
        const Chr* c = static_cast<Chr*>(e);
        return c->item != nullptr ? c->item->flags : 0u;
    };
    h.setEntityFlags = [stage](void* e, uint32_t v) {
        if (stage(e) != nullptr) return;
        Chr* c = static_cast<Chr*>(e);
        if (c->item != nullptr) c->item->flags = v;
    };
    h.flipItem = [sync, stage](void* e) {                        // orig FUN_001d4d70 (work/decompiled/001d4d70.c)
        if (stage(e) != nullptr) return;
        Chr* c = static_cast<Chr*>(e);
        if (c->item == nullptr) return;
        rotateAboutRight(c->xf.m, 3.1415927f);                   // FUN_00128c00(0x40490fdb, ent)
        sync(c);
        updateDiskFace(*c->item);                                // FUN_001d4db0
    };
    return h;
}

// FUN_0011f960 (synchronous): load, layout, collision, contents; then the world models
bool loadFloor(GameRuntime& r) {
    const int id = r.obj.floor;
    emittersStop(r);                              // FUN_0011f6c0 (previous floor's emitters, ctx +0xa4 = -1)
    hudWorld().portFloorFlags = 0;                // uRam0050a610 = 0 (Dungeon::load clears its own copy)
    hudWorld().clearedFloorFlags = 0;
    r.archive->setPack(data::Archive::packNameForFloor(id));
    r.dungeon = std::make_unique<Dungeon>(*r.archive, gameWorld().rng);
    r.dungeon->setGameFlags(GameFlags{[](uint32_t f) { return flags().test(f); }, [](uint32_t f) { flags().set(f); }});
    gameWorld().dungeon = r.dungeon.get();
    {
        EntityHooks h = entityHooks();
        h.placeItem = [](void* e) {                            // FUN_001d5620 on the FUN_00121f80 items
            Chr* c = static_cast<Chr*>(e);
            if (c->item != nullptr) itemEntityPlace(*c);
        };
        // FUN_00120260's player writes, at their point of the sequence (before FUN_00120a70(ctx, 1) creates the
        // characters). The camera reset FUN_0013e130 / FUN_0013e300 runs after the load (GameScene::init).
        h.placePlayer = [](const PlayerStart& s) {
            Chr* p = characters().player();                    // iRam0050b144
            if (p == nullptr || !s.valid) return;
            resetAxes(p->xf.m);                                // FUN_001286c0
            if (s.shift) {                                     // reload path: position shifted, no rotation
                p->xf.m.pos[0] = p->xf.m.pos[0] - s.x;
                p->xf.m.pos[2] = p->xf.m.pos[2] - s.z;
            } else {
                p->xf.m.pos = {s.x, s.y, s.z};                 // transform +0xc..+0x14
                rotateAboutUp(p->xf.m, s.angleRad);            // FUN_00128ae0(angle, player)
            }
            p->prevPos = p->xf.m.pos;
            // the original activates the player only after its asynchronous model load (FUN_00109c80), i.e. after the
            // floor exists; the port loads synchronously during Init, so clear any airborne state from that first tick
            p->phys.flags = 0;
            p->phys.vy = 0.0f;
        };
        h.snapshotTagCounts = [] { floorTagSnapshot(); };     // thunk_FUN_00127ff0(0x50b130)
        h.scatterOuterBoxes = [] {                             // FUN_001d9a20(0x53d910)
            if (Dungeon* d = gameWorld().dungeon) scatterOuterBoxes(*d);
        };
        r.dungeon->setEntityHooks(std::move(h));
    }
    // orig FUN_001d30c0 (work/decompiled/001d30c0.c) after its rand(0xff) (drawn by Dungeon::pickItem): the first row
    // of the class whose drop weight FUN_001d37d0 exceeds the remainder; id "%s%02d" (0x4641a0)
    r.dungeon->setItemPicker([](int cls, uint32_t rnd) -> uint32_t {
        ItemSystem& sys = itemSystem();
        int v = static_cast<int>(rnd);
        const int n = sys.rowCount(cls);
        for (int i = 0; i < n; ++i) {
            const int w = dropWeight(sys, *sys.row(cls, i));
            if (v < w) return makeItemId(cls, i);
            v -= w;
        }
        return 0;
    });
    std::string err;
    if (!r.dungeon->load(id, r.obj.prevFloor, &err)) {
        platform::log("game: floor " + std::to_string(id) + ": " + err);
        return false;
    }
    r.dungeon->generateLayout();
    if (!r.dungeon->loadCollision(&err)) {
        platform::log("game: floor collision: " + err);
        return false;
    }
    r.dungeon->placeContents();
    r.world = std::make_unique<FloorWorld>();
    if (!r.world->build(*r.dungeon, *r.archive, true, &err)) return false;
    for (const auto& m : r.world->missing) platform::log("game: missing " + m);
    r.world->scene().playerPos = characters().player() ? &characters().player()->xf.m.pos : nullptr;
    post::shadowPass().buildReceivers(*r.dungeon);   // receiver group 0x4e79b0 (FUN_001277c0 / FUN_00123280)
    r.world->scene().characterPush = chrPush;                    // FUN_00142290 from FUN_001416b0
    r.world->scene().characters = [](std::vector<CollBody>& out) {   // list 0x50a7b0 (FUN_00140250 tail)
        for (Chr* c : characters().list()) {
            if (c->type > kChrEnemy) continue;
            CollBody b;
            b.owner = c;
            b.phys = &c->phys;
            b.pos = &c->xf.m.pos;
            b.right = &c->xf.m.right;
            b.fwd = &c->xf.m.fwd;
            b.id0 = static_cast<char>(c->id & 0xff);
            b.active = c->active();
            b.removed = (c->flags24 & 0x10000000u) != 0;
            b.solid = chrSolidObstacle(*c);
            b.cell = c->cell;
            b.cellX = c->cellX;
            b.cellY = c->cellY;
            out.push_back(b);
        }
    };
    r.world->scene().cellAt = [](const Vec3f& p) -> const uint32_t* {   // FUN_00124c70
        Dungeon* d = gameWorld().dungeon;
        int x = 0, y = 0;
        return d != nullptr ? d->cellAtWorld(p.data(), &x, &y) : nullptr;
    };
    r.worldHidden = false;
    setCollisionScene(&r.world->scene());
    freeCameraWorldReset();   // PORT: free mouse camera occluders follow the floor world
    // FUN_00120260's player transform was written by the placePlayer hook during Dungeon::placeContents
    return true;
}

// orig FUN_001229d0 (work/decompiled/001229d0.c; flag 1 of the first test from the disasm 0x1229dc), called at the
// end of the floor finalize FUN_00120060: story events of the outer-world / event floors.
void floorEvents(GameRuntime& r) {
    GameObj& g = r.obj;
    FlagManager& f = flags();
    if (g.floor == 0x5f) {
        if (!f.test(1)) eventTrigger(1);
        else if (f.test(2)) eventTrigger(0x33);
        else if ((r.ctl.flags & 0x100000u) == 0) eventTrigger(0x34);
        else {
            r.ctl.flags &= 0xffefffffu;
            eventTrigger(0x35);
        }
    } else if (g.floor == 0x5c) {
        if (f.test(0xf)) {
            // FUN_0012ce70(0x50b1a0, "IS01") at (0, 100, 0), FUN_001d5620 (placed in the world), stage "PT13"
            Item* it = gameAdapter().createItem(itemId("IS01"), true);
            if (it != nullptr) {
                it->pos = Vec3{0.0f, 100.0f, 0.0f};
                placeItem(*it, itemSystem());                // FUN_001d5620
            }
            if (StageObject* o = stageCreate("PT13")) o->linked = it;
        } else if (f.test(0x58)) {
            eventTrigger(6);
        } else {
            eventTrigger(0x3e);
        }
    } else if (isFloor99(g.floor) && f.test(0x21)) {
        if (inventory().hasId(itemId("GN00"))) {
            // orig FUN_001d9860(0x53d910) (work/decompiled/001d9860.c): every carried gun (+0x24 class 0xc) gets
            // +0x6c = 5 (the rifle reloaded for the angel)
            Inventory& inv = inventory();
            for (int i = 0; i < inv.count; ++i)
                if (inv.slots[i] != nullptr && inv.slots[i]->cls == kClsGN) inv.slots[i]->count = 5;
        } else {
            Item* it = gameAdapter().createItem(itemId("GN00"), true);
            if (it != nullptr) {
                it->pos = Vec3{0.0f, 0.0f, 1000.0f};
                placeItem(*it, itemSystem());                // FUN_001d5620
            }
        }
    } else if (g.floor == 0x60) {
        if (f.test(0x58)) eventTrigger(0x47);
        else eventTrigger(0x48);
    } else if (r.dungeon && r.dungeon->fdt().mapSet == 6) {
        gameWorldToggle(f.test(0x49) ? 0 : 1);      // FUN_001b8430(FUN_00178a80(0x53c3b0, 0x49) == 0)
    }
}

int playerStatWord(int offset) {
    const Chr* p = characters().player();
    if (p == nullptr) return 0;
    const StatusBlock& b = p->status.block;
    switch (offset) {
    case 0x00: return b.hpMax;
    case 0x04: return b.hp;
    case 0x08: return b.vtMax;
    case 0x0c: return b.vt;
    case 0x10: return b.atkBase;
    case 0x14: return b.atkMax;
    case 0x18: return b.atkLevel;
    case 0x1c: return b.defBase;
    case 0x20: return b.defMax;
    case 0x24: return b.defLevel;
    case 0x28: return b.critical;
    case 0x2c: return b.expValue;
    case 0x48: return b.level;
    case 0x4c: return b.exp;
    case 0x50: return b.expNext;
    default: return 0;
    }
}

// Hooks of the progression / save / flag / event / stage modules over the Game scene's objects (PROGRESS_NATIVE.md 6)
void installGameHooks() {
    static bool done = false;
    if (done) return;
    done = true;
    FlagHooks& fh = flagHooks();
    fh.floorId = [] { return rt().obj.floor; };                         // Game +8
    fh.playerStatusBit = [](uint32_t bit) {                       // FUN_00138850(*(player +0x2c) +8, bit)
        if (Chr* p = characters().player()) statusSet(p->status, bit);   // PORT: guard (the original has a player)
    };
    ProgressHooks& ph = progressHooks();
    ph.playerExists = [] { return characters().player() != nullptr; };
    ph.playerStat = [](int offset) { return playerStatWord(offset); };
    ph.playerStatusNew = [] { if (Chr* p = characters().player()) statusPlayerNew(p->status); };     // FUN_00133a60
    ph.playerHiddenStats = [] { if (Chr* p = characters().player()) statusHiddenStats(p->status); };  // FUN_001395d0
    ph.playerSetAnim = [](const char* name) {                                                        // FUN_0010a7f0
        if (Chr* p = characters().player()) {
            const int clip = p->model.anim.findClip(name);
            if (clip >= 0) p->model.anim.set(clip);
        }
    };
    ph.revivePlayer = [] { playerRevive(); };                                                        // FUN_0011a8a0
    ph.requestScene = [](const char* name) { scenes().requestScene(name); };                        // FUN_00206550
    ph.triggerEvent = [](int n) { eventTrigger(n); };                                               // FUN_001d1d30
    ph.postRunScript = [](const char* cmd) {                                // FUN_00179f60(cmd) + FUN_0017b640(obj)
        if (StageObject* o = stageCreate(cmd)) stageRunEnd(*o);
    };
    ph.itemWorldRunEnd = [] {                                               // FUN_001d98b0 + FUN_001d5e60 (0x53d910)
        inventory().newGameReset(itemSystem());
        inventory().clear(itemSystem());
    };
    SaveHooks& sh = saveHooks();
    sh.writeDatabase = [](uint8_t* dst) {                                   // FUN_001db370(0x53dc00)
        static ItemSaveRecord rec[CollectorBag::kSlots];
        collectorBag().save(rec);
        std::memcpy(dst, rec, sizeof rec);
    };
    sh.readDatabase = [](const uint8_t* src) {                              // FUN_001db3f0(0x53dc00)
        static ItemSaveRecord rec[CollectorBag::kSlots];
        std::memcpy(rec, src, sizeof rec);
        collectorBag().load(rec, itemSystem());
    };
    sh.writeItemSlots = [](uint8_t* s) {                                    // FUN_001da6e0(0x53d910, ...)
        static ItemSaveRecord rec[Inventory::kMaxItems];
        int words[Inventory::kMaxItems] = {};
        int equip[4] = {};
        uint32_t backup[0x66] = {};
        inventory().save(rec, words, equip, backup);
        std::memcpy(s + slot::kItemRecords, rec, sizeof rec);
        std::memcpy(s + slot::kItemWords, words, sizeof words);
        std::memcpy(s + slot::kItemEquip, equip, sizeof equip);
        std::memcpy(s + slot::kItemBackup, backup, sizeof backup);
    };
    sh.readItemSlots = [](const uint8_t* s) {                               // FUN_001da8d0(0x53d910, ...)
        static ItemSaveRecord rec[Inventory::kMaxItems];
        int words[Inventory::kMaxItems] = {};
        int equip[4] = {};
        uint32_t backup[0x66] = {};
        std::memcpy(rec, s + slot::kItemRecords, sizeof rec);
        std::memcpy(words, s + slot::kItemWords, sizeof words);
        std::memcpy(equip, s + slot::kItemEquip, sizeof equip);
        std::memcpy(backup, s + slot::kItemBackup, sizeof backup);
        inventory().load(rec, words, equip, backup, itemSystem());
    };
    sh.gameFloor = [] { return rt().obj.floor; };                           // uRam0050a2a8
    sh.gameClock = [](uint8_t* sec, uint8_t* mn, uint16_t* hour) {          // Game +0x24 / +0x25 / +0x26
        *sec = rt().obj.sec;
        *mn = rt().obj.min;
        *hour = rt().obj.hour;
    };
    sh.playerExists = [] { return characters().player() != nullptr; };
    sh.playerBlockWord4 = [] { return playerStatWord(4); };
    sh.playerStatusNew = [] { if (Chr* p = characters().player()) statusPlayerNew(p->status); };
    sh.playerBlockWord4Clear = [] { if (Chr* p = characters().player()) p->status.block.hp = 0; };
    sh.playerHiddenStats = [] { if (Chr* p = characters().player()) statusHiddenStats(p->status); };
    sh.writePlayer = [](uint8_t* d) { if (Chr* p = characters().player()) playerRecordWrite(p->status, d); };   // FUN_00139750
    sh.readPlayer = [](const uint8_t* d) { if (Chr* p = characters().player()) playerRecordRead(p->status, d); };  // FUN_00139b40
    // sh.databaseCounter (0x53dcc8), writeInventoryA / writeOuterBoxes (0x53d7d0 list, 0x53d910 +0xbc boxes): the items
    // module has no translation of FUN_001b58a0 / FUN_001daad0 yet; the slot bytes round-trip unchanged.
    StageHooks& st = stageHooks();
    st.hideFloor = [] { rt().worldHidden = true; };                         // FUN_0011ed90(0x50a350)
    st.requestFloor = [](int floor) { requestFloor(rt().obj, floor, 1); };  // FUN_0011bfe0(0x50a2a0, floor, 1)
    st.currentFloor = [] { return rt().obj.floor; };
    EventHooks& eh = eventHooks();
    eh.fov = [] { return rt().proj.fovDeg; };                               // iRam004e66cc
    eh.farZ = [] { return rt().proj.farZ; };                                // iRam004e66d4
    eh.setProjection = [](float farZ, float fov) { setProjection(rt().proj, farZ, fov); };   // FUN_00101b60
    eh.fog = [](float* a, float* b) { *a = rt().fogNear; *b = rt().fogFar; };          // *(0x4e790c) +0x10 / +0x14
    eh.setFog = [](float a, float b) { rt().fogNear = a; rt().fogFar = b; };
    eh.gameCameraReset = [] { if (rt().cam) rt().cam->requestReset(); };   // FUN_0013f0b0(uRam0050b624)
    eh.mapHide = [] { hudMapHide(); };                                      // FUN_00126fb0(0x50a350)
    eh.mapIdCount = [] { return rt().dungeon ? rt().dungeon->roomCount() : 0; };
    eh.mapRoomId = [](int i) { return static_cast<int>(rt().dungeon->room(i).rdt.roomId); };   // **(0x50a640[i] + 4)
    // screen-effects object 0x4e7b80 and the shadow pre-pass object 0x4e79b4 (game/post)
    eh.bloom = [](float a, float b, int frames) { post::screenEffects().bloomOn(a, b, frames); };   // FUN_0010e010
    eh.bloomOff = [](int) { post::screenEffects().bloomOff(); };                                    // FUN_0010e0a0
    eh.noise = [](float a, int frames) { post::screenEffects().noiseOn(a, frames); };               // FUN_0010fb00
    eh.noiseOff = [](int) { post::screenEffects().noiseOff(); };                                    // FUN_0010fb70
    eh.screenEffect3 = [](float v, int frames) { post::screenEffects().dofOn(v, frames); };         // FUN_0010ed60
    eh.screenEffect3Off = [](int) { post::screenEffects().dofOff(); };                              // FUN_0010edc0
    eh.shadowPass = [](bool on) {                                       // FUN_00111db0 / FUN_00111df0 (0x4e79b4)
        if (on) post::shadowPass().on();
        else post::shadowPass().off();
    };
    eh.generatedFloor = [] { return isGeneratedFloor(rt().obj.floor); };   // FUN_0011c0a0(0x50a2a0)
    eh.mapRoomOrigin = [](int i, int axis) {                                // room +0xc / +0xe
        const Room& rm = rt().dungeon->room(i);
        return axis == 0 ? static_cast<int>(rm.posX) : static_cast<int>(rm.posY);
    };
    eh.activeCameraWorld = [](float w[4][4]) {                              // *0x4e7b18 +0x18 (the gameplay camera)
        if (!rt().cam) return false;
        std::memcpy(w, rt().cam->camWorld, sizeof rt().cam->camWorld);
        return true;
    };
    // orig FUN_00179cb0 registry walk (mask 0x10081, AUDIT_FRONTEND F18): actors of 0x50b130 named 'O' .. 'A' are
    // counted; the walk stops at the first whose +0x2c bit 0 (the O??A "fed" flag, objects_classes.cpp) is clear.
    // UNVERIFIED: whether the registry still lists actors flagged for removal (0x10000000); they are skipped here.
    counterResetHooks().scanObjects = [](int* count, bool* anyUnidentified) {
        for (const Chr* c : characters().list()) {
            if ((c->taskFlags & 0x10000000u) != 0 || c->obj == nullptr) continue;
            if ((c->id & 0xffu) != 'O' || ((c->id >> 24) & 0xffu) != 'A') continue;
            ++*count;
            if ((c->obj->I(0x2c) & 1) == 0) {
                *anyUnidentified = true;
                return;
            }
        }
    };
    engine().eventFrame = [](bool paused) { eventEngineTick(paused); };     // FUN_0016d3b0 in FUN_00101bd0
    eventBindEffects();                                                     // EXI wrappers 0x112190..
}

class GameScene : public Scene {
public:
    // orig FUN_0011a9f0
    void init() override {
        GameRuntime& r = rt();
        GameObj& g = r.obj;
        present::presenter().reset();               // PORT: no interpolation across a scene / floor change
        fade().set(0xff000000u, 0, 0);
        fade().set(0, 0xf, 0);
        fade().setPause(1);
        const bool prevGame = scenes().isPrevious("Game....");
        const bool prevEnding = scenes().isPrevious("Ending..");
        installGameHooks();
        if (!prevGame) {
            // FUN_0011b300
            g.sec = 0;
            g.min = 0;
            g.hour = 0;
            g.bgm = -1;
            sound().init();
            optionApply();
            ui::gradientCreate();
            hudGameInit();                          // FUN_001520d0(0x517b60), FUN_0014ee70(0x5179c0)
            ui::textures().loadRange(0x23, 0x2b);
            r.archive = std::make_unique<data::Archive>(platform::assetRoot());
            characters().assetRoot = platform::assetRoot();
            if (r.prm.records.empty()) {
                std::string err;
                auto prm = data::parsePrmChar(readFile(platform::assetRoot() / "DATA/PARAM/PRMCHAR.PRM"), &err);
                if (prm) r.prm = std::move(*prm);
                else platform::log("game: PRMCHAR " + err);
            }
            characters().prm = &r.prm;
            tasks().insert(&gGameTask, 0);
            // player: FUN_0012d2f0(0x50b1a0, "C00", 1); +0x28 = 0x11; vt+0xc; FUN_0012be80(0x50b130, chr)
            Chr* p = characters().create("C00", true);
            if (p != nullptr) {
                p->f28 = 0x11;
                characters().setPlayer(p);
                chrUpdate(*p);
            }
            // camera task FUN_0011d7e0
            r.cam = std::make_unique<Camera>(r.camWorld, r.proj, makeHooks());
            tasks().insert(&gCameraTask, 0);
            r.ctl.camera = r.cam.get();                              // 0x50b620 +0x04 = camera task (0x50b624)
            setPlayerCtl(&r.ctl);
            combatHooks().cameraPos = [] {                           // camera task +0x7c..+0x84
                GameRuntime& rr = rt();
                return rr.cam ? Vec3f{rr.cam->camWorld[3][0], rr.cam->camWorld[3][1], rr.cam->camWorld[3][2]} : Vec3f{};
            };
            playerControlHook = [](Chr& c) { playerControl(rt().ctl, c); };   // FUN_0013b0d0(0x50b620, chr)
            {   // inventory 0x53d910 / status HUD 0x5179c0 / message log 0x5180f0 behind the character core's hooks
                CombatHooks& ch = combatHooks();
                ch.hudHp = [](int hp) { hudSetHp(hp); };                      // FUN_00151ae0(0x5179c0, hp)
                ch.hudVt = [](int vt) { hudSetVt(vt); };                      // uRam00517b2c = vt
                ch.log = [](int msg, int value) { gameAdapter().message(msg, value); };   // FUN_0015d480(FUN_00114da0(msg), v)
                ch.hpMaxTotal = [](const StatusObj& st) { return statusHpMaxWithItems(st); };   // FUN_001343f0
                ch.playerAtk = [](int hand) {                                 // FUN_001d83d0 (hand < 0) / FUN_001d84b0
                    return hand < 0 ? inventory().atkTotal(itemSystem()) : inventory().atkOfHand(hand != 0, itemSystem());
                };
                ch.playerDef = [] { return inventory().defTotal(itemSystem()); };   // FUN_001d85b0
            }
            // end of FUN_0011b300: FUN_0010db20(0x4e7b80, host) screen-effects object
            post::screenEffects().construct();
            post::postRenderer().construct();
        }
        // FUN_0011a9f0 save / new-game / re-entry branch (floor shift +8 <- +0x10, prev <- +8, +0x10 = -1, the
        // FUN_0011a8a0 rebirth, the Ending return, FUN_00178ad0 / FUN_00116c90 new game, FUN_00116a50 continue, +4 &= ~1)
        saveHooks().newGameFlag = front().newGame;  // DAT_00469700
        const bool rebirth = prevGame && (g.flags4 & 1u) != 0;
        gameInitProgress(g, prevGame, prevEnding, front().newGame, front().loadSlot, saveManager().fileData());
        if (Chr* p = characters().player()) {
            if (rebirth || prevEnding) p->sm.gameOver = false;   // PORT: the death continuation flag of the old run
            // UNVERIFIED: FUN_0017b640 hid the player's model node at the run end; the code that shows it again was
            // not located, the model is shown at every floor start
            for (ChrModelPart& part : p->model.parts) part.visible = true;
        }
        r.gameOverPhase = 0;
        r.beatTimer = 0x3c;                         // uRam0050b660
        r.beatPeriod = 0x3c;                        // uRam0050b664
        hudFloorInit();                             // FUN_001d8210(0x53d910), FUN_001d5ef0(0x53d910)
        if (g.floor == 0x5b) floorSetupTown(r);    // FUN_0011b3e0
        else if (g.floor == 0x60) floorSetup60(r);  // FUN_0011b620
        else floorSetupDungeon(r);                  // FUN_0011b810
        floorFlags(g);
        r.floorReady = loadFloor(r);
        hudFloorReady();                            // iRam00517b34 = depth; FUN_00151af0(0x5179c0, 0)
        if (r.floorReady) {
            hudMapCreate();                         // FUN_00126120(0x50a350) at the head of FUN_00120060
            // tail of FUN_00120060 (floor finalize; the cell work runs inside Dungeon::placeContents):
            Chr* p = characters().player();
            if (p != nullptr && g.floor == 0x144) {
                // FUN_001349e0(st, -(hp >> 1)): HP delta with the clamp, HUD update and low-HP flags 0x401/0x2fb/0x2fc
                statusAddHp(p->status, -(p->status.block.hp >> 1));
            }
            r.ctl.flags &= 0xfffeffffu;              // uRam0050b620 &= ~0x10000 (FUN_0011ed10 == 0 path)
            fade().setPause(0);                       // FUN_00106810(0x4e7890, 0)
            // FUN_001229d0 (story events) ends FUN_00120060; it runs below, after the camera init (see there)
            bgmByRoomType(g, r.dungeon->fdt().mapSet);   // FUN_0011d2a0(0x50a2a0, *0x50a620)
            aiBuildNodes();                                   // FUN_001f01f0
            {   // port diagnostics: entities created by the floor
                std::string ids;
                int n = 0;
                for (const Chr* c : characters().list())
                    if (c != characters().player()) { ids += " " + c->idString(); ++n; }
                platform::log("game: floor " + std::to_string(g.floor) + " characters:" + std::to_string(n) + ids);
            }
        }
        if (Chr* p = characters().player()) {   // the camera reads the player through 0x50b144 directly
            for (int k = 0; k < 3; ++k) {
                r.camWorld.playerPos[k] = p->xf.m.pos[size_t(k)];
                r.camWorld.playerForward[k] = p->xf.m.fwd[size_t(k)];
            }
        }
        if (r.cam) r.cam->init();
        std::memcpy(r.camPrev, r.cam->camWorld, sizeof r.camPrev);
        // FUN_001229d0 at the end of FUN_00120060: the original's floor finalize runs from the asynchronous load
        // callback, i.e. after Init returned and the camera task was reset; the port loads synchronously, so the call
        // is placed after the camera init (FUN_0016b920 copies that camera into the event manager).
        if (r.floorReady) floorEvents(r);
        floorReverb(g);                             // FUN_0011d5e0(param_1) at the end of FUN_0011a9f0
        g.clockFrames = 0;
        r.active = true;
    }

    // orig FUN_0011bec0
    void update() override {
        GameRuntime& r = rt();
        engine().tick();
        // FUN_00108d20 (3-D pass of the tick, after the actor clock step): per-actor combiner update so the next
        // tick's ACT logic reads current local times. UNVERIFIED: FUN_00108d20's visibility condition not translated.
        for (Chr* c : characters().list()) c->model.anim.update();
        // render passes of this displayed frame (PORT split, see game/post/screen_effects.hpp): FUN_001113d0 head
        // (3-D pass step 2), the ramps of FUN_0010ede0 / FUN_0010e0c0 and the ramp + grain scroll of FUN_0010fb90
        post::shadowPass().tick();
        post::screenEffects().tick();
        effectTickPass(r);                            // FUN_001120b0 (EXI update + draw of the tick)
        chrActEffectsFrame();                         // FUN_0012a4e0 (FUN_00101bd0 order: after the 3-D / EXI pass)
        actTrailsTick();                              // FUN_001aa530 (weapon trails, after FUN_001120b0; act_trail.cpp)
        actPropsDevTick();                            // PORT: --equip-at / --trail-log (act_props.cpp)
        // PORT (AUDIT_WORLD W10): not in FUN_0011bec0. Duplicate of the cell lookup the character tick already does
        // (FUN_00124c70 in FUN_00129c60); kept so the cell is current after a walk-skipping tick (pause / event mode).
        if (Chr* p = characters().player(); p != nullptr && r.dungeon) p->cell = r.dungeon->cellAtWorld(p->xf.m.pos.data(), &p->cellX, &p->cellY);
        pickBgm(r);
        startBgm(r.obj);
        GameObj& g = r.obj;
        const bool floorUp = r.dungeon && (r.dungeon->floorFlags() & 3u) != 0;
        if (floorUp && fade().frames == 0 && stageWarpPause() != 1) {   // iRam0053c6b0 != 1 (warp pause)
            if (0x1d < ++g.clockFrames) {
                g.clockFrames = 0;
                if (0x3b < ++g.sec) {
                    g.sec = 0;
                    if (0x3b < ++g.min) {
                        g.min = 0;
                        if (99 < ++g.hour) {
                            g.hour = 99;
                            g.min = 0x3b;
                            g.sec = 0x3b;
                        }
                    }
                }
            }
        }
        renderTickPass(r);   // PORT: FUN_001075a0 posing + transition stepping of this tick (presentation only reads)
    }

    // orig FUN_0011bad0 (subset)
    void exit() override {
        GameRuntime& r = rt();
        present::presenter().reset();               // PORT: the snapshots reference characters removed below
        r.obj.bgm = -1;
        if (eventHooks().padRumbleStop) eventHooks().padRumbleStop();   // FUN_0013a940(0x50b620)
        talkManager().end();                        // FUN_001a6710(0x53c6e0)
        r.ctl.flags &= 0xcfffffffu;
        sound().stopAll();
        playerStateReset();                         // state-machine reset + FUN_00143bc0 of FUN_0011bad0
        characters().removeAllButPlayer();          // FUN_0012bfd0(0x50b130)
        floorTagFloorExit(characters().player() ? characters().player()->id : 0u);   // FUN_0012bfd0 table loop
        stageClearAll();                            // FUN_00179fe0
        emittersStop(r);                            // FUN_0011f6c0 emitter handles (stopped before the kill-all)
        effects().killAll();                        // FUN_00112610
        effectsClearRecording();
        actTrailsClear();                           // PORT: the trail pool of FUN_001aa530 (owners are gone)
        // FUN_0011f6c0(0x50a350) floor unload: uRam0050a610 = 0, FUN_00126300 map reset
        hudWorld().portFloorFlags = 0;
        hudWorld().clearedFloorFlags = 0;
        hudMapReset();
        gameWorld().dungeon = nullptr;
        const bool nextIsGame = scenes().isNext("Game....");
        const bool nextIsEnding = scenes().isNext("Ending..");
        if (!nextIsGame) {
            tasks().unlink(&gGameTask);
            tasks().unlink(&gCameraTask);
            characters().destroyAll();
            floorTagClearAll();                     // FUN_0012bed0 table loop (0x50a710)
            chrAdaptersReset();                     // port: item-side views of the destroyed characters
            playerControlHook = nullptr;
            r.ctl.camera = nullptr;                 // PORT: 0x50b624 readers (sound listener FUN_001150b0) see no camera
            r.cam.reset();
            post::screenEffects().destroy();         // FUN_0011bcb0: FUN_0010def0(0x4e7b80)
            post::postRenderer().destroy();
            for (int id = 0x23; id <= 0x2b; ++id) ui::textures().release(id);   // FUN_0011bcb0
        }
        // FUN_001d8180(0x53d910); next != Game: FUN_0011bcb0 -> FUN_0014eef0, FUN_0011be70 (next != Ending),
        // FUN_001d8280; FUN_0015c3f0(-1) / FUN_0015ce70 / FUN_0015d0a0 (0x5180f0)
        hudGameExit(nextIsGame, nextIsEnding);
        if (!nextIsGame && !nextIsEnding) {
            flags().clearAll();                     // FUN_0011be70: FUN_00178760(0x53c3b0)
            counters().clearAll();                  //               FUN_00179540(0x53c5c0)
        }
        setCollisionScene(nullptr);
        freeCameraWorldReset();   // PORT
        r.world.reset();
        post::shadowPass().clearReceivers();
        r.dungeon.reset();
        r.floorReady = false;
        r.active = false;
    }

private:
    static CameraHooks makeHooks() {
        CameraHooks h;
        h.raycast = [](const float from[3], const float delta[3], float radius, float* dist) {
            return raycast(from, delta, radius, dist);
        };
        h.rotatePlayer = [](float rad) {
            if (Chr* p = characters().player()) {
                rotateAboutUp(p->xf.m, rad);
                for (int k = 0; k < 3; ++k) rt().camWorld.playerForward[k] = p->xf.m.fwd[size_t(k)];
            }
        };
        h.requestScreenTransition = [] { eventManager().requestTransition(0.0f, 2); };   // FUN_00177cc0(0, 0x51b620, 2)
        h.setLightDirection = [](const float* d) {                                    // FUN_00108b70(0x4e78e0, uRam0050a2cc, d)
            LightSlot& l = renderLights().slot[renderLights().mainSlot];
            l.dir[0] = d[0];
            l.dir[1] = d[1];
            l.dir[2] = d[2];
        };
        h.markPlayerModelOnReset = [] {};
        return h;
    }
};

GameScene gGameScene;

}  // namespace

GameEntry& gameEntry() { return gEntry; }
GameProgress& gameProgress() { return rt().obj; }

// orig FUN_0011bfe0
void gameRequestFloor(int floor, int flag) {
    gEntry.requestedFloor = floor;
    rt().obj.requestedFloor = floor;                 // Game +0x10
    if (flag == 1) scenes().requestScene("Game....");
}

bool gameActive() { return gRt != nullptr && gRt->active; }

// orig FUN_001b8430 (work/decompiled/001b8430.c) -> FUN_001b8440(scene root 0x4e78fc, on): every igSwitch named
// "Toggle" (0x462eb8) gets _childMask bit 0 set / bit 1 cleared (on == 0) or bit 1 set / bit 0 cleared (on != 0).
// The port applies it to the floor's map models (the characters / objects carry no "Toggle" switches).
void gameWorldToggle(int on) {
    if (gRt != nullptr && gRt->world) gRt->world->setToggle(on != 0 ? 1 : 0);
}
// PORT: mouse look drives the free mouse camera (free_camera.hpp; dxDeg > 0 turns right, dyDeg > 0 looks down). The
// original camera (FUN_0013e340) is left to the pad.
void gameMouseLook(float dxDeg, float dyDeg) {
    if (gRt != nullptr && freeCameraTakesInput(*gRt)) freeCameraLook(dxDeg, dyDeg);   // nothing moves behind menus / pause
}

void registerGameScene() {
    scenes().replaceScene("Game....", &gGameScene);
    installGameHooks();                              // event tick in FUN_00101bd0 for every scene, Game-side hooks
}

// FUN_001075a0 subset: clear colour 0x4e7914 (black), projection (engine +0x8c fov horizontal at 4:3, near, far),
// view = inverse(camera +0x18), fog, floor world + characters, opaque then blended.
// FUN_00177cf0 (capture) + FUN_00178280 (quad) of the event manager's screen transition (RUNTIME.md 4 rows a / j):
// a requested transition grabs the picture into a 256x256 texture and fades it out over the requested frames,
// drawn over the 3-D pass in the 512x448 raster (quad -offset/2 .. 512+offset, 448+offset, blend (4,5)).
// PORT: the state steps in renderTickPass (once per tick); the presentation takes the picture at the end of the 3-D
// pass of the first frame after the capture tick (that frame still shows the previous camera: the camera tasks copy
// their matrices one tick later) and draws the alpha the tick recorded. That frame is presented at alpha 1 (the
// picture of tick N, as the original's capture inside the tick's own render pass); the opaque quad hides the step back
// to the interpolated frames that follow.
// PORT widescreen (PRESENTATION_PLAN.md V8): the picture is the presented area and the quad spans the extended raster
// (-delta - offset/2 .. 512 + delta + offset), so nothing is squashed into the 4:3 box.
Texture2D gTransitionTex{};
uint32_t gTransitionGrabbed = 0;   // PORT presentation state: last transCaptureSerial grabbed into gTransitionTex
void eventTransitionPass(const GameRuntime& r, int screenW, int screenH) {
    const ScreenLayout L = screenLayout(screenW, screenH);
    if (gTransitionGrabbed != r.transCaptureSerial) {
        gTransitionGrabbed = r.transCaptureSerial;
        unsigned char* px = rlReadScreenPixels(screenW, screenH);
        if (px != nullptr) {
            Image img{px, screenW, screenH, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            if (L.x != 0 || L.y != 0 || L.w != screenW || L.h != screenH)
                ImageCrop(&img, Rectangle{float(L.x), float(L.y), float(L.w), float(L.h)});
            ImageResize(&img, 256, 256);                   // the 256x256 capture texture (+0x1c0(256, 256, 5, ..))
            if (gTransitionTex.id != 0) UnloadTexture(gTransitionTex);
            gTransitionTex = LoadTextureFromImage(img);
            UnloadImage(img);
        }
    }
    const float offset = r.transDrawOffset;
    const float a = r.transDrawAlpha;
    if (a <= 0.0f || gTransitionTex.id == 0) return;
    const float sx = float(L.boxW) / 512.0f, sy = float(L.boxH) / 448.0f;
    const float x0 = -L.delta - offset / 2.0f + 0.75f, y0 = -offset / 2.0f + 0.5f;
    const float x1 = offset + 512.0f + L.delta + 0.75f, y1 = offset + 448.0f + 0.5f;
    const Rectangle dst{float(L.boxX) + x0 * sx, float(L.boxY) + y0 * sy, (x1 - x0) * sx, (y1 - y0) * sy};
    float al = a * 255.0f;
    if (al > 255.0f) al = 255.0f;
    DrawTexturePro(gTransitionTex, Rectangle{0, 0, 256, 256}, dst, Vector2{0, 0}, 0.0f,
                   Color{255, 255, 255, static_cast<unsigned char>(al)});
    rlDrawRenderBatchActive();
}

// PORT: see-through of what lies between the camera and the player (keyboard + mouse camera).
// Nine lines from the eye to the player (head / chest / knees, centre and half a body to either side) find the
// occluders: map meshes they cross (free camera occluder set, keyed instance << 32 | mesh) and characters near them.
// An occluder stays "in the way" for kHoldSeconds after its last hit (no flicker at edges); its strength eases in
// fast (kFadeInPerSec) and out slowly (kFadeOutPerSec). see_through = object: an occluding map mesh fades out by alpha,
// an occluding character dithers; silhouette: occluders are dropped only where they cover the player (mask). Every
// other character dithers when it comes near the eye. Returns the player (exempt), or nullptr when it is off.
namespace {
struct CutFade {
    float strength = 0.0f;
    float hold = 0.0f;   // seconds left "in the way"
};
std::unordered_map<uint64_t, CutFade> gCutFades;     // per occluding map mesh
std::unordered_map<uint64_t, float> gCutMeshes;      // the renderer's view of gCutFades (strengths)
std::unordered_map<const Chr*, CutFade> gCutChrs;    // per occluding character
CutFade gCutSilhouette;                              // silhouette mode
RenderTexture2D gCutMask{};                          // silhouette: the player drawn white (window sized)
constexpr float kHoldSeconds = 0.35f, kFadeInPerSec = 14.0f, kFadeOutPerSec = 5.0f;

// true while the fade is still visible
bool stepFade(CutFade& f, bool hit, float dt) {
    if (hit) f.hold = kHoldSeconds;
    else f.hold -= dt;
    const bool on = f.hold > 0.0f;
    const float rate = on ? kFadeInPerSec : kFadeOutPerSec;
    const float k = 1.0f - std::exp(-rate * (dt > 0.0f ? dt : 0.0f));
    f.strength += ((on ? 1.0f : 0.0f) - f.strength) * k;
    if (on && f.strength > 0.995f) f.strength = 1.0f;
    if (!on && f.strength < 0.01f) f.strength = 0.0f;
    return on || f.strength > 0.0f;
}
}  // namespace

const Chr* updateSeeThrough(GameRuntime& r, const alchemy::Mat4& camM, const alchemy::Mat4& view, const alchemy::Mat4& proj,
                            bool gameplay,
                            const Vec3f& plPos, const std::vector<present::DrawChr>& draws, int screenW, int screenH) {
    constexpr float kChestY = 95.0f, kHeadY = 165.0f, kKneeY = 40.0f, kSide = 28.0f, kFeetMargin = 15.0f;
    constexpr float kBodyDepth = 25.0f, kChrBlockRadius = 45.0f;
    constexpr float kNearDrop = 30.0f;                       // world geometry nearer than this to the eye is dropped
    constexpr float kChrNearGone = 70.0f, kChrNearFull = 190.0f;   // characters: fully dithered .. untouched
    constexpr float kChrOccluderDensity = 0.75f;            // dither density of a character in the way
    const Chr* player = characters().player();
    const float dt = GetFrameTime();
    const bool on = gameplay && player != nullptr && freeCamera().active;
    std::vector<uint64_t> keys;
    std::vector<const Chr*> chrs;
    float pv[3] = {0.0f, 0.0f, 0.0f};
    if (on) {
        const float eye[3] = {camM.m[12], camM.m[13], camM.m[14]};
        // camera right (row 0), flattened: the side offsets of the lines
        float rx = camM.m[0], rz = camM.m[2];
        const float rl = std::sqrt(rx * rx + rz * rz);
        if (rl > 1e-4f) { rx /= rl; rz /= rl; } else { rx = 1.0f; rz = 0.0f; }
        const float ys[3] = {kHeadY, kChestY, kKneeY};
        const float sides[3] = {0.0f, -kSide, kSide};
        for (float y : ys)
            for (float sd : sides) {
                const float target[3] = {plPos[0] + rx * sd, plPos[1] + y, plPos[2] + rz * sd};
                freeCameraSegmentMeshes(r.world.get(), eye, target, keys);
                const float seg[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
                const float len2 = seg[0] * seg[0] + seg[1] * seg[1] + seg[2] * seg[2];
                if (len2 <= 0.0f) continue;
                for (const present::DrawChr& d : draws) {
                    if (d.c == player || std::find(chrs.begin(), chrs.end(), d.c) != chrs.end()) continue;
                    const auto& cp = d.c->xf.m.pos;
                    for (float cy : ys) {
                        const float q[3] = {cp[0] - eye[0], cp[1] + cy - eye[1], cp[2] - eye[2]};
                        const float t = (q[0] * seg[0] + q[1] * seg[1] + q[2] * seg[2]) / len2;
                        if (t <= 0.0f || t >= 0.92f) continue;
                        const float e[3] = {q[0] - seg[0] * t, q[1] - seg[1] * t, q[2] - seg[2] * t};
                        if (e[0] * e[0] + e[1] * e[1] + e[2] * e[2] < kChrBlockRadius * kChrBlockRadius) { chrs.push_back(d.c); break; }
                    }
                }
            }
        const float chest[3] = {plPos[0], plPos[1] + kChestY, plPos[2]};
        alchemy::transformPoint(view, chest, pv);
    }
    if (!on) {
        gCutFades.clear();
        gCutMeshes.clear();
        gCutChrs.clear();
        gCutSilhouette = CutFade{};
        r.renderer.setCut({});
        r.renderer.setCutMeshes(nullptr);
        return nullptr;
    }
    for (uint64_t key : keys) gCutFades.try_emplace(key);
    for (const Chr* c : chrs) gCutChrs.try_emplace(c);
    gCutMeshes.clear();
    for (auto it = gCutFades.begin(); it != gCutFades.end();) {
        const bool hit = std::find(keys.begin(), keys.end(), it->first) != keys.end();
        if (!stepFade(it->second, hit, dt)) { it = gCutFades.erase(it); continue; }
        gCutMeshes[it->first] = it->second.strength;
        ++it;
    }
    for (auto it = gCutChrs.begin(); it != gCutChrs.end();) {
        const bool hit = std::find(chrs.begin(), chrs.end(), it->first) != chrs.end();
        it = stepFade(it->second, hit, dt) ? std::next(it) : gCutChrs.erase(it);
    }
    stepFade(gCutSilhouette, !keys.empty() || !chrs.empty(), dt);
    alchemy::SceneRenderer::CutParams c;
    c.enabled = true;
    c.silhouette = present::portSettings().seeThroughSilhouette;
    c.silhouetteStrength = gCutSilhouette.strength;
    c.depth = -pv[2] - kBodyDepth;
    c.floorY = plPos[1] + kFeetMargin;
    c.dotPx = std::max(1.0f, std::floor(float(screenH) / 540.0f + 0.5f));
    c.nearFade[0] = kNearDrop;
    c.nearFade[1] = kNearDrop;
    c.chrNearFade[0] = kChrNearGone;
    c.chrNearFade[1] = kChrNearFull;
    if (c.silhouette && gCutSilhouette.strength > 0.0f) {
        // the player mask: the player alone, drawn white into a window-sized target (own depth buffer)
        const unsigned prev = rlGetActiveFramebuffer();   // before the load: it leaves framebuffer 0 bound
        if (gCutMask.id == 0 || gCutMask.texture.width != screenW || gCutMask.texture.height != screenH) {
            if (gCutMask.id != 0) UnloadRenderTexture(gCutMask);
            gCutMask = LoadRenderTexture(screenW, screenH);
        }
        rlDrawRenderBatchActive();
        rlEnableFramebuffer(gCutMask.id);
        rlViewport(0, 0, screenW, screenH);
        rlColorMask(true, true, true, true);
        rlClearColor(0, 0, 0, 0);
        rlClearScreenBuffers();
        r.renderer.begin(view, proj);   // rebinds the program (rlDrawRenderBatchActive unbinds it)
        r.renderer.setCut({});
        r.renderer.setMaskMode(true);
        for (const present::DrawChr& d : draws)
            if (d.c == player)
                for (int pass = 0; pass < 2; ++pass) chrDrawAt(*d.c, d.state, r.renderer, pass);
        r.renderer.setMaskMode(false);
        rlDrawRenderBatchActive();
        rlEnableFramebuffer(prev);
        rlViewport(0, 0, screenW, screenH);
        c.maskTex = gCutMask.texture.id;
        c.maskSize[0] = float(screenW);
        c.maskSize[1] = float(screenH);
    }
    r.renderer.setCut(c);
    r.renderer.setCutMeshes(&gCutMeshes);
    return player;
}

float seeThroughChr(const Chr* c) {
    const auto it = gCutChrs.find(c);
    return it != gCutChrs.end() ? it->second.strength * 0.75f : 0.0f;   // kChrOccluderDensity
}

bool gameRender3D(int screenW, int screenH, float alpha) {
    if (gRt == nullptr || !gRt->active || !gRt->floorReady || !gRt->cam) return false;
    GameRuntime& r = *gRt;
    if (!r.rendererReady) r.rendererReady = r.renderer.init();
    // PORT (PRESENTATION_PLAN.md 2.3): everything below is drawn from the presenter's snapshots of the last two ticks
    // (A = N-1, B = N) at alpha; alpha 1 is tick N exactly. Nothing here writes logic state.
    present::Presenter& P = present::presenter();
    if (!P.ready()) return false;                    // no tick ran since the scene / floor started
    if (!present::portSettings().interpolation) alpha = 1.0f;
    if (gTransitionGrabbed != r.transCaptureSerial) alpha = 1.0f;   // the transition picture is tick N (see above)
    alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    alchemy::Mat4 camM = alchemy::Mat4::identity();
    // active camera *0x4e7b18 of the snapshots: event camera task (FUN_0010da00), talk camera, else the gameplay one
    float cw[4][4];
    int camSource = present::kCamGameplay;
    if (!P.camera(alpha, cw, &camSource)) std::memcpy(cw, r.cam->camWorld, sizeof cw);
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 3; ++k) camM.m[size_t(row * 4 + k)] = cw[row][k];
    const bool evCam = camSource != present::kCamGameplay;
    // PORT: keyboard / mouse play draws the free mouse camera (free_camera.hpp) instead of the original gameplay camera:
    // orbit around the presented player at the original preset height / boom, mouse yaw / pitch applied every
    // presented frame, boom swept against the collision scene every frame. Event / talk cameras keep priority.
    Vec3f plPos{};
    const bool havePlayer = P.playerPos(alpha, &plPos);
    if (havePlayer && !evCam && freeCamera().active) {
        const float pp[3] = {plPos[0], plPos[1], plPos[2]};
        float fw[4][4];
        freeCameraMatrix(r.world.get(), pp, r.cam->tun.height, r.cam->tun.dist, r.cam->tun.defaultPitch, GetFrameTime(), fw);
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 3; ++k) camM.m[size_t(row * 4 + k)] = fw[row][k];
    }
    alchemy::Mat4 view;
    if (!alchemy::invert(camM, view)) return false;
    // Hor+ over the whole window (the max-aspect pillars are drawn over it afterwards: the visible part is the capped
    // Hor+ view, the projection being linear in x)
    const float aspect = float(screenW) / float(screenH);
    const alchemy::Mat4 proj = present::presentProjection(r.proj.fovDeg, r.proj.nearZ, r.proj.farZ, aspect);
    // light follows the presented gameplay camera (FUN_0013e340 tail sets -forward of the tick's camera)
    const present::CamSnap* camB = P.latestCamera();
    if (camB != nullptr && camB->lightFollowsCamera && !evCam) {
        const float l[3] = {-camM.m[8], -camM.m[9], -camM.m[10]};
        modelSetPresentLightDir(l);
    }
    // igTransform inputs at the presented time between the two ticks
    gPresentSceneClock = P.sceneClock(alpha);
    alchemy::setSceneTimeSource(&presentSceneClock);
    // characters, objects and class extras at alpha; the skins are posed once for both passes below
    const std::vector<present::DrawChr>& draws = P.prepare(alpha);
    const unsigned display = rlGetActiveFramebuffer();   // host display target (window or the --shot texture)
    {   // FUN_001075a0 step 2: FUN_001113d0(renderer +0xd4) shadow pre-pass. Eye height +0x1c = player y + 500
        // (FUN_00142fe0 in the physics tick). Casters: the characters (FUN_001099e0(.., 1, ..) registers their models
        // through FUN_00109f10). UNVERIFIED / needs an alchemy hook: the original swaps each character to its "sh"
        // skin for this pass; SceneModel does not build that skin, the visible model is drawn instead.
        static float eyeY = 500.0f;
        if (havePlayer) eyeY = plPos[1] + 500.0f;
        post::shadowPass().renderCasters(r.renderer, camM, eyeY, [&draws](alchemy::SceneRenderer& sr, int pass) {
            for (const present::DrawChr& d : draws)
                if (d.c->type <= kChrEnemy) chrDrawAt(*d.c, d.state, sr, pass);
        }, display, screenW, screenH);
    }
    rlDrawRenderBatchActive();
    rlViewport(0, 0, screenW, screenH);
    rlEnableDepthTest();
    // host clear (+0x29c(0x4e7914)) and fog attr colour := renderer +0x34..+0x40 (FUN_001075a0)
    auto c8 = [](float v) { return static_cast<unsigned char>(v * 255.0f + 0.5f); };
    rlClearColor(c8(r.clearColor[0]), c8(r.clearColor[1]), c8(r.clearColor[2]), c8(r.clearColor[3]));
    rlClearScreenBuffers();
    r.renderer.setFog(true, r.clearColor, r.fogNear, r.fogFar);
    r.renderer.begin(view, proj);
    const Chr* cutPlayer = updateSeeThrough(r, camM, view, proj, havePlayer && !evCam, plPos, draws, screenW, screenH);
    r.renderer.begin(view, proj);   // PORT: rebind after the silhouette mask pass
    for (int pass = 0; pass < 2; ++pass) {
        if (r.world && !r.worldHidden) r.world->draw(r.renderer, pass);   // FUN_0011ed90 hides the floor root
        for (const present::DrawChr& d : draws) {
            r.renderer.setCutAllowed(d.c != cutPlayer);   // PORT: the player is never dithered
            r.renderer.setCutUniform(seeThroughChr(d.c));
            chrDrawAt(*d.c, d.state, r.renderer, pass);
        }
    }
    r.renderer.setCutAllowed(true);
    r.renderer.setCutUniform(0.0f);
    r.renderer.setCut({});   // PORT: off for every other pass (shadow casters, effects)
    r.renderer.setCutMeshes(nullptr);
    rlActiveTextureSlot(1);   // PORT: unbind the silhouette mask
    rlDisableTexture();
    rlActiveTextureSlot(0);
    r.renderer.end();
    rlDrawRenderBatchActive();
    // shadow consumer node (renderer +0xd4 +0x34, child 1 of the scene root after the scene group): receivers x 5.
    // UNVERIFIED: Alchemy's render-list order; drawn after the scene's blended geometry (the node blends).
    // FUN_0011ed90 (worldHidden) only clears the floor group (renderer +0x24) flags; the receiver group stays.
    post::shadowPass().drawReceivers(view, proj);
    // FUN_00101bd0 2-D passes in order (RUNTIME.md 4.2): b/c sprite layers (not in this path), e FUN_0010ede0
    // distance blur (uses the scene depth), f markers, g FUN_001120b0 EXI, h dialogs, i FUN_0010e0c0 bloom,
    // j FUN_00178280 transition, k FUN_0010fb90 film grain, then the UI layers (renderer2d in present()).
    post::postRenderer().drawDistanceBlur(screenW, screenH, proj, r.proj.fovDeg);
    rlEnableDepthTest();
    {   // EXI recording of this tick, drawn with the presented camera and projection (effectsReplay, V7)
        float w[4][4];
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 3; ++k) w[row][k] = camM.m[size_t(row * 4 + k)];
        effectsReplay(effectView(r, w, aspect), effectView(r, w).projection);   // mode-8 UVs: 4:3 frame copy band
        rlDrawRenderBatchActive();
        actTrailsReplay(effectView(r, w, aspect));   // FUN_001aa530 strips of this tick (after the EXI pass)
        rlDrawRenderBatchActive();
    }
    rlDisableDepthTest();
    post::postRenderer().drawBloom(screenW, screenH);
    eventTransitionPass(r, screenW, screenH);
    post::postRenderer().drawNoise(screenW, screenH);
    modelSetPresentLightDir(nullptr);                     // PORT: back to the logic values
    alchemy::setSceneTimeSource(&ActorAnim::clock);
    return true;
}

}  // namespace baroque::game
