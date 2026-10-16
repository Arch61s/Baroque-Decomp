#include "game/ui/strings.hpp"
#include "game/ui/texture_registry.hpp"
#include "platform/platform.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace baroque::game::ui {
namespace {
int gLanguage = 0;   // uRam004e9768

// PORT (packaging): the localized string table is read from the player's own SLUS_217.14 (copied next to the movies
// by the first-start setup, platform/data_setup.cpp) instead of being compiled in, so no game text ships with the
// port. Table 0x437480: 881 ids x {EN, JP} char pointers (FUN_00114da0: table[id * 2 + lang]); pointers outside the
// ELF image (RAM buffers 0x4e5c8c / 0x4e5c90, filled at run time) read as "" (as tools/gen_strings.py).
constexpr int kStringCount = 881;
constexpr uint32_t kStringTable = 0x437480;
constexpr uint32_t kImageLo = 0x100000, kImageHi = 0x4e5b70;

struct StringTable {
    std::vector<std::string> text[2];   // [lang][id]
    bool loaded = false;
};
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24 : 0;
}
const StringTable& table() {
    static StringTable t;
    if (t.loaded) return t;
    t.loaded = true;
    for (auto& v : t.text) v.assign(kStringCount, std::string());
    const auto path = platform::discRoot() / "SLUS_217.14";
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> elf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // ELF32 program headers: the PT_LOAD segments map the virtual addresses to file offsets
    struct Seg { uint32_t off, vaddr, size; };
    std::vector<Seg> segs;
    if (elf.size() > 0x34 && std::memcmp(elf.data(), "\x7f" "ELF", 4) == 0) {
        const uint32_t phoff = rd32(elf, 0x1c);
        const uint16_t phentsize = uint16_t(elf[0x2a] | elf[0x2b] << 8), phnum = uint16_t(elf[0x2c] | elf[0x2d] << 8);
        for (uint32_t i = 0; i < phnum; ++i) {
            const size_t ph = phoff + size_t(i) * phentsize;
            if (rd32(elf, ph) == 1) segs.push_back({rd32(elf, ph + 4), rd32(elf, ph + 8), rd32(elf, ph + 16)});
        }
    }
    auto at = [&](uint32_t va) -> size_t {   // file offset of a virtual address, SIZE_MAX if unmapped
        for (const Seg& s : segs)
            if (va >= s.vaddr && va < s.vaddr + s.size) return size_t(s.off) + (va - s.vaddr);
        return SIZE_MAX;
    };
    if (segs.empty() || at(kStringTable) == SIZE_MAX) {
        platform::log("strings: cannot read the string table from " + path.string());
        return t;
    }
    for (int id = 0; id < kStringCount; ++id)
        for (int lang = 0; lang < 2; ++lang) {
            const uint32_t p = rd32(elf, at(kStringTable + uint32_t(id) * 8 + uint32_t(lang) * 4));
            if (p < kImageLo || p >= kImageHi) continue;
            const size_t o = at(p);
            if (o == SIZE_MAX) continue;
            size_t n = 0;
            while (n < 1024 && o + n < elf.size() && elf[o + n] != 0) ++n;
            t.text[lang][id].assign(reinterpret_cast<const char*>(&elf[o]), n);
        }
    // (2026-10-16: identical to the former compiled-in table, tools/gen_strings.py output, for all 881 ids x 2)
    return t;
}

