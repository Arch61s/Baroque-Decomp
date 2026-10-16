#pragma once
// TLK conversation runtime: talk manager 0x53c6e0 (EVENTS_EFFECTS_PROGRESSION.md 1, work/reports/TALK_NATIVE.md).
//   task update FUN_001a1dc0 (engine tick, right after the event tick FUN_0016d3b0), reset FUN_001a1c30, selection
//   FUN_001a23a0 and its selectors (talk_select_rt.cpp), async / sync load FUN_001a7660 / FUN_001a73a0 / callback
//   FUN_001a7270, begin FUN_001a7740, page parser FUN_001a77c0, trigger expansion FUN_001a80c0 (data/tlk.cpp) and
//   firing FUN_001a8750, per tick FUN_001a2110, end FUN_001a6710, conversation camera FUN_001a2b40 / FUN_001a2cd0 /
//   FUN_001a1610 / FUN_001a2950 (talk_camera.cpp), move job FUN_001a2870, speaker name FUN_001a99b0, speaker index
//   FUN_001a98a0, player name FUN_001a9b20, actor by id FUN_001a9810, talk by voice number FUN_001aa1b0.
// Globals next to the manager: 0x53c6c8 talk state, 0x53c6b8 page data pointer, 0x53c6c0 loaded file.
// The world the manager reads (actor registry 0x50b130, scene / engine / menu state, sound manager 0x4e9790, EXI
// effects, renderer lights, item screen 0x517b60, database 0x53dc00) is reached through TalkWorld; the game binding is
// talk_wiring.cpp (Baroque-Native), the check binds its own (checks/talk_check_main.cpp).
// Field names carry the original byte offset inside the manager object.
#include "data/tlk.hpp"
#include "game/progress/talk_select.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace baroque::game {

using TalkActor = void*;   // actor registry entry (Chr* in the game binding)
using TalkObj = void*;     // actor or item object (+0xfac / +0xfbc / FUN_001395c0 results); binding-defined handle

// Hit record of FUN_0014e090(0x50c780, actor) (records at 0x517820: {+0 attacker, +4 attribute word, +8 flags})
struct TalkHitRecord {
    TalkActor attacker = nullptr;   // +0x0 (hurt registration +0x00)
    uint32_t m1 = 0;                // +0x4 (registration +0x0c: hit sub-type bits 1 / 2 / 4)
    uint32_t flags = 0;             // +0x8 (registration +0x10: 0x10000 player with a sword)
};

