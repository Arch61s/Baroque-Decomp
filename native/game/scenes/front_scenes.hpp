#pragma once
// Front-end scenes (work/reports/SCENES.md §1-4), translated from the pseudocode:
//   BootMC 0x50a248 (FUN_0011a240 init / FUN_0011a290 exit / FUN_0011a2f0 update)
//   Credit 0x50a130 (FUN_00117ab0 / FUN_00117b20 / FUN_00117b70, state switch FUN_00117e90)
//   Adv    0x50a1e8 (FUN_00119640 / 0x1196e0 / FUN_00119720)
//   Title  0x50a160 (FUN_001181c0 / FUN_001182a0 / FUN_001182d0 = FUN_00118580 logic + FUN_001189a0 draw)
// Scenes not translated yet are registered as NotTranslatedScene (port-only placeholder, see .cpp).
#include "game/scene_manager.hpp"

#include <cstdint>

namespace baroque::game {

namespace ui { struct Sprite; }

// Globals shared between front-end scenes.
struct FrontGlobals {
    int newGame = 0;        // DAT_00469700 (1 = start a new game in NameEntr)
    int loadSlot = 0;       // DAT_00469708 (-1 written by Title)
    int ea48 = 0;           // uRam0053ea48
    int advMovie = 0;       // iRam0050a1f0 (0 OPENING / 1 PLAYABLE, toggled by Adv exit)
    int bootState = 0;      // iRam0050a258 (BootMC state machine)
};
FrontGlobals& front();

class BootMcScene : public Scene {   // 0x50a248
public:
    void init() override;
    void exit() override;
    void update() override;
private:
    void logic();          // FUN_0011a330
    bool errorMessage();   // FUN_0011a5e0
    int mcMode = 0;        // +0x04
    int frames = 0;        // +0x08
};

class CreditScene : public Scene {   // 0x50a130
public:
    void init() override;
    void exit() override;
    void update() override;
private:
    void setState(int s);  // FUN_00117e90
    ui::Sprite* spriteA = nullptr;   // +0x04
    ui::Sprite* spriteB = nullptr;   // +0x08
    uint32_t alpha = 0;              // +0x0c
    int state = 0;                   // +0x10
    uint32_t frames = 0;             // +0x14
};

class AdvScene : public Scene {      // 0x50a1e8
public:
    void init() override;
    void exit() override;
    void update() override;
private:
    int frames = 0;        // +0x04
    bool playing = false;  // port: the blocking FUN_00213840 spread over host frames
};

class TitleScene : public Scene {    // 0x50a160
public:
    void init() override;
    void exit() override;
    void update() override;
private:
    void build();          // FUN_00118c60
    void destroyAll();     // FUN_001190e0
    void logic();          // FUN_00118580
    void input();          // FUN_001184d0
    void memoryCard();     // FUN_00118310
    void mcStart();        // FUN_00118470
    void mcStop();         // FUN_001184b0
    void enterStart();     // FUN_00118800
    void draw();           // FUN_001189a0
    uint32_t frames = 0;             // +0x04
    uint32_t idle = 0;               // +0x08
    ui::Sprite* tiles[4] = {};       // +0x0c..+0x18 (x-major: [0]=(0,0) [1]=(0,1) [2]=(1,0) [3]=(1,1))
    ui::Sprite* logo[5] = {};        // +0x1c..+0x2c
    ui::Sprite* copyJp = nullptr;    // +0x30
    ui::Sprite* copyEn[4] = {};      // +0x34..+0x40
    ui::Sprite* pressStart = nullptr;// +0x44
    int speed = 0;                   // +0x48
    int8_t state = 0;                // +0x4c
    int8_t nextState = 0;            // +0x4d
    int mcState = 0;                 // +0x50
    int lastPort = 0;                // +0x54
    int verdict = 0;                 // +0x58
    SceneName pending = ~0ull;       // +0x60 (-1 = none)
    int angle = 0;                   // +0x68
    int logoAlpha = 0;               // +0x6c
    int copyAlpha = 0;               // +0x70
    int bgAlpha = 0;                 // +0x74
};

// Ending 0x50a2e0 (SCENES.md 10): Init FUN_0011da80, Update FUN_0011dbc0, Exit FUN_0011dad0. EV15B.PSS when the
// true-ending route is open and the game is not cleared yet, then EV18.PSS, then the run end FUN_0011cd40 (stage
// script "PT99": continue / save / title menu; the command handler FUN_0017a510 leaves the scene).
class EndingScene : public Scene {
public:
    void init() override;
    void exit() override;
    void update() override;
    int frames = 0;                  // +0x4
    int armed = 0;                   // +0x8
    int step = 0;                    // +0xc
private:
    void afterMovie();
    bool playing_ = false;           // PORT: FUN_00213840 blocks; the port plays across frames
    int movie_ = -1;
};

// Port-only placeholder for a scene whose code is not translated yet: shows its name and returns to Title on
// START. Not original behaviour; replaced scene by scene.
class NotTranslatedScene : public Scene {
public:
    explicit NotTranslatedScene(const char* name) : name_(name) {}
    void init() override;
    void exit() override;
    void update() override;
private:
    const char* name_;
    int frames_ = 0;
};

void registerFrontScenes();   // FUN_00100100 (all ten scenes, original order)

}  // namespace baroque::game
