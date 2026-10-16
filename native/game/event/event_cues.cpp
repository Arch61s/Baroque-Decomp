// EVT cue dispatcher FUN_00175e30 and every opcode handler (EVENTS_EFFECTS_PROGRESSION.md 2.4, corrections in
// work/reports/EVENTS_NATIVE.md), plus the per-tick jobs FUN_001769d0 / FUN_00177050 / FUN_00177360 / FUN_00177210.
// Operands are read exactly where each handler reads them (pc +0x20964 advances like the original, including the
// ignored words of ops 47, 75, 85, 120/123, 132/133).
#include "game/event/event_manager.hpp"

#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/event/event_services.hpp"
#include "game/event/event_sound.hpp"   // PORT: voice start log (test runs)
#include "game/flag_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/item.hpp"
#include "game/items/item_world.hpp"
#include "game/player_move.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "platform/app_host.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace baroque::game {

// game/objects/objects.hpp (not included: its rotateAbout* overloads clash with this file's local FUN_00128c00 copies)
void itemEntitySyncFromChr(Chr& c);

namespace {

constexpr int kWordsPerBlock = 0x104 / 4;

// word idx of the current block; reads past the 64 command words continue into the following blocks like the
// original's flat array (+0x6ad8 + cue*0x104 + idx*4)
int32_t cw(const EventManager& m, int idx) {
    const size_t flat = static_cast<size_t>(m.cueIndex) * kWordsPerBlock + static_cast<size_t>(idx);
    const size_t blk = flat / kWordsPerBlock;
    const size_t off = flat % kWordsPerBlock;
    if (blk >= m.cues.size()) return 0;
    const EvtCueBlock& b = m.cues[blk];
    return off < 64 ? b.words[off] : b.frame;
}
int32_t rd(EventManager& m) { return cw(m, m.pc++); }   // the "iVar = pc; pc = iVar + 1; w[iVar]" idiom

bool filterOk(const EventManager& m, int counter) { return m.instanceFilter == -1 || m.instanceFilter == counter; }
bool removed(const Chr* c) { return (c->taskFlags & 0x10000000u) != 0; }
uint32_t utype(const Chr* c) { return static_cast<uint32_t>(c->type); }

std::string name8(int32_t w0, int32_t w1) {
    char b[9] = {};
    std::memcpy(b, &w0, 4);
    std::memcpy(b + 4, &w1, 4);
    return std::string(b);   // NUL-terminated within the 8 bytes in every shipped file
}

// FUN_0011e6c0 / FUN_0011e740 consumers: map offset of the event's room
float ox(const EventManager& m) { return m.mapOfs[0]; }
float oy(const EventManager& m) { return m.mapOfs[1]; }
float oz(const EventManager& m) { return m.mapOfs[2]; }

// orig FUN_0039ddd0 used by FUN_00128c00 / FUN_001289c0 (see event_manager.cpp)
void axisAngle(float angle, const float axis[3], float m[3][3]) {
    const float s = std::sin(angle);
    const float c = std::cos(angle);
    const float omc = 1.0f - c;
    const float zero = 0.0f;
    float t = omc * axis[0];
    m[0][0] = (zero + c) + t * axis[0];
    m[0][1] = (zero + s * axis[2]) + t * axis[1];
    m[0][2] = t * axis[2] - s * axis[1];
    t = omc * axis[1];
    m[1][0] = t * axis[0] - s * axis[2];
    m[1][1] = (zero + c) + t * axis[1];
    m[1][2] = (zero + s * axis[0]) + t * axis[2];
    t = omc * axis[2];
    m[2][0] = (zero + s * axis[1]) + t * axis[0];
    m[2][1] = t * axis[1] - s * axis[0];
    m[2][2] = (zero + c) + t * axis[2];
}
void xform(Vec3f& v, const float m[3][3]) {
    const float x = v[0], y = v[1], z = v[2];
    v[0] = y * m[1][0] + x * m[0][0] + z * m[2][0];
    v[1] = y * m[1][1] + x * m[0][1] + z * m[2][1];
    v[2] = y * m[1][2] + x * m[0][2] + z * m[2][2];
}
// orig FUN_00128c00 (work/decompiled/00128c00.c): forward about the right axis, then up = fwd x right, fwd = right x up
void rotateAboutRight(ChrTransform& t, float rad) {
    float m[3][3];
    const float ax[3] = {t.right[0], t.right[1], t.right[2]};
    axisAngle(rad, ax, m);
    xform(t.fwd, m);
    normalize3(t.fwd.data());
    t.up[0] = t.fwd[1] * t.right[2] - t.fwd[2] * t.right[1];
    t.up[1] = t.fwd[2] * t.right[0] - t.fwd[0] * t.right[2];
    t.up[2] = t.fwd[0] * t.right[1] - t.fwd[1] * t.right[0];
    normalize3(t.up.data());
    t.fwd[0] = t.right[1] * t.up[2] - t.right[2] * t.up[1];
    t.fwd[1] = t.right[2] * t.up[0] - t.right[0] * t.up[2];
    t.fwd[2] = t.right[0] * t.up[1] - t.right[1] * t.up[0];
    normalize3(t.fwd.data());
}
// orig FUN_001289c0 (work/decompiled/001289c0.c): up about the forward axis, rebuild right and up
void rotateAboutForward(ChrTransform& t, float rad) {
    float m[3][3];
    const float ax[3] = {t.fwd[0], t.fwd[1], t.fwd[2]};
    axisAngle(rad, ax, m);
    xform(t.up, m);
    normalize3(t.up.data());
    t.right[0] = t.up[1] * t.fwd[2] - t.up[2] * t.fwd[1];
    t.right[1] = t.up[2] * t.fwd[0] - t.up[0] * t.fwd[2];
    t.right[2] = t.up[0] * t.fwd[1] - t.up[1] * t.fwd[0];
    normalize3(t.right.data());
    t.up[0] = t.fwd[1] * t.right[2] - t.fwd[2] * t.right[1];
    t.up[1] = t.fwd[2] * t.right[0] - t.fwd[0] * t.right[2];
    t.up[2] = t.fwd[0] * t.right[1] - t.fwd[1] * t.right[0];
    normalize3(t.up.data());
}

EvtActorJob* findJob(EventManager& m, Chr* c) {
    for (EvtActorJob& j : m.jobs)
        if (j.actor == c) return &j;
    return nullptr;
}
EvtActorJob* freeJob(EventManager& m) {
    for (EvtActorJob& j : m.jobs)
        if (j.actor == nullptr) return &j;
    return nullptr;
}

// orig FUN_00177600 (work/decompiled/00177600.c)
void jobMove(EventManager& m, Chr* c, int32_t id, int frames, const float vel[3], const float acc[3], int* counter) {
    if (static_cast<uint32_t>(id) != c->id) return;
    if (filterOk(m, *counter)) {
        EvtActorJob* j = findJob(m, c);
        if (j == nullptr) {
            j = freeJob(m);
            if (j != nullptr) j->actor = c;
        }
        if (j != nullptr) {
            j->moveFrames = frames;
            for (int k = 0; k < 3; ++k) {
                j->vel[k] = vel[k];
                j->acc[k] = acc[k];
            }
        }
    }
    *counter += 1;
}
// orig FUN_00177880 (work/decompiled/00177880.c)
void jobRotate(EventManager& m, float deg, Chr* c, int32_t id, int frames, int axis, int* counter) {
    if (static_cast<uint32_t>(id) != c->id) return;
    if (filterOk(m, *counter)) {
        EvtActorJob* j = findJob(m, c);
        if (j == nullptr) {
            j = freeJob(m);
            if (j != nullptr) j->actor = c;
        }
        if (j != nullptr && axis >= 0 && axis < 3) {
            j->rotFrames[axis] = frames;
            j->rotSpeed[axis] = deg;
        }
    }
    *counter += 1;
}
// orig FUN_00177a90 (work/decompiled/00177a90.c)
void jobFall(EventManager& m, float targetY, Chr* c, int32_t id, int frames, int* counter) {
    if (static_cast<uint32_t>(id) != c->id) return;
    if (filterOk(m, *counter)) {
        EvtActorJob* j = findJob(m, c);
        if (j == nullptr) {
            j = freeJob(m);
            if (j != nullptr) j->actor = c;
        }
        if (j != nullptr) {
            j->fallFrames = frames;
            j->hasTarget = 1;
            j->target[0] = c->xf.m.pos[0];
            j->target[1] = targetY;
            j->target[2] = c->xf.m.pos[2];
        }
    }
    *counter += 1;
}
// PORT: the item entity keeps its transform in the Item (item_entity.cpp); an event write on a type-3 record's
// transform (the item's own transform +0x20 in the original) is mirrored into it so the item tick does not undo it
void syncItemRecord(Chr* c) {
    if (utype(c) == 3u && c->item != nullptr) itemEntitySyncFromChr(*c);
}

// FUN_001769d0's FUN_00109d40(model, target, transform scale, fwd, up, right) on the job actor, behind vt+0x20: items
// (class 0x4cd960, vt+0x20 = FUN_0012a5d0 -> 1 [V ELF vtable + disasm]) publish their holder +0x2c, characters
// (type < 3, vt+0x20 = FUN_0012d400 = +0x24 bit 0) the first node of +0x2c; type 4/5 are not published.
// FUN_00109d40 acts only on a loaded holder (+0x2c bit 31) and starts with FUN_0010a160, the material fader step
// (the fades of ops 112 / 113 advance through it). The character's own transform is untouched; the port's publish
// override is the jitter slot (an item record's is re-derived from Item::modelXf by its tick, so that is written too).
void publishAtTarget(Chr* c, const float target[3]) {
    const uint32_t ty = utype(c);
    if (ty > 3u || (ty < 3u && !c->active())) return;
    if (!c->model.loaded()) return;
    modelPublishFx(c->model);                                // FUN_0010a160
    c->jitter = true;
    c->jitterPos = {target[0], target[1], target[2]};
    if (ty == 3u && c->item != nullptr) {
        Item::ModelXf& m = c->item->modelXf;
        m.pos = Vec3{target[0], target[1], target[2]};
        m.right = Vec3{c->xf.m.right[0], c->xf.m.right[1], c->xf.m.right[2]};
        m.up = Vec3{c->xf.m.up[0], c->xf.m.up[1], c->xf.m.up[2]};
        m.fwd = Vec3{c->xf.m.fwd[0], c->xf.m.fwd[1], c->xf.m.fwd[2]};
        m.scale = c->xf.scale[0];
        m.valid = true;
    }
}

// orig FUN_00177500 (work/decompiled/00177500.c)
void setPosition(EventManager& m, float x, float y, float z, Chr* c, int32_t id, int* counter) {
    if (static_cast<uint32_t>(id) != c->id) return;
    if (filterOk(m, *counter)) {
        c->xf.m.pos = {x, y, z};
        syncItemRecord(c);
        for (EvtActorJob& j : m.jobs) {
            if (j.actor == c && j.hasTarget == 1) {
                j.target[0] = x;
                j.target[1] = y;
                j.target[2] = z;
                break;
            }
        }
    }
    *counter += 1;
}
// orig FUN_00177420 (work/decompiled/00177420.c)
void removeActor(EventManager& m, Chr* c, int32_t id, int* counter, int unload) {
    if (static_cast<uint32_t>(id) != c->id) return;
    if (filterOk(m, *counter)) {
        for (EvtActorJob& j : m.jobs)
            if (j.actor == c) j.actor = nullptr;
        // unload != 0: FUN_0012b7f0(id) drops the model from the load cache (the port loads per character)
        (void)unload;
        c->taskFlags |= 0x10000000u;                         // FUN_00207350
    }
    *counter += 1;
}

// anim lookup of FUN_00170480: strcmp over the actor's igAnimation list
int findAnim(const Chr& c, const std::string& name) {
    for (size_t i = 0; i < c.model.clips.size(); ++i)
        if (c.model.clips[i]->name == name) return static_cast<int>(i);
    return -1;
}

void effectRot0(float r[4]) { r[0] = r[1] = r[2] = 0.0f; r[3] = 1.0f; }
void effectScale100(float s[4]) { s[0] = s[1] = s[2] = 100.0f; s[3] = 1.0f; }

}  // namespace

