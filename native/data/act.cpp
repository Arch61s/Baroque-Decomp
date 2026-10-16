#include "data/act.hpp"
#include <cstdio>

namespace baroque::data {

namespace {

constexpr uint32_t kTagACTN = tag4("ACTN");  // 0x44b2f0
constexpr uint32_t kTagCMND = tag4("CMND");  // 0x44b2f8

struct NameHandler {
    const char* name;
    uint32_t handler;
};
constexpr NameHandler kNameHandler[kActCommandCount] = {
#include "data/tables/act_command_table.inc"
};

// Operand templates re-derived from each handler's operand reads (lwc1 / c.lt.s = float, lw = int, strings
// passed to the bone lookup vtable+0x70 = S). Differences from tools/game_formats.py ACT_OPERANDS are marked.
struct Layout {
    ActCommand cmd;
    bool container;
    uint8_t lead;
    const char* tmpl;
};
constexpr Layout kLayouts[kActCommandCount] = {
    {ActCommand::End, false, 0, "i"},          // thunk -> FUN_00143bc0 (operand unused)
    {ActCommand::Action, false, 0, "s8"},      // FUN_00146a90
    {ActCommand::Initial, true, 0, ""},        // FUN_00146ac0
    {ActCommand::Frame, true, 1, ""},          // FUN_00146af0: i frame
    {ActCommand::Frame_T, true, 1, ""},        // FUN_00146bb0: i frame
    {ActCommand::AnmSet, false, 0, "s4"},      // FUN_00146c50
    {ActCommand::AnmChg, false, 0, "s4"},      // FUN_00146d50
    {ActCommand::AnmChgEx, false, 0, "s4 f i f"},  // FUN_00146e60 (lwc1 +4, +0xc)
    {ActCommand::AnmSpeed, false, 0, "f"},     // FUN_001473b0
    {ActCommand::AnmFrame, false, 0, "i"},     // FUN_001473d0
    {ActCommand::Hit_a, false, 0, "i f i i i i f S"},        // FUN_00147720
    {ActCommand::Hit_b, false, 0, "i f i i i i f S S"},      // FUN_001477b0
    {ActCommand::Hit_c, false, 0, "i f i i i i f S f f f"},  // FUN_00147880: three lwc1 after the bone (python: f f i)
    {ActCommand::HitEnd, false, 0, "i"},       // FUN_00147960
    {ActCommand::Move, false, 0, "f f"},       // FUN_001479d0
    {ActCommand::MoveAcc, false, 0, "f"},      // FUN_00147a70
    {ActCommand::MoveJump, false, 0, "f f f"}, // FUN_00147aa0
    {ActCommand::Pad, false, 0, "i"},          // FUN_00147b90
    {ActCommand::PadEnd, true, 1, ""},         // FUN_00147bf0: i btn
    {ActCommand::BGM, false, 0, "i"},          // FUN_00147d20
    {ActCommand::Voice, false, 0, "i"},        // FUN_00147d40
    {ActCommand::SE, false, 0, "i"},           // FUN_00147d60
    {ActCommand::SEvol, false, 0, "i f"},      // FUN_00147d90
    {ActCommand::SEatr, false, 0, "i i i i"},  // FUN_00147db0
    {ActCommand::SEadx, false, 0, "i i"},      // FUN_001480b0
    {ActCommand::SEadxEnd, false, 0, "i"},     // FUN_00148110
    {ActCommand::Exi_a, false, 0, "i i i f f f f f"},    // FUN_00148120: lwc1 +0xc..+0x1c (python: i i i i f i i i)
    {ActCommand::Exi_b, false, 0, "i i i f f f f f S"},  // FUN_001481f0: c.lt.s on +0xc, lwc1 +0x10..+0x1c
    {ActCommand::ExiZofs, false, 0, "i f"},    // FUN_00148320
    {ActCommand::ExiLpOut, false, 0, "i"},     // FUN_00148330
    {ActCommand::ExiEnd, false, 0, "i"},       // FUN_00148340
    {ActCommand::Object, false, 0, "s4 f f f S"},        // FUN_00148350
    {ActCommand::ObjThrow, false, 0, "s4 f f f f f S"},  // FUN_001485a0: [4],[5] via mtc1, no cvt (python: i i)
    {ActCommand::ThrowItm, false, 0, "s4 f f f f f f S"},  // FUN_00148880: lwc1 +0x10,+0x14,+0x18 (python: i f f)
    {ActCommand::DropItem, false, 0, "s4 f f f S"},      // FUN_00149000
    {ActCommand::ClearItm, false, 0, "i"},     // FUN_00149210
    {ActCommand::Track, false, 0, "i S"},      // FUN_001492b0
    {ActCommand::TrackEx, false, 0, "i i i i S"},        // FUN_00149320
    {ActCommand::TrackEnd, false, 0, "i i"},   // FUN_00149390
    {ActCommand::Fade, false, 0, "i i i f f f f f f f f"},  // FUN_001493f0
    {ActCommand::FadeEnd, false, 0, "i"},      // FUN_00149470
    {ActCommand::Enemy, false, 0, "s4 s8 f f f f S"},    // FUN_00149480
    {ActCommand::CollsRes, true, 1, ""},       // FUN_00149630: i mask
    {ActCommand::ChrColls, false, 0, "f f"},   // FUN_00149730
    {ActCommand::NTurnPos, false, 0, "f f f"}, // FUN_00149750
    {ActCommand::NTurnAct, false, 0, "s4 S"},  // FUN_00149840
    {ActCommand::NTurnEnd, false, 0, "i"},     // FUN_00149970
    {ActCommand::RotYPos, false, 0, "f f f f"},  // FUN_00149980
    {ActCommand::RotYAct, false, 0, "s4 f f"}, // FUN_00149a70
    {ActCommand::RotYEnd, false, 0, "i"},      // thunk -> FUN_001460d0
    {ActCommand::Event, false, 0, "i"},        // FUN_00149b70
    {ActCommand::Destroy, false, 0, "i"},      // FUN_00149b80
    {ActCommand::ShowBox, false, 0, "i"},      // FUN_00149b90 (operand unused)
    {ActCommand::HideBox, false, 0, "i"},      // FUN_00149c00 (operand unused)
    {ActCommand::SetPos, false, 0, "f f f f"}, // FUN_00149c70
    {ActCommand::SetScale, false, 0, "f f f"}, // FUN_00149dc0
    {ActCommand::AnmScale, false, 0, "i"},     // FUN_00149df0
    {ActCommand::Invincib, false, 0, "i"},     // 0x149e00 (not in the Ghidra export; disasm: 0 clears, else sets flag 0x1000000)
    {ActCommand::Warp, false, 0, "i"},         // FUN_00149e40
    {ActCommand::SetFlag, false, 0, "i i"},    // FUN_00149ff0
    {ActCommand::ChkFlag, true, 2, ""},        // FUN_0014a040: i flag, i val
    {ActCommand::GetItem, false, 0, "i"},      // FUN_0014a0b0
    {ActCommand::ShowItem, false, 0, "f f f S"},  // FUN_0014a100: vec3 then bone at +0xc (python: i f f S)
    {ActCommand::HideItem, false, 0, "i"},     // FUN_0014a2e0
    {ActCommand::FpsView, true, 0, ""},        // FUN_0014a340
    {ActCommand::NrmView, true, 0, ""},        // FUN_0014a380
};

std::array<ActCommandDesc, kActCommandCount> buildTable() {
    std::array<ActCommandDesc, kActCommandCount> t{};
    for (int i = 0; i < kActCommandCount; ++i) {
        t[static_cast<size_t>(i)] = {kNameHandler[i].name, kNameHandler[i].handler, kLayouts[i].container, kLayouts[i].lead, kLayouts[i].tmpl};
    }
    return t;
}

// Decodes a template like "s4 f i f" over the scalar body; leftovers become Raw words (python decode_operands).
void decodeOperands(const char* tmpl, Bytes body, std::vector<ActOperand>& out) {
    size_t q = 0;
    const char* p = tmpl;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* e = p;
        while (*e && *e != ' ') ++e;
        const std::string_view t(p, static_cast<size_t>(e - p));
        p = e;
        if (q >= body.size()) break;
        ActOperand op;
        if (t == "i") {
            op.kind = ActOperandKind::Int;
            op.i = s32le(body, q);
            q += 4;
        } else if (t == "f") {
            op.kind = ActOperandKind::Float;
            op.i = s32le(body, q);
            op.f = f32le(body, q);
            q += 4;
        } else if (t == "s4") {
            op.kind = ActOperandKind::Id4;
            op.i = s32le(body, q);
            op.s = fixedString(body, q, 4);
            q += 4;
        } else if (t == "s8") {
            op.kind = ActOperandKind::Name8;
            needBytes(body, q, 8);
            op.s = fixedString(body, q, 8);
            q += 8;
        } else if (t == "S") {
            op.kind = ActOperandKind::Str;
            const Bytes s = cstringAt(body, q);
            op.s = asciiString(s);
            q += (s.size() + 4) & ~static_cast<size_t>(3);
        } else {
            throw FormatError("bad operand template " + std::string(t));
        }
        out.push_back(std::move(op));
    }
    while (q + 4 <= body.size()) {
        ActOperand op;
        op.kind = ActOperandKind::Raw;
        op.i = s32le(body, q);
        out.push_back(op);
        q += 4;
    }
}

std::vector<ActCmnd> parseCmnds(Bytes file, size_t p, size_t end, int depth) {
    std::vector<ActCmnd> cmds;
    while (p + 8 <= end) {
        const uint32_t tag = u32le(file, p);
        if (tag != kTagCMND) {
            if (tag != kTagACTN && tag != 0) throw FormatError("bad chunk tag at " + hex(p));
            break;
        }
        const uint32_t size = u32le(file, p + 4);
        if (size < 12 || p + 8 + size > end || (size % 4) != 0) throw FormatError("bad CMND size " + std::to_string(size) + " at " + hex(p));
        ActCmnd c;
        c.fileOffset = static_cast<uint32_t>(p);
        c.size = size;
        c.name = fixedString(file, p + 8, 8);
        const auto cmd = findActCommand(file.subspan(p + 8, 8));
        if (!cmd) throw FormatError("unknown command '" + c.name + "' at " + hex(p));
        c.cmd = *cmd;
        c.argc = u32le(file, p + 16);
        const size_t bodyStart = p + 20, bodyEnd = p + 8 + size;
        const Bytes body = file.subspan(bodyStart, bodyEnd - bodyStart);
        const Layout& lay = kLayouts[static_cast<size_t>(*cmd)];
        if (lay.container) {
            for (uint8_t i = 0; i < lay.lead; ++i) {
                ActOperand op;
                op.kind = ActOperandKind::Int;
                op.i = s32le(body, 4u * i);
                c.args.push_back(op);
            }
            c.body = parseCmnds(file, bodyStart + 4u * lay.lead, bodyEnd, depth + 1);
            if (c.body.size() != static_cast<size_t>(c.argc) - lay.lead)
                throw FormatError(c.name + " argc=" + std::to_string(c.argc) + " lead=" + std::to_string(lay.lead) + " nested=" + std::to_string(c.body.size()) + " at " + hex(p));
        } else {
            for (size_t q = 0; q + 4 <= body.size(); ++q) {
                if (u32le(body, q) == kTagCMND) throw FormatError("unexpected nested CMND in " + c.name + " at " + hex(p));
            }
            decodeOperands(lay.tmpl, body, c.args);
        }
        cmds.push_back(std::move(c));
        p += 8 + size;
    }
    return cmds;
}

}  // namespace

