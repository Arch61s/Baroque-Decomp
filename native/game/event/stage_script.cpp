// Stage-script objects 0x53c690 (see stage_script.hpp).
#include "game/event/stage_script.hpp"

#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/character/model_fx.hpp"
#include "game/dungeon.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/event_services.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/char_access.hpp"
#include "game/items/item.hpp"
#include "game/items/item_links.hpp"
#include "game/player_move.hpp"
#include "game/sound_stub.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/pad.hpp"
#include "game/progress/save_record.hpp"
#include "game/save_manager.hpp"
#include "game/scene_manager.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/message_box.hpp"
#include "platform/platform.hpp"

#include <cstring>
#include <memory>
#include <vector>

namespace baroque::game {

namespace {

std::vector<std::unique_ptr<StageObject>> gObjects;   // 0x53c690[], count 0x53c6a0
StageHooks gHooks;
int gSaveFlow = 0;                                     // iRam0053ea40
int gWarpPause = 0;                                    // uRam0053c6b0

constexpr uint32_t tag(const char (&s)[5]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}
constexpr uint32_t kPT00 = tag("PT00");   // 0x44d890
constexpr uint32_t kPT10 = tag("PT10");   // 0x44d858
constexpr uint32_t kPT11 = tag("PT11");   // 0x44d860
constexpr uint32_t kPT12 = tag("PT12");   // 0x44d868
constexpr uint32_t kPT13 = tag("PT13");   // 0x44d870
constexpr uint32_t kPT14 = tag("PT14");   // 0x44d878

int curFloor() { return gHooks.currentFloor != nullptr ? gHooks.currentFloor() : -1; }
void requestFloor(int floor) {                          // FUN_0011bfe0(0x50a2a0, floor, 1)
    if (gHooks.requestFloor != nullptr) gHooks.requestFloor(floor);
}
bool playerDead(const Chr* p) {
    bool v = true;
    if (p->type != 0 && p->type != 2) v = false;
    return v && p->status.block.hp == 0;
}
int roomOf(const Chr* c) {
    if (c->cell == nullptr) return -1;
    return (*c->cell & 0xf000u) == 0x1000u ? static_cast<int>(*c->cell & 0xfu) : -1;
}

// orig FUN_0017a2a0 (work/decompiled/0017a2a0.c): vt+0x1c, the initial state by command (table 0x44d7f8)
void stageInit(StageObject& o) {
    if (o.cmd == kPT10) o.state = StageState::TowerGate;
    else if (o.cmd == kPT11) o.state = StageState::Exit;
    else if (o.cmd == kPT12) o.state = StageState::Exit;
    else if (o.cmd == kPT13) o.state = StageState::Warp;
    else if (o.cmd == kPT14) o.state = StageState::Return;
    else o.state = StageState::Exit;
}

// orig FUN_0017afc0 (work/decompiled/0017afc0.c)
void exitMessage(StageObject& o, int msg) {
    ui::messageBox().open(msg, 999999);                  // FUN_001ab430(0x53d730, msg, 999999)
    o.state = StageState::ExitConfirm;                   // 0x44d898
}

// orig FUN_0017b930 (work/decompiled/0017b930.c)
void fadeOut(StageObject& o) {
    fade().set(0, 0, 0);
    fade().set(0xff000000u, 0xf, 1);
    o.state = StageState::FadeOut;                       // 0x44d918
    talkManager().end();                                 // FUN_001a6710(0x53c6e0)
}

// orig FUN_0017b6d0 (work/decompiled/0017b6d0.c)
void runMenu(StageObject& o) {
    ui::messageBox().open(0x3b, 999999);
    o.state = StageState::RunMenu;                       // 0x44d8e8
}

// orig FUN_0017b3d0 (work/decompiled/0017b3d0.c)
void stairsMenu(StageObject& o) {
    flags().runReset(0x10000);                           // FUN_00178b30(0x53c3b0, 0x10000)
    counters().runReset(0x10000);                        // FUN_00179cb0(0x53c5c0, 0x10000)
    ui::messageBox().open(0x3a, 999999);
    o.state = StageState::StairsMenu;                    // 0x44d8b8
}

// orig FUN_00206040 (work/decompiled/00206040.c)
void saveFlowStart() {
    gSaveFlow = 1;                                       // uRam0053ea40
    saveManager().titleCheck();                          // FUN_00117150(0x5093d0)
    ui::messageBox().open(3, 999999);
}
// orig FUN_00206090 (work/decompiled/00206090.c)
void saveFlowTick() {
    ui::MessageBox& mb = ui::messageBox();
    SaveManager& sm = saveManager();
    if (gSaveFlow == 0 || sm.busy != 0 || !mb.isOpen()) return;
    if (mb.currentId() == 3) {
        if (sm.result == 4 || sm.result == 6) {
            mb.close();
            mb.open(0x28, static_cast<int>(sm.requiredKB));   // uRam005097a0
        } else if (sm.result == 1) {
            mb.close();
            mb.open(0x29, static_cast<int>(sm.requiredKB));
        } else {
            mb.close();
            ui::menu().open(9);                          // FUN_001677c0(0x51b140, 9)
            gSaveFlow = 0;
        }
    } else if (static_cast<uint32_t>(mb.currentId() - 0x28) < 2u && (pad().port[0].pressed & 0x60u) != 0) {   // 0x4e6288
        mb.close();
        gSaveFlow = 0;
    }
}

// orig FUN_0017ba10 (work/decompiled/0017ba10.c): PT10 (tower gate) proximity
void stTowerGate(StageObject& o) {
    Chr* pl = characters().player();
    if (pl == nullptr || playerDead(pl)) return;
    if (pl->xf.m.pos[2] < o.pos[2]) {
        const float x = pl->xf.m.pos[0];
        if (x < -200.0f && -900.0f < x) {
            if ((o.flags & 1u) != 0) return;
            eventMode(1);                                // FUN_0017a0c0() -- argument dropped by Ghidra, UNVERIFIED 1
            o.flags |= 1u;
            exitMessage(o, 0x33);
            return;
        }
    }
    o.flags &= 0xfffffffeu;
}

// orig FUN_0017aad0 (work/decompiled/0017aad0.c): exit object proximity (PT00 / PT11 / PT12 / default)
void stExit(StageObject& o) {
    Chr* pl = characters().player();
    if (pl == nullptr || playerDead(pl)) return;
    EventHooks& H = eventHooks();
    const float rot0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float scl[4] = {100.0f, 100.0f, 100.0f, 1.0f};
    if (o.effectA == -1 || !(H.effectAlive && H.effectAlive(o.effectA))) {
        if (o.cmd == kPT00) {
            const float p[4] = {o.pos[0], o.pos[1] + 1.0f, o.pos[2], 1.0f};
            o.effectA = H.effectSpawn ? H.effectSpawn(0.0f, 0x9a, p, rot0, scl) : -1;   // FUN_00112190
        }
    }
    const float dx = pl->xf.m.pos[0] - o.pos[0];
    const float dz = pl->xf.m.pos[2] - o.pos[2];
    const float d2 = dz * dz + dx * dx + 0.0f;
    if (d2 < 32400.0f) {
        if (o.effectB == -1) {
            if (o.cmd == kPT00) {
                const float p[4] = {o.pos[0], o.pos[1] + 1.0f, o.pos[2], 1.0f};
                o.effectB = H.effectSpawn ? H.effectSpawn(0.0f, 0x9b, p, rot0, scl) : -1;
                if (H.se3d) H.se3d(101.0f, 0xd4, o.pos);  // FUN_00115470(0x42ca0000, snd, 0xd4, pos)
            }
        }
        if (o.effectB != -1 && H.effectAlive && H.effectAlive(o.effectB)) {
            const float dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
            if (H.effectAddRot) H.effectAddRot(o.effectB, dir);   // FUN_001123b0
        }
    } else if (o.effectB != -1) {
        if (H.effectLoopOut) H.effectLoopOut(o.effectB, 0, -1, -1);   // FUN_001126e0(h, 0, -1, -1)
        o.effectB = -1;
    }
    const float r = (o.cmd == kPT11 || o.cmd == kPT12) ? 10000.0f : 2500.0f;
    if (d2 < r) {
        const float py = pl->xf.m.pos[1];
        const float oy = o.pos[1];
        if (py < oy + 10.0f && oy < py + pl->shape.height) {   // physics +0x5c -> +8
            if ((o.flags & 1u) != 0) return;
            eventMode(1);
            o.flags |= 1u;
            int msg = 0x34;
            if (o.cmd == kPT11) msg = flags().test(0x1e) ? 0x37 : 0x35;
            else if (o.cmd == kPT12) msg = 0x36;
            exitMessage(o, msg);
            return;
        }
    }
    o.flags &= 0xfffffffeu;
}

// orig FUN_0017b020 (work/decompiled/0017b020.c)
void stExitConfirm(StageObject& o) {
    ui::MessageBox& mb = ui::messageBox();
    if (mb.currentId() == 0x38) {
        const int c = mb.choice();
        FlagManager& f = flags();
        if (c == 2) {
            mb.close();
            eventMode(0);
            o.state = StageState::Exit;                  // 0x44d8a8
        } else if (c == 0xc) { f.set(0x39d); f.set(0x3f4); fadeOut(o); }
        else if (c == 0xb) { f.set(0x39c); f.set(0x3f1); fadeOut(o); }
        else if (c == 10) {
            inventory().rotClass(3);                     // FUN_001d9610(0x53d910, 3)
            inventory().rotClass(4);
            f.set(0x39b);
            f.set(0x44);
            fadeOut(o);
        } else if (c == 9) { f.set(0x39a); f.set(0x41); fadeOut(o); }
        else if (c == 8) { f.set(0x399); f.set(0x3e); fadeOut(o); }
        else if (c == 7) { f.set(0x398); f.set(0x3b); fadeOut(o); }
        else if (c == 6) { f.set(0x397); fadeOut(o); }
        return;
    }
    if (mb.choice() == 2) {
        mb.close();
        eventMode(0);
        stageInit(o);                                    // vt+0x1c
    } else if (mb.choice() == 1) {
        const int fl = curFloor();
        if (isRandomDungeon(fl) || isSecondDungeon(fl)) {   // FUN_0011c290 / FUN_0011c390
            mb.close();
            stairsMenu(o);
        } else if (mb.currentId() == 0x37) {
            mb.close();
            exitMessage(o, 0x38);
        } else {
            flags().runReset(0x10000);
            counters().runReset(0x10000);
            fadeOut(o);
        }
    }
}

// orig FUN_0017b450 (work/decompiled/0017b450.c)
void stStairsMenu(StageObject& o) {
    ui::MessageBox& mb = ui::messageBox();
    if (mb.choice() == 3) {
        fadeOut(o);
    } else if (mb.choice() == 4) {
        mb.close();
        saveFlowStart();                                 // FUN_00206040
        o.state = StageState::StairsSaveWait;            // 0x44d8c8
    } else if (mb.choice() == 5) {
        mb.close();
        mb.open(0x39, 999999);
        o.state = StageState::StairsTitle;               // 0x44d8d8
    }
}

// orig FUN_0017b550 (work/decompiled/0017b550.c) / FUN_0017b840 (work/decompiled/0017b840.c)
void stSaveWait(StageObject& o, bool stairs) {
    if (gSaveFlow == 0) {
        if (!ui::menu().isOpen() && saveManager().busy == 0) {   // FUN_00167d70, iRam00509838
            if (stairs) stairsMenu(o);
            else runMenu(o);
        }
    } else {
        saveFlowTick();                                  // FUN_00206090
    }
}

// orig FUN_0017b5c0 (work/decompiled/0017b5c0.c) / FUN_0017b8b0 (work/decompiled/0017b8b0.c)
void stTitle(StageObject& o, bool stairs) {
    ui::MessageBox& mb = ui::messageBox();
    if (mb.choice() == 1) {
        o.flags |= 0x80000000u;
        fadeOut(o);
    } else if (mb.choice() == 2) {
        mb.close();
        if (stairs) stairsMenu(o);
        else runMenu(o);
    }
}

// orig FUN_0017b730 (work/decompiled/0017b730.c)
void stRunMenu(StageObject& o) {
    ui::MessageBox& mb = ui::messageBox();
    if (mb.choice() == 3) {
        o.flags |= 0x40000000u;
        fadeOut(o);
    } else if (mb.choice() == 4) {
        mb.close();
        ui::menu().open(9);                              // FUN_001677c0(0x51b140, 9): in-game FILE_SELECT save
        o.state = StageState::RunSaveWait;               // 0x44d8f8
    } else if (mb.choice() == 5) {
        mb.close();
        mb.open(0x39, 999999);
        o.state = StageState::RunTitle;                  // 0x44d908
    }
}

// orig FUN_0017b9b0 (work/decompiled/0017b9b0.c)
void stFadeOut(StageObject& o) {
    if (fade().frames != 0) return;                      // iRam004e7898
    if (ui::messageBox().isOpen()) ui::messageBox().close();   // iRam0053d734 == 1 -> FUN_001ac020
    stageCommand(o);                                     // FUN_0017a510
}

// orig FUN_0017c4e0 (work/decompiled/0017c4e0.c): the player died during the warp chain -> material fader ended
// (FUN_0010a130), uRam0053c6b0 = 0, state = null PTMF (uRam0055fdc8..); returns true then.
bool warpPlayerDied(StageObject& o) {
    Chr* pl = characters().player();                     // iRam0050b144
    if (pl == nullptr || !playerDead(pl)) return false;
    modelFadeEnd(pl->model);                             // FUN_0010a130(*(player +0x2c))
    gWarpPause = 0;
    o.state = StageState::None;
    return true;
}

void stWarpFade(StageObject& o);
void stWarpCamera(StageObject& o);
void stWarpFlash(StageObject& o);

// orig FUN_0017c0c0 (work/decompiled/0017c0c0.c). Each step of the chain: when the state (+0x28) is not yet this
// step's PTMF (FUN_0040caa0 against 0x44d928 / 0x44d948 / 0x44d968 / 0x44d988), it is set (0x44d938 / 0x44d958 /
// 0x44d978 / 0x44d998, the same function) and the step starts; the counter +0x44 runs down, 0 -> next step.
void stWarpGlow(StageObject& o) {
    if (warpPlayerDied(o)) return;
    if (o.state != StageState::WarpGlow) {
        gWarpPause = 1;                                  // uRam0053c6b0 = 1
        o.state = StageState::WarpGlow;
        Chr* pl = characters().player();
        if (PlayerControl* ctl = playerCtl()) ctl->flags |= 0x10000u;   // uRam0050b620 |= 0x10000
        linkPlayerIdle(*pl);                             // FUN_0013d810(0x50b620, player)
        const float from[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float to[4] = {1.0f, 0.3f, 0.0f, 1.0f};   // 0x3f800000, 0x3e99999a, 0, 0x3f800000
        modelFadeStart(pl->model, 1, 10, from, to);      // FUN_0010a0a0(model, 1, 10, from, to)
        o.counter = 10;
    }
    const int n = o.counter - 1;
    o.counter = n;
    if (n == 0) stWarpFlash(o);                          // FUN_0017c1e0
}

// orig FUN_0017c1e0 (work/decompiled/0017c1e0.c)
void stWarpFlash(StageObject& o) {
    if (warpPlayerDied(o)) return;
    if (o.state != StageState::WarpFlash) {
        o.state = StageState::WarpFlash;
        Chr* pl = characters().player();
        const float pos[4] = {pl->xf.m.pos[0], pl->xf.m.pos[1], pl->xf.m.pos[2], 1.0f};
        const float scl[4] = {100.0f, 100.0f, 100.0f, 1.0f};   // 0x42c80000 x3, 0x3f800000
        EventHooks& H = eventHooks();
        if (H.effectSpawn) H.effectSpawn(0.0f, 0xbf, pos, nullptr, scl);   // FUN_00112190(0, 0xbf, pos, 0, scale)
        sound().playSe(1.0f, 0xd9);                      // FUN_00115390(1.0, 0x4e9790, 0xd9)
        const float to[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        modelFadeStart(pl->model, 0, 10, nullptr, to);   // FUN_0010a0a0(model, 0, 10, 0, to)
        o.counter = 0x14;
    }
    const int n = o.counter - 1;
    o.counter = n;
    if (n == 0) stWarpCamera(o);                         // FUN_0017c310
}

// orig FUN_0017c310 (work/decompiled/0017c310.c)
void stWarpCamera(StageObject& o) {
    if (warpPlayerDied(o)) return;
    if (o.state != StageState::WarpCamera) {
        o.state = StageState::WarpCamera;
        o.counter = 0x1e;
    }
    if (PlayerControl* ctl = playerCtl(); ctl != nullptr && ctl->camera != nullptr)
        ctl->camera->pitchInput(2.0f);                   // FUN_0013eff0(0x40000000, uRam0050b624)
    const int n = o.counter - 1;
    o.counter = n;
    if (n == 0) stWarpFade(o);                           // FUN_0017c3b0
}

// orig FUN_0017c3b0 (work/decompiled/0017c3b0.c)
void stWarpFade(StageObject& o) {
    if (warpPlayerDied(o)) return;
    if (o.state != StageState::WarpFade) {
        o.state = StageState::WarpFade;
        fade().set(0, 0, 0);                             // FUN_00106c00(0x4e7890, 0, 0, 0)
        fade().set(0xff000000u, 0xf, 0);                 // FUN_00106c00(0x4e7890, 0xff000000, 0xf, 0)
    }
    if (PlayerControl* ctl = playerCtl(); ctl != nullptr && ctl->camera != nullptr)
        ctl->camera->pitchInput(2.0f);                   // FUN_0013eff0(0x40000000, uRam0050b624)
    if (fade().frames == 0) {                            // iRam004e7898
        Chr* pl = characters().player();
        modelFadeEnd(pl->model);                         // FUN_0010a130
        // the model's switch (+0xc -> +0x10 mask, count +8 > 0): bit 0 cleared (hidden); FUN_0017b350 repeats it
        for (ChrModelPart& part : pl->model.parts) part.visible = false;
        stageDescendMenu(o);                             // FUN_0017b350
        gWarpPause = 0;                                  // uRam0053c6b0 = 0
    }
}

// orig FUN_0017bb30 (work/decompiled/0017bb30.c): PT13 (floor 0x5c IS01 warp). While the linked item (+0x3c) is a
// live object of the item list 0x50a830 (flags +0x10 bit0 set, 0x10000000 clear) nothing happens; once it is gone
// (picked up) the warp chain starts. Nothing while the player is dead.
void stWarp(StageObject& o) {
    Chr* pl = characters().player();
    if (pl == nullptr || playerDead(pl)) return;
    GameAccess* g = itemSystem().game;
    const int n = g != nullptr ? g->floorItemCount() : 0;   // FUN_0012c1e0(0x50b130) / FUN_0012c1f0(0x50b130)
    for (int i = 0; i < n; ++i) {
        const Item* it = g->floorItem(i);
        if (it == nullptr) continue;
        if ((it->taskFlags & 1u) != 0 && (it->taskFlags & 0x10000000u) == 0 && it == o.linked) return;
    }
    stWarpGlow(o);                                       // FUN_0017c0c0
}

// orig FUN_0017bc40 (work/decompiled/0017bc40.c): PT14, the exit of the scenario floors 307/317/327 and of floor 499.
// The live player within 50 (dx^2 + dz^2 <= 2500) consumes the first set flag of the chain (FUN_00178a30 clear,
// FUN_00178890 set x2, FUN_00179320 guarded achievement) and starts the warp chain.
void stReturn(StageObject& o) {
    Chr* pl = characters().player();
    if (pl == nullptr || playerDead(pl)) return;
    const float dx = pl->xf.m.pos[0] - o.pos[0];
    const float dz = pl->xf.m.pos[2] - o.pos[2];
    if (!(dz * dz + dx * dx + 0.0f <= 2500.0f)) return;
    FlagManager& f = flags();
    struct Step { uint32_t test, set1, set2; int guarded; };
    static const Step kSteps[10] = {
        {0x2f5, 0x31, 0x3f7, 0},     {0x2f6, 0x32, 0x3f8, 0},     {0x2f7, 0x33, 0x3f9, 0x47b},
        {0x397, 0x39, 0x3fa, 0x443}, {0x398, 0x3c, 0x3fb, 0x444}, {0x399, 0x3f, 0x3fc, 0x445},
        {0x39a, 0x42, 0x3fd, 0x446}, {0x39b, 0x45, 0x3fe, 0x447}, {0x39c, 0x3f2, 0x3ff, 0x448},
        {0x39d, 0x3f5, 0x400, 0x449}};
    for (const Step& st : kSteps) {
        if (!f.test(st.test)) continue;
        f.clear(st.test);                                // FUN_00178a30
        f.set(st.set1);                                  // FUN_00178890
        f.set(st.set2);
        if (st.guarded != 0) f.setGuarded(st.guarded);   // FUN_00179320
        break;
    }
    stWarpGlow(o);                                       // FUN_0017c0c0
}

// orig FUN_0017a4d0 (work/decompiled/0017a4d0.c): call the PTMF when it is set (FUN_0040caf0 / FUN_0040ca60)
void stageUpdate(StageObject& o) {
    switch (o.state) {
    case StageState::None: break;
    case StageState::TowerGate: stTowerGate(o); break;
    case StageState::Exit: stExit(o); break;
    case StageState::Warp: stWarp(o); break;
    case StageState::Return: stReturn(o); break;
    case StageState::WarpGlow: stWarpGlow(o); break;
    case StageState::WarpFlash: stWarpFlash(o); break;
    case StageState::WarpCamera: stWarpCamera(o); break;
    case StageState::WarpFade: stWarpFade(o); break;
    case StageState::ExitConfirm: stExitConfirm(o); break;
    case StageState::StairsMenu: stStairsMenu(o); break;
    case StageState::StairsSaveWait: stSaveWait(o, true); break;
    case StageState::StairsTitle: stTitle(o, true); break;
    case StageState::RunMenu: stRunMenu(o); break;
    case StageState::RunSaveWait: stSaveWait(o, false); break;
    case StageState::RunTitle: stTitle(o, false); break;
    case StageState::FadeOut: stFadeOut(o); break;
    }
}

}  // namespace

StageHooks& stageHooks() { return gHooks; }
int stageCount() { return static_cast<int>(gObjects.size()); }
int stageWarpPause() { return gWarpPause; }

// orig FUN_00179f60 (work/decompiled/00179f60.c): FUN_0017c5f0 -> FUN_0017a180 ctor, FUN_00128690(obj, 5, cmd)
StageObject* stageCreate(const char* cmd) {
    auto o = std::make_unique<StageObject>();
    o->flags = 0;                                        // +0x24
    o->state = StageState::None;                         // +0x28..+0x30 = uRam0055fdc8..
    o->effectA = -1;                                     // +0x34
    o->effectB = -1;                                     // +0x38
    o->linked = nullptr;                                 // +0x3c
    o->type = 5;                                         // +0x18
    std::memcpy(&o->cmd, cmd, 4);                        // +0x1c
    stageInit(*o);                                       // vt+0x1c = FUN_0017a2a0
    gObjects.push_back(std::move(o));
    return gObjects.back().get();
}

// orig FUN_0017a050 (work/decompiled/0017a050.c)
void stageTickAll() {
    for (size_t i = 0; i < gObjects.size(); ++i) stageUpdate(*gObjects[i]);   // vt+0xc = FUN_0017a4d0
}

// orig FUN_00179fe0 (work/decompiled/00179fe0.c): FUN_0017c5a0 (vt+8 dtor, free) on each, count 0
void stageClearAll() { gObjects.clear(); }

// orig FUN_0017b640 (work/decompiled/0017b640.c): run end ("PT99" from FUN_0011cd40)
void stageRunEnd(StageObject& o) {
    fade().flags &= 0xfffffffdu;                         // uRam004e7890 &= ~2
    if (Chr* p = characters().player()) {
        // actor node flags bit0 cleared: the player model is hidden (restored by the rebirth path, UNVERIFIED)
        for (ChrModelPart& part : p->model.parts) part.visible = false;
    }
    if (gHooks.hideFloor != nullptr) gHooks.hideFloor();   // FUN_0011ed90(0x50a350)
    eventMode(1);                                        // FUN_0017a0c0(1)
    runMenu(o);                                          // FUN_0017b6d0
}

// orig FUN_0017b350 (work/decompiled/0017b350.c): the player model's switch (+0xc mask) bit 0 cleared (model
// hidden), FUN_0011ed90(0x50a350), FUN_0017a0c0(1), FUN_0017b3d0 (descend / save menu 0x3a)
void stageDescendMenu(StageObject& o) {
    if (Chr* p = characters().player()) {
        for (ChrModelPart& part : p->model.parts) part.visible = false;
    }
    if (gHooks.hideFloor != nullptr) gHooks.hideFloor();   // FUN_0011ed90(0x50a350)
    eventMode(1);                                        // FUN_0017a0c0(1)
    stairsMenu(o);                                       // FUN_0017b3d0
}

// orig FUN_0017a510 (work/decompiled/0017a510.c)
void stageCommand(StageObject& o) {
    if ((o.flags & 0x80000000u) == 0) {
        const uint32_t c = o.cmd;
        if (c == kPT00) {
            requestFloor(saveDungeonId(curFloor()));     // FUN_0011c570 -> FUN_0011bfe0(Game, FUN_0011c480(), 1)
            const Chr* pl = characters().player();
            const int room = pl != nullptr ? roomOf(pl) : -1;
            for (const Chr* e : characters().list()) {
                if ((e->taskFlags & 0x10000000u) == 0 && e->type == 2 && e->cell != nullptr && roomOf(e) == room) {
                    counters().add(0x1b, 1);             // FUN_00179570(0x53c5c0, 0x1b, 1)
                    break;
                }
            }
        } else if (c == kPT10) {
            flags().runReset(0x1000);
            counters().runReset(0x1000);
            requestFloor(100);
        } else if (c == kPT11) {
            FlagManager& f = flags();
            if (f.test(0x1e)) {
                f.runReset(0x4000);
                counters().runReset(0x4000);
                requestFloor(400);
            } else if (f.test(0x32)) {
                f.runReset(0x2000);
                counters().runReset(0x2000);
                f.set(0x2f7);
                requestFloor(0x141);
            } else if (f.test(0x31)) {
                f.runReset(0x2000);
                counters().runReset(0x2000);
                f.set(0x2f6);
                requestFloor(0x137);
            } else {
                f.runReset(0x2000);
                counters().runReset(0x2000);
                f.set(0x2f5);
                f.set(0x2f);
                requestFloor(0x12d);
            }
        } else if (c == kPT12) {
            requestFloor(0x5c);
        } else if (c == kPT13 || c == kPT14) {
            requestFloor(0x5b);
        } else if ((o.flags & 0x40000000u) == 0 && isNoStairsFloor(curFloor())) {   // FUN_0011c220
            requestFloor(saveDungeonId(curFloor()));     // FUN_0011c570
        } else {
            requestFloor(0x5b);
        }
    } else {
        scenes().requestScene("Title...");               // FUN_00206550(0x53ea80, "Title...")
    }
    eventMode(0);                                        // FUN_0017a0c0(0)
    o.state = StageState::None;                          // +0x28.. = uRam0055fdc8..
    // FUN_0013a940(0x50b620): pad rumble stop, control flags &= 0xcfffffff
    if (eventHooks().padRumbleStop) eventHooks().padRumbleStop();
    if (PlayerControl* ctl = playerCtl()) ctl->flags &= 0xcfffffffu;
}

}  // namespace baroque::game