// Everything the manager reads or drives outside itself. Every method has the original call in its comment.
struct TalkWorld {
    virtual ~TalkWorld() = default;
    // scene / engine / menus
    virtual bool sceneIsGame() = 0;                  // uRam0053ea90 == "Game...." (0x462270)
    virtual bool itemScreenOpen() = 0;               // iRam00517b68
    virtual bool systemMenuOpen() = 0;               // FUN_00167d70(0x51b140)
    virtual bool enginePaused() = 0;                 // FUN_00102020(0x4e6640)
    virtual bool messageBoxOpen() = 0;               // iRam0053d734
    virtual uint32_t floorFlags() = 0;               // uRam0050a610
    virtual bool eventRunning() = 0;                 // iRam0051b620
    virtual int eventMode() = 0;                     // iRam0053c6a8
    virtual int floorId() = 0;                       // iRam0050a2a8 (Game +8)
    virtual int floorNumber() = 0;                   // FUN_0011c3b0(0x50a2a0)
    virtual uint32_t playerControlFlags() = 0;       // uRam0050b620
    virtual bool fadeRunning() = 0;                  // iRam004e7898 != 0
    virtual bool optionBit4() = 0;                   // FUN_0015dcb0: uRam0051af68 & 4
    virtual uint32_t padPressed() = 0;               // uRam004e6288
    virtual uint32_t talkButton() = 0;               // uRam004e65c0 (key-assign slot 2 "Conversation Camera")
    virtual float lingerGate() = 0;                  // fRam004e62d8
    // actor registry 0x50b130
    virtual int actorCount() = 0;                    // FUN_0012c1c0(0x50b130)
    virtual TalkActor actorAt(int i) = 0;            // FUN_0012c1d0(0x50b130)[i]
    virtual TalkActor player() = 0;                  // iRam0050b144
    virtual int actorType(TalkActor a) = 0;          // +0x18
    virtual uint32_t actorId(TalkActor a) = 0;       // +0x1c
    virtual void actorPos(TalkActor a, float out[3]) = 0;      // *(+0x20) +0xc..+0x14
    virtual void setActorPos(TalkActor a, const float p[3]) = 0;
    virtual void actorForward(TalkActor a, float out[3]) = 0;  // *(+0x20) +0x30..+0x38
    virtual int actorRoom(TalkActor a) = 0;          // *(+0x30) cell: (v & 0xf000) == 0x1000 ? v & 0xf : -1 (-2 = no cell)
    virtual int actorHp(TalkActor a) = 0;            // status block +4
    virtual int actorHpMax(TalkActor a) = 0;         // status block +0
    virtual int actorVt(TalkActor a) = 0;            // status block +0xc
    virtual int actorVtMax(TalkActor a) = 0;         // status block +8
    virtual TalkObj carriedItem(TalkActor a) = 0;    // FUN_001395c0(*(+0x2c)+8): status block +0x60
    virtual TalkObj objOfActor(TalkActor a) = 0;     // the registry entry as a +0xfbc object
    virtual TalkActorRef objRef(TalkObj o) = 0;      // fields FUN_001a3cc0 / FUN_001a6d00 read
    virtual void objOrFlags70(TalkObj o, uint32_t bits) = 0;   // object +0x70 |= bits
    // part 0 body (**(+0x2c)), 1 head (vt+0x28), 2 hair (vt+0x2c): FUN_0010a8e0(blend, speed, actor, anim found by
    // name in the part's animation list (FUN_0023f3fc strcmp), 0). Returns false when the part does not exist.
    virtual bool partAnim(TalkActor a, int part, const char* name, float blend, float speed) = 0;
    virtual void actAction(TalkActor a, const char* name8) = 0;   // FUN_00143a40(*(+0x2c)+0x10, name)
    virtual int hitRecords(TalkActor a, TalkHitRecord out[32]) = 0;   // FUN_0014e090(0x50c780, a): count (0 = none)
    // map (floor context 0x50a350)
    virtual int roomMapId(int room) = 0;             // **(0x50a640[room] + 4)
    virtual int floorRoomByte() = 0;                 // *pbRam0050a620
    virtual float roomOrigin(int room, int axis) = 0;    // FUN_0011e6c0 (axis 0) / FUN_0011e740 (axis 2)
    // item screen 0x517b60 / database 0x53dc00
    virtual uint32_t giftTarget() = 0;               // FUN_0015c2c0: +0x534
    virtual TalkObj giftItem() = 0;                  // FUN_0015c2d0: +0x538
    virtual void giftClear() = 0;                    // FUN_0015c2e0: +0x534 = -1
    virtual int databaseCount() = 0;                 // iRam0053dcc8 (database 0x53dc00 +200)
    virtual int listFlagCount() = 0;                 // FUN_001f8c60 (flags of list 0x49b950 that are set)
    // sound manager 0x4e9790
    virtual void voice3d(int id) = 0;                // FUN_00115970(1.0, snd, id, 0)
    virtual void voice(int id) = 0;                  // FUN_00115a60(1.0, snd, id)
    virtual void bgmVolume(float v, int frames) = 0; // FUN_00115f00(v, snd, frames)
    virtual int voiceStatus() = 0;                   // FUN_00115cd0
    virtual void voiceStop() = 0;                    // FUN_00115c10
    virtual void voiceVolume(float v) = 0;           // FUN_00115af0
    virtual void se(int id) = 0;                     // FUN_00115390(1.0, snd, id)
    // effects / renderer / cameras
    virtual void effect(int id, const float pos[4]) = 0;   // FUN_00112190(0, id, pos, rot 0, scale (100,100,100,1))
    virtual void mainLightDir(float out[3]) = 0;     // FUN_00108c90(0x4e78e0, uRam0050a2cc)
    virtual void setMainLightDir(const float d[3]) = 0;    // FUN_00108b70(0x4e78e0, uRam0050a2cc, d)
    virtual bool activeCameraExists() = 0;           // iRam004e7b18 != 0
    virtual void transition() = 0;                   // FUN_00177cc0(0, 0x51b620, 3)
    // asynchronous file load FUN_0010cf60 (the callback FUN_001a7270 is run by the manager when it returns true)
    virtual bool loadFile(const char* dataPath, std::vector<uint8_t>& out) = 0;
};
void setTalkWorld(TalkWorld* w);
TalkWorld* talkWorld();

// Conversation camera task (FUN_001aa280 -> pool FUN_00132b20(0) -> FUN_001a1610, vtable 0x4cde20). Fixed matrix,
// published by FUN_0010da30 while it is the active camera (*0x4e7b18).
struct TalkCamera {
    float world[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};   // +0x18 rows right/up/fwd/pos
    bool pushed = false;        // linked as the active camera (FUN_0010da00 .. FUN_0010d9c0)
};

