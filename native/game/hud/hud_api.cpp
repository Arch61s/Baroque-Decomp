#include "game/hud/hud_api.hpp"
#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/game_world.hpp"
#include "game/hud/ui_sprite2d.hpp"
#include "game/flag_manager.hpp"
#include "game/render2d.hpp"
#include "game/ui/sprite.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/item_links.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/strings.hpp"
#include "platform/platform.hpp"
#include <fstream>
#include <iterator>

namespace baroque::game {

namespace {
StatusHud gStatusHud;            // 0x5179c0
MessageLog gMessageLog;          // 0x5180f0
Inventory gInventory;            // 0x53d910
CollectorBag gBag;               // 0x53dc00
ItemScreen gItemScreen;          // 0x517b60
ItemRibbon gItemRibbon;          // 0x517b60 (ribbon fields)
ItemSystem gItemSystem;
GameAdapter gGame;
data::PrmSet gPrm;               // iRam0053d860.. PRMITEM file objects
bool gPrmLoaded = false;
bool gStaticInit = false;
Map2D gMap;                      // 0x50a350 map part (+0xa8..+0xd0)
// pause overlay of FUN_00160af0: iRam0051afc0 (overlay allowed), iRam0051afb8 dim quad, iRam0051afb0 "PAUSE" caption
int gPauseAllowed = 0;
ui::Sprite* gPauseDim = nullptr;
ui::Sprite* gPauseCaption = nullptr;

// the static constructors FUN_004c7c50 (0x5179c0: FUN_0014ec20) and FUN_004c7cd0 (0x5180f0: FUN_0015c2f0) plus
// the port wiring of the module objects
void staticInit() {
    if (gStaticInit) return;
    gStaticInit = true;
    gStatusHud.resetCounters();
    gItemSystem.prm = &gPrm;
    gItemSystem.rng = &gameWorld().rng;   // 0x53eb20
    gItemSystem.game = &gGame;
    gItemSystem.inventory = &gInventory;
    gItemSystem.bag = &gBag;
    gGame.sys = &gItemSystem;
    gGame.log = &gMessageLog;
    gGame.screen = &gItemScreen;
    ChrAdapterEnv& env = chrAdapterEnv();
    env.inventory = &gInventory;
    env.items = &gItemSystem;
    env.hud = &gStatusHud;
}

// floor values from the generated floor when the Game scene has one (gameWorld().dungeon)
void syncWorld() {
    ItemWorldState& w = gGame.world;
    if (const Dungeon* d = gameWorld().dungeon) {
        w.floorId = d->floorId();
        w.floorFlags = (d->floorFlags() & ~w.clearedFloorFlags) | w.portFloorFlags;
        w.floorSeedByte = d->fdt().mapSet;   // UNVERIFIED: *pcRam0050a620 taken as byte 0 of the floor's FDT (mapSet)
        w.targetCellRoom = d->context().stairsRoom;   // uRam0050a360 = floor context +0x10
    }
}

HudVisibility visibility(const HudEnvironment& e) {
    const ItemWorldState& w = gGame.world;
    HudVisibility v;
    v.sceneIsGame = e.sceneIsGame;                    // uRam0053ea90
    v.floorGenerated = (w.floorFlags & 1u) != 0;     // uRam0050a610 & 1
    v.systemMenuOpen = e.systemMenuOpen;              // FUN_00167d70(0x51b140)
    v.eventRunning = w.eventRunning;                  // iRam0051b620
    v.fading = e.fading;                              // iRam004e7898
    v.dialogOpen = w.dialogOpen;                      // iRam0053bf88
    v.talkActive = w.talkActive;                      // FUN_001a2950(0x53c6e0)
    v.paused = e.paused;                              // FUN_00102020(0x4e6640)
    v.talkSystemBusy = w.talkBusy;                    // iRam0053c6a8
    return v;
}

void syncButtons(const HudEnvironment& e) {
    HudButtons h;
    h.talk = static_cast<int>(e.buttonTalk);
    h.itemScreen = static_cast<int>(e.buttonItemScreen);
    h.slotNext = static_cast<int>(e.buttonNext);
    h.slotPrev = static_cast<int>(e.buttonPrev);
    gStatusHud.buttons = h;
    gItemRibbon.buttons = h;
}

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

StatusHud& statusHud() { staticInit(); return gStatusHud; }
MessageLog& messageLog() { staticInit(); return gMessageLog; }
Inventory& inventory() { staticInit(); return gInventory; }
CollectorBag& collectorBag() { staticInit(); return gBag; }
ItemScreen& itemScreen() { staticInit(); return gItemScreen; }
ItemRibbon& itemRibbon() { staticInit(); return gItemRibbon; }
ItemSystem& itemSystem() { staticInit(); return gItemSystem; }
GameAdapter& gameAdapter() { staticInit(); return gGame; }
ItemWorldState& hudWorld() { staticInit(); return gGame.world; }

// orig FUN_001d3530 (work/decompiled/001d3530.c): PRMITEM00..14 except 0xe, path 0x4641b0 (JP) / 0x4641d0 (EN)
bool itemPrmLoad() {
    staticInit();
    bool ok = true;
    const bool jp = ui::language() == 1;
    for (uint32_t i = 0; i < data::kItemClassCount; ++i) {
        const std::string rel = data::prmItemPath(jp, i);
        const std::vector<uint8_t> bytes = readFile(platform::assetRoot() / rel);
        std::string err;
        auto f = data::parsePrmItem(bytes, static_cast<data::ItemClass>(i), &err);
        if (!f) {
            platform::log("items: " + rel + ": " + err);
            ok = false;
            continue;
        }
        gPrm.items[i] = std::move(*f);
    }
    // PRMCHAR (FUN_00133620) for the flesh items' PRMCHAR +0x90 / +0x94 (FUN_001ce7e0 -> FUN_00133560)
    {
        const std::string rel = data::prmCharPath(jp);
        std::string err;
        auto c = data::parsePrmChar(readFile(platform::assetRoot() / rel), &err);
        if (c) gPrm.chars = std::move(*c);
        else { platform::log("items: " + rel + ": " + err); ok = false; }
    }
    gPrmLoaded = ok;
    return ok;
}

// FUN_0011b300 part: FUN_001520d0(0x517b60) and FUN_0014ee70(0x5179c0)
void hudGameInit() {
    staticInit();
    if (!gPrmLoaded) itemPrmLoad();       // boot-time FUN_001d3530 in the original
    gItemScreen.reset();                  // FUN_001520d0: screen fields
    gItemRibbon = ItemRibbon{};           // FUN_001520d0: +0x10 = 0, +0x18 = -1, +0x28 = -1, +0x50 = 0, +0x5c = 0
    gStatusHud.createTextures();          // FUN_0014ee70
}

// FUN_0011a9f0 part: FUN_001d8210(0x53d910), FUN_001d5ef0(0x53d910)
void hudFloorInit() {
    staticInit();
    syncWorld();
    gInventory.restoreEquipment(gItemSystem);
    gInventory.refreshFlags(gItemSystem);
}

// FUN_0011a9f0 tail after FUN_0011f960: iRam00517b34 = FUN_0011c100 ? 99999 : FUN_0011c3b0 * 100;
// FUN_00151af0(0x5179c0, 0)
void hudFloorReady() {
    staticInit();
    syncWorld();
    const int id = gGame.world.floorId;
    if (!isFloor99(id)) gStatusHud.setDepth(floorNumber(id) * 100);
    else gStatusHud.setDepth(99999);
    gStatusHud.refreshBars(0);
}

// FUN_00129c60 tail for the player: iRam00517b34 = 99999 on floor 99, else
// FUN_0011c3b0 * 100 - (int)FUN_00206a50(y) / 10; FUN_00151af0(0x5179c0, 4)
void hudPlayerDepth(float playerY) {
    staticInit();
    const int id = gGame.world.floorId;
    if (isFloor99(id)) {
        gStatusHud.setDepth(99999);
    } else {
        float r = playerY;   // FUN_00206a50: round half away from zero
        if (r < 0.0f) r = r - 0.5f;
        else r = r + 0.5f;
        const float rounded = static_cast<float>(static_cast<int>(r));
        gStatusHud.setDepth(floorNumber(id) * 100 - static_cast<int>(rounded) / 10);
    }
    gStatusHud.refreshBars(4);
}

// FUN_001d9b60(0x53d910) from the player controller FUN_0013b0d0
void hudPlayerTick() {
    staticInit();
    if (characters().player() == nullptr) return;
    syncWorld();   // the floor flags (bit 8 "new floor") as the game task sees them this tick
    gInventory.tick(gItemSystem);
}

// FUN_00101bd0 part (see hud_api.hpp for the position in the engine tick)
void hudGameTick() {
    staticInit();
    syncWorld();
    gGame.purgeItems();   // item part of the task walk (FUN_00207380 / FUN_00101200), see GameAdapter::purgeItems
    const HudEnvironment envGlobals = hudReadEnvironment();
    gGame.runPendingTraps(!envGlobals.paused);   // trap objects FUN_001e5ca0 (no object-module hooks), see game_adapter.cpp
    syncButtons(envGlobals);
    Chr* p = characters().player();
    gStatusHud.player = p != nullptr ? &chrAdapter(p) : nullptr;   // iRam0050b144
    UiSprite2D& out = uiSprite2D();
    out.beginFrame();
    const HudVisibility vis = visibility(envGlobals);

    // FUN_00156de0(0x517b60): item screen and ribbon (the +0x28 cursor is shared by both)
    ItemScreenEnv env;
    env.sceneIsGame = vis.sceneIsGame;
    env.systemMenuOpen = vis.systemMenuOpen;
    env.eventRunning = vis.eventRunning;
    env.talkBusy = vis.talkSystemBusy;
    env.talkChoice = gGame.world.talkChoice;
    env.talkWindow = gGame.world.talkWindow;
    env.paused = vis.paused;
    env.floorGenerated = vis.floorGenerated;
    env.playerNoInput = (linkPlayerCtlFlags() & 0x10000u) != 0;   // uRam0050b620 & 0x10000
    env.cursorResetOption = envGlobals.cursorPosOption == 1;   // iRam0051af34 == 1
    env.itemScreenButton = gItemRibbon.buttons.itemScreen;
    bool playerDead = false;   // the inline type 0 / 2 + HP 0 test of FUN_00156de0 on iRam0050b144
    if (gStatusHud.player != nullptr) {
        const int t = gStatusHud.player->actorType();
        playerDead = (t == 0 || t == 2) && !gStatusHud.player->isAlive();
    }
    gItemScreen.tick(gItemSystem, env, gStatusHud.player != nullptr, playerDead);
    if (gItemScreenMouse.items != nullptr) gItemScreenMouse.items(gItemScreen, gItemSystem, env);   // PORT: mouse items
    gItemRibbon.setCursor(gItemScreen.ribbonCursor());
    gItemRibbon.tick(gItemSystem, vis, gItemScreen.page(), gItemScreen.isOpen(), out);
    gItemScreen.setRibbonCursor(gItemRibbon.cursor());
    {   // PORT widescreen: the ribbon (bottom right) keeps its distance to the right screen edge
        ui::AnchorScope anchor(kAnchorRight);
        gItemRibbon.draw(out);
    }
    // 2D map sprite (+0xa8) and markers: persistent in the original (created by FUN_00126cc0, moved by FUN_00126380
    // in the game task); the port re-emits it here, after beginFrame(), from the state the game task left
    gMap.draw(out);

    // FUN_00150ba0(0x5179c0)
    {   // PORT widescreen: the status block (top left) keeps its distance to the left edge; its bottom-right item
        // frame is anchored right inside StatusHud::build
        ui::AnchorScope anchor(kAnchorLeft);
        gStatusHud.tick(vis);
    }

    // FUN_0015c380(0x5180f0) -> FUN_0015c3b0: FUN_0015c650 (ui::helpBar, in ui::uiTick), FUN_0015cec0, FUN_0015d120
    if (gMessageLog.active != 0) {
        gMessageLog.drawCentre();
        ui::AnchorScope anchor(kAnchorLeft);   // PORT widescreen: the log lines (x 24) stay at the left edge
        gMessageLog.drawLog();
    }
}

// FUN_00160af0(paused) (work/decompiled/00160af0.c), called by FUN_00101bd0 when engine +0x98 == 0: the pause overlay
// (FUN_00160c30 show / FUN_00160d30 hide) and the message log enable +0x2b00 (uRam0051abf0)
void hudPauseOverlay(bool paused) {
    staticInit();
    const HudEnvironment e = hudReadEnvironment();
    const bool screenOpen = gItemScreen.isOpen();   // iRam00517b68
    if (!paused || !screenOpen) {
        if (!paused && !screenOpen) gPauseAllowed = 1;
    } else {
        gPauseAllowed = 0;
    }
    if (!paused || gPauseAllowed == 0 || e.systemMenuOpen || e.messageBoxOpen || gGame.world.talkBusy) {
        // orig FUN_00160d30 (work/decompiled/00160d30.c)
        ui::sprites().destroyRef(gPauseCaption);
        ui::sprites().destroyRef(gPauseDim);
    } else {
        // orig FUN_00160c30 (work/decompiled/00160c30.c) [V disasm: stack v1 / texW / texH]
        if (gPauseDim == nullptr) {
            gPauseDim = ui::sprites().create(0.0f, 0.0f, 0.0f, 512.0f, 448.0f, 0.0f, 0.0f, 0.0f, 0.0f, kTexNoTexture, 3);
            if (gPauseDim != nullptr) gPauseDim->color = 0x20000000u;
        }
        if (gPauseCaption == nullptr) {   // "PAUSE": TEX_UI11 (0x29) u 0..64, v 13..29, texW / texH 64
            gPauseCaption = ui::sprites().create(225.0f, 217.0f, 5.1f, 64.0f, 16.0f, 0.0f, 13.0f, 64.0f, 29.0f, kTexUi11, 0);
            if (gPauseCaption != nullptr) gPauseCaption->color = 0x407f7f7fu;
        }
    }
    if (!paused || gMessageLog.enabled == 0 || screenOpen) {
        if ((!paused && gMessageLog.enabled == 0) || screenOpen) gMessageLog.enabled = 1;
    } else {
        gMessageLog.enabled = 0;
    }
}

// FUN_0011bad0 part (see hud_api.hpp for the exact positions)
void hudGameExit(bool nextIsGame, bool nextIsEnding) {
    staticInit();
    gInventory.stashEquipment(gItemSystem);   // FUN_001d8180(0x53d910)
    if (!nextIsGame) {
        gStatusHud.releaseTextures();         // FUN_0011bcb0 -> FUN_0014eef0(0x5179c0)
        if (!nextIsEnding) hudItemsWipe();    // FUN_0011bcb0 -> FUN_0011be70 (item part)
        gInventory.stashReset();              // FUN_001d8280(0x53d910)
    }
    ui::helpBar().clear(-1);                  // FUN_0015c3f0(0x5180f0, -1): text slots of the same object
    gMessageLog.clearCentre();                // FUN_0015ce70(0x5180f0)
    gMessageLog.clear();                      // FUN_0015d0a0(0x5180f0)
    uiSprite2D().beginFrame();
}

// FUN_0011be70 item part: FUN_001d5e60(0x53d910), FUN_001d9b00(0x53d910), FUN_001dac60(0x53dc00)
void hudItemsWipe() {
    staticInit();
    gInventory.clear(gItemSystem);
    gInventory.freePartB(gItemSystem);
    gBag.clear(gItemSystem);
}

// ---- 2D map of the floor context 0x50a350 --------------------------------------------------------------------
Map2D& map2d() { return gMap; }
// FUN_00126120(0x50a350) from the floor finalize FUN_00120060
void hudMapCreate() { staticInit(); gMap.createTexture(dungeonMapSource(), uiSprite2D()); }
// FUN_00126300(0x50a350) (from the floor unload FUN_0011f6c0 and the generators FUN_00123dc0 / FUN_00124250)
void hudMapReset() { staticInit(); gMap.reset(uiSprite2D()); }
// FUN_00126380(0x50a350) from FUN_0011edf0 (game task)
void hudMapTick() { staticInit(); gMap.tick(dungeonMapSource(), uiSprite2D()); }
// orig FUN_00126cc0 head: FUN_00178890(0x53c3b0, 99) once the map is created
void hudMapShow() {
    staticInit();
    syncWorld();
    if (gMap.show(dungeonMapSource(), gGame.world.floorId)) flags().set(99);
}
void hudMapHide() { gMap.hide(); }                 // FUN_00126fb0(0x50a350)
bool hudMapShown() { return gMap.shown(); }        // iRam0050a3f8 != 0

// FUN_0015c070(0x517b60) from FUN_0013aaa0
void hudItemBagRequestTick(bool playerDead, bool eventCamera) {
    staticInit();
    syncWorld();
    const HudEnvironment e = hudReadEnvironment();
    ItemScreenEnv env;
    env.sceneIsGame = e.sceneIsGame;
    env.systemMenuOpen = e.systemMenuOpen;
    env.eventRunning = gGame.world.eventRunning;
    env.talkBusy = gGame.world.talkBusy;
    env.talkChoice = gGame.world.talkChoice;
    env.talkWindow = gGame.world.talkWindow;
    env.paused = e.paused;
    env.floorGenerated = (gGame.world.floorFlags & 1u) != 0;
    env.cursorResetOption = e.cursorPosOption == 1;
    env.itemScreenButton = static_cast<int>(e.buttonItemScreen);
    gItemScreen.setRibbonCursor(gItemRibbon.cursor());
    gItemScreen.bagRequestTick(gItemSystem, env, playerDead, eventCamera, gMessageLog.enabled);
    gItemRibbon.setCursor(gItemScreen.ribbonCursor());
}
bool hudItemScreenOpen() { return gItemScreen.isOpen(); }   // iRam00517b68 != 0

// inventory / ribbon reads of the player controller FUN_0013b0d0 (player_control.cpp hooks)
int hudInventoryCount() { return inventory().count; }                                  // iRam0053d910
Item* hudInventoryItem(int slot) {                                                     // *(0x53d914 + slot*4)
    Inventory& inv = inventory();
    return slot >= 0 && slot < Inventory::kMaxItems ? inv.slots[slot] : nullptr;
}
int hudItemSlotSelected() { staticInit(); return gItemRibbon.cursor(); }                // FUN_001573f0: +0x28
void hudSetItemSlotSelected(int slot) {                                                 // FUN_00157400: +0x28 = slot
    staticInit();
    gItemRibbon.setCursor(slot);
    gItemScreen.setRibbonCursor(slot);
}

// FUN_00157a60(0x517b60) from FUN_0013aaa0
void hudItemScreenInput(uint32_t pressed, uint32_t repeat) {
    staticInit();
    const HudEnvironment e = hudReadEnvironment();
    ItemScreenEnv env;
    env.sceneIsGame = e.sceneIsGame;
    env.systemMenuOpen = e.systemMenuOpen;
    env.eventRunning = gGame.world.eventRunning;
    env.talkBusy = gGame.world.talkBusy;
    env.talkChoice = gGame.world.talkChoice;
    env.talkWindow = gGame.world.talkWindow;
    env.paused = e.paused;
    env.floorGenerated = (gGame.world.floorFlags & 1u) != 0;
    env.cursorResetOption = e.cursorPosOption == 1;
    env.itemScreenButton = static_cast<int>(e.buttonItemScreen);
    PadInput pad;
    pad.pressed = pressed;
    pad.repeat = repeat;
    gItemScreen.setRibbonCursor(gItemRibbon.cursor());
    if (gItemScreenMouse.hover != nullptr) gItemScreenMouse.hover(gItemScreen, gItemSystem, env);   // PORT: mouse hover
    gItemScreen.input(gItemSystem, pad, env);
    gItemRibbon.setCursor(gItemScreen.ribbonCursor());
}

// FUN_001575f0 / FUN_00157410 (0x517b60) from FUN_0013c790
void hudItemSlotStep(bool next) {
    staticInit();
    gItemRibbon.setCursor(gItemScreen.ribbonCursor());
    if (next) gItemRibbon.stepNext(gItemSystem);
    else gItemRibbon.stepPrev(gItemSystem);
    gItemScreen.setRibbonCursor(gItemRibbon.cursor());
}

void hudSetHp(int hp) { statusHud().setHp(hp); }
void hudSetVt(int vt) { statusHud().setVt(vt); }
void hudSetDepth(int depth) { statusHud().setDepth(depth); }
void hudRefreshBars(uint32_t which) { statusHud().refreshBars(which); }
void hudLayoutBars(int flag) { statusHud().layout(flag); }

// PORT: --give (main.cpp)
bool hudDevGiveItem(uint32_t id, bool identified) {
    staticInit();
    if (!gPrmLoaded) itemPrmLoad();
    Item* it = gGame.createItem(id, false);
    if (it == nullptr) return false;
    if (identified) it->flags |= kItemIdentified;
    if (gInventory.add(it, gItemSystem)) return true;
    gGame.freeItem(it);
    return false;
}

// PORT-ONLY: window-resolution overlays (the keyboard + mouse gun reticle, game/hud/gun_reticle.cpp)
namespace {
HudWindowOverlayFn& windowOverlay() {
    static HudWindowOverlayFn fn = nullptr;
    return fn;
}
}  // namespace
void hudSetWindowOverlay(HudWindowOverlayFn fn) { windowOverlay() = fn; }
void hudDrawWindowOverlay(int screenW, int screenH, float alpha) {
    if (HudWindowOverlayFn fn = windowOverlay()) fn(screenW, screenH, alpha);
}

}  // namespace baroque::game
