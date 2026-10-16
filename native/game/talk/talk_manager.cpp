// Talk manager 0x53c6e0: lifecycle, loading, page parser, triggers, per-tick (see talk_manager.hpp).
#include "game/talk/talk_manager.hpp"

#include "game/flag_manager.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace baroque::game {

namespace {

TalkManager gTalk;
TalkWorld* gWorld = nullptr;

constexpr uint32_t kC18A = data::tag4("C18A");     // 0x4538e8
constexpr int kTextSlot = 3;                        // text window slot of the talk window (0x5180f0 slot 3)

// 0x4620e0: talk window rectangles [layout][lang] {x, y, w, h} (indexed lang*0x10 + layout*0x20), 12 layouts up to the
// colour table 0x462260; the TLK files use 0..10 (Baroque-TalkCheck asserts it). The original does not bound the index.
const float* windowRect(int layout, int lang) {
    return data::kTalkWindowRects[layout][lang];
}

// The registry compare of the selectors: the 4 chars of a talk-table string against an actor id (signed bytes).
bool idMatches(const char* s, uint32_t id) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) == (id & 0xffu) &&
           static_cast<uint32_t>(static_cast<uint8_t>(s[1])) == ((id >> 8) & 0xffu) &&
           static_cast<uint32_t>(static_cast<uint8_t>(s[2])) == ((id >> 16) & 0xffu) &&
           static_cast<uint32_t>(static_cast<uint8_t>(s[3])) == (id >> 24);
}

// The sentence copy loop shared by FUN_001a77c0 and FUN_001a8750 type 2: bytes of +0x8c are copied until the n+1-th
// ';' after the start; every ';' up to the n-th restarts the buffer. PORT: bounded by the 0x400 text bytes (the
// original walks on into the fields after +0x48c when the n-th sentence has no closing ';'; every shipped file has
// it, see the check).
void extractSentence(const char* text, uint32_t n, char out[0x410]) {
    int src = 0;
    int dst = 0;
    uint32_t count = 0;
    std::memset(out, 0, 0x410);
    do {
        if (src >= 0x400) break;                                        // PORT bound
        int next = src + 1;
        out[dst] = text[src];
        ++dst;
        if (next < 0x400 && text[next] == ';') {
            ++count;
            if (count <= n) {
                std::memset(out, 0, 0x401);
                dst = 0;
                next = src + 2;
            }
        }
        src = next;
        out[dst] = 0;
        if (dst >= 0x40f) break;                                        // PORT bound (buffer 0x410)
    } while (count <= n);
}

}  // namespace

TalkManager& talkManager() { return gTalk; }
void setTalkWorld(TalkWorld* w) { gWorld = w; }
TalkWorld* talkWorld() { return gWorld; }

// orig FUN_001a9780 (work/decompiled/001a9780.c): (&PTR_s_C00A_V15B_00457020)[idx * 3]
const char* talkFileName(int idx) {
    if (idx < 0 || idx >= data::kTalkTableCount) return "";
    return data::talkTable()[idx].file;
}

// orig FUN_001a1b70 (work/decompiled/001a1b70.c): FUN_001a1c30, memset(+0x494, 0, 600), 32 trigger entries cleared
void TalkManager::construct() {
    reset();
    voiceId = 0;
    sentence = 0;
    triggerBlock.fill(0);
    for (data::TlkTrigger& t : triggers) t = data::TlkTrigger{};
}