// ================================================================================================= handlers
namespace {

// orig FUN_0016f690 (work/decompiled/0016f690.c): op 27 text line into a text window slot
void opShowText(EventManager& m) {
    m.pc += 1;
    MessageLog& log = messageLog();
    if (log.active == 0) log.active = 1;                     // iRam005180f0
    const uint32_t line = static_cast<uint32_t>(rd(m));
    char buf[0x108] = {};
    int src = 0, dst = 0;
    uint32_t seps = 0;
    do {
        const int nx = src + 1;
        if (dst < 0x107) buf[dst] = m.text[src];
        ++dst;
        int after = nx;
        if (nx < 0x800 && m.text[nx] == ';') {
            seps += 1;
            if (seps <= line) {
                std::memset(buf, 0, 0x100);
                dst = 0;
                after = src + 2;
            }
        }
        src = after;
        if (dst < 0x108) buf[dst] = 0;
        if (src >= 0x7ff) break;                             // PORT: the original reads on past the text buffer
    } while (seps <= line);
    const int slot = rd(m);
    float x = static_cast<float>(rd(m));
    float y = static_cast<float>(rd(m));
    float w = static_cast<float>(rd(m));
    const int h = rd(m);
    ui::TextRenderer& t = ui::text();
    if (x <= -999.0f) {
        if (ui::language() == 1) {
            t.halfW = 8.56f;
            t.halfH = 9.63f;
        } else {
            t.halfW = 1.16f * 8.0f;                          // DAT_0044c180
            t.halfH = 1.22f * 9.0f;                          // DAT_0044c188
        }
        const float width = t.measure("%s", buf);            // FUN_00113cf0(0x4e7e90, buf)
        ui::centreOnScreen(width, 0.0f, &x, &y);             // FUN_00167250
        w = 512.0f;
    }
    ui::HelpBar& hb = ui::helpBar();
    hb.set(x, y, w, static_cast<float>(h), slot, "%s", buf);   // FUN_0015c490(.., buf as the format)
    if (ui::language() == 1) hb.setScale(slot, 1.07f, 1.07f);  // 0x3f88f5c3
    else hb.setScale(slot, 1.16f, 1.22f);
    hb.setColor(slot, ui::language() == 1 ? 0x7f7f7fu : 0xbbbbbbu);   // 0x44d110[lang]
}

// orig FUN_0016fe70 (work/decompiled/0016fe70.c): ops 30 / 48 spawn %c%c%c%c
void opSpawn(EventManager& m) {
    m.pc += 1;
    const int32_t id = rd(m);
    const int32_t x = rd(m);
    const int32_t y = rd(m);
    const int32_t z = rd(m);
    char name[5] = {static_cast<char>(id & 0xff), static_cast<char>((id >> 8) & 0xff),
                    static_cast<char>((id >> 16) & 0xff), static_cast<char>((id >> 24) & 0xff), 0};   // 0x44d168
    Chr* c = characters().create(name, true);                // FUN_0012d2f0(0x50b1a0, name, 1)
    if (c == nullptr) {
        // FUN_0012ce70 returns 0 for a tag outside the factory table 0x44aa10 (103 entries; e.g. E11's "J026", a
        // map-model tag that only FUN_00120f70 places): the original skips the spawn silently as well.
        platform::log(std::string("event: spawn ") + name + " returned null (FUN_0012ce70)");
        return;
    }
    if (utype(c) < 3u) c->f28 = 0x40000011;
    c->xf.m.pos = {static_cast<float>(x) + ox(m), static_cast<float>(y) + oy(m), static_cast<float>(z) + oz(m)};
    c->prevPos = c->xf.m.pos;   // PORT: the port's collision sweep starts from prevPos (not a field of the original)
    if (utype(c) == 3u && c->item != nullptr) {
        // PORT: the item entity keeps its transform in the Item; the record's position write is mirrored into it
        itemEntitySyncFromChr(*c);
        c->item->moveVel = Vec3{};                            // body +0x30 -> +0x7c / +0x80 / +0x84 = 0
        itemSystem().game->itemEnterWorld(*c->item);          // FUN_001d3a30(obj)
    }
}


}  // namespace

