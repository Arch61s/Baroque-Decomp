// DATA/AI/AISEQ.BIN enemy AI byte-code (GAME_FORMATS.md section 6). Loader FUN_001ed200, record selection
// FUN_001f0630, state entry FUN_001f0750, interpreter FUN_001f07b0 (work/decompiled/001f07b0.c). The operand
// layout below is exactly what the interpreter reads; execution semantics belong to the game module.
#pragma once
#include "data/common.hpp"
#include <map>

namespace baroque::data {

// Byte-code opcodes (first byte of each instruction) and their operand bytes as read by FUN_001f07b0.
enum class AiOp : uint8_t {
    End = 0x00,         // return to the saved continuation (set by a jump with pushFlag) or stop
    SetAction = 0x01,   // cstring action name -> FUN_00143a40 when the action exists (FUN_00143e80)
    Jump = 0x02,        // i32 target(file offset), u8 pushFlag
    JumpIfAction = 0x03,  // cstring action, i32 target, u8 pushFlag: taken when the action is current/listed (FUN_001f2430)
    JumpIfVar = 0x04,   // u32 var, u32 value(or var ref), u8 cmp(0 ==,1 !=,2 >,3 >=,4 <,5 <=,6 &), i32 target, u8 pushFlag
    JumpIfChr = 0x05,   // u32 chrId(4 chars), i32 target, u8 pushFlag: taken when chr+0x1c == chrId
    Flag6 = 0x06,       // flags(+0x14) |= 8
    Label = 0x0e,       // cstring (skipped)
    Id4 = 0x0f,         // 4 bytes (stored on the stack, unused)
    SetVar = 0x10,      // u32 var, u32 value
    RandVar = 0x12,     // u32 var, u32 range -> var = rand(range) (FUN_002071e0)
    AddVar = 0x13,      // u32 var, u32 value
    SubVar = 0x14,      // u32 var, u32 value (clamped at 0)
    AndVar = 0x15,      // u32 var, u32 value
    Target = 0x20,      // u8 mode: 0/1 player, 2/3 +search, 'd' (0x64) and others: drop target
    Move = 0x21,        // f32 speed (bVar6 = true -> FUN_00128fc0 move at the end of the step)
    Rotate = 0x22,      // f32 degrees applied to the move direction
};
// Values >= 0xffffff passed as `value` are variable references resolved by FUN_001f1730;
// 0xffffff..0x1000006 = the 8 slots at ai+0x1c..+0x38 (FUN_001f1690), 0x1000007.. = computed queries.
inline constexpr uint32_t kAiVarRefMin = 0xffffff;

struct AiInstr {
    AiOp op = AiOp::End;
    uint32_t offset = 0;    // file offset of the opcode byte
    uint32_t length = 0;    // total instruction bytes
    uint32_t a = 0;         // var / chrId / value1 (bit pattern; Move/Rotate: float bits)
    uint32_t b = 0;         // value / range
    uint8_t cmp = 0;        // JumpIfVar comparison
    uint8_t flag = 0;       // pushFlag / Target mode
    int32_t target = -1;    // jump target (absolute file offset)
    std::string str;        // SetAction / JumpIfAction / Label text, Id4 chars
    float asFloat() const { return bitsToFloat(a); }
};

struct AiSequence {         // one linear run of instructions ending at End
    uint32_t offset = 0;
    std::vector<AiInstr> instrs;
};

struct AiRecord {
    std::string id;                      // 4-char character id (E01A.., C18A)
    uint32_t offset = 0;                 // index entry: file offset of the state table
    std::vector<uint32_t> states;        // u32 per state: file offset of the sequence (0 = none); FUN_001f0750
};

struct AiSeqFile {
    uint32_t headerOffsets[3] = {};      // words 0..2: 0xc, 0x10, 0x14 (offsets to the three header parts)
    uint32_t maxSteps = 0;               // word @ offsets[0] (0x400): DAT_004663e0 instruction budget per run
    uint32_t word10 = 0;                 // word @ offsets[1] (0x2d5) UNVERIFIED: iRam0053dd68 points here, no reader found
    std::vector<AiRecord> records;       // index @ offsets[2] until the "    " entry (FUN_001f0630)
    std::map<uint32_t, AiSequence> sequences;  // every reachable sequence keyed by file offset
    std::vector<uint8_t> raw;
};

// Decodes one instruction at `offset` (throws FormatError on unknown opcode / truncation).
AiInstr decodeAiInstr(Bytes data, uint32_t offset);
AiSeqFile parseAiSeqOrThrow(Bytes data);
std::optional<AiSeqFile> parseAiSeq(Bytes data, std::string* error = nullptr);
// orig FUN_001f0630 record search: first entry whose id matches, or the "    " terminator (-1 here).
int findAiRecord(const AiSeqFile& file, uint32_t chrId);
inline constexpr const char* kAiSeqPath = "DATA/AI/AISEQ.BIN";  // 0x466408

}  // namespace baroque::data