// orig FUN_001a1c30 (work/decompiled/001a1c30.c)
void TalkManager::reset() {
    enabled = 1;
    cooldown = 0xf;
    active = 0;
    talkIndex = -1;
    facing = 0;
    playerInitiated = 0;
    outOfRange = 0;
    noVoice = 0;
    category = -1;
    speaker = 0;
    prevSpeaker = 0;
    candidate = 0;
    prevCandidate = 0;
    itemTalkActor = 0;
    lastDbCount = 0;
    excluded = 0;
    // words 0xe..0x1c: +0x38 .. +0x70 (volume, its timer, the camera pointer and the camera vectors)
    volume = 0.0f;
    volTimer = 0;
    camera = nullptr;   // the camera task is not popped here (FUN_001a6710 / FUN_001a2cd0 do that)
    for (int k = 0; k < 3; ++k) {
        camPos[k] = 0.0f;
        camRight[k] = 0.0f;
        camUp[k] = 0.0f;
        camFwd[k] = 0.0f;
    }
    frame = -1;
    dataValid = false;   // uRam0053c6b8 = 0
    dataOfs = 0;
    state = 0;           // uRam0053c6c8 = 0
    moveDelta[0] = 0.0f;
    moveDelta[1] = 0.0f;
    moveDelta[2] = 0.0f;
    moveActor = 0;
    floorTalkDone = 0;
    idleTimer = 0x708;
    mapId = -1;
    roomByte = -1;
    stareTimer = -1;
    farewell = 0;
    lastHp = 0;
    lastVt = 0;
    mapList.fill(-1);
    zone = -1;
    listCursor = 0;
    specialItem = nullptr;
    fb0 = 0;
    lingerTicks = 0;
    lingerNpc = 0;
    giftObj = nullptr;
    fc0 = 0;
    dbStage = 1;
    tutorialCounter = 0;
    scheduledCount = -1;
    nameIndex = -1;
    number = -1;
    skippable = 1;
}

// orig FUN_001a2910 (work/decompiled/001a2910.c): talk window slot 3 paused (+0x2900) and disabled (+0x2928)
void TalkManager::freezeWindow() {
    ui::HelpBar& hb = ui::helpBar();
    if (hb.freeze(kTextSlot) == 0) hb.setFreeze(kTextSlot, 1);
    if (hb.enabled(kTextSlot) != 0) hb.setEnabled(kTextSlot, 0);
}

// orig FUN_001a1dc0 (work/decompiled/001a1dc0.c): the talk task of the engine tick FUN_00101bd0
void TalkManager::taskUpdate() {
    TalkWorld& w = *gWorld;
    if (loadPending) {                                            // PORT: FUN_0010cf60 completion (see loadAsync)
        loadPending = false;
        loadCallback();                                           // FUN_001a7270
    }
    if (w.messageBoxOpen()) startDelay = 0;                       // iRam0053d734 != 0 -> +0x88 = 0
    if (0 < cooldown) cooldown = cooldown - 1;
    if (w.sceneIsGame() && (w.itemScreenOpen() || w.systemMenuOpen() || w.enginePaused())) {
        freezeWindow();                                           // FUN_001a2910
        return;
    }
    if (w.player() == nullptr) return;                            // iRam0050b144 == 0
    bool selected = false;
    if (w.sceneIsGame()) {
        if ((w.floorFlags() & 1u) != 0 && !w.eventRunning() && w.eventMode() == 0 && cooldown < 1 && enabled != 0) {
            if (state == 2) return;
            select();                                             // FUN_001a23a0
            tick();                                               // FUN_001a2110
            moveJob();                                            // FUN_001a2870
            selected = true;                                      // goto LAB_001a202c
        }
    }
    if (!selected) {
        if (active == 0) {
            if (cooldown < 1) cooldown = 0xf;
        } else {
            end();                                                // FUN_001a6710
        }
    }
    // LAB_001a202c
    if (w.eventMode() != 0) scheduledCount = -1;
    if (w.sceneIsGame() && (w.floorFlags() & 1u) == 0) {
        reset();                                                  // FUN_001a1c30
        giftGlow = 0;                                             // uRam00518090 = 0
    }
}

