// Talk selection of the talk manager 0x53c6e0: FUN_001a23a0 and the selectors it calls in priority order
// (EVENTS_EFFECTS_PROGRESSION.md 1.2; the table search / conditions are game/progress/talk_select.cpp).
#include "game/talk/talk_manager.hpp"

#include "game/camera.hpp"
#include "game/flag_manager.hpp"
#include "game/ui/help_bar.hpp"

#include <cmath>
#include <cstring>

namespace baroque::game {

namespace {

constexpr uint32_t kC23A = data::tag4("C23A");   // 0x454228
constexpr uint32_t kC23B = data::tag4("C23B");   // 0x462288
constexpr uint32_t kC23C = data::tag4("C23C");   // 0x462290
constexpr uint32_t kC23D = data::tag4("C23D");   // 0x462298
constexpr uint32_t kJ030 = data::tag4("J030");   // 0x4622a0
constexpr uint32_t kGN00 = data::tag4("GN00");   // 0x4622a8
constexpr uint32_t kC24A = data::tag4("C24A");   // 0x4547b8

bool isC23(uint32_t id) { return id == kC23A || id == kC23B || id == kC23C || id == kC23D; }

// 0x4987d0: positional talk zones {x, 0, z, radius}, indexed by condition c3 - 1
constexpr float kZones[9][4] = {
    {-600.0f, 0.0f, 500.0f, 250.0f}, {800.0f, 0.0f, 2500.0f, 100.0f}, {1000.0f, 0.0f, 2500.0f, 100.0f},
    {1000.0f, 0.0f, 1500.0f, 250.0f}, {0.0f, 0.0f, 1300.0f, 200.0f}, {0.0f, 0.0f, -200.0f, 200.0f},
    {1600.0f, 0.0f, -2250.0f, 200.0f}, {1000.0f, 0.0f, 1700.0f, 200.0f}, {-600.0f, 0.0f, -1450.0f, 500.0f}};

// orig FUN_001a71b0 (work/decompiled/001a71b0.c): id not in the excluded list 0x498860 (empty: first word 0)
bool notExcludedList(uint32_t) { return true; }

// orig FUN_001a9f00 (work/decompiled/001a9f00.c): leaving map `m` (old +0xd88) sets a tutorial flag
void leftMap(int m) {
    if (!talkTutorialGate()) return;                 // FUN_001a6420
    if (m == 1) flags().set(0xe47);
    else if (m == 2) flags().set(0xe46);
    else if (m == 0xc) flags().set(0xe45);
    else if (m == 3) flags().set(0xe44);
}
// orig FUN_001aa120 (work/decompiled/001aa120.c)
void hpFlag(int n) {
    if (!talkTutorialGate()) return;
    if (n == 2) flags().set(0xe59);
    else if (n == 1) flags().set(0xe58);
    else if (n == 0) flags().set(0xe57);
}
// orig FUN_001aa070 (work/decompiled/001aa070.c)
void vtFlag(int n) {
    if (!talkTutorialGate()) return;
    if (n == 3) flags().set(0xe56);
    else if (n == 2) flags().set(0xe55);
    else if (n == 1) flags().set(0xe54);
    else if (n == 0) flags().set(0xe53);
}
// orig FUN_001aa030 (work/decompiled/001aa030.c)
void idleFlag() { if (talkTutorialGate()) flags().set(0xe52); }

// orig FUN_001a97a0 (work/decompiled/001a97a0.c): talk-table index of a file name, -1 when absent
int indexOfName(const char* name) {
    const data::TalkEntry* t = data::talkTable();
    for (int i = 0; i < 0x636; ++i)
        if (std::strcmp(t[i].file, name) == 0) return i;
    return -1;
}

bool idMatches(const char* s, uint32_t id) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) == (id & 0xffu) &&
           static_cast<uint32_t>(static_cast<uint8_t>(s[1])) == ((id >> 8) & 0xffu) &&
           static_cast<uint32_t>(static_cast<uint8_t>(s[2])) == ((id >> 16) & 0xffu) &&
           static_cast<uint32_t>(static_cast<uint8_t>(s[3])) == (id >> 24);
}

// orig FUN_001a2360 (work/decompiled/001a2360.c): an NPC (type 1) that is not the excluded speaker (+0x1010)
bool talkable(const TalkManager& m, TalkWorld& w, TalkActor a) {
    if (w.actorType(a) != 1) return false;
    return m.excluded != w.actorId(a);
}

}  // namespace

