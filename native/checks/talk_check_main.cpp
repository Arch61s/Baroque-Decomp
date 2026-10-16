// Baroque-TalkCheck: headless check of the TLK conversation runtime (native/game/talk, work/reports/TALK_NATIVE.md).
//  1. every TLK of the talk table (EN and JP) through the manager's load path FUN_001a7660 -> FUN_001a7270 ->
//     FUN_001a7740 -> FUN_001a77c0 -> FUN_001a80c0, compared field by field with data::parseTlk (python summary
//     equivalent): voice, sentence, speed, layout, number op, trigger list; corpus facts the port relies on
//     (sentence terminators, '%' only in formatted pages, at most 32 triggers)
//  2. trigger kind histogram of the expanded tables
//  3. conversations with a scripted fake world: the approach selector FUN_001a2d40 for a few speakers, the per-tick
//     FUN_001a2110 with a fake voice that plays for 45 ticks, printing the talk window pages (slot 3), voice ids and the
//     fired triggers; the VOICE LIST path FUN_001a73a0 and FUN_001aa1b0 (talk by voice number)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "data/archive.hpp"
#include "data/tlk.hpp"
#include "game/flag_manager.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/strings.hpp"
#include "platform/platform.hpp"

#include "raylib.h"

using namespace baroque;
using namespace baroque::game;