struct TalkManager {
    // 0x53c6e0 ..
    int32_t cooldown = 0;           // +0x00 (15 after a reset)
    int32_t enabled = 0;            // +0x04
    int32_t active = 0;             // +0x08
    int32_t facing = 0;             // +0x0c player faces the speaker
    int32_t playerInitiated = 0;    // +0x10
    int32_t talkIndex = -1;         // +0x14
    uint32_t speaker = 0;           // +0x18 actor id
    uint32_t prevSpeaker = 0;       // +0x1c
    uint32_t candidate = 0;         // +0x20
    uint32_t prevCandidate = 0;     // +0x24
    uint32_t itemTalkActor = 0;     // +0x28
    int32_t outOfRange = 0;         // +0x2c
    int32_t noVoice = 0;            // +0x30
    int32_t category = -1;          // +0x34
    float volume = 0.0f;            // +0x38
    int32_t volTimer = 0;           // +0x3c
    TalkCamera* camera = nullptr;   // +0x40
    float camPos[3] = {};           // +0x44 (FUN_001a1db0)
    float camRight[3] = {};         // +0x50 (FUN_001a1d80)
    float camUp[3] = {};            // +0x5c (FUN_001a1da0)
    float camFwd[3] = {};           // +0x68 (FUN_001a1d90)
    float savedLight[3] = {};       // +0x74
    int32_t pc = 0;                 // +0x80
    int32_t frame = -1;             // +0x84
    uint32_t startDelay = 0;        // +0x88
    char text[0x400] = {};          // +0x8c
    int32_t speed = 1;              // +0x48c
    int32_t layout = 0;             // +0x490
    // +0x494 .. +0x6ec: 600 bytes cleared together (FUN_001a6710 / FUN_001a1b70)
    int32_t voiceId = 0;            // +0x494
    int32_t sentence = 0;           // +0x498
    std::array<int32_t, 0x94> triggerBlock{};    // +0x49c (0x250 bytes)
    std::array<data::TlkTrigger, 33> triggers{}; // +0x6ec (32 x 0x34; the 33rd entry FUN_001a80c0 may write is kept
                                                 // apart instead of overlapping +0xd6c, see talk_manager.cpp)
    int32_t moveFrames = 0;         // +0xd6c
    float moveDelta[3] = {};        // +0xd70
    uint32_t moveActor = 0;         // +0xd7c
    int32_t floorTalkDone = 0;      // +0xd80
    int32_t idleTimer = 0x708;      // +0xd84
    int32_t mapId = -1;             // +0xd88
    int32_t roomByte = -1;          // +0xd8c
    int32_t stareTimer = -1;        // +0xd90
    uint32_t farewell = 0;          // +0xd98
    int32_t lastHp = 0;             // +0xd9c
    int32_t lastVt = 0;             // +0xda0
    std::array<int32_t, 0x80> mapList{};   // +0xda4 kind-3 talks with a positional zone (-1 = end)
    int32_t zone = -1;              // +0xfa4
    int32_t listCursor = 0;         // +0xfa8
    TalkObj specialItem = nullptr;  // +0xfac (FUN_001a6230)
    int32_t fb0 = 0;                // +0xfb0
    uint32_t lingerTicks = 0;       // +0xfb4
    uint32_t lingerNpc = 0;         // +0xfb8
    TalkObj giftObj = nullptr;      // +0xfbc
    uint32_t fc0 = 0;               // +0xfc0 (FUN_001a5330)
    int32_t dbStage = 1;            // +0xfc4
    int32_t tutorialCounter = 0;    // +0xfc8
    char scheduled[0x40] = {};      // +0xfcc
    int32_t scheduledCount = -1;    // +0x100c
    uint32_t excluded = 0;          // +0x1010
    int32_t lastDbCount = 0;        // +0x1014
    int32_t nameIndex = -1;         // +0x1018
    int32_t number = -1;            // +0x101c
    int32_t skippable = 1;          // +0x1020

    // globals beside the object
    int32_t state = 0;              // iRam0053c6c8: 0 idle, 1 load pending, 2 parsing, 3/4 (hold), 5 playing, 6 finished
    std::vector<uint8_t> file;      // iRam0053c6c0 (loaded file); 0x53c6b8 points into it
    size_t dataOfs = 0;             // iRam0053c6b8 - file data
    bool dataValid = false;         // iRam0053c6b8 != 0
    bool loadPending = false;       // PORT: FUN_0010cf60 read issued, callback FUN_001a7270 due at the next task update
    uint32_t giftGlow = 0;          // uRam00518090 (written only here; reader not located, UNVERIFIED)

