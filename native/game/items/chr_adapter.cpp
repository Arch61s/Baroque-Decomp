#include "game/items/chr_adapter.hpp"
#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item_links.hpp"
#include "game/sound_stub.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

namespace baroque::game {

namespace {
ChrAdapterEnv gEnv;
std::map<const Chr*, std::unique_ptr<ChrAdapter>> gAdapters;
ChrAdapter gNullAdapter;
constexpr uint32_t kIdSD19 = itemId("SD19");   // 0x44b068 [V]
constexpr uint32_t kIdST19 = itemId("ST19");   // 0x44b090 [V]

bool isPlayerObj(const StatusObj& s) { return s.owner != nullptr && s.owner == characters().player(); }

// orig FUN_00134310 (work/decompiled/00134310.c): equipment PRM effect slots {kind, type == 1, .., s16 +6} add to
// the base value; kind 1 = HP max (FUN_001343f0), 2 = VT max (FUN_00134470) [V disasm a1]
int equipmentStatBonus(int base, int kind) {
    std::vector<EquipEffectRecord> recs;
    equipmentEffectRecords(recs);
    for (const EquipEffectRecord& r : recs)
        if (r.rec->type == 1 && static_cast<int>(r.rec->kind) == kind) base = base + r.rec->count;
    return base;
}
}  // namespace

// FUN_001d2e80(item +0x24, item +0x28) -> FUN_001d2f10(row, i) for the equipment words 0x53d9b4..0x53d9c0 in slot
// order, record 0 then 1. FUN_001d2e80 resolves SD (stride 0xa0) / CT (0x8c) / WG (0x88) rows only; any other class
// (and an empty slot) gives a null record, which every caller skips.
void equipmentEffectRecords(std::vector<EquipEffectRecord>& out) {
    out.clear();
    const Inventory* inv = gEnv.inventory;
    const ItemSystem* sys = gEnv.items;
    if (inv == nullptr || sys == nullptr) return;
    for (int slot = 0; slot < 4; ++slot) {
        Item* it = inv->equip[slot];
        if (it == nullptr || it->cls < 0 || 2 < it->cls) continue;
        const data::PrmItem* row = sys->row(it->cls, it->idx);
        if (row == nullptr) continue;
        for (int i = 0; i < 2; ++i) out.push_back(EquipEffectRecord{&row->effects[static_cast<size_t>(i)], it});
    }
}

int equipKindBonusOf(int base, int kind) { return equipmentStatBonus(base, kind); }   // orig FUN_00134310

namespace {
// orig FUN_00136de0 (work/decompiled/00136de0.c): WM row index 9..0xf -> status bit
uint32_t parasiteImmunity(int idx) {
    switch (idx) {
    case 9: return 1;
    case 10: return 2;
    case 0xb: return 4;
    case 0xc: return 8;
    case 0xd: return 0x40;
    case 0xe: return 0x10;
    case 0xf: return 0x20;
    default: return 0;
    }
}
}  // namespace

// orig FUN_00136e50 (work/decompiled/00136e50.c; the parasite chains are read through a1, which FUN_00136de0 keeps
// [V 0x136e90..0x136ebc, 0x136fb0..0x136fdc]): the player's parasite (status +0x5c) and its +0x38, the +0x38 parasite
// chain of every equipped CT item, and the records with byte +1 == 5 of the equipped items "CT16" 0x3d, "WG06" 1,
// "WG07" 0x12, "WG08" 5, "WG09" 0x18, "WG10" 0x1f (0x44b0a8.. [V]).
uint32_t playerEquipmentImmunities() {
    uint32_t m = 0;
    const Chr* pl = characters().player();
    if (pl != nullptr) {
        if (const Item* p = static_cast<const Item*>(pl->status.block.parasite)) {
            m = parasiteImmunity(p->idx);
            if (p->parasite != nullptr) m |= parasiteImmunity(p->parasite->idx);
        }
    }
    const Inventory* inv = gEnv.inventory;
    const ItemSystem* sys = gEnv.items;
    if (inv == nullptr || sys == nullptr) return m;
    for (int slot = 0; slot < 4; ++slot) {
        const Item* it = inv->equip[slot];
        if (it == nullptr) continue;
        if (it->cls == 1 && it->parasite != nullptr) {
            m |= parasiteImmunity(it->parasite->idx);
            if (it->parasite->parasite != nullptr) m |= parasiteImmunity(it->parasite->parasite->idx);
        }
        if (it->cls < 0 || 2 < it->cls) continue;
        const data::PrmItem* row = sys->row(it->cls, it->idx);
        if (row == nullptr) continue;
        for (int i = 0; i < 2; ++i) {
            if (row->effects[static_cast<size_t>(i)].type != 5) continue;
            const uint32_t id = it->id;
            if (id == itemId("CT16")) m |= 0x3d;
            else if (id == itemId("WG06")) m |= 1;
            else if (id == itemId("WG07")) m |= 0x12;
            else if (id == itemId("WG08")) m |= 5;
            else if (id == itemId("WG09")) m |= 0x18;
            else if (id == itemId("WG10")) m |= 0x1f;
        }
    }
    return m;
}

ChrAdapterEnv& chrAdapterEnv() { return gEnv; }

ChrAdapter& chrAdapter(Chr* c) {
    if (c == nullptr) return gNullAdapter;
    auto& slot = gAdapters[c];
    if (!slot) slot = std::make_unique<ChrAdapter>(c);
    return *slot;
}
void chrAdaptersReset() {
    gAdapters.clear();
    linkPlayerModelsReset();   // the equipment model parts died with the characters
}

// ---- status routines -------------------------------------------------------------------------------------

// orig FUN_001343f0 (work/decompiled/001343f0.c)
int statusHpMaxWithItems(const StatusObj& s) {
    if (isPlayerObj(s)) {
        int v = equipmentStatBonus(s.block.hpMax, 1);
        v = (gEnv.inventory != nullptr && gEnv.items != nullptr ? gEnv.inventory->hpMaxBonus(const_cast<ItemSystem&>(*gEnv.items)) : 0) + v;   // FUN_001d8290
        if (0x3e6 < v) v = 999;
        return v;
    }
    return s.block.hpMax;
}

// orig FUN_00134470 (work/decompiled/00134470.c)
int statusVtMaxWithItems(const StatusObj& s) {
    if (isPlayerObj(s)) {
        int v = equipmentStatBonus(s.block.vtMax, 2);
        v = (gEnv.inventory != nullptr && gEnv.items != nullptr ? gEnv.inventory->vtMaxBonus(const_cast<ItemSystem&>(*gEnv.items)) : 0) + v;   // FUN_001d8330
        if (0x3e6 < v) v = 999;
        return v;
    }
    return s.block.vtMax;
}

// orig FUN_001349e0 (work/decompiled/001349e0.c) with FUN_00134960 (clamp to FUN_001343f0)
bool statusAddHpHud(StatusObj& s, int v) {
    StatusBlock& b = s.block;
    b.hp = b.hp + v;
    const bool alive = 0 < b.hp;
    if (alive) {
        const int mx = statusHpMaxWithItems(s);
        if (mx < b.hp) b.hp = mx;
    } else {
        b.hp = 0;
    }
    if (isPlayerObj(s)) {
        if (gEnv.hud != nullptr) gEnv.hud->setHp(b.hp);   // FUN_00151ae0(0x5179c0, hp)
        flags().clear(0x401);
        flags().clear(0x2fb);
        flags().clear(0x2fc);
        if (b.hp < 0xb) flags().set(0x401);
        else if (b.hp < 0x15) flags().set(0x2fb);
        else if (b.hp < 0x1f) flags().set(0x2fc);
    }
    return alive;
}

// orig FUN_00134b60 (work/decompiled/00134b60.c) with FUN_001349a0 (clamp to FUN_00134470)
void statusAddVtHud(StatusObj& s, int v) {
    StatusBlock& b = s.block;
    b.vt = b.vt + v;
    if (b.vt < 0) {
        b.vt = 0;
    } else {
        const int mx = statusVtMaxWithItems(s);
        if (mx < b.vt) b.vt = mx;
    }
    if (isPlayerObj(s)) {
        if (gEnv.hud != nullptr) gEnv.hud->setVt(b.vt);   // uRam00517b2c = vt
        flags().clear(0x2f8);
        flags().clear(0x402);
        flags().clear(0x2fa);
        flags().clear(0x2f9);
        if (b.vt == 0) flags().set(0x2f8);
        else if (b.vt < 0xb) flags().set(0x402);
        else if (b.vt < 0x10) flags().set(0x2fa);
        else if (b.vt < 0x1f) flags().set(0x2f9);
        // uRam0050b1d8 = 0 (player-control global, not part of this module)
    }
}

// orig FUN_00134920 (work/decompiled/00134920.c)
void statusRecompute(StatusObj& s) {
    statusAddHpHud(s, 0);
    statusAddVtHud(s, 0);
}

// ---- CharacterAccess -------------------------------------------------------------------------------------

bool ChrAdapter::isPlayer() const { return chr_ != nullptr && chr_ == characters().player(); }
int ChrAdapter::actorType() const { return chr_ ? chr_->type : -1; }
uint32_t ChrAdapter::charId() const { return chr_ ? chr_->id : 0; }
bool ChrAdapter::isRemoved() const { return chr_ == nullptr || (chr_->taskFlags & 0x10000000u) != 0; }
bool ChrAdapter::isAlive() const { return chr_ != nullptr && chr_->status.block.hp != 0; }
Vec3 ChrAdapter::position() const {
    if (!chr_) return {};
    const auto& p = chr_->xf.m.pos;
    return {p[0], p[1], p[2]};
}
void ChrAdapter::setPosition(const Vec3& p) {
    if (!chr_) return;
    chr_->xf.m.pos = {p.x, p.y, p.z};
}
Vec3 ChrAdapter::forward() const {
    if (!chr_) return {0, 0, 1};
    const auto& f = chr_->xf.m.fwd;
    return {f[0], f[1], f[2]};
}
float ChrAdapter::height() const { return chr_ && chr_->status.prm ? chr_->status.prm->height : 0.0f; }          // PRMCHAR +0xa0
float ChrAdapter::iconHeight() const { return chr_ && chr_->status.prm ? chr_->status.prm->iconHeight : 0.0f; }  // PRMCHAR +0xac
float ChrAdapter::radius() const { return chr_ ? chr_->shape.radius : 0.0f; }       // physics +0x5c -> +4
float ChrAdapter::bodyHeight() const { return chr_ ? chr_->shape.height : 0.0f; }   // physics +0x5c -> +8
int ChrAdapter::roomIndex() const {
    if (!chr_ || chr_->cell == nullptr) return -1;
    const uint32_t w = *chr_->cell;
    return (w & 0xf000u) == 0x1000u ? static_cast<int>(w & 0xfu) : -1;
}
bool ChrAdapter::hasCell() const { return chr_ != nullptr && chr_->cell != nullptr; }
bool ChrAdapter::jointPosition(const char* name, Vec3& out) const { return chr_ != nullptr && linkJointPos(*chr_, name, out); }
bool ChrAdapter::jointMatrix(const char* name, float out[16]) const { return chr_ != nullptr && linkJointMatrix(*chr_, name, out); }

int ChrAdapter::hp() const { return chr_ ? chr_->status.block.hp : 0; }
int ChrAdapter::hpMaxBase() const { return chr_ ? chr_->status.block.hpMax : 0; }
int ChrAdapter::hpMaxTotal() const { return chr_ ? statusHpMaxWithItems(chr_->status) : 0; }
int ChrAdapter::vt() const { return chr_ ? chr_->status.block.vt : 0; }
int ChrAdapter::vtMaxBase() const { return chr_ ? chr_->status.block.vtMax : 0; }
int ChrAdapter::vtMaxTotal() const { return chr_ ? statusVtMaxWithItems(chr_->status) : 0; }
int ChrAdapter::atkMax() const { return chr_ ? chr_->status.block.atkBase : 0; }    // +0x10
int ChrAdapter::atkCur() const { return chr_ ? chr_->status.block.atkMax : 0; }     // +0x14
int ChrAdapter::defMax() const { return chr_ ? chr_->status.block.defBase : 0; }    // +0x1c
int ChrAdapter::defCur() const { return chr_ ? chr_->status.block.defMax : 0; }     // +0x20
uint32_t ChrAdapter::statusMask() const { return chr_ ? chr_->status.block.statusMask : 0; }
int ChrAdapter::level() const { return chr_ ? chr_->status.block.level : 0; }
int ChrAdapter::exp() const { return chr_ ? chr_->status.block.exp : 0; }
int ChrAdapter::expNext() const { return chr_ ? chr_->status.block.expNext : 0; }
uint32_t ChrAdapter::brand() const { return chr_ ? chr_->status.block.brand : 0; }
int ChrAdapter::hiddenStat(int i) const { return chr_ && i >= 0 && i < 5 ? chr_->status.block.hidden[static_cast<size_t>(i)] : 0; }
int ChrAdapter::atkLevelBonus() const { return chr_ ? chr_->status.block.atkLevel : 0; }   // +0x18

// orig FUN_001344f0(status, -1) (work/decompiled/001344f0.c)
int ChrAdapter::attackPower() const {
    if (!chr_) return 0;
    const StatusBlock& b = chr_->status.block;
    if (!isPlayer()) return b.atkMax;   // +0x14
    if (gEnv.inventory == nullptr || gEnv.items == nullptr) return b.atkMax + b.atkLevel;
    ItemSystem& sys = const_cast<ItemSystem&>(*gEnv.items);
    int v = gEnv.inventory->atkTotal(sys) + b.atkMax + b.atkLevel;   // FUN_001d83d0 + status +0x14 + +0x18
    const Item* w = gEnv.inventory->equip[0];                        // iRam0053d9b4
    if (w != nullptr && w->id == kIdSD19) {
        const data::PrmItem* row = (w->cls >= 0 && w->cls <= 2) ? sys.row(w->cls, w->idx) : nullptr;
        if (row != nullptr) {
            const int hpMaxT = statusHpMaxWithItems(chr_->status);
            const data::PrmItemEffect& s1 = row->effects[1];
            if (static_cast<float>(b.hp) <= (static_cast<float>(static_cast<int>(s1.x)) / 100.0f) * static_cast<float>(hpMaxT)) {
                v = static_cast<int>(static_cast<float>(v) * (static_cast<float>(static_cast<int>(s1.count)) / 100.0f));
            } else {
                const data::PrmItemEffect& s0 = row->effects[0];
                if (static_cast<float>(b.hp) <= (static_cast<float>(static_cast<int>(s0.x)) / 100.0f) * static_cast<float>(hpMaxT))
                    v = static_cast<int>(static_cast<float>(v) * (static_cast<float>(static_cast<int>(s0.count)) / 100.0f));
            }
        }
    }
    return v;
}

// status object +0x10 & 1: zeroed by the ctor FUN_001336c0, set / cleared by ACT GetItem (FUN_0014a0b0, act_props.cpp)
// and cleared by every ACT reset (FUN_00143bf0); FUN_001d9040 is the reader.
bool ChrAdapter::refusesItems() const { return chr_ != nullptr && (chr_->status.flags10 & 1u) != 0; }
Item* ChrAdapter::parasite() const { return chr_ ? static_cast<Item*>(chr_->status.block.parasite) : nullptr; }   // +0x5c
Item* ChrAdapter::heldItem() const { return chr_ ? static_cast<Item*>(chr_->status.block.carried) : nullptr; }    // +0x60

bool ChrAdapter::addHp(int d) { return chr_ ? statusAddHpHud(chr_->status, d) : false; }
void ChrAdapter::addVt(int d) { if (chr_) statusAddVtHud(chr_->status, d); }

// orig FUN_00134b10 (work/decompiled/00134b10.c)
void ChrAdapter::addHpMax(int d) {
    if (!chr_) return;
    StatusBlock& b = chr_->status.block;
    b.hpMax = b.hpMax + d;
    if (b.hpMax < 1000) {
        if (b.hpMax < 0) b.hpMax = 0;
    } else {
        b.hpMax = 999;
    }
    const int mx = statusHpMaxWithItems(chr_->status);   // FUN_00134960
    if (mx < b.hp) b.hp = mx;
}

// orig FUN_00134cb0 (work/decompiled/00134cb0.c)
void ChrAdapter::addVtMax(int d) {
    if (!chr_) return;
    StatusBlock& b = chr_->status.block;
    b.vtMax = b.vtMax + d;
    if (b.vtMax < 1000) {
        if (b.vtMax < 0) b.vtMax = 0;
    } else {
        b.vtMax = 999;
    }
    const int mx = statusVtMaxWithItems(chr_->status);   // FUN_001349a0
    if (mx < b.vt) b.vt = mx;
    if (isPlayer() && 499 < b.vtMax) flags().set(0x32d);
}

// orig FUN_00134d60 (work/decompiled/00134d60.c): +0x14 += d, clamped to [0, +0x10]
void ChrAdapter::addAtk(int d) {
    if (!chr_) return;
    StatusBlock& b = chr_->status.block;
    b.atkMax = b.atkMax + d;
    if (b.atkBase < b.atkMax) b.atkMax = b.atkBase;
    else if (b.atkMax < 0) b.atkMax = 0;
}

// orig FUN_00134db0 (work/decompiled/00134db0.c): +0x10 += d clamped [0, 999]; +0x14 clamped to it
void ChrAdapter::addAtkMax(int d) {
    if (!chr_) return;
    StatusBlock& b = chr_->status.block;
    b.atkBase = b.atkBase + d;
    if (b.atkBase < 1000) {
        if (b.atkBase < 0) b.atkBase = 0;
    } else {
        b.atkBase = 999;
    }
    if (b.atkBase < b.atkMax) b.atkMax = b.atkBase;
}

// orig FUN_00134e20 (work/decompiled/00134e20.c): +0x20 += d, clamped to [0, +0x1c]
void ChrAdapter::addDef(int d) {
    if (!chr_) return;
    StatusBlock& b = chr_->status.block;
    b.defMax = b.defMax + d;
    if (b.defBase < b.defMax) b.defMax = b.defBase;
    else if (b.defMax < 0) b.defMax = 0;
}

// orig FUN_00134e70 (work/decompiled/00134e70.c)
void ChrAdapter::addDefMax(int d) {
    if (!chr_) return;
    StatusBlock& b = chr_->status.block;
    b.defBase = b.defBase + d;
    if (b.defBase < 1000) {
        if (b.defBase < 0) b.defBase = 0;
    } else {
        b.defBase = 999;
    }
    if (b.defBase < b.defMax) b.defMax = b.defBase;
}

// FUN_00138660 / FUN_00138970 / FUN_00138ac0 / FUN_00138e40: the character status module (game/character/combat.cpp)
uint32_t ChrAdapter::inflict(uint32_t mask) { return chr_ ? linkInflictStatus(chr_->status, mask) : 0; }
uint32_t ChrAdapter::cure(uint32_t mask) { return chr_ ? linkStatusExpire(chr_->status, mask) : 0; }
void ChrAdapter::cureAll() { if (chr_) linkCureAllStatus(chr_->status); }
// orig FUN_00134ee0: FUN_00138660(status, 0x80)
void ChrAdapter::hpVtMaxShift() { inflict(0x80); }
// orig FUN_00139060 (work/decompiled/00139060.c): FUN_00138e40(status, +0x50 - +0x4c)
void ChrAdapter::levelUp() {
    if (!chr_) return;
    const StatusBlock& b = chr_->status.block;
    linkStatusAddExp(chr_->status, b.expNext - b.exp);
}
// orig FUN_00139080 (work/decompiled/00139080.c; FUN_00138dc0 receives the level before the decrement in a1 [V])
void ChrAdapter::levelDown() {
    if (!chr_) return;
    StatusBlock& b = chr_->status.block;
    if (b.level < 2) return;
    b.expNext = linkExpForLevel(b.level);
    b.exp = b.expNext + -1;
    b.atkLevel = b.atkLevel + -1;
    b.defLevel = b.defLevel + -1;
    b.level = b.level + -1;
    if (b.level == 1) {
        b.hpMax = flags().test(0x58) ? 99 : 100;
    } else {
        // DAT_0048e4e0[FUN_002071e0(0x53eb20, 0x14)] (the level-up HP gain table) [V]
        static constexpr int8_t kHpGain[21] = {2, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 8};
        const uint32_t k = gameWorld().rng.rand(0x14);
        b.hpMax = b.hpMax - kHpGain[k];
        if (b.hpMax < 0) b.hpMax = 0;
    }
    const int mx = statusHpMaxWithItems(chr_->status);   // FUN_00134960
    if (mx < b.hp) b.hp = mx;
    if (b.level < 0xf) flags().clear(0x3ce);
    sound().playSe(1.0f, 0x8f);                          // FUN_00115390(1.0, 0x4e9790, 0x8f)
}
void ChrAdapter::addExp(int exp) { if (chr_) linkStatusAddExp(chr_->status, exp); }   // FUN_00138e40

// orig FUN_001391d0 (id != 0) / FUN_00139240 (id == 0)
void ChrAdapter::setBrand(uint32_t id) {
    if (!chr_) return;
    StatusBlock& b = chr_->status.block;
    if (id != 0) {
        b.brand = id;
        if (id == kIdST19) flags().set(0x3cc);
    } else if (b.brand != 0) {
        b.brand = 0;
        flags().clear(0x3cc);
    }
}

// orig FUN_00139280 (work/decompiled/00139280.c)
void ChrAdapter::setParasite(Item* p) {
    if (!chr_) return;
    chr_->status.block.parasite = p;
    flags().set(0x3cb);
}

bool ChrAdapter::canWarp() const { return chr_ != nullptr && statusCanWarp(chr_->status); }   // FUN_00138ad0
void ChrAdapter::recomputeMaxima() { if (chr_) statusRecompute(chr_->status); }

// ---- equipment models (player class 0x4cccb0) -----------------------------------------------------------------------
namespace {
constexpr uint32_t kIdGN00 = itemId("GN00");          // 0x462990 [V]
const char* const kGunHand = "DATA/OBJ/OBJ002.IGB";   // 0x4629a0 [V]
const char* const kGunBack = "DATA/OBJ/OBJ043.IGB";   // 0x4629e0 [V]
// "DATA/OBJ/SD%c%c.IGB" (0x4629c0) / "CT%c%c" (0x462a00) / "WG%c%c" (0x462a60) with the id's characters 2 and 3
std::string objPath(const char* prefix, uint32_t id) {
    char b[0x50];
    std::snprintf(b, sizeof b, "DATA/OBJ/%s%c%c.IGB", prefix, static_cast<char>((id >> 16) & 0xffu),
                  static_cast<char>(id >> 24));
    return b;
}
// FUN_001d9310(0x53d910, 'GN00') / FUN_001d7d10(0x53d910)
int gunCount() { return gEnv.inventory != nullptr ? gEnv.inventory->countId(kIdGN00) : 0; }
int weaponClassNow() {
    return gEnv.inventory != nullptr && gEnv.items != nullptr ? gEnv.inventory->weaponClass(*gEnv.items) : -1;
}

// orig FUN_001b1f20 (work/decompiled/001b1f20.c): the gun on the back, once (node 6 empty): OBJ002 (0x4629a0) under
// node 6 ("OBJ002_Attach_BASE"), OBJ043 (0x4629e0) under node 5 ("Spine1"); FUN_00325420 finds the resident files.
void gunBackAttach(Chr& c) {
    if (linkPlayerSlotAttached(c, 6)) return;
    linkPlayerSlotAttach(c, 6, kGunHand);
    linkPlayerSlotAttach(c, 5, kGunBack);
}
// orig FUN_001b20a0 (work/decompiled/001b20a0.c): the gun in the hand (node 4) taken off unless `force` is 0 and the
// weapon class is still 3
void gunHandRelease(Chr& c, bool force) {
    if (!linkPlayerSlotAttached(c, 4)) return;
    if (!force && weaponClassNow() == 3) return;
    linkPlayerSlotDetach(c, 4);
}
// orig FUN_001b2120 (work/decompiled/001b2120.c): the back parts (nodes 6, 5) taken off unless `force` is 0 and more
// guns than the ones in use are carried (count > 2 with the gun in hand, > 1 otherwise; unsigned compare)
void gunBackRelease(Chr& c, bool force) {
    if (!linkPlayerSlotAttached(c, 6)) return;
    if (!force) {
        const uint32_t inUse = weaponClassNow() == 3 ? 2u : 1u;
        if (inUse < static_cast<uint32_t>(gunCount())) return;
    }
    linkPlayerSlotDetach(c, 6);
    linkPlayerSlotDetach(c, 5);
}
// orig FUN_001b2060 (work/decompiled/001b2060.c; a1 passed on to both [V 0x1b2070..0x1b2084])
void gunRelease(Chr& c, bool force) {
    gunHandRelease(c, force);
    gunBackRelease(c, force);
}
// orig FUN_001b1e30 (work/decompiled/001b1e30.c): the weapon-class animation set (slot 0) released (FUN_001b2730);
// for the right hand (1) holding a non-'S' item (the gun: +0x58 low byte != 0x53) the gun goes to the back when one is
// carried (FUN_001b1f20) and node 4 is the one emptied.
void handRelease(Chr& c, uint32_t hand58, int which) {
    linkPlayerSetRelease(c, 0);
    if (which == 1 && hand58 != 0 && (hand58 & 0xffu) != 0x53u) {
        if (gunCount() != 0) gunBackAttach(c);
        which = 4;
    }
    linkPlayerSlotDetach(c, which);
}
// orig FUN_001b1c50 (work/decompiled/001b1c50.c): class 3 (gun): with exactly one gun the back parts go
// (FUN_001b2060(chr, 1) [V a1 = 1 at 0x1b1cd4]) and OBJ002 hangs under node 4 at once (resident file). Other classes:
// "DATA/OBJ/SD%c%c.IGB" loaded (pending count 0x53d780 + 1, flags | 0x30000, callback FUN_001b0df0 with the nodes
// {class 1 ? 2 : 1, paired ? 2 : -1}); the callback hangs the file's igSceneInfo (0x4627f8) graph under both nodes.
void handAttach(Chr& c, uint32_t id, bool paired) {
    const int cls = weaponClassNow();
    if (cls == 3) {
        if (gunCount() == 1) gunRelease(c, true);
        linkPlayerSlotAttach(c, 4, kGunHand);
        return;
    }
    const int nodes[2] = {cls == 1 ? 2 : 1, paired ? 2 : -1};
    const std::string path = objPath("SD", id);
    for (int n : nodes)
        if (n >= 0) linkPlayerSlotAttach(c, n, path.c_str());   // FUN_001b0df0 loop, then FUN_001b14e0
}
}  // namespace

// orig FUN_001b1540 (work/decompiled/001b1540.c), player class: compares the equipped items with the ids attached last
// (+0x58 / +0x5c hands, +0x60 / +0x64 coat / wings), re-attaches what changed and, when a hand changed, loads the
// weapon-class animation set into slot 0 and restarts the class's base animation (unless a "TH" action runs).
// Set / animation per FUN_001d7d10 class [V strings 0x462940..0x462988]: 0 "AWOH"/"M001", 1 "AWTH"/"M002",
// 2 "AWDH"/"M003", 3 "AWAG"/"M004", none "M005" (no set). An animation name missing from the database (FUN_0010a7f0
// with a null animation) restarts nothing. The loader flush at the head (piRam004e6648 vt+0x28c / +0x290: wait for the
// pending asynchronous loads) has nothing to wait for in the port (synchronous loads).
void ChrAdapter::refreshModelAttachments() {
    if (!chr_ || gEnv.inventory == nullptr || gEnv.items == nullptr) return;
    Chr& c = *chr_;
    const Inventory& inv = *gEnv.inventory;
    const Item* w = inv.equip[0];   // iRam0053d9b4
    bool changed = false;
    bool paired = false;
    if (w != nullptr && inv.equip[1] != nullptr) {   // iRam0053d9b8
        if (hand5c_ != inv.equip[1]->id) {
            handRelease(c, hand58_, 1);              // FUN_001b1e30(chr, 1)
            handRelease(c, hand58_, 2);              // FUN_001b1e30(chr, 2)
            handAttach(c, w->id, true);              // FUN_001b1c50(chr, weapon id, 1)
            changed = true;
            hand5c_ = w->id;
            hand58_ = w->id;
        }
        paired = true;
    }
    if (!paired) {
        changed = hand5c_ != 0;
        if (changed) {
            handRelease(c, hand58_, 2);              // FUN_001b1e30(chr, 2)
            hand5c_ = 0;
        }
        if (w == nullptr) {
            if (hand58_ != 0) {
                handRelease(c, hand58_, 2);
                handRelease(c, hand58_, 1);
                hand58_ = 0;
                changed = true;
            }
        } else if (hand58_ != w->id) {
            handRelease(c, hand58_, 2);
            handRelease(c, hand58_, 1);
            handAttach(c, w->id, false);             // FUN_001b1c50(chr, id, 0)
            changed = true;
            hand58_ = w->id;
        }
    }
    for (int i = 2; i < 4; ++i) {   // LAB_001b1698: equipment slots 2 (coat, +0x60) and 3 (wings, +0x64)
        const Item* e = inv.equip[i];
        uint32_t& cur = equip60_[i - 2];
        if (e == nullptr) {
            if (cur != 0) {
                if (i == 3) {
                    linkPlayerSlotDetach(c, 3);      // FUN_001b25c0
                } else {
                    coatRelease();                   // FUN_001b2400
                    linkPlayerBodySkin(c, true);     // FUN_003c7cb0(skin list, +0x44)
                }
                cur = 0;
            }
        } else if (cur != e->id) {
            if (i == 3) {
                linkPlayerSlotDetach(c, 3);          // FUN_001b25c0
                linkPlayerSlotAttach(c, 3, objPath("WG", e->id).c_str());   // FUN_001b2500: callback FUN_001b0df0 {3, -1}
            } else {
                coatRelease();                       // FUN_001b2400
                coatLoad(e->id);                     // FUN_001b2230
            }
            cur = e->id;
        }
    }
    if (!changed) return;
    const bool throwing = currentActionIs("TH");   // FUN_00143d30 name vs 0x462940 "TH"
    const int cls = inv.weaponClass(*gEnv.items);   // FUN_001d7d10(0x53d910)
    static constexpr const char* kSet[4] = {"AWOH", "AWTH", "AWDH", "AWAG"};
    static constexpr const char* kAnim[5] = {"M001", "M002", "M003", "M004", "M005"};
    if (cls >= 0 && cls < 4) linkPlayerSetLoad(c, 0, kSet[cls]);   // FUN_001b2620(chr, 0, set)
    if (!throwing) linkPlayerRestartAnim(c, kAnim[cls >= 0 && cls < 4 ? cls : 4]);   // FUN_0010a7f0
}

// orig FUN_001b2400 (work/decompiled/001b2400.c): no coat skin (+0x48 == 0): the BODY skin leaves the skin list
// (FUN_003c7cc0(list, +0x44)); else the coat file is released (FUN_0010cd50 on "DATA/OBJ/CT%c%c.IGB" of +0x68), its
// skin leaves the list and is dereferenced, +0x48 = 0, +0x68 = "CTXX" (0x462808 [V]), +0x6c = 0.
void ChrAdapter::coatRelease() {
    if (!linkPlayerCoatLoaded(*chr_)) {
        linkPlayerBodySkin(*chr_, false);
        return;
    }
    linkPlayerCoatUnload(*chr_);
    coat68_ = itemId("CTXX");
    coatShadow6c_ = nullptr;
    linkPlayerCoatShadow(*chr_, nullptr);
}
// orig FUN_001b2230 (work/decompiled/001b2230.c): +0x68 = id; "DATA/OBJ/CT%c%c.IGB" (0x462a00) loaded (pending count
// + 1, flags | 0x30000, callback FUN_001b0d20 with index 1: +0x48 = the first skin of its igAnimationDatabase
// (0x4627e0), added to the skin list); +0x6c = the shadow skin by the number (FUN_002388f0 on characters 2..3):
// 0 / 2 / 15 / 19 none, 1 / 9 / 10 / 12 / 13 / 14 / 16 / 18 "DATA/OBJ/CT01_KAGE.IGB" (0x462a20), 3..8 / 11 / 17
// "DATA/OBJ/CT06_KAGE.IGB" (0x462a40) [V strings]; other numbers leave +0x6c as it was.
void ChrAdapter::coatLoad(uint32_t id) {
    coat68_ = id;
    linkPlayerCoatLoad(*chr_, objPath("CT", id).c_str());
    const char num[3] = {static_cast<char>((id >> 16) & 0xffu), static_cast<char>(id >> 24), 0};
    switch (std::atoi(num)) {   // FUN_002388f0
        case 0: case 2: case 0xf: case 0x13:
            coatShadow6c_ = nullptr;
            linkPlayerCoatShadow(*chr_, nullptr);
            break;
        case 1: case 9: case 10: case 0xc: case 0xd: case 0xe: case 0x10: case 0x12:
            coatShadow6c_ = "DATA/OBJ/CT01_KAGE.IGB";
            linkPlayerCoatShadow(*chr_, coatShadow6c_);
            break;
        case 3: case 4: case 5: case 6: case 7: case 8: case 0xb: case 0x11:
            coatShadow6c_ = "DATA/OBJ/CT06_KAGE.IGB";
            linkPlayerCoatShadow(*chr_, coatShadow6c_);
            break;
        default:
            break;
    }
}

// on = true: FUN_001b1f20 (an item of class GN entered the inventory, FUN_001d65c0); on = false: FUN_001b2060(chr,
// force) (FUN_001d6840 / FUN_001d9710 pass 0 [V a1 at 0x1d68d8 / 0x1d97a8], the event costume FUN_001d7dc0 passes 1
// [V 0x1d8034]).
void ChrAdapter::gunEquipped(bool on, bool force) {
    if (!chr_) return;
    if (on) gunBackAttach(*chr_);
    else gunRelease(*chr_, force);
}
void ChrAdapter::invincibleFlash() { if (chr_) linkHeadFlash(*chr_); }   // FUN_0013de20

// orig FUN_00136170 (work/decompiled/00136170.c; FUN_00135f00(status, attacker) [V a0/a1 kept at 0x136198])
int ChrAdapter::projectileDamage(CharacterAccess* attacker, int base, uint32_t m1) {
    if (!chr_) return 0;
    const Chr* a = attacker != nullptr ? static_cast<ChrAdapter*>(attacker)->chr() : nullptr;
    const float red = linkVictimReduction(chr_->status, a);
    const float weak = linkElementWeakness(chr_->status, m1);
    float lv = 0.0f, k1 = 1.0f, k2 = 1.0f;
    if (a != nullptr && a == characters().player()) {
        const StatusBlock& b = a->status.block;
        lv = static_cast<float>(b.level);
        k1 = static_cast<float>(b.hidden[3]) / 255.0f + 1.0f;
        k2 = (static_cast<float>(b.hidden[2]) / 255.0f) * 0.5f + 1.0f;
    }
    return static_cast<int>(red * k2 * k1 * (weak + 1.0f) * (lv * 2.0f + static_cast<float>(base) + 0.0f));
}
// FUN_00136580(status, attacker, dmg, m3, 0, 0)
bool ChrAdapter::applyDamage(CharacterAccess* attacker, int dmg, uint32_t m3) {
    if (!chr_) return false;
    Chr* a = attacker != nullptr ? static_cast<ChrAdapter*>(attacker)->chr() : nullptr;
    return linkApplyDamage(chr_->status, a, dmg, m3);
}
// FUN_0014c580(chr, 0, m1, 0)
void ChrAdapter::hitElementFx(uint32_t m1) { if (chr_) linkHitElementFx(*chr_, m1); }
// orig FUN_00139540 (work/decompiled/00139540.c)
Item* ChrAdapter::dropHeldItem() {
    if (!chr_) return nullptr;
    Item* it = static_cast<Item*>(chr_->status.block.carried);
    chr_->status.block.carried = nullptr;
    return it;
}
void ChrAdapter::die() { if (chr_) linkRequestDeath(*chr_, 0); }   // FUN_0012a5e0(chr, 0)
void ChrAdapter::react(uint32_t kind, const Vec3& from, bool reversed) { if (chr_) linkReact(*chr_, kind, from, reversed); }   // FUN_0012a710
void ChrAdapter::returnToIdle() { if (chr_) linkPlayerIdle(*chr_); }   // FUN_0013d810(0x50b620, chr)

// FUN_00143d30(act, -1) and the 2-byte compare of its callers (FUN_001d7750, FUN_001d9040)
bool ChrAdapter::currentActionIs(const char* tag2) const {
    if (!chr_ || tag2 == nullptr) return false;
    const char* name = chr_->act.currentName();
    return name != nullptr && name[0] == tag2[0] && name[1] == tag2[1];
}

// orig FUN_001395c0 (work/decompiled/001395c0.c)
bool ChrAdapter::isHoldingItem() const { return chr_ != nullptr && chr_->status.block.carried != nullptr; }
// orig FUN_00139590 (work/decompiled/00139590.c)
bool ChrAdapter::takeItem(Item* item) {
    if (!chr_) return false;
    const bool empty = chr_->status.block.carried == nullptr;
    if (empty) chr_->status.block.carried = item;
    return empty;
}
// AI component comp+0x14: FUN_001f1690(ai, ev, 1) (a2 = 1 at 0x1dcc50 [V]) / FUN_001f1730(ai, ev) != 0 / +0x14 bit 0
void ChrAdapter::sendAiEvent(uint32_t ev) { if (chr_) linkAiSetVar(*chr_, ev, 1); }
bool ChrAdapter::aiAccepts(uint32_t ev) const { return chr_ != nullptr && linkAiVar(*chr_, ev) != 0; }
bool ChrAdapter::aiDisabled() const { return chr_ != nullptr && (chr_->ai.flags & 1u) != 0; }
// vt+0x20 = FUN_0012d400 (+0x24 bit 0) of every class
bool ChrAdapter::aiAlertable() const { return chr_ != nullptr && chr_->active(); }

// orig FUN_00138ad0 (work/decompiled/00138ad0.c): false for "E16" / "E19" (0x44b0f0 / 0x44b0d8 [V]); no cell or a
// non-room cell -> true; in a room, false for the RDT room ids (byte 0 of room +4) 0xc..0xd on map set 4, 0x14..0x15
// on map set 2, 0x15..0x18 on map set 0 (*pcRam0050a620 = FDT byte 0); any other map set -> true.
bool statusCanWarp(const StatusObj& s) {
    const Chr* c = s.owner;
    if (c == nullptr) return true;
    const uint32_t id3 = c->id & 0xffffffu;
    if (id3 == (itemId("E16\0") & 0xffffffu) || id3 == (itemId("E19\0") & 0xffffffu)) return false;
    if (c->cell == nullptr) return true;
    const uint32_t w = *c->cell;
    if ((w & 0xf000u) != 0x1000u) return true;
    const int room = static_cast<int>(w & 0xfu);
    const Dungeon* d = gameWorld().dungeon;
    if (d == nullptr || room >= d->roomCount()) return true;   // PORT: no floor context (the original always has one)
    const char set = static_cast<char>(d->fdt().mapSet);
    const uint8_t rid = d->room(room).rdt.roomId;
    if (set == '\x04') {
        if (static_cast<uint8_t>(rid - 0xc) < 2) return false;
    } else if (set == '\x02') {
        if (static_cast<uint8_t>(rid - 0x14) < 2) return false;
    } else {
        if (set != '\0') return true;
        if (static_cast<uint8_t>(rid - 0x15) < 4) return false;
    }
    return true;
}

}  // namespace baroque::game