const std::array<ActCommandDesc, kActCommandCount>& actCommandTable() {
    static const std::array<ActCommandDesc, kActCommandCount> table = buildTable();
    return table;
}

// orig FUN_00146950 (work/decompiled/00146950.c): two-word compare against entries 0..0x41.
std::optional<ActCommand> findActCommand(Bytes name8) {
    if (name8.size() < 8) return std::nullopt;
    const uint32_t w0 = u32le(name8, 0), w1 = u32le(name8, 4);
    for (uint32_t i = 0; i <= 0x41; ++i) {
        const auto padded = padName8(kNameHandler[i].name);
        const Bytes pb(padded.data(), 8);
        if (u32le(pb, 0) == w0 && u32le(pb, 4) == w1) return static_cast<ActCommand>(i);
    }
    return std::nullopt;
}

std::array<uint8_t, 8> padName8(std::string_view name) {
    std::array<uint8_t, 8> out{};
    for (size_t i = 0; i < 8 && i < name.size(); ++i) out[i] = static_cast<uint8_t>(name[i]);
    return out;
}

// Mirrors tools/game_formats.py parse_act(): records until a non-'ACTN' word; the list terminator must be 0.
ActFile parseActOrThrow(Bytes d) {
    if (d.size() % 2048 != 0) throw FormatError("ACT size not a multiple of 2048");
    ActFile f;
    size_t p = 0;
    while (p + 8 <= d.size() && u32le(d, p) == kTagACTN) {
        const uint32_t size = u32le(d, p + 4);
        if (size < 8 || p + 8 + size > d.size() || (size % 4) != 0) throw FormatError("bad ACTN size at " + hex(p));
        ActAction a;
        a.fileOffset = static_cast<uint32_t>(p);
        a.size = size;
        a.name = fixedString(d, p + 8, 8);
        a.cmds = parseCmnds(d, p + 16, p + 8 + size, 0);
        f.actions.push_back(std::move(a));
        p += 8 + size;
    }
    // FUN_00143fe0 stops on a zero word or an 'ACTN' tag; bytes after the terminator are stale buffer contents.
    if (f.actions.empty() || (p < d.size() && u32le(d, p) != 0)) throw FormatError("bad ACTN list terminator");
    f.raw.assign(d.begin(), d.end());
    return f;
}

