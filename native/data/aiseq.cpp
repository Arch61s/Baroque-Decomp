#include "data/aiseq.hpp"
#include <algorithm>

namespace baroque::data {

namespace {
uint8_t u8at(Bytes d, size_t o) { needBytes(d, o, 1); return d[o]; }
}  // namespace

// Operand reads of FUN_001f07b0 (work/decompiled/001f07b0.c); no float conversions exist in the function, so
// every 4-byte operand is a plain 32-bit word (Ghidra's float typing of var ids/targets is cosmetic).
AiInstr decodeAiInstr(Bytes d, uint32_t offset) {
    AiInstr in;
    in.offset = offset;
    const size_t p = offset;
    const uint8_t op = u8at(d, p);
    in.op = static_cast<AiOp>(op);
    switch (op) {
    case 0x00: in.length = 1; break;
    case 0x06: in.length = 1; break;
    case 0x22: case 0x21: in.a = u32le(d, p + 1); in.length = 5; break;
    case 0x20: in.flag = u8at(d, p + 1); in.length = 2; break;
    case 0x15: case 0x14: case 0x13: case 0x12: case 0x10:
        in.a = u32le(d, p + 1); in.b = u32le(d, p + 5); in.length = 9; break;
    case 0x0f: in.str = fixedString(d, p + 1, 4); in.a = u32le(d, p + 1); in.length = 5; break;
    case 0x0e: { const Bytes s = cstringAt(d, p + 1); in.str = asciiString(s); in.length = static_cast<uint32_t>(2 + s.size()); break; }
    case 0x05: in.a = u32le(d, p + 1); in.target = s32le(d, p + 5); in.flag = u8at(d, p + 9); in.length = 10; break;
    case 0x04:
        in.a = u32le(d, p + 1); in.b = u32le(d, p + 5); in.cmp = u8at(d, p + 9); in.target = s32le(d, p + 10); in.flag = u8at(d, p + 14);
        in.length = 15; break;
    case 0x03: {
        const Bytes s = cstringAt(d, p + 1);
        in.str = asciiString(s);
        const size_t q = p + 2 + s.size();
        in.target = s32le(d, q); in.flag = u8at(d, q + 4);
        in.length = static_cast<uint32_t>(q + 5 - p);
        break;
    }
    case 0x02: in.target = s32le(d, p + 1); in.flag = u8at(d, p + 5); in.length = 6; break;
    case 0x01: { const Bytes s = cstringAt(d, p + 1); in.str = asciiString(s); in.length = static_cast<uint32_t>(2 + s.size()); break; }
    default: throw FormatError("unknown AI opcode " + hex(op) + " at " + hex(offset));  // orig: pc = 0, return 0
    }
    return in;
}

namespace {
void decodeReachable(Bytes d, uint32_t start, AiSeqFile& f) {
    std::vector<uint32_t> work{start};
    while (!work.empty()) {
        const uint32_t s = work.back();
        work.pop_back();
        if (f.sequences.count(s)) continue;
        AiSequence seq;
        seq.offset = s;
        uint32_t p = s;
        for (;;) {
            AiInstr in = decodeAiInstr(d, p);
            p += in.length;
            const bool end = in.op == AiOp::End;
            if (in.target >= 0) {
                if (static_cast<size_t>(in.target) >= d.size()) throw FormatError("AI jump target out of file at " + hex(in.offset));
                work.push_back(static_cast<uint32_t>(in.target));
                if (in.flag != 0) work.push_back(p);  // continuation saved for the next End
            }
            seq.instrs.push_back(std::move(in));
            if (end) break;
        }
        f.sequences.emplace(s, std::move(seq));
    }
}
}  // namespace

// orig FUN_001ed200 header reads + FUN_001f0630 index walk + FUN_001f0750 state tables; python parse_aiseq() asserts.
AiSeqFile parseAiSeqOrThrow(Bytes d) {
    AiSeqFile f;
    for (size_t i = 0; i < 3; ++i) f.headerOffsets[i] = u32le(d, 4 * i);
    if (f.headerOffsets[0] != 0xc || f.headerOffsets[1] != 0x10 || f.headerOffsets[2] != 0x14) throw FormatError("bad AISEQ header offsets");
    f.maxSteps = u32le(d, f.headerOffsets[0]);
    f.word10 = u32le(d, f.headerOffsets[1]);
    size_t idx = f.headerOffsets[2];
    for (;;) {
        needBytes(d, idx, 8);
        if (u32le(d, idx) == tag4("    ")) break;
        AiRecord r;
        r.id = fixedString(d, idx, 4);
        r.offset = u32le(d, idx + 4);
        if (r.offset >= d.size()) throw FormatError("AISEQ record offset out of file");
        f.records.push_back(std::move(r));
        idx += 8;
    }
    if (f.records.empty()) throw FormatError("AISEQ has no records");
    // State table length is not stored: the table runs up to the lowest non-zero sequence offset it contains
    // (all 85 records have 4 entries; code passes states 0, 1 (FUN_001f0630) and 2 (FUN_001f2240)).
    for (auto& r : f.records) {
        uint32_t p = r.offset;
        uint32_t minNonZero = 0xffffffffu;
        for (;;) {
            if (p >= minNonZero) break;
            const uint32_t w = u32le(d, p);
            if (w != 0) minNonZero = std::min(minNonZero, w);
            r.states.push_back(w);
            p += 4;
            if (r.states.size() > 64) throw FormatError("AISEQ state table runaway for " + r.id);
        }
        for (uint32_t s : r.states)
            if (s) {
                if (s >= d.size()) throw FormatError("AISEQ state offset out of file for " + r.id);
                decodeReachable(d, s, f);
            }
    }
    f.raw.assign(d.begin(), d.end());
    return f;
}

std::optional<AiSeqFile> parseAiSeq(Bytes data, std::string* error) {
    return guarded([&] { return parseAiSeqOrThrow(data); }, error);
}

// orig FUN_001f0630 (work/decompiled/001f0630.c): walk 8-byte entries until id matches or "    ".
int findAiRecord(const AiSeqFile& file, uint32_t chrId) {
    for (size_t i = 0; i < file.records.size(); ++i)
        if (id4(file.records[i].id) == chrId) return static_cast<int>(i);
    return -1;
}

}  // namespace baroque::data