// orig FUN_001a2110 (work/decompiled/001a2110.c)
void TalkManager::tick() {
    TalkWorld& w = *gWorld;
    if (active == 0) return;
    if (state == 6) return;
    cameraTick();                                                 // FUN_001a2b40
    drawSpeakerName(nameIndex);                                   // FUN_001a99b0(mgr, +0x1018)
    bool run;
    if (state == 3) run = true;
    else run = state == 5 || state == 4 || noVoice != 0;
    if (run) {
        bool stoppedCheck;
        if (noVoice == 0) {
            if (state != 5) stoppedCheck = true;
            else {
                const int st = w.voiceStatus();                   // FUN_00115cd0
                stoppedCheck = st != 4 && w.voiceStatus() != 3;
            }
        } else {
            stoppedCheck = false;
        }
        if (stoppedCheck) {
            const int st = w.voiceStatus();
            if ((st == 6 || w.voiceStatus() == 0 || w.voiceStatus() == 7) && state != 6) {
                state = 6;
                if (facing != 0 && static_cast<uint32_t>(category) < 3u) farewell = speaker;   // +0xd98 = +0x18
                end();                                            // FUN_001a6710
            }
        } else {
            fireTriggers(0);                                      // FUN_001a8750(mgr, 0)
            frame = frame + 1;
            state = 5;
            if (noVoice != 0 && 5 < frame) end();
        }
    }
    // LAB_001a22b0: talk window slot 3 resumed and enabled
    ui::HelpBar& hb = ui::helpBar();
    if (hb.freeze(kTextSlot) != 0) hb.setFreeze(kTextSlot, 0);
    if (hb.enabled(kTextSlot) == 0) hb.setEnabled(kTextSlot, 1);
    if (camera != nullptr && speaker != 0) {
        if (TalkActor a = actorById(speaker)) {                   // FUN_001a9810(mgr, +0x18)
            float f[3];
            w.actorForward(a, f);
            const float d[3] = {-f[0], -f[1], -f[2]};
            w.setMainLightDir(d);                                 // FUN_00108b70(0x4e78e0, uRam0050a2cc, -forward)
        }
    }
}

// orig FUN_001a2870 (work/decompiled/001a2870.c): the trigger 0x6e move job (actor id from +0xd7c, dropped by Ghidra)
void TalkManager::moveJob() {
    TalkWorld& w = *gWorld;
    if (-1 < moveFrames && moveActor != 0) {
        TalkActor a = actorById(moveActor);
        if (a != nullptr) {
            float p[3];
            w.actorPos(a, p);
            const float np[3] = {p[0] + moveDelta[0], p[1] + moveDelta[1], p[2] + moveDelta[2]};
            w.setActorPos(a, np);
            moveFrames = moveFrames - 1;
            if (moveFrames < 0) {
                moveDelta[0] = 0.0f;
                moveDelta[1] = 0.0f;
                moveDelta[2] = 0.0f;
                moveActor = 0;
            }
        }
    }
}

// orig FUN_001a6710 (work/decompiled/001a6710.c)
void TalkManager::end() {
    TalkWorld& w = *gWorld;
    if (!(active != 0 && 2u < static_cast<uint32_t>(state))) return;
    if (frame == 0) {
        fireTriggers(0);
        frame = frame + 1;
    }
    if (state == 6) fireTriggers(1);
    else if (state == 5) fireTriggers(2);
    if (TalkActor a = actorById(speaker)) {
        // head sub-object (vt+0x28): FUN_0010a8e0(0.2, 1.0, head, anim "F01" (0x462508) or 0, 0)
        w.partAnim(a, 1, "F01", 0.2f, 1.0f);
    }
    voiceId = 0;                                                  // memset(+0x494, 0, 600)
    sentence = 0;
    triggerBlock.fill(0);
    for (data::TlkTrigger& t : triggers) t = data::TlkTrigger{};  // 32 x memset(+0x6ec + i*0x34, 0, 0x34)
    ui::helpBar().clear(kTextSlot);                               // FUN_0015c3f0(0x5180f0, 3)
    if (noVoice == 0) w.voiceStop();                              // FUN_00115c10
    cameraRelease();                                              // FUN_001a2cd0
    frame = -1;
    talkIndex = -1;
    prevSpeaker = speaker;
    speaker = 0;
    active = 0;
    state = 0;
    stareTimer = category == 9 ? 0x5a : -1;
    category = -1;
    w.bgmVolume(1.0f, 4);                                         // FUN_00115f00(1.0, snd, 4)
    volume = 1.0f;
    nameIndex = -1;
    number = -1;
    noVoice = 0;
}

// orig FUN_001a7200 (work/decompiled/001a7200.c): voice volume follows the distance, at most every 10 ticks
void TalkManager::setVolume(float v) {
    if (volTimer == 0) {
        float f = 0.3f;
        if (0.3f <= v) f = v;
        gWorld->voiceVolume(f);                                   // FUN_00115af0(f, 0x4e9790)
        volTimer = 10;
    } else {
        volTimer = volTimer - 1;
    }
}

