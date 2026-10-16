// DATA/EXI/BQ.EXI particle-effect definitions: IFF "FORM"/"EXIF" chunks parsed per FUN_001e8b20
// (work/decompiled/001e8b20.c; GAME_FORMATS.md section 6). The original relocates indices into pointers in place;
// here the indices are kept and range-checked against the referenced chunk.
#pragma once
#include "data/common.hpp"
#include <array>

namespace baroque::data {

struct ExiChunk {
    std::string tag;       // VTXS / KFMS / OBJS / EMTS / SEQS / RSQS / EFCS
    uint32_t offset = 0;   // of the 8-byte chunk header in the file
    uint32_t size = 0;     // incl. the 8-byte header
};

struct ExiVertex { std::array<uint8_t, 16> raw{}; };     // VTXS, 0x10 bytes each (contents not decoded here)
struct ExiKeyframe { uint32_t w0 = 0, w1 = 0; };          // KFMS, 8 bytes each

struct ExiObjectAnim {         // 17 x 8 bytes at OBJS+0x2c
    int16_t a = 0;             // +0 (set to 0 / 1 after load when b == 0 / 1)
    int16_t b = 0;             // +2
    uint32_t keyframe = 0;     // +4 KFMS index (x8 relocated)
};
struct ExiObject {             // OBJS, 0xb4 bytes
    std::array<uint8_t, 0xb4> raw{};
    uint16_t vertexCount = 0;  // +0x04 (summed into vertexBase of the following objects)
    uint8_t flags7 = 0;        // +0x07 (bit 2 -> +0x0e = 0xffff)
    uint32_t allOnes8 = 0;     // +0x08 computed: 1 when anim[13..16].a/b are all 1
    uint32_t vertexBase = 0;   // +0x18 computed running sum of vertexCount
    uint32_t vertexRef[4] = {};  // +0x1c..+0x28 VTXS indices (x0x10 relocated)
    std::array<ExiObjectAnim, 17> anim{};  // +0x2c
};
struct ExiEmitter {            // EMTS, 0xac bytes
    std::array<uint8_t, 0xac> raw{};
    uint32_t object = 0;       // +0x04 OBJS index (x0xb4)
    uint32_t keyframe[17] = {};  // +0x28 + 8k for k < 16, and +0xa8 (KFMS indices, x8)
};
struct ExiSequence {           // SEQS, 0x50 bytes
    std::array<uint8_t, 0x50> raw{};
    uint32_t ref = 0;          // +0x40 raw word: bit 15 -> EMTS index (x0xac) else OBJS index (x0xb4), index = ref & 0xfff
    bool refIsEmitter = false;
    uint32_t refIndex = 0;
};
struct ExiRsq { uint32_t w0 = 0, w1 = 0; };               // RSQS, 8 bytes
struct ExiEffect {             // EFCS, 0x28 bytes: the ids used by ACT Exi_a/Exi_b and EVT 29/90
    std::array<uint8_t, 0x28> raw{};
    uint32_t sequence = 0;     // +0x1c SEQS index (x0x50)
    int32_t rsqCount = 0;      // +0x20
    uint32_t rsq = 0;          // +0x24 RSQS index (x8); null when rsqCount < 1
};

struct ExiFile {
    uint32_t totalSize = 0;    // FORM size word (end of the chunk list)
    uint32_t version = 0;      // word after "EXIF" (1)
    std::vector<ExiChunk> chunks;
    std::vector<ExiVertex> vertices;
    std::vector<ExiKeyframe> keyframes;
    std::vector<ExiObject> objects;
    std::vector<ExiEmitter> emitters;
    std::vector<ExiSequence> sequences;
    std::vector<ExiRsq> rsqs;
    std::vector<ExiEffect> effects;
};

ExiFile parseExiOrThrow(Bytes data);
std::optional<ExiFile> parseExi(Bytes data, std::string* error = nullptr);
inline constexpr const char* kExiPath = "DATA/EXI/BQ.EXI";

}  // namespace baroque::data
