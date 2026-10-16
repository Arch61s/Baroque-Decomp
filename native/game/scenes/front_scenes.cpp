// Front-end scenes, translated (see front_scenes.hpp).
#include "game/scenes/front_scenes.hpp"
#include "game/scenes/menu_scenes.hpp"

#include "game/engine.hpp"
#include "game/event/stage_script.hpp"
#include "game/fade.hpp"
#include "game/hud/hud_api.hpp"
#include "game/progress/progression.hpp"
#include "game/scenes/game_scene.hpp"
#include "game/flag_manager.hpp"
#include "game/movie_service.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/save_manager.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include "platform/platform.hpp"

#include <bit>

namespace baroque::game {

namespace {

FrontGlobals gFront;

constexpr uint32_t kSinTable[1024] = {
#include "game/sin_table.inc"
};

// orig FUN_00206940: sine of an angle in degrees through the 1024-entry table 0x469830
float sinDeg(float deg) { return std::bit_cast<float>(kSinTable[size_t(int(deg * 2.8444445f) & 0x3ff)]); }

uint32_t pressed() { return pad().port[0].pressed; }   // uRam004e6288

// the float -> int conversion with the 2^31 wrap the compiler emitted (FUN_001189a0)
int ftoiWrap(float f) {
    if (2.1474836e+09f <= f) f = f - 2.1474836e+09f;
    return int(f);
}

}  // namespace

FrontGlobals& front() { return gFront; }

// =============================================================================================================
// BootMC
// =============================================================================================================
// orig FUN_0011a240
void BootMcScene::init() {
    ui::gradientCreate();                      // FUN_00163e20
    ui::textures().load(0x25);                 // TEX_UI02
    ui::textures().load(0x27);                 // TEX_UI04
    sound().init();                            // FUN_00114fd0
}

// orig FUN_0011a290
void BootMcScene::exit() {
    ui::textures().release(0x27);
    ui::textures().release(0x25);
    ui::gradientFree();                        // FUN_00163fc0
    optionApply();                             // FUN_0015da60
    // orig FUN_0011a5a0: +4 = 3; busy-wait until the card is idle; FUN_0020e7f0
    mcMode = 3;
    while (saveManager().busy != 0) saveManager().poll();
    saveManager().portReset();
}

// orig FUN_0011a2f0
void BootMcScene::update() {
    logic();
    engine().tick();
    ++frames;
}

// orig FUN_0011a5e0
bool BootMcScene::errorMessage() {
    const int r = saveManager().result;        // iRam00509834
    const int kb = int(saveManager().requiredKB);
    if (r == 3) ui::messageBox().open(4, 999999);
    else if (r == 6) ui::messageBox().open(1, kb);
    else if (r == 4) ui::messageBox().open(1, kb);
    else if (r == 1) ui::messageBox().open(2, kb);
    else return false;
    return true;
}

// orig FUN_0011a330 (state iRam0050a258)
void BootMcScene::logic() {
    int& st = front().bootState;
    ui::MessageBox& box = ui::messageBox();
    if (st == 0) {
        st = 1;
    } else if (st == 1) {
        saveManager().bootCheck();             // FUN_0011a560 -> FUN_00117140
        mcMode = 1;
        st = 2;
        frames = 0;
        // PORT: the PC save check finishes at once; the "Checking save data" window (and its open
        // sound) only appears when it is still busy, instead of flashing for one frame on every start
        saveManager().poll();
        if (saveManager().busy != 0) box.open(3, 999999);
    } else if (st == 2) {
        if (saveManager().busy == 0) {
            if (box.isOpen() && box.currentId() == 3) box.close();
            st = errorMessage() ? 4 : 3;
        }
    } else if (st == 3) {
        scenes().requestScene("Credit..");
    } else if (st == 4 && box.isOpen() && (box.currentId() == 4 || box.currentId() == 2 || box.currentId() == 1)) {
        if (box.choice() == 1) {
            box.close();
            st = 3;
        } else if (box.choice() == 2) {
            box.close();
            st = 1;
        }
    }
}

// =============================================================================================================
// Credit
// =============================================================================================================
// orig FUN_00117ab0
void CreditScene::init() {
    ui::textures().load(0x11b);                // BG05_00
    if (ui::language() == 0) ui::textures().load(0x11c);   // TEX_UI60 (EN)
    setState(0);
    alpha = 0;
}

// orig FUN_00117b20
void CreditScene::exit() {
    ui::textures().release(0x11b);
    if (ui::language() == 0) ui::textures().release(0x11c);
}

// orig FUN_00117e90
void CreditScene::setState(int s) {
    state = s;
    frames = 0;
    if (state == 1) {
        spriteA = ui::sprites().create(112.0f, 206.0f, 1.0f, 256.0f, 32.0f, 0.0f, 0.0f, 256.0f, 32.0f, 0x11b, 1);
        alpha = 0;
    } else if (state == 9) {
        ui::sprites().destroyRef(spriteB);
    } else if (state == 5) {
        ui::sprites().destroyRef(spriteA);
        spriteB = ui::sprites().create(128.0f, 160.0f, 1.0f, 256.0f, 128.0f, 0.0f, 0.0f, 256.0f, 128.0f, 0x11c, 1);
        alpha = 0;
    }
}

// orig FUN_00117b70
void CreditScene::update() {
    ++frames;
    switch (state) {
    case 0:
        setState(1);
        break;
    case 1:
        setState(2);
        break;
    case 2:
    case 6:
        if (alpha < 0x40) {
            alpha += 4;
            if (0x40 < alpha) alpha = 0x40;
        }
        if (0xf < frames) setState(state == 2 ? 3 : 7);
        break;
    case 3:
    case 7:
        // PORT: Cross (Space) skips a logo as well as START (Enter)
        if (0x5a < frames || (pressed() & (0x800u | platform::PAD_CROSS)) != 0) setState(state == 3 ? 4 : 8);
        break;
    case 4:
        if (alpha != 0) alpha -= 4;
        if (0xf < frames) {
            if (ui::language() == 1) setState(9);
            else if (ui::language() == 0) setState(5);
        }
        break;
    case 5:
        setState(6);
        break;
    case 8:
        if (alpha != 0) alpha -= 4;
        if (0xf < frames) setState(9);
        break;
    case 9:
        if (0xf < frames) scenes().requestScene("Adv.....");
        break;
    default:
        break;
    }
    if (spriteA != nullptr) spriteA->color = alpha * 0x1000000u + 0x7f7f7fu;
    if (spriteB != nullptr) spriteB->color = alpha * 0x1000000u + 0x7f7f7fu;
    // PORT: mouse menu layer -- a click anywhere on the picture is START (the original skips each logo on START)
    ui::mouse::item(this, 0, 0.0f, 0.0f, 512.0f, 448.0f, platform::PAD_START);
    engine().tick();
}

// =============================================================================================================
// Adv (attract movie)
// =============================================================================================================
// orig FUN_00119640
void AdvScene::init() {
    // FUN_00304e60(0): release the Alchemy scene (none loaded in the front end)
    sound().setPitch(0);                       // FUN_00115f70(snd, 0)
    if (front().advMovie == 1) {
        flags().set(0x3a0);
        flags().set(0x28f);
    } else if (front().advMovie == 0) {
        flags().set(0x39f);
        flags().set(0x2a6);
    }
    frames = 0;
    playing = false;
}

// orig 0x1196e0 (not exported by Ghidra; disassembled): toggle 0 <-> 1, then FUN_00304e60(0)
void AdvScene::exit() {
    if (front().advMovie == 1) front().advMovie = 0;
    else if (front().advMovie == 0) front().advMovie = 1;
    movieEnd();
}

// orig FUN_00119720. The blocking FUN_00213840 is spread over host frames: while the movie plays no engine tick
// runs (as in the original), then the remaining tick + scene request happen in the same Update.
void AdvScene::update() {
    if (playing) {
        if (movieUpdate()) return;
        movieEnd();                            // FUN_002137b0
        playing = false;
        engine().tick();
        scenes().requestScene("Title...");
        return;
    }
    engine().tick();
    if (frames < 0xf) {
        ++frames;
        return;
    }
    // uRam00540938 = 1 (no-skip = 0 path arms the skip check), index = iRam0050a1f0, volume = BGM option * 10
    // PORT: skippable 2 = START, a click or Cross (Space) -- the opening / attract movie only
    if (movieBegin(front().advMovie, 2, movieVolumePercent(options().bgmVol))) {
        playing = true;
    } else {
        engine().tick();
        scenes().requestScene("Title...");
    }
}

// =============================================================================================================
// Title
// =============================================================================================================
// orig FUN_00118470
void TitleScene::mcStart() {
    saveManager().titleCheck();                // FUN_00117150
    mcState = 1;
}

// orig FUN_001184b0
void TitleScene::mcStop() {
    mcState = 4;
    saveManager().portReset();                 // FUN_0020e7f0
}

// orig FUN_001181c0
void TitleScene::init() {
    mcStart();
    verdict = 0;
    // clear colour 0x4e7914 = (0,0,0,1) and host camera reset (+0x29c identity): no 3-D in the front end
    fade().clearHold();                        // uRam004e7890 &= ~2
    build();
    speed = 0;
    state = 0;
    nextState = 0;
    pending = ~0ull;
    lastPort = 0;
    frames = 0;
    idle = 0;
    angle = -360;                              // 0xfffffe98
    logoAlpha = 0;
    copyAlpha = 0;
    bgAlpha = 0;
    sound().init();                            // FUN_00114fd0
}

// orig FUN_00118c60
void TitleScene::build() {
    ui::textures().loadRange(0x1d, 0x20);      // BG00A_00/02/01/03
    ui::textures().load(0x21);                 // TEX_UI32
    if (ui::language() == 0) ui::textures().load(0x22);   // TEX_UI33
    int tex = 0x1d;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y, ++tex) {
            ui::Sprite* s = ui::sprites().alloc(tex, 1);
            tiles[x * 2 + y] = s;
            if (s == nullptr) continue;
            s->cx = (float(x) * 256.0f - 1.0f) + 128.0f;
            s->cy = (float(y) * 256.0f - 1.0f) + 128.0f;
            s->z = 0.0f;
            s->hw = 128.0f;
            s->hh = 128.0f;
            s->color = 0x7f7f7f;
            s->u0 = 0.0f;
            s->v0 = 0.0f;
            s->u1 = 1.0f;
            s->v1 = 1.0f;
            s->wide = ui::kWideArtTitle;   // PORT: 16:9 title art while the area is wider than 4:3
        }
    // tables 0x48c680 (x, y, w, h) and 0x48c6d0 (u0, v0, u1, v1)
    static const float kLogoRect[5][4] = {{26, 65, 172, 106}, {198, 65, 84, 128}, {282, 65, 29, 128}, {311, 65, 175, 113}, {486, 143, 13, 22}};
    static const float kLogoUv[5][4] = {{0, 0, 172, 106}, {172, 0, 256, 128}, {0, 128, 29, 256}, {29, 128, 204, 241}, {204, 128, 217, 150}};
    for (int i = 0; i < 5; ++i) {
        logo[i] = ui::sprites().create(kLogoRect[i][0], kLogoRect[i][1], 1.0f, kLogoRect[i][2], kLogoRect[i][3], kLogoUv[i][0],
                                       kLogoUv[i][1], kLogoUv[i][2], kLogoUv[i][3], 0x21, 1);
        if (logo[i] != nullptr) logo[i]->color = 0x407f7f7f;
    }
    if (ui::language() == 1) {
        // 0x48c720: (205, 386, 103, 16) uv (0, 106, 103, 122); flag 1 (disasm 0x118f38 a1 = 1, v1 122.0 0x48c73c on the stack)
        copyJp = ui::sprites().create(205.0f, 386.0f, 1.0f, 103.0f, 16.0f, 0.0f, 106.0f, 103.0f, 122.0f, 0x21, 1);
    } else if (ui::language() == 0) {
        static const float kEnRect[4][4] = {{54.5f, 386, 128, 16}, {182.5f, 386, 128, 16}, {291.5f, 386, 128, 16}, {380.5f, 386, 128, 16}};
        static const float kEnUv[4][4] = {{0, 48, 128, 64}, {0, 64, 128, 80}, {0, 80, 128, 96}, {0, 96, 128, 112}};
        for (int i = 0; i < 4; ++i)
            copyEn[i] = ui::sprites().create(kEnRect[i][0], kEnRect[i][1], 1.0f, kEnRect[i][2], kEnRect[i][3], kEnUv[i][0], kEnUv[i][1],
                                             kEnUv[i][2], kEnUv[i][3], 0x22, 1);
    }
    // 0x48c7c0: PRESS START (144, 312, 227, 15) uv (29, 241, 256, 256)
    pressStart = ui::sprites().create(144.0f, 312.0f, 1.0f, 227.0f, 15.0f, 29.0f, 241.0f, 256.0f, 256.0f, 0x21, 1);
}

