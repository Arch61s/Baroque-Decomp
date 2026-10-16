// Baroque-ProgressCheck: headless check of native/game/progress + flag_manager + save_manager
// (work/reports/PROGRESS_NATIVE.md).
//  1. talk predicates FUN_001a0e60: 1590 results for 8 flag states vs. the MIPS interpreter in
//     tools/gen_progress_tables.py (checks/progress_expected.inc)
//  2. every TLK file under work/assets: talk index, condition table entry, predicate under every flag state,
//     trigger flag ids (0x6c set / 0x6d clear) inside the 4000-flag array, predicate again after applying them
//  3. talk search FUN_001a69e0 for every speaker of table 0x461f80 and every trigger kind
//  4. counters FUN_00179570 (gates, clamp, side effects), run resets FUN_00178b30 / FUN_00179cb0, death / stairs /
//     clear flows, ranking insertion FUN_001f2b00 / FUN_001f2c40
//  5. save file: write slot (FUN_001165f0) -> card file -> read + verify (FUN_0020ba60) -> restore (FUN_00116870)
//     -> write again: the two files must be byte-identical; continue FUN_00116a50 and database keep FUN_00116c90.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "data/tlk.hpp"
#include "game/flag_manager.hpp"
#include "game/progress/progression.hpp"
#include "game/progress/save_record.hpp"
#include "game/progress/talk_predicates.hpp"
#include "game/progress/talk_select.hpp"
#include "game/save_manager.hpp"
#include "platform/platform.hpp"

namespace fs = std::filesystem;
using namespace baroque;
using namespace baroque::game;

#include "checks/progress_expected.inc"