TalkSelectState TalkManager::selectState() const {
    static TalkActorRef shown;
    static TalkActorRef gift;
    TalkSelectState s;
    s.itemTalkActor = itemTalkActor;                 // +0x28
    if (specialItem != nullptr) {
        shown = talkWorld()->objRef(specialItem);
        s.shownItem = &shown;                        // +0xfac
    }
    s.fb0 = fb0;
    if (giftObj != nullptr) {
        gift = talkWorld()->objRef(giftObj);
        s.giftItem = &gift;                          // +0xfbc
    }
    s.fc0 = fc0;
    s.databaseStageSeen = dbStage;                   // +0xfc4
    s.excludedSpeaker = excluded;                    // +0x1010
    return s;
}

// orig FUN_001a69e0(mgr, actor, kind, a, b, anySpeaker) (game/progress/talk_select.cpp)
int TalkManager::search(TalkActor actor, int kind, int a, int b, int anySpeaker) {
    const TalkSelectState s = selectState();
    TalkActorRef ref;
    const TalkActorRef* rp = nullptr;
    if (actor != nullptr) {
        ref.type = talkWorld()->actorType(actor);
        ref.id = talkWorld()->actorId(actor);
        rp = &ref;
    }
    return talkSearch(s, rp, kind, a, b, anySpeaker != 0, flags());
}

// orig FUN_001a6d00(mgr, idx, kind, a, b)
int TalkManager::condition(int idx, int kind, int a, int b) {
    const TalkSelectState s = selectState();
    return talkCondition(s, idx, static_cast<uint32_t>(kind), a, b, flags());
}

// The registry loop the map / floor / positional / HP / VT / idle selectors share: the first talkable NPC whose id is
// the talk row's string `field` (2 = speaker at +8; FUN_001a5650 reads +4, the voice-number string, see selVt).
TalkActor TalkManager::firstNpcOfTalk(int idx, int field) const {
    TalkWorld& w = *talkWorld();
    const data::TalkEntry& e = data::talkTable()[idx];
    const char* s = field == 1 ? e.voiceNo : e.speaker;
    const int n = w.actorCount();
    for (int i = 0; i < n; ++i) {
        TalkActor a = w.actorAt(i);
        if (talkable(*this, w, a) && idMatches(s, w.actorId(a))) return a;
    }
    return nullptr;
}

// orig FUN_001a23a0 (work/decompiled/001a23a0.c)
int TalkManager::select() {
    TalkWorld& w = *talkWorld();
    TalkActor pl = w.player();
    if (pl == nullptr) return 0;
    if (static_cast<uint32_t>(state - 1) < 2u) return 0;
    if (w.itemScreenOpen()) return 0;                                        // iRam00517b68
    if (startDelay < 0x14) {
        startDelay = startDelay + 1;
        return 0;
    }
    const bool free = (w.playerControlFlags() & 0x10000u) == 0;            // uRam0050b620 & 0x10000
    if (free && selHitItem() != 0) return 1;                                 // FUN_001a3320
    if (free && selApproach() != 0) return 1;                                // FUN_001a2d40
    if (free) {
        const int room = w.actorRoom(pl);                                   // *(player +0x30)
        if (room != -2) {
            if (room < 0) {
                if (mapId != -1) leftMap(mapId);                            // FUN_001a9f00(mgr, +0xd88)
                mapId = -1;
            } else {
                const int m = w.roomMapId(room);                            // **(0x50a640[room] + 4)
                if (mapId != m) {
                    mapId = m;
                    mapList.fill(-1);
                    listCursor = 0;
                    roomByte = w.floorRoomByte();                           // *pbRam0050a620
                    for (int i = 0; i < 0x636; ++i) mapListAdd(i, m, w.floorRoomByte());   // FUN_001a27a0
                    if (selMap(m, w.floorRoomByte()) != 0) return 1;       // FUN_001a5940
                }
            }
        }
        if (floorTalkDone == 0) {
            if (selFloor() != 0) return 1;                                  // FUN_001a5ae0
            floorTalkDone = 1;
        }
        if (selPositionalList() != 0) return 1;                             // FUN_001a5c90
    }
    if (free && selHp() != 0) return 1;                                      // FUN_001a5370
    if (free && selVt() != 0) return 1;                                      // FUN_001a5650
    if (free && selLinger() != 0) return 1;                                  // FUN_001a5ff0
    if (free && facing != 0 && active == 0 && stareTimer < 0x5a) {
        stareTimer = stareTimer + 1;
        if (stareTimer == 0x5a && selStare() != 0) return 1;               // FUN_001a60c0
    }
    if (free && facing == 0 && farewell != 0 && selFarewell() != 0) return 1;   // FUN_001a6170
    if (free && selSpecialItem() != 0) return 1;                             // FUN_001a6240
    if ((frame == -1 || 0x14 < frame) && selTutorial() != 0) return 1;      // FUN_001a6300
    if (!free) return 0;
    if (idleTimer < 1) return 0;
    idleTimer = idleTimer - 1;
    if (idleTimer != 0) return 0;
    idleTimer = 0x708;
    if (active != 0) return 0;
    return selIdle() != 0 ? 1 : 0;                                           // FUN_001a6550
}

