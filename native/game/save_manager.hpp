#pragma once
// Memory-card save manager 0x5093d0 (RUNTIME.md 5, SCENES.md 0.7, work/reports/PROGRESS_NATIVE.md 4).
// The card file BASLUS-21714_DATA (0x1f76c bytes, all three slots + header) is stored byte-exactly as
// save/BASLUS-21714_DATA beside the EXE; the in-memory buffer is the original RAM image 0x4e9c50
// (layout in game/progress/save_record.hpp). Header checksum/magic/RTC are sealed on every write (FUN_0020ba10) and
// verified on every read (FUN_0020ba60).
// Fields: +0x464 result (iRam00509834), +0x468 busy/mode (iRam00509838: 1 boot check, 2 title check, 3 save,
// 4 load, 5 delete, 6 format), +0x46c slot argument, +0x470 cleared-game flag, +0x474 slot summaries (3 x 0x38),
// +0x3d0 required free space in KB (uRam005097a0), +0x454 port-check bits (0x100 done, 0x200 changed).
// Result codes (FUN_00116ff0 -> FUN_00117110): 0 OK, 1 insufficient space, 2 card OK but no Baroque file, 3 corrupt,
// 4/6 card error, 5 no card. Files never produce 1/5/6; a file of the wrong size or with a bad checksum yields 3.
// Operations complete at the next poll() (one tick later) so the scene state machines see busy exactly once.
#include <cstdint>
#include <filesystem>
#include <vector>
#include "game/progress/save_record.hpp"
namespace baroque::game {
struct SlotSummary {           // mgr+0x474 + slot*0x38 (FUN_00116d30)
    int present = 0;           // +0
    char name[16] = {};        // +4   (slot +0xa64)
    int level = 0;             // +0x14 (slot +0xc6c = counter 1, the cycle count shown by FILE SELECT)
    int hasChar = 0;           // +0x18 (slot +0xa458)
    int dungeonType = 0;       // +0x1c 1 FUN_0011c290 (tower / 99), 2 FUN_0011c2c0 (scenario), 3+ FUN_0011c390
                               //       (second dungeon; 3 + index of the first set flag 0x397..0x39d), 0 otherwise
    int floor = 0;             // +0x20 dungeonId % 100
    int countA = 0;            // +0x24 (slot +0x5ce8)
    int countB = 0;            // +0x28 (slot +0x5ce4)
    int achievement = 0;       // +0x2c index of the first set flag of 0x4f1..0x4f4 (difficulty), 4 when none
    uint8_t time[8] = {};      // +0x30 sec, min, hour, day, month, year (RTC) + 2 pad
};
class SaveManager {
public:
    void init();                               // FUN_001169b0: zero buffer, set the save directory
    void setDirectory(const std::filesystem::path& dir);
    const std::filesystem::path& directory() const { return dir_; }
    std::filesystem::path filePath() const;    // <dir>/BASLUS-21714_DATA
    // requests (set busy and the pending operation)
    void bootCheck();                          // FUN_00117140
    void titleCheck();                         // FUN_00117150
    void save(int slot);                       // FUN_001171a0
    void load(int slot);                       // FUN_001171c0
    void deleteSlot(int slot);                 // FUN_001171e0
    void format();                             // FUN_00117200
    void poll();                               // FUN_00116ff0 (per engine tick)
    // port check used by Title / Option / FILE SELECT
    void portCheckStart(int port);             // FUN_0020e6e0
    int portCheckResult() const;               // FUN_0020e830: -1 not started, 1 done (unchanged), 0 changed
    void portReset();                          // FUN_0020e7f0
    // state
    int result = 0;                            // +0x464
    int busy = 0;                              // +0x468
    int slotArg = 0;                           // +0x46c
    int clearedFlag = 0;                       // +0x470
    SlotSummary summary[3];                    // +0x474
    uint32_t requiredKB = 0x8d;                // +0x3d0 (UNVERIFIED: FUN_0020c880 KB sum of icon.sys + ICON + data)
    // data access
    bool slotPresent(int slot) const;          // summary[slot].present
    uint8_t* slotData(int slot);               // buffer + 0x10 + slot*0xa7c8
    const uint8_t* slotData(int slot) const;
    uint8_t* fileData() { return buffer_.data(); }             // whole RAM image 0x4e9c50 (0x1f76c bytes)
    const uint8_t* fileData() const { return buffer_.data(); }
    int status() const;                        // *(int*)buffer: 0 empty, -1 corrupt, else valid
    void buildSummaries();                     // FUN_00116d30
private:
    int readFile();                            // FUN_0020e150 + FUN_0020ba60: 0 ok, 2 no file, 3 corrupt
    bool writeFile();                          // FUN_0020e400 + FUN_0020ba10
    std::vector<uint8_t> buffer_;
    std::filesystem::path dir_;
    int pendingOp_ = 0;
    uint32_t portBits_ = 0;                    // +0x454
    bool portPending_ = false;
};
SaveManager& saveManager();
}
