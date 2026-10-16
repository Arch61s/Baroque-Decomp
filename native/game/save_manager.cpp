#include "game/save_manager.hpp"
#include "game/flag_manager.hpp"
#include "game/dungeon.hpp"
#include "platform/platform.hpp"
#include "raylib.h"
#include <cstring>
#include <fstream>
namespace baroque::game {
namespace { SaveManager gSave; }
SaveManager& saveManager() { return gSave; }

// orig FUN_001169b0 (work/decompiled/001169b0.c): memset(buffer 0x4e9c50, 0, 0x1f76c) + file table setup
void SaveManager::init() {
    buffer_.assign(kSaveFileSize, 0);
    if (dir_.empty()) dir_ = std::filesystem::path(GetApplicationDirectory()) / "save";
    result = 0; busy = 0; pendingOp_ = 0; portBits_ = 0; portPending_ = false;
    for (SlotSummary& s : summary) s = SlotSummary{};
}
void SaveManager::setDirectory(const std::filesystem::path& dir) { dir_ = dir; }
std::filesystem::path SaveManager::filePath() const { return dir_ / "BASLUS-21714_DATA"; }   // 0x449dd0
uint8_t* SaveManager::slotData(int slot) { return buffer_.data() + kSaveSlotBase + static_cast<size_t>(slot) * kSaveSlotSize; }
const uint8_t* SaveManager::slotData(int slot) const { return buffer_.data() + kSaveSlotBase + static_cast<size_t>(slot) * kSaveSlotSize; }
int SaveManager::status() const { return saveFileStatus(buffer_.data()); }
bool SaveManager::slotPresent(int slot) const { return slot >= 0 && slot < 3 && summary[slot].present != 0; }

void SaveManager::bootCheck() { busy = 1; result = 0; pendingOp_ = 1; }                      // FUN_00117140 -> FUN_001172c0
void SaveManager::titleCheck() { busy = 2; result = 0; for (SlotSummary& s : summary) s = SlotSummary{}; pendingOp_ = 2; } // FUN_00117150
void SaveManager::save(int slot) { slotArg = slot; busy = 3; result = 0; pendingOp_ = 3; }    // FUN_001171a0 -> FUN_00117360
void SaveManager::load(int slot) { slotArg = slot; busy = 4; result = 0; pendingOp_ = 4; }    // FUN_001171c0 -> FUN_00117530
void SaveManager::deleteSlot(int slot) { slotArg = slot; busy = 5; result = 0; pendingOp_ = 5; } // FUN_001171e0 -> FUN_00117620
void SaveManager::format() { busy = 6; result = 0; pendingOp_ = 6; }                          // FUN_00117200

// Card read of the data file (FUN_0020e150) followed by the checksum verify FUN_0020ba60 (call at 0x20df14:
// negative -> library state 4 -> result 3 "corrupt"). Returns the result code class: 0 read, 2 no file, 3 corrupt.
int SaveManager::readFile() {
    const auto p = filePath();
    if (!std::filesystem::exists(p)) return 2;
    std::ifstream in(p, std::ios::binary);
    std::vector<char> tmp((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (tmp.size() != static_cast<size_t>(kSaveFileSize)) {
        platform::log("save: bad size " + p.string());
        return 3;
    }
    std::memcpy(buffer_.data(), tmp.data(), kSaveFileSize);
    if (saveVerify(buffer_.data(), kSaveFileSize) < 0) return 3;  // FUN_0020ba60 wrote -1 to +0
    return 0;
}
// Card write of the data file (FUN_0020e400): FUN_0020ba10 seals magic/RTC/checksum first (call at 0x20cf48).
bool SaveManager::writeFile() {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    saveSealHeader(buffer_.data(), kSaveFileSize);
    std::ofstream out(filePath(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(buffer_.data()), kSaveFileSize);
    return static_cast<bool>(out);
}

// orig FUN_00116d30 (work/decompiled/00116d30.c): 14-word summary per slot
void SaveManager::buildSummaries() {
    for (int i = 0; i < 3; ++i) {
        const uint8_t* s = slotData(i);
        SlotSummary& o = summary[i];
        int hasChar; std::memcpy(&hasChar, s + slot::kHasChar, 4);
        if (hasChar == 0) { o.present = 0; continue; }
        o.present = 1;
        std::memcpy(o.name, s + slot::kName, 16);
        std::memcpy(&o.level, s + slot::kCounters + 4, 4);
        o.hasChar = hasChar;
        int dungeonId; std::memcpy(&dungeonId, s + slot::kDungeonId, 4);
        if (isRandomDungeon(dungeonId)) o.dungeonType = 1;                          // FUN_0011c290
        else if (isScenarioFloor(dungeonId)) o.dungeonType = 2;                     // FUN_0011c2c0
        else if (isSecondDungeon(dungeonId)) {                                      // FUN_0011c390
            o.dungeonType = 3;
            for (uint32_t f = 0x397; f < 0x39e; ++f, ++o.dungeonType) {
                uint32_t w; std::memcpy(&w, s + slot::kFlags + (f >> 5) * 4, 4);
                if (w & (1u << (f & 0x1f))) break;
            }
        } else o.dungeonType = 0;
        o.floor = dungeonId % 100;
        std::memcpy(&o.countA, s + slot::kDbCounter, 4);
        std::memcpy(&o.countB, s + slot::kDbStage, 4);
        o.achievement = 0;
        for (uint32_t f = 0x4f1; f < 0x4f5; ++f) {
            uint32_t w; std::memcpy(&w, s + slot::kFlags + (f >> 5) * 4, 4);
            if (w & (1u << (f & 0x1f))) break;
            ++o.achievement;
        }
        std::memcpy(o.time, s + slot::kTimestamp, 8);
    }
    std::memcpy(&clearedFlag, buffer_.data() + kSaveClearedOffset, 4);
}

// orig FUN_00116ff0 (work/decompiled/00116ff0.c) + the operation callbacks of table 0x449e60..0x449ee8
// (FUN_00117260/00117310/00117410/00117480/001174e0/00117590/001176e0/00117750)
void SaveManager::poll() {
    if (portPending_) { portBits_ |= 0x100; portPending_ = false; }
    if (busy == 0 || pendingOp_ == 0) return;
    const int op = pendingOp_;
    pendingOp_ = 0;
    switch (op) {
    case 1: {   // FUN_001172c0 load -> FUN_00117310 (state 1): result 0
        result = readFile();
        break;
    }
    case 2: {   // FUN_001172c0 -> FUN_00117310 (state 2) -> FUN_00117530 -> FUN_00117590 (state 2): FUN_00116d30
        result = readFile();
        if (result == 0) buildSummaries();
        break;
    }
    case 3: {   // FUN_00117360: write the slot, cleared marker, write the file -> FUN_00117410/00117480/001174e0
        saveWriteSlot(slotData(slotArg));                                           // FUN_001165f0
        if (flags().test(0x2a1)) { const int one = 1; std::memcpy(buffer_.data() + kSaveClearedOffset, &one, 4); }
        if (!writeFile()) { result = 4; break; }
        result = readFile();                                                        // FUN_0020e1f0 read-back
        if (result == 0) buildSummaries();                                          // FUN_001174e0
        break;
    }
    case 4: {   // FUN_00117530 read -> FUN_00117590 (state 4): FUN_00116870 restores options/ranking/name/flags/counters
        result = readFile();
        if (result == 0) saveRestoreSystem(slotData(slotArg));
        break;
    }
    case 5: {   // FUN_00117620 (state 5): memset(slot, 0, 0xa7c8), write -> FUN_00117410 .. FUN_001174e0
        std::memset(slotData(slotArg), 0, kSaveSlotSize);
        if (!writeFile()) { result = 4; break; }
        result = readFile();
        if (result == 0) buildSummaries();
        break;
    }
    case 6: {   // FUN_00117200 format -> FUN_00117260: memset(buffer, 0, 0x1f76c)
        std::error_code ec;
        std::filesystem::remove(filePath(), ec);
        std::memset(buffer_.data(), 0, kSaveFileSize);
        for (SlotSummary& s : summary) s = SlotSummary{};
        result = 0;
        break;
    }
    default: break;
    }
    busy = 0;
}
// orig FUN_0020e6e0 (work/decompiled/0020e6e0.c): clears bits 0x100/0x200 and kicks the check (FUN_0020e700)
void SaveManager::portCheckStart(int) { portBits_ &= 0xfffffcffu; portPending_ = true; }
// orig FUN_0020e830 (work/decompiled/0020e830.c)
int SaveManager::portCheckResult() const {
    if ((portBits_ & 0x100) == 0) return -1;
    return ((portBits_ & 0x200) != 0) ? 0 : 1;
}
// orig FUN_0020e7f0 (work/decompiled/0020e7f0.c)
void SaveManager::portReset() { portBits_ &= 0xfffffeffu; portPending_ = false; }
}
