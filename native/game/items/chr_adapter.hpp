#pragma once
// CharacterAccess over the real character object (game/character/character.hpp: Chr, StatusObj / StatusBlock,
// characters().player()). The status helpers the item code needs from the player status class (PLAYER.md 11) are
// translated here with the parts the character core leaves out (equipment bonuses FUN_00134310 / FUN_001d8290 /
// FUN_001d8330 and the HUD hooks FUN_00151ae0 / uRam00517b2c). Status tables, combat, AI, effects and the player
// animation sets and the player's equipment model parts (FUN_001b1540 / FUN_001b1f20 family) are reached through
// game/items/item_links.hpp.
#include "game/items/char_access.hpp"

#include <vector>

namespace baroque::data {
struct PrmItemEffect;
}

namespace baroque::game {

struct Chr;
struct StatusObj;
struct Inventory;
struct ItemSystem;
class StatusHud;

// The globals the status routines reach: inventory 0x53d910, item PRM tables, status HUD 0x5179c0.
struct ChrAdapterEnv {
    Inventory* inventory = nullptr;
    const ItemSystem* items = nullptr;
    StatusHud* hud = nullptr;
};
ChrAdapterEnv& chrAdapterEnv();

class ChrAdapter final : public CharacterAccess {
public:
    explicit ChrAdapter(Chr* c = nullptr) : chr_(c) {}
    Chr* chr() const { return chr_; }
    void bind(Chr* c) { chr_ = c; }

    bool isPlayer() const override;
    int actorType() const override;
    uint32_t charId() const override;
    bool isRemoved() const override;
    bool isAlive() const override;
    Vec3 position() const override;
    void setPosition(const Vec3& p) override;
    Vec3 forward() const override;
    float height() const override;
    float iconHeight() const override;
    float radius() const override;
    float bodyHeight() const override;
    int roomIndex() const override;
    bool hasCell() const override;
    bool jointPosition(const char* name, Vec3& out) const override;
    bool jointMatrix(const char* name, float out[16]) const override;

    int hp() const override;
    int hpMaxBase() const override;
    int hpMaxTotal() const override;
    int vt() const override;
    int vtMaxBase() const override;
    int vtMaxTotal() const override;
    int atkMax() const override;
    int atkCur() const override;
    int defMax() const override;
    int defCur() const override;
    uint32_t statusMask() const override;
    int level() const override;
    int exp() const override;
    int expNext() const override;
    uint32_t brand() const override;
    int hiddenStat(int i) const override;
    int atkLevelBonus() const override;
    int attackPower() const override;
    bool refusesItems() const override;
    Item* parasite() const override;
    Item* heldItem() const override;

    bool addHp(int d) override;
    void addVt(int d) override;
    void addHpMax(int d) override;
    void addVtMax(int d) override;
    void addAtk(int d) override;
    void addAtkMax(int d) override;
    void addDef(int d) override;
    void addDefMax(int d) override;
    uint32_t inflict(uint32_t mask) override;
    uint32_t cure(uint32_t mask) override;
    void cureAll() override;
    void hpVtMaxShift() override;
    void levelUp() override;
    void levelDown() override;
    void addExp(int exp) override;
    void setBrand(uint32_t id) override;
    void setParasite(Item* p) override;
    bool canWarp() const override;
    void recomputeMaxima() override;
    void refreshModelAttachments() override;
    void gunEquipped(bool on, bool force = false) override;
    void invincibleFlash() override;

    int projectileDamage(CharacterAccess* attacker, int base, uint32_t m1) override;
    bool applyDamage(CharacterAccess* attacker, int dmg, uint32_t m3 = 0) override;
    void hitElementFx(uint32_t m1) override;
    Item* dropHeldItem() override;
    void die() override;
    void react(uint32_t kind, const Vec3& from, bool reversed) override;
    void returnToIdle() override;
    bool currentActionIs(const char* tag2) const override;

    bool isHoldingItem() const override;
    bool takeItem(Item* item) override;
    void sendAiEvent(uint32_t ev) override;
    bool aiAccepts(uint32_t ev) const override;
    bool aiAlertable() const override;
    bool aiDisabled() const override;

private:
    Chr* chr_ = nullptr;
    // player class fields written by FUN_001b1540 (the item ids last attached): +0x58 right hand, +0x5c left hand,
    // +0x60 / +0x64 equipment slots 2 / 3 (0 = nothing)
    uint32_t hand58_ = 0, hand5c_ = 0;
    uint32_t equip60_[2] = {0, 0};
    uint32_t coat68_ = 0;                       // +0x68 coat id of the loaded coat file ("CTXX" after a release)
    const char* coatShadow6c_ = nullptr;        // +0x6c shadow skin ("sh" of CT01_KAGE / CT06_KAGE), null = none
    void coatRelease();                         // FUN_001b2400
    void coatLoad(uint32_t id);                 // FUN_001b2230
};

// Status routines on the real StatusObj (also used by the HUD API).
int statusHpMaxWithItems(const StatusObj& s);     // orig FUN_001343f0
int equipKindBonusOf(int base, int kind);         // orig FUN_00134310 (equipped items' PRM effect records of `kind`)
// The equipment effect records: FUN_001d2f10(FUN_001d2e80(item), i) for the four equipment slots (0x53d9b4.. =
// inventory +0xa4..+0xb0), records 0 and 1 of each SD / CT / WG item in slot order (other classes have none). Bytes:
// rec->kind = +0 (CombatHooks EquipEffect::type), rec->type = +1 (::category), value +2, x +4 (::extra), count +6
// (::chance). Source for CombatHooks::equipEffects and FUN_00134310.
struct EquipEffectRecord {
    const data::PrmItemEffect* rec = nullptr;
    Item* item = nullptr;
};
void equipmentEffectRecords(std::vector<EquipEffectRecord>& out);
uint32_t playerEquipmentImmunities();             // orig FUN_00136e50 (CombatHooks::playerImmunities)
int statusVtMaxWithItems(const StatusObj& s);     // orig FUN_00134470
bool statusAddHpHud(StatusObj& s, int v);         // orig FUN_001349e0
void statusAddVtHud(StatusObj& s, int v);         // orig FUN_00134b60
void statusRecompute(StatusObj& s);               // orig FUN_00134920
bool statusCanWarp(const StatusObj& s);           // orig FUN_00138ad0 (teleport allowed for the owner / its room)

// Adapter for any character of the list (one per Chr, kept for the life of the Chr); the player's adapter when
// `c` is characters().player().
ChrAdapter& chrAdapter(Chr* c);
void chrAdaptersReset();

}  // namespace baroque::game