// PORT: PC save wording. The English table speaks of the PS2 memory card ("memory card (8MB)(for PlayStation 2) in
// MEMORY CARD slot 1", "do not remove ... controller, or reset/switch off the console"). The port keeps its saves in a
// file beside the EXE, so for English these ids are replaced at lookup time with general save-data wording. Each
// replacement keeps the original's meaning, its %d argument, and its paragraph structure (the trailing "\n\n" blank
// line before the next string of the same message), and never has more lines or wider lines than the original, so
// every message still fits its window and the YES/NO row. The disc data and the Japanese table are untouched.
struct StringOverride { int id; const char* text; };
constexpr StringOverride kPcSaveWordingEN[] = {
    {0x16b, "No save data was found."},
    {0x1c9, "Save data error!\nPlease check the save folder\nand try again.\n\n"},
    {0x1cc, "Insufficient free space\nto create save data.\n\n"},
    {0x1cf, "Checking save data.\n\n"},
    {0x1d0, "Do not close the game or\nturn off the computer.\n"},
    {0x1d1, "The Baroque save data\nmay be corrupted.\n\n"},
    {0x1d3, "No Baroque save file\nwas found.\n\n"},
    {0x1d4, "Would you like to create one?\n"},
    {0x1d5, "Creating save data.\n\n"},
    {0x1d6, "Do not close the game or\nturn off the computer."},
    {0x1d7, "Save data created.\n"},
    {0x1d8, "Could not create save data!\n\nPlease check the save folder.\n\n"},
    {0x1da, "Checking save data.\n\n"},
    {0x1db, "Do not close the game or\nturn off the computer."},
    {0x1dc, "The save data could not\nbe accessed."},
    {0x1dd, "Checking save data.\n\n"},
    {0x1de, "Do not close the game or\nturn off the computer."},
    {0x1df, "Load game file %d?\n\n"},
    {0x1e1, "Loading game file %d.\n\n"},
    {0x1e2, "Do not close the game or\nturn off the computer.\n"},
    {0x1e4, "The save data could not\nbe accessed.\n\n"},
    {0x1e6, "No Baroque save data\nwas found.\n\n"},
    {0x1e8, "Baroque game file %d\nwas not found.\n\n"},
    {0x1ea, "The Baroque save data\nmay be corrupted.\n\n"},
    {0x1ec, "Load failed.\n\nCheck the save data and\nplease try again.\n\n"},
    {0x1ee, "No Baroque save file\nwas found.\n\n"},
    {0x1f0, "Checking save data.\n\n"},
    {0x1f1, "Do not close the game or\nturn off the computer.\n"},
    {0x1f2, "No Baroque save data\nwas found.\n\n"},
    {0x1f3, "Create Baroque save data?\n"},
    {0x1f4, "No Baroque save data\nwas found.\n\n"},
    {0x1f6, "The save data could not\nbe accessed.\n\n"},
    {0x1f9, "Insufficient free space\nto create save data.\n\n"},
    {0x1fc, "Saving Baroque save data.\n\n"},
    {0x1fd, "Do not close the game or\nturn off the computer."},
    {0x1ff, "Save game file %d data?\n\n"},
    {0x201, "Baroque game file %d data\nalready exists.\n\n"},
    {0x203, "The Baroque save data\nis corrupt.\n\n"},
    {0x206, "Save data error!\nPlease check the save folder\nand try again.\n\n"},
    {0x208, "Saving game file %d data.\n\n"},
    {0x209, "Do not close the game or\nturn off the computer.\n"},
    {0x20b, "The save data could not\nbe accessed.\n\n"},
    {0x20e, "Insufficient free space\nto create save data.\n\n"},
    {0x211, "The save data\nmay be corrupted.\n\n"},
    {0x213, "Save failed!\n\nCheck the save folder\nand please try again.\n\n"},
    {0x23a, "Do not close the game.\n"},
};
const char* pcOverrideEN(int id) {
    for (const StringOverride& o : kPcSaveWordingEN)
        if (o.id == id) return o.text;
    return nullptr;
}
}
int language() { return gLanguage; }
void setLanguage(int lang) { gLanguage = lang; }
int stringCount() { return kStringCount; }
// orig FUN_00114da0 (work/decompiled/00114da0.c)
const char* str(int id) {
    if (id < 0 || id >= kStringCount) return "";
    if (gLanguage == 1) return table().text[1][id].c_str();
    if (const char* pc = pcOverrideEN(id)) return pc;   // PORT: PC save wording (see kPcSaveWordingEN)
    return table().text[0][id].c_str();
}
// orig FUN_00114ca0 (work/decompiled/00114ca0.c): TEXASCII + FONT00..: EN loads 0..3 and 0x19, JP 0..0x19
void fontsLoad() {
    table();   // PORT: read the disc's string table up front (no load hitch on the first message)
    for (int id = 0; id < 0x1a; ++id)
        if (textures().isLoaded(id)) textures().release(id);
    if (gLanguage == 1) {
        textures().loadRange(0, 0x19);
    } else if (gLanguage == 0) {
        textures().loadRange(0, 3);
        textures().load(0x19);
    }
}
}