// orig FUN_001a27a0 (work/decompiled/001a27a0.c): a kind-3 talk of this map with a positional zone (c3 != -1)
void TalkManager::mapListAdd(int idx, int m, int room) {
    const TalkCondEntry e = talkCondEntry(idx);
    if (e.kind != 3) return;
    if (!(e.c1 == -1 || (m == e.c1 && (e.c2 == -1 || room == e.c2)))) return;
    if (e.c3 == -1) return;
    for (int i = 0; i < 0x80; ++i) {
        if (mapList[size_t(i)] == -1) {
            mapList[size_t(i)] = idx;
            return;
        }
    }
}

// orig FUN_001a3320 (work/decompiled/001a3320.c): attack reaction (category 1) / item shown or given (category 2)
int TalkManager::selHitItem() {
    TalkWorld& w = *talkWorld();
    int ret = 0;
    const bool c23 = isC23(speaker);
    if (!c23) {
        int cat;
        if (speaker == candidate) cat = category;
        else if (candidate != 0) cat = -100;                                // goto LAB_001a34c0
        else cat = category;
        if (cat != -100 && static_cast<uint32_t>(cat - 1) < 2u) return 0;
    }
    int kind = -1;   // iVar5
    int a = -1;      // iVar17
    int b = -1;      // iVar16
    TalkActor who = nullptr;   // iStack_30
    const int n = w.actorCount();
    for (int i = 0; i < n; ++i) {
        who = w.actorAt(i);
        TalkHitRecord recs[32];
        int count = 0;
        if (talkable(*this, w, who) && (count = w.hitRecords(who, recs)) != 0) {
            for (int r = 0; r < count; ++r) {
                const TalkHitRecord& h = recs[r];
                if (h.attacker == w.player()) {
                    kind = 1;
                    a = 1;
                    if ((h.flags & 0x10000u) == 0) a = 0;
                } else {
                    const uint32_t id = w.actorId(h.attacker);
                    if (id == kJ030) {
                        kind = 1;
                        a = 2;
                    } else if ((id & 0xffu) != 0x45 && ((id >> 8) & 0xffu) != 0x30) {
                        giftObj = w.objOfActor(h.attacker);                 // +0xfbc = attacker
                        kind = 2;
                    }
                }
                if (kind == 1) {
                    const uint32_t m = h.m1;   // hit sub-type: 1|2|4 -> 7, 2|4 -> 6, 1|4 -> 5, 1|2 -> 4, 1, 4 -> 2, 2 -> 3, 0
                    const bool b1 = (m & 1u) != 0, b2 = (m & 2u) != 0, b4 = (m & 4u) != 0;
                    if (b1 && b2 && b4) b = 7;
                    else if (b2 && b4) b = 6;
                    else if (b1 && b4) b = 5;
                    else if (b1 && b2) b = 4;
                    else if (b1) b = 1;
                    else if (b4) b = 2;
                    else if (b2) b = 3;
                    else b = 0;
                }
                if (kind != -1) break;
            }
        }
        if (kind != -1) break;
    }
    if (c23 && category == 1 && kind != -1) {
        end();
        return 0;
    }
    if (kind == -1 && w.giftTarget() != 0 && w.giftTarget() != 0) {
        TalkActor npc = actorById(w.giftTarget());                         // FUN_001a9810(mgr, FUN_0015c2c0(0x517b60))
        if (npc != nullptr) {
            TalkObj item;
            if (isC23(w.actorId(npc))) {
                if (lastDbCount == w.databaseCount()) {
                    item = w.carriedItem(npc);                              // FUN_001395c0(*(npc +0x2c) +8)
                } else {
                    TalkObj gi = w.giftItem();                              // FUN_0015c2d0(0x517b60)
                    if (gi != nullptr && w.objRef(gi).id == kGN00) item = w.carriedItem(npc);
                    else item = w.giftItem();
                }
            } else {
                item = w.carriedItem(npc);
            }
            if (item != nullptr) {
                giftObj = item;
                w.giftClear();                                              // FUN_0015c2e0(0x517b60)
                kind = 2;
                who = npc;
            }
        }
    }
    // LAB_001a39a0
    if (kind != -1 && (kind != category || (c23 && category == 2))) {
        if (kind == 2) itemTalkActor = w.actorId(who);                     // +0x28
        const int idx = search(who, kind, a, b, 0);                        // FUN_001a69e0(mgr, who, kind, a, b, 0)
        if (idx == -1) {
            speaker = 0;
        } else {
            ret = 1;
            end();
            active = 1;
            speaker = w.actorId(who);
            playerInitiated = 1;
            category = kind;
            loadAsync(idx, kind == 2 ? 1 : 0);                             // FUN_001a7660(mgr, idx, kind == 2)
            giftGlow = 0;                                                   // uRam00518090 = 0
            if (kind == 1 && speaker != 0 && isC23(speaker) && flags().test(5) && w.databaseCount() != 0) giftGlow = 1;
            if (flags().test(5) && speaker == kC24A && kind == 2 && giftObj != nullptr) {
                const uint32_t gid = w.objRef(giftObj).id;
                if ((gid & 0xffu) == 0x49 && ((gid >> 8) & 0xffu) == 0x53) w.objOrFlags70(giftObj, 0x10000u);   // "IS**"
            }
            giftObj = nullptr;
            fc0 = 0;
            dbStage = w.databaseCount() == databaseStage(flags()) ? 1 : 0;   // FUN_001dad50(0x53dc00)
        }
    }
    lastDbCount = w.databaseCount();                                        // +0x1014 = iRam0053dcc8
    return ret;
}