    void construct();                             // FUN_001a1b70 (static ctor body)
    void reset();                                 // FUN_001a1c30
    void taskUpdate();                            // FUN_001a1dc0
    void freezeWindow();                          // FUN_001a2910
    int select();                                 // FUN_001a23a0 (talk_select_rt.cpp)
    void tick();                                  // FUN_001a2110
    void moveJob();                               // FUN_001a2870
    void end();                                   // FUN_001a6710
    void loadAsync(int idx, int sync);            // FUN_001a7660
    void loadCallback();                          // FUN_001a7270
    void releaseFile();                           // FUN_001a69a0
    void playByName(const char* name);            // FUN_001a73a0
    void begin();                                 // FUN_001a7740
    void parsePage();                             // FUN_001a77c0
    void expandTriggers();                        // FUN_001a80c0
    void fireTriggers(int when);                  // FUN_001a8750 (0 current frame, 1 natural end (-1), 2 cut (-2))
    void drawSpeakerName(int idx);                // FUN_001a99b0
    int speakerIndex(const char* speakerStr) const;   // FUN_001a98a0
    TalkActor actorById(uint32_t id) const;       // FUN_001a9810
    TalkActor actorOfTalk(const char* name) const;    // FUN_001a7540
    void setVolume(float v);                      // FUN_001a7200
    // conversation camera (talk_camera.cpp)
    bool cameraAllowed() const;                   // FUN_001a2950
    void cameraTick();                            // FUN_001a2b40
    void cameraRelease();                         // FUN_001a2cd0
    void cameraBuild(TalkCamera& cam);            // FUN_001a1610
    // selectors (talk_select_rt.cpp)
    int selHitItem();                             // FUN_001a3320
    int selApproach();                            // FUN_001a2d40
    int selMap(int mapId, int room);              // FUN_001a5940
    int selFloor();                               // FUN_001a5ae0
    int selPositionalList();                      // FUN_001a5c90
    int selPositional(int idx);                   // FUN_001a5d10
    int selHp();                                  // FUN_001a5370
    int selVt();                                  // FUN_001a5650
    int selLinger();                              // FUN_001a5ff0
    int selStare();                               // FUN_001a60c0
    int selFarewell();                            // FUN_001a6170
    int selSpecialItem();                         // FUN_001a6240
    int selTutorial();                            // FUN_001a6300
    int selIdle();                                // FUN_001a6550
    void mapListAdd(int idx, int mapId, int room);    // FUN_001a27a0
    TalkSelectState selectState() const;          // the manager fields FUN_001a69e0 / FUN_001a6d00 / FUN_001a3cc0 read
    int search(TalkActor actor, int kind, int a, int b, int anySpeaker);   // FUN_001a69e0(mgr, ...)
    int condition(int idx, int kind, int a, int b);                        // FUN_001a6d00(mgr, ...)
    TalkActor firstNpcOfTalk(int idx, int field) const;   // the registry loop of the selectors
};
TalkManager& talkManager();                       // 0x53c6e0

// Talk table row text (FUN_001a9780 = file name; speaker / voice number strings).
const char* talkFileName(int idx);                // FUN_001a9780
const char* talkPlayerName();                     // FUN_001a9b20
void talkByVoiceNumber(int voiceNo);              // FUN_001aa1b0

// Notifications other modules send to the manager (the FUN_001a9xxx tutorial flags under FUN_001a6420).
void talkNoteItemTaken(TalkObj item);             // FUN_001a9cc0(0x53c6e0, item): flags 0xe3d.. by item id
void talkNoteCoinToss(int heads);                 // FUN_001a9c60: 0xe4c (tails) / 0xe4b
void talkNoteEnemyHit();                          // FUN_001a9c20: 0xe50
void talkNoteSpecial();                           // FUN_001a9ba0: 0xe4d
void talkNoteVoiceCounter();                      // FUN_001a9be0: 0xe51
void talkNoteMap();                               // FUN_001a9fb0: 0xe4e
void talkNoteItemScreen();                        // FUN_001a9ff0: 0xe4f
void talkNoteHandOver(TalkObj item, int arg);     // FUN_001a6230: +0xfac / +0xfb0
void talkNoteDatabase(uint32_t v);                // FUN_001a5330: +0xfc4 = FUN_001dad50, +0xfc0 = v

// Conversation camera presentation (Game scene 3-D pass and the player controller's camera axes).
bool talkCameraActive();                          // the talk camera task is the active camera (*0x4e7b18)
bool talkCameraMatrix(float out[4][4]);           // its +0x18 rows as last published into the renderer view
void talkCameraPublish();                         // FUN_0010da30 of the talk camera task (once per task-list walk)
bool talkCameraAxes(float fwd[3], float right[3]);   // 0x53c6e8 && 0x53c720: FUN_001a1d90 / FUN_001a1d80

}  // namespace baroque::game