// orig FUN_001190e0 (+ the texture releases at its end)
void TitleScene::destroyAll() {
    for (ui::Sprite*& s : tiles) ui::sprites().destroyRef(s);
    if (ui::language() == 1) ui::sprites().destroyRef(copyJp);
    else if (ui::language() == 0)
        for (ui::Sprite*& s : copyEn) ui::sprites().destroyRef(s);
    ui::sprites().destroyRef(pressStart);
    for (int id = 0x1d; id < 0x21; ++id) ui::textures().release(id);   // before the logo sprites, as in the original
    for (ui::Sprite*& s : logo) ui::sprites().destroyRef(s);
    ui::textures().release(0x21);
    if (ui::language() == 0) ui::textures().release(0x22);
}

// orig FUN_001182a0
void TitleScene::exit() {
    destroyAll();
    mcStop();
}

// orig FUN_001182d0
void TitleScene::update() {
    logic();
    draw();
    engine().tick();
}

// orig FUN_00118800: entering state 2
void TitleScene::enterStart() {
    fade().set(0, 0, 0);
    fade().set(0x80000000u, 0x1f, 0);
    if (verdict == 1 || unsigned(verdict - 3) < 2u) {
        front().newGame = 1;                   // DAT_00469700
        front().loadSlot = -1;                 // DAT_00469708
        pending = sceneName("NameEntr");
    } else if (verdict == 2) {
        pending = sceneName("GameLoad");
    }
    front().ea48 = 0;                          // uRam0053ea48
}

