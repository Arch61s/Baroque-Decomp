// Menu-driven front-end scenes, translated (see menu_scenes.hpp).
#include "game/scenes/menu_scenes.hpp"

#include "game/engine.hpp"
#include "game/fade.hpp"
#include "game/movie_service.hpp"
#include "game/flag_manager.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/save_manager.hpp"
#include "game/scenes/front_scenes.hpp"
#include "game/scenes/game_scene.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/message_box.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/texture_registry.hpp"

namespace baroque::game {

namespace {

EvMovieGlobals gEvMovie;

uint32_t pressed() { return pad().port[0].pressed; }   // uRam004e6288

// The 2x2 background of tex 0x3f..0x42 (BG02_00/02/01/03) shared by the three scenes: the inline texture load +
// sprite alloc of FUN_00169770 / FUN_00119910 / FUN_00201380. `bias` is -1 for Option/NameEntr ((x*256 - 1) + 128,
// z -1) and 0 for GameLoad (x*256 + 128, z 0).
void buildBackground(ui::Sprite* tiles[4], float bias, float z) {
    int tex = 0x3f;
    for (unsigned x = 0; x < 2; ++x)
        for (unsigned y = 0; y < 2; ++y, ++tex) {
            tiles[x * 2 + y] = nullptr;
            if (!ui::textures().isLoaded(tex)) ui::textures().load(tex);
            ui::Sprite* s = ui::sprites().alloc(tex, 0);
            tiles[x * 2 + y] = s;
            if (s == nullptr) continue;
            s->cx = (static_cast<float>(x) * 256.0f + bias) + 128.0f;
            s->cy = (static_cast<float>(y) * 256.0f + bias) + 128.0f;
            s->z = z;
            s->hw = 128.0f;
            s->hh = 128.0f;
            s->color = 0x407f7f7f;
            s->u0 = 0.0f;
            s->v0 = 0.0f;
            s->u1 = 1.0f;
            s->v1 = 1.0f;
            s->wide = ui::kWideFill | ui::kWideArtMenu;   // PORT widescreen: 16:9 art, else the side fill (render2d.cpp)
        }
}

// the matching destroy + release loop (FUN_00169a20 / FUN_00119d60 / FUN_00201610)
void freeBackground(ui::Sprite* tiles[4]) {
    int tex = 0x3f;
    for (unsigned x = 0; x < 2; ++x)
        for (unsigned y = 0; y < 2; ++y, ++tex) {
            ui::sprites().destroyRef(tiles[x * 2 + y]);
            if (ui::textures().isLoaded(tex)) ui::textures().release(tex);
        }
}

}  // namespace

EvMovieGlobals& evMovie() { return gEvMovie; }

// FUN_00101bd0 (the UI slice FUN_0015c380/FUN_00167d80/FUN_00168010 runs inside Engine::tick()).
void menuSceneTick() { engine().tick(); }

// =============================================================================================================
// Option
// =============================================================================================================
// orig FUN_00119910 (work/decompiled/00119910.c)
void OptionScene::init() {
    mcState_ = 1;
    saveManager().titleCheck();                // FUN_00117150
    ui::gradientCreate();                      // FUN_00163e20
    buildBackground(tiles_, -1.0f, -1.0f);
    fade().set(0x80000000u, 0, 0);
    fade().set(0, 0x14, 0);
    if (scenes().isPrevious("GameLoad")) ui::menu().open(-1);
    else if (scenes().isPrevious("EvMovie.")) ui::menu().open(gEvMovie.optionMode);   // FUN_002064e0
    pending_ = 0;
    sound().init();                            // FUN_00114fd0
}

// orig FUN_00119d60 (work/decompiled/00119d60.c)
void OptionScene::exit() {
    saveManager().portReset();                 // FUN_0020e7f0
    freeBackground(tiles_);
    sound().setPitch(0);                       // FUN_00115f70(snd, 0)
    ui::gradientFree();                        // FUN_00163fc0
}

// orig FUN_00119f40 (work/decompiled/00119f40.c)
void OptionScene::update() {
    if (!ui::menu().isOpen() && pending_ == 0) {
        fade().set(0, 0, 0);
        fade().set(0x80000000u, 0x15, 0);
        pending_ = (mcState_ == 5) ? sceneName("Title...") : sceneName("GameLoad");
    }
    if (pending_ != 0 && fade().finished()) {
        fade().set(0x80000000u, 0, 0);
        fade().set(0x80000000u, 1, 0);
        scenes().requestScene(pending_);
    }
    if (ui::menu().isOpen() && saveManager().busy == 0) {
        if (saveManager().result == 0) {
            if (mcState_ == 1) {
                mcState_ = 3;
                saveManager().portCheckStart(0);   // FUN_0020e6e0
            } else if (mcState_ == 3 && saveManager().portCheckResult() == 0) {
                mcState_ = 5;                  // card removed / changed
                ui::menu().close();
            }
        } else {
            mcState_ = 5;
            ui::menu().close();
        }
    }
    menuSceneTick();                           // FUN_00101bd0
}

// =============================================================================================================
// NameEntr
// =============================================================================================================
// orig FUN_00169770 (work/decompiled/00169770.c)
void NameEntrScene::buildTiles() { buildBackground(tiles_, -1.0f, -1.0f); }
// orig FUN_00169a20 (work/decompiled/00169a20.c)
void NameEntrScene::freeTiles() { freeBackground(tiles_); }

// orig FUN_001693b0 (work/decompiled/001693b0.c)
void NameEntrScene::init() {
    ui::gradientCreate();                      // FUN_00163e20
    buildTiles();
    fade().set(0x80000000u, 0, 0);
    fade().set(0, 0xf, 0);
    sound().init();                            // FUN_00114fd0
    ui::menu().open(8);                        // NAME ENTRY
    leaving_ = 0;
    frames_ = 0;
}

// orig 0x169430: thunk of FUN_00163fc0
void NameEntrScene::exit() { ui::gradientFree(); }

// orig FUN_00169440 (work/decompiled/00169440.c)
void NameEntrScene::update() {
    if ((pressed() & 0x20) != 0 && ui::nameEntry().canCancel != 0 && 0x1e < frames_ && fade().finished() && leaving_ == 0) {
        leaving_ = 1;
        fade().set(0x40000000u, 0, 0);
        fade().set(0x80000000u, 0x10, 0);
        ui::menu().close();                    // FUN_00167910
        sound().playSe(1.0f, 2);
        if (scenes().isPrevious("Game....")) sound().stopBgm(8);
    }
    if (scenes().isCurrent("NameEntr") && (!ui::menu().isOpen() || leaving_ != 0) && fade().finished()) {
        if (leaving_ == 0) {
            optionDefaults();                  // FUN_0015d990
            gameRequestFloor(0x5f, 0);         // FUN_0011bfe0(0x50a2a0, 0x5f, 0)
            scenes().requestScene("Game....");
        } else {
            scenes().requestScene(scenes().previous());
        }
        if (ui::messageBox().isOpen()) ui::messageBox().close();
        freeTiles();
    }
    menuSceneTick();                           // FUN_00101bd0
    ++frames_;
}

// =============================================================================================================
// GameLoad
// =============================================================================================================
// orig FUN_00201380 (work/decompiled/00201380.c)
void GameLoadScene::buildTiles() { buildBackground(tiles_, 0.0f, 0.0f); }
// orig FUN_00201610 (work/decompiled/00201610.c)
void GameLoadScene::freeTiles() { freeBackground(tiles_); }

// orig FUN_00200db0 (work/decompiled/00200db0.c)
void GameLoadScene::init() {
    ui::gradientCreate();                      // FUN_00163e20
    buildTiles();
    fade().set(0x7f000000u, 0, 0);
    fade().set(0, 0xf, 0);
    ui::menu().open(9);                        // FILE_SELECT
    leaving_ = 0;
    frames_ = 0;
    sound().init();                            // FUN_00114fd0
}

// orig 0x200e30: thunk of FUN_00163fc0
void GameLoadScene::exit() { ui::gradientFree(); }

// orig FUN_002012a0 (work/decompiled/002012a0.c): Circle on the file list -> back to the title
void GameLoadScene::cancelCheck() {
    if ((pressed() & 0x20) != 0 && !ui::messageBox().isOpen() && 0x1e < frames_ && fade().finished() && leaving_ == 0 &&
        ui::fileSelect().state == 2) {
        leaving_ = 1;
        fade().set(0x40000000u, 0, 0);
        fade().set(0x80000000u, 0x1f, 0);
        ui::menu().close();
        sound().playSe(1.0f, 2);
    }
}

// orig FUN_00200ec0 (work/decompiled/00200ec0.c)
void GameLoadScene::leaveCheck() {
    if (!scenes().isCurrent("GameLoad") || ui::menu().isOpen()) return;
    sound().stopBgm(8);                        // evaluated inside the condition, every frame the menu is closed
    if (!fade().finished()) return;
    if (leaving_ == 0) {
        const int st = ui::fileSelect().state;   // uRam0053ea28
        if (st == 0xb) scenes().requestScene("NameEntr");
        else if (st == 5) scenes().requestScene("Game....");
        else if (st == 8) scenes().requestScene("Option..");
        else if (static_cast<unsigned>(st - 0x10) < 2u) scenes().requestScene("Title...");
    } else {
        scenes().requestScene("Title...");
    }
    freeTiles();
}

// orig FUN_00200e40 (work/decompiled/00200e40.c)
void GameLoadScene::update() {
    cancelCheck();
    leaveCheck();
    if (frames_ == 0x1e) {
        sound().playBgm(0x18, 4, 1);           // FUN_00115d70(snd, 0x18, 4, 1)
        flags().set(0x3b6);
    }
    menuSceneTick();                           // FUN_00101bd0
    ++frames_;
}

// =============================================================================================================
// EvMovie
// =============================================================================================================
// orig FUN_00206260 (work/decompiled/00206260.c): FUN_00304e60(0) (no Alchemy scene in the front end), pitch reset.
// +4 is not cleared here: the scene object is static (.bss) and the original never resets it either. UNVERIFIED:
// the frame counter therefore only waits 15 frames on the first EvMovie of a run; kept as in the original.
void EvMovieScene::init() {
    sound().setPitch(0);                       // FUN_00115f70(snd, 0)
    playing_ = false;
}

// orig FUN_00206290 (work/decompiled/00206290.c): pitch reset, FUN_00304e60(0)
void EvMovieScene::exit() {
    sound().setPitch(0);
    movieEnd();                                // PORT: release a movie still open if the scene is left early
}

// orig FUN_002062c0: after the blocking play the scene requests "Title...", runs one engine tick
// (FUN_00101bd0) and requests "Option.." -- the last request wins, so the Option scene is entered.
void EvMovieScene::leave() {
    scenes().requestScene("Title...");
    engine().tick();
    scenes().requestScene("Option..");
}

// orig FUN_002062c0 (work/decompiled/002062c0.c). No engine tick while waiting (the original has none in this
// branch). Movie struct: +4 = DAT_004697f8, +8 = 1 (skippable: FUN_00213840 leaves the START callback FUN_00213990
// armed; 0 would call FUN_00216300 and clear it), +0xc = volume word of the BGM setting.
void EvMovieScene::update() {
    if (playing_) {
        if (movieUpdate()) return;
        movieEnd();                            // FUN_002137b0
        playing_ = false;
        leave();
        return;
    }
    if (frames_ < 0xf) {
        ++frames_;
        return;
    }
    if (movieBegin(gEvMovie.movieIndex, 1, movieVolumePercent(options().bgmVol)))   // FUN_00213730 + FUN_00213840
        playing_ = true;
    else
        leave();
}

}  // namespace baroque::game
