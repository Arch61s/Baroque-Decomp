#pragma once
// EVT event interpreter: event manager 0x51b620 (EVENTS_EFFECTS_PROGRESSION.md 2, work/reports/EVENTS_NATIVE.md).
//   trigger FUN_001d1d30, first-view check FUN_0016d300 (flag table 0x44d650), start FUN_0016c6d0 (+ actor set
//   table 0x44d2e0), reset FUN_0016bbf0, file parse FUN_0016be20, per tick FUN_0016d3b0 / FUN_0016dbf0, camera
//   FUN_0016dde0 / FUN_0016f250 / FUN_0016f070 / FUN_0016f1c0 / FUN_0016f430 / FUN_00177c50, cue dispatcher
//   FUN_00175e30 and its handlers (event_cues.cpp), per-tick jobs FUN_001769d0 / FUN_00177050 / FUN_00177360 /
//   FUN_00177210, skip FUN_0016d1a0 / FUN_0016d260, end FUN_0016cbf0, screen transition FUN_00177cc0 /
//   FUN_00177cf0 / FUN_00178280 / FUN_00178600, event camera task FUN_00178670 (vtable 0x4cdda0).
// Field names carry the original byte offset inside the 0x20e00-byte object. Services of modules that have no port
// yet (EXI effects, screen effects, renderer lights, sub-object animation, material fades, pad rumble) are reached
// through EventHooks (null = the call is skipped; each such site is marked UNVERIFIED in the .cpp).
#include <array>
#include <cstdint>
#include <functional>

