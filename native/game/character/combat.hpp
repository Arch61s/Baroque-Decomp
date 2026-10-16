#pragma once
// Combat (ENEMIES_COMBAT.md §4): hit manager 0x50c780 (hurt volumes + attack slots, per-tick overlap tests), the ACT
// hit commands, on-hit resolution FUN_0014c970, damage FUN_001354b0, application FUN_00136580 (+ EXP FUN_00138e40),
// status infliction FUN_00138660, hit FX/SE FUN_0014bc30, reactions FUN_0012a710 and the death request FUN_0012a5e0.
// The reaction/death states themselves live in character.cpp (state switch of FUN_00129a30).
// Item/equipment data (inventory 0x53d910) and visual effects (FUN_00112190) are reached through CombatHooks; with no
// hooks installed the code behaves like the original with an empty inventory (every RNG draw is still made).
#include "data/act.hpp"
#include "game/act_runner.hpp"
#include "game/collision.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace baroque::game {

struct Chr;
struct StatusObj;
struct StatusBlock;

// ---- hit manager 0x50c780 (ctor FUN_0014a840) ----
struct HurtReg {                 // 0x14 bytes, 32 per hurt volume
    Chr* attacker = nullptr;     // +0x00
    int32_t hitId = 0;           // +0x04
    uint32_t stamp = 0;          // +0x08 manager frame of the last contact
    uint32_t m1 = 0;             // +0x0c
    uint32_t flags = 0;          // +0x10 bit0 new, bit1 no damage, 0x10000 player with a sword (weapon class < 3)
};
struct HurtVolume {              // 0x298 bytes, 64 in the manager (+0x04)
    Chr* ent = nullptr;          // +0x00
    float r = 0.0f;              // +0x04 radius (box: |(b, c)|)
    float a = -1.0f;             // +0x08 height (-1 sphere)
    float b = -1.0f;             // +0x0c box half extent along right (-1 sphere / cylinder)
    float c = -1.0f;             // +0x10 box half extent along forward
    float topY = 0.0f;           // +0x14 a + ent.y (FUN_0014e1f0)
    std::array<HurtReg, 32> regs{};   // +0x18
};
struct HitSlot {                 // 0x4c bytes, 32 in the manager (+0xa604)
    Chr* owner = nullptr;        // +0x00
    int32_t hitId = 0;           // +0x04
    uint32_t flags = 0;          // +0x08 bit0 tested once (sweep from the last position), 0x100 ACT-created
    float radius = 0.0f;         // +0x0c
    int32_t boneA = 0;           // +0x10
    int32_t boneB = -1;          // +0x14 bone (capsule), -1 sphere, -2 offset vector
    Vec3f ofs{};                 // +0x18
    Vec3f posA{};                // +0x24
    Vec3f posB{};                // +0x30
    uint32_t m1 = 0, m2 = 0, m3 = 0;   // +0x3c attributes (element/flags), status bits, effect bits
    float power = 0.0f;          // +0x48
};

class HitManager {
public:
    void reset();                                               // FUN_0014a840
    int addSphere(float r, Chr* ent);                           // FUN_0014ab40
    int addCylinder(float r, float height, Chr* ent);           // FUN_0014abb0
    int addBox(float height, float b, float c, Chr* ent);       // FUN_0014ac40
    void removeVictim(int index);                               // FUN_0014ace0
    HitSlot* freeSlot();                                        // FUN_0014aaf0
    void hitEnd(const Chr* owner, int hitId, int type);         // FUN_0014de50
    void removeOwner(const Chr* owner, int type);               // FUN_0014ded0
    void tick();                                                // FUN_0014e1b0 (game task, after the characters)
    std::vector<Chr*> victimsHitBy(const Chr* ent) const;       // FUN_0014df50 (via FUN_0014e020)
    void forget(const Chr* c);                                  // port: entity freed (owner slots + registrations)
    // orig FUN_0014ad00(R, power, 0x50c780, attacker, hitId, pos, m1, m2, m3, weaponTag, exclude): every hurt volume
    // touched by the sphere (R, pos) is registered and resolved through FUN_0014c970; true when one counted.
    bool testPoint(float r, float power, Chr* attacker, int hitId, const Vec3f& pos, uint32_t m1, uint32_t m2,
                   uint32_t m3, uint32_t weaponTag, const Chr* exclude);

    uint32_t frame = 0;                                         // +0x00
    std::array<HurtVolume, 64> victims{};
    std::array<HitSlot, 32> slots{};

private:
    int freeVictim() const;                                     // FUN_0014a910
    HurtReg* registerHit(int victim, Chr* attacker, int hitId, uint32_t m1);  // FUN_0014a9f0 (null = known)
    void sphere(HitSlot& s);                                    // FUN_0014e3e0
    void capsule(HitSlot& s);                                   // FUN_0014e510
    void offset(HitSlot& s);                                    // FUN_0014e810
    void sweep(float r, HitSlot& s, const Vec3f& from, const Vec3f& to);   // FUN_0014eae0
};
HitManager& hitManager();