// orig FUN_001a7660 (work/decompiled/001a7660.c). PORT: the port reads the file at once; the completion callback
// FUN_001a7270 of FUN_0010cf60's asynchronous read runs at the start of the next talk task update (the original's
// read finishes at least one engine tick later; until then the state stays 1 and the selectors / the parser wait).
// `sync` is the 6th argument of FUN_0010cf60 (read priority), without effect here.
void TalkManager::loadAsync(int idx, int sync) {
    (void)sync;
    dataValid = false;                                            // uRam0053c6b8 = 0
    talkIndex = idx;
    state = 1;
    const std::string path = data::talkPath(ui::language() == 1, talkFileName(idx));   // 0x462520 / 0x462540
    file.clear();
    loadPending = gWorld->loadFile(path.c_str(), file);
}

// orig FUN_001a7270 (work/decompiled/001a7270.c)
void TalkManager::loadCallback() {
    TalkWorld& w = *gWorld;
    dataOfs = 0;                                                  // *(file object + 0x44): the file data
    dataValid = true;
    if (w.sceneIsGame() && (w.floorFlags() & 1u) != 0 && !w.eventRunning() && w.eventMode() == 0) {
        state = 2;
        begin();                                                  // FUN_001a7740(0x53c6e0)
        state = 5;
    } else {
        state = 0;
    }
    releaseFile();                                                // FUN_001a69a0
}

// orig FUN_001a69a0 (work/decompiled/001a69a0.c): FUN_0020ef70(file), iRam0053c6c0 = 0
void TalkManager::releaseFile() {
    file.clear();
    file.shrink_to_fit();
}

// orig FUN_001a7540 (work/decompiled/001a7540.c): the first NPC whose id is the speaker of a talk-table row named `name`
TalkActor TalkManager::actorOfTalk(const char* name) const {
    TalkWorld& w = *gWorld;
    const data::TalkEntry* t = data::talkTable();
    for (int i = 0; i < 0x636; ++i) {
        if (std::strcmp(t[i].file, name) != 0) continue;
        const int n = w.actorCount();
        for (int k = 0; k < n; ++k) {
            TalkActor a = w.actorAt(k);
            if (w.actorType(a) == 1 && idMatches(t[i].speaker, w.actorId(a))) return a;
        }
    }
    return nullptr;
}

// orig FUN_001a73a0 (work/decompiled/001a73a0.c): synchronous play by file name (VOICE LIST, FUN_001aa1b0)
void TalkManager::playByName(const char* name) {
    TalkWorld& w = *gWorld;
    end();                                                        // FUN_001a6710
    active = 1;
    TalkActor a = actorOfTalk(name);                              // FUN_001a7540
    speaker = a == nullptr ? 0 : w.actorId(a);
    candidate = speaker;
    prevCandidate = speaker;
    playerInitiated = 0;
    const data::TalkEntry* t = data::talkTable();
    for (int i = 0; i < data::kTalkTableCount && t[i].file != nullptr; ++i) {
        if (std::strcmp(t[i].file, name) != 0) continue;
        talkIndex = i;
        const std::string path = data::talkPath(ui::language() == 1, name);
        file.clear();
        loadPending = false;                                      // PORT: a pending asynchronous read is dropped
        // file system object 0x53f814: vt+0xc open, vt+0x14 read (synchronous), +0x44 data, vt+0x10 close
        if (!w.loadFile(path.c_str(), file)) file.assign(0x800, 0);   // UNVERIFIED: a missing file (never happens)
        dataOfs = 0;
        dataValid = true;
        state = 2;
        begin();                                                  // FUN_001a7740(0x53c6e0)
        releaseFile();                                            // FUN_0020ef70
        state = 5;
        return;
    }
}

// orig FUN_001a7740 (work/decompiled/001a7740.c)
void TalkManager::begin() {
    skippable = 1;
    farewell = 0;
    speed = 1;
    layout = 0;
    frame = 0;
    stareTimer = -1;
    std::memset(text, 0, sizeof text);
    if (file.size() >= dataOfs + 0x400) std::memcpy(text, file.data() + dataOfs, 0x400);   // FUN_0023c268(+0x8c, data, 0x400)
    dataOfs += 0x404;
    pc = 0;
    parsePage();                                                  // FUN_001a77c0
}

