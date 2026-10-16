// PORT (voice language): Japanese cut-scene timing with the English text.
// The USA disc carries two script sets: DATA/EVENT_EN (English text, timed for the English voices) and DATA/EVENT
// (the Japanese release's scripts; same opcode streams, cue frames and operands as the Japan disc SLPM-66747's
// DATA/EVENT for 52 of the 53 events it has, E36 differs in one AnimChange word and one flag). With Japanese voices
// the English scripts let lines talk over each other and end scenes before the last line finishes, so the port runs
// the Japanese script and swaps in the English text pages. The voice archive is the same: the undub's
// SOUND_EN/BRQSOUND.SFS is byte-identical to the Japan disc's SOUND/BRQSOUND.SFS, so no Japan-disc data is needed.
// Nothing here is original game code.
#pragma once
#include "data/common.hpp"

namespace baroque::data {

struct EvtJpTimingStats {
    int pages = 0;          // English text pages placed
    int pagesDropped = 0;   // Japanese text pages without an English page (the previous English page stays up)
    int unevenGroups = 0;   // voice segments where English has fewer pages than Japanese
    int stateMoved = 0;     // English-only state cues copied into the Japanese end cue
};

// Builds the merged EVT: the Japanese script `jp` (cue frames, cameras, actors, voices, clears, end frame) with
//  - the 0x800-byte text block of the English script `en`,
//  - every Japanese ShowText (op 27, plus the TextWindow110 speed that follows it) replaced by the English page of
//    the same voice segment and text slot (segments split at the voice cues 132/133, whose id sequence must match);
//    where English has fewer pages, they are placed on the Japanese pages nearest in text position (character
//    counts) and the remaining Japanese pages are removed,
//  - English-only state cues (flags, items, BGM, timers, movie, pad) of the English end cue copied to the start of
//    the Japanese end cue (E14 BGM restart, E36 flag).
// Returns nullopt with *why when the scripts do not line up (the caller then keeps the English script).
std::optional<std::vector<uint8_t>> buildJapaneseTimedEvt(Bytes en, Bytes jp, std::string* why,
                                                          EvtJpTimingStats* stats = nullptr);

}  // namespace baroque::data
