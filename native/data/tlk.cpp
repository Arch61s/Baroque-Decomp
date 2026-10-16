#include "data/tlk.hpp"
#include <cstdio>
#include <cstring>

namespace baroque::data {

namespace {
constexpr TalkEntry kTalkTable[kTalkTableCount] = {
#include "data/tables/talk_table.inc"
};

// (int)(float)x as the original does for frames (identity for |x| < 2^24).
int32_t viaFloat(int32_t v) { return static_cast<int32_t>(static_cast<float>(v)); }

std::vector<SjisString> splitSentences(Bytes text) {
    std::vector<SjisString> out;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == ';') {
            if (i > start) out.push_back(makeSjis(text.subspan(start, i - start)));
            start = i + 1;
        }
    }
    return out;
}
}  // namespace

const TalkEntry* talkTable() { return kTalkTable; }

// orig FUN_001a73a0 (work/decompiled/001a73a0.c): linear strcmp over the table until the NULL entry.
int findTalkIndex(std::string_view name) {
    for (int i = 0; i < kTalkTableCount; ++i) {
        if (name == kTalkTable[i].file) return i;
    }
    return -1;
}

std::string talkPath(bool japanese, std::string_view name) {
    return std::string(japanese ? "DATA/TALK/" : "DATA/TALK_EN/") + std::string(name) + ".TLK";
}

std::string TlkTrigger::nameText() const {
    char buf[9] = {};
    std::memcpy(buf, name, 8);
    return std::string(buf);
}

int tlkTriggerSize(int32_t kind) {
    switch (kind) {
    case 0x65: return 6; case 0x66: return 3; case 0x67: return 2; case 0x68: return 6; case 0x69: return 4;
    case 0x6a: return 6; case 0x6b: return 6; case 0x6c: return 3; case 0x6d: return 3; case 0x6e: return 6;
    case 0x6f: return 3; case 0x70: return 6; case 0x71: return 3; case 0x72: return 2; case 0x73: return 2;
    default: return 0;
    }
}

// orig FUN_001a80c0 (work/decompiled/001a80c0.c)
std::vector<TlkTrigger> expandTlkTriggers(const std::vector<int32_t>& block) {
    std::vector<TlkTrigger> out;
    auto word = [&](size_t i) -> int32_t { return i < block.size() ? block[i] : 0; };
    size_t pos = 0;      // iVar2 (word index into +0x49c)
    int entry = -1;      // iVar1
    for (;;) {
        const int32_t kind = word(pos);
        if (kind == 0 || entry > 0x1f) break;
        const int n = tlkTriggerSize(kind);
        if (n == 0) break;  // original: no branch matches -> loop spins without advancing (never happens in data)
        TlkTrigger t;
        t.kind = kind;
        t.frame = viaFloat(word(pos + 1));
        switch (kind) {
        case 0x65: t.type = 1; t.w = word(pos + 2); t.f0 = static_cast<float>(word(pos + 3)); t.f1 = static_cast<float>(word(pos + 4)); t.f2 = static_cast<float>(word(pos + 5)); break;
        case 0x66: t.type = 2; t.w = word(pos + 2); break;
        case 0x67: t.type = 3; break;
        case 0x68: t.type = 4; t.name[0] = static_cast<uint32_t>(word(pos + 2)); t.name[1] = static_cast<uint32_t>(word(pos + 3)); t.g0 = static_cast<float>(word(pos + 4)); t.g1 = static_cast<float>(word(pos + 5)); break;
        case 0x69: t.type = 5; t.name[0] = static_cast<uint32_t>(word(pos + 2)); t.name[1] = static_cast<uint32_t>(word(pos + 3)); break;
        case 0x6a: case 0x6b:
            t.type = kind == 0x6a ? 6 : 7;
            t.name[0] = static_cast<uint32_t>(word(pos + 2)); t.name[1] = static_cast<uint32_t>(word(pos + 3));
            t.g0 = static_cast<float>(word(pos + 4)); t.g1 = static_cast<float>(word(pos + 5));
            break;
        case 0x6c: t.type = 8; t.w = word(pos + 2); break;
        case 0x6d: t.type = 9; t.w = word(pos + 2); break;
        case 0x6e: t.type = 10; t.w = word(pos + 2); t.f0 = static_cast<float>(word(pos + 3)); t.f1 = static_cast<float>(word(pos + 4)); t.f2 = static_cast<float>(word(pos + 5)); break;
        case 0x6f: t.type = 11; t.w = word(pos + 2); break;
        case 0x70: {
            t.type = 12;
            t.name[0] = static_cast<uint32_t>(word(pos + 2)); t.name[1] = static_cast<uint32_t>(word(pos + 3));
            t.w714 = word(pos + 4);
            // (float)(uint)word5 then the compiler's float->int sequence (saturating split at 2^31)
            const float fv = static_cast<float>(static_cast<uint32_t>(word(pos + 5)));
            if (fv < 2.1474836e+09f) t.w = static_cast<int32_t>(fv);
            else t.w = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(fv - 2.1474836e+09f)) | 0x80000000u);
            break;
        }
        case 0x71: t.type = 13; t.w = word(pos + 2); break;
        case 0x72: t.type = 14; break;
        case 0x73: t.type = 15; break;
        default: break;
        }
        ++entry;
        pos += static_cast<size_t>(n);
        out.push_back(t);
    }
    return out;
}

