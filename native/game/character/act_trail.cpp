// Weapon trail objects (ACT Track / TrackEx / TrackEnd), see act_trail.hpp. Translated from the disassembly where the
// pseudocode lost operands (FUN_00144de0 0x144de0..0x145560, FUN_001aaa00 0x1aaa00..0x1aaf70, FUN_001aa2d0).
#include "game/character/act_trail.hpp"

#include "alchemy/scene_render.hpp"
#include "game/camera.hpp"
#include "game/character/character.hpp"
#include "game/effects/exi_math.hpp"
#include "game/engine.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/inventory.hpp"
#include "game/items/item.hpp"
#include "game/player_move.hpp"
#include "game/ui/texture_registry.hpp"
#include "platform/platform.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace baroque::game {

namespace {

struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Trail object (FUN_001ab170 ctor, vtable 0x4cde38, 0x5b0 bytes)
struct Trail {
    // +0x04 / +0x08 list links (std::list below), +0x0c callback (always FUN_00144de0 here),
    // +0x10 igVertexArray (0x60 vertices, format 0x10005: position, colour, one texture coordinate set)
    float life = 1.0f;               // +0x14 (FUN_001aa920)
    float lifeStep = 0.0f;           // +0x18 (FUN_001ab0e0)
    float scroll = 0.0f;             // +0x1c texture v scroll
    float speed = 0.0f;              // +0x20 scroll speed
    std::array<V3, 8> tip{};         // +0x24 history of the far point (newest first)
    std::array<V3, 8> base{};        // +0x84 history of the near point
    std::array<V3, 48> tipS{};       // +0xe4 smoothed tip points (FUN_001aa2d0)
    std::array<V3, 48> baseS{};      // +0x324 smoothed base points
    uint32_t flags = 0;              // +0x564: 1 ending, 2 moved this tick, 0x80000000 dead
    uint32_t count = 0;              // +0x568 history length 0..8
    float colA[4] = {0, 0, 0, 0};    // +0x56c colour of the tip vertices
    float colB[4] = {0, 0, 0, 0};    // +0x57c colour of the base vertices
    int slot = 0;                    // +0x58c ACT slot index
    int joint = -1;                  // +0x590
    int axis = 0;                    // +0x594
    int start = 0;                   // +0x598
    int length = 0;                  // +0x59c
    Chr* owner = nullptr;            // +0x5ac ACT player -> +4 (PORT: the character + its serial)
    uint32_t ownerSerial = 0;
};

std::list<std::unique_ptr<Trail>> gTrails;   // 0x53d710 head .. 0x53d718 tail, count 0x53d708 (< 0x10)

// ACT +0x5c[2] per character (PORT: side table keyed by the character, checked by serial)
struct ActSlots {
    uint32_t serial = 0;
    Trail* slot[2] = {nullptr, nullptr};
};
std::unordered_map<const Chr*, ActSlots> gSlots;

ActSlots& slotsOf(Chr& c) {
    ActSlots& s = gSlots[&c];
    if (s.serial != c.serial) s = ActSlots{c.serial, {nullptr, nullptr}};
    return s;
}

bool alive(const Trail& t) {
    for (const Chr* c : characters().list())
        if (c == t.owner) return c->serial == t.ownerSerial;
    return false;
}

// orig FUN_001ab0e0: end the trail over n ticks (n < 2: 2.0 per tick, i.e. gone at the next update)
void trailEnd(Trail& t, int n) {
    t.flags |= 1u;
    if (n < 2) t.lifeStep = 2.0f;
    else t.lifeStep = t.life / static_cast<float>(n);
}

// orig FUN_001aa880 (pool 0x53eb40 FUN_001ab280 -> FUN_001ab170) + FUN_001aa920(t, FUN_00144de0)
Trail* trailCreate() {
    if (gTrails.size() >= 0x10) return nullptr;   // uRam0053d708 < 0x10
    gTrails.push_back(std::make_unique<Trail>());
    return gTrails.back().get();
}

void setColour(float* c, float r, float g, float b, float a) {
    c[0] = r;
    c[1] = g;
    c[2] = b;
    c[3] = a;
}

// orig FUN_001aaf70: a new history entry when the midpoint moved at least 5 units, else the newest one is replaced
void trailPush(Trail& t, const float tipP[3], const float baseP[3]) {
    const float dz = (t.tip[0].z + t.base[0].z) * 0.5f - (tipP[2] + baseP[2]) * 0.5f;   // DAT_00462780 / 88 = 0.5
    const float dy = (t.tip[0].y + t.base[0].y) * 0.5f - (tipP[1] + baseP[1]) * 0.5f;
    const float dx = (t.tip[0].x + t.base[0].x) * 0.5f - (tipP[0] + baseP[0]) * 0.5f;
    if (5.0f <= std::sqrt(dz * dz + dx * dx + dy * dy)) {
        for (int i = 7; i > 0; --i) {
            t.tip[size_t(i)] = t.tip[size_t(i - 1)];
            t.base[size_t(i)] = t.base[size_t(i - 1)];
        }
        t.tip[0] = {tipP[0], tipP[1], tipP[2]};
        t.base[0] = {baseP[0], baseP[1], baseP[2]};
        if (t.count < 8) ++t.count;
        t.flags |= 2u;
    } else {
        t.tip[0] = {tipP[0], tipP[1], tipP[2]};
        t.base[0] = {baseP[0], baseP[1], baseP[2]};
    }
}

// FUN_0039f760(m, in, out, 1): vec4 row-vector transform (only xyz used by the caller; w stays 1)
void xform4(const ExiMat& m, const float in[4], float out[4]) {
    for (int c = 0; c < 4; ++c)
        out[c] = in[0] * m.m[c] + in[1] * m.m[4 + c] + in[2] * m.m[8 + c] + in[3] * m.m[12 + c];
}

ExiMat toExi(const alchemy::Mat4& m) {
    ExiMat e;
    for (int i = 0; i < 16; ++i) e.m[i] = m.m[size_t(i)];
    return e;
}

// PRMITEM row words (table *(0x53d860 + cls*4) + 0x44, row = item +0x28): the original indexes the SD table with the
// equipped item's row whatever its class, and the WM table with the parasite's
int32_t prmWord(int cls, const Item* it, uint32_t offset) {
    if (it == nullptr) return 0;
    return itemSystem().word(cls, it->idx, offset);
}

// orig FUN_00144de0 (trail callback, work/decompiled/00144de0.c; disasm 0x144de0..0x145560)
void trailCallback(Trail& t) {
    Chr* c = alive(t) ? t.owner : nullptr;
    if (c == nullptr || !c->model.loaded() || c->model.parts.empty()) {   // model +0x2c & 0x80000000, +0x30 actor
        trailEnd(t, 0);
        return;
    }
    float startF = 10.0f;    // f22
    float lengthF = 100.0f;  // f21
    uint32_t elem = 0;       // s1
    if (c->id == 0x41303043u) {   // "C00A" (0x44b338)
        const Inventory& inv = inventory();
        const Item* w = nullptr;   // the item tested below (0x53d9b4 / 0x53d9b8 as chosen)
        if (t.slot == 0) {
            if (inv.equip[0] != nullptr) {   // iRam0053d9b4
                w = inv.equip[0];
            } else {
                w = inv.equip[1];            // iRam0053d9b8
            }
        } else {
            if (inv.equip[1] != nullptr) {
                w = inv.equip[1];
            } else {
                w = inv.equip[0];
            }
        }
        if (w != nullptr) {
            elem = static_cast<uint32_t>(prmWord(kClsSD, w, 0x70));
            lengthF = static_cast<float>(prmWord(kClsSD, w, 0x8c));
            if (const Item* p = w->parasite) {   // item +0x38 and its +0x38: PRMITEM WM +0x70
                elem |= static_cast<uint32_t>(prmWord(kClsWM, p, 0x70));
                if (p->parasite != nullptr) elem |= static_cast<uint32_t>(prmWord(kClsWM, p->parasite, 0x70));
            }
        }
        if (const Item* p = static_cast<const Item*>(c->status.block.parasite)) {   // status block +0x5c (+ its +0x38)
            elem |= static_cast<uint32_t>(prmWord(kClsWM, p, 0x70));
            if (p->parasite != nullptr) elem |= static_cast<uint32_t>(prmWord(kClsWM, p->parasite, 0x70));
        }
    } else {
        startF = static_cast<float>(t.start);
        lengthF = static_cast<float>(t.length);
    }
    ExiMat a = toExi(chrActorMatrix(*c));   // FUN_0039d860(a0, model +0x10 +0x20)
    if (c == characters().player()) {
        const PlayerControl* ctl = playerCtl();
        if (ctl != nullptr && (ctl->flags & 0x20u) != 0) {   // uRam0050b620 & 0x20: first-person view
            const float up20[3] = {0.0f, 20.0f, 0.0f};
            exiPostTranslate(a, up20);                       // FUN_0039da90(a0, (0, 20, 0))
            const float pitch = ctl->camera != nullptr ? ctl->camera->pitchRad : 0.0f;   // *(iRam0050b624 + 200)
            ExiMat e;
            exiIdentity(e);                                  // FUN_0039d2e0
            const float t0[3] = {0.0f, -100.0f, 10.0f};
            exiIdentity(e);                                  // FUN_0039d920: identity + translation
            exiSetTranslation(e, t0);
            const float ax[3] = {1.0f, 0.0f, 0.0f};
            ExiMat r;
            exiAxisAngle(-pitch, r, ax);                     // FUN_0039e4e0(-pitch, e0, (1,0,0)): e0 = e0 * R
            exiMul(e, e, r);
            const float t1[3] = {0.0f, 100.0f, -10.0f};
            exiPostTranslate(e, t1);                         // FUN_0039da90
            exiPreMul(a, e);                                 // FUN_0039d640(a0, e0): a0 = e0 * a0
        }
    }
    const std::vector<alchemy::Mat4>& pose = chrTickPose(*c);   // actor +0x30: +0x24 / vt+0x6c bone matrices
    if (t.joint < 0 || static_cast<size_t>(t.joint) >= pose.size()) {
        trailEnd(t, 0);   // PORT guard: the original indexes the bone array without a check
        return;
    }
    ExiMat j = toExi(pose[size_t(t.joint)]);   // FUN_0039d860(m120, bones + joint * 0x40)
    exiMul(j, j, a);                           // FUN_0039d450(m120, m120, a0)
    float p[4];
    exiGetTranslation(j, p);                   // FUN_0039d9a0 (+0xc w: the translation row's 1.0)
    p[3] = j.m[15];
    float ex, ey, ez;
    exiExtractEuler(j, &ex, &ey, &ez);         // FUN_0039e160
    float sv[4] = {0.0f, 0.0f, 0.0f, 1.0f};    // +0xa0 start vector
    float ev[4] = {0.0f, 0.0f, 0.0f, 1.0f};    // +0xb0 end vector
    if (t.axis == 1) {
        ev[1] = lengthF * 0.8f - 0.0f;         // 0x3f4ccccd
        sv[1] = startF;
    } else if (t.axis == 0) {
        ev[0] = lengthF * 0.8f - 0.0f;
        sv[0] = startF;
    } else {
        ev[2] = lengthF * 0.8f - startF;
        sv[2] = startF;
    }
    ExiMat rot;
    exiIdentity(rot);
    exiEuler(ex, ey, ez, rot);                 // FUN_0039de50
    float baseP[4], tipP[4];
    xform4(rot, sv, baseP);                    // FUN_0039f760(rot, start, c0, 1)
    for (int k = 0; k < 4; ++k) baseP[k] = baseP[k] + p[k];
    xform4(rot, ev, tipP);                     // FUN_0039f760(rot, end, d0, 1)
    for (int k = 0; k < 4; ++k) tipP[k] = tipP[k] + baseP[k];
    switch (elem & 7u) {   // element bits of the sword / parasites: 1 fire, 2 ice, 4 lightning
    default:
        setColour(t.colA, 0.5f, 0.5f, 0.5f, 1.0f);
        setColour(t.colB, 0.5f, 0.5f, 0.5f, 1.0f);
        break;
    case 1:
        setColour(t.colA, 1.0f, 0.25f, 0.25f, 1.0f);
        setColour(t.colB, 1.0f, 0.25f, 0.25f, 1.0f);
        break;
    case 2:
        setColour(t.colA, 0.0f, 0.5f, 1.0f, 1.0f);
        setColour(t.colB, 0.0f, 0.5f, 1.0f, 1.0f);
        break;
    case 3:
        setColour(t.colB, 0.0f, 0.5f, 1.0f, 1.0f);
        setColour(t.colA, 1.0f, 0.25f, 0.25f, 1.0f);
        break;
    case 4:
        setColour(t.colA, 0.5f, 0.0f, 1.0f, 1.0f);
        setColour(t.colB, 0.5f, 0.0f, 1.0f, 1.0f);
        break;
    case 5:
        setColour(t.colB, 0.5f, 0.0f, 1.0f, 1.0f);
        setColour(t.colA, 1.0f, 0.25f, 0.25f, 1.0f);
        break;
    case 6:
        setColour(t.colB, 0.5f, 0.0f, 1.0f, 1.0f);
        setColour(t.colA, 0.0f, 0.5f, 1.0f, 1.0f);
        break;
    case 7:
        setColour(t.colB, 0.0f, 1.0f, 0.5f, 1.0f);
        setColour(t.colA, 1.0f, 0.5f, 0.0f, 1.0f);
        break;
    }
    trailPush(t, tipP, baseP);   // FUN_001aaf70(trail, d0 = tip, c0 = base)
}

// orig FUN_001aa2d0 (disasm 0x1aa2d0): cubic B-spline through the n history points, sampled every n * 0.03 from -1
// to n; returns the sample count
int bspline(const std::array<V3, 8>& pts, int n, std::array<V3, 48>& out) {
    const float fn = static_cast<float>(n);
    const float step = fn * 0.03f;   // 0x3cf5c28f
    float t = -1.0f;
    int count = 0;
    if (!(-1.0f <= fn)) return 0;
    do {
        if (count >= 48) break;   // PORT guard (n <= 8 gives at most 45 samples)
        V3& o = out[size_t(count)];
        o = {0.0f, 0.0f, 0.0f};
        if (!(n + 1 < -2)) {
            int k = -2;
            do {
                int idx = k;
                if (k < 0) idx = 0;
                if (n - 1 < k) idx = n - 1;
                float d = t - static_cast<float>(k);
                if (d < 0.0f) d = -d;
                float w;
                if (d < 1.0f) {
                    const float d2 = d * d;
                    w = 0.666f + ((0.5f * d2) * d - d2);          // 0x3f2a7efa
                } else if (d < 2.0f) {
                    const float e = d - 2.0f;
                    w = 0.16666667f * (e * (-e * e));              // 0x3e2aaaab
                } else {
                    w = 0.0f;
                }
                ++k;
                const V3& p = pts[size_t(idx < 0 ? 0 : idx)];
                o.x = (0.0f + o.x) + p.x * w;                      // adda.s / madd.s
                o.y = (0.0f + o.y) + p.y * w;
                o.z = (0.0f + o.z) + p.z * w;
            } while (!(n + 1 < k));
        }
        t = t + step;
        ++count;
    } while (t <= fn);
    return count;
}

// PORT: one recorded strip of the tick (drawn by actTrailsReplay)
struct StripVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};
struct Strip {
    std::vector<StripVertex> v;
};
std::vector<Strip> gRecorded;

