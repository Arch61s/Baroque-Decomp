// DATA/EVENT{,_EN}/E%02d.EVT cut-scene scripts (work/reports/GAME_FORMATS.md section 4).
// Loader FUN_0016c6d0 -> parser FUN_0016be20; cue dispatcher FUN_00175e30 (semantics live in the game module).
#pragma once
#include "data/common.hpp"
#include <array>

namespace baroque::data {

// Cue opcodes of FUN_00175e30: 86 dispatched values (83 handlers; 30/48, 35/50 and 91/98 share one) plus
// 999 (Wait: pc++ and yield until the next frame) and 0 (end of stream).
enum class EvtCueOp : uint16_t {
    EndOfStream = 0,
    ShowText = 27, ClearText = 28, Effect29 = 29, SpawnObject30 = 30, Op31 = 31, AnimChange = 32, AnimSpeed = 33,
    Op34 = 34, Op35 = 35, Op36 = 36, Op37 = 37, Op38 = 38, Op39 = 39, Op40 = 40, Op41 = 41, Op42 = 42, Movie43 = 43,
    Op44 = 44, Timer45 = 45, Timer46 = 46, Op47 = 47, SpawnObject48 = 48, Op50 = 50, Op51 = 51, DropItem52 = 52,
    Op53 = 53, Camera55 = 55, CameraPos56 = 56, CameraTarget57 = 57, CameraUp58 = 58, CameraReset59 = 59,
    Camera60 = 60, Camera61 = 61, Camera62 = 62, Camera63 = 63, Light75 = 75, Light76 = 76, Op80 = 80, Op81 = 81,
    Env85 = 85, Env86 = 86, Env87 = 87, Env88 = 88, Effect90 = 90, Effect91 = 91, EffectCtl92 = 92, EffectCtl93 = 93,
    EffectCtl94 = 94, EffectCtl95 = 95, EffectCtl96 = 96, EffectCtl97 = 97, Effect98 = 98, Op100 = 100, Op101 = 101,
    Anim105 = 105, TextWindow110 = 110, TextWindow111 = 111, Fade112 = 112, FadeEnd113 = 113, Sound120 = 120,
    SE121 = 121, Sound123 = 123, SE124 = 124, SEStop125 = 125, SEStop126 = 126, BGM128 = 128, BGMStop129 = 129,
    BGMFade130 = 130, Voice132 = 132, Voice133 = 133, SoundCtl137 = 137, SoundCtl138 = 138, SoundCtl139 = 139,
    SoundCtl140 = 140, SoundCtl141 = 141, SoundCtl142 = 142, SetAction150 = 150, Op180 = 180, Op189 = 189,
    SetFlag190 = 190, ClearFlag191 = 191, Pad192 = 192, Op193 = 193, Op198 = 198, Op199 = 199, EndEvent200 = 200,
    Wait999 = 999,
};

struct EvtCueOpDesc {
    uint16_t op;
    const char* name;
    int8_t operands;      // words consumed after the opcode (re-derived from each handler's pc increments)
    uint32_t handler;     // original handler address (0 for Wait999)
};
inline constexpr int kEvtCueOpCount = 87;  // 86 dispatched + Wait999
const std::array<EvtCueOpDesc, kEvtCueOpCount>& evtCueOpTable();
const EvtCueOpDesc* evtCueOpDesc(int32_t op);   // nullptr when the dispatcher has no case

// Stream opcodes outside cue blocks (FUN_0016be20): opcode -> operand words.
int evtStreamOperands(int32_t op);  // -1 when unknown; 20 = cue block (64 words), 201 = end

struct EvtActor {                  // 0x54-byte actor slot at event+0x858 + slot*0x54
    bool used = false;             // slot created by opcode 5 (slot 0 may also be the implicit origin)
    int32_t id = 0;                // +0x8a8: the 'current id' (opcode 1) at the time of opcode 5
    bool hasPos = false;  int32_t pos[3] = {};      // 7: +0x858 (floats = value + map offset at runtime)
    bool hasW864 = false; int32_t w864 = 0;         // 6: +0x864
    bool lookAt = false;  int32_t look[3] = {};     // 8: +0x868 = 1, +0x86c.. (+ map offset)
    bool has15 = false;   int32_t a878 = 0, b87c = 0;  // 15: +0x878/+0x87c
    bool hasRot[3] = {};  uint32_t rot[3] = {};     // 9/10/11: +0x880/+0x884/+0x888 = (float)(uint)word
    bool has12 = false, has13 = false, has14 = false; int32_t v88c = 0, v890 = 0, v894 = 0;
    bool hasSpline = false; int32_t spline[4] = {}; // 16: +0x898.. only stored when (float)t != 0
};

struct EvtCue {
    std::array<int32_t, 64> words{};  // +0x6ad8 + i*0x104: command stream (word 0 = first opcode)
    int32_t fireValue = 0;            // word 64 (+0x100): the 'current id' (opcode 1) preceding the block;
                                      // FUN_00175e30 runs the cue when it equals event+4 (frame counter)
};

struct EvtCueInstr {
    int32_t op = 0;
    std::vector<int32_t> operands;
};
// Walks a cue block as FUN_00175e30 does (stops at 0 or 200; 999 is kept as an instruction). *ok = false when an
// opcode has no dispatcher case or operands run past the 64 words (the original would spin forever / read on).
std::vector<EvtCueInstr> decodeCueStream(const EvtCue& cue, bool* ok);

struct EvtFile {
    std::vector<uint8_t> textRaw;        // 0x800 bytes copied to event+0x4c
    SjisString text;                     // trailing NUL/space trimmed
    std::vector<SjisString> lines;       // ';'-separated (empty fields dropped)
    int32_t mapId = -1;                  // 0x800 (-1 none; else matched against map table 0x50a640 at runtime)
    int32_t cueCountMinus1 = 0;          // 0x804 (+0x84c = value + 1)
    std::vector<EvtActor> actors;        // slots 0..lastActorSlot
    int lastActorSlot = -1;              // +0x850
    int actorOps = 0;                    // number of opcode-5 occurrences (python 'actors')
    bool actorZeroIsOrigin = true;       // bVar2: first opcode 5 came with id 0 -> actor 0 position is copied back
    int writesBeforeFirstActor = 0;      // actor opcodes seen while slot == -1 (orig writes before the array)
    std::vector<EvtCue> cues;            // opcode-20 blocks in order; +0x854 = cues.size()-1
    uint32_t streamWords = 0;            // words consumed including 201
};

EvtFile parseEvtOrThrow(Bytes data);
std::optional<EvtFile> parseEvt(Bytes data, std::string* error = nullptr);
std::string evtPath(bool japanese, std::string_view name);  // "DATA/EVENT/%s" (0x44d120) / "DATA/EVENT_EN/%s" (0x44d130); name = "Exx.EVT"

}  // namespace baroque::data