// orig FUN_00118580
void TitleScene::logic() {
    ++frames;
    ++idle;
    angle += speed;
    if (0x168 < angle) {
        angle = -360;
        if (nextState != state) {
            state = nextState;
            speed = 0xb;
            frames = 0;
            idle = 0;
            if (state == 2) enterStart();
        }
    }
    if (frames == 0x3c && state == 0) {
        speed = 0xb;
        state = 1;
        nextState = 1;
        sound().playBgm(0, 0, 0);              // FUN_00115d70(snd, 0, 0)
        flags().set(0x39e);
    }
    if (idle == 0x4ec && state == 1) {
        fade().set(0, 0, 0);
        fade().set(0x80000000u, 0x1f, 0);
        pending = sceneName("Adv.....");
        sound().stopBgm(8);                    // FUN_00115e10(snd, 8)
    }
    if (pending != ~0ull && fade().frames == 0) {
        fade().set(0, 0, 0);
        fade().set(0x80000000u, 1, 0);
        scenes().requestScene(pending);
        sound().stopBgm(8);
    }
    if (0x20 < frames && fade().frames == 0 && verdict != 0) input();
    memoryCard();
}

// orig FUN_001184d0
void TitleScene::input() {
    if (state != nextState) return;
    // PORT: Cross (Space by default) also works as START here; Enter is START already
    const uint32_t startBits = 0x800u | platform::PAD_CROSS;
    if (state == 0) {
        if ((pressed() & startBits) != 0) frames = 0x3b;
    } else if (state == 1 && (pressed() & startBits) != 0 && speed == 0xb) {
        nextState = 2;
        angle = -360;
        idle = 0;
        speed = 0x2c;
        sound().playSe(1.0f, 0);               // FUN_00115390(1.0, snd, 0)
    }
}