// orig FUN_001a77c0 (work/decompiled/001a77c0.c)
void TalkManager::parsePage() {
    TalkWorld& w = *gWorld;
    auto word = [&](int i) -> int32_t {
        const size_t o = dataOfs + static_cast<size_t>(i) * 4;
        if (o + 4 > file.size()) return 0xc9;                     // PORT bound: past the file = end of page
        int32_t v;
        std::memcpy(&v, file.data() + o, 4);
        return v;
    };
    uint32_t guard = 0;
    frame = 0;
    int32_t op = word(pc);
    bool began = false;
    if (op != 0xc9) {
        for (;;) {
            if (op == 2) {
                if (began) break;
                began = true;
                pc = pc + 1;
            } else if (op == 3) {
                voiceId = word(pc + 1);
                pc = pc + 2;
            } else if (op == 4) {
                sentence = word(pc + 1);
                pc = pc + 2;
            } else if (op == 7) {
                speed = word(pc + 1);
                pc = pc + 2;
            } else if (op == 8) {
                const int32_t v = word(pc + 1);
                pc = pc + 2;
                if (w.sceneIsGame()) layout = v;
            } else if (op == 9) {
                const int32_t base = w.databaseCount();           // iRam0053dcc8
                const int32_t v = word(pc + 1);
                pc = pc + 2;
                if (v != -1) {
                    if (v == 0) number = w.listFlagCount();       // FUN_001f8c60()
                    else number = databaseStage(flags()) - base;  // FUN_001dacd0(0x53dc00) - 0x53dcc8
                }
            } else if (op == 100) {
                pc = pc + 1;
                for (int i = 0; i < 0x94; ++i) triggerBlock[size_t(i)] = word(pc + i);   // FUN_0023c268(+0x49c, .., 0x250)
                pc = pc + 0x94;
            } else {
                break;
            }
            ++guard;
            if (0x8000 < guard) break;
            op = word(pc);
            if (op == 0xc9) break;
        }
    }
    if (voiceId == -1) {
        noVoice = 1;
    } else {
        if (!w.enginePaused()) {
            if (w.itemScreenOpen()) w.voice(voiceId);             // LAB_001a7b44: FUN_00115a60(1.0, snd, voice)
            else w.voice3d(voiceId);                              // FUN_00115970(1.0, snd, voice, 0)
        } else {
            w.voice(voiceId);                                     // FUN_00115a60(1.0, snd, voice)
        }
        w.bgmVolume(0.3f, 4);                                     // FUN_00115f00(0x3e99999a, snd, 4)
        noVoice = 0;
    }
    if (noVoice == 0) {
        char buf[0x410];
        extractSentence(text, static_cast<uint32_t>(sentence), buf);
        const int lang = ui::language();
        const float* r = windowRect(layout, lang);
        ui::HelpBar& hb = ui::helpBar();
        if (number == -1) {
            if (std::strcmp(talkFileName(talkIndex), "C24_V073") == 0) {   // 0x456950
                hb.set(r[0], r[1], r[2], r[3], kTextSlot, buf, talkPlayerName());   // FUN_001a9b20
            } else {
                hb.set(r[0], r[1], r[2], r[3], kTextSlot, buf);
                if (layout == 2 && lang == 0) hb.setStyle(kTextSlot, 1);   // uRam0051ab3c = 1
            }
        } else {
            hb.set(r[0], r[1], r[2], r[3], kTextSlot, buf, number);
        }
        hb.setSpeed(kTextSlot, speed);                            // uRam005181c8 = +0x48c
        if (w.sceneIsGame()) hb.setScale(kTextSlot, 1.16f, 1.22f);   // DAT_0044c180 / DAT_0044c188
        hb.setColor(kTextSlot, data::kTalkTextColour[lang]);     // uRam0051ab14 = 0x462260[lang]
        if (lang == 0 && speaker == kC18A) {
            hb.setProportional(kTextSlot, 0);                     // uRam0051ab64 = 0
            if (w.sceneIsGame()) hb.setScale(kTextSlot, 1.0f, 1.0f);
        }
    }
    // LAB_001a8054
    nameIndex = speakerIndex(talkIndex >= 0 && talkIndex < data::kTalkTableCount ? data::talkTable()[talkIndex].speaker : "");
    expandTriggers();                                             // FUN_001a80c0
}

