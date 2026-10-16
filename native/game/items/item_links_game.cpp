// Game-build implementations of game/items/item_links.hpp (Baroque-Native only).
#include "game/items/item_links.hpp"

#include "audio/sound_system.hpp"
#include "game/character/ai.hpp"
#include "game/character/character.hpp"
#include "game/character/combat.hpp"
#include "game/effects/effects.hpp"
#include "game/event/event_services.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/inventory.hpp"
#include "game/player_move.hpp"
#include "game/sound_stub.hpp"
#include "platform/platform.hpp"

#include <array>
#include <map>
#include <memory>

namespace baroque::game {

namespace {
Vec3f v3f(const Vec3& v) { return Vec3f{v.x, v.y, v.z}; }
audio::SoundSystem* soundSys() {
    auto* m = dynamic_cast<audio::GameSoundManager*>(&sound());
    return m != nullptr ? &m->system() : nullptr;
}
}  // namespace

uint32_t linkInflictStatus(StatusObj& s, uint32_t mask) { return inflictStatus(s, mask); }
uint32_t linkStatusExpire(StatusObj& s, uint32_t mask) { return statusExpire(s, mask); }
uint32_t linkCureAllStatus(StatusObj& s) { return cureAllStatus(s); }
bool linkStatusAddExp(StatusObj& s, int exp) { return statusAddExp(s, exp); }
int linkExpForLevel(int level) { return statusExpForLevel(level); }
float linkVictimReduction(StatusObj& s, const Chr* attacker) { return victimReduction(s, attacker); }
float linkElementWeakness(const StatusObj& s, uint32_t m1) { return elementWeakness(s, m1); }
bool linkApplyDamage(StatusObj& vic, Chr* attacker, int dmg, uint32_t m3) {
    return applyDamage(vic, attacker, dmg, m3, nullptr, nullptr);
}
void linkHitElementFx(Chr& victim, uint32_t m1) { hitElementFx(victim, nullptr, m1, nullptr); }
void linkRequestDeath(Chr& c, int variant) { requestDeath(c, variant); }
void linkReact(Chr& c, uint32_t m3, const Vec3& src, bool invert) { react(c, m3, v3f(src), invert); }

// orig FUN_0013d810 (work/decompiled/0013d810.c) through playerIdle (game/player_move.cpp): "WT03" with a status of
// 0x3f, else "WT02" below 30 % HP (FUN_001343f0), else "WT01" (0x44b248.. [V]), weapon suffix FUN_0013a4b0,
// FUN_00139f30 request, physics +0x7c..+0x84 zeroed.
void linkPlayerIdle(Chr& c) {
    MoveInputs in;
    in.statusMask = c.status.block.statusMask;
    in.hp = c.status.block.hp;
    in.maxHp = statusHpMaxWithItems(c.status);
    in.weaponClass = inventory().weaponClass(itemSystem());   // FUN_001d7d10(0x53d910)
    MoveOutputs out;
    out.requestAction = [&c](const std::string& name8) { c.act.request(name8); };   // FUN_00139f30
    playerIdle(c.phys, in, out);
}

// joint matrix of the tick pose x the actor matrix: vt+0x70 joint index by name, the actor's matrix array (+0x24 when
// +0x79 is set, else vt+0x6c) [index] copied (FUN_0039d860) and multiplied by the node matrix +0x10 +0x20
// (FUN_0039d450(dst, joint, node): row vectors, joint first)
bool linkJointMatrix(Chr& c, const char* joint, float out[16]) {
    const alchemy::Skeleton* sk = c.model.skeleton();
    if (sk == nullptr || joint == nullptr) return false;
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    for (size_t b = 0; b < sk->bones.size() && b < pose.size(); ++b)
        if (sk->bones[b].name == joint) {
            const alchemy::Mat4 w = alchemy::mul(pose[b], chrActorMatrix(c));
            for (int i = 0; i < 16; ++i) out[i] = w.m[size_t(i)];
            return true;
        }
    return false;
}

// orig FUN_0013de20 (work/decompiled/0013de20.c): FUN_00112190(0, 0x68, 0, 0, 0); the player's "Head" joint (0x44b260)
// world matrix; FUN_0039dc10 overwrites its diagonal elements [0] / [5] / [10] with (100, 100, 100) (0x42c80000);
// FUN_00112770(effect, matrix). The same routine as the ST05 revival effect of character.cpp (reviveFx).
// PORT guard: a skeleton without "Head" leaves the effect where FUN_00112190 put it (the original reads the matrix
// array at index -1).
void linkHeadFlash(Chr& c) {
    float m[16];
    const bool found = linkJointMatrix(c, "Head", m);
    const int h = effects().spawn(0.0f, 0x68, nullptr, nullptr, nullptr);
    if (!found) return;
    m[0] = 100.0f;
    m[5] = 100.0f;
    m[10] = 100.0f;
    ExiMat e;
    for (int i = 0; i < 16; ++i) e.m[size_t(i)] = m[i];
    effects().setMatrix(h, e);
}

// joint world matrix (linkJointMatrix), translation FUN_0039d9a0
bool linkJointPos(Chr& c, const char* joint, Vec3& out) {
    float m[16];
    if (!linkJointMatrix(c, joint, m)) return false;
    out = {m[12], m[13], m[14]};
    return true;
}

uint32_t linkPlayerCtlFlags() { return playerCtl() != nullptr ? playerCtl()->flags : 0u; }

uint32_t linkAiVar(Chr& c, uint32_t id) { return aiVar(c.ai, id); }
// orig FUN_001f1690 (work/decompiled/001f1690.c): ids 0xffffff..0x1000006 are the variables +0x1c..+0x38,
// 0x1000007 sets / clears +0x14 bit 0, 0x1000008 writes the MODE bits 1-2 of +0x14
void linkAiSetVar(Chr& c, uint32_t id, uint32_t v) {
    Ai& ai = c.ai;
    if (id >= 0xffffffu && id <= 0x1000006u) {
        ai.v[id - 0xffffffu] = v;
    } else if (id == 0x1000007u) {
        if (v == 0) ai.flags &= 0xfffffffeu;
        else ai.flags |= 1u;
    } else if (id == 0x1000008u) {
        ai.flags = (ai.flags & 0xfffffff9u) | ((v & 3u) << 1);
    }
}

int linkEffectSpawn(int fx, const Vec3& pos, const float* rotDeg, float scale, float tag) {
    const float p4[4] = {pos.x, pos.y, pos.z, 1.0f};
    const float r4[4] = {rotDeg != nullptr ? rotDeg[0] : 0.0f, rotDeg != nullptr ? rotDeg[1] : 0.0f,
                         rotDeg != nullptr ? rotDeg[2] : 0.0f, 1.0f};
    const float s4[4] = {scale, scale, scale, 1.0f};
    return effects().spawn(tag, fx, p4, rotDeg != nullptr ? r4 : nullptr, s4);
}
// FUN_00112190(0, fx, 0, 0, 0) (position (0, 0.15, 0), no rotation / scale) then FUN_00112770(id, m)
int linkEffectSpawnMatrix(int fx, const float* m) {
    const int id = effects().spawn(0.0f, fx, nullptr, nullptr, nullptr);
    if (m == nullptr) return id;
    ExiMat e;
    for (int i = 0; i < 16; ++i) e.m[size_t(i)] = m[i];
    effects().setMatrix(id, e);
    return id;
}
void linkEffectKill(int id) { effects().kill(id); }
void linkEffectEnd(int id) { effects().loopOut(id, 0, -1, -1); }
void linkEffectSetPos(int id, const Vec3& pos) {
    const float p[3] = {pos.x, pos.y, pos.z};
    effects().setPosition(id, p);
}

bool linkVoiceOverride() {
    audio::SoundSystem* s = soundSys();
    return s != nullptr && s->seOverride() != 0;
}
void linkVoiceStop() { if (audio::SoundSystem* s = soundSys()) s->stopCurrentVoice(); }
void linkVoicePause(bool on) { if (audio::SoundSystem* s = soundSys()) s->voicePause(on); }
void linkVoicePlay(int id) {
    if (audio::SoundSystem* s = soundSys()) s->playVoice(1.0f, id, false);
    else sound().playVoice(1.0f, id);
}

void linkPlayerSetLoad(Chr& c, int slot, const char* setId) { eventActorSetLoad(c, slot, setId); }
void linkPlayerSetRelease(Chr& c, int slot) { eventActorSetRelease(c, slot); }   // FUN_001b2730
void linkPlayerRestartAnim(Chr& c, const char* name) {
    const int clip = c.model.anim.findClip(name);
    if (clip >= 0) c.model.anim.set(clip);   // FUN_0010a7f0(actor, animation)
}

// ---- player equipment models -------------------------------------------------------------------------------------
// The player class's +0x50 joint-attach nodes are created by its constructor on the joints of table 0x462920 (strings
// 0x462890.. [V]); the port keeps every attached graph as a ChrModelPart of the player's model drawn at that joint
// (ChrModelPart::attachBoneIndex), identified by its IgbFile. The BODY skin (+0x44) out of the actor's skin list is held
// here until it is attached again. The Alchemy loads are asynchronous in the original (FUN_0010cf60 with the callbacks
// FUN_001b0df0 / FUN_001b0d20 and the pending counter 0x53d780 / FUN_001b14e0); the port loads synchronously.
namespace {
constexpr const char* kSlotJoint[7] = {"Head", "Right_WEAPON00_Attach", "Left_WEAPON00_Attach", "Center_ImWing_Attach",
                                       "Right_WEAPON00_Attach", "Spine1", "OBJ002_Attach_BASE"};
struct PlayerEquipModels {
    std::array<const alchemy::IgbFile*, 7> slot{};   // +0x50[i] +0: the attached graph (identifies the part)
    std::unique_ptr<ChrModelPart> body;              // +0x44 BODY skin while it is out of the actor's skin list
    const alchemy::IgbFile* coat = nullptr;          // +0x48 coat skin
};
std::map<const Chr*, PlayerEquipModels> gEquipModels;

int jointOf(const Chr& c, const char* name) {   // actor vt+0x70 (joint index by name)
    const alchemy::Skeleton* sk = c.model.skeleton();
    if (sk == nullptr) return -1;
    for (size_t b = 0; b < sk->bones.size(); ++b)
        if (sk->bones[b].name == name) return static_cast<int>(b);
    return -1;
}
// the part standing for `f` taken out of the model (nullptr when it is not there)
std::unique_ptr<ChrModelPart> takePart(Chr& c, const alchemy::IgbFile* f) {
    if (f == nullptr) return nullptr;
    auto& parts = c.model.parts;
    for (size_t i = 1; i < parts.size(); ++i) {
        if (parts[i].file.get() != f) continue;
        auto out = std::make_unique<ChrModelPart>(std::move(parts[i]));
        parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
        return out;
    }
    return nullptr;
}
bool isBodySkin(const ChrModelPart& p) {   // 0x462830 "DATA/CHR/C00/C00A_BODY.IGB" (character.cpp loadPlayerParts)
    if (!p.file) return false;
    const std::string n = p.file->path().filename().string();
    return n.size() >= 9 && n.compare(n.size() - 9, 9, "_BODY.IGB") == 0;
}
// PORT guard: the joint-attach nodes and skins exist once the player class is activated (FUN_001b1350 parts loaded);
// before that a part appended here would stand in for those parts.
bool playerModelReady(const Chr& c) { return c.model.loaded() && c.model.parts.size() >= 2; }
}  // namespace

bool linkPlayerSlotAttached(Chr& c, int slot) {
    if (slot < 1 || slot > 6) return false;
    auto it = gEquipModels.find(&c);
    return it != gEquipModels.end() && it->second.slot[size_t(slot)] != nullptr;
}

void linkPlayerSlotAttach(Chr& c, int slot, const char* path) {
    if (slot < 1 || slot > 6 || path == nullptr) return;
    PlayerEquipModels& e = gEquipModels[&c];
    if (!playerModelReady(c)) {
        platform::log(std::string("items: equipment model ") + path + " not attached (player model not activated)");
        return;
    }
    const int joint = jointOf(c, kSlotJoint[slot]);
    if (joint < 0) {
        platform::log(std::string("items: no joint ") + kSlotJoint[slot] + " for " + path);
        return;
    }
    ChrModelPart part;
    if (!chrLoadPart(part, characters().assetRoot / path, "")) return;
    part.attachBoneIndex = joint;   // FUN_003b1480 joint attach (+0x20 = joint index)
    // UNVERIFIED: a slot whose node already holds a graph (FUN_001b1c50 GN path writes +0x20 without a test) - the
    // old graph is replaced (FUN_003ded10 treated as "set child")
    takePart(c, e.slot[size_t(slot)]);
    e.slot[size_t(slot)] = part.file.get();
    c.model.parts.push_back(std::move(part));
}

void linkPlayerSlotDetach(Chr& c, int slot) {
    if (slot < 1 || slot > 6) return;
    auto it = gEquipModels.find(&c);
    if (it == gEquipModels.end() || it->second.slot[size_t(slot)] == nullptr) return;
    const alchemy::IgbFile* f = it->second.slot[size_t(slot)];
    it->second.slot[size_t(slot)] = nullptr;
    // the same graph may hang under two nodes (paired swords): only this slot's part goes
    takePart(c, f);
}

void linkPlayerBodySkin(Chr& c, bool attach) {
    PlayerEquipModels& e = gEquipModels[&c];
    if (attach) {   // FUN_003c7cb0(skin list, +0x44)
        if (e.body) {
            c.model.parts.push_back(std::move(*e.body));
            e.body.reset();
        }
        return;
    }
    auto& parts = c.model.parts;   // FUN_003c7cc0(skin list, +0x44)
    for (size_t i = 1; i < parts.size(); ++i) {
        if (!isBodySkin(parts[i])) continue;
        e.body = std::make_unique<ChrModelPart>(std::move(parts[i]));
        parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

bool linkPlayerCoatLoaded(const Chr& c) {
    auto it = gEquipModels.find(&c);
    return it != gEquipModels.end() && it->second.coat != nullptr;
}

void linkPlayerCoatLoad(Chr& c, const char* path) {
    if (path == nullptr) return;
    PlayerEquipModels& e = gEquipModels[&c];
    if (!playerModelReady(c)) {
        platform::log(std::string("items: coat ") + path + " not attached (player model not activated)");
        return;
    }
    ChrModelPart part;
    if (!chrLoadPart(part, characters().assetRoot / path, "")) return;
    if (const alchemy::Skeleton* sk = c.model.skeleton()) part.model->adoptSkeleton(*sk);   // skin on the actor's skeleton
    takePart(c, e.coat);
    e.coat = part.file.get();
    c.model.parts.push_back(std::move(part));
}

void linkPlayerCoatUnload(Chr& c) {
    auto it = gEquipModels.find(&c);
    if (it == gEquipModels.end() || it->second.coat == nullptr) return;
    takePart(c, it->second.coat);   // FUN_003c7cc0 + refcount release (FUN_00319b70) + FUN_0010cd50
    it->second.coat = nullptr;
}

void linkPlayerCoatShadow(Chr& c, const char* path) {
    if (path == nullptr) {
        c.model.coatShadow.reset();   // +0x6c = 0
        return;
    }
    if (!playerModelReady(c)) {
        platform::log(std::string("items: coat shadow ") + path + " not attached (player model not activated)");
        return;
    }
    // PORT: the original finds the file resident (loaded with the item models of table 0x464150, entries 0x464194 /
    // 0x464198); the port loads it here. Skinned on the actor's skeleton like the coat skin.
    auto part = std::make_shared<ChrModelPart>();
    if (!chrLoadPart(*part, characters().assetRoot / path, "")) {
        c.model.coatShadow.reset();
        return;
    }
    if (const alchemy::Skeleton* sk = c.model.skeleton()) part->model->adoptSkeleton(*sk);
    c.model.coatShadow = std::move(part);
}

void linkPlayerModelsReset() { gEquipModels.clear(); }

}  // namespace baroque::game