// orig FUN_001aaa00: returns true when the trail is dead (unlinked and freed by the caller)
bool trailUpdate(Trail& t, bool paused) {
    if (!paused) {
        trailCallback(t);   // (*(t +0xc))(t)
        if ((t.flags & 1u) != 0) {
            t.life = t.life - t.lifeStep;
            if (t.life <= 0.0f) t.flags |= 0x80000000u;
        }
    }
    if ((t.flags & 0x80000000u) != 0) return true;
    if (static_cast<int>(t.count) < 3) {
        t.flags &= 0xfffffffdu;
        return false;
    }
    bspline(t.base, static_cast<int>(t.count), t.baseS);
    const int n = bspline(t.tip, static_cast<int>(t.count), t.tipS);
    // the strip is moved so that its first smoothed sample sits on the newest point; sample 0 := sample 1
    const float f25 = t.tip[0].z - t.tipS[1].z;
    const float f24 = t.tip[0].y - t.tipS[1].y;
    const float f23 = t.tip[0].x - t.tipS[1].x;
    const float f22 = t.base[0].z - t.baseS[1].z;
    const float f21 = t.base[0].y - t.baseS[1].y;
    const float f20 = t.base[0].x - t.baseS[1].x;
    t.tipS[0] = {t.tip[0].x - f23, t.tip[0].y - f24, t.tip[0].z - f25};
    t.baseS[0] = {t.base[0].x - f20, t.base[0].y - f21, t.base[0].z - f22};
    const int half = (n < 0 ? n + 1 : n) >> 1;
    const float alphaStep = -(1.0f / static_cast<float>(half - 1));   // f28
    const float vStep = -(1.0f / static_cast<float>(n - 1));          // f26
    if ((t.flags & 2u) != 0) {
        const float dy = t.tipS[0].y - t.tipS[1].y;
        const float dx = t.tipS[0].x - t.tipS[1].x;
        const float dz = t.tipS[0].z - t.tipS[1].z;
        const float dist = std::fabs(std::sqrt(dy * dy + dx * dx + dz * dz));
        t.speed = t.speed - (t.speed - dist / 200.0f) / 4.0f;
        const float s = t.scroll + t.speed;
        t.scroll = s + 0.16f;   // 0x3e23d70a (the intermediate store of s is overwritten)
    } else if (!paused) {
        t.scroll = t.scroll + 0.025f;   // 0x3ccccccd
    }
    if (2.0f < t.scroll) t.scroll = t.scroll - 2.0f;
    Strip strip;
    strip.v.reserve(size_t(n) * 2);
    float fade = 1.0f;   // f29
    float vv = 1.0f;     // f27
    for (int i = 0; i < n; ++i) {
        const V3& tp = t.tipS[size_t(i)];
        const V3& bp = t.baseS[size_t(i)];
        const float aA = fade * (t.colA[3] * t.life);
        const float aB = fade * (t.colB[3] * t.life);
        const float v = vv + t.scroll;
        // vertex 2i: tip, colour A, uv (0, v); vertex 2i+1: base, colour B, uv (1, v)
        strip.v.push_back({tp.x + f23, tp.y + f24, tp.z + f25, 0.0f, v, t.colA[0], t.colA[1], t.colA[2], aA});
        strip.v.push_back({bp.x + f20, bp.y + f21, bp.z + f22, 1.0f, v, t.colB[0], t.colB[1], t.colB[2], aB});
        vv = vv + vStep;
        if (vv < 0.0f) vv = 0.0f;
        if (!(static_cast<uint32_t>(i) < static_cast<uint32_t>(half))) {
            fade = fade + alphaStep;
            if (fade < 0.0f) fade = 0.0f;
        }
    }
    // vt+0xd0 (vertex array) + vt+0x100(4 = triangle strip, 2n - 2 primitives, 0)
    if (n >= 2) gRecorded.push_back(std::move(strip));
    t.flags &= 0xfffffffdu;
    return false;
}

