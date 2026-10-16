// Headless check of the items / inventory / HUD module (see native/cmake/items.cmake).
//   1. status HUD 0x5179c0: build on the first Game tick, sprite count, digit / bar / label positions and colours
//      against ITEMS_HUD_MENUS.md 6.1, HP / VT easing (3 per tick) and bar colours, depth counter, level-change
//      animation, talk-mode strip, release when the floor is not generated
//   2. inventory 0x53d910: add / equip / totals / flags / full inventory / remove
//   3. message log 0x5180f0: print, line split, typewriter, fade timers, glyph sprites
//   4. capture: UI sprite layer rendered by game::renderer2d() into 1440x1080 -> work/native_captures/hud_status.png
#include "game/character/character.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/hud/ui_sprite2d.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/item_links.hpp"
#include "game/render2d.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include "raylib.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace baroque::game {
// The check stands in for hud_env.cpp (engine / scene / menu globals) and for the character manager.
HudEnvironment gCheckEnv;
HudEnvironment hudReadEnvironment() { return gCheckEnv; }
ChrManager& characters() {
    static ChrManager m;
    return m;
}
// ... and for the talk manager 0x53c6e0 (game/talk is not linked): the item-side notifications are counted only
int gTalkNotes = 0;
void talkItemTaken(Item*) { ++gTalkNotes; }
void talkItemHandOver(Item*, bool) { ++gTalkNotes; }
void talkNoteCoinToss(int) { ++gTalkNotes; }
void talkNoteEnemyHit() { ++gTalkNotes; }
void talkNoteItemScreen() { ++gTalkNotes; }
void talkNoteDatabase(uint32_t) { ++gTalkNotes; }
// ... and for game/items/item_links_game.cpp (combat, AI, effects, voice and animation sets are not linked): the
// status routines keep the bit / EXP bookkeeping the item code reads back, everything presentational is idle.
uint32_t linkInflictStatus(StatusObj& s, uint32_t mask) { s.block.statusMask |= mask; return mask; }
uint32_t linkStatusExpire(StatusObj& s, uint32_t mask) {
    const uint32_t c = s.block.statusMask & mask & 0x7ffffu;
    s.block.statusMask &= ~c;
    return c;
}
uint32_t linkCureAllStatus(StatusObj& s) { return linkStatusExpire(s, 0x7ffffu); }
bool linkStatusAddExp(StatusObj& s, int exp) { s.block.exp += exp; return false; }
int linkExpForLevel(int level) {   // FUN_00138dc0
    if (99 < level) level = 99;
    int k = 2, sum = 0, m = 6;
    do { sum += k * m; ++k; m += 3; } while (k <= level);
    return sum;
}
float linkVictimReduction(StatusObj&, const Chr*) { return 1.0f; }
float linkElementWeakness(const StatusObj&, uint32_t) { return 0.0f; }
bool linkApplyDamage(StatusObj& vic, Chr*, int dmg, uint32_t) { return !statusAddHpHud(vic, -dmg); }
void linkHitElementFx(Chr&, uint32_t) {}
void linkRequestDeath(Chr&, int) {}
void linkReact(Chr&, uint32_t, const Vec3&, bool) {}
void linkPlayerIdle(Chr&) {}
uint32_t linkPlayerCtlFlags() { return 0; }
void linkHeadFlash(Chr&) {}
uint32_t linkAiVar(Chr&, uint32_t) { return 0; }
void linkAiSetVar(Chr&, uint32_t, uint32_t) {}
int linkEffectSpawn(int, const Vec3&, const float*, float, float) { return 0; }
bool linkJointPos(Chr&, const char*, Vec3&) { return false; }
bool linkJointMatrix(Chr&, const char*, float*) { return false; }
int linkEffectSpawnMatrix(int, const float*) { return 0; }
void linkEffectKill(int) {}
void linkEffectEnd(int) {}
void linkEffectSetPos(int, const Vec3&) {}
bool linkVoiceOverride() { return false; }
void linkVoiceStop() {}
void linkVoicePause(bool) {}
void linkVoicePlay(int) {}
void linkPlayerSetLoad(Chr&, int, const char*) {}
void linkPlayerRestartAnim(Chr&, const char*) {}
void linkPlayerSetRelease(Chr&, int) {}
bool linkPlayerSlotAttached(Chr&, int) { return false; }
void linkPlayerSlotAttach(Chr&, int, const char*) {}
void linkPlayerSlotDetach(Chr&, int) {}
void linkPlayerBodySkin(Chr&, bool) {}
bool linkPlayerCoatLoaded(const Chr&) { return false; }
void linkPlayerCoatLoad(Chr&, const char*) {}
void linkPlayerCoatUnload(Chr&) {}
void linkPlayerCoatShadow(Chr&, const char*) {}
void linkPlayerModelsReset() {}
// ... and for the player's menu action request in character.cpp (GameAdapter::playerActionRequest): idle
bool playerMenuActionRequest(const char*) { return false; }
}  // namespace baroque::game

