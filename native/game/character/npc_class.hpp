#pragma once
// NPC character classes (factory table 0x44aa10, ENEMIES_NATIVE.md "id -> class"; work/reports/TALK_NATIVE.md 4):
//   type-1 activation FUN_001b3220 (called by the generic activation FUN_001299f0 for type-1 actors and by FUN_001b39f0)
//   Npc     FUN_00130300 -> ctor FUN_001b3880, vtable 0x4cccf0: Activate FUN_001b39f0 (<id>_HEAD.IGB on "Head"),
//           vt+0x28 FUN_00130800 (+0x40 head actor), ClassTick FUN_00129c50 (empty)
//   NpcC03A FUN_00130290 -> ctor FUN_001b3ae0, vtable 0x4ccd30: Activate FUN_001b3b90, ClassTick FUN_001b3d80 (wander)
//   NpcC08  FUN_00130220 -> ctor FUN_001b4fb0, vtable 0x4ccd70: Activate FUN_001b5190 (C08A_HEAD / C08A_HEAD2 on
//           "Head" / "Head_B"), vt+0x28 FUN_001308f0 (+0x40), vt+0x2c FUN_00130900 (+0x44), contact FUN_00130900
//   NpcC12A FUN_001301b0 -> ctor FUN_001b5270, vtable 0x4ccdb0: Activate FUN_001b5400 (head + OBJ007.IGB weapon)
//   NpcC18A FUN_00130140 -> ctor FUN_001b5990, vtable 0x4ccdf0: Activate FUN_001b5b60 ("HeadRote" joint spin),
//           ClassTick FUN_001b5c10 (the Cursebringer: item curse / orbiting items 0x53d7d0), dtor FUN_001b59e0
//   NpcC27A FUN_001300d0 -> ctor FUN_001b6ef0, vtable 0x4cce30: Activate FUN_001b7030 ("Battery" joint),
//           ClassTick FUN_001b7160 (turns the joint toward the player's "Spine1")
// Class-private fields (+0x40..) live in a side table keyed by the character (the shared Chr record in
// character.hpp belongs to the character module); the extra models are added to Chr::model.parts so the existing
// draw path shows them on their joints.
#include <cstdint>

namespace baroque::game {

struct Chr;

bool npcIsClass(const Chr& c);                       // one of the NPC classes above
void npcTypeActivate(Chr& c);                        // orig FUN_001b3220 (type 1)
bool npcClassActivate(Chr& c);                       // vt+0x30 of the NPC classes; false = not an NPC class
void npcClassTick(Chr& c);                           // vt+0x34 of the NPC classes (+ PORT: head animation pose)
void npcClassDestroy(Chr& c);                        // dtors FUN_001b38d0 / FUN_001b5080 / FUN_001b52c0 / FUN_001b59e0 / FUN_001b6f40
// vt+0x28 (part 1) / vt+0x2c (part 2) actor + FUN_0010a8e0(blend, speed, actor, anim by name, 0); false when the
// class has no such actor (FUN_0012d3e0 / FUN_0012d3f0 return 0)
bool npcPartAnim(Chr& c, int part, const char* name, float blend, float speed);
// C12A box node (+0x48 transform of OBJ007 on "Left_WEAPON00_Attach"): FUN_001b56c0 (show) / FUN_001b5680 (hide)
void npcC12Box(Chr& c, bool show);
// PORT (presentation, read-only): the extra actor whose model is Chr::model.parts[partIndex] (NPC heads), null when
// that part is not an extra actor's. Used by the snapshot presenter (game/present) to interpolate its pose.
class ActorAnim;
const ActorAnim* npcExtraActorAnim(const Chr& c, int partIndex);

}  // namespace baroque::game
