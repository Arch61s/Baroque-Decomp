// EXI particle-effect runtime: manager, instance creation, per-tick update, emitter spawn, keyframe tracks and the
// control functions. Draw pass and per-particle vertex build live in effects_draw.cpp. See effects.hpp.
#include "game/effects/effects.hpp"

#include "platform/platform.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace baroque::game {

namespace {

float rawF32(const uint8_t* p, int off) {
    float f;
    std::memcpy(&f, p + off, 4);
    return f;
}
int16_t rawS16(const uint8_t* p, int off) {
    int16_t v;
    std::memcpy(&v, p + off, 2);
    return v;
}
uint16_t rawU16(const uint8_t* p, int off) {
    uint16_t v;
    std::memcpy(&v, p + off, 2);
    return v;
}
int32_t rawS32(const uint8_t* p, int off) {
    int32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
uint32_t rawU32(const uint8_t* p, int off) {
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}

// LCG multiplier of FUN_001e9820 and the u16 -> float scale 0x37800080 (1.5259022e-05).
constexpr uint32_t kLcgMul = 0x5d588b65u;
const float kRandScale = exiBitsToFloat(0x37800080u);

}  // namespace

// orig FUN_001e7b80 (work/decompiled/001e7b80.c): seed +0xd8 = 0xbadface, id counter +0xd4 = 0, pause flags 0.
// The pools of FUN_00111e30 (0x8000 / 0xa0 instances, 0xe000 / 0x70 nodes) are allocated here.
EffectManager::EffectManager() {
    nodes_.resize(kExiNodeCapacity);
    instances_.resize(kExiInstanceCapacity);
    freeNodes_.reserve(nodes_.size());
    freeInstances_.reserve(instances_.size());
    for (size_t i = nodes_.size(); i-- > 0;) freeNodes_.push_back(&nodes_[i]);
    for (size_t i = instances_.size(); i-- > 0;) freeInstances_.push_back(&instances_[i]);
    exiIdentity(camera_);
    scratch_.resize(kExiMaxVertices);
}

// orig FUN_001e8b20 (work/decompiled/001e8b20.c): chunk copy + index relocation, done by data::parseExi (indices
// kept). Returns false like the original when the FORM/EXIF header is wrong (FUN_001120c0 then calls FUN_00239f68).
bool EffectManager::load(std::span<const uint8_t> exiBytes) {
    unload();
    std::string err;
    auto parsed = data::parseExi(exiBytes, &err);
    if (!parsed) {
        platform::log("effects: BQ.EXI parse failed: " + err);
        return false;
    }
    file_ = std::move(*parsed);
    loaded_ = true;
    return true;
}

// orig FUN_001e7e00 (work/decompiled/001e7e00.c) -> FUN_001e9230: releases the chunk buffers (instances are not
// freed there; the port also returns them to the pools so a reload starts clean).
void EffectManager::unload() {
    killAll();
    file_ = data::ExiFile{};
    loaded_ = false;
}

// ---------------------------------------------------------------------------------------------------------------
// Record access
// ---------------------------------------------------------------------------------------------------------------

EffectManager::Track EffectManager::emitterTrack(int emitter, int off) const {
    const uint8_t* p = file_.emitters[size_t(emitter)].raw.data();
    return {rawS16(p, off), rawS16(p, off + 2), rawU32(p, off + 4)};
}

// OBJS tracks use the loader-normalised type (FUN_001e8b20: count 0 -> type 0, count 1 -> type 1).
EffectManager::Track EffectManager::objectTrack(int object, int off) const {
    const auto& a = file_.objects[size_t(object)].anim[size_t((off - 0x2c) / 8)];
    return {a.a, a.b, a.keyframe};
}

float EffectManager::emitterF32(int e, int off) const { return rawF32(file_.emitters[size_t(e)].raw.data(), off); }
uint8_t EffectManager::emitterByte(int e, int off) const { return file_.emitters[size_t(e)].raw[size_t(off)]; }
int16_t EffectManager::emitterS16(int e, int off) const { return rawS16(file_.emitters[size_t(e)].raw.data(), off); }
uint8_t EffectManager::objectByte(int o, int off) const { return file_.objects[size_t(o)].raw[size_t(off)]; }
int EffectManager::refByte0(bool isEmitter, int index) const {
    return isEmitter ? file_.emitters[size_t(index)].raw[0] : file_.objects[size_t(index)].raw[0];
}

// Render-state record OBJS+4 as read by FUN_001eaa10 / FUN_001ecec0 / FUN_001e7e90.
ExiRenderState EffectManager::renderState(int object) const {
    ExiRenderState s;
    const auto& o = file_.objects[size_t(object)];
    const uint8_t* p = o.raw.data();
    s.object = object;
    s.vertsPerPrim = rawU16(p, 4);
    s.depthTest = (p[7] & 1) != 0;
    s.depthWrite = (p[7] & 2) != 0;
    s.textureOff = (p[7] & 4) != 0;
    s.blend = p[6] & 0xf;
    s.textureIndex = (p[7] & 4) != 0 ? 0xffff : rawU16(p, 0xe);  // FUN_001e8b20: byte7 bit2 -> +0xe = 0xffff
    s.uvWindow[0] = rawU16(p, 0x10);
    s.uvWindow[1] = rawU16(p, 0x12);
    s.uvWindow[2] = rawU16(p, 0x14);
    s.uvWindow[3] = rawU16(p, 0x16);
    s.triangleList = rawU16(p, 0xc) == 1;
    return s;
}

// orig FUN_001eac20 (work/decompiled/001eac20.c; compares and the cubic evaluation order from disasm 0x1eac20..)
float EffectManager::evalTrack(float t, const Track& tr) const {
    const auto& keys = file_.keyframes;
    const auto value = [&](int i) -> float {
        const size_t k = size_t(tr.key) + size_t(i);
        return k < keys.size() ? exiBitsToFloat(keys[k].w0) : 0.0f;  // port guard (original reads past the chunk)
    };
    const auto time = [&](int i) -> int32_t {
        const size_t k = size_t(tr.key) + size_t(i);
        return k < keys.size() ? static_cast<int32_t>(keys[k].w1) : 0;
    };
    const int n = tr.count;
    switch (tr.type) {
    case 3: {
        int p = 0;
        while (p < n && float(time(p)) < t) ++p;  // c.lt.s key.time, t
        if (p - 1 < 0) return value(p);
        if (p == n) return value(p - 1);
        const float next = (p + 1 == n) ? value(p) : value(p + 1);
        const float cur = value(p);
        const float prev = value(p - 1);
        const float f6 = next - cur;
        const float u = (t - float(time(p - 1))) / float(time(p) - time(p - 1));
        const float f7 = cur - prev;
        const float f2 = 2.0f * prev - 2.0f * cur;
        const float a = f6 + (f7 + f2);
        float b = 3.0f * cur + -3.0f * prev;
        b = b - 2.0f * f7;
        b = b - f6;
        const float u2 = u * u;
        const float u3 = u2 * u;
        float acc = b * u2;
        acc = acc + a * u3;
        const float r = acc + f7 * u;
        return prev + r;
    }
    case 2: {
        int p = 0;
        while (p < n && float(time(p)) <= t) ++p;  // c.le.s key.time, t
        if (p - 1 < 0) return value(p);
        if (p == n) return value(p - 1);
        const float prev = value(p - 1);
        const float d = (value(p) - prev) * (t - float(time(p - 1)));
        return prev + d / float(time(p) - time(p - 1));
    }
    case 1:
        return value(0);
    case 0: {
        int p = 0;
        while (p < n && float(time(p)) <= t) ++p;
        if (p - 1 < 0) return value(p);
        return value(p - 1);
    }
    default:
        return 0.0f;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Pools
// ---------------------------------------------------------------------------------------------------------------

// Pool vt+0x74 (FUN_002db8d0 pool): nullptr when exhausted. UNVERIFIED: the console pool hands out stale memory;
// the port zeroes the node (only group nodes read fields they never set: +0x44 in FUN_001ebb30).
ExiNode* EffectManager::allocNode() {
    if (freeNodes_.empty()) return nullptr;
    ExiNode* n = freeNodes_.back();
    freeNodes_.pop_back();
    *n = ExiNode{};
    n->inUse = true;
    return n;
}

// orig FUN_001e9790 (work/decompiled/001e9790.c): node pool vt+0x90.
void EffectManager::freeNode(ExiNode* n) {
    n->inUse = false;
    freeNodes_.push_back(n);
}

// orig FUN_001e97b0 (work/decompiled/001e97b0.c)
void EffectManager::freeTree(ExiNode* n) {
    while (n != nullptr) {
        ExiNode* next = n->next;
        if (n->firstChild != nullptr) freeTree(n->firstChild);
        freeNode(n);
        n = next;
    }
}

ExiInstance* EffectManager::allocInstance() {
    if (freeInstances_.empty()) return nullptr;
    ExiInstance* i = freeInstances_.back();
    freeInstances_.pop_back();
    *i = ExiInstance{};
    i->inUse = true;
    return i;
}

// orig FUN_001e9310 (work/decompiled/001e9310.c)
void EffectManager::freeInstance(ExiInstance* inst) {
    if (inst == nullptr) return;
    if (inst->seq != nullptr) freeTree(inst->seq);
    inst->effect = -1;
    inst->flags8a = 0;
    if (inst->prev == nullptr) head_ = inst->next;
    else inst->prev->next = inst->next;
    if (inst->next != nullptr) inst->next->prev = inst->prev;
    inst->inUse = false;  // the original keeps +4 intact (FUN_001e7e90 / FUN_001eaed0 read it after the free)
    freeInstances_.push_back(inst);
}

int EffectManager::instanceCount() const {
    int n = 0;
    for (const ExiInstance* i = head_; i != nullptr; i = i->next) ++n;
    return n;
}

int EffectManager::liveNodeCount() const { return kExiNodeCapacity - static_cast<int>(freeNodes_.size()); }

// ---------------------------------------------------------------------------------------------------------------
// Instance creation
// ---------------------------------------------------------------------------------------------------------------

// orig FUN_001eaf30 (work/decompiled/001eaf30.c; the removed block 0x1eaf78 is the u16 -> float fallback of
// `bltzl` on an lhu value, never taken)
ExiNode* EffectManager::makeSequenceNode(int seq) {
    ExiNode* n = allocNode();
    if (n == nullptr) return nullptr;
    const auto& s = file_.sequences[size_t(seq)];
    const uint8_t* p = s.raw.data();
    for (int i = 0; i < 16; ++i) n->matrix.m[i] = rawF32(p, i * 4);  // FUN_0039d860(node, seqs)
    n->refIsEmitter = s.refIsEmitter;
    n->ref = static_cast<int>(s.refIndex);
    n->start = static_cast<float>(rawU16(p, 0x44));
    n->end = static_cast<float>(static_cast<int>(rawU16(p, 0x44)) + static_cast<int>(rawU16(p, 0x46)));
    n->t = 0.0f;
    n->scale = rawF32(p, 0x48);
    n->firstChild = nullptr;
    n->lastChild = nullptr;
    n->childCount = 0;
    n->next = nullptr;
    n->accum = 0.0f;
    n->kind = refByte0(n->refIsEmitter, n->ref) == 0 ? 1 : 2;
    return n;
}

// orig FUN_001eb020 (work/decompiled/001eb020.c)
ExiNode* EffectManager::makeGroupNode() {
    ExiNode* n = allocNode();
    if (n == nullptr) return nullptr;
    n->kind = 0;
    n->firstChild = nullptr;
    n->lastChild = nullptr;
    n->childCount = 0;
    n->next = nullptr;
    n->accum = 0.0f;
    return n;
}

// orig FUN_001eb070 (work/decompiled/001eb070.c, disasm 0x1eb070): same record type byte, then the same OBJS
// record (objects) or the same EMTS->OBJS pointer (emitters).
bool EffectManager::sameRef(int seqA, int seqB) const {
    const auto& a = file_.sequences[size_t(seqA)];
    const auto& b = file_.sequences[size_t(seqB)];
    const int ta = refByte0(a.refIsEmitter, int(a.refIndex));
    const int tb = refByte0(b.refIsEmitter, int(b.refIndex));
    if (ta != tb) return false;
    if (ta == 0) return a.refIsEmitter == b.refIsEmitter && a.refIndex == b.refIndex;
    if (a.refIsEmitter && b.refIsEmitter)
        return file_.emitters[a.refIndex].object == file_.emitters[b.refIndex].object;
    // UNVERIFIED: a non-zero type byte on an OBJS record does not occur in BQ.EXI (all OBJS byte0 = 0).
    return a.refIsEmitter == b.refIsEmitter && a.refIndex == b.refIndex;
}

// orig FUN_001eb0d0 (work/decompiled/001eb0d0.c)
int EffectManager::create(int effectId, const float* pos, const float* rotDeg, const float* scale, const float* dir) {
    if (effectId < 0) return 0;
    if (!loaded_ || effectId >= effectCount()) return 0;  // port guard: the original indexes EFCS unchecked
    const auto& efcs = file_.effects[size_t(effectId)];
    const uint8_t* e = efcs.raw.data();
    const int seqCount = rawS16(e, 0x1a);
    if (seqCount == 0) return 0;
    ExiInstance* inst = allocInstance();
    if (inst == nullptr) return 0;
    const int prevCounter = idCounter_;
    idCounter_ = prevCounter + 1;
    if (prevCounter + 1 == 0) idCounter_ = 1;
    inst->id = idCounter_;
    inst->flags98 = 0;
    if (head_ == nullptr) {
        head_ = inst;
        inst->prev = nullptr;
    } else {
        ExiInstance* tail = head_;
        while (tail->next != nullptr) tail = tail->next;
        inst->prev = tail;
        tail->next = inst;
    }
    inst->next = nullptr;
    inst->effect = effectId;
    inst->time = 0;
    inst->prevTime = 0;
    inst->seq = nullptr;
    inst->particles = 0;
    inst->rsqIndex = 0;
    inst->loopCount = 0;
    inst->ovEnd = 0xffff;
    inst->ovRestart = 0xffff;
    inst->ovCount = 0;
    inst->flags8a = 0;
    inst->rate = 0x10000;
    inst->frac = 0;
    inst->tag = 0.0f;

    // Sequence list: consecutive SEQS sharing the same OBJS (FUN_001eb070) become one group node.
    const int seqBase = static_cast<int>(efcs.sequence);
    ExiNode* lastTop = nullptr;
    const auto flush = [&](int from, int to) {
        ExiNode* node = nullptr;
        if (to - from < 2) {
            node = makeSequenceNode(seqBase + from);
        } else {
            node = makeGroupNode();
            if (node != nullptr) {
                for (int j = from; j < to; ++j) {
                    ExiNode* child = makeSequenceNode(seqBase + j);
                    if (child == nullptr) continue;
                    if (node->lastChild == nullptr) node->firstChild = child;
                    else node->lastChild->next = child;
                    node->lastChild = child;
                    node->childCount += 1;
                }
            }
        }
        if (node != nullptr) {
            if (lastTop == nullptr) inst->seq = node;
            else lastTop->next = node;
            lastTop = node;
        }
    };
    int groupStart = 0;
    int prevSeq = -1;
    int i = 0;
    for (; i < seqCount; ++i) {
        const int cur = seqBase + i;
        if (prevSeq >= 0 && !sameRef(prevSeq, cur)) {
            flush(groupStart, i);
            groupStart = i;
        }
        prevSeq = cur;
    }
    flush(groupStart, i);

    if (scale == nullptr) {
        exiIdentity(inst->matrix);
        inst->scale[2] = 1.0f;
        inst->scale[1] = 1.0f;
        inst->scale[0] = 1.0f;
    } else {
        const float s[3] = {scale[0], scale[1], scale[2]};
        exiScaleMatrix(inst->matrix, s);
        inst->scale[0] = scale[0];
        inst->scale[1] = scale[1];
        inst->scale[2] = scale[2];
    }
    if (rotDeg == nullptr) {
        inst->rot[2] = 0.0f;
        inst->rot[1] = 0.0f;
        inst->rot[0] = 0.0f;
    } else {
        ExiMat r;
        exiIdentity(r);
        exiEulerTableDeg(r, rotDeg);
        exiMul(inst->matrix, inst->matrix, r);
        inst->rot[0] = rotDeg[0];
        inst->rot[1] = rotDeg[1];
        inst->rot[2] = rotDeg[2];
    }
    if (dir != nullptr) {
        const float up[3] = {0.0f, 1.0f, 0.0f};
        ExiMat r;
        exiIdentity(r);
        const float d = up[2] * dir[2] + up[0] * dir[0] + up[1] * dir[1];
        float axis[3];
        if (d <= -0.9999f || 0.9999f <= d) {
            axis[1] = 0.0f;
            axis[0] = 1.0f;
            axis[2] = 0.0f;
        } else {
            const float dv[3] = {dir[0], dir[1], dir[2]};
            exiCross(axis, dv, up);
        }
        exiAxisAngle(exiAcos(d), r, axis);
        exiMul(inst->matrix, inst->matrix, r);
    }
    if (pos != nullptr) {
        inst->matrix.m[12] = pos[0];
        inst->matrix.m[13] = pos[1];
        inst->matrix.m[14] = pos[2];
    }
    return inst->id;
}

// ---------------------------------------------------------------------------------------------------------------
// Per-tick update
// ---------------------------------------------------------------------------------------------------------------

// orig FUN_001e95c0 (work/decompiled/001e95c0.c): returns false when the instance is finished.
bool EffectManager::updateInstance(ExiInstance& inst) {
    const uint8_t* e = file_.effects[size_t(inst.effect)].raw.data();
    if ((flagsD0_ & 1u) == 0) {
        inst.particles = updateNodes(inst, inst.seq);
        inst.frac = static_cast<uint32_t>(static_cast<int32_t>(inst.frac) + inst.rate);
        inst.prevTime = inst.time;
        inst.time = inst.time + (static_cast<int32_t>(inst.frac) >> 16);
        inst.frac = inst.frac & 0xffffu;
    }
    bool haveRecord = false;
    uint16_t recEnd = 0, recRestart = 0;
    int16_t recCount = 0;
    if ((inst.flags8a & 1u) == 0) {
        if (inst.rsqIndex < rawS32(e, 0x20)) {
            const auto& r = file_.rsqs[size_t(file_.effects[size_t(inst.effect)].rsq) + size_t(inst.rsqIndex)];
            recEnd = static_cast<uint16_t>(r.w0 & 0xffff);
            recRestart = static_cast<uint16_t>(r.w0 >> 16);
            recCount = static_cast<int16_t>(r.w1 & 0xffff);
            haveRecord = true;
        }
    } else if (inst.rsqIndex <= 0) {
        recEnd = inst.ovEnd;
        recRestart = inst.ovRestart;
        recCount = inst.ovCount;
        haveRecord = true;
    }
    if (haveRecord && inst.prevTime < static_cast<int>(recEnd) && static_cast<int>(recEnd) <= inst.time) {
        if (recCount < 0) {
            inst.time = recRestart;
            inst.loopCount += 1;
        } else if (inst.loopCount < recCount) {
            inst.time = recRestart;
            if (recRestart < recEnd) {
                inst.loopCount += 1;
            } else {
                inst.rsqIndex += 1;
                inst.loopCount = 0;
            }
        } else {
            inst.rsqIndex += 1;
            inst.loopCount = 0;
        }
    }
    if (static_cast<int>(rawS16(e, 0x18)) < inst.time) return inst.particles != 0;
    return true;
}

// orig FUN_001e93a0 (work/decompiled/001e93a0.c): advances sequence time, spawns, ages and removes particles.
int EffectManager::updateNodes(ExiInstance& inst, ExiNode* node) {
    int total = 0;
    for (; node != nullptr; node = node->next) {
        if (node->kind == 2) {
            const float now = static_cast<float>(inst.time);
            if (node->start <= now && now <= node->end) {
                node->t = node->scale * (static_cast<float>(inst.time + 1) - node->start);
                node->accum = node->accum + evalTrack(node->t, emitterTrack(node->ref, 0x9c));
                emit(node->t, *node, inst.matrix);
            }
            ExiNode* prev = nullptr;
            ExiNode* p = node->firstChild;
            while (p != nullptr) {
                ExiNode* next = p->next;
                if (p->end < p->t) {
                    freeNode(p);
                    if (prev == nullptr) node->firstChild = next;
                    else prev->next = next;
                    if (next == nullptr) node->lastChild = prev;
                    node->childCount -= 1;
                } else {
                    p->t = p->t + p->scale;
                    prev = p;
                }
                p = next;
            }
            total += node->childCount;
        } else if (node->kind == 1) {
            const float now = static_cast<float>(inst.time);
            if (node->start <= now && now <= node->end)
                node->t = node->scale * (static_cast<float>(inst.time + 1) - node->start);
        } else if (node->kind == 0) {
            total += updateNodes(inst, node->firstChild);
        }
    }
    return total;
}

// LCG step of FUN_001e9820 (`seed = seed * 0x5d588b65 + 1`, high bits dropped; the low u16 scaled by 0x37800080).
float EffectManager::rand01() {
    seed_ = seed_ * kLcgMul + 1u;
    return static_cast<float>(seed_ & 0xffffu) * kRandScale;
}

// orig FUN_001e9820 (work/decompiled/001e9820.c, verified against disasm 0x1e9820..0x1eaa00).
// All 15 blocks Ghidra reports as unreachable (0x1e98e0, 0x1e9bb8, 0x1e9cb4, 0x1e9de0, 0x1e9e80, 0x1e9f20,
// 0x1ea010, 0x1ea0ac, 0x1ea138, 0x1ea1d8, 0x1ea2f8, 0x1ea3a8, 0x1ea4b0, 0x1ea778, 0x1ea8b8) are the compiler's
// unsigned->float fallback after `bltz(l)` on a zero-extended u16 (lhu / andi 0xffff): truly dead. The float
// operation order below follows the disassembly where it differs from the pseudocode (madd.s accumulations,
// range * (k * r) in velocity mode 2, angle = (0 + acos) + 2 * (pi * jitter)).
void EffectManager::emit(float t, ExiNode& node, const ExiMat& instanceMatrix) {
    const int e = node.ref;
    ExiMat emitterM;
    exiIdentity(emitterM);
    float total = 0.0f + node.accum;
    total = total + emitterF32(e, 0x14) * (rand01() - 0.5f);
    const float range = emitterF32(e, 0x20) - emitterF32(e, 0x1c);
    if (total < 1.0f) return;
    node.accum = total - static_cast<float>(static_cast<int>(total));

    const float pos[3] = {evalTrack(t, emitterTrack(e, 0x24)), evalTrack(t, emitterTrack(e, 0x2c)),
                          evalTrack(t, emitterTrack(e, 0x34))};
    const float scl[3] = {evalTrack(t, emitterTrack(e, 0x3c)), evalTrack(t, emitterTrack(e, 0x44)),
                          evalTrack(t, emitterTrack(e, 0x4c))};
    const float r0 = evalTrack(t, emitterTrack(e, 0x54));
    const float r1 = evalTrack(t, emitterTrack(e, 0x5c));
    const float r2 = evalTrack(t, emitterTrack(e, 0x64));
    const float ps0 = evalTrack(t, emitterTrack(e, 0x6c));
    const float ps1 = evalTrack(t, emitterTrack(e, 0x74));
    const float ps2 = evalTrack(t, emitterTrack(e, 0x7c));
    const float yaw = evalTrack(t, emitterTrack(e, 0x84));
    const float roll = evalTrack(t, emitterTrack(e, 0x8c));
    const float life = evalTrack(t, emitterTrack(e, 0xa4));

    ExiMat rotM;
    exiIdentity(rotM);
    const float twoPi = exiBitsToFloat(0x40c90fdbu);
    const float ang[3] = {r0 * twoPi, r1 * twoPi, r2 * twoPi};
    exiEulerTable(rotM, ang);
    exiScaleMatrix(emitterM, scl);
    exiMul(emitterM, emitterM, rotM);
    exiSetTranslation(emitterM, pos);
    if (total <= 0.0f) return;

    const float speedCube = 2.0f * range;
    const float pi = exiBitsToFloat(0x40490fdbu);
    float v[3] = {0.0f, 0.0f, 0.0f};  // stack 0x1d0: kept across iterations (UNVERIFIED for type bytes > 3)
    do {
        if (total < 1.0f) {
            if (total <= rand01()) return;
            total = 0.0f;
        } else {
            total = total - 1.0f;
        }
        ExiNode* p = allocNode();
        if (p == nullptr) return;
        if (node.lastChild == nullptr) node.firstChild = p;
        else node.lastChild->next = p;
        node.lastChild = p;
        node.childCount += 1;
        p->next = nullptr;

        const float j = emitterF32(e, 0xc) * (rand01() - 0.5f);
        ExiMat sm;
        const float sv[3] = {j + ps0, j + ps1, j + ps2};
        exiScaleMatrix(sm, sv);

        switch (static_cast<int8_t>(emitterByte(e, 8))) {
        case 3: {  // planar XZ unit vector
            v[1] = 0.0f;
            v[0] = rand01() - 0.5f;
            const float z = rand01() - 0.5f;
            float acc = v[1] * v[1];
            acc = acc + v[0] * v[0];
            acc = acc + z * z;
            const float inv = 1.0f / std::sqrt(acc);
            v[0] = v[0] * inv;
            v[1] = v[1] * inv;
            v[2] = z * inv;
            break;
        }
        case 2: {  // random direction * range * r
            const float speed = range * rand01();
            const float x = rand01() - 0.5f;
            const float y = rand01() - 0.5f;
            const float z = rand01() - 0.5f;
            float acc = y * y;
            acc = acc + x * x;
            acc = acc + z * z;
            const float inv = 1.0f / std::sqrt(acc);
            v[0] = x * inv * speed;
            v[1] = y * inv * speed;
            v[2] = z * inv * speed;
            break;
        }
        case 1: {  // cube
            const float x = rand01();
            const float y = rand01();
            const float z = rand01();
            v[0] = (x - 0.5f) * speedCube;
            v[1] = (y - 0.5f) * speedCube;
            v[2] = (z - 0.5f) * speedCube;
            break;
        }
        case 0:
            v[0] = 0.0f;
            v[1] = 0.0f;
            v[2] = 0.0f;
            break;
        default:
            break;
        }
        const float d[3] = {v[0], v[1], v[2]};  // stack 0x1c0
        const float jitter = emitterF32(e, 0x10) * (rand01() - 0.5f);
        float angle = 0.0f;
        float axis[3] = {0.0f, 0.0f, 0.0f};
        if (static_cast<int8_t>(emitterByte(e, 9)) == 0) {
            const float a = 2.0f * (pi * (jitter + yaw));
            axis[0] = exiCos(a);
            axis[1] = 0.0f;
            axis[2] = -exiSin(a);
            const float j2 = rand01() - 0.5f;
            angle = 2.0f * (pi * (emitterF32(e, 0x10) * j2 + roll));
        } else {
            const float up[3] = {0.0f, 1.0f, 0.0f};
            const float fwd[3] = {0.0f, 0.0f, 1.0f};
            float dot = d[1] * up[1];
            dot = dot + d[0] * up[0];
            dot = dot + d[2] * up[2];
            dot = std::fabs(dot);
            const float* ref = up;
            if (dot < 1.0f) exiCross(axis, up, d);
            else {
                exiCross(axis, d, fwd);
                ref = fwd;
            }
            float acc = axis[1] * axis[1];
            acc = acc + axis[0] * axis[0];
            acc = acc + axis[2] * axis[2];
            const float inv = 1.0f / std::sqrt(acc);
            axis[0] = axis[0] * inv;
            axis[2] = axis[2] * inv;
            axis[1] = axis[1] * inv;
            float c = d[1] * ref[1];
            c = c + d[0] * ref[0];
            c = c + d[2] * ref[2];
            const float pj = pi * jitter;
            angle = (0.0f + exiAcos(c)) + 2.0f * pj;
        }
        exiAxisAngle(angle, p->matrix, axis);
        exiPreMul(p->matrix, sm);
        exiTransformPoint(v, v, emitterM);
        exiMul(p->matrix, p->matrix, rotM);
        exiSetTranslation(p->matrix, v);
        exiMul(p->matrix, p->matrix, node.matrix);
        if (emitterS16(e, 0xa) == 0) exiMul(p->matrix, p->matrix, instanceMatrix);

        float l = 0.0f + life;
        l = l + emitterF32(e, 0x18) * (rand01() - 0.5f);
        if (l < 0.0f) l = -l;
        else if (l == 0.0f) l = 1.0f;
        const int obj = static_cast<int>(file_.emitters[size_t(e)].object);
        p->kind = 1;
        p->refIsEmitter = false;
        p->ref = obj;
        p->start = 0.0f;
        p->end = static_cast<float>(static_cast<int>(rawS16(file_.objects[size_t(obj)].raw.data(), 2)));
        p->t = 0.0f;
        p->scale = 1.0f / l;
        p->firstChild = nullptr;
        p->lastChild = nullptr;
        p->childCount = 0;
        p->accum = 0.0f;
    } while (!(total <= 0.0f));
}

// ---------------------------------------------------------------------------------------------------------------
// Control functions
// ---------------------------------------------------------------------------------------------------------------

// orig FUN_001e9750 (work/decompiled/001e9750.c)
ExiInstance* EffectManager::find(int id) {
    for (ExiInstance* i = head_; i != nullptr; i = i->next)
        if (i->id == id) return i;
    return nullptr;
}

// orig FUN_001ebc60 (work/decompiled/001ebc60.c; disasm: the tag is $f12, the id $a1)
void EffectManager::setTag(float tag, int id) {
    if (ExiInstance* i = find(id)) i->tag = tag;
}

// orig FUN_001ebc90 (work/decompiled/001ebc90.c)
void EffectManager::setMatrix(int id, const ExiMat& m) {
    if (ExiInstance* i = find(id)) exiCopy(i->matrix, m);
}

// orig FUN_001eb650 (work/decompiled/001eb650.c)
void EffectManager::kill(int id) {
    if (ExiInstance* i = find(id)) freeInstance(i);
}

// orig FUN_001eb690 (work/decompiled/001eb690.c)
void EffectManager::setFlag98(int id, bool on) {
    if (ExiInstance* i = find(id)) {
        if (on) i->flags98 |= 1u;
        else i->flags98 &= ~1u;
    }
}

// orig FUN_001eb710 (work/decompiled/001eb710.c)
void EffectManager::setPosition(int id, const float pos[3]) {
    if (ExiInstance* i = find(id)) {
        i->matrix.m[12] = pos[0];
        i->matrix.m[13] = pos[1];
        i->matrix.m[14] = pos[2];
    }
}

// orig FUN_001eb760 (work/decompiled/001eb760.c)
void EffectManager::getPosition(int id, float out[3]) {
    if (ExiInstance* i = find(id)) {
        out[0] = i->matrix.m[12];
        out[1] = i->matrix.m[13];
        out[2] = i->matrix.m[14];
    }
}

// Shared tail of FUN_001eb7b0 / FUN_001eb870 / FUN_001eb940 / FUN_001eba20: matrix = scale(+0x54) * euler(+0x64).
void EffectManager::rebuildMatrix(ExiInstance& inst, bool keepTranslation) {
    const float t[3] = {inst.matrix.m[12], inst.matrix.m[13], inst.matrix.m[14]};
    const float s[3] = {inst.scale[0], inst.scale[1], inst.scale[2]};
    exiScaleMatrix(inst.matrix, s);
    ExiMat r;
    exiIdentity(r);
    exiEulerTableDeg(r, inst.rot);
    exiMul(inst.matrix, inst.matrix, r);
    if (keepTranslation) exiSetTranslation(inst.matrix, t);
}

// orig FUN_001eb7b0 (work/decompiled/001eb7b0.c)
void EffectManager::setPositionRotation(int id, const float pos[3], const float rotDeg[3]) {
    if (ExiInstance* i = find(id)) {
        i->rot[0] = rotDeg[0];
        i->rot[1] = rotDeg[1];
        i->rot[2] = rotDeg[2];
        rebuildMatrix(*i, false);
        exiSetTranslation(i->matrix, pos);
    }
}

// orig FUN_001eb870 (work/decompiled/001eb870.c)
void EffectManager::setRotation(int id, const float rotDeg[3]) {
    if (ExiInstance* i = find(id)) {
        i->rot[0] = rotDeg[0];
        i->rot[1] = rotDeg[1];
        i->rot[2] = rotDeg[2];
        rebuildMatrix(*i, true);
    }
}

// orig FUN_001eb940 (work/decompiled/001eb940.c)
void EffectManager::addRotation(int id, const float rotDeg[3]) {
    if (ExiInstance* i = find(id)) {
        i->rot[0] = i->rot[0] + rotDeg[0];
        i->rot[1] = i->rot[1] + rotDeg[1];
        i->rot[2] = i->rot[2] + rotDeg[2];
        rebuildMatrix(*i, true);
    }
}

// orig FUN_001eba20 (work/decompiled/001eba20.c)
void EffectManager::setScale(int id, const float scale[4]) {
    if (ExiInstance* i = find(id)) {
        i->scale[0] = scale[0];
        i->scale[1] = scale[1];
        i->scale[2] = scale[2];
        i->scale[3] = scale[3];
        rebuildMatrix(*i, true);
    }
}

// orig FUN_001ebaf0 (work/decompiled/001ebaf0.c)
bool EffectManager::setTime(int id, int frame) {
    ExiInstance* i = find(id);
    if (i != nullptr) {
        i->time = frame;
        i->prevTime = frame;
    }
    return i != nullptr;
}

// orig FUN_001ebb30 (work/decompiled/001ebb30.c)
int EffectManager::sequenceLength(int id) {
    ExiInstance* i = find(id);
    if (i == nullptr || i->seq == nullptr) return -1;
    int r = 0;
    for (const ExiNode* n = i->seq; n != nullptr; n = n->next)
        if (static_cast<float>(r) < n->end) r = static_cast<int>(n->end);
    return r;
}

// orig FUN_001ebbb0 (work/decompiled/001ebbb0.c)
void EffectManager::setRate(int id, int rate16) {
    if (ExiInstance* i = find(id)) i->rate = rate16;
}

// orig FUN_001ebbe0 (work/decompiled/001ebbe0.c)
bool EffectManager::loopOut(int id, int16_t count, int16_t end, int restart) {
    ExiInstance* i = find(id);
    if (i == nullptr) return false;
    i->flags8a = 1;
    if (end < 0) end = rawS16(file_.effects[size_t(i->effect)].raw.data(), 0x18);
    i->ovEnd = static_cast<uint16_t>(end);
    if (restart < 0) restart = 0;
    i->ovRestart = static_cast<uint16_t>(restart);
    i->ovCount = count;
    return true;
}

// orig FUN_001eae80 (work/decompiled/001eae80.c)
void EffectManager::killAll() {
    ExiInstance* i = head_;
    while (i != nullptr) {
        ExiInstance* next = i->next;
        freeInstance(i);
        i = next;
    }
}

// orig FUN_001eaed0 (work/decompiled/001eaed0.c; disasm: frees instances whose time >= 9999 (slti 0x270f))
void EffectManager::killFinished() {
    ExiInstance* i = head_;
    while (i != nullptr) {
        ExiInstance* next = i->next;
        if (i->time >= 0x270f) freeInstance(i);
        i = next;
    }
}

// orig FUN_001e8a70 (work/decompiled/001e8a70.c)
void EffectManager::setPaused(bool paused) {
    if (paused) flagsD0_ |= 1u;
    else flagsD0_ &= ~1u;
}

// ---------------------------------------------------------------------------------------------------------------
// Game wrappers
// ---------------------------------------------------------------------------------------------------------------

Effects& effects() {
    static Effects instance;
    return instance;
}

// orig FUN_00111e30 / FUN_001120c0 (work/decompiled/00111e30.c, 001120c0.c): loads DATA/EXI/BQ.EXI through the
// file system object 0x53f814 and FUN_001e8b20. The BQ.PNG atlas (texture table id 0x1c -> 0x48a280[0x70],
// FUN_00106090(0x4e6a68, 0x1c, 1)) is owned by the GL renderer (effects_gl.cpp).
bool Effects::init() {
    const auto path = platform::assetRoot() / "DATA" / "EXI" / "BQ.EXI";
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        platform::log("effects: cannot open " + path.string());
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return mgr_.load(bytes);
}

// orig FUN_00112190 (work/decompiled/00112190.c, disasm: tag in $f12)
int Effects::spawn(float tag, int effectId, const float* pos4, const float* rotDeg, const float* scale) {
    // The original copies a float4 (w + 0.0 as well); FUN_001eb0d0 only reads x, y, z, so w is not read here.
    float p[3] = {0.0f, 0.15f, 0.0f};
    if (pos4 != nullptr) {
        p[0] = pos4[0] + 0.0f;
        p[1] = pos4[1] + 0.15f;
        p[2] = pos4[2] + 0.0f;
    }
    const int id = mgr_.create(effectId, p, rotDeg, scale, nullptr);
    mgr_.setTag(tag, id);
    return id;
}

// orig FUN_001122d0 (work/decompiled/001122d0.c)
void Effects::setPosition(int id, const float pos[3]) {
    const float p[3] = {pos[0] + 0.0f, pos[1] + 0.15f, pos[2] + 0.0f};
    mgr_.setPosition(id, p);
}

// orig FUN_001123f0 (work/decompiled/001123f0.c)
void Effects::setPositionRotation(int id, const float pos[3], const float rotDeg[3]) {
    const float p[3] = {pos[0] + 0.0f, pos[1] + 0.15f, pos[2] + 0.0f};
    mgr_.setPositionRotation(id, p, rotDeg);
}

// orig FUN_00112560 (work/decompiled/00112560.c)
void Effects::stop(int id) {
    if (mgr_.exists(id)) {
        mgr_.loopOut(id, 0, -1, -1);
        mgr_.setTime(id, 9999);
    }
}

}  // namespace baroque::game