// ---- rlgl backend of FUN_001aa530 ---------------------------------------------------------------------------------
// Render state of FUN_001aa530 (igVisualContext 0x4db6a0 slots, RUNTIME.md 1.2): view matrix stack pushed and set to
// the camera (+0x2f4(1), +0x2fc(1, camera +0x64)), colour mask (1, 1, 1, 0) (+0x348), blending on (+0x238), blend
// (4, 1) = Cs*As + Cd (+0x250), texture unit 0 on (+0x1b8(0, 1)), depth write off (+0x378(0)), texture attr of id 0x11d
// (+0x1e0); everything restored afterwards. Texture attr set by FUN_001aa470: slots +0x84..+0x90 all 1 (magnification /
// minification LINEAR, wrap s / t REPEAT; UNVERIFIED slot order). Vertex colour: the engine default scale (RGB
// 128/255, A 1.0: float 1.0 = GS 0x80), GS MODULATE: out = texel * colour; the 64x64 palette PNG has no tRNS (alpha 0x80
// = 1.0 after the loader's halving). UNVERIFIED: depth test / cull / fog are not set by the pass (left as the EXI pass
// restored them): depth test on, culling off (the strip is seen from both sides), no fog.
const char* kVs = R"GLSL(
#version 330
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 2) in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragUV;
out vec4 fragColor;
void main() {
    fragUV = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";
const char* kFs = R"GLSL(
#version 330
in vec2 fragUV;
in vec4 fragColor;
uniform sampler2D tex;
out vec4 finalColor;
void main() {
    vec4 t = texture(tex, fragUV);
    finalColor = vec4(min(t.rgb * fragColor.rgb, vec3(1.0)), min(t.a * fragColor.a, 1.0));
}
)GLSL";

