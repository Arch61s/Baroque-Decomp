// DATA/TALK{,_EN}/*.TLK talk scripts and the ELF talk table (work/reports/GAME_FORMATS.md section 3).
// Lookup/load FUN_001a73a0, text copy FUN_001a7740, page parser FUN_001a77c0, trigger expansion FUN_001a80c0.
#pragma once
#include "data/common.hpp"

namespace baroque::data {

// ELF table 0x457020: 1590 x {file, voiceNo, speakerId}, NULL terminated.
struct TalkEntry {
    const char* file;      // e.g. "C00A_V15B" -> DATA/TALK[_EN]/<file>.TLK
    const char* voiceNo;   // e.g. "1289" (FUN_001a98a0 resolves it against the speaker)
    const char* speaker;   // e.g. "C25B"
};
inline constexpr int kTalkTableCount = 0x636;
const TalkEntry* talkTable();                         // kTalkTableCount entries
int findTalkIndex(std::string_view name);             // orig FUN_001a73a0 strcmp loop; -1 when absent
std::string talkPath(bool japanese, std::string_view name);  // "DATA/TALK/%s.TLK" (0x462520) / "DATA/TALK_EN/%s.TLK" (0x462540)

#include "data/tables/talk_window_rects.inc"

// One expanded trigger (FUN_001a80c0 writes 0x34-byte entries at talk+0x6ec).
struct TlkTrigger {
    int32_t kind = 0;        // stream kind 0x65..0x73
    int32_t type = 0;        // +0x6f0: kind mapped 0x65->1 ... 0x73->15
    int32_t frame = 0;       // +0x6ec: (int)(float)word1
    float f0 = 0, f1 = 0, f2 = 0;  // +0x6f4,+0x6f8,+0x6fc: (float)word (kinds 0x65, 0x6e)
    int32_t w = 0;           // +0x708: word2 (kinds 0x65,0x66,0x6c,0x6d,0x6e,0x6f,0x71); kind 0x70: word5 via float round trip
    uint32_t name[2] = {};   // +0x70c,+0x710: 8-char name (kinds 0x68,0x69,0x6a,0x6b,0x70)
    int32_t w714 = 0;        // +0x714 (kind 0x70)
    float g0 = 0, g1 = 0;    // +0x718,+0x71c: (float)word (kinds 0x68,0x6a,0x6b)
    std::string nameText() const;  // name[] as text (SSnn_xx / FNnn_xx / ITnn_xx)
};

// Stream word counts incl. the kind word (0x65..0x73), used by the expansion and the asserts.
int tlkTriggerSize(int32_t kind);   // 0 when unknown

struct TlkScript {
    std::vector<uint8_t> textRaw;        // 0x400 bytes as copied to talk+0x8c (FUN_001a7740)
    SjisString text;                     // trailing NUL/space trimmed
    std::vector<SjisString> sentences;   // ';'-separated fields (empty fields dropped, like the python summary)
    uint32_t word400 = 0;                // always 1 in all files (skipped by the parser, which starts at 0x404)
    // Page state written by FUN_001a77c0 (nullopt = opcode absent).
    bool pageBegin = false;              // opcode 2 seen (a second 2 ends the parse)
    std::optional<int32_t> voice;        // 3 -> +0x494 (-1 = none)
    std::optional<int32_t> sentence;     // 4 -> +0x498 (index of the ';' field)
    std::optional<int32_t> speed;        // 7 -> +0x48c
    std::optional<int32_t> layout;       // 8 -> +0x490 (only applied in scene "Game"; selects kTalkWindowRects row)
    std::optional<int32_t> timer;        // 9 -> +0x101c source (-1 none, 0 FUN_001f8c60(), else FUN_001dacd0-base)
    std::vector<int32_t> triggerBlock;   // 100 -> 0x94 words copied to +0x49c (empty when no block)
    std::vector<TlkTrigger> triggers;    // FUN_001a80c0 expansion of triggerBlock
    uint32_t streamWords = 0;            // words consumed up to and including 201
    // Note: the original parser silently breaks on an unknown page opcode; the parser here throws like the
    // python tool (no file contains one).
};

TlkScript parseTlkOrThrow(Bytes data);                 // asserts: size 2048, word@0x400 == 1, known opcodes/kinds
std::optional<TlkScript> parseTlk(Bytes data, std::string* error = nullptr);
// orig FUN_001a80c0 on a 0x94-word block (stops at kind 0, after 33 entries, or on an unknown kind).
std::vector<TlkTrigger> expandTlkTriggers(const std::vector<int32_t>& block);

}  // namespace baroque::data