// orig FUN_001a2d40 (work/decompiled/001a2d40.c): approach talk (category 0), facing and the distance volume
int TalkManager::selApproach() {
    TalkWorld& w = *talkWorld();
    int ret = 0;
    TalkActor pl = w.player();
    if (pl == nullptr) return 0;
    float pp[3];
    w.actorPos(pl, pp);
    bool anyFacing = false;   // bVar6
    bool farAway = true;      // bVar7
    bool stop = false;        // bVar5
    const int n = w.actorCount();
    for (int i = 0; i < n && !stop; ++i) {
        TalkActor a = w.actorAt(i);
        if (!(w.actorType(a) == 1 && notExcludedList(w.actorId(a)))) continue;   // FUN_001a71b0
        float ap[3];
        w.actorPos(a, ap);
        const float dy = pp[1] - ap[1];
        const float dx = pp[0] - ap[0];
        const float dz = pp[2] - ap[2];
        const float d = std::sqrt(dz * dz + dx * dx + dy * dy);
        if (d < 70.0f) {
            if (0.0f < w.lingerGate()) {                                    // fRam004e62d8
                stop = true;
                lingerNpc = w.actorId(a);
                lingerTicks = lingerTicks + 1;
            } else {
                lingerTicks = 0;
                lingerNpc = 0;
            }
        }
        float dot = 0.0f;   // in_f23
        if (d < 300.0f) {
            float v[3] = {dx, dy, dz};
            normalize3(v);   // FUN_00206ab0 (y*y, x*x, z*z madd order; zero vector stays zero)
            float f[3];
            w.actorForward(a, f);
            dot = v[2] * f[2] + v[0] * f[0] + v[1] * f[1];
            // FUN_0040cc30((double)dot, 0.68) = __gtdf2 > 0: the NPC's forward points at the player
            if (!(static_cast<double>(dot) > 0.68)) {
                if (speaker == w.actorId(a)) facing = 0;
            } else {
                if (candidate != 0 && speaker != candidate && static_cast<uint32_t>(category) < 3u) end();
                const uint32_t ida = w.actorId(a);
                const uint32_t spk = speaker;
                if (!(spk == ida && category == 2) && !(spk == ida && category == 1) &&
                    !(spk == ida && category == 0) && candidate != prevCandidate && candidate != spk &&
                    excluded != ida && (playerInitiated == 0 || candidate == spk || spk == 0)) {
                    const int idx = search(a, 0, -1, -1, 0);                // FUN_001a69e0(mgr, a, 0, -1, -1, 0)
                    if (idx != -1) {
                        ret = 1;
                        end();
                        stop = true;
                        active = 1;
                        category = 0;
                        playerInitiated = 1;
                        speaker = ida;
                        loadAsync(idx, 0);
                    }
                }
            }
            if (speaker == w.actorId(a)) {
                farAway = false;
                setVolume(1.0f);                                            // FUN_001a7200(1.0, mgr)
                volume = 1.0f;
                ui::helpBar().setAlphaField(1.0f, 3, 1);                   // FUN_0015c620(1.0, 0x5180f0, 3, 1)
            }
        } else if (d < 500.0f) {
            if (speaker == w.actorId(a)) {
                facing = 0;
                farAway = false;
                if (playerInitiated != 0) {
                    float v = 1.0f;
                    const float t = 1.0f - (d - 300.0f) / 200.0f;
                    if (t <= 1.0f) v = t;
                    setVolume(v);
                    volume = v;
                    ui::helpBar().setAlphaField(v, 3, 1);
                }
            }
        } else if (500.0f <= d && speaker == w.actorId(a)) {
            farAway = true;
            outOfRange = 1;
        }
        if (d < 300.0f && static_cast<double>(dot) > 0.68) {
            prevCandidate = candidate;
            candidate = w.actorId(a);
            anyFacing = true;
            if (candidate == speaker) facing = 1;
        }
        if (d < 500.0f && speaker == w.actorId(a)) farAway = false;
    }
    if (!anyFacing) {
        facing = 0;
        candidate = 0;
    }
    if (farAway && playerInitiated != 0 && state != 0 && state != 1 && state != 2 && state != 3 && active != 0) {
        if (speaker == candidate) {
            candidate = 0;
            prevCandidate = 0;
        }
        end();
    }
    return ret;
}