using namespace baroque::game;

namespace {
int gFail = 0;
int gPass = 0;
void expect(bool ok, const std::string& what) {
    if (ok) {
        ++gPass;
    } else {
        ++gFail;
        std::printf("FAIL %s\n", what.c_str());
    }
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
std::string f2(float v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", v); return b; }

// one engine tick of the module: glyph sprites of the previous tick freed (FUN_00112c60), then the HUD tick
void tick(int n = 1) {
    for (int i = 0; i < n; ++i) {
        ui::text().endOfTick();
        hudGameTick();
    }
}

// FUN_00151660 bar width for a value (t = v/999; ((1-t)t + t) * 275)
float barWidth(int v) {
    const float t = static_cast<float>(v) / 999.0f;
    return ((1.0f - t) * t + t + 0.0f) * 275.0f;
}

void checkDigitRow(const char* name, const ui::Sprite* const* row, int count, const float (*rects)[4], float dy) {
    for (int i = 0; i < count; ++i) {
        const ui::Sprite* s = row[i];
        expect(s != nullptr, std::string(name) + " digit sprite " + std::to_string(i));
        if (!s) continue;
        // FUN_00166820 with x-1, y-1: centre = x - 1 + w/2 (the glyph setter changes the half width only)
        const float cx = rects[i][0] - 1.0f + rects[i][2] / 2.0f, cy = rects[i][1] + dy - 1.0f + rects[i][3] / 2.0f;
        expect(near(s->cx, cx) && near(s->cy, cy), std::string(name) + " digit " + std::to_string(i) + " centre (" + f2(s->cx) + "," + f2(s->cy) +
                                                      ") expected (" + f2(cx) + "," + f2(cy) + ")");
        expect(s->color == 0x407f7f7fu, std::string(name) + " digit colour");
        expect(s->texId == 0x23, std::string(name) + " digit texture TEX_UI00");
    }
}
}  // namespace

int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1440, 1080, "Baroque items check");
    const bool rendererOk = renderer2d().init();
    ui::setLanguage(0);
    ui::fontsLoad();
    ui::text().init();
    ui::textures().loadRange(0x23, 0x2b);   // FUN_00106120(0x4e6a68, 0x23, 0x2b) in FUN_0011b300
    for (int id = 0x23; id <= 0x2b; ++id) expect(ui::textures().isLoaded(id), "TEX_UI sheet " + std::to_string(id) + " loaded");

    // ---- fake player ---------------------------------------------------------------------------------------
    static Chr player;
    player.type = kChrPlayerType;
    player.id = itemId("C00A");
    player.status.owner = &player;
    StatusBlock& st = player.status.block;
    st.hpMax = 100; st.hp = 100; st.vtMax = 98; st.vt = 98; st.level = 1;
    st.atkBase = 0x11; st.atkMax = 0x11; st.atkLevel = 0x1a; st.defBase = 0xb; st.defMax = 0xb;
    characters().setPlayer(&player);
    gameWorld().rng.seed(0x1234);

