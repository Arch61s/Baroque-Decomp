#include "game/items/item_effects.hpp"
#include "game/items/item_world.hpp"
#include <cmath>
#include <cstdio>

namespace baroque::game {

namespace {
constexpr uint32_t kIdST03 = itemId("ST03");
constexpr uint32_t kIdST04 = itemId("ST04");
constexpr uint32_t kIdST11 = itemId("ST11");
constexpr uint32_t kIdST16 = itemId("ST16");
constexpr uint32_t kIdST02 = itemId("ST02");
constexpr uint32_t kIdHT01 = itemId("HT01");
constexpr uint32_t kIdHT02 = itemId("HT02");
constexpr uint32_t kIdMT02 = itemId("MT02");
constexpr uint32_t kIdMT03 = itemId("MT03");
constexpr uint32_t kIdBX03 = itemId("BX03");
constexpr uint32_t kIdWM22 = itemId("WM22");
constexpr uint32_t kIdWM04 = itemId("WM04");
constexpr uint32_t kIdWM05 = itemId("WM05");
constexpr uint32_t kIdWM06 = itemId("WM06");
constexpr uint32_t kIdWM07 = itemId("WM07");

// 0x4646b8: TL19 "random item -> random food" table (HT00 HT01 HT02 MT00 MT01 MT02 MT03) [V words]
const uint32_t kFoodTable[7] = {itemId("HT00"), itemId("HT01"), itemId("HT02"), itemId("MT00"), itemId("MT01"), itemId("MT02"), itemId("MT03")};
// 0x4997e0: TL19 reversed "random food -> random item of class" [V words]
const int kFoodToClass[9] = {0, 1, 2, 5, 6, 7, 8, 9, 10};
// 0x48e4e0: level-down HP max loss table (20 entries) [V words]
const int kLevelDownHp[20] = {2, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7};

Inventory& inv(ItemSystem& s) { return *s.inventory; }
CharacterAccess& player(ItemSystem& s) { return s.game->player(); }

// "Identified %s." when the item was unknown (shared head of every handler)
void identifyOnUse(ItemSystem& sys, Item& it) {
    if (it.identified()) return;
    it.flags |= kItemIdentified;
    sys.game->message(0x256, sys.name(it.cls, it.idx));
    sys.game->addCounter(0xc, 1);
}

Vec3 headPos(const CharacterAccess& c) { Vec3 p = c.position(); p.y += c.height() * 0.5f; return p; }
Vec3 iconPos(const CharacterAccess& c) { Vec3 p = c.position(); p.y += c.iconHeight() * 0.5f; return p; }

int toIntTrunc(float f) {   // the "fVar < 2.1474836e+09 ? (uint)f : ..." idiom (float -> unsigned conversion)
    if (f < 2.1474836e+09f) return static_cast<int>(f);
    return static_cast<int>(static_cast<uint32_t>(f - 2.1474836e+09f) | 0x80000000u);
}
}  // namespace

void messageName(ItemSystem& sys, const char* name) {
    sys.game->messageFmt(sys.game->language() == 1 ? "%s\x81\x84" : "%s", name);   // 0x464650 / 0x464658
}

// orig FUN_001d8950 (work/decompiled/001d8950.c)
bool useLegal(ItemSystem& sys, int idx, int targetIdx) {
    Inventory& v = inv(sys);
    if (idx < 0 || idx >= v.count) return false;
    if (v.equippedSlotOfIndex(idx) != -1 && !v.equipLegal(idx, false, sys)) return false;
    switch (v.slots[idx]->cls) {
        case kClsIS: return crystalTargetOk(sys, idx, targetIdx);
        case kClsWM: return parasiteTargetOk(sys, idx, targetIdx);
        case kClsST: return brandTargetOk(sys, idx, targetIdx);
        case kClsBX: return boxUsable(sys, idx);
        default: return true;
    }
}

// orig FUN_001d8810 (work/decompiled/001d8810.c)
bool useItem(ItemSystem& sys, int idx, int targetIdx) {
    if (!useLegal(sys, idx, targetIdx)) return false;
    Inventory& v = inv(sys);
    Item* it = v.slots[idx];
    v.usingItem = it;
    switch (it->cls) {
        case kClsHT: useHeart(sys, idx); break;
        case kClsMT: useFlesh(sys, idx); break;
        case kClsBX: useBox(sys, idx, false); break;
        case kClsLQ: useLiquid(sys, idx); break;
        case kClsST: useBrand(sys, idx, targetIdx); break;
        case kClsTL: useTorturer(sys, idx); break;
        case kClsBN: useBone(sys, idx); break;
        case kClsWM: useParasite(sys, idx, targetIdx); break;
        case kClsIS: useCrystal(sys, idx, targetIdx); break;
        default: break;
    }
    player(sys).recomputeMaxima();
    return true;
}

// orig FUN_001dccb0 (work/decompiled/001dccb0.c)
bool boxUsable(ItemSystem& sys, int idx) {
    const Item* it = inv(sys).slots[idx];
    if (it->idx == 4) {
        // FUN_001344f0(status, -1) (attack power) - status+0x18 (ATK level bonus) >= BX04 +0x84
        CharacterAccess& p = player(sys);
        return sys.word(kClsBX, 4, 0x84) <= p.attackPower() - p.atkLevelBonus();
    }
    if (it->idx == 3) return inv(sys).countId(kIdBX03) > 1;
    return true;
}

// orig FUN_001dfa00 (work/decompiled/001dfa00.c)
bool brandTargetOk(ItemSystem& sys, int idx, int targetIdx) {
    Inventory& v = inv(sys);
    const Item* it = v.slots[idx];
    const uint32_t bits = static_cast<uint32_t>(sys.word(*it, 0x74));
    uint32_t cur;
    if (targetIdx < 0) {
        if (!(bits & 1)) return false;
        cur = player(sys).brand();
    } else {
        if (!(bits & 6)) return false;
        const Item* t = v.slots[targetIdx];
        if (bits & 4) {
            if (t->cls != kClsSD && t->cls != kClsCT && t->cls != kClsWG) return false;
        }
        cur = t->brand;
    }
    bool r = cur != 0;
    if (it->id != kIdST11) r = !r;
    return r;
}

// orig FUN_001e4510 (work/decompiled/001e4510.c)
bool parasiteTargetOk(ItemSystem& sys, int idx, int targetIdx) {
    Inventory& v = inv(sys);
    const Item* it = v.slots[idx];
    const uint32_t bits = static_cast<uint32_t>(sys.word(*it, 0x74));
    if (targetIdx < 0) return (bits & 1) != 0;
    const Item* t = v.slots[targetIdx];
    if (bits & 2) return true;
    if ((bits & 4) && t->cls == kClsSD) return true;
    if ((bits & 8) && t->cls == kClsCT) return true;
    if ((bits & 0x10) && t->cls == kClsBX) return true;
    if (bits & 0x20) {
        if (it->parasite) return false;
        if (it->idx == 0x13) { if (t->cls == kClsWM) return true; }
        else if (t->cls == kClsWM && (static_cast<uint32_t>(sys.word(*t, 0x74)) & 0x20)) return true;
    }
    if ((bits & 0x40) && !t->identified()) return true;
    if ((bits & 0x80) && t->id == kIdWM22) return true;
    return false;
}

// orig FUN_001e52b0 (work/decompiled/001e52b0.c)
bool crystalTargetOk(ItemSystem& sys, int idx, int targetIdx) {
    if (targetIdx < 0) return false;
    const Item* it = inv(sys).slots[idx];
    if (it->idx == 1 || it->idx == 4) return false;
    return canFuseCrystal(*inv(sys).slots[targetIdx]);
}

namespace {
// shared tail of FUN_001dc250 / FUN_001dc6e0: rotten food -> Stomachache
void rottenEffect(ItemSystem& sys, CharacterAccess& p) {
    const uint32_t mask = p.statusMask();
    GameAccess& g = *sys.game;
    if (p.inflict(0x40) == 0) { g.message(0x290); g.message(0x2ed); }
    else { g.message(0x290); if (!(mask & 0x40)) g.message(0x2eb); g.message(0x268); }
}
}  // namespace

// orig FUN_001dc250 (work/decompiled/001dc250.c)
void useHeart(ItemSystem& sys, int idx) {
    GameAccess& g = *sys.game;
    g.setFlag(0x2b0);
    g.setAchievementFlag(0x45f);
    g.se(0x5b);
    Item* it = inv(sys).remove(idx, sys);
    if (!it) return;
    identifyOnUse(sys, *it);
    CharacterAccess& p = player(sys);
    const int gain = sys.word(*it, 0x74), maxGain = sys.word(*it, 0x78), lossPct = sys.word(*it, 0x7c);
    if (p.vt() == p.vtMaxTotal()) {
        if (it->id != kIdHT02) {
            if (p.vtMaxTotal() < 999) g.message(0x286, maxGain); else g.message(0x2b9);
            p.addVtMax(maxGain);
            p.addVt(maxGain);
        }
    } else {
        const float f = static_cast<float>(p.hiddenStat(4)) / 255.0f + 1.0f;
        p.addVt(static_cast<int>(static_cast<float>(gain) * f));
        if (it->id == kIdHT01) g.message(0x287);
        else if (it->id != kIdHT02) g.message(0x285, toIntTrunc(static_cast<float>(gain) * f));
    }
    if (it->id == kIdHT02) {
        p.addHp(-static_cast<int>(static_cast<float>(p.hp()) * (static_cast<float>(lossPct) / 100.0f)));
        g.message(0x288);
    }
    if (it->flags & kItemRotten) rottenEffect(sys, p);
    g.freeItem(it);
}

// orig FUN_001dc6e0 (work/decompiled/001dc6e0.c)
void useFlesh(ItemSystem& sys, int idx) {
    GameAccess& g = *sys.game;
    g.setFlag(0x2b2);
    g.setAchievementFlag(0x45f);
    g.se(0x5b);
    Item* it = inv(sys).remove(idx, sys);
    if (!it) return;
    identifyOnUse(sys, *it);
    CharacterAccess& p = player(sys);
    const int gain = sys.word(*it, 0x74), maxGain = sys.word(*it, 0x78), vtGain = sys.word(*it, 0x7c);
    if (p.hp() == p.hpMaxTotal()) {
        if (it->id != kIdMT03) {
            if (p.hpMaxTotal() < 999) g.message(0x27e, maxGain); else g.message(0x2b9);
            p.addHpMax(maxGain);
            p.addHp(maxGain);
        }
    } else {
        const float f = static_cast<float>(p.hiddenStat(4)) / 255.0f + 1.0f;
        p.addHp(static_cast<int>(static_cast<float>(gain) * f));
        if (it->id == kIdMT02) g.message(0x280);
        else if (it->id != kIdMT03) g.message(0x27d, toIntTrunc(static_cast<float>(gain) * f));
    }
    if (it->id == kIdMT03) { p.addVt(vtGain); g.message(0x281); }
    if (it->flags & kItemRotten) rottenEffect(sys, p);
    g.freeItem(it);
}

namespace {
// "%s" + "was in the chest." (EN order) / "In the chest" "%s" "Found" (JP) around a found item name
void foundMessages(ItemSystem& sys, const char* name) {
    GameAccess& g = *sys.game;
    if (g.language() == 1) { g.message(0x2cd); g.message(0x248, name); g.message(0x2e2); }
    else if (g.language() == 0) { g.message(0x248, name); g.message(0x300); }
}
}  // namespace

// orig FUN_001dcdb0 (work/decompiled/001dcdb0.c)
void useBox(ItemSystem& sys, int idx, bool recursed) {
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    g.setFlag(0x2b4);
    Item* box = v.slots[idx];
    identifyOnUse(sys, *box);
    const int bi = box->idx;
    if (bi == 0x12) { g.se(0x60); g.se(0x5c); }
    else if (bi == 0xe || bi == 0xd || bi == 0xc) g.se(0x62);
    else g.se(0x5c);
    CharacterAccess& p = player(sys);
    if (p.brand() == kIdST03) {
        p.invincibleFlash();
        const int heal = sys.word(kClsST, 3, 0x7c);
        p.addHp(heal);
        p.addVt(heal);
    } else if (!(box->flags & kItemDisarmed) && sys.rand(0xf) < static_cast<uint32_t>(sys.word(*box, 0x74))) {
        int kind = 0;
        if (bi == 10) { const uint32_t r = sys.rand(99); if (r > 0x18) kind = r < 0x41 ? 1 : 2; }
        g.spawnTrap(p.position(), kind, 1, 0, 0, sys.word(*box, 0x78), &p, 0);
        g.closeItemScreen();
        Item* rem = v.remove(idx, sys);
        if (rem) g.freeItem(rem);
        return;
    }
    const uint32_t roll = sys.rand(99);
    uint32_t n = static_cast<uint32_t>(box->count);
    uint32_t result = 0;
    switch (bi) {
        case 0x19: {
            if (v.count == Inventory::kMaxItems) { g.message(0x2b4); break; }
            const uint32_t id = weightedItemId(sys, randomFloorItemClass(sys));
            if (id == 0) break;
            Item* it = g.createItem(id, false);
            if (!it) break;
            v.add(it, sys);
            foundMessages(sys, itemDisplayName(*it, sys));
            box->count = static_cast<int>(n - 1);
            if (n - 1 != 0) return;
            g.se(0x5f);
            n = 0;
            break;
        }
        case 0x18: {
            int k = static_cast<int>(n) >> 2;
            g.addCounter(5, -g.counter(5));          // uRam0053c5d4 = 0
            if (k > 0xf) k = 0xf;
            n = static_cast<uint32_t>(sys.word(*box, 0x2c + 4u * static_cast<uint32_t>(k)));
            break;
        }
        case 0x15: alarmBoxEffect(sys); break;
        case 0x14:
            if (v.hungryChestSwap(idx, sys)) return;
            g.message(0x28e);
            break;
        case 0x13: {
            const uint32_t mask = p.statusMask();
            if (p.inflict(0x40) == 0) g.message(0x2ed);
            else { if (!(mask & 0x40)) g.message(0x2eb); g.message(0x268); }
            break;
        }
        case 0x12:
            p.addHp(-static_cast<int>(static_cast<float>(p.hp()) * (static_cast<float>(sys.word(*box, 0x84)) / 100.0f)));
            g.message(0x282);
            break;
        case 0x11:
            if (roll < static_cast<uint32_t>(sys.word(*box, 0x84))) {
                g.transformAllFloorItems(n);     // every lying item -> id `n` (effect 0x6c each), SE 0x55 + 0x297/0x2ef when any
                g.closeItemScreen();
            }
            break;
        case 0x10:
            if (roll < static_cast<uint32_t>(sys.word(*box, 0x84))) {
                g.se(0x58); g.se(0x91);
                v.stripAll(sys);
                g.closeItemScreen();
                g.message(0x2d3); g.message(0x2f2);
            }
            break;
        case 0xf: {
            const int hp = p.hp();
            if (p.hpMaxTotal() / 3 < hp) { p.addHp(-(hp >> 1)); p.addVt(-(p.vt() >> 1)); g.message(0x27a); }
            else { p.addHp(999); p.addVt(999); g.message(0x279); }
            g.se(0x61);
            break;
        }
        case 6:
            for (int r = 0; result == 0 && r < 64; ++r) {
                for (int cls = 0; cls < 12; ++cls) {
                    if (r < sys.rowCount(cls)) {
                        const uint32_t id = makeItemId(cls, r);
                        if (!v.hasId(id)) { result = id; break; }
                    }
                }
            }
            break;
        case 5: {
            Item* last = readItemRecord(v.backup, sys);   // 0x53da1c = inventory +0x10c (last added item record)
            if (last) {
                if (g.language() == 1) { g.message(0x2cd); g.message(0x248, itemDisplayName(*last, sys)); g.message(0x2e2); }
                else if (g.language() == 0) { g.message(0x2cd); g.message(0x248, itemDisplayName(*last, sys)); }
                v.swapSlot(idx, last);
                g.freeItem(box);
                return;
            }
            break;
        }
        case 3:
            if (!recursed) {
                for (int j = 0; j < v.count; ++j) {
                    if (j != idx && v.slots[j]->id == box->id) { useBox(sys, j, true); break; }
                }
            }
            break;
        case 1: {
            const int hp = p.hp();
            if (hp == p.hpMaxTotal()) g.message(0x2b9);
            else {
                if (hp + static_cast<int>(n) < p.hpMaxTotal()) g.message(0x27d, static_cast<int>(n)); else g.message(0x280);
                g.se(0x8c);
            }
            p.addHp(static_cast<int>(n));
            n = 0;
            break;
        }
        default: break;
    }
    const int i2 = v.indexOf(box);
    if ((n & 0xff) > 0x40 && (n & 0xff) < 0x5b) result = n;   // a 4-char id in the count word
    if (result == 0) {
        Item* rem = v.remove(i2, sys);
        if (rem) g.freeItem(rem);
    } else {
        v.replaceItem(i2, result, sys);
        foundMessages(sys, itemDisplayName(*v.slots[i2], sys));
    }
}

namespace {
// LQ ATK/DEF modifier application with the "%s" / "ATK n->m" / "1 up" messages (FUN_001ddc20 cases 1..5)
bool liquidModWeapon(ItemSystem& sys, Item* e, int a) {
    if (!e || e->cls == kClsGN || !addAtkMod(*e, a)) return false;
    GameAccess& g = *sys.game;
    CharacterAccess& p = player(sys);
    g.message(0x248, sys.name(e->cls, e->idx));
    const int total = inv(sys).atkTotal(sys);
    if (total < 0) g.message(0x26b, p.atkCur(), -total); else g.message(0x26a, p.atkCur(), total);
    g.message(a < 1 ? 0x267 : 0x266);
    return true;
}
bool liquidModCoat(ItemSystem& sys, Item* e, int a) {
    if (!e || !addDefMod(*e, a)) return false;
    GameAccess& g = *sys.game;
    CharacterAccess& p = player(sys);
    g.message(0x248, sys.name(e->cls, e->idx));
    const int total = inv(sys).defTotal(sys);
    if (total < 0) g.message(0x273, p.defCur(), -total); else g.message(0x272, p.defCur(), total);
    g.message(a < 1 ? 0x267 : 0x266);
    return true;
}
void liquidModFinish(ItemSystem& sys, bool any, int a) {
    GameAccess& g = *sys.game;
    if (any) g.se(a < 0 ? 0x91 : 0x90);
    else { g.message(0x2b9); g.se(0x74); }
}
bool cureReport(ItemSystem& sys, uint32_t bit, int msg) {
    if (!(player(sys).statusMask() & bit)) return false;
    sys.game->message(msg);
    return true;
}
}  // namespace

// orig FUN_001ddc20 (work/decompiled/001ddc20.c)
void useLiquid(ItemSystem& sys, int idx) {
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    g.setFlag(0x2b6);
    g.se(100);
    Item* it = v.remove(idx, sys);
    if (!it) return;
    identifyOnUse(sys, *it);
    CharacterAccess& p = player(sys);
    if (it->idx != 0x15) { p.addHp(10); p.addVt(10); }
    Item* e0 = v.equip[0];
    Item* e1 = v.equip[1];
    Item* e2 = v.equip[2];
    const int own = it->count;
    const int a = sys.word(*it, 0x74), b = sys.word(*it, 0x78);
    switch (it->idx) {
        case 0:
            p.addHp(999); p.addVt(999);
            if (p.cure(0x20) == 0) g.message(0x279); else { g.message(0x279); g.message(0x2d8); }
            g.se(0x8c);
            break;
        case 1: case 4: {
            bool any = liquidModWeapon(sys, e0, a);
            if (e1 && e1->idx < 0x1d) any |= liquidModWeapon(sys, e1, a);
            liquidModFinish(sys, any, a);
            break;
        }
        case 2: case 5: liquidModFinish(sys, liquidModCoat(sys, e2, a), a); break;
        case 3: {
            bool any = liquidModWeapon(sys, e0, a);
            if (e1 && e1->idx < 0x1d) any |= liquidModWeapon(sys, e1, a);
            any |= liquidModCoat(sys, e2, a);
            liquidModFinish(sys, any, a);
            break;
        }
        case 6: g.message(p.level() == 99 ? 0x2b9 : 0x29d); p.levelUp(); break;
        case 7: g.message(p.level() == 1 ? 0x2b9 : 0x29e); p.levelDown(); break;
        case 8: case 9: case 10: {
            bool ok = false;
            if (it->idx != 9) {
                if (p.atkCur() < p.atkMax()) { p.addAtk(999); g.message(0x271); ok = true; }
                else { ok = p.atkMax() < 999; if (ok) g.message(0x26f); p.addAtkMax(b); p.addAtk(b); }
            }
            if (it->idx != 8) {
                if (p.defCur() < p.defMax()) { p.addDef(999); g.message(0x278); ok = true; }
                else { if (p.defMax() < 999) { g.message(0x276); ok = true; } p.addDefMax(b); p.addDef(b); }
            }
            if ((p.statusMask() & 0x40) && p.atkCur() == p.atkMax() && p.defCur() == p.defMax()) { g.message(0x2e9); p.cure(0x40); ok = true; }
            if (!ok) g.message(0x2b9);
            g.se(0x90);
            break;
        }
        case 0xb: {
            bool ok = false;
            if (p.atkCur() < p.atkMax()) { g.message(0x271); ok = true; }
            else if (p.atkMax() < 999) { g.message(0x270); ok = true; }
            if (p.defCur() < p.defMax()) { g.message(0x278); ok = true; }
            else if (p.defMax() < 999) { g.message(0x277); ok = true; }
            p.addAtkMax(b); p.addDefMax(b); p.addAtk(999); p.addDef(999);
            if (p.statusMask() & 0x40) { g.message(0x2e9); p.cure(0x40); ok = true; }
            if (!ok) g.message(0x2b9);
            g.se(0x90);
            break;
        }
        case 0xc: { const bool c1 = cureReport(sys, 2, 0x2ba), c2 = cureReport(sys, 0x10, 0x2f5); if (c1 || c2) p.cure(0x12); else { g.message(0x2b9); g.se(0x74); } break; }
        case 0xd: { const bool c1 = cureReport(sys, 4, 0x2dd), c2 = cureReport(sys, 1, 0x299); if (c1 || c2) p.cure(5); else { g.message(0x2b9); g.se(0x74); } break; }
        case 0xe: { const bool c1 = cureReport(sys, 8, 0x2a1), c2 = cureReport(sys, 0x10, 0x2f5); if (c1 || c2) p.cure(0x18); else { g.message(0x2b9); g.se(0x74); } break; }
        case 0xf: if (!(p.statusMask() & 0x20)) { g.message(0x2b9); g.se(0x74); } else { g.message(0x2d5); p.cure(0x20); } break;
        case 0x10: {
            bool any = false;
            any |= cureReport(sys, 4, 0x2dd); any |= cureReport(sys, 1, 0x299); any |= cureReport(sys, 2, 0x2ba);
            any |= cureReport(sys, 0x10, 0x2f5); any |= cureReport(sys, 8, 0x2a1); any |= cureReport(sys, 0x20, 0x2d5);
            any |= cureReport(sys, 0x40, 0x2e9);
            if (any) p.cure(0x7f); else { g.message(0x2b9); g.se(0x74); }
            break;
        }
        case 0x11:
            p.addHp(999); p.addVt(999); p.addAtk(999); p.addDef(999); p.cureAll();
            for (int i = 0; i < v.count; ++i) {
                Item* x = v.slots[i];
                if (x->flags & kItemRotten) x->flags &= ~kItemRotten;
                if (x->atkMod < 0) addAtkMod(*x, -x->atkMod);
                if (x->defMod < 0) addDefMod(*x, -x->defMod);
            }
            g.message(0x279); g.message(0x269); g.message(0x2e6); g.message(0x2f8);
            g.se(0x8b);
            break;
        case 0x12: v.identifyAll(sys); g.se(0x54); break;
        case 0x13: {
            bool any = false;
            for (int i = 0; i < v.count; ++i) {
                Item* x = v.slots[i];
                if (x->cls < 5 && x->cls != kClsWG && !(x->flags & kItemSealed)) { x->flags |= kItemSealed; any = true; }
            }
            if (any) { g.message(0x2ce); g.message(0x2e7); g.message(0x2f9); g.se(0x65); }
            else { g.message(0x2b9); g.se(0x74); }
            break;
        }
        case 0x14: {
            bool any = false;
            for (int i = 0; i < v.count; ++i) {
                Item* x = v.slots[i];
                if (x->cls != kClsBN) continue;
                int n = own + x->count;
                if (n > 99) n = 99;
                x->count = n;
                g.message(0x248, itemDisplayName(*x, sys));
                g.message(0x247, own);
                any = true;
            }
            if (any) g.se(0x51); else g.message(0x2b9);
            break;
        }
        case 0x15: p.hpVtMaxShift(); break;
        case 0x16: g.message(0x2b9); break;
        case 0x17:
            p.addHp(-static_cast<int>(static_cast<float>(p.hp()) * (static_cast<float>(a) / 100.0f)));
            g.message(0x27f);
            g.se(0x66);
            break;
        case 0x18: {
            g.se(0x77);
            Item* np = g.createItem(static_cast<uint32_t>(own), false);
            Item* old = p.parasite();
            if (np) {
                g.message(0x2cb, itemDisplayName(*np, sys));
                p.setParasite(np);
            }
            if (old) {
                parasiteDie(*old);
                v.add(old, sys);
                g.message(0x2da);
                g.se(0x79);
            }
            break;
        }
        default: break;
    }
    g.freeItem(it);
}

// orig FUN_001dfb20 (work/decompiled/001dfb20.c)
void useBrand(ItemSystem& sys, int idx, int targetIdx) {
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    g.setFlag(0x2b8);
    Item* it = v.slots[idx];
    identifyOnUse(sys, *it);
    g.se(it->idx == 0xb ? 0x68 : 0x67);
    if (it->idx == 10) g.se(0x59);
    else if (it->idx == 4) g.se(0x54);
    CharacterAccess& p = player(sys);
    if (targetIdx < 0) {
        if (it->id == kIdST11) {
            g.message(0x25c, sys.name(kClsST, itemIndexOf(p.brand())));   // "Delete %s."
            p.setBrand(0);
        } else {
            p.setBrand(it->id);                                            // FUN_001391d0 (brand = usingItem id)
            messageName(sys, sys.name(it->cls, it->idx));
            g.message(0x2cc);                                              // "Marked self with Brand."
        }
        if (it->id == kIdST04) v.identifyAll(sys);
    } else {
        Item* t = v.slots[targetIdx];
        if (it->id == kIdST11) {
            g.message(0x25c, sys.name(kClsST, itemIndexOf(t->brand)));
            t->brand = 0;
        } else {
            t->brand = it->id;
            messageName(sys, sys.name(it->cls, it->idx));
            g.message(0x257, itemDisplayName(*t, sys));                    // "Marked on %s"
        }
    }
    v.remove(idx, sys);
    g.freeItem(it);
}

// orig FUN_001e16b0 (work/decompiled/001e16b0.c): schedules the player state 0x1e1710 (TL use action)
void useTorturer(ItemSystem& sys, int idx) {
    inv(sys).usingItem = inv(sys).slots[idx];
    sys.game->playerActionRequest("TL", 0);
}

// orig FUN_001e1550 (work/decompiled/001e1550.c), the part that runs when the action's 0x100000 trigger fires
void torturerUseComplete(ItemSystem& sys) {
    Inventory& v = inv(sys);
    Item* it = v.usingItem;
    if (!it) return;
    identifyOnUse(sys, *it);
    CharacterAccess& p = player(sys);
    torturerDeport(sys, &p, &p, *it);
    v.remove(v.indexOf(it), sys);
    sys.game->freeItem(it);
    v.usingItem = nullptr;
}

// orig FUN_001e1110 (work/decompiled/001e1110.c)
void torturerDeport(ItemSystem& sys, CharacterAccess* user, CharacterAccess* target, Item& item) {
    GameAccess& g = *sys.game;
    const bool byPlayer = user && user->isPlayer();
    if (byPlayer) g.setFlag(0x2ba);
    if (byPlayer && target && target->isPlayer()) {
        // FUN_00112190(0, 0x6d, 0, 0, 0); joint "Left_WEAPON00_Attach" (0x464680 [V]) world matrix with its diagonal
        // [0] / [5] / [10] overwritten by 100 (FUN_0039dc10, 0x42c80000), FUN_00112770. PORT guard: without the joint
        // the effect keeps FUN_00112190's placement (the original reads the matrix array at index -1).
        float m[16] = {};
        const bool found = user->jointMatrix("Left_WEAPON00_Attach", m);
        m[0] = 100.0f;
        m[5] = 100.0f;
        m[10] = 100.0f;
        g.effectMatrix(0x6d, found ? m : nullptr);
    } else {
        g.effect(0x6d, item.pos);   // FUN_00112490 scale 100, FUN_001122d0 item position
    }
    torturerField(sys, item.idx, user, target);
}

// orig FUN_001e0d40 (work/decompiled/001e0d40.c)
void torturerField(ItemSystem& sys, int tlIdx, CharacterAccess* user, CharacterAccess* target) {
    GameAccess& g = *sys.game;
    uint32_t statusMask = 0, reactMask = 0;
    const Vec3 tpos = target ? target->position() : Vec3{};
    switch (tlIdx) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: break;
        case 9: g.se3d(0x136, tpos); statusMask = 1; break;
        case 10: g.se3d(0x136, tpos); statusMask = 2; break;
        case 0xb: g.se3d(0x136, tpos); statusMask = 4; break;
        case 0xc: g.se3d(0x136, tpos); statusMask = 8; break;
        case 0xd: g.se3d(0x136, tpos); statusMask = 0x40; break;
        case 0xe: g.se3d(0x136, tpos); statusMask = 0x10; break;
        case 0xf: g.se3d(0x136, tpos); statusMask = 0x20; break;
        case 0x10: teleportRoomContents(sys, user, target); return;
        case 0x11: reactMask = 0x800; break;
        case 0x12: statusMask = 0x1000; break;
        case 0x13: statusMask = 0x400; break;
        case 0x14: g.se3d(0x50, tpos); inv(sys).rotClass(kClsMT); statusMask = 0x200; break;
        case 0x15: g.se3d(0x136, tpos); inv(sys).rotClass(kClsMT); inv(sys).rotClass(kClsHT); g.effect(0x79, tpos); break;
        case 0x16: reactMask = 0x100; break;
        case 0x17: killNearestEnemy(sys, user, target); return;
        case 0x18: healEveryone(sys, user, target); return;
        case 0x19: if (user) user->hpVtMaxShift(); statusMask = 0x80; break;
        case 0x1a: if (user && user->isPlayer()) g.addCounter(6, 1); torturerSpiral(sys, target); return;   // FUN_00179570(0x53c5c0, 6, 1) [V a2 = 1 at 0x1e0ffc]
        case 0x1b: torturerField(sys, static_cast<int>(sys.rand(0x1a)), user, target); return;
        default: return;
    }
    char tag[8];
    std::snprintf(tag, sizeof tag, "TL%02d", tlIdx);   // 0x464670
    const uint32_t tagId = static_cast<uint32_t>(static_cast<uint8_t>(tag[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[1])) << 8) |
                           (static_cast<uint32_t>(static_cast<uint8_t>(tag[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[3])) << 24);
    g.spawnTrap(tpos, sys.word(kClsTL, tlIdx, 0x74), static_cast<uint32_t>(sys.word(kClsTL, tlIdx, 0x70)), statusMask, reactMask,
                sys.word(kClsTL, tlIdx, 0x78), user, tagId);
}

namespace {
// TL direct-use variants (0x1e18e0 .. 0x1e2f00). `item.flags & 0x10` = reversed torturer.
bool tlHpVt(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e18e0
    static int seCooldown = 0;   // iRam0053dcd0
    int v;
    if (t.isPlayer()) v = sys.word(item, (item.flags & kItemReversed) ? 0x7c : 0x78);
    else v = sys.word(item, 0x80);
    t.addHp(v); t.addVt(v);
    if (seCooldown == 0) { seCooldown = 0x3c; sys.game->se3d(v < 0 ? 0x8d : 0x8c, t.position()); }
    else seCooldown--;
    return true;
}
bool tlDamage(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e19f0
    if ((item.flags & kItemReversed) && t.isPlayer()) return false;
    GameAccess& g = *sys.game;
    g.se3d(0xb8, t.position());
    // FUN_00112190(0, 0x8a, pos, (0, rand(0x167), 0), (100, 100, 100, 1))
    const float rot[3] = {0.0f, static_cast<float>(static_cast<int>(sys.rand(0x167))), 0.0f};
    g.effect(0x8a, item.pos, 100.0f, rot);
    const int power = sys.word(item, t.isPlayer() ? 0x78 : 0x80);
    // FUN_00136170(status, 0, base = PRM +0x78 / +0x80, m1 = PRM +0x70) (the base / element order of 0x1e1aa0)
    const int dmg = t.projectileDamage(nullptr, power, static_cast<uint32_t>(sys.word(item, 0x70)));
    if (t.applyDamage(nullptr, dmg)) t.die();
    else t.react(0, item.pos, false);
    return true;
}
bool tlRotOne(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e1bd0
    if (!t.isPlayer()) return true;
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    bool any = false;
    if (!(item.flags & kItemReversed)) {
        int i = static_cast<int>(sys.rand(static_cast<uint32_t>(v.count - 1)));
        for (int n = v.count; n != 0; --n) {
            Item* x = v.slots[i];
            if ((x->cls == kClsHT || x->cls == kClsMT) && !(x->flags & kItemRotten) && !(x->flags & kItemSealed) && x->id != kIdHT02 && x->id != kIdMT03) {
                x->flags |= kItemRotten;
                g.message(0x254, itemDisplayName(*x, sys));      // "%s has rotted."
                any = true;
                break;
            }
            if (++i >= v.count) i = 0;
        }
        if (any) g.se(0x50); else g.message(0x28c);
    } else {
        int i = static_cast<int>(sys.rand(static_cast<uint32_t>(v.count - 1)));
        for (int n = v.count; n != 0; --n) {
            Item* x = v.slots[i];
            if ((x->cls == kClsHT || x->cls == kClsMT) && (x->flags & kItemRotten)) {
                x->flags &= ~kItemRotten;
                g.message(g.language() == 1 ? 0x24c : 0x2ff, itemDisplayName(*x, sys));   // "%s revitalized."
                any = true;
                break;
            }
            if (++i >= v.count) i = 0;
        }
        if (any) g.se(0x8b); else g.message(0x289);
    }
    return true;
}
bool tlKnockback(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e1f90
    sys.game->effect(0x78, item.pos);
    t.react(0x800, item.pos, t.isPlayer() && (item.flags & kItemReversed) != 0);
    return true;
}
bool tlWarp(Item& item, CharacterAccess& t) {   // orig FUN_001e2060
    if ((item.flags & kItemReversed) && t.isPlayer()) return false;
    t.inflict(0x1000);
    return true;
}
bool tlWeaken(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e20c0
    GameAccess& g = *sys.game;
    if (!t.isPlayer()) {
        const int d = sys.word(item, 0x80);
        t.addAtk(d); t.addDef(d);
        return true;
    }
    Inventory& v = inv(sys);
    bool any = false;
    if (!(item.flags & kItemReversed)) {
        const int d = sys.word(item, 0x78);
        for (int i = 0; i < v.count; ++i) {
            Item* x = v.slots[i];
            if (x->cls == kClsSD && !(x->flags & kItemSealed)) { addAtkMod(*x, d); g.message(0x255, itemDisplayName(*x, sys)); any = true; }
            else if (x->cls == kClsCT && !(x->flags & kItemSealed)) { addDefMod(*x, d); g.message(0x255, itemDisplayName(*x, sys)); any = true; }
        }
        if (any) g.se(0x91); else g.message(0x28d);
    } else {
        const int d = sys.word(item, 0x7c);
        for (int i = 0; i < v.count; ++i) {
            Item* x = v.slots[i];
            if (x->cls == kClsSD && x->atkMod < 0) { addAtkMod(*x, d); g.message(0x24b, itemDisplayName(*x, sys)); any = true; }
            else if (x->cls == kClsCT && x->defMod < 0) { addDefMod(*x, d); g.message(0x24b, itemDisplayName(*x, sys)); any = true; }
        }
        if (any) g.se(0x90); else g.message(0x289);
    }
    return true;
}
bool tlTransmute(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e23c0
    if (!t.isPlayer()) return true;
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    int i = static_cast<int>(sys.rand(static_cast<uint32_t>(v.count - 1)));
    if (!(item.flags & kItemReversed)) {
        for (int n = v.count; n != 0; --n) {
            Item* x = v.slots[i];
            if (x->cls != kClsHT && x->cls != kClsMT) {
                g.message(0x249, itemDisplayName(*x, sys));
                v.replaceItem(i, kFoodTable[sys.rand(6)], sys);
                g.message(600, itemDisplayName(*x, sys));       // "turned into %s."
                g.se(0x55);
                return true;
            }
            if (++i >= v.count) i = 0;
        }
    } else {
        for (int n = v.count; n != 0; --n) {
            Item* x = v.slots[i];
            if (x->cls == kClsHT || x->cls == kClsMT) {
                const uint32_t id = weightedItemId(sys, kFoodToClass[sys.rand(9)]);
                if (id != 0) {
                    g.message(0x249, itemDisplayName(*x, sys));
                    v.replaceItem(i, id, sys);
                    g.message(600, itemDisplayName(*x, sys));
                    g.se(0x8a);
                    return true;
                }
            }
            if (++i >= v.count) i = 0;
        }
    }
    return true;
}
bool tlDrop(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e2870
    if (!t.isPlayer()) return true;
    GameAccess& g = *sys.game;
    if (t.brand() == kIdST16) { t.invincibleFlash(); return true; }
    Inventory& v = inv(sys);
    int n = sys.word(item, (item.flags & kItemReversed) ? 0x7c : 0x78);
    int i = v.randomUnequipped(sys);
    if (i < 0) { g.message(0x28a); return true; }
    do {
        if (n == 0) break;
        n--;
        Item* x = v.remove(i, sys);
        if (x) {
            g.itemEnterWorld(*x);                        // FUN_001d3a30
            x->pos = t.position();                       // player position, y + 100
            x->pos.y = x->pos.y + 100.0f;
            itemRequestState(*x, ItemState::Dropped);    // LAB_001e2a70 -> 0x1e34c0
        }
        i = v.randomUnequipped(sys);
    } while (i >= 0);
    g.se(0x56);
    return true;
}
bool tlVanish(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e2a80
    if (!t.isPlayer()) return true;
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    if (!(item.flags & kItemReversed)) {
        const int i = v.randomUnequipped(sys);
        if (i >= 0) {
            Item* x = v.remove(i, sys);
            if (x) {
                g.se(0x70);
                g.message(0x251, itemDisplayName(*x, sys));      // "%s disappeared."
                g.freeItem(x);
            }
        }
    } else if (v.count == Inventory::kMaxItems) {
        g.message(0x2b9);
    } else {
        const uint32_t id = weightedItemId(sys, randomFloorItemClass(sys));
        if (id != 0) {
            Item* x = g.createItem(id, false);
            if (x) {
                v.add(x, sys);
                g.se(0x51);
                g.message(0x24f, itemDisplayName(*x, sys));      // "%s appeared."
            }
        }
    }
    return true;
}
bool tlRemoveBrand(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e2c20
    if (item.flags & kItemReversed) return false;
    if (!t.isPlayer()) return true;
    GameAccess& g = *sys.game;
    // FUN_00112190(-20.0 tag in f12 (0xc1a00000), 0x8d, joint "head" (0x4646f8 [V]) world position, 0, (100, 100, 100, 1))
    Vec3 at = headPos(t);
    t.jointPosition("head", at);
    g.effect(0x8d, at, 100.0f, nullptr, -20.0f);
    if (t.brand() == 0) { g.message(0x28b); return true; }
    g.se(0x68);
    g.message(0x24a, sys.name(kClsST, itemIndexOf(t.brand())));
    t.setBrand(0);
    return true;
}
bool tlReverseDisks(ItemSystem& sys, Item& item) {   // orig FUN_001e2de0
    GameAccess& g = *sys.game;
    const int n = g.floorItemCount();
    for (int i = 0; i < n; ++i) {
        Item* x = g.floorItem(i);
        if (!x || !(x->taskFlags & 1) || x->removed() || x == &item || x->cls != kClsTP) continue;
        // FUN_001d4d70: flip by pi about the forward axis then re-evaluate the face (FUN_001d4db0)
        x->up.x = -x->up.x; x->up.y = -x->up.y; x->up.z = -x->up.z;
        if (x->up.y < 0.0f) x->flags |= kItemReversed; else x->flags &= ~kItemReversed;
    }
    g.message(0x296);
    g.se(0x6f);
    return true;
}
bool tlMaxUp(ItemSystem& sys, Item& item, CharacterAccess& t) {   // orig FUN_001e2f00
    GameAccess& g = *sys.game;
    if (!t.isPlayer()) { t.addHp(t.hpMaxTotal()); return true; }
    if (!(item.flags & kItemReversed)) {
        g.effect(0x8f, iconPos(t));
        if (t.hpMaxTotal() < 999) { t.addHpMax(sys.word(item, 0x78)); g.message(0x27e, sys.word(item, 0x78)); }
        else g.message(0x2b9);
    } else {
        g.effect(0x90, iconPos(t));
        if (t.vtMaxTotal() < 999) { t.addVtMax(sys.word(item, 0x7c)); g.message(0x286, sys.word(item, 0x7c)); }
        else g.message(0x2b9);
    }
    g.se(0x8c);
    return true;
}
}  // namespace

// orig FUN_001e3110 (work/decompiled/001e3110.c): reads PRMITEM09 (TP, iRam0053d884) rows by item.idx
bool diskEffect(ItemSystem& sys, Item& item, CharacterAccess& target) {
    GameAccess& g = *sys.game;
    uint32_t cureMask = 0;
    switch (item.idx) {
        case 0: return tlHpVt(sys, item, target);
        case 1: return tlDamage(sys, item, target);
        case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: break;
        case 0xb: cureMask = 2; break;
        case 0xc: cureMask = 8; break;
        case 0xd: cureMask = 0x40; break;
        case 0xe: cureMask = 0x7f; break;
        case 0xf: return tlRotOne(sys, item, target);
        case 0x10: return tlKnockback(sys, item, target);
        case 0x11: return tlWarp(item, target);
        case 0x12: return tlWeaken(sys, item, target);
        case 0x13: return tlTransmute(sys, item, target);
        case 0x14: return tlDrop(sys, item, target);
        case 0x15: return tlVanish(sys, item, target);
        case 0x16: return tlRemoveBrand(sys, item, target);
        case 0x17: return tlReverseDisks(sys, item);
        case 0x18: return tlMaxUp(sys, item, target);
        case 0x19: cureMask = 0x10; break;
        default: return false;
    }
    if (!(item.flags & kItemReversed) || !target.isPlayer()) {
        // not reversed (or hitting a creature): lay the field of this torturer with the status as its cure mask
        char tag[8];
        std::snprintf(tag, sizeof tag, "TP%02d", item.idx);   // 0x464700 (sic: the TP tag string is used)
        const uint32_t tagId = static_cast<uint32_t>(static_cast<uint8_t>(tag[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[1])) << 8) |
                               (static_cast<uint32_t>(static_cast<uint8_t>(tag[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(tag[3])) << 24);
        const int effectId = sys.word(item, target.isPlayer() ? 0x78 : 0x80);
        g.spawnTrap(item.pos, sys.word(item, 0x74), static_cast<uint32_t>(sys.word(item, 0x70)), cureMask, 0, effectId, &target, tagId);
        return true;
    }
    if (cureMask == 0) return false;
    if (item.idx == 0xe) g.effect(0x8c, item.pos);
    const uint32_t cured = target.cure(cureMask);
    if (cured & 1) g.message(0x299);
    if (cured & 2) g.message(0x2ba);
    if (cured & 4) g.message(0x2dd);
    if (cured & 8) g.message(0x2a1);
    if (cured & 0x10) g.message(0x2f5);
    if (cured & 0x20) g.message(0x2d5);
    if (cured & 0x40) g.message(0x2e9);
    return cured != 0;
}

// orig FUN_001e0050 (work/decompiled/001e0050.c): send every enemy and item of the room elsewhere (effect 0x77)
void teleportRoomContents(ItemSystem& sys, CharacterAccess* user, CharacterAccess* target) {
    GameAccess& g = *sys.game;
    if (!target) return;
    const int room = g.roomAt(target->position());
    if (room < 0 || g.roomIsSealed(room)) return;
    if (!user || !user->canWarp()) return;
    g.se3d(0x6a, target->position());
    const int nc = g.characterCount();
    for (int i = 0; i < nc; ++i) {
        CharacterAccess* c = g.character(i);
        if (!c || !c->aiAlertable() || c->actorType() != 2 || c == user) continue;
        if (!c->canWarp() || c->isRemoved()) continue;
        if (!c->isAlive()) continue;
        // 'E19' creatures only when their AI accepts event 0x1000008
        if ((c->charId() & 0xffffff) == (itemId("E19\0") & 0xffffff) && !c->aiAccepts(0x1000008)) continue;
        if (c->roomIndex() == room) continue;
        Vec3 out;
        if (g.randomCellInRoom(room, out)) {
            c->setPosition(out);
            g.effect(0x77, iconPos(*c));
        }
    }
    const int ni = g.floorItemCount();
    for (int i = 0; i < ni; ++i) {
        Item* it = g.floorItem(i);
        if (!it || !(it->taskFlags & 1) || it->removed()) continue;
        const int ir = g.roomAt(it->pos);
        if (ir < 0 || ir == room) continue;
        Vec3 out;
        if (g.randomCellInRoom(room, out)) {
            it->pos = out;
            g.effect(0x77, out);
            if (it->cls == kClsIS || it->cls == kClsFlesh) it->state.param0 = static_cast<int>(out.y + 120.0f);   // lying state rest height
        }
    }
}

// orig FUN_001e05b0 (work/decompiled/001e05b0.c)
void killNearestEnemy(ItemSystem& sys, CharacterAccess* user, CharacterAccess* target) {
    GameAccess& g = *sys.game;
    if (!target) return;
    g.se3d(0x94, target->position());
    const int room = g.roomAt(target->position());
    const float range = room >= 0 ? -1.0f : 600.0f;
    bool done = false;
    const int nc = g.characterCount();
    for (int i = 0; i < nc; ++i) {
        CharacterAccess* c = g.character(i);
        if (!c || !c->aiAlertable() || c->actorType() != 2 || c == user || c->isRemoved() || !c->isAlive()) continue;
        bool inRange;
        if (range < 0.0f) inRange = c->roomIndex() == room;
        else {
            const Vec3 d{c->position().x - target->position().x, c->position().y - target->position().y, c->position().z - target->position().z};
            const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            inRange = dist - c->radius() <= range;
        }
        if (!inRange) continue;
        if (!done && c->applyDamage(user, 99999)) c->die();
        done = !done;
    }
}

// orig FUN_001e08a0 (work/decompiled/001e08a0.c): heal by TL24 +0x7c (0xcdc into PRMITEM08)
void healEveryone(ItemSystem& sys, CharacterAccess* user, CharacterAccess* target) {
    GameAccess& g = *sys.game;
    if (!target) return;
    g.se3d(0x8c, target->position());
    const int heal = sys.word(kClsTL, 0x18, 0x7c);
    const int room = g.roomAt(target->position());
    const float range = room >= 0 ? -1.0f : 600.0f;
    const int nc = g.characterCount();
    for (int i = 0; i < nc; ++i) {
        CharacterAccess* c = g.character(i);
        if (!c || !c->aiAlertable() || c->actorType() != 2 || c == user || c->isRemoved() || !c->isAlive()) continue;
        bool inRange;
        if (range < 0.0f) inRange = c->roomIndex() == room;
        else {
            const Vec3 d{c->position().x - target->position().x, c->position().y - target->position().y, c->position().z - target->position().z};
            inRange = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z) - c->radius() <= range;
        }
        if (!inRange) continue;
        c->addHp(heal);
        g.effect(0x7a, iconPos(*c));
    }
    CharacterAccess& p = g.player();
    p.addHp(heal);
    g.effect(0x7a, iconPos(p));
}

// orig FUN_001e0c40 (work/decompiled/001e0c40.c)
void torturerSpiral(ItemSystem& sys, CharacterAccess* target) {
    GameAccess& g = *sys.game;
    g.setFloorFlags(0x1000000);
    if (!target) return;
    g.se3d(0x6c, target->position());
    (void)sys.rand(0x167);                 // random yaw of the effect
    g.effect(0x7c, target->position());
}

// orig FUN_001e3980 (work/decompiled/001e3980.c)
void useBone(ItemSystem& sys, int idx) {
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    g.setFlag(0x2be);
    g.setAchievementFlag(0x45f);
    Item* it = v.slots[idx];
    identifyOnUse(sys, *it);
    CharacterAccess& p = player(sys);
    const int bi = it->idx;
    p.addVt(10);
    if (bi != 0x16 && bi != 0x11 && bi != 2 && bi != 1 && bi != 0) p.addHp(10);
    int trapKind = -1, trapEffect = 0;
    const int v70 = sys.word(*it, 0x70), v74 = sys.word(*it, 0x74);
    switch (bi) {
        case 0: case 1: case 2:
            p.addHp(v74);
            g.message(0x2bf, v74 < 0 ? -v74 : v74);
            g.se(0x73);
            break;
        case 3: {
            trapKind = 1; trapEffect = 0x52;
            p.hitElementFx(static_cast<uint32_t>(v70));            // FUN_0014c580(player, 0, +0x70, 0)
            const int dmg = p.projectileDamage(&p, 0x52, static_cast<uint32_t>(v70));   // FUN_00136170
            if (p.applyDamage(nullptr, dmg)) { g.setGuardedFlag(0x475); p.die(); }
            else p.react(0, p.position(), false);
            break;
        }
        case 4: if (p.inflict(0x4000)) g.message(0x2af); break;
        case 5: if (p.inflict(0x8000)) g.message(0x2b8); break;
        case 6: if (!p.inflict(1)) g.message(0x29b); break;
        case 7: if (!p.inflict(2)) g.message(700); break;
        case 8: if (!p.inflict(4)) g.message(0x2e0); break;
        case 9: if (!p.inflict(8)) g.message(0x2a3); break;
        case 10: if (!p.inflict(0x40)) g.message(0x2ed); break;
        case 0xb: if (!p.inflict(0x10)) g.message(0x2f7); break;
        case 0xc: if (!p.inflict(0x20)) g.message(0x2d7); break;
        case 0xd: {
            const uint32_t mask = p.statusMask();
            if (!p.inflict(0x40)) g.message(0x2ed);
            else { if (!(mask & 0x40)) g.message(0x2eb); g.message(0x26e); }
            break;
        }
        case 0xe: {
            bool warped = false;
            // cell pointer +0x30 non-null and uRam0050a360 >= 0; a non-room cell compares as 0xffffffff (uVar13's
            // initial value), i.e. always "elsewhere"
            if (p.hasCell() && g.targetCellRoom() >= 0) {
                if (p.roomIndex() != g.targetCellRoom() && p.inflict(0x1000)) { g.closeItemScreen(); warped = true; }
            }
            if (!warped) g.message(0x2b9);
            break;
        }
        case 0xf: if (p.inflict(0x10000)) g.message(0x2f1); break;
        case 0x10: p.inflict(0x40000); break;
        case 0x11: p.inflict(0x100); break;
        case 0x12: g.spawnFieldObject(p.position()); g.closeItemScreen(); break;
        case 0x13: p.inflict(0x20000); g.closeItemScreen(); break;
        case 0x14: trapKind = 2; trapEffect = 0x5a; break;
        case 0x15: {
            Vec3 out;
            if ((g.floorFlags() & 0x100000) && g.randomCellInRoom(g.targetCellRoom(), out) && p.canWarp()) {
                g.effect(0x83, headPos(p));
                p.setPosition(out);
                g.se(0x45);
                g.closeItemScreen();
            } else {
                g.message(0x2b9);
            }
            break;
        }
        case 0x16:
            p.addHp(v74);
            g.message(0x2bf, v74 < 0 ? -v74 : v74);
            v.identifyAll(sys);
            g.se(0x73); g.se(0x54);
            break;
        default: break;
    }
    if (trapKind >= 0) {
        g.spawnTrap(p.position(), trapKind, static_cast<uint32_t>(v70), 0, 0, trapEffect, &p, 0);
        g.closeItemScreen();
    }
    it->count--;
    if (it->count == 0) {
        Item* rem = v.remove(v.indexOf(it), sys);
        if (rem) g.freeItem(rem);
    }
    if (p.hp() == 0) { p.die(); g.closeItemScreen(); }
}

// orig FUN_001e46f0 (work/decompiled/001e46f0.c)
void mergeParasiteLevels(Item& parasite, Item& host, int cap, ItemSystem& sys) {
    int n = parasite.count + host.count;
    if (cap < n) n = cap;
    host.count = n;
    messageName(sys, itemDisplayName(parasite, sys));
    sys.game->message(0x2fd, itemDisplayName(host, sys));
    Inventory& v = inv(sys);
    v.remove(v.indexOf(&parasite), sys);
    sys.game->freeItem(&parasite);
}

// orig FUN_001e4800 (work/decompiled/001e4800.c)
void useParasite(ItemSystem& sys, int idx, int targetIdx) {
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    g.setFlag(0x2c0);
    Item* it = v.slots[idx];
    identifyOnUse(sys, *it);
    Item* replaced = nullptr;
    if (targetIdx < 0) {
        g.se(0x78);
        CharacterAccess& p = player(sys);
        replaced = p.parasite();
        p.setParasite(it);
        messageName(sys, itemDisplayName(*it, sys));
        g.message(0x2fd, g.str(0x1c7));             // "Parasitize to\n%s." "Yourself"
    } else {
        g.se(0x77);
        Item* t = v.slots[targetIdx];
        replaced = t->parasite;
        if (t->cls == kClsWM) {
            g.setFlag(0x2c1);
            if (t->idx == 0x13 && it->idx != 0x14) {        // the target is a Cannibal: it eats the used parasite
                const bool grew = cannibalEat(*t, *it, sys);
                if (it->idx == 0x11 && !t->identified()) {   // Appraisal parasite identifies its eater
                    t->flags |= kItemIdentified;
                    g.addCounter(0xc, 1);
                    g.message(0x2e5);
                    g.message(0x256, sys.name(t->cls, t->idx));
                }
                g.message(0x25d, sys.name(it->cls, it->idx));   // "Ate %s."
                if (!grew) { g.message(0x2fb, t->count); if (g.language() == 1) g.message(0x293); }
                else g.message(0x2fc);
                v.remove(idx, sys);
                g.freeItem(it);
                g.se(0x7b);
                return;
            }
        }
        switch (it->idx) {
            case 0x13: {                                     // Cannibal used on a parasite
                const bool grew = cannibalEat(*it, *t, sys);
                g.message(0x25d, sys.name(t->cls, t->idx));
                if (!grew) { g.message(0x2fb, it->count); if (g.language() == 1) g.message(0x293); }
                else g.message(0x2fc);
                v.remove(targetIdx, sys);
                g.freeItem(t);
                g.se(0x7b);
                return;
            }
            case 0x15:                                       // Resurrect
                parasiteRevive(*t);
                v.remove(idx, sys); g.freeItem(it);
                g.se(0x7d); g.message(0x2d9); g.message(0x2e8);
                return;
            case 0x14: {                                     // Parasite Box: both vanish
                v.unequipSlotRefresh(v.equippedSlotOfIndex(targetIdx), sys);
                v.remove(targetIdx, sys); g.freeItem(t);
                v.remove(v.indexOf(it), sys); g.freeItem(it);
                g.se(0x7c);
                return;
            }
            case 0x12:                                       // Disarm
                t->flags |= kItemDisarmed;
                v.remove(idx, sys); g.freeItem(it);
                g.message(0x2e3); g.message(0x2d0);
                return;
            case 0x11:                                       // Appraisal
                if (!t->identified()) {
                    t->flags |= kItemIdentified;
                    g.addCounter(0xc, 1);
                    g.message(0x2e5);
                    g.message(0x256, sys.name(t->cls, t->idx));
                }
                v.remove(idx, sys); g.freeItem(it);
                return;
            case 7: case 6: case 5: case 4: {                // HP/VT/ATK/DEF parasites merge with the same kind
                static const uint32_t kKind[4] = {kIdWM04, kIdWM05, kIdWM06, kIdWM07};
                static const int kCap[4] = {999, 999, 99, 99};
                const int k = it->idx - 4;
                if (t->id == kKind[k]) { mergeParasiteLevels(*it, *t, kCap[k], sys); return; }
                if (replaced && replaced->id == kKind[k]) { mergeParasiteLevels(*it, *replaced, kCap[k], sys); return; }
                t->parasite = it;
                break;
            }
            default: t->parasite = it; break;
        }
        messageName(sys, itemDisplayName(*it, sys));
        g.message(0x2fd, itemDisplayName(*t, sys));
    }
    v.remove(idx, sys);
    if (replaced) {
        parasiteDie(*replaced);
        v.add(replaced, sys);
        g.se(0x79);
        g.message(0x2da);                                    // "Dead Parasite appeared."
    }
}

// orig FUN_001d4910 (work/decompiled/001d4910.c)
bool fuseCrystal(Item& sword, Item& crystal, ItemSystem& sys) {
    const uint32_t newSword = swordOfCrystal(crystal.id);
    if (newSword == 0) return false;
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    const uint32_t oldCrystal = sword.crystalId;
    const uint32_t oldFlags = sword.crystalFlags;
    changeItemId(sword, newSword, sys);
    sword.crystalId = crystal.id;
    sword.crystalFlags = crystal.flags;
    v.remove(v.indexOf(&crystal), sys);
    g.freeItem(&crystal);
    if (oldCrystal != 0) {
        Item* back = g.createItem(oldCrystal, false);
        if (back) { v.add(back, sys); back->flags = oldFlags; }
    }
    return true;
}

// orig FUN_001e5320 (work/decompiled/001e5320.c)
void useCrystal(ItemSystem& sys, int idx, int targetIdx) {
    GameAccess& g = *sys.game;
    Inventory& v = inv(sys);
    g.setFlag(0x2aa);
    Item* crystal = v.slots[idx];
    Item* sword = v.slots[targetIdx];
    identifyOnUse(sys, *crystal);
    if (g.language() == 1) {
        messageName(sys, sys.name(crystal->cls, crystal->idx));
        g.message(0x248, sys.name(sword->cls, sword->idx));
        g.message(0x295);
    } else if (g.language() == 0) {
        messageName(sys, sys.name(crystal->cls, crystal->idx));
        g.message(0x295);
        g.message(0x248, sys.name(sword->cls, sword->idx));
    }
    const int slot = v.equippedSlotOfIndex(targetIdx);
    if (slot != -1) v.unequipSlotRefresh(slot, sys);
    fuseCrystal(*sword, *crystal, sys);
    const int si = v.indexOf(sword);
    if (slot != -1) v.equipItem(si, true, sys);
    if (g.language() == 1) { g.message(0x248, itemDisplayName(*sword, sys)); g.message(0x294); }
    else if (g.language() == 0) { g.message(0x294); g.message(0x248, itemDisplayName(*sword, sys)); }
}

// ---- throw / step effects -----------------------------------------------------------------------

// orig FUN_001d1340 (work/decompiled/001d1340.c)
void throwEffect(ItemSystem& sys, Item& item, CharacterAccess& target, CharacterAccess* thrower) {
    if ((target.actorType() == 0 || target.actorType() == 2) && !target.isAlive()) return;
    switch (item.cls) {
        case kClsMT: fleshThrowEffect(sys, item, target); break;
        case kClsBX: boxThrowEffect(sys, item, target, thrower); break;
        case kClsLQ: liquidThrowEffect(sys, item, target); break;
        case kClsST: brandThrowEffect(sys, item, target); break;
        case kClsTL: torturerDeport(sys, thrower, &target, item); break;   // FUN_001e1720 -> FUN_001e1110(target, thrower, item)
        case kClsBN: boneThrowEffect(sys, item, target); break;
        case kClsWM: parasiteThrowEffect(sys, item, target); break;
        default: break;
    }
}

// orig FUN_001dcb50 (work/decompiled/001dcb50.c)
void fleshThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target) {
    int heal = 999;
    if (item.idx == 1) heal = 99;
    else if (item.idx == 0) heal = 0x28;
    target.addHp(heal);
    sys.game->message(0x27d, heal);
}

// orig FUN_001ddaa0 (work/decompiled/001ddaa0.c)
void boxThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target, CharacterAccess* thrower) {
    GameAccess& g = *sys.game;
    if (sys.rand(0xf) < static_cast<uint32_t>(sys.word(item, 0x7c))) {
        int kind = 0;
        if (item.idx == 10) { const uint32_t r = sys.rand(99); if (r > 0x18) kind = r < 0x41 ? 1 : 2; }
        g.spawnTrap(target.position(), kind, 1, 0, 0, sys.word(item, 0x80), thrower, 0);
    }
    if (item.idx == 0x12) { target.addHp(-(target.hp() >> 1)); g.message(0x2be); }
    else if (item.idx == 0x10) { target.addDef(sys.word(item, 0x88)); g.message(0x275, sys.word(item, 0x88)); }
}