struct TrailRenderer {
    unsigned int program = 0, vao = 0, vbo = 0, ebo = 0, tex = 0;
    int locMvp = -1, locTex = -1;
    bool failed = false;
    static constexpr int kMaxVerts = 0x60;   // igVertexArray size of FUN_001aa920
    bool init() {
        if (program != 0) return true;
        if (failed) return false;
        failed = true;
        program = rlLoadShaderCode(kVs, kFs);
        if (program == 0) return false;
        locMvp = rlGetLocationUniform(program, "mvp");
        locTex = rlGetLocationUniform(program, "tex");
        vao = rlLoadVertexArray();
        rlEnableVertexArray(vao);
        vbo = rlLoadVertexBuffer(nullptr, int(kMaxVerts * sizeof(StripVertex)), true);
        rlSetVertexAttribute(0, 3, RL_FLOAT, false, int(sizeof(StripVertex)), int(offsetof(StripVertex, x)));
        rlEnableVertexAttribute(0);
        rlSetVertexAttribute(1, 2, RL_FLOAT, false, int(sizeof(StripVertex)), int(offsetof(StripVertex, u)));
        rlEnableVertexAttribute(1);
        rlSetVertexAttribute(2, 4, RL_FLOAT, false, int(sizeof(StripVertex)), int(offsetof(StripVertex, r)));
        rlEnableVertexAttribute(2);
        std::vector<uint16_t> idx(size_t(kMaxVerts) * 3, 0);
        ebo = rlLoadVertexBufferElement(idx.data(), int(idx.size() * sizeof(uint16_t)), true);
        rlDisableVertexArray();
        // FUN_001aa470: FUN_00106090(0x4e6a68, 0x11d, 1) = "DATA/TEX_EN/C_SMOKE.PNG" / "DATA/TEX/C_SMOKE.PNG"
        const auto path = platform::assetRoot() / ui::textures().path(0x11d);
        Image img = LoadImage(path.string().c_str());
        if (img.data == nullptr) {
            platform::log("trails: cannot load " + path.string());
            return false;
        }
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        tex = rlLoadTexture(img.data, img.width, img.height, img.format, 1);
        UnloadImage(img);
        if (tex == 0) return false;
        rlTextureParameters(tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
        rlTextureParameters(tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
        rlTextureParameters(tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT);
        rlTextureParameters(tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_REPEAT);
        failed = false;
        return true;
    }
};
TrailRenderer gRenderer;

Matrix toRaylib(const ExiMat& r) {
    Matrix o;
    o.m0 = r.m[0]; o.m1 = r.m[1]; o.m2 = r.m[2]; o.m3 = r.m[3];
    o.m4 = r.m[4]; o.m5 = r.m[5]; o.m6 = r.m[6]; o.m7 = r.m[7];
    o.m8 = r.m[8]; o.m9 = r.m[9]; o.m10 = r.m[10]; o.m11 = r.m[11];
    o.m12 = r.m[12]; o.m13 = r.m[13]; o.m14 = r.m[14]; o.m15 = r.m[15];
    return o;
}

}  // namespace

// orig FUN_00145560 (work/decompiled/00145560.c): a busy slot is ended first (FUN_001456b0), then a new trail with the
// grey colours, slot, joint, axis, start, length and the ACT player
void actTrackStart(Chr& owner, int slot, int joint, int axis, int start, int length) {
    if (slot < 0 || slot > 1) return;   // PORT guard: two slots (+0x5c, +0x60)
    ActSlots& s = slotsOf(owner);
    if (s.slot[slot] != nullptr) {      // FUN_001456b0(act, slot)
        trailEnd(*s.slot[slot], 4);
        s.slot[slot] = nullptr;
    }
    Trail* t = trailCreate();
    s.slot[slot] = t;
    if (t == nullptr) return;
    setColour(t->colA, 0.5f, 0.5f, 0.5f, 1.0f);   // +0x56c..+0x578 (+0x57c stays 0 until the first callback)
    t->slot = slot;
    t->joint = joint;
    t->axis = axis;
    t->start = start;
    t->length = length;
    t->owner = &owner;
    t->ownerSerial = owner.serial;
}

// orig FUN_00149390 (TrackEnd [0] slot, [1] ticks)
void actTrackEnd(Chr& owner, int slot, int frames) {
    if (slot < 0 || slot > 1) return;
    ActSlots& s = slotsOf(owner);
    if (s.slot[slot] != nullptr) {
        trailEnd(*s.slot[slot], frames);
        s.slot[slot] = nullptr;
    }
}

// orig FUN_00145660 -> FUN_001456b0(act, 0..1): FUN_001ab0e0(trail, 4)
void actTrackReset(Chr& owner) {
    const auto it = gSlots.find(&owner);
    if (it == gSlots.end() || it->second.serial != owner.serial) return;
    for (Trail*& t : it->second.slot) {
        if (t == nullptr) continue;
        trailEnd(*t, 4);
        t = nullptr;
    }
}

// orig FUN_001aa530 (work/decompiled/001aa530.c) update part: FUN_001aaa00 per trail, dead ones unlinked + freed
void actTrailsTick() {
    gRecorded.clear();
    const bool paused = engine().paused();   // FUN_00102020(0x4e6640)
    for (auto it = gTrails.begin(); it != gTrails.end();) {
        if (trailUpdate(**it, paused)) {
            Trail* dead = it->get();
            for (auto& [chr, s] : gSlots)   // PORT: no ACT slot keeps a freed trail (the original slot is cleared first)
                for (Trail*& t : s.slot)
                    if (t == dead) t = nullptr;
            it = gTrails.erase(it);   // FUN_001aa9c0 (vertex array release) + FUN_001ab120 (dtor, pool free)
        } else {
            ++it;
        }
    }
}

void actTrailsReplay(const ExiView& view) {
    if (gRecorded.empty() || !gRenderer.init()) return;
    rlDrawRenderBatchActive();
    rlEnableShader(gRenderer.program);
    ExiMat mvp;
    exiMul(mvp, view.view, view.projection);
    rlSetUniformMatrix(gRenderer.locMvp, toRaylib(mvp));
    const int zero = 0;
    rlSetUniform(gRenderer.locTex, &zero, RL_SHADER_UNIFORM_INT, 1);
    rlColorMask(true, true, true, false);                                   // +0x348(1, 1, 1, 0)
    rlEnableColorBlend();                                                   // +0x238(1)
    rlSetBlendFactors(RL_SRC_ALPHA, RL_ONE, RL_FUNC_ADD);                   // +0x250(4, 1): Cs*As + Cd
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlEnableShader(gRenderer.program);   // rlSetBlendMode may flush raylib's batch and unbind the program
    rlEnableDepthTest();
    rlDisableDepthMask();                                                   // +0x378(0)
    rlDisableBackfaceCulling();
    rlActiveTextureSlot(0);
    rlEnableTexture(gRenderer.tex);                                         // +0x1e0(texture attr of 0x11d)
    std::vector<uint16_t> idx;
    for (const Strip& s : gRecorded) {
        const int n = static_cast<int>(std::min<size_t>(s.v.size(), size_t(TrailRenderer::kMaxVerts)));
        if (n < 3) continue;
        rlUpdateVertexBuffer(gRenderer.vbo, s.v.data(), int(size_t(n) * sizeof(StripVertex)), 0);
        idx.clear();
        for (int i = 0; i + 2 < n; ++i) {   // triangle strip -> triangle list
            idx.push_back(uint16_t(i));
            idx.push_back(uint16_t(i + 1));
            idx.push_back(uint16_t(i + 2));
        }
        rlEnableVertexArray(gRenderer.vao);
        rlUpdateVertexBufferElements(gRenderer.ebo, idx.data(), int(idx.size() * sizeof(uint16_t)), 0);
        rlDrawVertexArrayElements(0, int(idx.size()), nullptr);
        rlDisableVertexArray();
    }
    rlDisableTexture();
    rlDisableShader();
    rlColorMask(true, true, true, true);
    rlSetBlendMode(RL_BLEND_ALPHA);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void actTrailsClear() {
    gTrails.clear();
    gSlots.clear();
    gRecorded.clear();
}

int actTrailsLive() { return static_cast<int>(gTrails.size()); }

std::string actTrailsDescribe() {
    std::string out;
    char buf[256];
    for (const auto& t : gTrails) {
        const float dx = t->tip[0].x - t->base[0].x, dy = t->tip[0].y - t->base[0].y, dz = t->tip[0].z - t->base[0].z;
        const char* act = alive(*t) && t->owner->act.currentName() != nullptr ? t->owner->act.currentName() : "-";
        std::snprintf(buf, sizeof buf, " [%s slot %d n=%u life=%.2f width=%.1f tip=(%.0f,%.0f,%.0f) A=(%.2f,%.2f,%.2f) B=(%.2f,%.2f,%.2f)]",
                      act, t->slot, t->count, t->life, std::sqrt(dx * dx + dy * dy + dz * dz), t->tip[0].x, t->tip[0].y,
                      t->tip[0].z, t->colA[0], t->colA[1], t->colA[2], t->colB[0], t->colB[1], t->colB[2]);
        out += buf;
    }
    return out;
}

}  // namespace baroque::game