namespace baroque::game {

struct Chr;

// Camera keyframe record (+0x858 + i*0x54, 300 records). The EVT parser's "actor" slots are these records.
struct EvtCamKey {
    float pos[3] = {};          // +0x00 (+0x858) eye (map offset added at load)
    int32_t mode = 0;           // +0x0c (+0x864) 0 linear, 1 accel/decel, 2 accel, 3 decel
    int32_t lookValid = 0;      // +0x10 (+0x868) 1 = look-at point given (op 8)
    float look[3] = {};         // +0x14 (+0x86c)
    int32_t orbit = 0;          // +0x20 (+0x878) 1 = -180, 2 = +180 degrees around the midpoint
    int32_t orbitPlane = 0;     // +0x24 (+0x87c) 0 = XZ (FUN_00206be0), else XY (FUN_00206c40)
    float rot[3] = {};          // +0x28 (+0x880) angles in degrees (ops 9/10/11)
    int32_t rotDir[3] = {};     // +0x34 (+0x88c) ops 12/13/14: 1 = turn the other way round
    float spline[4] = {};       // +0x40 (+0x898) op 16: control point xyz (+ map offset) and strength (percent)
    int32_t frame = 0;          // +0x50 (+0x8a8) start frame (the 'current id' of op 1)
};
static_assert(sizeof(EvtCamKey) == 0x54);

// Cue block (+0x6ad8 + i*0x104, 400 blocks)
struct EvtCueBlock {
    int32_t words[64] = {};     // +0x000
    int32_t frame = 0;          // +0x100 (+0x6bd8) frame the block runs on
};
static_assert(sizeof(EvtCueBlock) == 0x104);

// Actor control slot (+0x2011c + slot*0x58, 24 slots), driven by FUN_001769d0
struct EvtActorJob {
    Chr* actor = nullptr;       // +0x00
    int32_t moveFrames = 0;     // +0x04
    float vel[3] = {};          // +0x08
    float acc[3] = {};          // +0x14
    int32_t rotFrames[3] = {};  // +0x20 / +0x24 / +0x28  axis 0 (FUN_00128c00), 1 (FUN_00128ae0), 2 (FUN_001289c0)
    float rotSpeed[3] = {};     // +0x2c / +0x34 / +0x3c degrees per tick (the words between are zeroed, unused)
    int32_t fallFrames = 0;     // +0x44
    int32_t hasTarget = 0;      // +0x48
    float target[3] = {};       // +0x4c
};

// Effect slot (+0x209dc + slot*0x34, 16 slots), driven by FUN_00177050
struct EvtEffectSlot {
    int32_t handle = 0;         // +0x00 FUN_00112190 result (0 = none)
    float pos[4] = {};          // +0x04 (w = 1)
    float vel[4] = {};          // +0x14 (w = 1)
    float acc[3] = {};          // +0x24
    int32_t frames = 0;         // +0x30
};

namespace ui { struct Sprite; }

// Renderer light slots (0x4e78e0 +0x48 + i*0xc; FUN_00108a30 .. FUN_00108cf0). The port's renderer does not light
// (IGB geometry carries baked colours), so the slot values are kept here for the presentation side.
struct LightSlot {
    int used = 0;               // +0x50 (+i*0xc)
    int enabled = 0;            // node vtable +0x84 argument (FUN_00108b10)
    float pos[4] = {};          // FUN_00108b40 (FUN_00108c60 returns its storage)
    float dir[4] = {};          // FUN_00108b70 (FUN_00108c90)
    float color[4] = {};        // FUN_00108ba0 (FUN_00108cc0)
    float ambient[4] = {};      // FUN_00108bd0 (FUN_00108cf0)
    float specular[4] = {};     // FUN_00108c00
    float atten[3] = {};        // FUN_00108c30
};
struct RenderLights {
    LightSlot slot[5];
    int mainSlot = 0;           // uRam0050a2cc
    int alloc(int secondary);   // FUN_00108a30
    void release(int s);        // FUN_00108ae0
};
RenderLights& renderLights();

// Calls into modules without a port (or owned elsewhere). Every member may be left empty.
struct EventHooks {
    // EXI effect manager game wrappers 0x112190.. (game/effects/effects.hpp; bound by eventBindEffects)
    std::function<int(float tag, int effectId, const float pos[4], const float rot[4], const float scale[4])> effectSpawn;   // FUN_00112190
    std::function<bool(int handle)> effectAlive;                         // FUN_00112500
    std::function<void(int handle)> effectKill;                          // FUN_00112530
    std::function<void(int handle, const float pos[4])> effectMove;      // FUN_001122d0 (position)
    std::function<void(int handle, const float rot[4])> effectSetRot;    // FUN_00112370 (rotation)
    std::function<void(int handle, const float rot[4])> effectAddRot;    // FUN_001123b0 (add rotation)
    std::function<void(int handle, const float scale[4])> effectScale4;  // FUN_00112490 (scale)
    std::function<void(int handle, int count, int end, int restart)> effectLoopOut;   // FUN_001126e0
    std::function<void(int handle, int rate)> effectRate;                // FUN_001126a0
    std::function<void(int handle, bool on)> effectFlag98;               // FUN_00112630
    std::function<void(bool on)> effectsPause;                           // FUN_00112620
    std::function<void()> effectsKillFinished;                           // FUN_00112600
    // screen effects object 0x4e7b80 / shadow pass 0x4e79b4 (game/post: screen_effects, shadow_pass; filled in
    // installGameHooks)
    std::function<void(float a, float b, int frames)> bloom;             // FUN_0010e010
    std::function<void(int frames)> bloomOff;                            // FUN_0010e0a0
    std::function<void(float alpha, int frames)> noise;                  // FUN_0010fb00
    std::function<void(int frames)> noiseOff;                            // FUN_0010fb70
    std::function<void(float v, int frames)> screenEffect3;              // FUN_0010ed60
    std::function<void(int frames)> screenEffect3Off;                    // FUN_0010edc0
    std::function<void(bool on)> shadowPass;                             // FUN_00111db0 (on) / FUN_00111df0 (off)
    // character presentation the character module does not expose yet
    std::function<void(Chr& c, const char* anim, float blend, float speed)> subObjectAnim;  // vt+0x28 head + FUN_0010a8e0
    std::function<void(Chr& c, int mode, int frames, const float a[4], const float b[4])> materialFade;  // FUN_0010a0a0
    std::function<void(Chr& c)> materialFadeEnd;                         // FUN_0010a130
    std::function<void(Chr& c)> playerEventState;                        // the FUN_001d1d30 state request (LAB_001d1de0)
    // FUN_00137f40 (off) / FUN_00137d40 (on) per status. Left unbound: the status module translates neither function
    // (combat.cpp: inflict FUN_00138660 / expire FUN_00138970 / cure FUN_00138ac0 skip them), so turning a visual on
    // here after an event (status 5 anim scale 0.5, status 0x12 SE override) would never be turned off again.
    std::function<void(int index, bool on)> playerStatusVisuals;
    // pad 0x4e6280 actuators: FUN_0020c540 stop (+0x2c0 = 0xff), FUN_0020c5e0 byte +0x2c2 / frames +0x2cc, FUN_0020c570
    // byte +0x2c1 / frames +0x2c8; FUN_0020c650 sends +0x2c1.. through FUN_00230e28 (scePadSetActDirect) and counts the
    // frames down. PORT: left unbound - the PS2 only drives the DualShock motors, and raylib's desktop (GLFW) backend
    // implements SetGamepadVibration as a warning-only stub (an XInput path would be a new dependency).
    std::function<void(int motor, int power, int frames)> padRumble;
    std::function<void()> padRumbleStop;
    // sound manager 0x4e9790 entries the game::SoundManager interface lacks (bound to audio::SoundSystem by the host)
    std::function<void(float vol, int seId, const float pos[3])> se3d;   // FUN_00115470
    std::function<uint32_t(int seId)> seFind;                            // FUN_00115930
    std::function<void(uint32_t handle, int time)> seStop;               // FUN_00115820
    std::function<void(uint32_t handle)> seStopHandle;                   // FUN_00115910
    std::function<void(int v)> sound137;                                 // FUN_00115920
    std::function<void(int v)> sound138;                                 // FUN_00115e70
    std::function<void(int v)> voicePause;                               // FUN_00115c50
    std::function<void(float vol, int voiceId)> voicePlay;               // FUN_00115a60
    std::function<void(uint32_t handle, int time)> voiceStop;            // FUN_00115bb0
    // Game scene state (engine 0x4e6640 projection, fog attribute *0x4e790c, camera 0x50b624, map 0x50a350)
    std::function<float()> fov;                                          // iRam004e66cc
    std::function<float()> farZ;                                         // iRam004e66d4
    std::function<void(float farZ, float fov)> setProjection;            // FUN_00101b60
    std::function<void(float* start, float* end)> fog;                   // *(0x4e790c) +0x10 / +0x14
    std::function<void(float start, float end)> setFog;
    std::function<void()> gameCameraReset;                               // FUN_0013f0b0(uRam0050b624)
    std::function<bool(float world[4][4])> activeCameraWorld;            // *0x4e7b18 +0x18 rows (FUN_0016b920)
    std::function<void()> mapHide;                                       // FUN_00126fb0(0x50a350)
    std::function<int()> mapIdCount;                                     // rooms in 0x50a640 (FUN_0016be20 lookup)
    std::function<int(int room)> mapRoomId;                              // **(0x50a640[i] + 4)
    std::function<bool()> generatedFloor;                                // FUN_0011c0a0(0x50a2a0)
    std::function<int(int room, int axis)> mapRoomOrigin;                // room +0xc (axis 0) / +0xe (axis 2), 8-cell blocks
};
EventHooks& eventHooks();

// Event camera task (FUN_00178670 -> FUN_0016b920, vtable 0x4cdda0): copies the manager's basis/eye every task-list
// walk (FUN_0016ba60) and publishes the view when it is the active camera (FUN_0010da30).
struct EventCamera {
    float world[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};   // +0x18 rows right/up/fwd/pos
    float prev[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};    // PORT: last tick (interpolation)
};

struct EventManager {
    int32_t active = 0;                 // +0x00
    int32_t frame = 0;                  // +0x04
    EventCamera* camera = nullptr;      // +0x08 camera task
    float mapOfs[3] = {};               // +0x0c / +0x10 / +0x14 room origin of the event map
    int32_t mapIndex = -1;              // +0x18
    float basis[3][3] = {};             // +0x1c right, +0x28 up, +0x34 forward
    float eye[3] = {};                  // +0x40
    char text[0x800] = {};              // +0x4c ';'-separated lines
    int32_t count = 0;                  // +0x84c header count + 1
    int32_t lastKey = 0;                // +0x850
    int32_t lastCue = 0;                // +0x854
    std::array<EvtCamKey, 402> keys{};  // +0x858 (300 records; the spare zero records keep the original's reads past
                                        // the last record (index up to the cue count) defined, UNVERIFIED)
    int32_t keyIndex = 0;               // +0x6ac8
    float angles[3] = {};               // +0x6acc
    std::array<EvtCueBlock, 402> cues{};   // +0x6ad8 (400 blocks; spares as for keys)
    int32_t cueIndex = 0;               // +0x20118
    std::array<EvtActorJob, 24> jobs{};    // +0x2011c
    int32_t instanceFilter = -1;        // +0x2095c (ops 41 / 189)
    int16_t seStopTime = 2;             // +0x20960 (op 140)
    int32_t pc = 0;                     // +0x20964
    ui::Sprite* letterTop = nullptr;    // +0x20968
    ui::Sprite* letterBottom = nullptr; // +0x2096c
    int32_t letterTimer = -1;           // +0x20970
    int32_t letterState = 0;            // +0x20974 1 opening, 2 shown, 3 closing
    ui::Sprite* colorQuad = nullptr;    // +0x20978
    int32_t quadStep = 0;               // +0x2097c
    float camVel[3] = {};               // +0x20980
    float camAcc[3] = {};               // +0x2098c
    float lookCur[3] = {};              // +0x20998
    int32_t equipOverride = 0;          // +0x209a4 (op 42 / 43)
    float savedFog[2] = {};             // +0x209a8 / +0x209ac
    float savedFar = 0.0f;              // +0x209b0
    float savedFov = 0.0f;              // +0x209b4
    int32_t actorSet = 0;               // +0x209b8 (table 0x44d2e0 entry loaded)
    int32_t transFrames = -1;           // +0x209bc
    float transStep = 0.0f;             // +0x209c0
    float transAlpha = 0.0f;            // +0x209c4
    int32_t transCaptured = 0;          // +0x209c8 / +0x209cc capture texture + quad geometry exist (PORT: flag)
    int32_t transRequest = 0;           // +0x209d0
    int32_t transRequestFrames = 0;     // +0x209d4
    float transOffset = 0.0f;           // +0x209d8
    std::array<EvtEffectSlot, 16> effects{};   // +0x209dc
    int32_t light = -1;                 // +0x20d1c
    float lightDrift[3] = {};           // +0x20d20
    int32_t lightDriftFrames = 0;       // +0x20d2c
    float lightDir[3] = {};             // +0x20d30 (op 60; 0 = follow the look-at)
    float savedLightDir[3] = {};        // +0x20d3c
    float savedLightColor[4] = {};      // +0x20d48
    float savedLightAmbient[4] = {};    // +0x20d58
    int32_t keyAdvanced = 0;            // +0x20d68
    int32_t chained = -1;               // +0x20d6c (op 180)
    uint32_t ticks = 0;                 // +0x20d70
    int32_t skippable = 1;              // +0x20d74 (op 193)
    int32_t firstView = 1;              // +0x20d78
    int32_t skipCountdown = -1;         // +0x20d7c