// The common start of the "any speaker" selectors: end the current talk, speaker = the first talkable NPC of the row
void startFound(TalkManager& m, int idx, int cat, int field, int playerInit, int sync) {
    TalkActor a = m.firstNpcOfTalk(idx, field);
    m.end();
    m.active = 1;
    m.speaker = a == nullptr ? 0 : talkWorld()->actorId(a);
    m.playerInitiated = playerInit;
    m.category = cat;
    m.loadAsync(idx, sync);
}

// orig FUN_001a5940 (work/decompiled/001a5940.c): map arrival (category 3)
int TalkManager::selMap(int m, int room) {
    const int idx = search(nullptr, 3, m, room, 1);
    if (idx == -1) return 0;
    startFound(*this, idx, 3, 2, 0, 0);
    return 1;
}

// orig FUN_001a5ae0 (work/decompiled/001a5ae0.c): floor (category 4), a = FUN_0011c3b0
int TalkManager::selFloor() {
    const int idx = search(nullptr, 4, talkWorld()->floorNumber(), -1, 1);
    if (idx == -1) return 0;
    startFound(*this, idx, 4, 2, 0, 0);
    return 1;
}

// orig FUN_001a5c90 (work/decompiled/001a5c90.c + disasm: the list entry is passed as the 2nd argument). The cursor
// +0xfa8 is never advanced, the walk ends at the first -1 entry. PORT: bounded by the 0x80 entries.
int TalkManager::selPositionalList() {
    for (int i = 0; listCursor < 0x80 && i < 0x80; ++i) {
        if (mapList[size_t(i)] == -1) break;
        if (selPositional(mapList[size_t(i)]) != 0) return 1;
    }
    return 0;
}

