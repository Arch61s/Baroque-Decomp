#include "game/progress/talk_predicates.hpp"
namespace baroque::game {
namespace {
using FlagView = FlagManager;
#include "game/progress/talk_predicates.inc"
}
// orig FUN_001a0e60 (work/decompiled/001a0e60.c): signed compare chain `idx < limit` (slti) -> block function
int talkPredicate(const FlagManager& f, int32_t idx) {
    for (const PredBlock& b : kPredBlocks)
        if (idx < static_cast<int32_t>(b.limit)) return b.fn(f, static_cast<uint32_t>(idx));
    return 0;
}
uint32_t talkPredicateBlockAddr(int32_t idx) {
    for (const PredBlock& b : kPredBlocks)
        if (idx < static_cast<int32_t>(b.limit)) return b.addr;
    return 0;
}
}