    gCheckEnv.sceneIsGame = true;
    hudGameInit();
    expect(itemSystem().rowCount(kClsSD) > 0, "PRMITEM00 loaded (" + std::to_string(itemSystem().rowCount(kClsSD)) + " swords)");
    expect(statusHud().texBack() >= ui::kTexDynamicFirst && statusHud().texHp() >= 0 && statusHud().texVt() >= 0, "FUN_0014ee70 dynamic textures");
    hudWorld().floorId = 101;           // random dungeon floor 1 (FUN_0011c3b0 = 101 % 100)
    hudWorld().floorFlags = 1;          // floor generated
    hudFloorInit();
    hudFloorReady();                    // iRam00517b34 = 100, FUN_00151af0(0)

    // ---- 1. status HUD ---------------------------------------------------------------------------------------
    tick();
    StatusHud& hud = statusHud();
    expect(hud.built(), "status HUD built on the first Game tick");
    expect(hud.spriteCount() == 71, "status HUD sprite count " + std::to_string(hud.spriteCount()) + " (expected 71: 4 bars, 2 labels, 14 HP/VT digits, 8 depth digits, EXP label, 13 frame, 6 indicators, 7 item frame, LV label, 2 level digits, 13 talk strip)");
    static const float kHpRects[7][4] = {{99, 26, 7, 12}, {107, 26, 7, 12}, {115, 26, 7, 12}, {124, 26, 5, 12}, {130, 26, 7, 12}, {138, 26, 7, 12}, {146, 26, 7, 12}};
    static const float kDepthRects[8][4] = {{47, 72, 6, 12}, {53, 72, 7, 12}, {61, 72, 7, 12}, {69, 72, 7, 12}, {77, 72, 7, 12}, {85, 72, 7, 12}, {93, 72, 7, 12}, {101, 72, 7, 12}};
    const ui::Sprite* hpRow[7]; const ui::Sprite* vtRow[7]; const ui::Sprite* depthRow[8];
    for (int i = 0; i < 7; ++i) { hpRow[i] = hud.hpDigit(i); vtRow[i] = hud.vtDigit(i); }
    for (int i = 0; i < 8; ++i) depthRow[i] = hud.depthDigit(i);
    checkDigitRow("HP", hpRow, 7, kHpRects, 0.0f);
    checkDigitRow("VT", vtRow, 7, kHpRects, 23.0f);
    checkDigitRow("depth", depthRow, 8, kDepthRects, 0.0f);
    // "100/100": '1' -> u 0..7/128, '/' half width 2.5, '0' -> u 63..70
    if (hpRow[0] && hpRow[3] && hpRow[1]) {
        expect(near(hpRow[0]->u0, 0.0f) && near(hpRow[0]->u1, 7.0f / 128.0f) && near(hpRow[0]->v1, 12.0f / 128.0f), "HP digit '1' UVs");
        expect(near(hpRow[1]->u0, 63.0f / 128.0f) && near(hpRow[1]->u1, 70.0f / 128.0f), "HP digit '0' UVs");
        expect(near(hpRow[3]->hw, 2.5f) && near(hpRow[3]->u0, 70.0f / 128.0f), "HP '/' glyph");
    }
    if (vtRow[1] && vtRow[2]) expect(near(vtRow[1]->u0, 8.0f * 7.0f / 128.0f) && near(vtRow[2]->u0, 7.0f * 7.0f / 128.0f), "VT \" 98/ 98\" digits '9','8'");
    // depth "%8d" of -100 = "    -100"
    if (depthRow[4] && depthRow[5]) expect(near(depthRow[4]->u0, 75.0f / 128.0f) && near(depthRow[5]->u0, 0.0f), "depth digits \"    -100\" ('-' then '1')");
    expect(hud.depthEased() == 100, "depth target/eased 100 on floor 101 (got " + std::to_string(hud.depthEased()) + ")");
    // bars (FUN_00151660): back = max, fill = eased; x from 100, y 41 / 63, half height 2, z -0.9 / -0.8
    auto checkBar = [](const ui::Sprite* s, int v, float y, float z, const char* name) {
        expect(s != nullptr, std::string(name) + " sprite");
        if (!s) return;
        const float w = barWidth(v);
        expect(near(s->cx, w * 0.5f + 100.0f) && near(s->cy, y) && near(s->hw, w * 0.5f) && near(s->hh, 2.0f) && near(s->z, z),
               std::string(name) + " rect cx " + f2(s->cx) + " hw " + f2(s->hw) + " expected width " + f2(w) + " at y " + f2(y));
        expect(s->color == 0x407f7f7fu && near(s->u1, 1.0f) && near(s->v1, 1.0f), std::string(name) + " colour / UVs");
    };
    checkBar(hud.hpBack(), 100, 41.0f, -0.9f, "HP bar back");
    checkBar(hud.hpFill(), 100, 41.0f, -0.8f, "HP bar fill");
    checkBar(hud.vtBack(), 98, 63.0f, -0.9f, "VT bar back");
    checkBar(hud.vtFill(), 98, 63.0f, -0.8f, "VT bar fill");
    // FUN_00151960 colours: >= 80 % green 0x44ff44, rows 0/3 alpha 0x10, rows 1/2 alpha 0x40 + 3x
    if (const uint32_t* px = ui::textures().dynamicPixels(hud.texHp())) {
        expect(px[0] == 0x1044ff44u && px[0x40] == 0x4044ff44u && px[0x40 + 63] == 0xfd44ff44u, "HP bar texture green gradient");
    }
    // HP 100 -> 25 through the status routine (FUN_001349e0 -> FUN_00151ae0), eased 3 per tick
    ChrAdapter& pa = chrAdapter(&player);
    pa.addHp(-75);
    tick();
    expect(hud.hpEased() == 97, "HP eased 100 -> 97 after one tick (got " + std::to_string(hud.hpEased()) + ")");
    tick(24);
    expect(hud.hpEased() == 25, "HP eased to 25 after 25 ticks (got " + std::to_string(hud.hpEased()) + ")");
    if (const uint32_t* px = ui::textures().dynamicPixels(hud.texHp())) expect(px[0x40] == 0x403333ffu, "HP bar red below 30 %");
    checkBar(hud.hpFill(), 25, 41.0f, -0.8f, "HP bar fill at 25");
    expect(flags().test(0x2fc) && !flags().test(0x2fb), "low-HP flag 0x2fc (HP 25)");
    pa.addVt(-50);
    tick(17);
    expect(hud.vtEased() == 48, "VT eased to 48 (got " + std::to_string(hud.vtEased()) + ")");
    if (const uint32_t* px = ui::textures().dynamicPixels(hud.texVt())) expect(px[0x40] == 0x403355ffu, "VT bar orange 30..50 %");
    // "%3d/%3d" of 25/100: ' ','2','5','/','1','0','0'
    if (hpRow[0] && hpRow[1]) expect(near(hpRow[0]->u1, 0.0f) && near(hpRow[0]->hw, 3.5f) && near(hpRow[1]->u0, 7.0f / 128.0f), "HP digits \" 25/100\"");
    // level change: frame 0 at once, +1 every 3 ticks up to 0x13, then -1
    st.level = 2;
    tick();
    expect(hud.levelFrame() == 0, "level-change frame 0 (got " + std::to_string(hud.levelFrame()) + ")");
    tick(3);
    expect(hud.levelFrame() == 1, "level-change frame 1 after 3 ticks (got " + std::to_string(hud.levelFrame()) + ")");
    tick(0x13 * 3);
    expect(hud.levelFrame() == -1, "level-change animation finished (got " + std::to_string(hud.levelFrame()) + ")");
    if (const ui::Sprite* l0 = hud.levelDigit(0)) expect(near(l0->cx, 25.0f + 5.5f) && near(l0->cy, 47.5f), "level digit x 30.5 for level < 10");
    // talk mode (FUN_001a2950 != 0 -> FUN_00150610(2)): strip at (23,112) etc., fade from 0
    hudWorld().talkActive = true;
    tick();
    expect(hud.talkMode() == 2 && hud.spriteCount() == 73, "talk mode 2: strip + button + TEX_UI11 caption (sprites " + std::to_string(hud.spriteCount()) + ")");
    hudWorld().talkActive = false;
    tick();
    expect(hud.talkMode() == 0 && hud.spriteCount() == 71, "talk mode cleared");
    // release / rebuild with the floor flag
    const int layerBefore = ui::sprites().liveCount();
    hudWorld().floorFlags = 0;
    tick();
    expect(!hud.built() && hud.spriteCount() == 0, "HUD released while the floor is not generated");
    expect(ui::sprites().liveCount() <= layerBefore - 71, "UI layer sprites freed (" + std::to_string(layerBefore) + " -> " + std::to_string(ui::sprites().liveCount()) + ")");
    hudWorld().floorFlags = 1;
    tick();
    expect(hud.built() && hud.hpEased() == 25 && hud.spriteCount() == 71, "HUD rebuilt from the status block (FUN_0014ecc0)");

