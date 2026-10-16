#pragma once
// Per-talk condition predicates (EVENTS_EFFECTS_PROGRESSION.md 1.2, work/reports/PROGRESS_NATIVE.md).
// FUN_001a0e60 dispatches a talk index (0..0x635) to one of 81 block functions (FUN_0017c700 .. FUN_001a0e50),
// 20 indices each; every case of their jump tables is a decision tree over flag tests FUN_00178a80(0x53c3b0, id) and
// nothing else (no counters, floors or inventory are read: the 5467 calls in 0x17c700..0x1a0e60 all target
// FUN_00178a80). The bodies are lifted from the MIPS by tools/gen_progress_tables.py into talk_predicates.inc.
#include <cstdint>
#include "game/flag_manager.hpp"
namespace baroque::game {
constexpr int kTalkPredicateBlocks = 81;
// orig FUN_001a0e60 (work/decompiled/001a0e60.c): 1 when talk `idx` may play under the flag state `f`, else 0.
// Indices >= 0x654 (and negative ones, via the first block's unsigned range check) return 0.
int talkPredicate(const FlagManager& f, int32_t idx);
// Address of the block function serving `idx` (diagnostics / the check), 0 when none.
uint32_t talkPredicateBlockAddr(int32_t idx);
}
