// Installs the services behind the player controller's hooks (player_control.hpp) in the full game build: floor id,
// fade, room table, floor map, inventory / item screen (hud_api), weapon class, AI query (ITEMS_HUD_NATIVE.md §6).
// Also installs the item-system services of the character core (CombatHooks weapon class, ThrowHooks).
#include "audio/sound_system.hpp"
#include "game/character/combat.hpp"
#include "game/character/player_control.hpp"
#include "game/talk/talk_manager.hpp"

#include "game/character/act_effects.hpp"
#include "game/dungeon.hpp"
#include "game/effects/effects.hpp"
#include "game/event/event_manager.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item_world.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"

namespace baroque::game {

namespace {

bool install() {
    PlayerControlServices& s = playerControlServices();
    s.floorId = [] { return hudWorld().floorId; };                    // iRam0050a2a8 (refreshed from the Dungeon each tick)
    s.fadeFramesLeft = [] { return fade().frames; };                  // iRam004e7898
    s.roomKind = [](int i) -> char {                                  // **(0x50a640[i] + 4): RDT byte 0
        const Dungeon* d = gameWorld().dungeon;
        if (d == nullptr || i < 0 || i >= d->roomCount()) return 0;
        return static_cast<char>(d->room(i).rdt.roomId);
    };
    // orig FUN_0011e6c0 / FUN_0011e740: FUN_0011c0a0 -> (float)(room +0xc / +0xe * 0x640 + 100), else 0
    s.roomOriginX = [](int i) -> float {
        const Dungeon* d = gameWorld().dungeon;
        if (d == nullptr || !isGeneratedFloor(d->floorId()) || i < 0 || i >= 16) return 0.0f;
        return static_cast<float>(static_cast<int>(d->room(i).posX) * 0x640 + 100);
    };
    s.roomOriginZ = [](int i) -> float {
        const Dungeon* d = gameWorld().dungeon;
        if (d == nullptr || !isGeneratedFloor(d->floorId()) || i < 0 || i >= 16) return 0.0f;
        return static_cast<float>(static_cast<int>(d->room(i).posY) * 0x640 + 100);
    };
    s.floorMapShown = [] { return hudMapShown(); };
    s.floorMapShow = [] { hudMapShow(); };
    s.floorMapHide = [] { hudMapHide(); };
    s.floorMapCorner = [] { return map2d().corner(); };
    s.floorMapSetCorner = [](bool on) { map2d().setCorner(on); };
    s.inventoryTick = [] { hudPlayerTick(); };
    s.inventoryCount = [] { return hudInventoryCount(); };
    s.inventoryItem = [](int slot) -> const void* { return hudInventoryItem(slot); };
    s.weaponClass = [] { return inventory().weaponClass(itemSystem()); };
    s.gunHasAmmo = [] {
        const Item* w = inventory().equip[0];                        // iRam0053d9b4
        return w != nullptr && w->count != 0;                         // +0x6c
    };
    s.equipKindBonus = [](int base, int kind) { return equipKindBonusOf(base, kind); };
    s.itemThrowCheck = [](int slot) { return inventory().equippedSlotOfIndex(slot); };   // FUN_001d7c80 -> FUN_001d7c90
    s.itemSlotSelected = [] { return hudItemSlotSelected(); };
    s.setItemSlotSelected = [](int slot) { hudSetItemSlotSelected(slot); };
    s.itemSlotStep = [](bool next) { hudItemSlotStep(next); };
    s.eventState = [] { return eventActive() ? 1 : 0; };              // iRam0051b620
    s.startFloorEvent = [](int n) { eventTrigger(n); };                // FUN_001d1d30
    s.eventCameraAxes = [](float fwd[3], float right[3]) { return talkCameraAxes(fwd, right); };   // FUN_001a1d90 / 1d80
    s.aiQuery = [](const Chr& c, int key) { return static_cast<int>(aiVar(const_cast<Ai&>(c.ai), static_cast<uint32_t>(key))); };
    // FUN_0013a150 (items module aimArc). Its statics DAT_0044b118 (strip phase) / DAT_0044b110 (angle) are game-wide
    // data words, both 1.0f (0x3f800000) in the ELF and never reset.
    s.aimTrajectory = [](const void* item, const Vec3f& start, const Vec3f& dir, const Vec3f& side, float verts[60][3],
                         uint32_t colours[60]) {
        static float phase = 1.0f;   // DAT_0044b118
        static float angle = 1.0f;   // DAT_0044b110
        Vec3 v[60];
        aimArc(*static_cast<Item*>(const_cast<void*>(item)), Vec3{start[0], start[1], start[2]},
               Vec3{dir[0], dir[1], dir[2]}, Vec3{side[0], side[1], side[2]}, itemSystem(), v, colours, phase, angle);
        for (int i = 0; i < 60; ++i) {
            verts[i][0] = v[i].x;
            verts[i][1] = v[i].y;
            verts[i][2] = v[i].z;
        }
    };

    // character core: FUN_001d7d10(0x53d910) for the reaction / death action suffixes (FUN_0013a4b0)
    CombatHooks& ch = combatHooks();
    ch.weaponClass = [] { return inventory().weaponClass(itemSystem()); };
    // FUN_00146f90 equipment part. FUN_001d4c50(item, id): item id == id, or its +0x38 sub-item's id == id.
    ch.attackRateItems = [](const StatusBlock& b, AttackRateInputs& in) {
        constexpr uint32_t kSD13 = itemId("SD13");                    // 0x44b778
        constexpr uint32_t kWM16 = itemId("WM16");                    // 0x44b780
        auto has = [](const Item* it) { return it->id == kWM16 || (it->parasite != nullptr && it->parasite->id == kWM16); };
        auto pair = [&](const Item* p) {                              // test p, then p +0x38 when p passed
            if (p == nullptr || !has(p)) return;
            ++in.wm16Hits;
            if (p->parasite != nullptr && has(p->parasite)) ++in.wm16Hits;
        };
        const Inventory& inv = inventory();
        if (const Item* w = inv.equip[0]) {                           // iRam0053d9b4
            in.weaponSd13 = w->id == kSD13;
            pair(w->parasite);                                        // weapon +0x38
        }
        if (const Item* w2 = inv.equip[1]) pair(w2->parasite);        // iRam0053d9b8 +0x38
        pair(static_cast<const Item*>(b.parasite));                   // status block +0x5c
    };
    // Equipment effect records FUN_001d2f10(FUN_001d2e80(item), i) of the four equipment words 0x53d9b4.. in slot order
    // (items module: equipmentEffectRecords), record bytes +0 type, +1 category, +2 value, +4 extra, +6 chance.
    ch.equipEffects = [](std::vector<EquipEffect>& out) {
        std::vector<EquipEffectRecord> recs;
        equipmentEffectRecords(recs);
        const Inventory& inv = inventory();
        out.clear();
        int n = 0;
        for (const EquipEffectRecord& r : recs) {
            if (r.rec == nullptr) {
                ++n;
                continue;
            }
            EquipEffect e;
            e.type = r.rec->kind;
            e.category = r.rec->type;
            e.value = r.rec->value;
            e.extra = r.rec->x;
            e.chance = static_cast<uint16_t>(r.rec->count);
            e.item = r.item;
            e.itemTag = r.item != nullptr ? r.item->id : 0u;
            for (int k = 0; k < 4; ++k)
                if (inv.equip[k] == r.item) e.slot = static_cast<uint8_t>(k);
            e.rec = static_cast<uint8_t>(n & 1);   // records 0 then 1 of each item
            out.push_back(e);
            ++n;
        }
    };
    // FUN_001d5030(item, amount) inside FUN_00135f00 / FUN_001351b0: a 'CT12' (0x44b078) whose DEF drops below 1 prints
    // message 0x259 with the item name (FUN_001d3ba0)
    ch.wearItem = [](void* p, int amount) {
        if (p == nullptr) return;
        Item& it = *static_cast<Item*>(p);
        const int before = it.defBase + it.defMod;   // +0x58 + +0x64
        addDefMod(it, amount);
        if (it.id == itemId("CT12") && it.defBase + it.defMod < 1 && 0 < before)
            gameAdapter().message(0x259, itemDisplayName(it, itemSystem()));
    };
    // FUN_001d4f70(item, amount) inside FUN_00136290 (type 0x18): SE 0x91 at the player when it returned 1 for a
    // negative amount; an 'SD17' (0x44b098) whose ATK drops below 1 prints message 0x259
    ch.wearAtkItem = [](void* p, int amount) {
        if (p == nullptr) return;
        Item& it = *static_cast<Item*>(p);
        const int before = it.atkBase + it.atkMod;   // +0x54 + +0x60
        if (addAtkMod(it, amount) && static_cast<int16_t>(amount) < 0) {
            if (Chr* pl = characters().player()) sound().playSe3d(1.0f, 0x91, pl->xf.m.pos.data());
        }
        if (it.id == itemId("SD17") && it.atkBase + it.atkMod < 1 && 0 < before)
            gameAdapter().message(0x259, itemDisplayName(it, itemSystem()));
    };
    ch.playerCritBonus = [] { return inventory().wg03Total(itemSystem()); };                      // FUN_001d8720
    ch.heldElementMask = [] {                                                                    // FUN_001d50f0(iRam0053d9bc)
        const Item* coat = inventory().equip[2];
        return coat != nullptr ? itemElementMask(itemSystem(), *coat) : 0u;
    };
    ch.playerImmunities = [] { return playerEquipmentImmunities(); };                           // FUN_00136e50
    ch.equippedItem = [](uint32_t tag) -> const void* {                                         // FUN_001d7cc0
        const Inventory& inv = inventory();
        for (int k = 0; k < 4; ++k)
            if (inv.equip[k] != nullptr && inv.equip[k]->id == tag) return inv.equip[k];
        return nullptr;
    };
    ch.itemChance = [](const void* p) {                                                         // FUN_001d2f10(.., 0) +6
        const data::PrmItem* row = itemSystem().row(*static_cast<const Item*>(p));
        return row != nullptr ? static_cast<int>(row->effects[0].count) : 0;
    };
    ch.inventoryHas = [](uint32_t tag) { return inventory().hasId(tag); };                       // FUN_001d92d0
    // *(*(0x53d87c) +0x44): PRMITEM ST table (FUN_001d2c90 case 7, stride 0x80), word +0x78 of row n
    ch.brandValue = [](int n) { return static_cast<int>(itemSystem().word(kClsST, n, 0x78)); };
    ch.brandSt01Value = [] { return static_cast<int>(itemSystem().word(kClsST, 1, 0x78)); };      // +0xf8
    ch.itemScreenOpen = [] { return hudItemScreenOpen(); };                                       // iRam00517b68
    ch.canWarp = [](const StatusObj& st) { return statusCanWarp(st); };                          // FUN_00138ad0
    ch.bx24Tick = [] { inventory().bx24Tick(); };                                                 // FUN_001d9560
    ch.parasiteElementMask = [] {                                                                 // FUN_0014b170 head
        Chr* p = characters().player();
        const Item* par = p != nullptr ? static_cast<const Item*>(p->status.block.parasite) : nullptr;
        uint32_t m = 0;
        if (par != nullptr) {
            m |= static_cast<uint32_t>(itemSystem().word(kClsWM, par->idx, 0x70));
            if (par->parasite != nullptr) m |= static_cast<uint32_t>(itemSystem().word(kClsWM, par->parasite->idx, 0x70));
        }
        return m;
    };
    ch.swordElementMask = [](const void* p) {                                                     // FUN_001d50f0
        const Item* it = static_cast<const Item*>(p);
        return it != nullptr && it->cls == kClsSD ? itemElementMask(itemSystem(), *it) : 0u;
    };
    // FUN_001d4ee0 (work/decompiled/001d4ee0.c; 0x464280 "SD23"): +0x68 += amount; >= 0x97 -> -= 0x96 and
    // FUN_001d4f70(item, 1), returns 1
    ch.evolveItem = [](void* p, int amount) {
        Item& it = *static_cast<Item*>(p);
        if (!(it.atkBase + it.atkMod < 99)) return;                  // +0x54 + +0x60 < 99 (FUN_00136580)
        if (it.id != itemId("SD23")) return;
        it.unused68 = it.unused68 + amount;
        if (it.unused68 < 0x97) return;
        it.unused68 = it.unused68 - 0x96;
        addAtkMod(it, 1);
        gameAdapter().message(0x252, itemDisplayName(it, itemSystem()));
    };
    // status visuals FUN_00137d40 / FUN_00137f40
    ch.effectStop = [](int id) { effects().stop(id); };                                           // FUN_00112560
    ch.effectMove = [](int id, const Vec3f& pos) { effects().setPosition(id, pos.data()); };      // FUN_001122d0
    ch.seOverride = [](bool on) {                                                                 // FUN_001161a0 / 1b0
        if (auto* m = dynamic_cast<audio::GameSoundManager*>(&sound())) {
            if (on) m->system().seOverrideBegin();
            else m->system().seOverrideEnd();
        }
    };
    // ThrowLock: item +0x24 class, FUN_001d5640 throw
    ThrowHooks& th = throwHooks();
    th.itemClass = [](const void* item) { return static_cast<const Item*>(item)->cls; };

    // ACT sound / effect commands (act_runner.cpp)
    ActRunnerHooks& ah = actRunnerHooks();
    ah.se3d = [](float vol, int id, const Vec3f& pos) { sound().playSe3d(vol, id, pos.data()); };   // FUN_00115470
    ah.se3dHandle = [](float vol, int id, const Vec3f& pos) { return sound().playSe3d(vol, id, pos.data()); };   // FUN_00115470
    ah.seStop = [](uint32_t h, int time) { sound().stopSe(h, time); };                                          // FUN_00115820
    // UNVERIFIED: FUN_00115970 (voice with the "nobody else talking" test of the talk system) -> FUN_00115ce0 shape
    ah.voice = [](int id) { sound().playVoice(1.0f, id); };
    ah.bgm = [](int id) { sound().playBgm(id, 0, 1); };                                             // FUN_00115d70(snd, id, 0, 1)
    ah.effect = [](int fx, const Vec3f& pos, const Vec3f* rot, float scale) {                       // FUN_00112190
        if (combatHooks().effect) combatHooks().effect(fx, pos, rot, scale);
    };
    ah.effectCommand = [](ActRunner& r, const data::ActCmnd& c) { actEffectCommand(r, c); };      // Exi_a .. ExiEnd
    ah.effectReset = [](ActRunner& r) { actEffectReset(r); };                                     // FUN_00144a70
    ah.event = [](int n) { eventTrigger(n); };                                                    // FUN_00149b70
    // Pad latch / PadEnd (FUN_00143fe0 / FUN_00147bf0)
    ah.padPressed = [] { return pad().port[0].pressed; };                                         // uRam004e6288
    ah.buttonSlot = [](int slot) { return buttons().slot[static_cast<size_t>(slot) & 7u]; };      // *(0x4e65c0 + slot*8)
    ah.playerControl = [] { return playerCtl(); };                                                // 0x50b620
    ah.counterAdd = [](int id, int d) { counters().add(id, d); };                                 // FUN_00179570(0x53c5c0, ..)

    // FUN_00112190(0, fx, pos, rot, (s, s, s, 1)) for the character module's effects (hit FX, death FX, hazards).
    // UNVERIFIED: FUN_0014b840 passes the tag 30 (-30 for id 2) in f12; CombatHooks::effect carries no tag (0 here).
    ch.effect = [](int fx, const Vec3f& pos, const Vec3f* rot, float scale) {
        const float p4[4] = {pos[0], pos[1], pos[2], 0.0f};
        const float r4[4] = {rot != nullptr ? (*rot)[0] : 0.0f, rot != nullptr ? (*rot)[1] : 0.0f, rot != nullptr ? (*rot)[2] : 0.0f, 1.0f};
        const float s4[4] = {scale, scale, scale, 1.0f};
        effects().spawn(0.0f, fx, p4, rot != nullptr ? r4 : nullptr, s4);
    };

    return true;
}

const bool gInstalled = install();

}  // namespace

// Hooks living in objects constructed by other translation units' static init (eventHooks() is a namespace-scope
// object of the event module): installed at run time, when the first player character is created.
void installCharacterRuntimeHooks() {
    // FUN_001d1d30 Event state request: (+0x14 == 0 && +0x18 == 0) || +0xc < 3 -> LAB_001d1de0, arg player, prio 2
    eventHooks().playerEventState = [](Chr& c) { c.sm.request(StateId::Event, 0, 2); };
    eventHooks().playerStatusVisuals = [](int i, bool on) {                                       // FUN_00137d40 / 37f40
        if (Chr* p = characters().player()) statusVisual(p->status, i, on);
    };
}

}  // namespace baroque::game