    // ---- 2. inventory ------------------------------------------------------------------------------------------
    Inventory& inv = inventory();
    GameAdapter& ga = gameAdapter();
    Item* sword = ga.createItem(itemId("SD00"), false);
    expect(sword != nullptr && sword->cls == kClsSD && sword->idx == 0, "FUN_0012ce70 creates SD00");
    if (sword) {
        expect(sword->atkBase == itemSystem().word(kClsSD, 0, 0x88), "SD00 base ATK = PRM +0x88 (" + std::to_string(sword->atkBase) + ")");
        expect(inv.add(sword, itemSystem()) && inv.count == 1 && flags().test(0x2a8) && flags().test(0x2cb), "add: count 1, flags 0x2a8 / 0x2cb");
        expect(inv.equipItem(0, true, itemSystem()) && inv.equip[0] == sword && sword->identified(), "equip SD00 into slot 0 (identified)");
        expect(flags().test(0x2a9) && flags().test(0x2d0), "equip flags 0x2a9 / 0x2d0");
        expect(inv.atkTotal(itemSystem()) == sword->atkBase + sword->atkMod, "FUN_001d83d0 total ATK");
        expect(pa.attackPower() == sword->atkBase + sword->atkMod + st.atkMax + st.atkLevel, "FUN_001344f0 attack power");
    }
    Item* wings = ga.createItem(itemId("WG04"), false);
    if (wings) {
        inv.add(wings, itemSystem());
        inv.equipItem(inv.indexOf(wings), true, itemSystem());
        hudRefreshBars(0);   // the item screen's equip path refreshes the bars (FUN_00158890 -> FUN_00151af0)
        expect(inv.equip[3] == wings && inv.hpMaxBonus(itemSystem()) == wings->hpMaxBonus, "WG04 in slot 3, HP max bonus " + std::to_string(wings->hpMaxBonus));
        tick();
        expect(hud.hpMax() == (100 + wings->hpMaxBonus > 999 ? 999 : 100 + wings->hpMaxBonus), "HUD HP max includes the equipment bonus (FUN_001343f0 -> " + std::to_string(hud.hpMax()) + ")");
    }
    while (inv.count < Inventory::kMaxItems) {
        Item* bone = ga.createItem(itemId("BN00"), false);
        if (!bone || !inv.add(bone, itemSystem())) break;
    }
    Item* extra = ga.createItem(itemId("HT00"), false);
    expect(inv.count == 20 && extra && !inv.add(extra, itemSystem()) && flags().test(0x2c5), "21st item refused, 'full' flag 0x2c5");
    ga.freeItem(extra);
    Item* removed = inv.remove(0, itemSystem());
    expect(removed == sword && inv.equip[0] == nullptr && inv.count == 19, "remove(0) unequips and shifts");
    if (removed) ga.freeItem(removed);