// orig FUN_001df520 (work/decompiled/001df520.c)
void liquidThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target) {
    GameAccess& g = *sys.game;
    const int v = sys.word(item, 0x7c);
    const Vec3 tp = target.position();
    switch (item.idx) {
        case 0: target.addHp(target.hpMaxTotal()); g.message(0x280); g.se3d(0x8c, tp); return;
        case 1: case 4: case 8: case 0x17:
            target.addAtk(v);
            if (v < 0) { g.message(0x26d, -v); g.se3d(0x91, tp); } else { g.message(0x26c, v); g.se3d(0x90, tp); }
            break;
        case 2: case 5: case 9:
            target.addDef(v);
            if (v < 0) { g.message(0x275, -v); g.se3d(0x91, tp); } else { g.message(0x274, v); g.se3d(0x90, tp); }
            break;
        case 3: case 6: case 7: case 10: case 0xb:
            target.addAtk(v); target.addDef(v);
            if (v < 0) { g.message(0x26d, -v); g.message(0x275, -v); g.se3d(0x91, tp); }
            else { g.message(0x26c, v); g.message(0x274, v); g.se3d(0x90, tp); }
            break;
        case 0x11: target.addHp(target.hpMaxTotal()); g.message(0x29f); g.se3d(0x8c, tp); break;
        case 0x12: g.message(0x2bd, target.hp()); g.se3d(0x54, tp); break;
        case 0x16: {
            const uint32_t id3 = target.charId() & 0xffffff;
            if (id3 == (itemId("E16\0") & 0xffffff) || id3 == (itemId("E19\0") & 0xffffff)) { g.message(0x2b9); break; }
            Item* clone = g.createItem(target.charId(), true);   // the creature id creates its flesh (class 0xe)
            if (clone) { clone->pos = tp; g.se3d(0x8a, tp); }
            break;
        }
        case 0x18:
            if (item.count != 0) {
                Item* stored = g.createItem(static_cast<uint32_t>(item.count), true);
                if (stored) {
                    dropItemAtActor(*stored, -1.0f, &target, sys);   // FUN_001d5410(-1 yaw, item, target)
                    g.removeCharacter(&target);                      // FUN_00207350(target)
                }
            }
            break;
        default: break;
    }
}