// orig FUN_001a5d10 (work/decompiled/001a5d10.c): positional talk (category 5) in zone 0x4987d0[c3 - 1]
int TalkManager::selPositional(int idx) {
    TalkWorld& w = *talkWorld();
    bool entered = false;
    TalkActor pl = w.player();
    if (pl == nullptr) return 0;
    if (zone == idx || zone == -1) {
        const int room = w.actorRoom(pl);
        if (room >= 0) {
            const TalkCondEntry e = talkCondEntry(idx);
            if (e.c3 < 1 || e.c3 > 9) return 0;                             // PORT bound (table 0x4987d0 has 9 zones)
            const float* z = kZones[e.c3 - 1];                              // 0x4987d0 + (c3 - 1) * 0x10
            float p[3];
            w.actorPos(pl, p);
            const float ox = w.roomOrigin(room, 0);                         // FUN_0011e6c0
            const float oz = w.roomOrigin(room, 2);                         // FUN_0011e740
            const float fz = (p[2] - oz) - z[2];
            const float fx = (p[0] - ox) - z[0];
            if (std::sqrt(fx * fx + fz * fz) < z[3]) {
                if (zone == -1) entered = true;
                zone = idx;
            } else {
                zone = -1;
            }
        }
    }
    if (!entered) return 0;
    if (condition(idx, 5, -1, -1) == 0) {                                  // FUN_001a6d00(mgr, idx, 5, -1, -1)
        zone = -1;
        return 0;
    }
    startFound(*this, idx, 5, 2, 0, 0);
    return 1;
}

// orig FUN_001a5370 (work/decompiled/001a5370.c): low HP (category 6) and the HP tutorial flags
int TalkManager::selHp() {
    TalkWorld& w = *talkWorld();
    int ret = 0;
    TalkActor pl = w.player();
    if (pl == nullptr) return 0;
    const int hp = w.actorHp(pl);
    const int hpMax = w.actorHpMax(pl);
    int last = lastHp;
    bool skipCross = false;
    if (last != 0 && hp == 0) {
        hpFlag(0);                                                          // FUN_001aa120(mgr, 0)
        last = hpMax;
        skipCross = true;                                                   // goto LAB_001a5498
    }
    if (!skipCross) {
        if (static_cast<float>(last) < static_cast<float>(hpMax) * 0.3f || static_cast<float>(hpMax) * 0.3f <= static_cast<float>(hp)) {
            if (static_cast<float>(hpMax) * 0.2f <= static_cast<float>(last) && static_cast<float>(hp) < static_cast<float>(hpMax) * 0.2f)
                hpFlag(1);
        } else {
            hpFlag(2);
        }
        last = hpMax;
    }
    // LAB_001a5498 (`last` now holds the maximum, iVar12 = *piVar1)
    if (static_cast<float>(last) * 0.1f <= static_cast<float>(lastHp) && static_cast<float>(hp) < static_cast<float>(last) * 0.1f) {
        const int idx = search(nullptr, 6, -1, -1, 1);
        if (idx != -1) {
            ret = 1;
            startFound(*this, idx, 6, 2, 0, 0);
        }
    }
    lastHp = hp;
    return ret;
}

// orig FUN_001a5650 (work/decompiled/001a5650.c): low VT (category 7) and the VT tutorial flags. The speaker loop
// compares the actors with the row's voice-number string (+4, disasm 0x1a5828: 0x457020 + 4), so no NPC matches and
// the speaker is 0 (original behaviour kept).
int TalkManager::selVt() {
    TalkWorld& w = *talkWorld();
    int ret = 0;
    TalkActor pl = w.player();
    if (pl == nullptr) return 0;
    const int vt = w.actorVt(pl);
    const int vtMax = w.actorVtMax(pl);
    if (lastVt != 0 && vt == 0) vtFlag(0);                                  // FUN_001aa070(mgr, 0)
    if (static_cast<float>(lastVt) < static_cast<float>(vtMax) * 0.3f || static_cast<float>(vtMax) * 0.3f <= static_cast<float>(vt)) {
        if (static_cast<float>(vtMax) * 0.15f <= static_cast<float>(lastVt) && static_cast<float>(vt) < static_cast<float>(vtMax) * 0.15f)
            vtFlag(2);
    } else {
        vtFlag(3);
    }
    if (static_cast<float>(vtMax) * 0.1f <= static_cast<float>(lastVt) && static_cast<float>(vt) < static_cast<float>(vtMax) * 0.1f) {
        vtFlag(1);
        const int idx = search(nullptr, 7, -1, -1, 1);
        if (idx != -1) {
            ret = 1;
            startFound(*this, idx, 7, 1, 0, 0);
        }
    }
    lastVt = vt;
    return ret;
}