// orig FUN_001a80c0 (work/decompiled/001a80c0.c) through data::expandTlkTriggers. The original writes up to 33
// entries (the bound test `0x1f < entry` runs before the increment): a 33rd entry would land on +0xd6c..; no shipped
// file has more than 32 (the check counts them), the port keeps a 33rd slot instead (UNVERIFIED for that case).
void TalkManager::expandTriggers() {
    const std::vector<int32_t> block(triggerBlock.begin(), triggerBlock.end());
    const std::vector<data::TlkTrigger> list = data::expandTlkTriggers(block);
    for (size_t i = 0; i < list.size() && i < triggers.size(); ++i) triggers[i] = list[i];
}

// orig FUN_001a8750 (work/decompiled/001a8750.c)
void TalkManager::fireTriggers(int when) {
    TalkWorld& w = *gWorld;
    if (!(1u < static_cast<uint32_t>(state - 1))) return;
    for (int i = 0; i < 0x20; ++i) {
        data::TlkTrigger& t = triggers[size_t(i)];
        const int type = t.type;
        if (type == 0) return;
        const int f = t.frame;
        if (!(frame == f || (when == 1 && f == -1) || (when == 2 && f == -2))) continue;
        const bool game = w.sceneIsGame();
        if (type == 1 && game) {
            // EXI effect at the speaker + offset: FUN_00112190(0, id, pos, rot (0,0,0,1), scale (100,100,100,1))
            if (speaker != 0) {
                if (TalkActor a = actorById(speaker)) {
                    float p[3];
                    w.actorPos(a, p);
                    const float pos[4] = {t.f0 + p[0], t.f1 + p[1], t.f2 + p[2], 1.0f};
                    w.effect(t.w, pos);
                }
            }
        } else if (type == 2) {
            if (noVoice == 0) {
                char buf[0x410];
                extractSentence(text, static_cast<uint32_t>(t.w), buf);
                const int lang = ui::language();
                const float* r = windowRect(layout, lang);
                ui::HelpBar& hb = ui::helpBar();
                if (number == -1) {
                    hb.set(r[0], r[1], r[2], r[3], kTextSlot, buf);
                    if (layout == 2 && lang == 0) hb.setStyle(kTextSlot, 1);
                } else {
                    hb.set(r[0], r[1], r[2], r[3], kTextSlot, buf, number);
                }
                hb.setSpeed(kTextSlot, speed);
                if (game) hb.setScale(kTextSlot, 1.16f, 1.22f);
                hb.setColor(kTextSlot, data::kTalkTextColour[lang]);
                if (lang == 0 && speaker == kC18A) {
                    hb.setProportional(kTextSlot, 0);
                    if (game) hb.setScale(kTextSlot, 1.0f, 1.0f);
                }
            }
        } else if (type == 3) {
            ui::helpBar().clear(kTextSlot);                       // FUN_0015c3f0(0x5180f0, 3) ($a1 = 3, disasm 0x1a8d78)
        } else if (type == 0xb) {
            layout = t.w;
        } else if (type == 4 && game) {
            if (speaker != 0) {
                if (TalkActor a = actorById(speaker)) {
                    const std::string name = t.nameText();
                    // FUN_0010a8e0($f12 = +0x71c / 10 blend, $f13 = +0x718 / 10 speed, body actor, anim, 0)
                    w.partAnim(a, 0, name.c_str(), t.g1 / 10.0f, t.g0 / 10.0f);
                }
            }
        } else if (static_cast<uint32_t>(type - 6) < 2u && game) {
            if (speaker != 0) {
                if (TalkActor a = actorById(speaker)) {
                    const std::string name = t.nameText();
                    w.partAnim(a, type == 6 ? 1 : 2, name.c_str(), t.g1 / 10.0f, t.g0 / 10.0f);   // vt+0x28 / vt+0x2c
                }
            }
        } else if (type == 5 && game) {
            if (speaker != 0) {
                if (TalkActor a = actorById(speaker)) {
                    char name[12] = {};
                    std::memcpy(name, t.name, 8);
                    w.actAction(a, name);                         // FUN_00143a40(*(+0x2c)+0x10, name)
                }
            }
        } else if (type == 8 && game) {
            flags().set(static_cast<uint32_t>(t.w));              // FUN_00178890(0x53c3b0, w)
        } else if (type == 9 && game) {
            flags().clear(static_cast<uint32_t>(t.w));            // FUN_00178a30(0x53c3b0, w)
        } else if (type == 10 && game) {
            if (speaker != 0) {
                if (TalkActor a = actorById(speaker)) {
                    moveActor = w.actorId(a);
                    moveFrames = t.w;
                    moveDelta[0] = t.f0;
                    moveDelta[1] = t.f1;
                    moveDelta[2] = t.f2;
                }
            }
        } else if (type == 0xc && game) {
            char name[16] = {};
            std::memcpy(name, t.name, 8);
            std::memcpy(name + 8, &t.w714, 4);
            std::memset(scheduled, 0, sizeof scheduled);
            std::memcpy(scheduled, name, sizeof name);            // FUN_0023f540 strcpy(+0xfcc, name) (name is NUL padded)
            scheduledCount = t.w;
        } else if (type == 0xd && game) {
            counters().add(t.w, 1);                               // FUN_00179570(0x53c5c0, w, 1)
        } else if (type == 0xe && game) {
            if (speaker != 0) {
                if (TalkActor a = actorById(speaker)) excluded = w.actorId(a);
            }
        } else if (type == 0xf && game) {
            skippable = 0;
        }
    }
}