std::optional<ActFile> parseAct(Bytes data, std::string* error) {
    return guarded([&] { return parseActOrThrow(data); }, error);
}

// orig FUN_00143aa0 (work/decompiled/00143aa0.c)
const ActAction* findAction(const ActFile& file, Bytes name8) {
    if (name8.size() < 8 || file.raw.size() < 16) return nullptr;
    const Bytes d(file.raw);
    if (u32le(d, 0) != kTagACTN) return nullptr;
    const uint32_t w0 = u32le(name8, 0), w1 = u32le(name8, 4);
    size_t p = 0;
    for (size_t idx = 0;; ++idx) {
        if (u32le(d, p + 8) == w0 && u32le(d, p + 12) == w1) return idx < file.actions.size() ? &file.actions[idx] : nullptr;
        const size_t next = p + u32le(d, p + 4) + 8;
        if (next + 8 > d.size() || u32le(d, next) != kTagACTN) return nullptr;
        p = next;
    }
}

const ActAction* findAction(const ActFile& file, std::string_view name) {
    const auto n = padName8(name);
    return findAction(file, Bytes(n.data(), 8));
}

// orig FUN_00143d30 (work/decompiled/00143d30.c), index >= 0 branch
const ActAction* actionByIndex(const ActFile& file, int index) {
    if (index < 0 || static_cast<size_t>(index) >= file.actions.size()) return nullptr;
    return &file.actions[static_cast<size_t>(index)];
}