namespace {

int gFail = 0;
void expect(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++gFail;
}

struct FakeActor {
    int type = 1;
    uint32_t id = 0;
    float pos[3] = {};
    float fwd[3] = {0, 0, 1};
    int hp = 100, hpMax = 100, vt = 100, vtMax = 100;
};

std::string idText(uint32_t id) {
    std::string s(4, ' ');
    for (int k = 0; k < 4; ++k) s[size_t(k)] = static_cast<char>((id >> (8 * k)) & 0xffu);
    return s;
}

// Fake world: Game scene, floor generated, no events / menus, a list of actors, a voice that plays for N ticks.
struct FakeWorld final : TalkWorld {
    std::vector<FakeActor> actors;     // [0] = player
    bool game = true;
    uint32_t flagsWord = 1;
    int voicePlaying = 0;              // ticks left
    int voiceLen = 45;
    bool log = false;
    std::vector<std::string> events;
    bool sceneIsGame() override { return game; }
    bool itemScreenOpen() override { return false; }
    bool systemMenuOpen() override { return false; }
    bool enginePaused() override { return false; }
    bool messageBoxOpen() override { return false; }
    uint32_t floorFlags() override { return flagsWord; }
    bool eventRunning() override { return false; }
    int eventMode() override { return 0; }
    int floorId() override { return 0x5b; }
    int floorNumber() override { return 0; }
    uint32_t playerControlFlags() override { return 0; }
    bool fadeRunning() override { return false; }
    bool optionBit4() override { return false; }
    uint32_t padPressed() override { return 0; }
    uint32_t talkButton() override { return 0x80; }
    float lingerGate() override { return 0.0f; }
    int actorCount() override { return static_cast<int>(actors.size()); }
    TalkActor actorAt(int i) override { return &actors[size_t(i)]; }
    TalkActor player() override { return actors.empty() ? nullptr : &actors[0]; }
    static FakeActor& A(TalkActor a) { return *static_cast<FakeActor*>(a); }
    int actorType(TalkActor a) override { return A(a).type; }
    uint32_t actorId(TalkActor a) override { return a == nullptr ? 0u : A(a).id; }
    void actorPos(TalkActor a, float o[3]) override { for (int k = 0; k < 3; ++k) o[k] = A(a).pos[k]; }
    void setActorPos(TalkActor a, const float p[3]) override { for (int k = 0; k < 3; ++k) A(a).pos[k] = p[k]; }
    void actorForward(TalkActor a, float o[3]) override { for (int k = 0; k < 3; ++k) o[k] = A(a).fwd[k]; }
    int actorRoom(TalkActor) override { return -2; }
    int actorHp(TalkActor a) override { return A(a).hp; }
    int actorHpMax(TalkActor a) override { return A(a).hpMax; }
    int actorVt(TalkActor a) override { return A(a).vt; }
    int actorVtMax(TalkActor a) override { return A(a).vtMax; }
    TalkObj carriedItem(TalkActor) override { return nullptr; }
    TalkObj objOfActor(TalkActor a) override { return a; }
    TalkActorRef objRef(TalkObj o) override {
        TalkActorRef r;
        if (o != nullptr) { r.type = A(o).type; r.id = A(o).id; }
        return r;
    }
    void objOrFlags70(TalkObj, uint32_t) override {}
    bool partAnim(TalkActor a, int part, const char* name, float blend, float speed) override {
        if (log) {
            char b[128];
            std::snprintf(b, sizeof b, "anim %s part %d \"%s\" blend %.1f speed %.1f", idText(A(a).id).c_str(), part, name, blend, speed);
            events.push_back(b);
        }
        return true;
    }
    void actAction(TalkActor a, const char* name8) override {
        if (log) events.push_back("action " + idText(A(a).id) + " \"" + std::string(name8) + "\"");
    }
    int hitRecords(TalkActor, TalkHitRecord[32]) override { return 0; }
    int roomMapId(int) override { return -1; }
    int floorRoomByte() override { return 0; }
    float roomOrigin(int, int) override { return 0.0f; }
    uint32_t giftTarget() override { return 0; }
    TalkObj giftItem() override { return nullptr; }
    void giftClear() override {}
    int databaseCount() override { return 0; }
    int listFlagCount() override { return 0; }
    void voice3d(int id) override { startVoice(id, "3d"); }
    void voice(int id) override { startVoice(id, "2d"); }
    void startVoice(int id, const char* kind) {
        voicePlaying = voiceLen;
        if (log) events.push_back("voice " + std::to_string(id) + " (" + kind + ")");
    }
    void bgmVolume(float v, int f) override {
        if (log) {
            char b[64];
            std::snprintf(b, sizeof b, "bgm volume %.1f over %d", v, f);
            events.push_back(b);
        }
    }
    int voiceStatus() override { return voicePlaying > 0 ? 4 : 0; }
    void voiceStop() override { voicePlaying = 0; }
    void voiceVolume(float) override {}
    void se(int) override {}
    void effect(int id, const float p[4]) override {
        if (log) {
            char b[96];
            std::snprintf(b, sizeof b, "effect %d at (%.0f, %.0f, %.0f)", id, p[0], p[1], p[2]);
            events.push_back(b);
        }
    }
    void mainLightDir(float o[3]) override { o[0] = -1; o[1] = -1; o[2] = -1; }
    void setMainLightDir(const float*) override {}
    bool activeCameraExists() override { return true; }
    void transition() override {}
    bool loadFile(const char* path, std::vector<uint8_t>& out) override {
        data::Archive ar(platform::assetRoot());
        auto b = ar.load(path);
        if (!b) return false;
        out = std::move(*b);
        return true;
    }
};

FakeWorld gW;

// ---- 1. parser over every file ----------------------------------------------------------------------------------
void parseAll(int lang, std::map<int, int>& kindHist) {
    ui::setLanguage(lang);
    TalkManager& m = talkManager();
    int files = 0, missing = 0, voiced = 0, noVoice = 0, mismatch = 0, maxTrig = 0, trigTotal = 0, noTerm = 0;
    int percentBad = 0, numberPages = 0, c24 = 0, layout2 = 0, emptyText = 0, nulEnded = 0;
    int layoutMax = 0;   // page op 8 and trigger 0xb (0x6f) values index the 0x4620e0 rect table (12 rows)
    for (int i = 0; i < data::kTalkTableCount; ++i) {
        flags().clearAll();
        m.construct();
        gW.actors.clear();
        gW.actors.push_back(FakeActor{0, data::tag4("C00A")});
        m.loadAsync(i, 0);                                    // FUN_001a7660
        if (!m.loadPending) {
            ++missing;
            continue;
        }
        m.loadPending = false;
        // keep the file for the comparison (the callback releases it)
        const std::vector<uint8_t> bytes = m.file;
        m.loadCallback();                                     // FUN_001a7270 -> FUN_001a7740 -> FUN_001a77c0 -> FUN_001a80c0
        ++files;
        std::string err;
        auto s = data::parseTlk(bytes, &err);
        if (!s) {
            ++mismatch;
            std::printf("  %s: data::parseTlk failed: %s\n", talkFileName(i), err.c_str());
            continue;
        }
        bool ok = m.state == 5;
        ok = ok && m.voiceId == s->voice.value_or(0);
        ok = ok && m.sentence == s->sentence.value_or(0);
        ok = ok && m.speed == s->speed.value_or(1);
        ok = ok && m.layout == s->layout.value_or(0);
        const int32_t numOp = s->timer.value_or(-1);
        ok = ok && ((numOp == -1) == (m.number == -1));
        int n = 0;
        while (n < 33 && m.triggers[size_t(n)].type != 0) ++n;
        ok = ok && n == static_cast<int>(s->triggers.size());
        for (int k = 0; k < n && ok; ++k) {
            const data::TlkTrigger& a = m.triggers[size_t(k)];
            const data::TlkTrigger& b = s->triggers[size_t(k)];
            ok = a.kind == b.kind && a.type == b.type && a.frame == b.frame && a.w == b.w && a.name[0] == b.name[0] &&
                 a.name[1] == b.name[1] && a.f0 == b.f0 && a.g0 == b.g0 && a.g1 == b.g1;
            ++kindHist[a.kind];
        }
        if (!ok) {
            ++mismatch;
            if (mismatch < 10) std::printf("  mismatch %s\n", talkFileName(i));
        }
        trigTotal += n;
        if (maxTrig < n) maxTrig = n;
        if (m.noVoice != 0) ++noVoice;
        else ++voiced;
        if (m.layout == 2) ++layout2;
        if (layoutMax < m.layout) layoutMax = m.layout;
        for (int k = 0; k < n; ++k)
            if (m.triggers[size_t(k)].type == 0xb && layoutMax < m.triggers[size_t(k)].w) layoutMax = m.triggers[size_t(k)].w;
        if (m.number != -1) ++numberPages;
        // sentences the page and the 0x66 triggers show must have their closing ';' inside the 0x400 text bytes
        // (or end the text with its NUL inside the 0x400 bytes: the original's copy then runs past +0x48c, but the
        // window shows the same string up to the NUL, so the port's bound is equivalent)
        auto terminated = [&](uint32_t sentence) {
            uint32_t count = 0;
            for (int p = 1; p < 0x400; ++p) {
                if (m.text[p] == ';' && ++count > sentence) return true;
                if (m.text[p] == '\0' && count == sentence) {
                    ++nulEnded;
                    return true;
                }
            }
            return false;
        };
        auto noteUnterminated = [&](uint32_t sentence) {
            ++noTerm;
            std::printf("  %s: sentence %u has no closing ';'\n", talkFileName(i), sentence);
        };
        if (m.noVoice == 0 && !terminated(static_cast<uint32_t>(m.sentence))) noteUnterminated(static_cast<uint32_t>(m.sentence));
        for (int k = 0; k < n; ++k)
            if (m.triggers[size_t(k)].type == 2 && !terminated(static_cast<uint32_t>(m.triggers[size_t(k)].w)))
                noteUnterminated(static_cast<uint32_t>(m.triggers[size_t(k)].w));
        const bool isC24 = std::strcmp(talkFileName(i), "C24_V073") == 0;
        if (isC24) ++c24;
        if (std::memchr(m.text, '%', sizeof m.text) != nullptr && m.number == -1 && !isC24) ++percentBad;
        if (m.noVoice == 0 && ui::helpBar().slotText(3)[0] == '\0') ++emptyText;
        m.end();
    }
    std::printf("lang %s: %d files (%d missing), voiced %d, text-only %d, layout 2 %d, number pages %d, triggers %d (max %d per file)\n",
                lang == 1 ? "JP" : "EN", files, missing, voiced, noVoice, layout2, numberPages, trigTotal, maxTrig);
    expect(missing == 0, "every talk-table file loads through FUN_001a7660 (" + std::string(lang ? "TALK" : "TALK_EN") + ")");
    expect(mismatch == 0, "manager page state == data::parseTlk for every file");
    expect(maxTrig <= 32, "no file needs the 33rd trigger entry");
    // C18A uses layouts 1/6/7/9/10, C22A/B 8/9, C19 5/10: a 3-row table read past its end (garbage rect, text at the
    // top-left corner wrapped every 2 characters)
    expect(layoutMax < static_cast<int>(std::size(data::kTalkWindowRects)),
           "every window layout (max " + std::to_string(layoutMax) + ") is a row of the 0x4620e0 rect table (" +
               std::to_string(std::size(data::kTalkWindowRects)) + " rows)");
    // JP: C03_V826, C04A_V005, C04A_V016 hold only "-1;" while a 0x66 trigger shows sentence 2: the original's copy loop
    // runs past the text into the manager fields (UNVERIFIED content), the port's bounded copy shows an empty line
    expect(noTerm == (lang == 1 ? 3 : 0), "every shown sentence ends with ';' or the text's NUL (" + std::to_string(nulEnded) +
                                              " by NUL, " + std::to_string(noTerm) + " unterminated" +
                                              (lang == 1 ? ", 3 known JP placeholders)" : ")"));
    expect(percentBad == 0, "'%' only in number pages / C24_V073 (" + std::to_string(percentBad) + " others)");
    expect(emptyText == 0, "every voiced page puts text in window slot 3");
}

// ---- 3. conversations -------------------------------------------------------------------------------------------
void runTicks(int ticks, bool printText) {
    TalkManager& m = talkManager();
    std::string last = "";
    for (int t = 0; t < ticks; ++t) {
        m.taskUpdate();                                       // FUN_001a1dc0
        if (gW.voicePlaying > 0) --gW.voicePlaying;
        for (const std::string& e : gW.events) std::printf("    t%03d %s\n", t, e.c_str());
        gW.events.clear();
        const char* txt = ui::helpBar().slotText(3);
        if (printText && last != txt) {
            last = txt;
            if (txt[0] != '\0') std::printf("    t%03d window: \"%s\"\n", t, txt);
            else std::printf("    t%03d window cleared\n", t);
        }
    }
}

int converse(const char* speaker, uint32_t extraFlag) {
    TalkManager& m = talkManager();
    flags().clearAll();
    flags().newGameWipe();
    if (extraFlag != 0) flags().set(extraFlag);
    m.construct();
    gW.actors.clear();
    gW.actors.push_back(FakeActor{0, data::tag4("C00A"), {0, 0, 150}, {0, 0, -1}});
    FakeActor npc{1, 0, {0, 0, 0}, {0, 0, 1}};
    std::memcpy(&npc.id, speaker, 4);
    gW.actors.push_back(npc);
    gW.log = true;
    std::printf("  %s (flags new game%s):\n", speaker, extraFlag ? (" + " + std::to_string(extraFlag)).c_str() : "");
    int started = -1;
    for (int t = 0; t < 60 && started < 0; ++t) {   // cooldown 15 + start-up delay 20
        m.taskUpdate();
        if (m.talkIndex >= 0) started = m.talkIndex;
    }
    if (started < 0) {
        std::printf("    no approach talk started (candidate %s, previous %s, state %d, talk search %d)\n",
                    idText(m.candidate).c_str(), idText(m.prevCandidate).c_str(), m.state,
                    m.search(&gW.actors[1], 0, -1, -1, 0));
        gW.log = false;
        return -1;
    }
    std::printf("    talk %d %s (voice no. %s, category %d)\n", started, talkFileName(started),
                data::talkTable()[started].voiceNo, m.category);
    runTicks(200, true);
    std::printf("    state %d active %d farewell %s\n", m.state, m.active, idText(m.farewell).c_str());
    gW.log = false;
    return started;
}

}  // namespace