// ACT commands of the combat family (FUN_00147720 Hit_a, FUN_001477b0 Hit_b, FUN_00147880 Hit_c,
// FUN_00147960 HitEnd); owner = the ACT player's character (+4).
void actCombatCommand(Chr& owner, ActRunner& act, const data::ActCmnd& c);
// (+ MoveAcc FUN_00147a70, MoveJump FUN_00147aa0, ChrColls FUN_00149730, Invincib 0x149e00, RotYAct FUN_00149a70,
//  RotYEnd FUN_001460d0)
void actTurnStep(Chr& c);     // orig FUN_00146120 (first step of the ACT tick FUN_00143fe0)
void actHitLatch(Chr& c);     // FUN_00143fe0: 0x2000000 -> 0x4000000 when this action's hits dealt damage
void actScaleStep(Chr& c);    // orig FUN_00146220 (third step of the ACT tick prologue)

// ---- equipment / presentation services (inventory 0x53d910, effect system, log window) ----
struct EquipEffect {             // FUN_001d2f10(FUN_001d2e80(item), i) record of an equipped item
    uint8_t type = 0;            // +0 effect type (0x10..0x13 projectiles, 0x15 guard, 0x17 evade, 0x19 wear, 0x1e scale..)
    uint8_t category = 0;        // +1 (2 attack, 3 attack mult, 5 defence, 10 kill)
    int16_t value = 0;           // +2
    int16_t extra = 0;           // +4
    uint16_t chance = 0;         // +6 (also the signed amount of the wear effects)
    void* item = nullptr;        // owning item
    uint32_t itemTag = 0;        // owning item's id (+0x1c), e.g. 'WG05'
    uint8_t slot = 0;            // equipment slot 0..3 (inventory +0xa4 + slot*4: 0 weapon, 1 second weapon, 2 coat, 3 wings)
    uint8_t rec = 0;             // record index 0 / 1 of FUN_001d2f10 (the per-item loop variable of the callers)
};
struct CombatHooks {
    // FUN_001d2e80/FUN_001d2f10 over inventory +0xa4..+0xb0 (4 equipment slots x 2 effect records), in that order
    std::function<void(std::vector<EquipEffect>&)> equipEffects;
    std::function<void(void* item, int amount)> wearItem;                 // FUN_001d5030 (+ 'CT12' break message 0x259)
    // FUN_001d4f70 (+ SE 0x91 when it returned 1 for a negative amount, 'SD17' break message 0x259; FUN_00136290 0x18)
    std::function<void(void* item, int amount)> wearAtkItem;
    // PRMITEM ST row value *(*(0x53d87c)+0x44) + n*0x80 + 0x78 (brand 'STnn' chance / power; ST01 0xf8, ST07 0x3f8,
    // ST17 0x8f8)
    std::function<int(int n)> brandValue;
    std::function<bool()> itemScreenOpen;                                 // iRam00517b68 != 0
    std::function<bool(const StatusObj&)> canWarp;                        // FUN_00138ad0 (teleport possible)
    // FUN_0014b170: PRMITEM WM +0x70 (0x53d88c +0x44, stride 0x80) of the player's parasite (status +0x5c) | of its
    // +0x38 parasite; FUN_001d50f0(item) for an equipped item of class 0 (SD), else 0
    std::function<uint32_t()> parasiteElementMask;
    std::function<uint32_t(const void* item)> swordElementMask;
    // FUN_00136580 tail: (item +0x54 + +0x60 < 99) && FUN_001d4ee0(item, amount) == 1 -> message 0x252 with the name
    std::function<void(void* item, int amount)> evolveItem;
    std::function<void()> bx24Tick;                                       // FUN_001d9560(0x53d910)
    // FUN_001e5610(player pos, 0, 1, 0, 0, 0x50, player, 'ST17') + FUN_0013de20 (ST17 brand counter of FUN_001351b0).
    // UNVERIFIED: the effect-damage object FUN_001e5610 is not translated; unset = no counter object.
    std::function<void(Chr& player)> st17Counter;
    // status visuals FUN_00137f40 / status tick: FUN_00112560 (stop), FUN_001122d0 (move), sound manager FUN_001161b0
    // (SE override off; on = status_set.cpp statusVisualOn)
    std::function<void(int id)> effectStop;
    std::function<void(int id, const Vec3f& pos)> effectMove;
    std::function<void(bool on)> seOverride;
    std::function<int()> weaponClass;                                     // FUN_001d7d10(0x53d910), -1 none
    std::function<int(int hand)> playerAtk;                               // FUN_001d83d0 (-1) / FUN_001d84b0 (hand)
    std::function<int()> playerDef;                                       // FUN_001d85b0
    std::function<int()> playerCritBonus;                                 // FUN_001d8720
    std::function<uint32_t()> heldElementMask;                            // FUN_001d50f0 (iRam0053d9bc held item)
    std::function<uint32_t()> playerImmunities;                           // FUN_00136e50 (wings)
    std::function<int(const StatusObj&)> hpMaxTotal;                      // FUN_001343f0 (base + items)
    std::function<void(Chr& dead)> dropItem;                              // FUN_00136d30
    std::function<void(int fx, const Vec3f& pos, const Vec3f* rotDeg, float scale)> effect;   // FUN_00112190 subset
    std::function<void(Chr* attacker, int value, const Vec3f& pos)> damageNumber;          // FUN_001e6970 (DTNT)
    std::function<void(int msgId, int value)> log;                        // FUN_0015d480(0x5180f0, FUN_00114da0(id), ..)
    std::function<void(int hp)> hudHp;                                    // FUN_00151ae0(0x5179c0, hp)
    std::function<void(int vt)> hudVt;                                    // uRam00517b2c
    std::function<bool(StatusObj& thief, StatusObj& victim)> steal;       // FUN_001392f0
    std::function<Vec3f()> cameraPos;                                     // gameplay camera +0x7c..+0x84
    // status tick FUN_00133bf0 (player regeneration): equipped item by tag FUN_001d7cc0(0x53d910, tag) (null = none),
    // its first effect record's chance FUN_001d2f10(FUN_001d2e80(item), 0)+6, inventory holds tag FUN_001d92d0,
    // brand table value *(*(0x53d87c)+0x44)+0xf8 (ST01)
    std::function<const void*(uint32_t tag)> equippedItem;
    std::function<int(const void* item)> itemChance;
    std::function<bool(uint32_t tag)> inventoryHas;
    std::function<int()> brandSt01Value;
    std::function<bool()> talkBusy;                                       // iRam0053c6a8 == 1
    // FUN_00146f90 equipment part: weapon *0x53d9b4 id "SD13" and the six FUN_001d4c50(.., "WM16") tests (weapon
    // +0x38 and its +0x38, *0x53d9b8 +0x38 and its +0x38, status block +0x5c and its +0x38)
    std::function<void(const StatusBlock& block, AttackRateInputs& in)> attackRateItems;
};
CombatHooks& combatHooks();