    // item screen: triangle -> open request (FUN_00157a60), opened on the next input call (FUN_00157fa0 ->
    // FUN_001524b0), fade +8 per tick; triangle again -> 8-tick close countdown (FUN_001529a0)
    hudItemScreenInput(0x10, 0);
    hudItemScreenInput(0, 0);
    expect(itemScreen().isOpen() && itemScreen().page() == 0, "item screen opens on the inventory page");
    tick(8);
    expect(itemScreen().fade() == 0x40 && itemScreen().ready(), "item screen fade complete after 8 ticks");
    {
        // FUN_001524b0 sprites: backdrop + 3 labels + 33 digits, FUN_00153790 32 + 8 + 4 pieces (no hints at +0x54 = 1
        // unless a description page exists), FUN_00155500 2 + 56 + 18 + 2 arrows + 2 tabs + caption + sort hint
        tick();   // FUN_00159150 runs before FUN_001591e0, so the backdrop follows the fade one tick later
        ItemScreen& is = itemScreen();
        const int base = 37 + 44 + 83;
        expect(is.spriteCount() >= base, "item screen sprites (" + std::to_string(is.spriteCount()) + " >= " + std::to_string(base) + ")");
        expect(is.backdrop() && is.backdrop()->color == (static_cast<uint32_t>(static_cast<int>(0x40 * 0.9f)) << 24) &&
                   near(is.backdrop()->cx, 256.0f) && near(is.backdrop()->hw, 261.0f),
               "FUN_00159150 backdrop (-5,-5,522x458) alpha 0.9 * fade");
        expect(is.expDigit(0) && near(is.expDigit(0)->cx, 95.0f + 3.5f) && near(is.expDigit(0)->cy, 141.0f + 6.0f), "EXP digit 0 at (96-1, 142-1)");
        hudItemScreenInput(0, 0x4000);   // down: FUN_00158500 -> FUN_001575f0, FUN_00153f00 -> text-window slot 1
        ui::HelpBar& hb = ui::helpBar();
        expect(hb.slotText(1)[0] != '\0' && near(hb.x(1), 24.0f) && near(hb.y(1), 369.0f) && near(hb.w(1), 480.0f) && near(hb.h(1), 40.0f) &&
                   hb.speed(1) == 4,
               std::string("description in help slot 1 at (24, 369, 480x40), speed 4: \"") + hb.slotText(1) + "\"");
        hudItemScreenInput(0x40, 0);     // cross: context menu (FUN_00152a00)
        expect(is.contextOpen() && is.contextBox() && near(is.contextBox()->cx, 275.0f + 57.5f) && near(is.contextBox()->z, 2.0f),
               "context box (275, 134, 115x78) z 2");
        hudItemScreenInput(0x20, 0);     // circle: FUN_001580a0 -> FUN_00152ba0
        expect(!is.contextOpen() && !is.contextBox(), "circle closes the context menu and frees its sprites");
    }
    hudItemScreenInput(0x10, 0);
    expect(ui::helpBar().slotText(1)[0] == '\0', "FUN_001529a0 clears help slots 1 / 2");
    tick(8);
    expect(!itemScreen().isOpen() && itemScreen().page() == -1, "item screen closed after the 8-tick countdown");
    expect(itemScreen().spriteCount() == 0, "FUN_001561f0 freed every item-screen sprite");