// orig FUN_001a99b0 (work/decompiled/001a99b0.c): speaker name above the talk window (string 0x16d + index)
void TalkManager::drawSpeakerName(int idx) {
    if (idx == -1) return;
    float a = volume * 64.0f;
    if (2.1474836e+09f <= a) a = a - 2.1474836e+09f;
    ui::TextRenderer& t = ui::text();
    t.layerFlag = 1;
    t.halfW = 1.16f * 8.0f;                                       // DAT_0044c180 * 8
    t.halfH = 1.22f * 9.0f;                                       // DAT_0044c188 * 9
    const int lang = ui::language();
    t.color = static_cast<uint32_t>(static_cast<int>(a)) * 0x1000000u + data::kTalkTextColour[lang];
    const float* r = windowRect(0, lang);
    t.x = r[0];
    t.z = 3.5f;                                                   // 0x40600000
    t.y = r[1] - 27.0f;
    t.draw("%s", ui::str(idx + 0x16d));                           // FUN_00112e80(0x4e7e90, "%s", FUN_00114da0(..))
}

// orig FUN_001a98a0 (work/decompiled/001a98a0.c): index in the speaker list 0x4625c0 (52 ids) when voiced in Game
int TalkManager::speakerIndex(const char* s) const {
    static const char* const kSpeakers[] = {   // 0x4625c0 {char*, 0} pairs, null terminated
        "C00A", "C01A", "C02A", "C03A", "C04A", "C04B", "C04E", "C05A", "C05B", "C05C", "C05D", "C05E", "C05F",
        "C05G", "C05H", "C05I", "C05J", "C05K", "C06A", "C07A", "C08A", "C08B", "C08C", "C09A", "C10A", "C10B",
        "C11A", "C12A", "C13A", "C13B", "C13C", "C14A", "C14B", "C15A", "C15B", "C16A", "C17A", "C18A", "C19A",
        "C20A", "C21A", "C22A", "C23A", "C23B", "C23C", "C23D", "C24A", "C25A", "C25B", "C26A", "C26B", "C27A"};
    if (noVoice == 0 && gWorld->sceneIsGame()) {
        for (int i = 0; i < static_cast<int>(sizeof kSpeakers / sizeof kSpeakers[0]); ++i)
            if (std::strcmp(kSpeakers[i], s) == 0) return i;
    }
    return -1;
}

// orig FUN_001a9810 (work/decompiled/001a9810.c): the first type-1 actor with that id
TalkActor TalkManager::actorById(uint32_t id) const {
    TalkWorld& w = *gWorld;
    const int n = w.actorCount();
    for (int i = 0; i < n; ++i) {
        TalkActor a = w.actorAt(i);
        if (w.actorType(a) == 1 && id == w.actorId(a)) return a;
    }
    return nullptr;
}