// orig FUN_001dff60 (work/decompiled/001dff60.c)
void brandThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target) {
    GameAccess& g = *sys.game;
    if (item.idx == 0x15) { target.addHp(target.hpMaxTotal()); g.message(0x280); }
    else if (item.idx == 0x11) g.spawnTrap(target.position(), 0, 1, 0, 0, sys.word(kClsST, 0x11, 0x7c), &target, 0);
    else if (item.idx == 4) g.message(0x2bd, target.hp());
}

// orig FUN_001e41e0 (work/decompiled/001e41e0.c)
void boneThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target) {
    GameAccess& g = *sys.game;
    if (item.idx == 0x16) { g.message(0x2bd, target.hp()); g.se3d(0x54, target.position()); }
    else if (item.idx == 0x15) {
        Vec3 out;
        if ((g.floorFlags() & 0x100000) && g.randomCellInRoom(g.targetCellRoom(), out) && target.canWarp()) {
            g.se3d(0x45, target.position());
            g.effect(0x83, headPos(target));
            target.setPosition(out);
        } else {
            g.message(0x2b9);
        }
    } else if (item.idx == 0x12) {
        g.spawnFieldObject(target.position());
    } else if (item.idx == 0xe && target.inflict(0x1000) == 0) {
        g.message(0x2b9);
    }
}

