// DATA/ACTION/*.ACT character action scripts: container parser, command table and loader cache.
// Format and functions: work/reports/GAME_FORMATS.md section 1 (loader FUN_001442a0, lookup FUN_00143aa0,
// dispatch FUN_00146950, nested executor FUN_00146a10). No gameplay side effects live here: the game module
// executes the decoded command list.
#pragma once
#include "data/common.hpp"
#include <array>
#include <functional>
#include <memory>

namespace baroque::data {

// Index = position in the dispatch table at 0x48e600 (66 x 0x18, filled by static ctors 0x4c4ea0..).
enum class ActCommand : uint8_t {
    End, Action, Initial, Frame, Frame_T, AnmSet, AnmChg, AnmChgEx, AnmSpeed, AnmFrame,
    Hit_a, Hit_b, Hit_c, HitEnd, Move, MoveAcc, MoveJump, Pad, PadEnd, BGM, Voice, SE,
    SEvol, SEatr, SEadx, SEadxEnd, Exi_a, Exi_b, ExiZofs, ExiLpOut, ExiEnd, Object,
    ObjThrow, ThrowItm, DropItem, ClearItm, Track, TrackEx, TrackEnd, Fade, FadeEnd, Enemy,
    CollsRes, ChrColls, NTurnPos, NTurnAct, NTurnEnd, RotYPos, RotYAct, RotYEnd, Event,
    Destroy, ShowBox, HideBox, SetPos, SetScale, AnmScale, Invincib, Warp, SetFlag, ChkFlag,
    GetItem, ShowItem, HideItem, FpsView, NrmView,
    Count
};
inline constexpr int kActCommandCount = 66;

// Operand kinds of the scalar operands (each counts as one `argc` argument).
enum class ActOperandKind : uint8_t {
    Int,    // i  : 4-byte int
    Float,  // f  : 4-byte float
    Id4,    // s4 : 4-char id ("M001", "C00A", "ITEM")
    Name8,  // s8 : 8-char action name (two words)
    Str,    // S  : NUL-terminated string padded to 4 bytes (bone names)
    Raw,    // leftover word not covered by the template (kept verbatim)
};

struct ActCommandDesc {
    const char* name;           // 8-byte name as stored in the table / CMND record
    uint32_t handler;           // original handler address (PTMF fn at 0x44b358 + 0x10*i); thunks for End/RotYEnd
    bool container;             // handler runs a nested CMND list through FUN_00146a10
    uint8_t leadScalars;        // scalar operands before the nested list (containers only)
    const char* operands;       // scalar template, e.g. "s4 f i f" (verified from the handler code, see act.cpp)
};
const std::array<ActCommandDesc, kActCommandCount>& actCommandTable();
// orig FUN_00146950 name match (two-word compare over indices 0..0x41); nullopt when unknown.
std::optional<ActCommand> findActCommand(Bytes name8);

struct ActOperand {
    ActOperandKind kind = ActOperandKind::Raw;
    int32_t i = 0;       // Int / Raw (bit pattern); also the bits of Float
    float f = 0.0f;      // Float
    std::string s;       // Id4 / Name8 / Str (NULs trimmed)
};

struct ActCmnd {
    ActCommand cmd = ActCommand::End;
    std::string name;                 // raw 8-char name
    uint32_t argc = 0;                // +0x10: argument count (nested CMND = 1, name8 = 1, scalar = 1, string = 1)
    std::vector<ActOperand> args;     // scalar operands (containers: the lead scalars as Int)
    std::vector<ActCmnd> body;        // nested commands (containers only)
    uint32_t fileOffset = 0;          // offset of the 'CMND' tag in the file
    uint32_t size = 0;                // +4: bytes after the 8-byte header
};

struct ActAction {
    std::string name;                 // 8-char action name (e.g. "WT01_S1")
    std::vector<ActCmnd> cmds;
    uint32_t fileOffset = 0;          // offset of the 'ACTN' tag
    uint32_t size = 0;
};

struct ActFile {
    std::vector<ActAction> actions;
    std::vector<uint8_t> raw;         // the file as loaded (the game walks it in place)
};

// Parses a whole .ACT image with the asserts of tools/game_formats.py parse_act(). Throws FormatError.
ActFile parseActOrThrow(Bytes data);
std::optional<ActFile> parseAct(Bytes data, std::string* error = nullptr);

// orig FUN_00143aa0 (work/decompiled/00143aa0.c): walk ACTN records comparing the two name words. name8 is
// the 8-byte padded name. Returns nullptr when absent (orig returns 0 and leaves the player untouched).
const ActAction* findAction(const ActFile& file, Bytes name8);
const ActAction* findAction(const ActFile& file, std::string_view name);   // pads/truncates to 8 (FUN_00143a40)
// orig FUN_00143d30 with index >= 0: the n-th ACTN record (nullptr past the end).
const ActAction* actionByIndex(const ActFile& file, int index);
// Byte offset -> 8-byte padded name helper used by callers that keep names as two words.
std::array<uint8_t, 8> padName8(std::string_view name);

// orig FUN_001442a0 cache: 10 slots of {data*, chrId, refcount} at 0x50c6e0 (12 bytes each).
class ActCache {
public:
    struct Slot {
        std::shared_ptr<const ActFile> data;   // +0  (0 until the async load callback FUN_00144450 stores it)
        uint32_t id = 0;                       // +4  4-char character id (chr+0x1c)
        int refCount = 0;                      // +8  0 = free
    };
    using ExistsFn = std::function<bool(const std::string& path)>;
    using LoadFn = std::function<std::shared_ptr<const ActFile>(const std::string& path)>;

    static std::string pathFor(uint32_t chrId);        // "DATA/ACTION/%c%c%c%c.ACT" (0x44b300)
    void clear();                                      // orig FUN_00143820: all slots data=0, refcount=0
    void sweep();                                      // orig FUN_00143850: free slots whose refcount == 1
    // orig FUN_001442a0: returns the slot index (player+8) or -1 when the original returns 0
    // (no free slot, or the file does not exist). A fresh load starts with refcount 2 (verbatim).
    int acquire(uint32_t chrId, const ExistsFn& exists, const LoadFn& load);
    void onLoaded(int slot, std::shared_ptr<const ActFile> data);  // orig FUN_00144450
    void release(int slot);                            // orig FUN_00143970 fragment: refcount-- when slot >= 0
    const Slot& slot(int i) const { return slots_[static_cast<size_t>(i)]; }
    static constexpr int kSlots = 10;

private:
    std::array<Slot, kSlots> slots_{};
};

}  // namespace baroque::data