// orig FUN_001a9b20 (work/decompiled/001a9b20.c): the first name (string 0x82 + i) whose flag 0x443 + i is set, else
// "　" (0x462560). UNVERIFIED: the original loop only ends on a null string pointer; past the string table the port
// treats the id as null.
const char* talkPlayerName() {
    int i = 0;
    for (;;) {
        if (0x82 + i >= ui::stringCount()) return "\x81\x40";
        if (flags().test(static_cast<uint32_t>(i + 0x443))) break;
        ++i;
    }
    return ui::str(0x82 + i);                                     // FUN_001fafb0(i) = FUN_00114da0(i + 0x82)
}

// orig FUN_001aa1b0 (work/decompiled/001aa1b0.c): play the talk whose voice-number string is `voiceNo`
void talkByVoiceNumber(int voiceNo) {
    const data::TalkEntry* t = data::talkTable();
    for (int i = 0; i < 0x636; ++i) {
        if (std::atoi(t[i].voiceNo) == voiceNo) {               // FUN_002388f0
            char buf[0x40] = {};
            std::snprintf(buf, sizeof buf, "%s", t[i].file);      // FUN_0023edb8(buf, "%s", file)
            gTalk.playByName(buf);                                // FUN_001a73a0(0x53c6e0, buf)
            return;
        }
    }
}

// ---- notifications (FUN_001a9xxx, FUN_001a6230, FUN_001a5330) ----------------------------------------------------

// orig FUN_001a9cc0 (work/decompiled/001a9cc0.c)
void talkNoteItemTaken(TalkObj item) {
    if (!talkTutorialGate()) return;                              // FUN_001a6420
    flags().set(0xe3d);
    const uint32_t id = gWorld != nullptr ? gWorld->objRef(item).id : 0;
    if (id == data::tag4("HT00")) flags().set(0xe3e);             // 0x4624b8
    if (id == data::tag4("BN15")) flags().set(0xe3f);             // 0x462568
    if (id == data::tag4("BN14")) flags().set(0xe40);             // 0x462468
    if (id == data::tag4("BN02")) flags().set(0xe41);             // 0x462570
    if (id == data::tag4("BN00")) flags().set(0xe42);             // 0x462578
    if (id == data::tag4("BX00")) flags().set(0xe43);             // 0x462580
}
// orig FUN_001a9c60 (work/decompiled/001a9c60.c)
void talkNoteCoinToss(int heads) {
    if (!talkTutorialGate()) return;
    flags().set(heads == 0 ? 0xe4c : 0xe4b);
}
// orig FUN_001a9c20 (work/decompiled/001a9c20.c)
void talkNoteEnemyHit() { if (talkTutorialGate()) flags().set(0xe50); }
// orig FUN_001a9ba0 (work/decompiled/001a9ba0.c)
void talkNoteSpecial() { if (talkTutorialGate()) flags().set(0xe4d); }
// orig FUN_001a9be0 (work/decompiled/001a9be0.c)
void talkNoteVoiceCounter() { if (talkTutorialGate()) flags().set(0xe51); }
// orig FUN_001a9fb0 (work/decompiled/001a9fb0.c)
void talkNoteMap() { if (talkTutorialGate()) flags().set(0xe4e); }
// orig FUN_001a9ff0 (work/decompiled/001a9ff0.c)
void talkNoteItemScreen() { if (talkTutorialGate()) flags().set(0xe4f); }
// orig FUN_001a6230 (work/decompiled/001a6230.c)
void talkNoteHandOver(TalkObj item, int arg) {
    gTalk.specialItem = item;
    gTalk.fb0 = arg;
}
// orig FUN_001a5330 (work/decompiled/001a5330.c)
void talkNoteDatabase(uint32_t v) {
    const int count = gWorld != nullptr ? gWorld->databaseCount() : 0;
    gTalk.dbStage = count == databaseStage(flags()) ? 1 : 0;      // FUN_001dad50(0x53dc00)
    gTalk.fc0 = v;
}

}  // namespace baroque::game
