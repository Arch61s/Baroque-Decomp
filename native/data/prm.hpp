// DATA/PARAM{,_EN}/PRM*.PRM parameter tables (work/reports/GAME_FORMATS.md section 2).
// PRMCHAR.PRM: loader FUN_00133620 + callback FUN_00133480, lookup FUN_00133560.
// PRMITEM00..13.PRM: loader FUN_001d3530 + callback FUN_001d3600, record accessor FUN_001d2c90 (jump table 0x499450).
// Files are a record array terminated by a zero id word followed by the string pool; +4/+8 of every record are
// file offsets of the name/description strings (relocated to pointers in place by the callbacks).
#pragma once
#include "data/common.hpp"
#include <array>

namespace baroque::data {

// PRMCHAR.PRM record, 0xb4 bytes (0x2d words). Field meanings per GAME_FORMATS.md ([I] = inferred there).
struct PrmChar {
    std::string id;            // +0x00 "Cxxx" / "Exxx"
    uint32_t nameOfs = 0;      // +0x04 (file offset before relocation)
    uint32_t descOfs = 0;      // +0x08
    SjisString name, desc;
    int32_t hpMax = 0;         // +0x0c -> status[0],[1]          (FUN_00133840)
    int32_t vtMax = 0;         // +0x10 -> status+8,+0xc
    int32_t statA = 0;         // +0x14 -> status+0x10,+0x14      [I attack]
    int32_t statB = 0;         // +0x18 -> status+0x1c,+0x20      [I defense]
    int32_t f1c = 0;           // +0x1c -> status+0x28
    int32_t f20 = 0;           // +0x20 -> status+0x2c
    int32_t f24 = 0;           // +0x24 (not copied)
    int32_t v28[3] = {};       // +0x28 -> status+0x30
    int32_t v34[3] = {};       // +0x34 -> status+0x34
    int32_t resist[14] = {};   // +0x40 -> status+0x38 slots 0..13
    uint16_t w78[3][4] = {};   // +0x78 [I]
    int32_t fleshHp = 0;       // +0x90 restored when the flesh item is eaten (FUN_001ce7e0)
    int32_t fleshVt = 0;       // +0x94
    float radius = 0;          // +0x98
    float f9c = 0;             // +0x9c
    float height = 0;          // +0xa0 (x0.5 for hit/fx height, FUN_001d5410)
    float fa4 = 0;             // +0xa4
    float throwSpread = 0;     // +0xa8 (x0.25, FUN_0014b9f0)
    float iconHeight = 0;      // +0xac status-icon height
    float fb0 = 0;             // +0xb0
    uint32_t fileOffset = 0;
};
inline constexpr uint32_t kPrmCharStride = 0xb4;

struct PrmCharFile {
    std::vector<PrmChar> records;   // Cxxx first, then Exxx
    size_t firstEnemy = 0;          // index of the first 'E' record (piRam0050b1c8 in FUN_00133480)
    std::vector<uint8_t> raw;
};
PrmCharFile parsePrmCharOrThrow(Bytes data);
std::optional<PrmCharFile> parsePrmChar(Bytes data, std::string* error = nullptr);
// orig FUN_00133560: searches the C list when the first char is 'C', else the E list; nullptr if absent.
const PrmChar* findPrmChar(const PrmCharFile& file, uint32_t id);

// Item classes = PRMITEM file index (prefix table 0x463eb0, strides from the jump table 0x499450).
enum class ItemClass : uint8_t { SD, CT, WG, HT, MT, BX, LQ, ST, TL, TP, BN, WM, GN, IS, Count };
inline constexpr int kItemClassCount = 14;
inline constexpr uint32_t kItemStride[kItemClassCount] = {0xa0, 0x8c, 0x88, 0x80, 0x80, 0x8c, 0x80, 0x80, 0x84, 0x84, 0x7c, 0x80, 0x74, 0x74};
inline constexpr const char* kItemPrefix[kItemClassCount] = {"SD", "CT", "WG", "HT", "MT", "BX", "LQ", "ST", "TL", "TP", "BN", "WM", "GN", "IS"};
inline constexpr const char* kItemKind[kItemClassCount] = {"sword", "coat", "wings", "heart", "flesh", "box", "fluid", "brand", "torturer", "disk", "bone", "parasite", "gun", "crystal"};
// orig FUN_001d2c90 (work/decompiled/001d2c90.c + disasm 0x1d2cb8..0x1d2e6c): record stride per class, 0 when cls > 13.
uint32_t itemRecordStride(uint32_t cls);

struct PrmItemEffect {        // 2 slots at +0x78, 8 bytes each
    uint8_t kind = 0;         // +0 ('!' = 0x21 marks a sealed slot, FUN_001d4030)
    uint8_t type = 0;         // +1 (2 = special)
    int16_t value = 0;        // +2
    int16_t x = 0;            // +4
    int16_t count = 0;        // +6
};

// Record layout by stride. Every class has the 0x74-byte prefix (id, strings, dropWeight, f1c, variant16, th, f70);
// the fields after it exist only when they fit inside the class stride:
//   0x74 GN/IS : prefix only
//   0x7c BN    : + chance74, + 1 raw word (0x78)           (no full effect slot)
//   0x80 HT/MT/LQ/ST/WM : + chance74 + effect slot 0
//   0x84 TL/TP : + chance74 + effect slot 0 + 1 raw word (0x80)
//   0x88 WG    : + chance74 + 2 effect slots
//   0x8c CT/BX : ... + 1 raw word (0x88: CT DEF, FUN_001d41e0)
//   0xa0 SD    : ... + 6 raw words (0x88: ATK [I], ...)
// (tools/game_formats.py reads chance74/effects for every class, i.e. into the next record for small strides.)
// Readers in the ELF that touch +0x74/+0x78 (FUN_001d4030 via FUN_001d2e80) only accept classes 0..2.
struct PrmItem {
    std::string id;                 // +0x00 "SD00"
    uint32_t nameOfs = 0, descOfs = 0;
    SjisString name, desc;
    int32_t dropWeight[4] = {};     // +0x0c by floor band (FUN_001d37d0)
    int32_t f1c[4] = {};            // +0x1c per band vs rand(255) (FUN_001d3840) [I]
    int32_t variant[16] = {};       // +0x2c picked by rand(15) (FUN_001d38b0); WM: 4-char ids of the grown form
    int32_t th = 0;                 // +0x6c throw value
    int32_t f70 = 0;                // +0x70
    bool hasChance74 = false;       // stride >= 0x78
    int32_t chance74 = 0;           // +0x74 vs rand(255) -> cursed/unknown name (FUN_001d4030)
    int effectCount = 0;            // full 8-byte slots at +0x78 inside the stride (0..2)
    PrmItemEffect effects[2];       // +0x78, +0x80
    uint32_t tailOffset = 0;        // first raw word after the last full field (0x78 + 8*effectCount)
    std::vector<int32_t> tail;      // raw words tailOffset .. stride
    uint32_t fileOffset = 0;
    std::string variantId(int i) const;  // variant[i] as a 4-char id (WM class)
};

struct PrmItemFile {
    ItemClass cls = ItemClass::SD;
    std::vector<PrmItem> records;
    std::vector<uint8_t> raw;
};
PrmItemFile parsePrmItemOrThrow(Bytes data, ItemClass cls);
std::optional<PrmItemFile> parsePrmItem(Bytes data, ItemClass cls, std::string* error = nullptr);
// orig FUN_001d2c90 semantics on a parsed file: record idx (nullptr when out of range or the class is invalid).
const PrmItem* itemRecord(const PrmItemFile& file, uint32_t idx);

// All 15 PRM files of one language tree (DATA/PARAM or DATA/PARAM_EN).
struct PrmSet {
    PrmCharFile chars;
    std::array<PrmItemFile, kItemClassCount> items;
};
std::string prmCharPath(bool japanese);              // 0x44aff0 / 0x44b010
std::string prmItemPath(bool japanese, uint32_t idx);  // 0x4641b0 / 0x4641d0 "DATA/PARAM[_EN]/PRMITEM%02d.PRM"

}  // namespace baroque::data
