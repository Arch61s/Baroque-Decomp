// EXI draw pass (FUN_001e7e90) and per-particle vertex build (FUN_001ebd90). GPU submission is done by the
// ExiBatchSink (effects_gl.cpp); this file is GL-free so the headless check runs the identical pass.
#include "game/effects/effects.hpp"

#include <cmath>
#include <cstring>
#include <utility>

namespace baroque::game {

namespace {

const float* vtx(const data::ExiFile& f, uint32_t ref, int i) {
    const size_t k = size_t(ref) + size_t(i);
    static const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (k >= f.vertices.size()) return zero;  // port guard
    return reinterpret_cast<const float*>(f.vertices[k].raw.data());
}

// Float -> colour byte packing of FUN_001ebd90 (the `< 2147483648.0` split is the compiler's unsigned conversion).
uint32_t toUnsignedBits(float f) {
    if (f < 2147483648.0f) return static_cast<uint32_t>(static_cast<int32_t>(f));
    return static_cast<uint32_t>(static_cast<int32_t>(f - 2147483648.0f)) | 0x80000000u;
}
uint32_t toSignedBits(float f) {
    if (!(f < 2147483648.0f)) f = f - 2147483648.0f;
    return static_cast<uint32_t>(static_cast<int32_t>(f));
}

float len3(float x, float y, float z) { return std::sqrt(z * z + x * x + y * y); }

}  // namespace

// orig FUN_001ebd90 (work/decompiled/001ebd90.c). `parent` = instance matrix (+0x14) or null (world particles).
bool EffectManager::buildParticle(float tag, const ExiView& view, const ExiNode& node, const ExiMat* parent,
                                  ExiDrawVertex* out) const {
    const int o = node.ref;
    const auto& obj = file_.objects[size_t(o)];
    const float t = node.t;
    const auto ev = [&](int off) { return evalTrack(t, objectTrack(o, off)); };
    ExiMat L;
    exiIdentity(L);
    ExiMat P;
    exiIdentity(P);
    const float tx = ev(0x2c), ty = ev(0x34), tz = ev(0x3c);
    const float sx = ev(0x44), sy = ev(0x4c), sz = ev(0x54);
    const float rx = ev(0x5c), ry = ev(0x64), rz = ev(0x6c);
    float uScale, vScale, uOff, vOff;
    if ((obj.allOnes8 & 1u) == 0) {
        uScale = ev(0xa4);
        vScale = ev(0xac);
        uOff = ev(0x94);
        vOff = ev(0x9c);
    } else {
        uScale = evalTrack(0.0f, objectTrack(o, 0xa4));
        vScale = evalTrack(0.0f, objectTrack(o, 0xac));
        uOff = evalTrack(0.0f, objectTrack(o, 0x94));
        vOff = evalTrack(0.0f, objectTrack(o, 0x9c));
    }
    float R = ev(0x74);
    const float Graw = ev(0x7c);
    float B = ev(0x84);
    const float Araw = ev(0x8c);
    if (1.0f < R) R = 1.0f;
    float G = 1.0f;
    if (Graw <= 1.0f) G = Graw;
    if (1.0f < B) B = 1.0f;
    float A = 1.0f;
    if (Araw * 1.9921875f <= 1.0f) A = Araw * 1.9921875f;

    const float pi = 3.1415927f;
    const float a = rx * 2.0f * pi;
    const float sa = exiSin(a);
    const float b = ry * 2.0f * pi;
    const float sb = exiSin(b);
    const float c = rz * 2.0f * pi;
    const float sc = exiSin(c);
    const float ca = exiCos(a);
    const float cb = exiCos(b);
    const float cc = exiCos(c);
    L.m[0] = (cb * cc + sc * sa * sb) * sx;
    L.m[1] = ca * sc * sx;
    L.m[2] = (-sb * cc + sc * sa * cb) * sx;
    L.m[3] = 0.0f;
    L.m[4] = (-cb * sc + cc * sa * sb) * sy;
    L.m[5] = ca * cc * sy;
    L.m[6] = (sb * sc + cc * sa * cb) * sy;
    L.m[7] = 0.0f;
    L.m[8] = ca * sb * sz;
    L.m[9] = -sa * sz;
    L.m[10] = ca * cb * sz;
    L.m[11] = 0.0f;
    L.m[12] = tx;
    L.m[13] = ty;
    L.m[14] = tz;
    L.m[15] = 1.0f;

    if (parent == nullptr) {
        exiCopy(P, node.matrix);
    } else if (tag < 0.0f) {
        // Camera-aligned parent: parent rotation pre-multiplied by the camera (+0x60), parent translation kept.
        ExiMat Q;
        exiCopy(Q, *parent);
        Q.m[12] = 0.0f;
        Q.m[13] = 0.0f;
        Q.m[14] = 0.0f;
        exiPreMul(Q, camera_);
        Q.m[12] = parent->m[12];
        Q.m[13] = parent->m[13];
        Q.m[14] = parent->m[14];
        exiMul(P, node.matrix, Q);
        tag = -tag;
    } else {
        exiMul(P, node.matrix, *parent);
    }
    exiMul(L, L, P);
    if (tag != 0.0f) {
        const float v[3] = {0.0f, 0.0f, tag};
        float w[3];
        exiTransformVector(w, v, camera_);  // FUN_003a0330(.., mgr+0x60)
        exiPostTranslate(L, w);             // FUN_0039da90
    }

    // Distance fade against the fog range (fog attr 0x4e790c +0x10 start, +0x14 end).
    float camPos[3], pPos[3];
    exiGetTranslation(camera_, camPos);
    exiGetTranslation(L, pPos);
    const float dx = camPos[0] - pPos[0], dy = camPos[1] - pPos[1], dz = camPos[2] - pPos[2];
    const float dist = std::sqrt(dz * dz + dx * dx + dy * dy);
    const float fogStart = view.fogStart;
    const float fadeStart = view.fogEnd - (view.fogEnd - fogStart) / 2.5f;
    if (fogStart <= dist) {
        if (fadeStart <= dist) A = 0.0f;
        else A = A * ((fadeStart - dist) / (fadeStart - fogStart));
    }
    if (A == 0.0f) return false;

    const uint8_t b6 = objectByte(o, 6);
    const uint8_t b7 = objectByte(o, 7);
    const bool pivot = (b7 & 0x80) != 0;
    float off[3] = {0.0f, 0.0f, 0.0f};
    if (pivot) {
        off[0] = L.m[12] - P.m[12];
        off[1] = L.m[13] - P.m[13];
        off[2] = L.m[14] - P.m[14];
    }
    const int billboard = (b6 & 0x7f) >> 4;
    const auto pivotPosition = [&](float outPos[3]) {
        // FUN_0039e160 on the camera, first angle * 57.29578 (degrees) handed to FUN_0039de50 as the Y angle, which
        // takes radians: original behaviour, kept as is.
        float e0 = 0.0f, e1 = 0.0f, e2 = 0.0f;
        exiExtractEuler(camera_, &e0, &e1, &e2);
        e0 = e0 * 57.29578f;
        ExiMat yawM;
        exiIdentity(yawM);
        exiEuler(0.0f, e0, 0.0f, yawM);
        exiTransformVector(outPos, off, yawM);
        outPos[0] = outPos[0] + P.m[12];
        outPos[1] = outPos[1] + P.m[13];
        outPos[2] = outPos[2] + P.m[14];
    };
    if (billboard == 1) {
        // Y-axis billboard: axes replaced by the world axes (scales kept), then the camera rotation with its
        // row 1 forced to (0,1,0).
        const float l0 = len3(L.m[0], L.m[1], L.m[2]);
        const float l1 = len3(L.m[4], L.m[5], L.m[6]);
        const float l2 = len3(L.m[8], L.m[9], L.m[10]);
        const float yAxis[3] = {0.0f, 1.0f, 0.0f};
        const float zAxis[3] = {0.0f, 0.0f, 1.0f};
        float x[3];
        exiCross(x, yAxis, zAxis);
        const float inv = 1.0f / len3(x[0], x[1], x[2]);
        L.m[4] = yAxis[0] * l1;
        L.m[5] = yAxis[1] * l1;
        L.m[0] = x[0] * inv * l0;
        L.m[6] = yAxis[2] * l1;
        L.m[1] = x[1] * inv * l0;
        L.m[8] = l2 * zAxis[0];
        L.m[9] = l2 * zAxis[1];
        L.m[10] = l2 * zAxis[2];
        L.m[2] = x[2] * inv * l0;
        float np[3];
        if (pivot) pivotPosition(np);
        else exiGetTranslation(L, np);
        ExiMat C;
        exiCopy(C, camera_);
        C.m[5] = 1.0f;
        C.m[4] = 0.0f;
        C.m[6] = 0.0f;
        exiMul(L, L, C);
        exiSetTranslation(L, np);
    } else if (billboard == 2 || billboard == 3) {
        // Full billboard: keep the roll of the local Y axis projected on the XY plane, then the camera rotation.
        const float zAxis[3] = {0.0f, 0.0f, 1.0f};
        const float l1 = len3(L.m[4], L.m[5], L.m[6]);
        const float l0 = len3(L.m[0], L.m[1], L.m[2]);
        float bx[3] = {L.m[0], L.m[1], L.m[2]};
        float inv = 1.0f / l1;
        float cy[3] = {L.m[4] * inv, L.m[5] * inv, L.m[6] * inv};
        const float l2 = len3(L.m[8], L.m[9], L.m[10]);
        exiCross(bx, cy, zAxis);
        inv = 1.0f / len3(bx[0], bx[1], bx[2]);
        bx[0] = bx[0] * inv;
        bx[1] = bx[1] * inv;
        bx[2] = bx[2] * inv;
        exiCross(cy, zAxis, bx);
        inv = 1.0f / len3(cy[0], cy[1], cy[2]);
        L.m[0] = bx[0] * l0;
        L.m[1] = bx[1] * l0;
        L.m[2] = bx[2] * l0;
        L.m[4] = cy[0] * inv * l1;
        L.m[8] = l2 * zAxis[0];
        L.m[9] = l2 * zAxis[1];
        L.m[10] = l2 * zAxis[2];
        L.m[5] = cy[1] * inv * l1;
        L.m[6] = cy[2] * inv * l1;
        float np[3];
        if (pivot) pivotPosition(np);
        else exiGetTranslation(L, np);
        exiMul(L, L, camera_);
        exiSetTranslation(L, np);
    }

    // Vertices (LAB_001ec950): VTXS streams +0x1c position, +0x20 normal, +0x24 colour, +0x28 UV.
    const int count = rawVertexCount(o);
    const int uvMode = (b7 & 0x7f) >> 3;
    for (int i = 0; i < count; ++i) {
        ExiDrawVertex& vo = out[i];
        float p[3];
        exiTransformPoint(p, vtx(file_, obj.vertexRef[0], i), L);
        vo.x = p[0];
        vo.y = p[1];
        vo.z = p[2];
        if (uvMode == 8) {
            // Screen-space UV into the frame copy (texture slot 1): view position + normal * 10, projected.
            float vp[3];
            exiTransformPoint(vp, p, view.view);
            const float* n = vtx(file_, obj.vertexRef[1], i);
            vp[1] = vp[1] + n[1] * 10.0f;
            vp[2] = vp[2] + n[2] * 10.0f;
            vp[0] = vp[0] + n[0] * 10.0f;
            float ndc[3];
            exiTransformPoint(ndc, vp, view.projection);
            const int vx = view.viewport[0], vy = view.viewport[1], vw = view.viewport[2], vh = view.viewport[3];
            float u = (static_cast<float>(vx) + ((ndc[0] + 1.0f) * static_cast<float>(vw)) / 2.0f) / 512.0f;
            float v = (static_cast<float>(vh) -
                       (static_cast<float>(vy) + ((ndc[1] + 1.0f) * static_cast<float>(vh)) / 2.0f)) / 512.0f;
            if (1.0f < u) u = 1.0f;
            else if (u < 0.0f) u = 0.0f;
            if (0.875f < v) v = 0.875f;
            else if (v < 0.0f) v = 0.0f;
            vo.u = u;
            vo.v = v;
        } else if (uvMode == 7) {
            // Sphere-map style UV from the view-space normal into the 1/4 x 1/8 corner (region-repeated).
            const float* n = vtx(file_, obj.vertexRef[1], i);
            float nn[3] = {n[0], n[1], n[2]};
            exiTransformVector(nn, nn, L);
            exiTransformVector(nn, nn, view.view);
            const float inv = 1.0f / len3(nn[0], nn[1], nn[2]);
            nn[0] = nn[0] * inv;
            nn[2] = nn[2] * inv;
            nn[1] = nn[1] * inv;
            vo.u = (nn[0] + 1.0f) * 0.5f * 0.25f;
            vo.v = (1.0f - nn[1]) * 0.5f * 0.125f;
        } else {
            const float* uv = vtx(file_, obj.vertexRef[3], i);
            vo.u = uScale * (uv[0] + uOff);
            vo.v = vScale * (uv[1] + vOff);
        }
        const float* col = vtx(file_, obj.vertexRef[2], i);
        const float fa = col[3] * A;
        const float fb = col[2] * B;
        const float fg = col[1] * G;
        const float fr = col[0] * R;
        vo.rgba = toUnsignedBits(fr) | (toSignedBits(fg) << 8) | (toSignedBits(fa) << 24) | (toSignedBits(fb) << 16);
    }
    return true;
}

int EffectManager::rawVertexCount(int object) const { return file_.objects[size_t(object)].vertexCount; }

// orig FUN_001e7e90 (work/decompiled/001e7e90.c). Order kept: per instance, its sequence nodes are drawn, then the
// instance is updated (FUN_001e95c0) and freed when finished (FUN_001e9310). Vertex budget mgr+0xe0 = 0x1400 per
// pass; FUN_001ebcd0 / FUN_001ece50 (lock / unlock of the vertex streams) are the scratch writes, FUN_001ecec0 the
// sink draw. The double-buffer half select (+0xdc) only picks the vertex array half and is not modelled.
ExiPassStats EffectManager::drawPass(const ExiView& view, ExiBatchSink* sink, ExiRecording* record) {
    ExiPassStats st;
    if (head_ == nullptr || !loaded_ || file_.objects.empty()) return st;
    ExiMat inv;
    if (exiInvert(inv, view.view)) exiCopy(camera_, inv);  // FUN_0039f1d0(+0x20, *(+0x1c)); FUN_0039d860(+0x60, +0x20)
    int current = -1;  // mgr+0x10 = 0
    int used = 0;      // iVar28
    std::vector<ExiParticleJob> jobs;   // PORT recording: build attempts of the current batch
    const auto setState = [&](const ExiRenderState& rs) {
        if (current != rs.object) {
            current = rs.object;
            if (sink != nullptr) sink->setState(rs);
            if (record != nullptr) record->push_back({true, rs, {}});
        }
    };
    // FUN_001ebd90 call (PORT: recorded with its inputs when `record` is set)
    const auto build = [&](float tag, const ExiNode& node, const ExiMat* parent, ExiDrawVertex* out) {
        if (record != nullptr) {
            ExiParticleJob j;
            j.tag = tag;
            j.node = node;
            j.node.firstChild = j.node.lastChild = j.node.next = nullptr;
            j.hasParent = parent != nullptr;
            if (parent != nullptr) j.parent = *parent;
            jobs.push_back(j);
        }
        return buildParticle(tag, view, node, parent, out);
    };
    const auto submit = [&](const ExiRenderState& rs, int first, int count) {
        if (record != nullptr && !jobs.empty()) {
            record->push_back({false, rs, std::move(jobs)});
            jobs.clear();
        }
        if (count == 0) return;  // FUN_001ecec0 draws nothing for 0
        st.batches += 1;
        st.vertices += count;
        if (sink != nullptr) sink->draw(rs, &scratch_[size_t(first)], count);
    };
    // Particles of one emitter node into the current batch (inner loop shared by the emitter and group cases).
    const auto emitterParticles = [&](const ExiInstance& inst, const ExiNode& em, int nv, int& written) {
        const ExiMat* parent = (emitterS16(em.ref, 0xa) == 0) ? nullptr : &inst.matrix;
        for (const ExiNode* p = em.firstChild; p != nullptr && used + nv <= kExiMaxVertices; p = p->next) {
            if (build(inst.tag, *p, parent, &scratch_[size_t(used)])) {
                st.primitives += 1;
                used += nv;
                written += nv;
            }
        }
    };

    ExiInstance* inst = head_;
    while (inst != nullptr) {
        st.instances += 1;
        for (ExiNode* node = inst->seq; node != nullptr; node = node->next) {
            if (node->kind == 2) {
                if (node->firstChild == nullptr) continue;
                const ExiRenderState rs = renderState(static_cast<int>(file_.emitters[size_t(node->ref)].object));
                setState(rs);
                const int nv = rs.vertsPerPrim;
                const int total = nv * node->childCount;
                if (total != 0 && used + nv <= kExiMaxVertices) {
                    const int first = used;
                    int written = 0;
                    emitterParticles(*inst, *node, nv, written);
                    submit(rs, first, written);
                }
            } else if (node->kind == 1) {
                const ExiRenderState rs = renderState(node->ref);
                setState(rs);
                const float now = static_cast<float>(inst->time);
                if (node->start <= now && now <= node->end) {
                    const int nv = rs.vertsPerPrim;
                    const int first = used;
                    int written = 0;
                    if (nv != 0 && used + nv <= kExiMaxVertices) {
                        if (build(inst->tag, *node, &inst->matrix, &scratch_[size_t(used)])) {
                            st.primitives += 1;
                            used += nv;
                            written = nv;
                        }
                    }
                    submit(rs, first, written);
                }
            } else if (node->kind == 0 && node->firstChild != nullptr) {
                ExiNode* child = node->firstChild;
                if (child->kind == 1) {
                    const ExiRenderState rs = renderState(child->ref);
                    setState(rs);
                    const int first = used;
                    int written = 0;
                    const float now = static_cast<float>(inst->time);
                    for (; child != nullptr; child = child->next) {
                        const int nv = rs.vertsPerPrim;
                        if (child->start <= now && now <= child->end && nv != 0 && used + nv <= kExiMaxVertices) {
                            if (build(inst->tag, *child, &inst->matrix, &scratch_[size_t(used)])) {
                                st.primitives += 1;
                                used += nv;
                                written += nv;
                            }
                        }
                    }
                    submit(rs, first, written);
                } else if (child->kind == 2) {
                    const ExiRenderState rs =
                        renderState(static_cast<int>(file_.emitters[size_t(child->ref)].object));
                    setState(rs);
                    const int first = used;
                    int written = 0;
                    for (; child != nullptr; child = child->next) {
                        const int nv = rs.vertsPerPrim;
                        const int total = nv * child->childCount;
                        if (total != 0 && used + nv <= kExiMaxVertices) emitterParticles(*inst, *child, nv, written);
                    }
                    submit(rs, first, written);
                }
            }
        }
        const bool alive = updateInstance(*inst);
        ExiInstance* next = inst->next;
        if (!alive) freeInstance(inst);
        inst = next;
    }
    return st;
}

// PORT: replay of a recorded FUN_001e7e90 pass with the presentation's camera (see effects.hpp). Same per-particle
// budget test (`used + nv <= mgr+0xe0`) and batch submission as the pass; the instance update is not repeated.
ExiPassStats EffectManager::replay(const ExiView& view, const ExiRecording& record, ExiBatchSink* sink) {
    ExiPassStats st;
    if (!loaded_ || file_.objects.empty()) return st;
    ExiMat inv;
    if (exiInvert(inv, view.view)) exiCopy(camera_, inv);
    int used = 0;
    for (const ExiRecordedCall& c : record) {
        if (c.isState) {
            if (sink != nullptr) sink->setState(c.state);
            continue;
        }
        const int nv = c.state.vertsPerPrim;
        const int first = used;
        int written = 0;
        for (const ExiParticleJob& j : c.jobs) {
            if (nv == 0 || used + nv > kExiMaxVertices) continue;
            if (buildParticle(j.tag, view, j.node, j.hasParent ? &j.parent : nullptr, &scratch_[size_t(used)])) {
                st.primitives += 1;
                used += nv;
                written += nv;
            }
        }
        if (written == 0) continue;
        st.batches += 1;
        st.vertices += written;
        if (sink != nullptr) sink->draw(c.state, &scratch_[size_t(first)], written);
    }
    return st;
}

}  // namespace baroque::game
