// Baroque-DataCheck: parses every ACT/PRM/TLK/EVT file plus BQ.EXI and AISEQ.BIN under the asset root with the
// native loaders and compares counts/histograms with work/game_formats_summary.json (tools/game_formats.py),
// whose values are baked into data_expected.inc by tools/gen_tables.py. Prints PASS/FAIL per section.
#include "data/act.hpp"
#include "data/aiseq.hpp"
#include "data/evt.hpp"
#include "data/evt_jptime.hpp"
#include "data/exi.hpp"
#include "data/prm.hpp"
#include "data/text_tables.hpp"
#include "data/tlk.hpp"
#include "platform/platform.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {
#include "data_expected.inc"
}

using namespace baroque::data;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void report(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

template <class T>
void expectEq(const char* what, T got, T want) {
    report(got == want, std::string(what) + ": " + std::to_string(got) + (got == want ? "" : " (expected " + std::to_string(want) + ")"));
}

std::vector<fs::path> listFiles(const fs::path& dir, const std::string& ext) {
    std::vector<fs::path> out;
    if (!fs::exists(dir)) return out;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ext) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<uint8_t> load(const fs::path& p) {
    auto d = readFile(p);
    if (!d) throw FormatError("cannot read " + p.string());
    return *d;
}

void countCommands(const std::vector<ActCmnd>& cmds, std::array<int, kActCommandCount>& hist) {
    for (const auto& c : cmds) {
        ++hist[static_cast<size_t>(c.cmd)];
        countCommands(c.body, hist);
    }
}

void checkAct(const fs::path& root) {
    std::array<int, kActCommandCount> hist{};
    int files = 0, actions = 0;
    std::string firstError;
    for (const auto& p : listFiles(root / "DATA" / "ACTION", ".ACT")) {
        std::string err;
        const auto act = parseAct(load(p), &err);
        if (!act) { if (firstError.empty()) firstError = p.filename().string() + ": " + err; continue; }
        ++files;
        actions += static_cast<int>(act->actions.size());
        for (const auto& a : act->actions) countCommands(a.cmds, hist);
        // lookup round trip (FUN_00143aa0) for every action name
        for (size_t i = 0; i < act->actions.size(); ++i) {
            const ActAction* found = findAction(*act, act->actions[i].name);
            if (!found || found->name != act->actions[i].name) { if (firstError.empty()) firstError = p.filename().string() + ": findAction failed for " + act->actions[i].name; }
        }
    }
    report(firstError.empty(), "ACT: all files parse" + (firstError.empty() ? "" : " - " + firstError));
    expectEq("ACT files", files, kExpActFiles);
    expectEq("ACT actions", actions, kExpActActions);
    int total = 0;
    bool histOk = true;
    for (int i = 0; i < kActCommandCount; ++i) {
        total += hist[static_cast<size_t>(i)];
        if (hist[static_cast<size_t>(i)] != kExpActCommandHistogram[i]) {
            histOk = false;
            std::printf("      %s: %d (expected %d)\n", actCommandTable()[static_cast<size_t>(i)].name, hist[static_cast<size_t>(i)], kExpActCommandHistogram[i]);
        }
    }
    report(histOk, "ACT command histogram matches (66 commands)");
    expectEq("ACT commands total", total, kExpActCommandsTotal);
    // table sanity: names resolve to their own index (FUN_00146950 compare)
    bool tableOk = true;
    for (int i = 0; i < kActCommandCount; ++i) {
        const auto n = padName8(actCommandTable()[static_cast<size_t>(i)].name);
        const auto f = findActCommand(Bytes(n.data(), 8));
        if (!f || static_cast<int>(*f) != i) tableOk = false;
    }
    report(tableOk, "ACT command table resolves all 66 names");
}

void checkPrm(const fs::path& root) {
    for (const bool japanese : {true, false}) {
        const char* tag = japanese ? "PARAM" : "PARAM_EN";
        std::string err;
        const auto chars = parsePrmChar(load(root / prmCharPath(japanese)), &err);
        report(chars.has_value(), std::string(tag) + "/PRMCHAR.PRM parses" + (chars ? "" : " - " + err));
        if (chars) {
            expectEq((std::string(tag) + " PRMCHAR records").c_str(), static_cast<int>(chars->records.size()), kExpPrmCharRecords);
            const PrmChar* c00a = findPrmChar(*chars, id4("C00A"));
            const PrmChar* e01a = findPrmChar(*chars, id4("E01A"));
            report(c00a && e01a && c00a->id == "C00A" && e01a->id == "E01A", std::string(tag) + " findPrmChar C00A/E01A");
            if (!japanese && c00a) std::printf("      C00A name '%s' hp %d vt %d\n", c00a->name.utf8.c_str(), c00a->hpMax, c00a->vtMax);
        }
        bool itemsOk = true;
        std::string itemErr;
        for (int i = 0; i < kItemClassCount; ++i) {
            const auto items = parsePrmItem(load(root / prmItemPath(japanese, static_cast<uint32_t>(i))), static_cast<ItemClass>(i), &err);
            if (!items) { itemsOk = false; if (itemErr.empty()) itemErr = prmItemPath(japanese, static_cast<uint32_t>(i)) + ": " + err; continue; }
            if (static_cast<int>(items->records.size()) != kExpPrmItemRecords[i]) {
                itemsOk = false;
                std::printf("      PRMITEM%02d: %zu records (expected %d)\n", i, items->records.size(), kExpPrmItemRecords[i]);
            }
            if (!japanese && i == 0 && !items->records.empty())
                std::printf("      SD00 name '%s' tail[0] (ATK) %d\n", items->records[0].name.utf8.c_str(), items->records[0].tail.empty() ? 0 : items->records[0].tail[0]);
        }
        report(itemsOk, std::string(tag) + "/PRMITEM00..13 parse with expected record counts" + (itemErr.empty() ? "" : " - " + itemErr));
    }
    report(itemRecordStride(14) == 0 && itemRecordStride(0) == 0xa0 && itemRecordStride(13) == 0x74, "PRM item stride jump table (0x499450)");
}

void checkTlk(const fs::path& root) {
    int files = 0, enFiles = 0, enTriggers = 0, enLines = 0;
    std::array<int, 15> kinds{};
    std::string firstError;
    int tableHits = 0;
    for (const bool japanese : {true, false}) {
        for (const auto& p : listFiles(root / (japanese ? "DATA/TALK" : "DATA/TALK_EN"), ".TLK")) {
            std::string err;
            const auto s = parseTlk(load(p), &err);
            if (!s) { if (firstError.empty()) firstError = p.filename().string() + ": " + err; continue; }
            ++files;
            if (!japanese) {
                ++enFiles;
                enTriggers += static_cast<int>(s->triggers.size());
                enLines += static_cast<int>(s->sentences.size());
                for (const auto& t : s->triggers) ++kinds[static_cast<size_t>(t.kind - 0x65)];
                if (findTalkIndex(p.stem().string()) >= 0) ++tableHits;
            }
        }
    }
    report(firstError.empty(), "TLK: all files parse" + (firstError.empty() ? "" : " - " + firstError));
    expectEq("TLK files", files, kExpTlkFiles);
    expectEq("TLK EN files", enFiles, kExpTlkEnFiles);
    expectEq("TLK EN triggers", enTriggers, kExpTlkEnTriggers);
    expectEq("TLK EN sentences", enLines, kExpTlkEnLines);
    bool kindsOk = true;
    for (size_t i = 0; i < 15; ++i)
        if (kinds[i] != kExpTlkTriggerHistogram[i]) { kindsOk = false; std::printf("      kind 0x%zx: %d (expected %d)\n", 0x65 + i, kinds[i], kExpTlkTriggerHistogram[i]); }
    report(kindsOk, "TLK trigger kind histogram matches");
    expectEq("TLK EN files present in the ELF talk table", tableHits, kExpTlkEnFiles);
    report(findTalkIndex("C00A_V15B") == 0 && findTalkIndex("C27_V001") == kTalkTableCount - 1 && findTalkIndex("nope") == -1, "talk table first/last entries");
}

void checkEvt(const fs::path& root) {
    int files = 0, enCues = 0, enActors = 0, enLines = 0, badStreams = 0, totalCues = 0, waitOps = 0;
    std::map<int, int> firstOps;
    std::string firstError;
    for (const bool japanese : {true, false}) {
        for (const auto& p : listFiles(root / (japanese ? "DATA/EVENT" : "DATA/EVENT_EN"), ".EVT")) {
            std::string err;
            const auto e = parseEvt(load(p), &err);
            if (!e) { if (firstError.empty()) firstError = p.filename().string() + ": " + err; continue; }
            ++files;
            for (const auto& c : e->cues) {
                ++totalCues;
                bool ok = true;
                const auto instrs = decodeCueStream(c, &ok);
                if (!ok) ++badStreams;
                for (const auto& in : instrs) if (in.op == 999) ++waitOps;
            }
            if (!japanese) {
                enCues += static_cast<int>(e->cues.size());
                enActors += e->actorOps;
                enLines += static_cast<int>(e->lines.size());
                for (const auto& c : e->cues) ++firstOps[c.words[0]];
            }
        }
    }
    report(firstError.empty(), "EVT: all files parse" + (firstError.empty() ? "" : " - " + firstError));
    expectEq("EVT files", files, kExpEvtFiles);
    expectEq("EVT EN cues", enCues, kExpEvtEnCues);
    expectEq("EVT EN actors (opcode 5)", enActors, kExpEvtEnActors);
    expectEq("EVT EN text lines", enLines, kExpEvtEnLines);
    bool firstOk = firstOps.size() == sizeof(kExpEvtEnCueFirstOps) / sizeof(kExpEvtEnCueFirstOps[0]);
    for (const auto& kv : kExpEvtEnCueFirstOps) {
        const auto it = firstOps.find(kv[0]);
        if (it == firstOps.end() || it->second != kv[1]) { firstOk = false; std::printf("      first op %d: %d (expected %d)\n", kv[0], it == firstOps.end() ? 0 : it->second, kv[1]); }
    }
    report(firstOk, "EVT EN cue first-opcode histogram matches");
    report(badStreams == 0, "EVT cue streams decode with the derived operand counts (" + std::to_string(totalCues) + " cues, " + std::to_string(badStreams) + " bad, " + std::to_string(waitOps) + " Wait999)");
    report(kEvtCueOpCount == 87 && evtCueOpDesc(50)->operands == 8 && evtCueOpDesc(118) == nullptr, "EVT cue opcode table (86 dispatched + Wait999)");
}

// PORT (voice language): Japanese timing + English text for every event (data/evt_jptime.*). With the environment
// variable BAROQUE_EVT_JPTIME_DUMP set to a folder, the merged files are written there for comparison.
void checkEvtJpTiming(const fs::path& root) {
    int merged = 0, total = 0;
    EvtJpTimingStats sum;
    std::string fallbacks;
    char* dumpEnv = nullptr;
    size_t dumpLen = 0;
    const bool dump = _dupenv_s(&dumpEnv, &dumpLen, "BAROQUE_EVT_JPTIME_DUMP") == 0 && dumpEnv != nullptr;
    const fs::path dumpDir = dump ? fs::path(dumpEnv) : fs::path();
    std::free(dumpEnv);
    for (const auto& p : listFiles(root / "DATA/EVENT_EN", ".EVT")) {
        ++total;
        const fs::path jpPath = root / "DATA/EVENT" / p.filename();
        if (!fs::exists(jpPath)) { fallbacks += " " + p.filename().string() + "(no Japanese script)"; continue; }
        const auto en = load(p), jp = load(jpPath);
        std::string why;
        EvtJpTimingStats st;
        const auto m = buildJapaneseTimedEvt(en, jp, &why, &st);
        if (!m) { fallbacks += " " + p.filename().string() + "(" + why + ")"; continue; }
        if (!parseEvt(*m)) { fallbacks += " " + p.filename().string() + "(merged file does not parse)"; continue; }
        ++merged;
        sum.pages += st.pages;
        sum.pagesDropped += st.pagesDropped;
        sum.unevenGroups += st.unevenGroups;
        sum.stateMoved += st.stateMoved;
        if (dump) {
            fs::create_directories(dumpDir);
            std::FILE* f = nullptr;
            if (fopen_s(&f, (dumpDir / p.filename()).string().c_str(), "wb") == 0 && f) {
                std::fwrite(m->data(), 1, m->size(), f);
                std::fclose(f);
            }
        }
    }
    std::printf("      EVT Japanese timing: %d/%d merged, %d English pages placed, %d Japanese pages dropped, %d uneven "
                "segments, %d state cues moved;%s\n", merged, total, sum.pages, sum.pagesDropped, sum.unevenGroups,
                sum.stateMoved, fallbacks.empty() ? " no fallbacks" : (" fallbacks:" + fallbacks).c_str());
    report(merged == total, "EVT Japanese timing: every event merges");
}

void checkExi(const fs::path& root) {
    std::string err;
    const auto e = parseExi(load(root / kExiPath), &err);
    report(e.has_value(), std::string("EXI: BQ.EXI parses") + (e ? "" : " - " + err));
    if (!e) return;
    bool ok = e->chunks.size() == 7;
    for (size_t i = 0; ok && i < 7; ++i) ok = e->chunks[i].tag == kExpExiChunks[i] && e->chunks[i].size == kExpExiChunkSizes[i];
    report(ok, "EXI chunk list matches (VTXS KFMS OBJS EMTS SEQS RSQS EFCS)");
    std::printf("      vertices %zu keyframes %zu objects %zu emitters %zu sequences %zu rsqs %zu effects %zu\n", e->vertices.size(), e->keyframes.size(),
                e->objects.size(), e->emitters.size(), e->sequences.size(), e->rsqs.size(), e->effects.size());
    report(e->effects.size() == (kExpExiChunkSizes[6] - 8) / 0x28 && e->sequences.size() == (kExpExiChunkSizes[4] - 8) / 0x50, "EXI record counts follow the chunk sizes");
}

void checkAiSeq(const fs::path& root) {
    std::string err;
    const auto a = parseAiSeq(load(root / kAiSeqPath), &err);
    report(a.has_value(), std::string("AISEQ: AISEQ.BIN parses") + (a ? "" : " - " + err));
    if (!a) return;
    expectEq("AISEQ header word @0xc", a->maxSteps, kExpAiSeqA);
    expectEq("AISEQ header word @0x10", a->word10, kExpAiSeqB);
    expectEq("AISEQ records", static_cast<int>(a->records.size()), kExpAiSeqRecords);
    bool ids = a->records.size() == sizeof(kExpAiSeqIds) / sizeof(kExpAiSeqIds[0]);
    for (size_t i = 0; ids && i < a->records.size(); ++i) ids = a->records[i].id == kExpAiSeqIds[i];
    report(ids, "AISEQ record ids match");
    std::map<size_t, int> stateCounts;
    size_t instrs = 0;
    std::map<uint8_t, int> opHist;
    for (const auto& r : a->records) ++stateCounts[r.states.size()];
    for (const auto& kv : a->sequences) { instrs += kv.second.instrs.size(); for (const auto& in : kv.second.instrs) ++opHist[static_cast<uint8_t>(in.op)]; }
    std::string st;
    for (const auto& kv : stateCounts) st += std::to_string(kv.first) + " states x" + std::to_string(kv.second) + " ";
    std::printf("      state tables: %s; %zu reachable sequences, %zu instructions\n", st.c_str(), a->sequences.size(), instrs);
    std::string oh;
    for (const auto& kv : opHist) oh += hex(kv.first) + ":" + std::to_string(kv.second) + " ";
    std::printf("      opcode histogram: %s\n", oh.c_str());
    report(findAiRecord(*a, id4("E01A")) == 0 && findAiRecord(*a, id4("C18A")) == kExpAiSeqRecords - 1 && findAiRecord(*a, id4("ZZZZ")) == -1, "AISEQ record lookup");
}

void checkTextTables() {
    FontIndex jp, en;
    jp.build(true);
    en.build(false);
    bool ok = true;
    for (int g = 0; g < kGlyphCountJp; ++g) if (jp.lookup(kGlyphCodesJp[g], true) != g) { ok = false; break; }
    for (int g = 0; g < kGlyphCountEn; ++g) if (en.lookup(kGlyphCodesEn[g], false) != g) { ok = false; break; }
    report(ok, "font lookup round-trips every glyph code (JP 1344, EN 115)");
    const int zeroEn = en.lookup(static_cast<uint16_t>(0x824f), false);  // "0"
    report(zeroEn > 0 && asciiGlyph('5', en, false) == zeroEn + 5 && asciiGlyph('A', en, false) == en.lookup(static_cast<uint16_t>(0x8260), false) && asciiGlyph(' ', en, false) == 0 && asciiGlyph('\x01', en, false) == -1,
           "ASCII -> full-width glyph mapping");
    const GlyphCell c = glyphCell(57);  // 57 = row 7 -> second atlas, cell (1, 0)
    report(c.fontIndex == 1 && c.px == 16.0f && c.py == 0.0f, "glyph atlas cell math (16x18 cells, 8x7 per 128x128 FONTxx)");
    report(std::string(textureName(1, false)) == "DATA/TEX_EN/FONT00.PNG" && textureName(284, true) == nullptr && std::string(textureName(285, true)) == "DATA/TEX/C_SMOKE.PNG", "texture registry entries 1/284/285");
    report(sjisToUtf8(std::vector<uint8_t>{0x82, 0x4f, 0x41}) == "\xef\xbc\x90" "A", "sjisToUtf8 through the glyph table");
}

}  // namespace

int main() {
    const fs::path root = baroque::platform::assetRoot();
    std::printf("asset root: %s\n", root.string().c_str());
    try {
        checkAct(root);
        checkPrm(root);
        checkTlk(root);
        checkEvt(root);
        checkEvtJpTiming(root);
        checkExi(root);
        checkAiSeq(root);
        checkTextTables();
    } catch (const std::exception& e) {
        std::printf("FAIL  exception: %s\n", e.what());
        ++g_failures;
    }
    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