// orig FUN_00118310
void TitleScene::memoryCard() {
    SaveManager& mc = saveManager();
    if (mc.busy != 0) return;
    if (mcState == 1) {
        mc.portCheckStart(0);                  // FUN_0020e6e0(mc, 0)
        mcState = 3;
    }
    const int r = mc.portCheckResult();        // FUN_0020e830
    if (mcState == 3) {
        if (r == 1 && mc.result == 0) verdict = 2;
        else if (r == 1 && mc.result == 2) verdict = 1;
        else if (r == 1 && (mc.result == 3 || mc.result == 5)) verdict = 4;
        else if (r != -1) verdict = 3;
    }
    if (mcState == 3 && r == 1 && lastPort == 0) {
        mcStop();
        mcState = 5;
    } else if (mcState == 5) {
        mcStart();
    }
    lastPort = r;
}

// orig FUN_001189a0
void TitleScene::draw() {
    float s = sinDeg(float(angle));
    if (state == 1) {
        s = (s + 1.0f) * 32.0f;
        if (pressStart != nullptr) pressStart->color = uint32_t(ftoiWrap(s)) * 0x1000000u + 0x7f7f7fu;
    }
    if (logoAlpha < 0x40 && 0x14 < frames) {
        logoAlpha += 4;
        if (0x40 < logoAlpha) logoAlpha = 0x40;
    }
    if (copyAlpha < 0x40 && 0x3c < frames) {
        copyAlpha += 8;
        if (0x40 < copyAlpha) copyAlpha = 0x40;
    }
    for (ui::Sprite* l : logo)
        if (l != nullptr) l->color = uint32_t(logoAlpha) * 0x1000000u + 0x7f7f7fu;
    if (ui::language() == 1) {
        if (copyJp != nullptr) copyJp->color = uint32_t(copyAlpha) * 0x1000000u + 0x7f7f7fu;
    } else if (ui::language() == 0) {
        for (ui::Sprite* c : copyEn)
            if (c != nullptr) c->color = uint32_t(copyAlpha) * 0x1000000u + 0x7f7f7fu;
    }
    // background fades in while (next == 0 && state == 0) or state == 1
    const bool bgIn = (nextState == 0) ? (state == 0 || state == 1) : (state == 1);
    if (bgIn && bgAlpha < 0x40) {
        bgAlpha += 2;
        if (0x40 < bgAlpha) bgAlpha = 0x40;
    }
    for (ui::Sprite* t : tiles)
        if (t != nullptr) t->color = uint32_t(bgAlpha) * 0x1000000u + 0x7f7f7fu;
    // PORT: mouse menu layer -- a click anywhere on the title is START (logo intro and PRESS START)
    if ((state == 0 || state == 1) && nextState == state) ui::mouse::item(this, 0, 0.0f, 0.0f, 512.0f, 448.0f, platform::PAD_START);
}

