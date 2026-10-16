// Default SaveHooks / ProgressHooks backed by modules that already exist in Baroque-Native (options 0x51af30,
// ranking 0x53deb0, player name 0x51b5f8, the card RTC). Installed by a static initialiser so the front-end save
// paths (FILE SELECT save/load) carry these blocks without the Game scene owner doing anything. Not linked into the
// headless Baroque-ProgressCheck (which installs its own hooks).
#include "game/progress/save_record.hpp"
#include "game/progress/progression.hpp"
#include "game/options.hpp"
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include <cstring>
#include <ctime>
namespace baroque::game {
namespace {
void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
uint32_t get32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

// memcpy(slot+0, 0x51af30, 0x3c): the Options struct mirrors the block field by field (game/options.hpp offsets).
// UNVERIFIED: padding bytes of the original block (+1..+3, +0xf, +0x35..+0x37) are written as 0.
void writeOptions(uint8_t* d) {
    const Options& o = options();
    std::memset(d, 0, 0x3c);
    d[0x00] = o.initialized;
    put32(d + 0x04, o.cursorPos);
    put32(d + 0x08, o.mono);
    d[0x0c] = static_cast<uint8_t>(o.bgmVol);
    d[0x0d] = static_cast<uint8_t>(o.seVol);
    d[0x0e] = static_cast<uint8_t>(o.voiceVol);
    put32(d + 0x10, o.vibrationOff);
    for (int i = 0; i < 8; ++i) put32(d + 0x14 + i * 4, o.keys[i]);
    d[0x34] = static_cast<uint8_t>(o.brightness);
    put32(d + 0x38, o.camera);
}
void readOptions(const uint8_t* s) {
    Options& o = options();
    o.initialized = s[0x00];
    o.cursorPos = get32(s + 0x04);
    o.mono = get32(s + 0x08);
    o.bgmVol = static_cast<int8_t>(s[0x0c]);
    o.seVol = static_cast<int8_t>(s[0x0d]);
    o.voiceVol = static_cast<int8_t>(s[0x0e]);
    o.vibrationOff = get32(s + 0x10);
    for (int i = 0; i < 8; ++i) o.keys[i] = get32(s + 0x14 + i * 4);
    o.brightness = static_cast<int8_t>(s[0x34]);
    o.camera = get32(s + 0x38);
}
void writeRanking(uint8_t* d) { std::memcpy(d, ui::rankingBlock().data(), ui::kRankingBytes); }
// orig FUN_001f2720 (work/decompiled/001f2720.c) + validity check FUN_001f2e40 (work/decompiled/001f2e40.c)
void readRanking(const uint8_t* s) {
    std::memcpy(ui::rankingBlock().data(), s, ui::kRankingBytes);
    const uint8_t* r = ui::rankingBlock().data();
    bool ok = true;
    for (int i = 0; i < 0x32 && ok; ++i, r += 0x34) {
        const int32_t rank = static_cast<int32_t>(get32(r));
        const int32_t area = static_cast<int32_t>(get32(r + 0x20));
        const int32_t mode = static_cast<int32_t>(get32(r + 0x28));
        if (rank < 0 || 0x32 < rank) ok = false;
        else if (area < 0 || 10 < area) ok = false;
        else if (mode < 0 || 1 < mode) ok = false;
    }
    if (!ok) ui::rankingClear();                                   // FUN_001f2700
}
// strcpy semantics: only the bytes up to and including the NUL are written (the rest keeps its old content)
void writeName(char* d) {                                           // strcpy(slot+0xa64, 0x51b5f8)
    const char* n = ui::nameEntry().name;
    const size_t len = strnlen(n, 15);
    std::memcpy(d, n, len);
    d[len] = 0;
}
void readName(const char* s) {                                      // strcpy(0x51b5f8, slot+0xa64)
    const size_t len = strnlen(s, 15);
    std::memcpy(ui::nameEntry().name, s, len);
    ui::nameEntry().name[len] = 0;
}
// orig FUN_0020e860 (work/decompiled/0020e860.c): sceCdReadClock -> binary sec,min,hour,day,month,year.
// The console clock is replaced by the host clock (platform boundary, not game logic).
void rtc(uint8_t out[6]) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    out[0] = static_cast<uint8_t>(tm.tm_sec);
    out[1] = static_cast<uint8_t>(tm.tm_min);
    out[2] = static_cast<uint8_t>(tm.tm_hour);
    out[3] = static_cast<uint8_t>(tm.tm_mday);
    out[4] = static_cast<uint8_t>(tm.tm_mon + 1);
    out[5] = static_cast<uint8_t>(tm.tm_year % 100);
}
uint8_t* rankingBlockPtr() { return ui::rankingBlock().data(); }
const char* playerName() { return ui::nameEntry().name; }

struct Installer {
    Installer() {
        SaveHooks& h = saveHooks();
        h.writeOptions = &writeOptions;
        h.readOptions = &readOptions;
        h.writeRanking = &writeRanking;
        h.readRanking = &readRanking;
        h.writeName = &writeName;
        h.readName = &readName;
        h.rtc = &rtc;
        ProgressHooks& p = progressHooks();
        p.rankingBlock = &rankingBlockPtr;
        p.playerName = &playerName;
    }
};
const Installer gInstaller;
}
}
