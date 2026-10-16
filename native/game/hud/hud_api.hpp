#pragma once
// Integration API of the items / inventory / HUD module for the Game scene owner. Each entry point is a slice of an
// original routine; the comment names the routine and the exact place of the call inside it.
//
//   Game Init, first entry   FUN_0011b300 (game_scene.cpp GameScene::init, "FUN_0011b300" block):
//       ... FUN_00114fd0 sound init, [hudGameInit(): FUN_001520d0(0x517b60)], FUN_0015da60 optionApply,
//       FUN_00163e20 gradientCreate, [hudGameInit(): FUN_0014ee70(0x5179c0)], FUN_00106120(0x23..0x2b) ...
//       -> call hudGameInit() once, after sound().init() and before ui::textures().loadRange(0x23, 0x2b).
//   Game Init, every floor   FUN_0011a9f0 (GameScene::init after the FUN_0011b300 block):
//       uRam0050b660 = uRam0050b664 = 0x3c; [hudFloorInit(): FUN_001d8210(0x53d910), FUN_001d5ef0(0x53d910)]
//       ... FUN_0011f960 floor load ... [hudFloorReady(): iRam00517b34 = depth; FUN_00151af0(0x5179c0, 0)]
//       -> hudFloorInit() where the port's comment "FUN_001d8210/FUN_001d5ef0 (inventory/weather) not translated"
//          stands; hudFloorReady() right after the floor load (loadFloor) succeeded.
//   Engine tick              FUN_00101bd0 (engine.cpp Engine::tick):
//       FUN_001ac4a0(0x53d730) message box, [hudGameTick(): FUN_00156de0(0x517b60), FUN_00150ba0(0x5179c0),
//       FUN_0015c380(0x5180f0) centre line + log], FUN_00167d80 menu, FUN_00168010 brightness,
//       if engine +0x98 == 0: [hudPauseOverlay(FUN_00102020()): FUN_00160af0]
//       -> hudGameTick() between ui::messageBox().update() and ui::uiTick() (the text-slot part of FUN_0015c380 is
//          ui::helpBar().update(), already in ui::uiTick()); hudPauseOverlay() right after ui::uiTick().
//   Game task                FUN_0011d850 (game_scene.cpp GameTask): FUN_0013aaa0 pause / item screen input
//       -> hudItemBagRequestTick() (FUN_0015c070), hudItemScreenInput() (FUN_00157a60), hudItemScreenOpen();
//       FUN_0011edf0 floor context tick -> hudMapTick() (FUN_00126380)
//   Floor load / unload      FUN_00120060 floor finalize -> hudMapCreate() (FUN_00126120);
//                            FUN_0011f6c0 floor unload (Game Exit) -> hudMapReset() (FUN_00126300)
//   Player controller        FUN_0013b0d0 (player_control.cpp hooks): FUN_001d9b60(0x53d910) -> hudPlayerTick();
//       iRam0053d910 -> hudInventoryCount(); FUN_001573f0 / FUN_00157400 -> hudItemSlotSelected() /
//       hudSetItemSlotSelected(); SELECT: iRam0050a3f8 / FUN_00126cc0 / FUN_00126fb0 -> hudMapShown() /
//       hudMapShow() / hudMapHide(); *(0x53d914 + slot*4) -> hudInventoryItem()
//   Character tick           FUN_00129c60 tail, player only (character.cpp chrCommonTick):
//       iRam00517b34 = floor 99 ? 99999 : floorNumber*100 - round(y)/10; FUN_00151af0(0x5179c0, 4)
//       -> hudPlayerDepth(y)
//   HP / VT changes          FUN_001349e0 -> FUN_00151ae0(0x5179c0, hp) -> hudSetHp(hp);
//                            FUN_00134b60 -> uRam00517b2c = vt         -> hudSetVt(vt)
//                            (chr_adapter.cpp statusAddHpHud / statusAddVtHud already do both for item effects)
//   Item screen input        FUN_0013aaa0 (player control) -> FUN_00157a60(0x517b60) -> hudItemScreenInput(pressed, repeat)
//   Ribbon slot step         FUN_0013c790 (player control, L1 / R1 config words) -> FUN_00157410 / FUN_001575f0(0x517b60)
//                            -> hudItemSlotStep(false / true)
//   Game Exit                FUN_0011bad0 (GameScene::exit): FUN_001d8180(0x53d910); next scene != "Game....":
//       FUN_0011bcb0 -> FUN_0014eef0(0x5179c0) and (next != "Ending..") FUN_0011be70 item part, FUN_001d8280(0x53d910);
//       FUN_0015c3f0(0x5180f0, -1), FUN_0015ce70(0x5180f0), FUN_0015d0a0(0x5180f0)
//       -> hudGameExit(nextIsGame, nextIsEnding) where the original calls FUN_001d8180.
#include "game/hud/hud.hpp"
#include "game/hud/map2d.hpp"
#include "game/hud/message_log.hpp"
#include "game/items/game_adapter.hpp"
#include "game/items/item_screen.hpp"

