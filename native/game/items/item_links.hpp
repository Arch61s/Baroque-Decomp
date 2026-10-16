#pragma once
// Calls of the item / HUD module into subsystems that only the full game links (character combat / status, AI,
// player movement, effect system, sound manager voice channel, the player's animation sets). Implemented in
// game/items/item_links_game.cpp (Baroque-Native); Baroque-ItemsCheck links its own minimal definitions
// (checks/items_check_main.cpp), the way the original behaves with those subsystems idle.
#include <cstdint>

namespace baroque::game {

struct Chr;
struct StatusObj;
struct Vec3;

// character status (game/character/combat.cpp)
uint32_t linkInflictStatus(StatusObj& s, uint32_t mask);   // FUN_00138660
uint32_t linkStatusExpire(StatusObj& s, uint32_t mask);    // FUN_00138970
uint32_t linkCureAllStatus(StatusObj& s);                  // FUN_00138ac0
bool linkStatusAddExp(StatusObj& s, int exp);              // FUN_00138e40
int linkExpForLevel(int level);                            // FUN_00138dc0
float linkVictimReduction(StatusObj& s, const Chr* attacker);   // FUN_00135f00
float linkElementWeakness(const StatusObj& s, uint32_t m1);     // FUN_00135c50
bool linkApplyDamage(StatusObj& vic, Chr* attacker, int dmg, uint32_t m3);   // FUN_00136580(s, a, dmg, m3, 0, 0)
void linkHitElementFx(Chr& victim, uint32_t m1);           // FUN_0014c580(chr, 0, m1, 0)
void linkRequestDeath(Chr& c, int variant);                // FUN_0012a5e0
void linkReact(Chr& c, uint32_t m3, const Vec3& src, bool invert);   // FUN_0012a710
void linkPlayerIdle(Chr& c);                               // FUN_0013d810(0x50b620, chr)
uint32_t linkPlayerCtlFlags();                             // uRam0050b620 (player control +0x00), 0 without one
void linkHeadFlash(Chr& c);                                // FUN_0013de20 (effect 0x68 at the "Head" joint)
bool linkJointPos(Chr& c, const char* joint, Vec3& out);   // joint world position (exact name), false = no joint
bool linkJointMatrix(Chr& c, const char* joint, float out[16]);   // the joint's world matrix (FUN_0039d860 / FUN_0039d450)
// AI component (comp +0x14, game/character/ai.cpp)
uint32_t linkAiVar(Chr& c, uint32_t id);                   // FUN_001f1730
void linkAiSetVar(Chr& c, uint32_t id, uint32_t v);        // FUN_001f1690
// effect system (game/effects): FUN_00112190(0, fx, pos, rot, (s, s, s, 1)) -> instance id (0 = none)
int linkEffectSpawn(int fx, const Vec3& pos, const float* rotDeg, float scale, float tag = 0.0f);   // tag = f12
void linkEffectKill(int id);                               // FUN_00112530
void linkEffectEnd(int id);                                // FUN_001126e0(id, 0, -1, -1)
void linkEffectSetPos(int id, const Vec3& pos);            // FUN_001122d0
int linkEffectSpawnMatrix(int fx, const float* m);         // FUN_00112190(0, fx, 0, 0, 0) + FUN_00112770(id, m) (m != null)
// sound manager 0x4e9790 voice channel (audio::SoundSystem)
bool linkVoiceOverride();                                  // FUN_001161c0: +0x4ac != 0
void linkVoiceStop();                                      // FUN_00115c10
void linkVoicePause(bool on);                              // FUN_00115c50
void linkVoicePlay(int id);                                // FUN_00115970(1.0, snd, id, 0)
// player animation sets (FUN_001b2620 slot load / FUN_0010a7f0 restart by exact name), used by FUN_001b1540
void linkPlayerSetLoad(Chr& c, int slot, const char* setId);
void linkPlayerRestartAnim(Chr& c, const char* name);
void linkPlayerSetRelease(Chr& c, int slot);               // FUN_001b2730
// player model parts of the equipment (FUN_001b1540 family): the player class's seven joint-attach nodes +0x50 (on
// the joints of table 0x462920: Head, Right_WEAPON00_Attach, Left_WEAPON00_Attach, Center_ImWing_Attach,
// Right_WEAPON00_Attach, Spine1, OBJ002_Attach_BASE; 8-byte entries {attached graph, attach node}) and the actor's
// skin list (+0x44 BODY skin, +0x48 coat skin). Slot 0 (HEAD) belongs to the character module.
bool linkPlayerSlotAttached(Chr& c, int slot);             // +0x50[slot] +0 != 0
// the igSceneInfo graph of `path` (DATA/OBJ/...) under the slot's attach node: +0x50[slot] +0 = graph,
// FUN_003ded10(node, graph), FUN_003c7cf0(actor attach list, node)
void linkPlayerSlotAttach(Chr& c, int slot, const char* path);
void linkPlayerSlotDetach(Chr& c, int slot);               // FUN_003df010(node, graph), +0 = 0, FUN_003c7d00(list, node)
void linkPlayerBodySkin(Chr& c, bool attach);              // FUN_003c7cb0 / FUN_003c7cc0(actor skin list, +0x44)
bool linkPlayerCoatLoaded(const Chr& c);                   // +0x48 != 0
// FUN_001b2230 load + callback FUN_001b0d20 (index 1): +0x48 = first skin of the file's igAnimationDatabase,
// FUN_003c7cb0(skin list, +0x48)
void linkPlayerCoatLoad(Chr& c, const char* path);
void linkPlayerCoatUnload(Chr& c);                         // FUN_001b2400 coat branch: FUN_003c7cc0 + release, +0x48 = 0
// player +0x6c (FUN_001b2230 / FUN_001b2400): the first skin of the resident DATA/OBJ/CT01_KAGE.IGB / CT06_KAGE.IGB
// igAnimationDatabase (FUN_00325420 lookup), null = none; drawn by the shadow pre-pass only (ChrModel::coatShadow)
void linkPlayerCoatShadow(Chr& c, const char* path);
void linkPlayerModelsReset();                              // the characters were destroyed (chrAdaptersReset)

}  // namespace baroque::game