// orig FUN_001e5220 (work/decompiled/001e5220.c)
void parasiteThrowEffect(ItemSystem& sys, Item& item, CharacterAccess& target) {
    if (item.idx == 0x11) { sys.game->message(0x2bd, target.hp()); sys.game->se3d(0x54, target.position()); }
}

// orig FUN_001d0d30 (work/decompiled/001d0d30.c)
int throwHitMask(ItemSystem& sys, const Item& item, uint32_t& statusMask, uint32_t& reactMask) {
    statusMask = 0;
    reactMask = 0;
    int hit = 0;
    const uint32_t roll = sys.rand(99);
    if (item.cls > kClsWG) {   // FUN_001d2e80 == 0: hard-coded mappings
        if (item.cls == kClsWM) {
            static const uint32_t m[7] = {1, 2, 4, 8, 0x40, 0x10, 0x20};
            if (item.idx >= 9 && item.idx <= 0xf) statusMask |= m[item.idx - 9];
        } else if (item.cls == kClsBN) {
            switch (item.idx) {
                case 1: hit = 1; break;
                case 3: reactMask |= 1; break;
                case 6: statusMask |= 1; break;
                case 7: statusMask |= 2; break;
                case 8: statusMask |= 4; break;
                case 9: statusMask |= 8; break;
                case 10: statusMask |= 0x40; break;
                case 0xb: statusMask |= 0x10; break;
                case 0xc: statusMask |= 0x20; break;
                case 0xd: statusMask |= 0x200; break;
                case 0xf: statusMask |= 0x10000; break;
                case 0x11: statusMask |= 0x100; break;
                default: break;
            }
        } else if (item.cls == kClsST) {
            if (item.idx == 0x14) statusMask |= 0x10000;
            else if (item.idx == 0x13) statusMask |= 0x10;
            else if (item.idx == 0x11) reactMask |= 1;
            else if (item.idx == 0xc && roll < static_cast<uint32_t>(sys.word(kClsST, 0xc, 0x7c))) reactMask |= 0x800;
        } else if (item.cls == kClsLQ) {
            if (item.idx == 0x15) statusMask |= 0x80;
            else if (item.idx == 0x10 || (item.idx >= 0xc && item.idx <= 0xf)) statusMask |= 1u << (static_cast<uint32_t>(item.count) & 0x1f);
            else return 0;
        } else if (item.cls == kClsMT && (item.flags & kItemRotten)) {
            statusMask |= 0x40;
        }
        return hit;
    }
    const data::PrmItem* r = sys.row(item);
    if (!r) return 0;
    for (int slot = 0; slot < 2 && slot < r->effectCount; ++slot) {
        const data::PrmItemEffect& e = r->effects[slot];
        if (e.type != 4) continue;
        const uint32_t chance = static_cast<uint32_t>(e.count);
        if (e.kind == 0x14) { if (roll < chance) reactMask |= 1; }
        else if (e.kind == 0x1f) {
            if (roll < chance) {
                const uint32_t bits = static_cast<uint16_t>(e.value);
                uint32_t n = 0;
                static const uint32_t kMap[9][2] = {{1, 1}, {2, 2}, {4, 4}, {8, 8}, {0x10, 0x10}, {0x20, 0x20}, {0x40, 0x40}, {0x100, 0x80}, {0x200, 0x1000}};
                for (const auto& m : kMap) if (bits & m[0]) { n++; statusMask |= m[1]; }
                if (bits & 0x800) {   // "one random status of the set"
                    uint32_t pick = sys.rand(n - 1);
                    for (uint32_t bit = 1;; bit <<= 1) {
                        if (statusMask & bit) { if (pick == 0) { statusMask &= bit; break; } pick--; }
                        if (bit == 0) break;
                    }
                }
            }
        } else if (e.kind == 0x0f) { if (roll < chance) hit = 1; }
        else if (e.kind == 0x0e && roll < chance) reactMask |= 0x800;
    }
    return hit;
}