int main() {
    SetTraceLogLevel(LOG_NONE);
    setTalkWorld(&gW);
    std::map<int, int> hist;
    std::printf("== 1. page parser + trigger expansion over every TLK\n");
    parseAll(0, hist);
    std::map<int, int> histJp;
    parseAll(1, histJp);
    ui::setLanguage(0);
    std::printf("== 2. trigger kinds (EN): ");
    int total = 0;
    for (const auto& [k, n] : hist) {
        std::printf("0x%x:%d ", k, n);
        total += n;
    }
    std::printf("(%d)\n", total);
    expect(hist[0x69] == 4968 && hist[0x6c] == 5286 && hist[0x6a] == 720 && hist[0x66] == 483,
           "trigger corpus counts match the analysis (0x69 4968, 0x6c 5286, 0x6a 720, 0x66 483)");

    std::printf("== 3. conversations (fake world, voice 45 ticks)\n");
    // speakers with an approach talk (kind 0) under the new-game flags, found through FUN_001a69e0 itself
    flags().clearAll();
    flags().newGameWipe();
    std::vector<std::string> speakers;
    for (int i = 0; i < data::kTalkTableCount && speakers.size() < 6; ++i) {
        const char* sp = data::talkTable()[i].speaker;
        if (std::strlen(sp) != 4 || sp[0] != 'C') continue;
        bool seen = false;
        for (const std::string& x : speakers) seen = seen || x == sp;
        if (seen) continue;
        TalkActorRef r;
        r.type = 1;
        std::memcpy(&r.id, sp, 4);
        if (talkSearch(TalkSelectState{}, &r, 0, -1, -1, false, flags()) >= 0) speakers.push_back(sp);
    }
    int ok = 0;
    for (const std::string& s : speakers)
        if (converse(s.c_str(), 0) >= 0) ++ok;
    expect(ok >= 3, "at least three speakers start an approach talk (" + std::to_string(ok) + ")");

    std::printf("  VOICE LIST path FUN_001a73a0(\"C04A_V009\") in a menu (no Game scene):\n");
    gW.game = false;
    gW.log = true;
    talkManager().construct();
    talkManager().playByName("C04A_V009");
    const int idxPlayed = talkManager().talkIndex;
    for (int t = 0; t < 60; ++t) {
        talkManager().tick();                                 // the page update calls FUN_001a2110 itself
        if (gW.voicePlaying > 0) --gW.voicePlaying;
        for (const std::string& e : gW.events) std::printf("    t%03d %s\n", t, e.c_str());
        gW.events.clear();
    }
    std::printf("    window \"%s\", state %d\n", ui::helpBar().slotText(3), talkManager().state);
    expect(idxPlayed >= 0 && std::strcmp(talkFileName(idxPlayed), "C04A_V009") == 0, "FUN_001a73a0 finds and plays C04A_V009");
    std::printf("  FUN_001aa1b0(voice number of row 100 = %s):\n", data::talkTable()[100].voiceNo);
    talkByVoiceNumber(std::atoi(data::talkTable()[100].voiceNo));
    expect(talkManager().talkIndex >= 0 && std::atoi(data::talkTable()[talkManager().talkIndex].voiceNo) ==
                                               std::atoi(data::talkTable()[100].voiceNo),
           "talk by voice number");
    for (const std::string& e : gW.events) std::printf("    %s\n", e.c_str());
    gW.events.clear();
    gW.log = false;
    std::printf(gFail == 0 ? "ALL PASS\n" : "%d FAILURES\n", gFail);
    return gFail == 0 ? 0 : 1;
}