// orig FUN_001a5ff0 (work/decompiled/001a5ff0.c): standing next to an NPC (category 8); FUN_001a9810(+0xfb8) (disasm)
int TalkManager::selLinger() {
    bool r = false;
    if (lingerNpc == 0) return 0;
    if (lingerTicks < 0x3d) return 0;
    TalkActor a = actorById(lingerNpc);
    const int idx = search(a, 8, -1, -1, 0);
    if (idx == -1) {
        lingerTicks = 0;
    } else {
        r = lingerNpc != 0;
        if (r) {
            end();
            active = 1;
            playerInitiated = 1;
            category = 8;
            speaker = lingerNpc;
            loadAsync(idx, 0);
        }
        lingerTicks = 0;
    }
    lingerNpc = 0;
    return r ? 1 : 0;
}

// orig FUN_001a60c0 (work/decompiled/001a60c0.c): stare (category 9) at the candidate
int TalkManager::selStare() {
    TalkActor a = actorById(candidate);
    const int idx = search(a, 9, -1, -1, 0);
    if (idx == -1) return 0;
    const bool r = speaker != 0;
    if (r) {
        end();
        active = 1;
        playerInitiated = 1;
        category = 9;
        loadAsync(idx, 0);
    }
    return r ? 1 : 0;
}

// orig FUN_001a6170 (work/decompiled/001a6170.c): farewell (category 10) after a finished approach talk
int TalkManager::selFarewell() {
    bool r = false;
    TalkActor a = actorById(farewell);
    const int idx = search(a, 10, -1, -1, 0);
    if (idx == -1) {
        speaker = 0;
    } else {
        r = farewell != 0;
        if (r) {
            end();
            active = 1;
            playerInitiated = 0;
            category = 10;
            speaker = farewell;
            loadAsync(idx, 0);
        } else {
            farewell = 0;
        }
        speaker = 0;
    }
    farewell = 0;
    return r ? 1 : 0;
}

// orig FUN_001a6240 (work/decompiled/001a6240.c): special item (category 12) in map 0x20 of map set 2
int TalkManager::selSpecialItem() {
    int r = 0;
    if (specialItem == nullptr) return 0;
    if (roomByte == 2) {
        if (mapId == 0x20) {
            const int idx = search(nullptr, 0xc, -1, -1, 1);
            if (idx != -1) {
                r = 1;
                end();
                active = 1;
                playerInitiated = 0;
                category = 0xc;
                loadAsync(idx, 1);                                          // $a2 = 1 (disasm 0x1a62b8)
            }
        }
        specialItem = nullptr;
    } else {
        specialItem = nullptr;
    }
    fb0 = 0;
    return r;
}

// orig FUN_001a6300 (work/decompiled/001a6300.c): tutorial / scheduled talk (category 13)
int TalkManager::selTutorial() {
    int r = 0;
    if (!talkTutorialGate()) return 0;                                      // FUN_001a6420
    tutorialCounter = tutorialCounter + 1;
    if (tutorialCounter == 0x1518) idleFlag();                              // FUN_001aa030
    int idx = -1;
    if (scheduledCount != -1) {
        scheduledCount = scheduledCount - 1;
        if (scheduledCount == 0) idx = indexOfName(scheduled);             // FUN_001a97a0(mgr, +0xfcc)
    }
    if (idx == -1) idx = search(nullptr, 0xd, -1, -1, 1);
    if (idx != -1) {
        r = 1;
        end();
        active = 1;
        playerInitiated = 0;
        category = 0xd;
        loadAsync(idx, 0);
    }
    for (uint32_t f = 0xe3d; f < 0xe59; ++f) flags().clear(f);             // FUN_00178a30 0xe3d .. 0xe58
    return r;
}

// orig FUN_001a6550 (work/decompiled/001a6550.c): idle talk (category 15), a = floor number
int TalkManager::selIdle() {
    if (active != 0) return 0;
    const int idx = search(nullptr, 0xf, talkWorld()->floorNumber(), -1, 1);
    if (idx == -1) return 0;
    startFound(*this, idx, 0xf, 2, 0, 0);
    return 1;
}

}  // namespace baroque::game