namespace {
int gFail = 0;
void expect(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++gFail;
}

// flag states of tools/gen_progress_tables.py lcg_states(): all clear, all set, 6 LCG states (bit 16 per step)
FlagManager flagState(int s) {
    FlagManager f;
    if (s == 1) {
        for (uint32_t id = 0; id < 4000; ++id) f.setRaw(id);
    } else if (s >= 2) {
        uint32_t st = static_cast<uint32_t>(s - 1) * 0x1234567u;
        for (uint32_t id = 0; id < 4000; ++id) {
            st = st * 0x19660du + 0x3c6ef35fu;
            if ((st >> 16) & 1u) f.setRaw(id);
        }
    }
    return f;
}

std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void checkPredicates() {
    std::printf("-- talk predicates (FUN_001a0e60, %d block functions)\n", kTalkPredicateBlocks);
    bool all = true;
    for (int s = 0; s < 8; ++s) {
        const FlagManager f = flagState(s);
        int count = 0;
        uint32_t h = 0x811c9dc5u;
        int first[8];
        int nf = 0;
        for (int i = 0; i < 8; ++i) first[i] = -1;
        for (int idx = 0; idx < data::kTalkTableCount; ++idx) {
            const int r = talkPredicate(f, idx);
            count += r;
            h = (h ^ static_cast<uint32_t>(r)) * 0x01000193u;
            if (r != 0 && nf < 8) first[nf++] = idx;
        }
        bool ok = count == kPredExpected[s].trueCount && h == kPredExpected[s].fnv;
        for (int i = 0; i < 8; ++i) ok = ok && first[i] == kPredExpected[s].first[i];
        std::printf("   state %d: %4d true, fnv %08x (expected %4d, %08x)\n", s, count, h, kPredExpected[s].trueCount,
                    kPredExpected[s].fnv);
        all = all && ok;
    }
    expect(all, "1590 predicates x 8 flag states match the MIPS interpreter");
    const FlagManager f = flagState(3);
    expect(talkPredicate(f, -1) == 0 && talkPredicate(f, 0x636) == 0 && talkPredicate(f, 0x653) == 0 &&
               talkPredicate(f, 0x654) == 0 && talkPredicateBlockAddr(0) == 0x0017c700u &&
               talkPredicateBlockAddr(0x62c) == 0x001a0c40u && talkPredicateBlockAddr(0x640) == 0x001a0e50u,
           "out-of-range indices return 0; block addresses 0x17c700 / 0x1a0c40 / 0x1a0e50");
    std::printf("   distinct flags tested by the predicates: %d (max 0x%x)\n", kPredFlagsUsed, kPredFlagMax);
}

void checkTlkFiles(const fs::path& root) {
    std::printf("-- TLK files (%s)\n", root.string().c_str());
    int files = 0, resolved = 0, parsed = 0, evaluated = 0, flagTriggers = 0, maxFlag = 0, outOfRange = 0;
    int trueByState[8] = {};
    int changedAfterTriggers = 0;
    for (const char* dir : {"DATA/TALK", "DATA/TALK_EN"}) {
        const fs::path d = root / dir;
        if (!fs::exists(d)) continue;
        for (const auto& e : fs::directory_iterator(d)) {
            if (e.path().extension() != ".TLK") continue;
            ++files;
            const std::string name = e.path().stem().string();
            const int idx = data::findTalkIndex(name);
            if (idx < 0) continue;
            ++resolved;
            const auto bytes = readAll(e.path());
            std::string err;
            const auto tlk = data::parseTlk(data::Bytes(bytes.data(), bytes.size()), &err);
            if (!tlk) continue;
            ++parsed;
            const TalkCondEntry c = talkCondEntry(idx);
            for (int s = 0; s < 8; ++s) {
                FlagManager f = flagState(s);
                const int before = talkPredicate(f, idx);
                trueByState[s] += before;
                ++evaluated;
                // the condition evaluated with the entry's own kind and a/b = c1/c2 must reduce to the predicate for
                // the predicate-only kinds
                if (c.kind == 0 || c.kind == 13 || (c.kind >= 7 && c.kind <= 11)) {
                    TalkSelectState st;
                    if (talkCondition(st, idx, static_cast<uint32_t>(c.kind), c.c1, c.c2, f) != before) ++outOfRange;
                }
                for (const data::TlkTrigger& t : tlk->triggers) {
                    if (t.kind != 0x6c && t.kind != 0x6d) continue;
                    if (s == 0) {
                        ++flagTriggers;
                        if (t.w > maxFlag) maxFlag = t.w;
                        if (t.w < 0 || t.w >= 4000) ++outOfRange;
                    }
                    if (t.kind == 0x6c) f.set(static_cast<uint32_t>(t.w));
                    else f.clear(static_cast<uint32_t>(t.w));
                }
                if (talkPredicate(f, idx) != before) ++changedAfterTriggers;
            }
        }
    }
    std::printf("   files %d, in the talk table %d, parsed %d, predicate evaluations %d\n", files, resolved, parsed,
                evaluated);
    std::printf("   true per state:");
    for (int s = 0; s < 8; ++s) std::printf(" %d", trueByState[s]);
    std::printf("\n   flag triggers %d (max flag 0x%x), predicates changed by the talk's own triggers %d\n",
                flagTriggers, maxFlag, changedAfterTriggers);
    expect(files == 3180 && resolved == 3180 && parsed == 3180, "3180 TLK files, all in the ELF talk table, all parsed");
    expect(outOfRange == 0, "trigger flags inside 0..3999 and talkCondition == predicate for predicate-only kinds");
}

void checkSearch() {
    std::printf("-- talk search (FUN_001a69e0)\n");
    const data::TalkEntry* t = data::talkTable();
    std::vector<uint32_t> speakers;
    for (int i = 0; i < data::kTalkTableCount; ++i) {
        uint32_t id = 0;
        std::memcpy(&id, t[i].speaker, 4);
        bool seen = false;
        for (uint32_t s : speakers) seen = seen || s == id;
        if (!seen) speakers.push_back(id);
    }
    int found = 0, bad = 0;
    for (int s = 0; s < 8; ++s) {
        const FlagManager f = flagState(s);
        TalkSelectState st;
        for (uint32_t id : speakers) {
            TalkActorRef a;
            a.type = 1;
            a.id = id;
            for (int kind = 0; kind < 16; ++kind) {
                const int r = talkSearch(st, &a, kind, -1, -1, false, f);
                if (r < 0) continue;
                ++found;
                const TalkCondEntry c = talkCondEntry(r);
                char want[5];
                std::memcpy(want, &id, 4);
                want[4] = 0;
                const bool c23 = std::strncmp(want, "C23", 3) == 0;
                if ((!c23 && std::strcmp(t[r].speaker, want) != 0) || (c.kind != kind && kind != 5)) ++bad;
            }
        }
        // excluded speaker (+0x1010) never matches
        st.excludedSpeaker = speakers[0];
        TalkActorRef a;
        a.id = speakers[0];
        if (talkSearch(st, &a, 5, 0, 0, false, f) != -1) ++bad;
    }
    std::printf("   %zu speakers x 16 kinds x 8 states: %d talks found\n", speakers.size(), found);
    expect(bad == 0, "search results belong to the speaker and the requested kind; excluded speaker ignored");
    expect(talkSpeakerStart(0x41323043u) == 30 && talkSpeakerStart(0x44333243u) == 1100 &&
               talkSpeakerStart(0x42353243u) == 0 && talkSpeakerStart(0x41373243u) == 1582,
           "speaker start table 0x461f80 (C02A 30, C23D->C23A 1100, C25B 0, C27A 1582)");
}

int gFloor = 150;
int floorHook() { return gFloor; }

void checkCounters() {
    std::printf("-- counters / run resets / run end\n");
    flagHooks().floorId = &floorHook;
    FlagManager& f = flags();
    Counters& c = counters();
    f.clearAll();
    c.clearAll();
    c.add(5, 7);                                             // type 2: tower floor 150 -> counted
    gFloor = 0x5b;
    c.add(5, 7);                                             // not in the tower -> ignored
    c.add(1, 1);                                             // type 1 needs flag 5 -> ignored
    f.setRaw(5);
    c.add(1, 2);                                             // runs: 2 -> flag 976
    c.add(3, 25);                                            // max 20, flagOnMax 1115 -> flag 1115 + 0xaf9
    expect(c.values[5] == 7 && c.values[1] == 2 && f.test(976) && c.values[3] == 20 && f.test(1115 + 0xaf9) &&
               c.atMax(3),
           "FUN_00179570 gates (type 1/2), clamp at max, flagOnMax, counter-1 flag table");
    c.add(0x10, 25);                                         // items collected: exact 25 -> 281, range 20..29 -> 738
    expect(f.test(281) && f.test(738) && f.test(730), "FUN_001799a0 thresholds (281, range 738, first pair 281->730)");
    f.set(0xa60);                                            // voice unlock range bumps counter 0x11
    expect(c.values[0x11] == 1, "FUN_00178890 flags 0xa55..0xb82 increment counter 0x11");
    // run reset (death mask 1): flag 0x1e -> 0x4e7..; table 0x495b90 cleared
    f.setRaw(0x1e);
    f.setRaw(728);
    f.runReset(1);
    expect(f.test(0x4e7) && f.test(0x4f5) && !f.test(728) && f.test(730), "FUN_00178b30 mask 1 (0x1e chain, 0x495b10 cleared, pair set)");
    c.values[2] = 9; c.values[6] = 3; c.values[25] = 4;
    c.runReset(0x81);
    expect(c.values[2] == 9 && c.values[6] == 3 && c.values[25] == 0, "FUN_00179cb0 mask 0x81 only clears 21/22/24/25");
    c.runReset(0x7000);
    expect(c.values[2] == 0 && c.values[6] == 0, "FUN_00179cb0 mask 0x7000");

    // death in the tower without story flags -> hub return applies the masks
    f.clearAll();
    c.clearAll();
    gFloor = 150;
    GameProgress g;
    g.floor = 150;
    g.sec = 5; g.min = 6; g.hour = 7;
    f.setRaw(91);                                             // cleared by FUN_001791f0 on death
    f.setRaw(0xd);                                            // death mask 1: 0xd -> 0xe
    std::vector<uint8_t> ranking(0xa28, 0);
    static std::vector<uint8_t>* gRank = nullptr;
    gRank = &ranking;
    progressHooks().rankingBlock = [] { return gRank->data(); };
    progressHooks().playerExists = [] { return true; };
    progressHooks().playerStat = [](int off) { return off == 0x4c ? 12 : (off == 0x14 ? 3 : 0); };
    progressHooks().playerName = [] { return "TEST"; };
    onPlayerDeath(g);
    expect(!f.test(91) && f.test(0xe) && (g.flags4 & 1) != 0 && g.flagResetMask == 0 && g.counterResetMask == 0 &&
               c.values[1] == 0 && g.sec == 0 && g.hour == 0,
           "FUN_0011d120 -> FUN_0011cd40: run flags cleared, mask 1 applied, Game +4 bit0, clock reset");
    int rank0;
    std::memcpy(&rank0, ranking.data(), 4);
    expect(rank0 == 1 && std::strcmp(reinterpret_cast<const char*>(ranking.data() + 4), "TEST") == 0 &&
               ranking[0x1c] == 50,
           "ranking row 0: rank 1, name, floor number 50 (FUN_001f2a00 / FUN_001f2c40)");
    // a better record (higher +0x18) is inserted above and the old row shifts down with rank 2
    runRecord().stat4c = 20;
    rankingSubmit(1);
    int r0, r1, s0;
    std::memcpy(&r0, ranking.data(), 4);
    std::memcpy(&r1, ranking.data() + 0x34, 4);
    std::memcpy(&s0, ranking.data() + 0x18, 4);
    expect(r0 == 1 && r1 == 2 && s0 == 20, "FUN_001f2b00 ordering and FUN_001f2c40 shift/renumber");
    // stairs: mask by floor class | 0x81, counters 0x80
    GameProgress g2;
    g2.floor = 150;
    f.setRaw(0x21);
    static bool revived = false;
    progressHooks().revivePlayer = [] { revived = true; };
    onStairsExit(g2);
    expect(g2.flagResetMask == (4u | 0x81u) && g2.counterResetMask == 0x80 && revived && g2.requestedFloor == 0x5f,
           "FUN_0011ce60: tower mask 4|0x81, counters 0x80, flag 0x21 -> revive + floor 0x5f");
    // dungeon cleared without flag 5 -> flags 2/0xc0c/0xc1d/0xc1e, floor 0x5f
    f.clearAll();
    GameProgress g3;
    onDungeonCleared(g3);
    expect(f.test(2) && f.test(0xc0c) && f.test(0xc1d) && f.test(0xc1e) && g3.requestedFloor == 0x5f,
           "FUN_0011cfa0 first clear");
    flagHooks().floorId = nullptr;
}

// ---- save round trip
uint8_t gPattern = 0;
void fill(uint8_t* d, size_t n, uint8_t seed) { for (size_t i = 0; i < n; ++i) d[i] = static_cast<uint8_t>(seed + i * 7); }
std::vector<uint8_t> gDb(50 * 0x198), gInvA(3 * 0x198), gBoxes(20 * 0x198), gPlayer(0x448), gOptions(0x3c),
    gRanking(0xa28);
std::vector<uint8_t> gItemSlots(0xa374 - 0x819c);
char gName[16] = "BAROQUE";
void copyStr(char* d, const char* s) { const size_t n = std::strlen(s); std::memcpy(d, s, n + 1); }

void installSaveHooks() {
    fill(gDb.data(), gDb.size(), 1);
    fill(gInvA.data(), gInvA.size(), 2);
    fill(gBoxes.data(), gBoxes.size(), 3);
    fill(gPlayer.data(), gPlayer.size(), 4);
    gPlayer[4] = 9;                                          // slot +0xa378 != 0 (character present)
    fill(gOptions.data(), gOptions.size(), 5);
    fill(gRanking.data(), gRanking.size(), 6);
    fill(gItemSlots.data(), gItemSlots.size(), 7);
    SaveHooks& h = saveHooks();
    h.writeOptions = [](uint8_t* d) { std::memcpy(d, gOptions.data(), 0x3c); };
    h.readOptions = [](const uint8_t* s) { std::memcpy(gOptions.data(), s, 0x3c); };
    h.writeRanking = [](uint8_t* d) { std::memcpy(d, gRanking.data(), 0xa28); };
    h.readRanking = [](const uint8_t* s) { std::memcpy(gRanking.data(), s, 0xa28); };
    h.writeName = [](char* d) { copyStr(d, gName); };
    h.readName = [](const char* s) { copyStr(gName, s); };
    h.writeDatabase = [](uint8_t* d) { std::memcpy(d, gDb.data(), gDb.size()); };
    h.readDatabase = [](const uint8_t* s) { std::memcpy(gDb.data(), s, gDb.size()); };
    h.databaseCounter = [] { return 33; };
    h.writeInventoryA = [](uint8_t* d) { std::memcpy(d, gInvA.data(), gInvA.size()); };
    h.readInventoryA = [](const uint8_t* s) { std::memcpy(gInvA.data(), s, gInvA.size()); };
    h.writeOuterBoxes = [](uint8_t* d) { std::memcpy(d, gBoxes.data(), gBoxes.size()); };
    h.readOuterBoxes = [](const uint8_t* s) { std::memcpy(gBoxes.data(), s, gBoxes.size()); };
    h.writeItemSlots = [](uint8_t* s) { std::memcpy(s + slot::kItemRecords, gItemSlots.data(), gItemSlots.size()); };
    h.readItemSlots = [](const uint8_t* s) { std::memcpy(gItemSlots.data(), s + slot::kItemRecords, gItemSlots.size()); };
    h.gameFloor = [] { return 0x7f; };                       // tower floor 27 -> dungeon id 0x7f
    h.gameClock = [](uint8_t* s, uint8_t* m, uint16_t* hr) { *s = 12; *m = 34; *hr = 56; };
    h.playerExists = [] { return true; };
    h.playerBlockWord4 = [] { return 100; };
    h.writePlayer = [](uint8_t* d) { std::memcpy(d, gPlayer.data(), gPlayer.size()); };
    h.readPlayer = [](const uint8_t* s) { std::memcpy(gPlayer.data(), s, gPlayer.size()); };
    h.rtc = [](uint8_t o[6]) { const uint8_t t[6] = {1, 2, 3, 10, 10, 26}; std::memcpy(o, t, 6); };
}

void checkSave(const fs::path& dir) {
    std::printf("-- save file (%s)\n", dir.string().c_str());
    std::error_code ec;
    fs::remove_all(dir, ec);
    installSaveHooks();
    FlagManager& f = flags();
    Counters& c = counters();
    f = flagState(4);
    f.setRaw(0x4f3);                                         // difficulty index 2
    f.setRaw(0x2a1);                                         // cleared marker
    f.clear(0x4f1);
    f.clear(0x4f2);
    for (int i = 0; i < Counters::kCount; ++i) c.values[i] = i * 3 + 1;
    const FlagManager savedFlags = f;
    Counters savedCounters = c;

    SaveManager& mc = saveManager();
    mc.setDirectory(dir);
    mc.init();
    mc.bootCheck();
    mc.poll();
    expect(mc.result == 2 && mc.busy == 0, "boot check without a file: result 2 (no Baroque data)");
    mc.save(1);
    mc.poll();
    const auto file1 = readAll(mc.filePath());
    expect(mc.result == 0 && file1.size() == static_cast<size_t>(kSaveFileSize), "save slot 2: file of 0x1f76c bytes");
    uint32_t sum = 0;
    for (size_t i = 4; i < file1.size(); ++i) sum += file1[i];
    uint32_t hdr, magic, cleared;
    std::memcpy(&hdr, file1.data(), 4);
    std::memcpy(&magic, file1.data() + 4, 4);
    std::memcpy(&cleared, file1.data() + kSaveClearedOffset, 4);
    expect(hdr == sum && magic == 0x07102200u && file1[8] == 1 && file1[13] == 26 && cleared == 1,
           "header: checksum = byte sum of +4.., magic 0x07102200, RTC at +8, cleared marker at +0x1f768");
    const uint8_t* s = file1.data() + kSaveSlotBase + kSaveSlotSize;
    uint32_t w0; int32_t ctr5, dun, flo, dbStage, dbCtr, hasChar;
    std::memcpy(&w0, s + slot::kFlags + 4 * (0x4f3 >> 5), 4);
    std::memcpy(&ctr5, s + slot::kCounters + 5 * 4, 4);
    std::memcpy(&dun, s + slot::kDungeonId, 4);
    std::memcpy(&flo, s + slot::kFloorId, 4);
    std::memcpy(&dbStage, s + slot::kDbStage, 4);
    std::memcpy(&dbCtr, s + slot::kDbCounter, 4);
    std::memcpy(&hasChar, s + slot::kHasChar, 4);
    // floor 0x7f (tower 27F) saves dungeon id 27 + 0x65 = 128 through FUN_0011a710 (table entry {74, 128, 228})
    const int expectDun = f.test(74) ? 228 : 128;
    std::printf("   dungeon id for floor 0x7f: %d (flag 74 %s)\n", dun, f.test(74) ? "set" : "clear");
    expect((w0 & (1u << (0x4f3 & 31))) != 0 && ctr5 == 16 && dun == expectDun && flo == 0x7f && dbStage == databaseStage(f) &&
               dbCtr == 33 && std::memcmp(s + slot::kName, "BAROQUE", 8) == 0 && s[slot::kClockSec] == 12 &&
               s[slot::kClockMin] == 34 && std::memcmp(s + slot::kPlayer, gPlayer.data(), 0x448) == 0 &&
               std::memcmp(s + slot::kDatabase, gDb.data(), gDb.size()) == 0,
           "slot record offsets (flags +0xa74, counters +0xc68, dungeon/floor +0x8194/+0x8198, name, clock, player)");
    (void)hasChar;
    // corrupt a byte -> corrupt
    {
        auto bad = file1;
        bad[0x1000] ^= 0xff;
        std::ofstream(mc.filePath(), std::ios::binary).write(reinterpret_cast<const char*>(bad.data()),
                                                             static_cast<std::streamsize>(bad.size()));
        mc.titleCheck();
        mc.poll();
        expect(mc.result == 3, "checksum mismatch -> result 3 (corrupt, FUN_0020ba60)");
        std::ofstream(mc.filePath(), std::ios::binary).write(reinterpret_cast<const char*>(file1.data()),
                                                             static_cast<std::streamsize>(file1.size()));
    }
    // wipe the live state, load slot 2 (system part), continue (FUN_00116a50), then save again
    f.clearAll();
    c.clearAll();
    std::fill(gDb.begin(), gDb.end(), uint8_t{0});
    std::fill(gPlayer.begin(), gPlayer.end(), uint8_t{0});
    std::fill(gOptions.begin(), gOptions.end(), uint8_t{0});
    std::fill(gRanking.begin(), gRanking.end(), uint8_t{0});
    std::fill(gInvA.begin(), gInvA.end(), uint8_t{0});
    std::fill(gBoxes.begin(), gBoxes.end(), uint8_t{0});
    std::fill(gItemSlots.begin(), gItemSlots.end(), uint8_t{0});
    copyStr(gName, "X");
    mc.titleCheck();
    mc.poll();
    expect(mc.result == 0 && mc.summary[1].present == 1 && mc.summary[0].present == 0 &&
               std::strcmp(mc.summary[1].name, "BAROQUE") == 0 && mc.summary[1].dungeonType == 1 &&
               mc.summary[1].floor == 28 && mc.summary[1].achievement == 2 && mc.summary[1].level == 4 &&
               mc.summary[1].countA == 33 && mc.clearedFlag == 1,
           "title check summaries (FUN_00116d30)");
    mc.load(1);
    mc.poll();
    bool same = mc.result == 0;
    for (int i = 0; i < FlagManager::kWords; ++i) same = same && f.words[i] == savedFlags.words[i];
    for (int i = 0; i < Counters::kCount; ++i) same = same && c.values[i] == savedCounters.values[i];
    expect(same && std::strcmp(gName, "BAROQUE") == 0, "load: FUN_00116870 restores flags, counters, name, options");
    ContinueResult cr;
    const bool cont = saveContinue(mc.fileData(), 1, &cr);
    expect(cont && cr.floor == (savedFlags.test(74) ? 228 : 128) && cr.prevFloor == 0x7f && cr.sec == 12 && cr.min == 34 && cr.hour == 56 &&
               gPlayer[4] == 9,
           "continue FUN_00116a50: dungeon floor, previous floor, clock, player block");
    mc.save(1);
    mc.poll();
    const auto file2 = readAll(mc.filePath());
    expect(mc.result == 0 && file1 == file2, "write -> read -> restore -> write: byte-identical save file");
    // database keep (new game from a slot): only the keep lists survive
    f.clearAll();
    f.setRaw(5);
    c.clearAll();
    const bool kept = saveDatabaseKeep(mc.fileData(), 1);
    bool keepOk = kept;
    for (uint32_t id = 0; id < 4000; ++id)
        if (f.test(id) && !isDatabaseKeepFlag(id)) keepOk = false;
    for (int i = 0; i < Counters::kCount; ++i)
        if (c.values[i] != 0 && !isDatabaseKeepCounter(i)) keepOk = false;
    keepOk = keepOk && c.values[16] == savedCounters.values[16];
    expect(keepOk, "database keep FUN_00116c90: flags of 0x496230 and counters 16..18 only");
    // hub floor / dead character -> start in the hub as a new character
    {
        std::vector<uint8_t> img(mc.fileData(), mc.fileData() + kSaveFileSize);
        uint8_t* ss = img.data() + kSaveSlotBase + kSaveSlotSize;
        std::memset(ss + slot::kPlayerWord1, 0, 4);
        static bool fresh = false;
        saveHooks().playerStatusNew = [] { fresh = true; };
        f.clearAll();
        const bool ok = saveContinue(img.data(), 1, &cr);
        expect(ok && cr.floor == 0x5b && cr.prevFloor == -1 && fresh && f.test(0x480 + 0xaf9),
               "continue with an empty player block -> floor 0x5b, new status, achievement 0x480");
    }
    mc.deleteSlot(1);
    mc.poll();
    expect(mc.result == 0 && mc.summary[1].present == 0, "delete: slot zeroed and the file rewritten");
    mc.format();
    mc.poll();
    expect(!fs::exists(mc.filePath()) && mc.status() == 0, "format: file removed, buffer zeroed");
    fs::remove_all(dir, ec);
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const fs::path root = BAROQUE_ASSET_ROOT;
    static int statusBits = 0;
    flagHooks().playerStatusBit = [](uint32_t bit) { statusBits |= static_cast<int>(bit); };   // FUN_00138850 stand-in
    checkPredicates();
    checkTlkFiles(root);
    checkSearch();
    checkCounters();
    checkSave(fs::temp_directory_path() / "baroque_progress_check");
    std::printf("%s (%d failures)\n", gFail == 0 ? "ALL PASS" : "FAILURES", gFail);
    return gFail == 0 ? 0 : 1;
}