// =============================================================================================================
// Ending
// =============================================================================================================
// orig FUN_0011da80 (work/decompiled/0011da80.c)
void EndingScene::init() {
    // FUN_00304e60(0): Alchemy scene-graph notification (no scene graph in the port's front end)
    sound().setPitch(0);                       // FUN_00115f70(0x4e9790, 0)
    armed = 1;
    frames = 0;
    step = 0;
    playing_ = false;
    movie_ = -1;
}

// orig FUN_0011dad0 (work/decompiled/0011dad0.c)
void EndingScene::exit() {
    movieEnd();                                // PORT: release a movie still open if the scene is left early
    ui::gradientFree();                        // FUN_00163fc0
    for (int id = 0x23; id < 0x2c; ++id) ui::textures().release(id);   // FUN_00105f00(0x4e6a68, id)
    if (!scenes().isNext("Game....")) {        // FUN_0011be70(0x50a2a0)
        hudItemsWipe();
        flags().clearAll();                    // FUN_00178760
        counters().clearAll();                 // FUN_00179540
    }
    stageClearAll();                           // FUN_00179fe0
    // FUN_00304e60(0): Alchemy scene notification (no Alchemy scene is loaded by the port's Ending scene)
}

// the part of FUN_0011dbc0 after the blocking FUN_00213730 / FUN_00213840 / FUN_002137b0 of each movie
void EndingScene::afterMovie() {
    if (movie_ == 0x1a) {
        flags().set(0x1e);
        flags().set(0x2a1);
        flags().set(0x3b8);
        frames = 0xf;
        step = 1;
    } else {
        ui::gradientCreate();                  // FUN_00163e20
        ui::textures().loadRange(0x23, 0x2b);  // FUN_00106120(0x4e6a68, 0x23, 0x2b)
        sound().init();                        // FUN_00114fd0(0x4e9790)
        returnToHub(gameProgress());           // FUN_0011cd40(0x50a2a0)
        armed = 0;
    }
    movie_ = -1;
}