// ---- resolution ----
// orig FUN_0014c970: returns 1 when the hit counted (registration keeps the updated m1), 0 = no hit
int onHit(float power, Chr& victim, Chr* attacker, uint32_t& m1, uint32_t& m2, uint32_t& m3, uint32_t weaponTag,
          int hitId, Chr* owner);
// orig FUN_001354b0
int damageFormula(float power, StatusObj& vic, Chr* attacker, uint32_t m1, uint32_t weaponTag, uint32_t& flags,
                  int hitId, float& atkOut);
// orig FUN_00136580: true when the victim died
bool applyDamage(StatusObj& vic, Chr* attacker, int dmg, uint32_t m3, void* evolveItem, Chr* owner);
uint32_t inflictStatus(StatusObj& s, uint32_t mask);      // orig FUN_00138660
uint32_t cureAllStatus(StatusObj& s);                     // orig FUN_00138ac0
uint32_t statusExpire(StatusObj& s, uint32_t mask);       // orig FUN_00138970
void statusVisual(StatusObj& s, int i, bool on);          // orig FUN_00137d40 (on, status_set.cpp) / FUN_00137f40 (off)
void statusTick(StatusObj& s);                            // orig FUN_00133bf0 (per character, FUN_00129c60)
bool statusAddHp(StatusObj& s, int d);                    // orig FUN_001349e0 (true when HP > 0)
void statusAddVt(StatusObj& s, int d);                    // orig FUN_00134b60
bool statusAddExp(StatusObj& s, int exp);                 // orig FUN_00138e40 (true on level up)
int statusHpMax(const StatusObj& s);                      // orig FUN_001343f0
int statusDef(const StatusObj& s);                        // orig FUN_00134740
int statusAtk(const StatusObj& s, int hand);              // orig FUN_001344f0
float victimReduction(StatusObj& s, const Chr* attacker); // orig FUN_00135f00
float elementWeakness(const StatusObj& s, uint32_t m1);   // orig FUN_00135c50
float difficultyScale(const StatusObj& s, const Chr* attacker);  // orig FUN_00135d80
void hitFx(float value, Chr& victim, Chr* attacker, uint32_t m1, uint32_t m2, uint32_t m3, uint32_t dmgFlags);  // FUN_0014bc30
void react(Chr& c, uint32_t m3, const Vec3f& src, bool invert);  // orig FUN_0012a710
void hitElementFx(Chr& victim, Chr* attacker, uint32_t m1, const Vec3f* at);   // orig FUN_0014c580 (at null = FUN_0014b9f0 point)
void requestDeath(Chr& c, int variant);                   // orig FUN_0012a5e0 (0 normal, 1 vanish)
// orig FUN_0014b170: returns the evolving 'SD23' sword (passed on to FUN_00136580), null otherwise
void* playerAttackAttributes(uint32_t& m1, uint32_t& m2, uint32_t& m3, int hitId);

}  // namespace baroque::game