// Mirrors tools/game_formats.py parse_tlk() plus the original parser's state (FUN_001a7740 / FUN_001a77c0).
TlkScript parseTlkOrThrow(Bytes d) {
    if (d.size() != 2048) throw FormatError("TLK size != 2048");
    TlkScript s;
    s.textRaw.assign(d.begin(), d.begin() + 0x400);
    size_t end = 0x400;
    while (end > 0 && d[end - 1] == 0) --end;
    while (end > 0 && d[end - 1] == ' ') --end;
    s.text = makeSjis(d.subspan(0, end));
    s.sentences = splitSentences(d.subspan(0, end));
    s.word400 = u32le(d, 0x400);
    if (s.word400 != 1) throw FormatError("TLK word@0x400 = " + hex(s.word400));
    const size_t nWords = (d.size() - 0x404) / 4;
    auto word = [&](size_t i) -> int32_t {
        if (i >= nWords) throw FormatError("TLK stream has no terminator");
        return s32le(d, 0x404 + 4 * i);
    };
    size_t pc = 0;
    bool began = false;
    uint32_t guard = 0;
    for (;;) {
        const int32_t op = word(pc);
        if (op == 201) break;
        if (op == 2) {
            if (began) break;  // second 'begin page' ends the parse
            began = true;
            s.pageBegin = true;
            pc += 1;
        } else if (op == 3) { s.voice = word(pc + 1); pc += 2; }
        else if (op == 4) { s.sentence = word(pc + 1); pc += 2; }
        else if (op == 7) { s.speed = word(pc + 1); pc += 2; }
        else if (op == 8) { s.layout = word(pc + 1); pc += 2; }
        else if (op == 9) { s.timer = word(pc + 1); pc += 2; }
        else if (op == 100) {
            s.triggerBlock.clear();
            for (size_t i = 0; i < 0x94; ++i) s.triggerBlock.push_back((pc + 1 + i) < nWords ? s32le(d, 0x404 + 4 * (pc + 1 + i)) : 0);
            pc += 1 + 0x94;
            // python validates every trigger kind inside the block
            size_t q = 0;
            while (q < s.triggerBlock.size() && s.triggerBlock[q] != 0) {
                const int n = tlkTriggerSize(s.triggerBlock[q]);
                if (n == 0) throw FormatError("bad trigger kind " + std::to_string(s.triggerBlock[q]));
                q += static_cast<size_t>(n);
            }
        } else {
            throw FormatError("bad page opcode " + std::to_string(op) + " at word " + std::to_string(pc));
        }
        if (++guard > 0x8000) break;  // original iteration guard
    }
    s.streamWords = static_cast<uint32_t>(pc + 1);
    s.triggers = expandTlkTriggers(s.triggerBlock);
    return s;
}

std::optional<TlkScript> parseTlk(Bytes data, std::string* error) {
    return guarded([&] { return parseTlkOrThrow(data); }, error);
}

}  // namespace baroque::data