// orig FUN_00175e30 (work/decompiled/00175e30.c)
bool EventManager::runCues(bool force) {
    EventHooks& H = eventHooks();
    if (cues[static_cast<size_t>(cueIndex)].frame == frame || force) {
        int32_t op = cw(*this, pc);
        while (op != 0) {
            switch (op) {
            case 0x1b: opShowText(*this); break;
            case 0x1c: {   // FUN_0016fab0
                pc += 1;
                ui::helpBar().clear(rd(*this));
                break;
            }
            case 0x6e: {   // FUN_0016fb10: typewriter speed (0x5181bc[slot])
                pc += 1;
                const int slot = rd(*this);
                ui::helpBar().setSpeed(slot, rd(*this));
                break;
            }
            case 0x6f: {   // FUN_0016fbc0 -> FUN_0015c620
                pc += 1;
                const int slot = rd(*this);
                const int v = rd(*this);
                const int isAlpha = rd(*this);
                ui::helpBar().setAlphaField(static_cast<float>(v) / 100.0f, slot, isAlpha);
                break;
            }
            case 0x1d: {   // FUN_0016fcb0: EXI effect at a map position
                pc += 1;
                const int32_t eff = rd(*this);
                float pos[4];
                pos[0] = static_cast<float>(rd(*this)) + ox(*this);
                pos[1] = static_cast<float>(rd(*this)) + oy(*this);
                pos[2] = static_cast<float>(rd(*this)) + oz(*this);
                pos[3] = 1.0f;
                float rot[4], scl[4];
                effectRot0(rot);
                effectScale100(scl);
                if (H.effectSpawn) H.effectSpawn(0.0f, eff, pos, rot, scl);   // FUN_00112190
                break;
            }
            case 0x1e:
            case 0x30: opSpawn(*this); break;
            case 0x1f: {   // FUN_00170060
                pc += 1;
                const int32_t id = rd(*this);
                const int unload = rd(*this);
                int counter = 0;
                for (Chr* c : std::vector<Chr*>(characters().list())) {
                    if (removed(c)) continue;
                    if (utype(c) <= 4u) removeActor(*this, c, id, &counter, unload);
                }
                break;
            }
            case 0x96: {   // FUN_00170210: ACT action by name (FUN_00143a40)
                pc += 1;
                const int32_t id = rd(*this);
                const int32_t w0 = rd(*this);
                const int32_t w1 = rd(*this);
                const std::string act = name8(w0, w1);
                int counter = 0;
                for (Chr* c : characters().list()) {
                    if (removed(c) || utype(c) >= 3u || static_cast<uint32_t>(id) != c->id) continue;
                    if (filterOk(*this, counter) && c->actLoaded) c->act.start(act);   // FUN_00143aa0; PORT: actLoaded guard (the original always calls FUN_00143a40)
                    ++counter;
                }
                break;
            }
            case 0x20:
            case 0x69: {   // FUN_00170480 (body) / FUN_00170a90 (sub-object vt+0x28)
                pc += 1;
                const int32_t id = rd(*this);
                const int32_t w0 = rd(*this);
                const int32_t w1 = rd(*this);
                const float speed = static_cast<float>(static_cast<uint32_t>(rd(*this))) / 10.0f;   // $f13
                const float blend = static_cast<float>(static_cast<uint32_t>(rd(*this))) / 10.0f;   // $f12
                const std::string anim = name8(w0, w1);
                int counter = 0;
                for (Chr* c : characters().list()) {
                    if (removed(c) || utype(c) >= 3u || static_cast<uint32_t>(id) != c->id) continue;
                    if (filterOk(*this, counter)) {
                        if (op == 0x20) {
                            const int idx = findAnim(*c, anim);
                            // UNVERIFIED: a name that is not in the list passes a null igAnimation to FUN_0010a8e0
                            if (idx >= 0) c->model.anim.change(blend, speed, idx, 0);
                            else platform::log("event: " + c->idString() + " has no animation " + anim);
                        } else if (!chrSubActorAnim(*c, anim.c_str(), blend, speed) && H.subObjectAnim) {
                            H.subObjectAnim(*c, anim.c_str(), blend, speed);   // classes without a sub-actor: vt+0x28 = 0
                        }
                    }
                    ++counter;
                }
                break;
            }
            case 0x21: {   // FUN_00170870: animation speed FUN_0010ad90
                pc += 1;
                const int32_t id = rd(*this);
                const float v = static_cast<float>(static_cast<uint32_t>(rd(*this))) / 10.0f;
                int counter = 0;
                for (Chr* c : characters().list()) {
                    if (removed(c) || utype(c) >= 3u || static_cast<uint32_t>(id) != c->id) continue;
                    if (filterOk(*this, counter)) c->model.anim.setSpeed(v);
                    ++counter;
                }
                break;
            }
            case 0x22: {   // FUN_00170ea0
                pc += 1;
                const int32_t id = rd(*this);
                const int32_t x = rd(*this);
                const int32_t y = rd(*this);
                const int32_t z = rd(*this);
                const float fx = static_cast<float>(x) + ox(*this);
                const float fy = static_cast<float>(y) + oy(*this);
                const float fz = static_cast<float>(z) + oz(*this);
                int counter = 0;
                for (Chr* c : characters().list())
                    if (!removed(c) && utype(c) <= 4u) setPosition(*this, fx, fy, fz, c, id, &counter);
                break;
            }
            case 0x23:
            case 0x32: {   // FUN_00171100
                const int32_t which = cw(*this, pc);
                pc += 1;
                const int32_t id = rd(*this);
                const int32_t frames = rd(*this);
                const int32_t vx = rd(*this);
                const int32_t vy = rd(*this);
                const int32_t vz = rd(*this);
                float acc[3] = {0.0f, 0.0f, 0.0f};
                if (which == 0x32) {
                    acc[0] = static_cast<float>(rd(*this)) / 1000.0f;
                    acc[1] = static_cast<float>(rd(*this)) / 1000.0f;
                    acc[2] = static_cast<float>(rd(*this)) / 1000.0f;
                }
                const float vel[3] = {static_cast<float>(vx) / 10.0f, static_cast<float>(vy) / 10.0f,
                                      static_cast<float>(vz) / 10.0f};
                int counter = 0;
                for (Chr* c : characters().list())
                    if (!removed(c) && utype(c) <= 4u) jobMove(*this, c, id, frames, vel, acc, &counter);
                break;
            }
            case 0x24: {   // FUN_00171510 -> FUN_001777f0 -> FUN_001286c0 (identity axes)
                pc += 1;
                const int32_t id = rd(*this);
                int counter = 0;
                for (Chr* c : characters().list()) {
                    if (removed(c) || utype(c) > 4u || static_cast<uint32_t>(id) != c->id) continue;
                    if (filterOk(*this, counter)) {
                        resetAxes(c->xf.m);
                        syncItemRecord(c);                   // PORT
                    }
                    ++counter;
                }
                break;
            }
            case 0x25:
            case 100:
            case 0x65: {   // FUN_00171670 (axis 1) / FUN_00171860 (axis 0) / FUN_00171a50 (axis 2)
                const int axis = op == 0x25 ? 1 : (op == 100 ? 0 : 2);
                pc += 1;
                const int32_t id = rd(*this);
                const int32_t frames = rd(*this);
                const int32_t deg = rd(*this);
                int counter = 0;
                for (Chr* c : characters().list())
                    if (!removed(c) && utype(c) <= 4u) jobRotate(*this, static_cast<float>(deg), c, id, frames, axis, &counter);
                break;
            }
            case 0x26: {   // FUN_00171c40
                pc += 1;
                const int32_t id = rd(*this);
                const int32_t frames = rd(*this);
                const int32_t y = rd(*this);
                int counter = 0;
                for (Chr* c : characters().list())
                    if (!removed(c) && utype(c) <= 3u) jobFall(*this, static_cast<float>(y), c, id, frames, &counter);
                break;
            }
            case 0x33: {   // FUN_00171e20: scale (transform +0)
                pc += 1;
                const int32_t id = rd(*this);
                const int32_t sx = rd(*this);
                const int32_t sy = rd(*this);
                const int32_t sz = rd(*this);
                int counter = 0;
                for (Chr* c : characters().list()) {
                    if (removed(c) || utype(c) > 4u || static_cast<uint32_t>(id) != c->id) continue;
                    if (filterOk(*this, counter))
                        c->xf.scale = {static_cast<float>(sx) / 10.0f, static_cast<float>(sy) / 10.0f, static_cast<float>(sz) / 10.0f};
                    ++counter;
                }
                break;
            }
            case 0x34:
            case 0x35: {   // FUN_00175ae0 / FUN_00175ca0: type-3 item entities of the registry 0x50b134 with +0x1c == id
                pc += 1;
                const int32_t id = rd(*this);
                int counter = 0;
                for (Chr* c : std::vector<Chr*>(characters().list())) {
                    if (removed(c) || utype(c) != 3u || static_cast<uint32_t>(id) != c->id) continue;
                    if (filterOk(*this, counter) && c->item != nullptr) {
                        // op 0x34: FUN_001d5410(-1.0, obj, 0) = drop at its own position with a random yaw
                        if (op == 0x34) dropItemAtActor(*c->item, -1.0f, nullptr, itemSystem());
                        c->item->flags |= kItemIdentified;   // obj +0x70 |= 1
                    }
                    ++counter;
                }
                break;
            }
            case 0x70: {   // FUN_00172090: material fade FUN_0010a0a0
                pc += 1;
                const int32_t id = rd(*this);
                const int32_t mode = rd(*this);
                const int32_t frames = rd(*this);
                float a[4], b[4];
                for (float& v : a) v = static_cast<float>(rd(*this)) / 10.0f;
                for (float& v : b) v = static_cast<float>(rd(*this)) / 10.0f;
                int fm = 1;
                if (mode == 1) fm = 0x11;
                else if (mode != 2) {
                    if (mode == 3) fm = 0x10;
                    else {
                        fm = 2;
                        if (mode != 4) {
                            if (mode == 5) fm = 3;
                            else if (mode == 6) fm = 0x13;
                            else if (mode == 7) fm = 2;
                            else {
                                fm = 0;
                                if (mode != 8 && mode == 9) fm = 0x10;
                            }
                        }
                    }
                }
                int counter = 0;
                for (Chr* c : characters().list()) {
                    if (removed(c) || utype(c) > 4u || static_cast<uint32_t>(id) != c->id) continue;
                    if (filterOk(*this, counter)) modelFadeStart(c->model, static_cast<uint32_t>(fm), frames, a, b);   // FUN_0010a0a0
                    ++counter;
                }
                break;
            }
            case 0x71: {   // FUN_00172700: FUN_0010a130
                pc += 1;
                const int32_t id = rd(*this);
                int counter = 0;
                for (Chr* c : characters().list()) {
                    if (removed(c) || utype(c) > 4u || static_cast<uint32_t>(id) != c->id) continue;
                    if (filterOk(*this, counter)) modelFadeEnd(c->model);   // FUN_0010a130
                    ++counter;
                }
                break;
            }
            case 0x27: {   // FUN_00172920
                pc += 1;
                quadStep = -rd(*this);
                break;
            }
            case 0x28: {   // FUN_00172990
                pc += 1;
                quadStep = rd(*this);
                const uint32_t color = static_cast<uint32_t>(rd(*this));
                if (colorQuad == nullptr) {
                    colorQuad = ui::sprites().create(-5.0f, -5.0f, 2.0f, 522.0f, 458.0f, 0, 0, 0, 0, 0x129, 0);
                    if (colorQuad != nullptr) colorQuad->color = color;
                }
                break;
            }
            case 0x29: {   // FUN_00172ad0
                pc += 1;
                instanceFilter = rd(*this);
                break;
            }
            case 0xbd: {   // FUN_00172b40
                pc += 1;
                const int32_t id = rd(*this);
                instanceFilter = -1;
                for (Chr* c : characters().list())
                    if (!removed(c) && utype(c) <= 4u && static_cast<uint32_t>(id) == c->id) instanceFilter += 1;
                const int32_t k = rd(*this);
                if (instanceFilter < 0) instanceFilter = 0;
                else if (0 < instanceFilter) instanceFilter = instanceFilter - k;
                break;
            }
            case 0x2a: {   // FUN_00172d40: event costume (FUN_001d7dc0(0x53d910, v)), once
                pc += 1;
                const int32_t v = rd(*this);
                if (equipOverride == 0) {
                    eventEquipOverride(v);
                    equipOverride = 1;
                }
                break;
            }
            case 0x2b: {   // FUN_00172de0 -> FUN_00177c40 -> FUN_001d8060
                pc += 1;
                if (equipOverride != 0) {
                    eventEquipRestore();
                    equipOverride = 0;
                }
                break;
            }
            case 0x2c: {   // FUN_00172e40: fog (0,0 restores the saved range)
                pc += 1;
                float s = static_cast<float>(rd(*this));
                float e = static_cast<float>(rd(*this));
                if (s == 0.0f && e == 0.0f) {
                    s = savedFog[0];
                    e = savedFog[1];
                }
                if (H.setFog) H.setFog(s, e);
                break;
            }
            case 0x2d: {   // FUN_00172f20: FUN_00101b60(far = v or saved, fov 55)
                pc += 1;
                float f = static_cast<float>(rd(*this));
                if (f == 0.0f) f = savedFar;
                if (H.setProjection) H.setProjection(f, 55.0f);   // 0x425c0000
                break;
            }
            case 0x2e: {   // FUN_00172fb0: FUN_00101b60(saved far, fov = v/10 or saved)
                pc += 1;
                float f = static_cast<float>(rd(*this));
                if (f == 0.0f) f = savedFov;
                else f = f / 10.0f;
                if (H.setProjection) H.setProjection(savedFar, f);
                break;
            }
            case 0x2f: {   // FUN_00173050: FUN_00177cc0(10.0, mgr, frames); the second word is skipped
                pc += 1;
                const int32_t frames = rd(*this);
                pc += 1;
                requestTransition(10.0f, frames);
                break;
            }
            case 0x37: {   // FUN_001730c0: event light at keyframe 0's position
                pc += 1;
                if (light == -1) {
                    RenderLights& L = renderLights();
                    light = L.alloc(1);                      // FUN_00108a30(0x4e78e0, 1)
                    if (light >= 0) {
                        LightSlot& s = L.slot[light];
                        s.enabled = 1;                       // FUN_00108b10(.., 1)
                        s.pos[0] = keys[0].pos[0];
                        s.pos[1] = keys[0].pos[1];
                        s.pos[2] = keys[0].pos[2];
                        // disasm 0x173168..0x173194: sp+0x40 = 1.0, +0x44 = 1.0, +0x48 = 0, +0x4c = 1.0 -> FUN_00108ba0
                        const float col[4] = {1.0f, 1.0f, 0.0f, 1.0f};
                        std::memcpy(s.color, col, sizeof col);
                        const float amb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        std::memcpy(s.ambient, amb, sizeof amb);
                        const float spec[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                        std::memcpy(s.specular, spec, sizeof spec);
                        s.atten[0] = 1.0f;
                        s.atten[1] = 0.0f;
                        s.atten[2] = 0.0f;
                    }
                }
                break;
            }
            case 0x3b: {   // FUN_00173230 (the slot index stays in +0x20d1c)
                pc += 1;
                if (light != -1) renderLights().release(light);
                break;
            }
            case 0x38: {   // FUN_00173290
                pc += 1;
                const float x = static_cast<float>(rd(*this)) + ox(*this);
                const float y = static_cast<float>(rd(*this)) + oy(*this);
                const float z = static_cast<float>(rd(*this)) + oz(*this);
                if (light >= 0 && light < 5) {   // PORT: bound of the port's light table (the original passes +0x20d1c, even -1)
                    LightSlot& s = renderLights().slot[light];
                    s.pos[0] = x;
                    s.pos[1] = y;
                    s.pos[2] = z;
                }
                break;
            }
            case 0x3f: {   // FUN_001733c0
                pc += 1;
                lightDriftFrames = rd(*this);
                lightDrift[0] = static_cast<float>(rd(*this));
                lightDrift[1] = static_cast<float>(rd(*this));
                lightDrift[2] = static_cast<float>(rd(*this));
                break;
            }
            case 0x39: {   // FUN_00173520: event light colour
                pc += 1;
                const float r = static_cast<float>(rd(*this)) / 100.0f;
                const float g = static_cast<float>(rd(*this)) / 100.0f;
                const float b = static_cast<float>(rd(*this)) / 100.0f;
                if (light >= 0 && light < 5) {   // PORT: bound of the port's light table (the original passes +0x20d1c, even -1)
                    LightSlot& s = renderLights().slot[light];
                    s.color[0] = r; s.color[1] = g; s.color[2] = b; s.color[3] = 1.0f;
                }
                break;
            }
            case 0x3a: {   // FUN_00173650: event light attenuation
                pc += 1;
                const float a = static_cast<float>(rd(*this)) / 10000.0f;
                const float b = static_cast<float>(rd(*this)) / 10000.0f;
                const float c = static_cast<float>(rd(*this)) / 10000.0f;
                if (light >= 0 && light < 5) {   // PORT: bound of the port's light table (the original passes +0x20d1c, even -1)
                    LightSlot& s = renderLights().slot[light];
                    s.atten[0] = a; s.atten[1] = b; s.atten[2] = c;
                }
                break;
            }
            case 0x3c: {   // FUN_00173780: main light direction override (+0x20d30)
                pc += 1;
                lightDir[0] = static_cast<float>(rd(*this)) / 100.0f;
                lightDir[1] = static_cast<float>(rd(*this)) / 100.0f;
                lightDir[2] = static_cast<float>(rd(*this)) / 100.0f;
                break;
            }
            case 0x3d:
            case 0x3e: {   // FUN_001738a0 (main light colour) / FUN_001739d0 (main light ambient)
                pc += 1;
                const float r = static_cast<float>(rd(*this)) / 100.0f;
                const float g = static_cast<float>(rd(*this)) / 100.0f;
                const float b = static_cast<float>(rd(*this)) / 100.0f;
                RenderLights& L = renderLights();
                float* dst = op == 0x3d ? L.slot[L.mainSlot].color : L.slot[L.mainSlot].ambient;
                dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 1.0f;
                break;
            }
            case 0x4b: {   // FUN_00173b00: FUN_0010fb00(alpha, 0x4e7b80, frames); third word skipped
                pc += 1;
                const int32_t frames = rd(*this);
                const float a = static_cast<float>(rd(*this)) / 100.0f;
                pc += 1;
                if (H.noise) H.noise(a, frames);
                break;
            }
            case 0x4c: {   // FUN_00173bd0
                pc += 1;
                const int32_t frames = rd(*this);
                if (H.noiseOff) H.noiseOff(frames);
                break;
            }
            case 0x50: {   // FUN_00173c30: bloom FUN_0010e010(a, b, obj, frames)
                pc += 1;
                const int32_t frames = rd(*this);
                const float a = static_cast<float>(rd(*this)) / 100.0f;
                const float b = static_cast<float>(rd(*this)) / 100.0f;
                if (H.bloom) H.bloom(a, b, frames);
                break;
            }
            case 0x51: {   // FUN_00173d30
                pc += 1;
                const int32_t frames = rd(*this);
                if (H.bloomOff) H.bloomOff(frames);
                break;
            }
            case 0x55: {   // FUN_00173d90: FUN_0010ed60(v, obj, frames); third word skipped
                pc += 1;
                const int32_t frames = rd(*this);
                const float v = static_cast<float>(rd(*this)) / 100.0f;
                pc += 1;
                if (H.screenEffect3) H.screenEffect3(v, frames);
                break;
            }
            case 0x56: {   // FUN_00173e60
                pc += 1;
                const int32_t frames = rd(*this);
                if (H.screenEffect3Off) H.screenEffect3Off(frames);
                break;
            }
            case 0x57:     // FUN_00173ec0: FUN_00111db0(0x4e79b4)
            case 0x58: {   // FUN_00173f10: FUN_00111df0(0x4e79b4)
                if (H.shadowPass) H.shadowPass(op == 0x57);
                pc += 1;
                break;
            }
            case 0xc6: {   // FUN_00173f60: letterbox in
                pc += 1;
                if (letterTop == nullptr && letterBottom == nullptr && letterState == 0 && letterTimer == -1) {
                    ui::SpriteLayer& sl = ui::sprites();
                    letterTop = sl.create(-5.0f, -53.0f, 3.0f, 522.0f, 58.0f, 0, 0, 0, 0, 0x129, 0);
                    if (letterTop != nullptr) letterTop->color = 0xff000000u;
                    letterBottom = sl.create(-5.0f, 443.0f, 3.0f, 522.0f, 74.0f, 0, 0, 0, 0, 0x129, 0);
                    if (letterBottom != nullptr) letterBottom->color = 0xff000000u;
                    letterTimer = 0x1e;
                    letterState = 1;
                }
                break;
            }
            case 199: {    // FUN_00174100: letterbox out
                pc += 1;
                if (letterTop != nullptr && letterBottom != nullptr && letterState == 2 && letterTimer == -1) {
                    letterTimer = 0x1e;
                    letterState = 3;
                }
                break;
            }
            case 0x5a: {   // FUN_001741a0: effect into an event slot
                // PORT: here and in ops 0x5b..0x62 the slot is masked to the 16-entry table (`slot & 0xf`); the original
                // indexes +slot*0x34 unbounded (every shipped EVT uses slots 0..15)
                pc += 1;
                const int32_t slot = rd(*this);
                const int32_t eff = rd(*this);
                EvtEffectSlot& s = effects[static_cast<size_t>(slot) & 0xf];
                s.pos[0] = static_cast<float>(rd(*this)) + ox(*this);
                s.pos[1] = static_cast<float>(rd(*this)) + oy(*this);
                s.pos[2] = static_cast<float>(rd(*this)) + oz(*this);
                s.pos[3] = 1.0f;
                if (s.handle == 0) {
                    float rot[4], scl[4];
                    effectRot0(rot);
                    effectScale100(scl);
                    s.handle = H.effectSpawn ? H.effectSpawn(0.0f, eff, s.pos, rot, scl) : 0;   // FUN_00112190
                }
                break;
            }
            case 0x5b:
            case 0x62: {   // FUN_001743e0: move an effect slot
                const int32_t which = cw(*this, pc);
                pc += 1;
                const int32_t slot = rd(*this);
                const int32_t frames = rd(*this);
                const int32_t vx = rd(*this);
                const int32_t vy = rd(*this);
                const int32_t vz = rd(*this);
                float acc[3] = {0.0f, 0.0f, 0.0f};
                if (which == 0x62) {
                    acc[0] = static_cast<float>(rd(*this)) / 1000.0f;
                    acc[1] = static_cast<float>(rd(*this)) / 1000.0f;
                    acc[2] = static_cast<float>(rd(*this)) / 1000.0f;
                }
                EvtEffectSlot& s = effects[static_cast<size_t>(slot) & 0xf];
                if (s.handle != 0) {
                    s.frames = frames;
                    s.vel[0] = static_cast<float>(vx) / 10.0f;
                    s.vel[1] = static_cast<float>(vy) / 10.0f;
                    s.vel[2] = static_cast<float>(vz) / 10.0f;
                    s.vel[3] = 1.0f;
                    s.acc[0] = acc[0];
                    s.acc[1] = acc[1];
                    s.acc[2] = acc[2];
                }
                break;
            }
            case 0x5c: {   // FUN_00174720: FUN_00112490 = scale (x, y, z, 1)
                pc += 1;
                const int32_t slot = rd(*this);
                float p[4];
                p[0] = static_cast<float>(rd(*this));
                p[1] = static_cast<float>(rd(*this));
                p[2] = static_cast<float>(rd(*this));
                p[3] = 1.0f;
                const int h = effects[static_cast<size_t>(slot) & 0xf].handle;
                if (h != 0 && H.effectScale4) H.effectScale4(h, p);
                break;
            }
            case 0x5d: {   // FUN_001748a0: FUN_001126e0(h, v, -1, -1) = loop-out
                pc += 1;
                const int32_t slot = rd(*this);
                const int32_t v = rd(*this);
                const int h = effects[static_cast<size_t>(slot) & 0xf].handle;
                if (h != 0 && H.effectLoopOut) H.effectLoopOut(h, v, -1, -1);
                break;
            }
            case 0x5e: {   // FUN_00174980: FUN_00112370 rotation /100
                pc += 1;
                const int32_t slot = rd(*this);
                float r[4];
                r[0] = static_cast<float>(rd(*this)) / 100.0f;
                r[1] = static_cast<float>(rd(*this)) / 100.0f;
                r[2] = static_cast<float>(rd(*this)) / 100.0f;
                r[3] = 1.0f;
                const int h = effects[static_cast<size_t>(slot) & 0xf].handle;
                if (h != 0 && H.effectSetRot) H.effectSetRot(h, r);
                break;
            }
            case 0x5f:
            case 0x60: {   // FUN_00174b20 (FUN_001126a0 rate) / FUN_00174bf0 (FUN_00112630(h, v != 0) flag 0x98)
                pc += 1;
                const int32_t slot = rd(*this);
                const int32_t v = rd(*this);
                const int h = effects[static_cast<size_t>(slot) & 0xf].handle;
                if (h != 0) {
                    if (op == 0x5f) { if (H.effectRate) H.effectRate(h, v); }
                    else if (H.effectFlag98) H.effectFlag98(h, v != 0);
                }
                break;
            }
            case 0x61: {   // FUN_00174cd0: kill
                pc += 1;
                const int32_t slot = rd(*this);
                EvtEffectSlot& s = effects[static_cast<size_t>(slot) & 0xf];
                if (s.handle != 0) {
                    if (H.effectAlive && H.effectAlive(s.handle) && H.effectKill) H.effectKill(s.handle);
                    s.handle = 0;
                }
                break;
            }
            case 0xbe: {   // FUN_00174d90
                pc += 1;
                flags().set(static_cast<uint32_t>(rd(*this)));      // FUN_00178890
                break;
            }
            case 0xbf: {   // FUN_00174df0
                pc += 1;
                flags().clear(static_cast<uint32_t>(rd(*this)));    // FUN_00178a30
                break;
            }
            case 0x76:
            case 0x77:
                // no case in the dispatcher: pc does not advance and the original loops forever (never in the data)
                platform::log("event: op " + std::to_string(op) + " (unhandled in the original)");
                return true;
            case 0x78:
            case 0x7b: {   // FUN_00174e50 / FUN_00175090: SE FUN_00115390(vol/10, snd, id); third word skipped
                pc += 1;
                const int32_t id = rd(*this);
                const float vol = static_cast<float>(rd(*this)) / 10.0f;
                pc += 1;
                sound().playSe(vol, id);
                break;
            }
            case 0x79:
            case 0x7c: {   // FUN_00174f20 / FUN_00175160: 3-D SE FUN_00115470(1.0, snd, id, pos)
                pc += 1;
                const int32_t id = rd(*this);
                float p[3];
                p[0] = static_cast<float>(rd(*this)) + ox(*this);
                p[1] = static_cast<float>(rd(*this)) + oy(*this);
                p[2] = static_cast<float>(rd(*this)) + oz(*this);
                if (H.se3d) H.se3d(1.0f, id, p);
                else sound().playSe3d(1.0f, id, p);
                break;
            }
            case 0x7d: {   // FUN_001752d0: FUN_00115820(snd, FUN_00115930(snd, id), +0x20960)
                pc += 1;
                const int32_t id = rd(*this);
                if (H.seFind && H.seStop) H.seStop(H.seFind(id), seStopTime);
                break;
            }
            case 0x7e: {   // FUN_00175370: FUN_00115910(snd, FUN_00115930(snd, id))
                pc += 1;
                const int32_t id = rd(*this);
                if (H.seFind && H.seStopHandle) H.seStopHandle(H.seFind(id));
                break;
            }
            case 0x85:
            case 0x84: {   // FUN_00175400 (3 words skipped) / FUN_001754a0 (2 skipped): FUN_00115a60(1.0, snd, id)
                pc += 1;
                const int32_t id = rd(*this);
                pc += op == 0x85 ? 3 : 2;
                const bool busy = !platform::playerRun() && eventVoiceStatus() != 0;   // PORT (test log)
                if (H.voicePlay) H.voicePlay(1.0f, id);
                else sound().playVoice(1.0f, id);
                if (!platform::playerRun())
                    platform::log("event: voice " + std::to_string(id) + " start frame " + std::to_string(frame) +
                                  (busy ? " (previous voice still playing)" : ""));
                break;
            }
            case 0x80: {   // FUN_00175530: FUN_00115d70(snd, id, 0, 1)
                pc += 1;
                const int32_t id = rd(*this);
                sound().playBgm(id, 0, 1);
                break;
            }
            case 0x81: {   // FUN_001755d0: FUN_00115e10(snd, 8)
                pc += 1;
                sound().stopBgm(8);
                break;
            }
            case 0x82: {   // FUN_00175600: FUN_00115f00(vol/10, snd, (s16) frames)
                pc += 1;
                const float vol = static_cast<float>(rd(*this)) / 10.0f;
                const int16_t frames = static_cast<int16_t>(rd(*this));
                sound().setBgmFadeTarget(vol, frames);
                break;
            }
            case 0x89: { pc += 1; const int32_t v = rd(*this); if (H.sound137) H.sound137(v); break; }   // FUN_001756c0 -> FUN_00115920
            case 0x8a: { pc += 1; const int32_t v = rd(*this); if (H.sound138) H.sound138(v); break; }   // FUN_00175720 -> FUN_00115e70
            case 0x8b: { pc += 1; const int32_t v = rd(*this); if (H.voicePause) H.voicePause(v); break; }   // FUN_00175780 -> FUN_00115c50
            case 0x8c: { pc += 1; seStopTime = static_cast<int16_t>(rd(*this)); break; }                    // FUN_001757e0
            case 0x8d: { pc += 1; sound().stopBgm(static_cast<int16_t>(rd(*this))); break; }               // FUN_00175850 -> FUN_00115e10
            case 0x8e: { pc += 2; break; }                                                                     // FUN_001758b0
            case 0xc0: {   // FUN_001758e0: FUN_0020c5e0(pad, 0, (u8) small, frames); FUN_0020c570(pad, 0, big, frames)
                pc += 1;
                const int32_t big = rd(*this);
                const int32_t small = static_cast<uint8_t>(rd(*this));
                const int32_t frames = rd(*this);
                if (H.padRumble) {
                    H.padRumble(0, small, frames);
                    H.padRumble(1, big, frames);
                }
                break;
            }
            case 0xc1: { pc += 1; skippable = rd(*this); break; }   // FUN_00175a00
            case 0xb4: { pc += 1; chained = rd(*this); break; }     // FUN_00175a70
            case 200: {    // FUN_00175e10
                pc += 1;
                end();                                       // FUN_0016cbf0
                return false;
            }
            case 999:
                pc += 1;
                return true;
            default:
                // no dispatcher case: the original spins on the same word forever (never in the data, evt.cpp check)
                platform::log("event: unknown cue op " + std::to_string(op));
                return true;
            }
            op = cw(*this, pc);
        }
    }
    actorJobs();                                             // FUN_001769d0
    effectJobs();                                            // FUN_00177050
    lightJob();                                              // FUN_00177360
    colorQuadJob();                                          // FUN_00177210
    return true;
}

// orig FUN_001769d0 (work/decompiled/001769d0.c)
void EventManager::actorJobs() {
    for (EvtActorJob& j : jobs) {
        Chr* c = j.actor;
        if (c == nullptr) continue;
        if (0 < j.rotFrames[0]) rotateAboutRight(c->xf.m, (j.rotSpeed[0] * 3.1415927f) / 180.0f);    // FUN_00128c00
        if (0 < j.rotFrames[1]) rotateAboutUp(c->xf.m, (j.rotSpeed[1] * 3.1415927f) / 180.0f);       // FUN_00128ae0
        if (0 < j.rotFrames[2]) rotateAboutForward(c->xf.m, (j.rotSpeed[2] * 3.1415927f) / 180.0f);  // FUN_001289c0
        if (0 < j.rotFrames[0] || 0 < j.rotFrames[1] || 0 < j.rotFrames[2]) syncItemRecord(c);   // PORT
        if (j.moveFrames < 1) {
            if (0 < j.fallFrames) publishAtTarget(c, j.target);
        } else {
            if (j.fallFrames < 1) {
                Vec3f& p = c->xf.m.pos;
                const float z = j.vel[2];
                float y;
                if (c->type == 4 || c->type == 3) y = j.vel[1] + p[1];
                else y = p[1];
                p[0] = j.vel[0] + p[0];
                p[1] = y;
                p[2] = z + p[2];
                syncItemRecord(c);                           // PORT
            } else {
                j.target[0] = j.target[0] + j.vel[0];
                j.target[1] = j.target[1] + j.vel[1];
                j.target[2] = j.target[2] + j.vel[2];
                publishAtTarget(c, j.target);
            }
            j.vel[0] = j.vel[0] + j.acc[0];
            j.vel[1] = j.vel[1] + j.acc[1];
            j.vel[2] = j.vel[2] + j.acc[2];
        }
        j.moveFrames -= 1;
        j.rotFrames[0] -= 1;
        j.rotFrames[1] -= 1;
        j.rotFrames[2] -= 1;
        j.fallFrames -= 1;
        if (j.moveFrames < 1 && j.rotFrames[0] < 1 && j.rotFrames[1] < 1 && j.rotFrames[2] < 1 && j.fallFrames < 1) {
            j.moveFrames = 0;
            j.rotFrames[0] = j.rotFrames[1] = j.rotFrames[2] = 0;
            j.fallFrames = 0;
            j.actor = nullptr;
            j.vel[0] = j.vel[1] = j.vel[2] = 0.0f;
            j.acc[0] = j.acc[1] = j.acc[2] = 0.0f;
            j.rotSpeed[0] = j.rotSpeed[1] = j.rotSpeed[2] = 0.0f;
            j.hasTarget = 0;
        }
    }
}

// orig FUN_00177050 (work/decompiled/00177050.c)
void EventManager::effectJobs() {
    EventHooks& H = eventHooks();
    for (EvtEffectSlot& s : effects) {
        if (s.handle != 0 && 0 < s.frames) {
            for (int k = 0; k < 4; ++k) s.pos[k] = s.pos[k] + s.vel[k];
            if (H.effectMove) H.effectMove(s.handle, s.pos);  // FUN_001122d0
            for (int k = 0; k < 3; ++k) s.vel[k] = s.vel[k] + s.acc[k];
            s.frames -= 1;
        }
    }
}

// orig FUN_00177360 (work/decompiled/00177360.c + disasm 0x177360..0x177418): $a1 holds the decremented counter
// only for the store at 0x177390; 0x177398 reloads it from +0x20d1c (the event light) before the getter
// FUN_00108c60(0x4e78e0, light). Only the x component of the light's own position is read (0x1773bc) and added to
// each drift word: new pos = (x + d0, x + d1, x + d2) -> FUN_00108b40(0x4e78e0, light); kept.
void EventManager::lightJob() {
    if (0 < lightDriftFrames) {
        lightDriftFrames -= 1;
        if (light != -1) {
            RenderLights& L = renderLights();
            if (light >= 0 && light < 5) {   // PORT: slot bound of the port's light table (the original passes +0x20d1c)
                const float base = L.slot[light].pos[0];
                L.slot[light].pos[0] = base + lightDrift[0];
                L.slot[light].pos[1] = base + lightDrift[1];
                L.slot[light].pos[2] = base + lightDrift[2];
            }
        }
    }
}

// orig FUN_00177210 (work/decompiled/00177210.c)
void EventManager::colorQuadJob() {
    if (colorQuad == nullptr) return;
    if (quadStep == 0) return;                               // PORT: 0x80 / 0 is a division trap on the EE too
    const int d = 0x80 / quadStep;
    const int a = static_cast<int>(colorQuad->color >> 24) + d;
    if (a < 0) {
        ui::sprites().destroyRef(colorQuad);
    } else if (a < 0x81) {
        colorQuad->color = colorQuad->color + static_cast<uint32_t>(d) * 0x1000000u;
    }
}

}  // namespace baroque::game