    // ---- 3. message log ----------------------------------------------------------------------------------------
    MessageLog& log = messageLog();
    log.clear();
    log.print("Obtained %s.", "Sword");
    expect(std::strcmp(log.line[0], "Obtained Sword.") == 0 && log.timer[0] == 0x5a && log.reveal == 0, "log print -> line 0, timer 90");
    log.print("First line\nSecond line");
    expect(std::strcmp(log.line[0], "Second line") == 0 && std::strcmp(log.line[1], "First line") == 0 && std::strcmp(log.line[2], "Obtained Sword.") == 0,
           "'\\n' splits into two log lines (remainder printed last = newest)");
    ga.message(0x261, "Bone");   // "Obtained %s." through FUN_00114da0
    expect(std::strstr(log.line[0], "Bone") != nullptr, std::string("localized message 0x261 -> \"") + log.line[0] + "\"");
    tick();
    expect(log.reveal == 4 && log.timer[0] == 0x59, "typewriter +4 chars, timer -1 per tick");
    expect(ui::text().glyphCount() > 0, "log glyph sprites emitted (" + std::to_string(ui::text().glyphCount()) + ")");
    tick(0x58);
    expect(log.timer[0] == 1, "line 0 timer near the end of its fade-out");
    tick();
    expect(log.timer[0] == 0 && log.line[0][0] == 0, "log cleared when line 0 expires (FUN_0015d0a0)");
    expect(near(log.x, 24.0f) && near(log.y, 290.0f), "log origin (24, 290)");

