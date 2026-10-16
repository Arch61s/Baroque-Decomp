#pragma once
// Talk selection: which TLK entry a speaker plays for a trigger kind (EVENTS_EFFECTS_PROGRESSION.md 1.2,
// work/reports/PROGRESS_NATIVE.md 2). FUN_001a69e0 search, FUN_001a6d00 entry condition (table 0x45bac0 + the
// per-talk predicate FUN_001a0e60), FUN_001a3cc0 item-category match, FUN_001dacd0 database stage.
// The talk manager 0x53c6e0 and the actor registry 0x50b130 are not ported yet; the fields these functions read
// are passed in through TalkSelectState / TalkActorRef and the two hooks below.
#include <cstdint>
#include "game/flag_manager.hpp"
namespace baroque::game {

// Fields of an actor (registry 0x50b130 entry) read by the search and the item match.
struct TalkActorRef {
    int type = 0;            // +0x18 (1 NPC, 2 enemy, 3 object, ...)
    uint32_t id = 0;         // +0x1c 4-char id, little-endian ("C02A" = 0x41323043)
    int f24 = 0;             // +0x24 (FUN_001a3cc0 category 0x32)
    int f54 = 0;             // +0x54 (category 0x3b, `SD**` objects)
    int f58 = 0;             // +0x58 (category 0x3b, `CT**` objects)
    int f60 = 0;             // +0x60 (categories 0x32 / 0x3b)
    int f64 = 0;             // +0x64 (categories 0x32 / 0x3b)
    uint32_t flags70 = 0;    // +0x70 (bit 2 / bit 16 tested)
};

// Talk manager 0x53c6e0 fields read by FUN_001a69e0 / FUN_001a6d00 / FUN_001a3cc0.
struct TalkSelectState {
    uint32_t itemTalkActor = 0;                  // +0x28 actor id (FUN_001a9810 lookup, kind 2)
    const TalkActorRef* shownItem = nullptr;     // +0xfac special item (kind 12)
    int fb0 = 0;                                 // +0xfb0 (category 0x5c)
    const TalkActorRef* giftItem = nullptr;      // +0xfbc item / gift actor (kind 2)
    uint32_t fc0 = 0;                            // +0xfc0 (category 0x3c)
    int databaseStageSeen = 0;                   // +0xfc4 (categories 0x3e / 0x3f)
    uint32_t excludedSpeaker = 0;                // +0x1010 (trigger 0x72)
};

struct TalkSelectHooks {
    // FUN_001a9810(mgr, id): an actor of type 1 with that id exists (null -> UNVERIFIED: treated as absent)
    bool (*npcExists)(uint32_t id) = nullptr;
    // database object 0x53dc00 +200 (entry count compared by FUN_001dad50; null -> UNVERIFIED: 0)
    int (*databaseCount)() = nullptr;
};
TalkSelectHooks& talkSelectHooks();

// orig FUN_001a69e0: index of the first talk of the speaker (or of the whole table when anySpeaker) whose condition
// passes, -1 when none. `actor` may be null only with anySpeaker.
int talkSearch(const TalkSelectState& mgr, const TalkActorRef* actor, int kind, int a, int b, bool anySpeaker,
               const FlagManager& f);
// orig FUN_001a6d00: entry `idx` matches trigger kind/a/b (kind 5 = any kind) and its predicate passes.
int talkCondition(const TalkSelectState& mgr, int idx, uint32_t kind, int a, int b, const FlagManager& f);
// orig FUN_001a3cc0: item actor matches category `cat` (table 0x45bac0 c1 of kind 2/12 entries).
int talkItemMatch(const TalkSelectState& mgr, const TalkActorRef& item, long cat, const FlagManager& f);
// orig FUN_001dacd0: first set flag of table 0x499650 {736:50, 735:40, ... 729:10} -> stage, else 5.
int databaseStage(const FlagManager& f);
// Speaker start index from table 0x461f80 (0 when the speaker is not listed), as FUN_001a69e0 computes it.
int talkSpeakerStart(uint32_t actorId);
// Condition table 0x45bac0 entry (kind, c1, c2, c3).
struct TalkCondEntry { int32_t kind, c1, c2, c3; };
TalkCondEntry talkCondEntry(int idx);
}
