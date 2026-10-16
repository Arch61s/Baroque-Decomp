// Player Event state (factory LAB_001d1de0 -> FUN_001d2ac0, vtable 0x4ced70; ctor FUN_001d1df0, dtor FUN_001d2130,
// update FUN_001d21d0), requested by FUN_001d1d30 (eventTrigger) through eventHooks().playerEventState. Its PTMF is
// chosen by the trigger number (uRam0053d840) and runs once the event script is over (iRam0051b620 != 1): fades,
// return to the hub / floor 0x5b, Ending, the IS01 / IS04 gifts, the PT99 run-end menu (table 0x463d80..0x463e00).
// Game build only (event, progression, stage, items modules).
#include "game/character/character.hpp"

#include "audio/sound_system.hpp"
#include "game/character/combat.hpp"
#include "game/items/item_links.hpp"
#include "game/sound_stub.hpp"
#include "game/character/player_control.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/stage_script.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/item_world.hpp"
#include "game/objects/objects.hpp"
#include "game/progress/progression.hpp"
#include "game/scene_manager.hpp"
#include "game/scenes/game_scene.hpp"
#include "platform/platform.hpp"

namespace baroque::game {

namespace {

constexpr uint32_t kEvDefault = 0x1d2220, kEvHub = 0x1d2280, kEvGiftIS01 = 0x1d2360, kEvGiftIS04 = 0x1d24c0,
                   kEvHub2 = 0x1d25e0, kEvHub58 = 0x1d2690, kEvHub3 = 0x1d2780, kEvEnding = 0x1d2830, kEvFall = 0x1d2950;

// the pop of every PTMF: (+0x14 == 0 && +0x18 == 0) || +0xc < 3 -> +0x18 = -1, +8 = 0, +0xc = +0x10 = 2
void popPrio2(Chr& c) {
    StateMachine& sm = c.sm;
    if ((sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority < 3) {
        sm.next = StateId::None;
        sm.popRequested = true;
        sm.arg = 0;
        sm.priority = 2;
        sm.priorityCopy = 2;
    }
}

void fadeBlack() {
    fade().set(0xff000000u, 0, 0);   // FUN_00106c00(0x4e7890, 0xff000000, 0, 0)
    fade().set(0xff000000u, 1, 0);
}

uint32_t tag4(const char* s) { return objTag(s); }

// FUN_0012ce70(0x50b1a0, tag, 0) + FUN_001d65c0(0x53d910, item); null result of the add -> `full(item)`
template <class F> void gift(const char* tag, F&& full) {
    Chr* e = characters().createTag(tag4(tag), false);
    if (e == nullptr || e->item == nullptr) return;
    if (!inventory().add(e->item, itemSystem())) full(*e);
}

// FUN_00179f60("PT99") + FUN_0017b350 (descend / save menu)
void runEndMenu() {
    StageObject* o = stageCreate("PT99");
    if (o == nullptr) return;
    stageDescendMenu(*o);   // FUN_0017b350
}

}  // namespace

// orig FUN_001d1df0 (ctor = enter)
void playerEventEnter(Chr& c) {
    StateMachine& sm = c.sm;
    if (PlayerControl* ctl = playerCtl()) ctl->flags |= 0x10000u;
    const int n = eventManager().triggerId;   // uRam0053d840
    uint32_t f = kEvDefault;                  // 0x463d80
    if (n == 0x32) f = kEvFall;
    else if (n == 0xf) f = kEvEnding;
    else if (n == 0xb) f = kEvHub3;
    else if (n == 0x48 || n == 0x47) f = kEvHub58;
    else if (n == 0x53 || n == 10) f = kEvHub2;
    else if (n == 0xd) f = kEvGiftIS04;
    else if (n == 0x3e || n == 6) f = kEvGiftIS01;
    else if (n == 0x35 || n == 0x34 || n == 0x33 || n == 1) f = kEvHub;
    sm.eventPtmf = f;
}

// orig FUN_001d2130 (dtor = exit)
void playerEventExit(Chr& c) {
    (void)c;
    if (PlayerControl* ctl = playerCtl()) ctl->flags &= 0xfffeffffu;
}

// orig FUN_001d21d0: the PTMF runs when no event script is running
void playerEventUpdate(Chr& c) {
    StateMachine& sm = c.sm;
    if (eventActive() || sm.eventPtmf == 0) return;
    const uint32_t f = sm.eventPtmf;
    switch (f) {
    case kEvDefault:   // FUN_001d2220 (disasm)
        break;
    case kEvHub:       // FUN_001d2280
        fadeBlack();
        if (eventManager().triggerId == 1) {
            fade().setPause(1);                       // FUN_00106810(0x4e7890, 1)
            requestFloor(gameProgress(), 0x5b, 1);    // FUN_0011bfe0(0x50a2a0, 0x5b, 1)
        } else {
            returnToHub(gameProgress());              // FUN_0011cd40
        }
        break;
    case kEvGiftIS01:  // FUN_001d2360 (0x463e10 "IS01")
        gift("IS01", [](Chr& e) { gameAdapter().freeItem(e.item); });   // FUN_001b7570
        fadeBlack();
        runEndMenu();
        break;
    case kEvGiftIS04:  // FUN_001d24c0 (0x463e20 "IS04"): a full inventory drops it at the player
        gift("IS04", [](Chr& e) {
            if (const Chr* pl = characters().player()) {
                e.xf.m.pos = pl->xf.m.pos;
                e.item->pos = {pl->xf.m.pos[0], pl->xf.m.pos[1], pl->xf.m.pos[2]};
            }
            dropItemAtActor(*e.item, -1.0f, nullptr, itemSystem());   // FUN_001d5410(-1.0, item, 0)
        });
        break;
    case kEvHub2:      // FUN_001d25e0
    case kEvHub3:      // FUN_001d2780
        fadeBlack();
        returnToHub(gameProgress());
        break;
    case kEvHub58:     // FUN_001d2690
        fadeBlack();
        if (flags().test(0x58)) flags().clear(0x58);
        else flags().set(0x58);
        returnToHub(gameProgress());
        break;
    case kEvEnding:    // FUN_001d2830 (0x463e28 "Ending..")
        fadeBlack();
        scenes().requestScene("Ending..");
        break;
    case kEvFall:      // FUN_001d2950
        fadeBlack();
        if (flags().test(0x1e) && !flags().test(0x3bd)) onPlayerDeath(gameProgress());   // FUN_0011d120
        else runEndMenu();
        break;
    default: return;
    }
    popPrio2(c);   // the PTMF stays set: it runs again only when the pop was refused (as in the original)
}

// ---- TypeId state of C19A / C19D (factory LAB_001293e0 -> FUN_0012de30, vtable 0x4cdac0), requested with priority 0
// by FUN_001b3220. PTMF (state +8..+0x10) in sm.eventPtmf, frame counter +0x14 in sm.timer. The run ends here:
// approaching the angel -> FUN_0011d120, the angel touched by J030 -> FUN_0011cfa0 (dungeon cleared).
namespace {
constexpr uint32_t kTiApproach = 0x1b3030, kTiAbsorbed = 0x1b3100, kTiCleared = 0x1b3190;

// the pop of FUN_001b3100 / FUN_001b3190: (+0x14 == 0 && +0x18 == 0) || +0xc < 1 -> +0x18 = -1, +8 = 0, +0xc = +0x10 = 0
void popPrio0(Chr& c) {
    StateMachine& sm = c.sm;
    if ((sm.curFactory == StateId::None && sm.next == StateId::None) || sm.priority < 1) {
        sm.next = StateId::None;
        sm.popRequested = true;
        sm.arg = 0;
        sm.priority = 0;
        sm.priorityCopy = 0;
    }
}

// FUN_00115020(0x4e9790, 0): BGM fade-out (as gameOverTick)
void bgmFadeOut() {
    if (auto* m = dynamic_cast<audio::GameSoundManager*>(&sound())) m->system().sceneChangeStop(0);
}

// orig FUN_001b2d60 (work/decompiled/001b2d60.c)
void typeIdApproachDone(Chr& c) {
    if (PlayerControl* ctl = playerCtl()) ctl->flags |= 0x10000u;   // uRam0050b620 |= 0x10000
    if (Chr* pl = characters().player()) linkPlayerIdle(*pl);      // FUN_0013d810(0x50b620, player)
    fade().set(0, 0, 0);                                           // FUN_00106c00(0x4e7890, 0, 0, 0)
    fade().set(0xff000000u, 0x96, 1);
    c.sm.eventPtmf = kTiAbsorbed;                                  // 0x462aa0 = FUN_001b3100
    bgmFadeOut();
}
}  // namespace

// orig FUN_001b2f40 (work/decompiled/001b2f40.c)
void typeIdEnter(Chr& c) {
    StateMachine& sm = c.sm;
    if (flags().test(0x21) || eventActive()) sm.eventPtmf = 0;     // iRam0051b620 == 1: null PTMF (uRam0055fdc8..)
    else sm.eventPtmf = kTiApproach;                               // 0x462ac0 = FUN_001b3030
    sm.timer = 0;                                                  // +0x14
}

// orig FUN_001b2e10 (work/decompiled/001b2e10.c): reached from FUN_00129140 (C19x touched by J030 in this state)
void typeIdShot(Chr& c) {
    if (auto* m = dynamic_cast<audio::GameSoundManager*>(&sound())) m->system().stopCurrentVoice();   // FUN_00115c10
    if (flags().test(0x16)) sound().playVoice(1.0f, 0x46a);        // FUN_00115ce0(1.0, 0x4e9790, 0x46a)
    else if (flags().test(5)) sound().playVoice(1.0f, 0x467);
    if (PlayerControl* ctl = playerCtl()) ctl->flags |= 0x10000u;
    if (Chr* pl = characters().player()) linkPlayerIdle(*pl);
    fade().set(0, 0, 0);
    fade().set(0xff000000u, 0xaa, 1);
    c.sm.eventPtmf = kTiCleared;                                   // 0x462ab0 = FUN_001b3190
    bgmFadeOut();
}

// orig FUN_001b2ff0 (work/decompiled/001b2ff0.c): the PTMF when set
void typeIdUpdate(Chr& c) {
    StateMachine& sm = c.sm;
    switch (sm.eventPtmf) {
    case kTiApproach: {   // FUN_001b3030
        const Chr* pl = characters().player();
        if (pl == nullptr) return;
        const float dz = c.xf.m.pos[2] - pl->xf.m.pos[2];
        const float dx = c.xf.m.pos[0] - pl->xf.m.pos[0];
        if (dz * dz + dx * dx + 0.0f < 40000.0f) {
            const Vec3f& v = pl->phys.moveVel;                     // player physics +0x7c..+0x84
            if (0.0f < v[2] * dz + v[0] * dx + v[1] * 0.0f + 0.0f + 0.0f) {
                sm.timer = sm.timer + 1;
                if (0x1e < sm.timer) typeIdApproachDone(c);
            } else {
                sm.timer = 0;
            }
        } else {
            sm.timer = 0;
        }
        break;
    }
    case kTiAbsorbed:     // FUN_001b3100
        if (fade().frames != 0) return;                            // iRam004e7898
        onPlayerDeath(gameProgress());                             // FUN_0011d120(0x50a2a0)
        popPrio0(c);
        break;
    case kTiCleared:      // FUN_001b3190
        if (fade().frames != 0) return;
        onDungeonCleared(gameProgress());                          // FUN_0011cfa0(0x50a2a0)
        popPrio0(c);
        break;
    default: break;
    }
}

}  // namespace baroque::game