    // ---- 4. capture ----------------------------------------------------------------------------------------------
    // state for the picture: HP 25/120-ish, VT 48/98, depth -100, two log lines, level 2
    log.print("Obtained %s.", "Sword");
    log.print("%s has evolved.", "Sword");
    tick(12);
    const fs::path outDir = fs::path(BAROQUE_WORK_ROOT) / "native_captures";
    fs::create_directories(outDir);
    const fs::path out = outDir / "hud_status.png";
    bool saved = false;
    if (rendererOk) {
        RenderTexture2D rt = LoadRenderTexture(1440, 1080);
        BeginTextureMode(rt);
        ClearBackground(BLACK);
        renderer2d().drawSprites(1440, 1080);
        EndTextureMode();
        Image img = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&img);
        saved = ExportImage(img, out.string().c_str());
        // the status frame region (0..160, 0..100 of 512x448) must contain drawn pixels, the centre of the screen not
        int lit = 0, litCentre = 0;
        for (int y = 0; y < 100; y += 2)
            for (int x = 0; x < 160; x += 2) {
                const Color c = GetImageColor(img, static_cast<int>(static_cast<float>(x) * 2.8125f), static_cast<int>(static_cast<float>(y) * 2.4107f));
                if (c.r + c.g + c.b > 0) ++lit;
            }
        for (int y = 150; y < 250; y += 2)
            for (int x = 200; x < 300; x += 2) {
                const Color c = GetImageColor(img, static_cast<int>(static_cast<float>(x) * 2.8125f), static_cast<int>(static_cast<float>(y) * 2.4107f));
                if (c.r + c.g + c.b > 0) ++litCentre;
            }
        expect(lit > 400 && litCentre == 0, "capture: status frame drawn top-left (" + std::to_string(lit) + " lit samples), centre empty");
        UnloadImage(img);
        UnloadRenderTexture(rt);
    }
    expect(saved, "capture written to " + out.string());

    std::printf("items check: %d passed, %d failed; UI layer sprites %d (HUD %d, transient %d); items %d; capture %s\n", gPass, gFail,
                ui::sprites().liveCount(), statusHud().spriteCount(), uiSprite2D().liveCount(), ga.itemObjectCount(), saved ? out.string().c_str() : "-");
    hudGameExit(false);
    renderer2d().shutdown();
    CloseWindow();
    return gFail == 0 ? 0 : 1;
}