namespace baroque::game {

void hudGameInit();                  // FUN_0011b300 part
void hudFloorInit();                 // FUN_0011a9f0 part (FUN_001d8210 + FUN_001d5ef0)
void hudFloorReady();                // FUN_0011a9f0 tail (depth + FUN_00151af0(0))
void hudGameTick();                  // FUN_00101bd0 part
void hudPlayerTick();                // FUN_001d9b60(0x53d910) from FUN_0013b0d0
void hudPlayerDepth(float playerY);  // FUN_00129c60 tail for the player
void hudPauseOverlay(bool paused);   // FUN_00160af0(paused): pause overlay FUN_00160c30 / FUN_00160d30 + log enable
void hudGameExit(bool nextIsGame, bool nextIsEnding = false);   // FUN_0011bad0 part
void hudItemsWipe();                 // FUN_0011be70 item part: FUN_001d5e60, FUN_001d9b00 (0x53d910), FUN_001dac60 (0x53dc00)
// 2D map of the floor context 0x50a350
void hudMapCreate();                 // FUN_00126120
void hudMapReset();                  // FUN_00126300
void hudMapTick();                   // FUN_00126380
void hudMapShow();                   // FUN_00126cc0 (+ FUN_00178890(0x53c3b0, 99))
void hudMapHide();                   // FUN_00126fb0
bool hudMapShown();                  // iRam0050a3f8 != 0
Map2D& map2d();
Map2DSource& dungeonMapSource();     // hud_map.cpp: over gameWorld().dungeon and the player
// item screen 0x517b60 / inventory 0x53d910 reads of the game task and the player controller
void hudItemBagRequestTick(bool playerDead, bool eventCamera);   // FUN_0015c070(0x517b60)
bool hudItemScreenOpen();            // iRam00517b68 != 0
int hudInventoryCount();             // iRam0053d910
Item* hudInventoryItem(int slot);    // *(0x53d914 + slot*4)
int hudItemSlotSelected();           // FUN_001573f0(0x517b60)
void hudSetItemSlotSelected(int slot);   // FUN_00157400(0x517b60, slot)
void hudItemScreenInput(uint32_t pressed, uint32_t repeat);   // FUN_00157a60(0x517b60)
void hudItemSlotStep(bool next);     // FUN_001575f0 (next) / FUN_00157410 (prev), closed-screen path
void hudSetHp(int hp);               // FUN_00151ae0(0x5179c0, hp)
void hudSetVt(int vt);               // uRam00517b2c = vt
void hudSetDepth(int depth);         // iRam00517b34 = depth
void hudRefreshBars(uint32_t which); // FUN_00151af0(0x5179c0, which)
void hudLayoutBars(int flag);        // FUN_00151660(0x5179c0, flag)
// PORT: developer option (main.cpp --give): creates item `id` through FUN_0012ce70 and adds it with FUN_001d65c0;
// `identified` sets +0x70 bit 0 first (--give-identified). False when the inventory refused it.
bool hudDevGiveItem(uint32_t id, bool identified);

// Engine / scene / menu / option globals the HUD reads each tick (hud_env.cpp).
struct HudEnvironment {
    bool sceneIsGame = false;        // uRam0053ea90 == "Game...."
    bool systemMenuOpen = false;     // FUN_00167d70(0x51b140)
    bool messageBoxOpen = false;     // iRam0053d734 (message box 0x53d730 +4)
    bool fading = false;             // iRam004e7898 != 0
    bool paused = false;             // FUN_00102020(0x4e6640)
    int pauseFrames = 0;             // engine +0x98
    int cursorPosOption = 0;         // iRam0051af34 (CURSOR POSITION option)
    uint32_t buttonTalk = 0x20;      // 0x4e65c0
    uint32_t buttonItemScreen = 0x10;// 0x4e65d8
    uint32_t buttonNext = 8;         // 0x4e65e0
    uint32_t buttonPrev = 4;         // 0x4e65f0
};
HudEnvironment hudReadEnvironment();

// PORT-ONLY: window-resolution overlays drawn right after the 3-D pass and before the 2-D UI layers (main.cpp
// present(); alpha = presented time between the last two ticks). The keyboard + mouse gun reticle
// (game/hud/gun_reticle.cpp) installs itself here, so targets without it (Baroque-ItemsCheck) draw nothing.
using HudWindowOverlayFn = void (*)(int screenW, int screenH, float alpha);
void hudSetWindowOverlay(HudWindowOverlayFn fn);
void hudDrawWindowOverlay(int screenW, int screenH, float alpha);

// Boot-time PRMITEM load (orig FUN_001d3530 from FUN_00100730); hudGameInit() calls it when not loaded yet.
bool itemPrmLoad();

// Objects of the module (original addresses)
StatusHud& statusHud();              // 0x5179c0
MessageLog& messageLog();            // 0x5180f0 (log / centre part)
Inventory& inventory();              // 0x53d910
CollectorBag& collectorBag();        // 0x53dc00
ItemScreen& itemScreen();            // 0x517b60 (screen part)
ItemRibbon& itemRibbon();            // 0x517b60 (ribbon part)
ItemSystem& itemSystem();
GameAdapter& gameAdapter();
// World values the floor context / talk system own. When gameWorld().dungeon is set, floorId / floorFlags /
// floorSeedByte are refreshed from it at every hudGameTick; the remaining fields are written by their owners.
ItemWorldState& hudWorld();

}  // namespace baroque::game