// orig FUN_0011dbc0 (work/decompiled/0011dbc0.c)
void EndingScene::update() {
    if (playing_) {
        if (movieUpdate()) return;
        movieEnd();                            // FUN_002137b0(0x540930)
        playing_ = false;
        afterMovie();
        return;
    }
    stageTickAll();                            // FUN_0017a050
    engine().tick();                           // FUN_00101bd0(0x4e6640)
    if (frames < 0xf) {
        ++frames;
        return;
    }
    if (armed != 1) return;
    // uRam0054093c volume word: (bgm * 10 clamped 0..100) scaled as FUN_00119720 does; +8 = 1 (START skips)
    const int volume = movieVolumePercent(options().bgmVol);   // cRam0051af3c * 10
    if (step == 0) {
        if (flags().test(0x19) && !flags().test(0x1e)) {
            movie_ = 0x1a;                     // EV15B.PSS
            if (movieBegin(movie_, 1, volume)) {
                playing_ = true;
                return;
            }
            afterMovie();
            return;
        }
        frames = 0xf;
        step = 1;
        return;
    }
    if (step == 1 && (flags().test(0x19) || flags().test(0x1e))) {
        movie_ = 0x19;                         // EV18.PSS
        if (movieBegin(movie_, 1, volume)) {
            playing_ = true;
            return;
        }
    }
    movie_ = 0x19;
    afterMovie();
}

// =============================================================================================================
// Not translated yet (port-only placeholder)
// =============================================================================================================
void NotTranslatedScene::init() {
    frames_ = 0;
    platform::log(std::string("scene ") + name_ + ": not translated yet (placeholder)");
}
void NotTranslatedScene::exit() {}
void NotTranslatedScene::update() {
    ui::TextRenderer& t = ui::text();
    t.x = 40.0f;
    t.y = 200.0f;
    t.z = 10.0f;
    t.color = 0x807f7f7f;
    t.draw("%s - not translated yet", name_);
    t.y = 230.0f;
    t.draw("Press START to return to the title");
    engine().tick();
    if (++frames_ > 10 && (pressed() & 0x800) != 0) scenes().requestScene("Title...");
}

// orig FUN_00100100: registration order of the ten scenes
void registerFrontScenes() {
    static BootMcScene boot;
    static CreditScene credit;
    static TitleScene title;
    static AdvScene adv;
    static OptionScene option;
    static NameEntrScene nameEntr;
    static GameLoadScene gameLoad;
    static EvMovieScene evMovie;
    static NotTranslatedScene game("Game");
    static EndingScene ending;
    SceneManager& m = scenes();
    m.registerScene("BootMC..", &boot);
    m.registerScene("Credit..", &credit);
    m.registerScene("Title...", &title);
    m.registerScene("Adv.....", &adv);
    m.registerScene("Option..", &option);
    m.registerScene("EvMovie.", &evMovie);
    m.registerScene("NameEntr", &nameEntr);
    m.registerScene("GameLoad", &gameLoad);
    m.registerScene("Game....", &game);
    m.registerScene("Ending..", &ending);
}

}  // namespace baroque::game