    // port bookkeeping
    int32_t triggerId = -1;             // uRam0053d840 (FUN_001d1d30 argument)
    uint32_t keyEnd = 0;                // PORT: number of keyframe records written by the file (<= 300)
    uint32_t camCutSerial = 0;          // PORT: ++ at start and at the EVT camera hard cuts (presentation only)

    void reset();                                        // FUN_0016bbf0
    void start(const char* fileName);                    // FUN_0016c6d0
    bool load(const char* fileName);                     // FUN_0016be20
    void end();                                          // FUN_0016cbf0
    void tick(bool paused);                              // FUN_0016d3b0
    void step();                                         // FUN_0016dbf0
    void cameraInterp(float dur);                        // FUN_0016dde0
    void cameraAccel();                                  // FUN_0016f250
    void cameraSpline(float dur, int t);                 // FUN_0016f070
    void lookAtInit();                                   // FUN_0016f1c0
    void basisFromAngles(float x, float y, float z);     // FUN_0016f430
    float angleOf(float a, float b, float c, float d);   // FUN_00177c50
    bool runCues(bool force);                            // FUN_00175e30 (event_cues.cpp)
    void actorJobs();                                    // FUN_001769d0 (event_cues.cpp)
    void effectJobs();                                   // FUN_00177050 (event_cues.cpp)
    void lightJob();                                     // FUN_00177360 (event_cues.cpp)
    void colorQuadJob();                                 // FUN_00177210 (event_cues.cpp)
    void skip(bool force);                               // FUN_0016d1a0
    void skipTick();                                     // FUN_0016d260
    void requestTransition(float offset, int frames);    // FUN_00177cc0
    void transitionRelease();                            // FUN_00178600
};
EventManager& eventManager();                            // 0x51b620

void eventTrigger(int n);                                // FUN_001d1d30
bool eventFirstView(const char* fileName);               // FUN_0016d300
bool eventActive();                                      // *(int*)0x51b620 != 0
void eventEngineTick(bool paused);                       // FUN_0016d3b0(0x51b620, FUN_00102020()) from FUN_00101bd0

// Presentation (Game scene 3-D pass): the event camera when it is the active camera (*0x4e7b18), interpolated.
bool eventCameraMatrix(float out[4][4], float alpha);
bool eventCameraIsActive();                              // the event camera task is *0x4e7b18
// FUN_00177cf0 / FUN_00178280: screen-transition capture and quad. capturePending() is true for the presentation
// frame that has to grab the previous picture; drawTransition() returns the quad alpha (0 = nothing to draw) and
// advances the fade once per presented tick (the original runs it once per frame = once per tick).
bool eventTransitionCapturePending();
void eventTransitionCaptured();
float eventTransitionAlpha(float* offset);
void eventTransitionAdvance();

}  // namespace baroque::game