// orig FUN_001dcbc0 (work/decompiled/001dcbc0.c): BX21 alarm -> AI event 0x1000006 to every active enemy
void alarmBoxEffect(ItemSystem& sys) {
    GameAccess& g = *sys.game;
    g.se(0x1e);
    const int n = g.characterCount();
    for (int i = 0; i < n; ++i) {
        CharacterAccess* c = g.character(i);
        if (!c || c->isPlayer() || c->actorType() == 1) continue;
        if (c->aiAlertable() && !c->aiDisabled()) c->sendAiEvent(0x1000006);   // vt+0x20 == 1, ai +0x14 bit 0 clear
    }
}

// orig FUN_001d2f30 (work/decompiled/001d2f30.c)
// The weights are the FDT bytes 8..0x13 of the loaded floor, which the flag cases rewrite in place (persisting for the
// floor), so the one translation of the routine (Dungeon::pickItemCategory, game/dungeon.cpp) is used.
int randomFloorItemClass(ItemSystem& sys) { return sys.game->floorItemCategory(); }

// orig FUN_001d30c0 (work/decompiled/001d30c0.c)
uint32_t weightedItemId(ItemSystem& sys, int cls) {
    if (cls == -1) return 0;
    int r = static_cast<int>(sys.rand(0xff));
    const int n = sys.rowCount(cls);
    for (int i = 0; i < n; ++i) {
        const int w = dropWeight(sys, *sys.row(cls, i));
        if (r < w) return makeItemId(cls, i);
        r -= w;
    }
    return 0;
}