// ---------------------------------------------------------------- cache (0x50c6e0)
std::string ActCache::pathFor(uint32_t chrId) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "DATA/ACTION/%c%c%c%c.ACT", static_cast<char>(chrId & 0xff), static_cast<char>((chrId >> 8) & 0xff),
                  static_cast<char>((chrId >> 16) & 0xff), static_cast<char>(chrId >> 24));
    return buf;
}

// orig FUN_00143820 (work/decompiled/00143820.c)
void ActCache::clear() {
    for (auto& s : slots_) {
        s.refCount = 0;
        s.data.reset();
    }
}

// orig FUN_00143850 (work/decompiled/00143850.c): slots held only by the cache (refcount 1) are freed.
void ActCache::sweep() {
    for (auto& s : slots_) {
        if (s.refCount == 1) {
            s.refCount = 0;
            s.data.reset();
        }
    }
}

// orig FUN_001442a0 (work/decompiled/001442a0.c)
int ActCache::acquire(uint32_t chrId, const ExistsFn& exists, const LoadFn& load) {
    for (int i = 0; i < kSlots; ++i) {
        Slot& s = slots_[static_cast<size_t>(i)];
        if (s.refCount != 0 && s.id == chrId) {
            s.refCount += 1;
            return i;
        }
    }
    for (int i = 0; i < kSlots; ++i) {
        Slot& s = slots_[static_cast<size_t>(i)];
        if (s.refCount == 0) {
            const std::string path = pathFor(chrId);
            if (!exists(path)) return -1;  // orig: file-system vtable+0x18 returned 0 -> return 0
            s.data.reset();
            s.id = chrId;
            s.refCount = 2;  // verbatim: one for the cache, one for the requesting player
            onLoaded(i, load(path));  // orig: FUN_0010cf60 async load, callback FUN_00144450
            return i;
        }
    }
    return -1;
}

// orig FUN_00144450 (work/decompiled/00144450.c)
void ActCache::onLoaded(int slot, std::shared_ptr<const ActFile> data) {
    if (slot >= 0 && slot < kSlots) slots_[static_cast<size_t>(slot)].data = std::move(data);
}

// orig FUN_00143970 (work/decompiled/00143970.c): `if (-1 < slot) refcount--`
void ActCache::release(int slot) {
    if (slot >= 0 && slot < kSlots) slots_[static_cast<size_t>(slot)].refCount -= 1;
}

}  // namespace baroque::data
