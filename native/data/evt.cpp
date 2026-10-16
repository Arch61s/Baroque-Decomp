#include "data/evt.hpp"

namespace baroque::data {

namespace {
// Operand counts: pc (+0x20964) increments of each handler, validated against all 5181 cue blocks of the 108 files.
constexpr std::array<EvtCueOpDesc, kEvtCueOpCount> kCueOps = {{
    {27, "ShowText", 6, 0x0016f690},      // line index, slot, x, y, w, h -> FUN_0015c490
    {28, "ClearText", 1, 0x0016fab0},
    {29, "Effect29", 4, 0x0016fcb0},      // effect id, x, y, z -> FUN_00112190
    {30, "SpawnObject30", 4, 0x0016fe70},
    {31, "Op31", 2, 0x00170060},
    {32, "AnimChange", 5, 0x00170480},    // -> FUN_0010a8e0
    {33, "AnimSpeed", 2, 0x00170870},
    {34, "Op34", 4, 0x00170ea0},
    {35, "Op35", 5, 0x00171100},          // shares the handler with 50, which reads 3 more words (/1000 vector)
    {36, "Op36", 1, 0x00171510},
    {37, "Op37", 3, 0x00171670},
    {38, "Op38", 3, 0x00171c40},
    {39, "Op39", 1, 0x00172920},
    {40, "Op40", 2, 0x00172990},
    {41, "Op41", 1, 0x00172ad0},
    {42, "Op42", 1, 0x00172d40},
    {43, "Movie43", 0, 0x00172de0},       // -> FUN_00177c40 [I movie]
    {44, "Op44", 2, 0x00172e40},
    {45, "Timer45", 1, 0x00172f20},       // -> FUN_00101b60
    {46, "Timer46", 1, 0x00172fb0},
    {47, "Op47", 2, 0x00173050},
    {48, "SpawnObject48", 4, 0x0016fe70},
    {50, "Op50", 8, 0x00171100},
    {51, "Op51", 4, 0x00171e20},
    {52, "DropItem52", 1, 0x00175ae0},    // -> FUN_001d5410
    {53, "Op53", 1, 0x00175ca0},
    {55, "Camera55", 0, 0x001730c0},
    {56, "CameraPos56", 3, 0x00173290},
    {57, "CameraTarget57", 3, 0x00173520},
    {58, "CameraUp58", 3, 0x00173650},
    {59, "CameraReset59", 0, 0x00173230},
    {60, "Camera60", 3, 0x00173780},
    {61, "Camera61", 3, 0x001738a0},
    {62, "Camera62", 3, 0x001739d0},
    {63, "Camera63", 4, 0x001733c0},
    {75, "Light75", 3, 0x00173b00},
    {76, "Light76", 1, 0x00173bd0},
    {80, "Op80", 3, 0x00173c30},
    {81, "Op81", 1, 0x00173d30},
    {85, "Env85", 3, 0x00173d90},
    {86, "Env86", 1, 0x00173e60},
    {87, "Env87", 0, 0x00173ec0},
    {88, "Env88", 0, 0x00173f10},
    {90, "Effect90", 5, 0x001741a0},
    {91, "Effect91", 5, 0x001743e0},      // shares the handler with 98, which reads 3 more words
    {92, "EffectCtl92", 4, 0x00174720},   // -> FUN_00112490
    {93, "EffectCtl93", 2, 0x001748a0},
    {94, "EffectCtl94", 4, 0x00174980},
    {95, "EffectCtl95", 2, 0x00174b20},
    {96, "EffectCtl96", 2, 0x00174bf0},
    {97, "EffectCtl97", 1, 0x00174cd0},
    {98, "Effect98", 8, 0x001743e0},
    {100, "Op100", 3, 0x00171860},
    {101, "Op101", 3, 0x00171a50},
    {105, "Anim105", 5, 0x00170a90},
    {110, "TextWindow110", 2, 0x0016fb10},
    {111, "TextWindow111", 3, 0x0016fbc0},
    {112, "Fade112", 11, 0x00172090},     // -> FUN_0010a0a0
    {113, "FadeEnd113", 1, 0x00172700},
    {120, "Sound120", 3, 0x00174e50},     // -> FUN_00115390
    {121, "SE121", 4, 0x00174f20},        // -> FUN_00115470
    {123, "Sound123", 3, 0x00175090},
    {124, "SE124", 4, 0x00175160},
    {125, "SEStop125", 1, 0x001752d0},
    {126, "SEStop126", 1, 0x00175370},
    {128, "BGM128", 1, 0x00175530},       // -> FUN_00115d70
    {129, "BGMStop129", 0, 0x001755d0},
    {130, "BGMFade130", 2, 0x00175600},   // -> FUN_00115f00
    {132, "Voice132", 3, 0x001754a0},     // -> FUN_00115a60 (report said 1 operand; the handler advances 3)
    {133, "Voice133", 4, 0x00175400},
    {137, "SoundCtl137", 1, 0x001756c0},
    {138, "SoundCtl138", 1, 0x00175720},
    {139, "SoundCtl139", 1, 0x00175780},
    {140, "SoundCtl140", 1, 0x001757e0},
    {141, "SoundCtl141", 1, 0x00175850},
    {142, "SoundCtl142", 1, 0x001758b0},
    {150, "SetAction150", 3, 0x00170210}, // -> FUN_00143a40
    {180, "Op180", 1, 0x00175a70},
    {189, "Op189", 2, 0x00172b40},
    {190, "SetFlag190", 1, 0x00174d90},
    {191, "ClearFlag191", 1, 0x00174df0},
    {192, "Pad192", 3, 0x001758e0},
    {193, "Op193", 1, 0x00175a00},
    {198, "Op198", 0, 0x00173f60},
    {199, "Op199", 0, 0x00174100},
    {200, "EndEvent200", 0, 0x00175e10},
    {999, "Wait999", 0, 0},
}};

std::vector<SjisString> splitLines(Bytes text) {
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

const std::array<EvtCueOpDesc, kEvtCueOpCount>& evtCueOpTable() { return kCueOps; }

const EvtCueOpDesc* evtCueOpDesc(int32_t op) {
    for (const auto& d : kCueOps)
        if (d.op == op) return &d;
    return nullptr;
}

int evtStreamOperands(int32_t op) {
    switch (op) {
    case 1: return 1; case 5: return 0; case 6: return 1; case 7: return 3; case 8: return 3;
    case 9: case 10: case 11: case 12: case 13: case 14: return 1;
    case 15: return 2; case 16: return 4; case 20: return 64; case 201: return 0;
    default: return -1;
    }
}

// orig FUN_00175e30 (work/decompiled/00175e30.c) walk, with the operand counts of the handlers.
std::vector<EvtCueInstr> decodeCueStream(const EvtCue& cue, bool* ok) {
    std::vector<EvtCueInstr> out;
    bool good = true;
    size_t pc = 0;
    while (pc < cue.words.size()) {
        const int32_t op = cue.words[pc];
        if (op == 0) break;
        const EvtCueOpDesc* d = evtCueOpDesc(op);
        if (!d) { good = false; break; }
        EvtCueInstr in;
        in.op = op;
        if (pc + 1 + static_cast<size_t>(d->operands) > cue.words.size()) { good = false; out.push_back(in); break; }
        for (int i = 0; i < d->operands; ++i) in.operands.push_back(cue.words[pc + 1 + static_cast<size_t>(i)]);
        pc += 1 + static_cast<size_t>(d->operands);
        out.push_back(std::move(in));
        if (op == 200) break;  // FUN_00175e10 ends the event; dispatcher returns 0
    }
    if (ok) *ok = good;
    return out;
}

// Mirrors FUN_0016be20 (work/decompiled/0016be20.c) and the asserts of tools/game_formats.py parse_evt().
EvtFile parseEvtOrThrow(Bytes d) {
    if (d.size() % 2048 != 0 || d.size() <= 0x808) throw FormatError("bad EVT size");
    EvtFile f;
    f.textRaw.assign(d.begin(), d.begin() + 0x800);
    size_t end = 0x800;
    while (end > 0 && d[end - 1] == 0) --end;
    while (end > 0 && d[end - 1] == ' ') --end;
    f.text = makeSjis(d.subspan(0, end));
    f.lines = splitLines(d.subspan(0, end));
    f.mapId = s32le(d, 0x800);
    f.cueCountMinus1 = s32le(d, 0x804);
    const size_t nWords = (d.size() - 0x808) / 4;
    auto word = [&](size_t i) -> int32_t {
        if (i >= nWords) throw FormatError("EVT stream has no terminator");
        return s32le(d, 0x808 + 4 * i);
    };
    auto actorAt = [&](int slot) -> EvtActor* {
        if (slot < 0) { ++f.writesBeforeFirstActor; return nullptr; }
        if (static_cast<size_t>(slot) >= f.actors.size()) f.actors.resize(static_cast<size_t>(slot) + 1);
        return &f.actors[static_cast<size_t>(slot)];
    };
    size_t pc = 0;        // iVar15
    int32_t id = 0;       // iVar11
    int slot = -1;        // iVar14
    int cueIdx = -1;      // iVar12
    uint32_t guard = 0;   // uVar13
    bool ended = false;
    while (!ended) {
        const int32_t op = word(pc);
        switch (op) {
        case 201: ended = true; break;
        case 1: id = word(pc + 1); pc += 2; break;
        case 5: {
            int next = slot + 1;
            if (next == 0 && id != 0) {
                f.actorZeroIsOrigin = false;
                next = slot + 2;
                if (next > 299) throw FormatError("EVT actor slots exhausted (FUN_0016cbf0)");
            }
            slot = next;
            ++f.actorOps;
            pc += 1;
            if (EvtActor* a = actorAt(slot)) { a->used = true; a->id = id; }
            break;
        }
        case 7: { EvtActor* a = actorAt(slot); if (a) { a->hasPos = true; for (int i = 0; i < 3; ++i) a->pos[i] = word(pc + 1 + static_cast<size_t>(i)); } pc += 4; break; }
        case 6: { EvtActor* a = actorAt(slot); if (a) { a->hasW864 = true; a->w864 = word(pc + 1); } pc += 2; break; }
        case 8: { EvtActor* a = actorAt(slot); if (a) { a->lookAt = true; for (int i = 0; i < 3; ++i) a->look[i] = word(pc + 1 + static_cast<size_t>(i)); } pc += 4; break; }
        case 15: { EvtActor* a = actorAt(slot); if (a) { a->has15 = true; a->a878 = word(pc + 1); a->b87c = word(pc + 2); } pc += 3; break; }
        case 9: case 10: case 11: { EvtActor* a = actorAt(slot); if (a) { a->hasRot[op - 9] = true; a->rot[op - 9] = static_cast<uint32_t>(word(pc + 1)); } pc += 2; break; }
        case 12: { EvtActor* a = actorAt(slot); if (a) { a->has12 = true; a->v88c = word(pc + 1); } pc += 2; break; }
        case 13: { EvtActor* a = actorAt(slot); if (a) { a->has13 = true; a->v890 = word(pc + 1); } pc += 2; break; }
        case 14: { EvtActor* a = actorAt(slot); if (a) { a->has14 = true; a->v894 = word(pc + 1); } pc += 2; break; }
        case 16: {
            const int32_t t = word(pc + 4);
            if (static_cast<float>(t) != 0.0f) {
                if (EvtActor* a = actorAt(slot)) { a->hasSpline = true; for (int i = 0; i < 4; ++i) a->spline[i] = word(pc + 1 + static_cast<size_t>(i)); }
            }
            pc += 5;
            break;
        }
        case 20: {
            ++cueIdx;
            if (cueIdx > 399) throw FormatError("EVT cue slots exhausted (FUN_0016cbf0)");
            pc += 1;
            EvtCue c;
            for (size_t i = 0; i < 64; ++i) c.words[i] = word(pc + i);
            pc += 64;
            c.fireValue = id;
            if (!(c.words[0] == 0 || evtCueOpDesc(c.words[0]) != nullptr)) throw FormatError("EVT cue starts with unknown opcode " + std::to_string(c.words[0]));
            f.cues.push_back(c);
            break;
        }
        default:
            // The original leaves pc unchanged and spins until the 0x8000 guard; the python tool asserts instead.
            throw FormatError("bad EVT opcode " + std::to_string(op) + " at word " + std::to_string(pc));
        }
        if (!ended && ++guard > 0x8000) break;
    }
    f.streamWords = static_cast<uint32_t>(pc + 1);
    f.lastActorSlot = slot;
    if (f.actorOps > 300 || f.cues.size() > 400) throw FormatError("EVT actor/cue count out of range");
    if (f.cueCountMinus1 + 1 < static_cast<int32_t>(f.cues.size()) - 1) throw FormatError("EVT cue count header mismatch");
    return f;
}

std::optional<EvtFile> parseEvt(Bytes data, std::string* error) {
    return guarded([&] { return parseEvtOrThrow(data); }, error);
}

std::string evtPath(bool japanese, std::string_view name) {
    return std::string(japanese ? "DATA/EVENT/" : "DATA/EVENT_EN/") + std::string(name);
}

}  // namespace baroque::data