// orig FUN_001d31b0 (work/decompiled/001d31b0.c): one unseen item per class, then a random class among them
uint32_t anyUnseenItemId(ItemSystem& sys) {
    GameAccess& g = *sys.game;
    int pick[14];
    int classes = 0;
    for (int cls = 0; cls < 14; ++cls) {
        pick[cls] = -1;
        auto eligible = [&](int i) {
            if (cls == 0) return i != 0x1f && i != 0x22 && !g.itemSeen(makeItemId(cls, i));
            if (cls == 0xd) return (i < 7 || i > 10) && !g.itemSeen(makeItemId(cls, i));
            return !g.itemSeen(makeItemId(cls, i));
        };
        int n = 0;
        const int rows = sys.rowCount(cls);
        for (int i = 0; i < rows; ++i) if (eligible(i)) n++;
        if (n == 0) continue;
        int k = static_cast<int>(sys.rand(static_cast<uint32_t>(n - 1)));
        classes++;
        for (int i = 0; i < rows; ++i) {
            if (!eligible(i)) continue;
            if (k == 0) { pick[cls] = i; break; }
            k--;
        }
    }
    if (classes == 0) return 0;
    int k = static_cast<int>(sys.rand(static_cast<uint32_t>(classes - 1)));
    for (int cls = 0; cls < 14; ++cls) {
        if (pick[cls] == -1) continue;
        if (k == 0) return makeItemId(cls, pick[cls]);
        k--;
    }
    return 0;
}

}  // namespace baroque::game
