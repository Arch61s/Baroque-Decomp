// Player shadow pre-pass, renderer +0xd4 (see shadow_pass.hpp).
#include "game/post/shadow_pass.hpp"

#include "game/character/character.hpp"
#include "game/character/model_fx.hpp"
#include "game/post/post_gl.hpp"

#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace baroque::game::post {

namespace {

ShadowPass gShadow;
Target gTarget;   // +0x8: 256x256 target (FUN_001101e0: +0x98 {2, 0x100, 0x100, 0x10, 0, ...}), texture +0xc

// orig FUN_001400b0 (work/decompiled/001400b0.c): floor face test of the room receivers
bool floorFace(const data::ClsFace& f) {
    if (f.attr[2] == 'S') return false;          // param_1[3]
    if (f.type != 'F') return false;             // param_1[0]
    if (f.attr[1] == 'E') return false;          // param_1[2]
    if (f.attr[1] == 'D') return false;
    if (f.attr[0] == 'W') return false;          // param_1[1]
    if (f.attr[0] == 'D') return false;
    return true;
}

void pushVertex(std::vector<float>& out, float x, float y, float z) {
    out.push_back(x);
    out.push_back(y);
    out.push_back(z);
}

}  // namespace

ShadowPass& shadowPass() { return gShadow; }

// orig FUN_00111db0 (work/decompiled/00111db0.c): +0x10 = 1 and the consumer node back into the scene root
void ShadowPass::on() {
    if (enabled == 0) enabled = 1;
}

// orig FUN_00111df0 (work/decompiled/00111df0.c): +0x10 = 0 and the consumer node removed (FUN_003df010)
void ShadowPass::off() {
    if (enabled != 0) enabled = 0;
}

// orig FUN_001113d0 head (work/decompiled/001113d0.c; jitter constants 0x40300000 / 0x40400000 at 0x1114a0)
void ShadowPass::tick() {
    rendered_ = false;
    if (enabled == 0) return;
    ++counter;
    jitter = (counter & 1u) == 0 ? 2.75f : 3.0f;
}

void ShadowPass::clearReceivers() { tris_.clear(); }

void ShadowPass::buildReceivers(const Dungeon& d) {
    tris_.clear();
    // orig FUN_001277c0 (work/decompiled/001277c0.c) from the room loader FUN_0011e520 (room position, y = 0):
    // floor faces (FUN_001400b0) moved 0.15 along their normal, UV = world (x, z).
    // UNVERIFIED: fixed maps (FUN_0011c0a0 == 0) use their single room at the origin like the collision.
    const bool generated = isGeneratedFloor(d.floorId());
    for (int i = 0; i < d.roomCount(); ++i) {
        const Room& r = d.room(i);
        if (!r.cls) continue;
        if (generated && !r.placed()) continue;
        if (!generated && i != 0) break;
        const float ox = generated ? r.originX() : 0.0f;
        const float oz = generated ? r.originZ() : 0.0f;
        const data::Cls& c = *r.cls;
        for (const data::ClsFace& f : c.faces) {
            if (!floorFace(f)) continue;
            const data::ClsVec3& n = c.vectors[f.normal];
            const float nx = n.x, ny = n.y * 0.15f, nz = n.z;
            for (uint32_t vi : f.verts) {
                const data::ClsVec3& v = c.vertices[vi];
                pushVertex(tris_, (v.x + ox) + nx * 0.15f, (v.y + 0.0f) + ny, (v.z + oz) + nz * 0.15f);
            }
        }
    }
    // orig FUN_00123280 (work/decompiled/00123280.c), receiver loop: corridor cells (0x2000) except piece ids
    // 0xf..0x12, faces of type 'F' of the MT collision 0x50ba50[id] at the cell, y + 0.15 (or 300.15 for cells with
    // the 0xf00 bits = PieceInstance y 300).
    for (const PieceInstance& p : d.pieces()) {
        const int id = p.id & 0xff;
        if (id == 0x12 || id == 0x11 || id == 0x10 || id == 0xf) continue;
        if (id >= 19 || !d.mtCls(id)) continue;
        const data::Cls& c = *d.mtCls(id);
        for (const data::ClsFace& f : c.faces) {
            if (f.type != 'F') continue;
            for (uint32_t vi : f.verts) {
                const data::ClsVec3& v = c.vertices[vi];
                pushVertex(tris_, v.x + p.x, v.y + (p.y + 0.15f), v.z + p.z);
            }
        }
    }
}

// `drawCasters` may draw every task (the caster filter and the skin swaps are applied per model by model_fx). PORT
// (transitional): when it drew no task of type >= 3, the other models of the caster root (item entities, dungeon
// objects, projectiles) are added here at their tick position. The alpha overload below draws the whole root itself.
void ShadowPass::renderCasters(alchemy::SceneRenderer& r, const alchemy::Mat4& camWorld, float eyeY,
                               const CasterDraw& drawCasters, unsigned displayFbo, int screenW, int screenH) {
    render(r, camWorld, eyeY, [&drawCasters](alchemy::SceneRenderer& sr, int pass) {
        modelShadowCasterPassSawObjects(true);
        drawCasters(sr, pass);
        if (modelShadowCasterPassSawObjects(true)) return;
        for (const Chr* c : characters().list())
            if (c->type > kChrEnemy) chrDraw(*c, sr, pass, 1.0f);
    }, displayFbo, screenW, screenH);
}

// The caster root 0x4e79ec: models linked by FUN_00109f10 (model_fx modelShadowCaster filters the rest out).
void ShadowPass::renderCasters(alchemy::SceneRenderer& r, const alchemy::Mat4& camWorld, float eyeY, float alpha,
                               unsigned displayFbo, int screenW, int screenH) {
    render(r, camWorld, eyeY, [alpha](alchemy::SceneRenderer& sr, int pass) {
        for (const Chr* c : characters().list()) chrDraw(*c, sr, pass, alpha);
    }, displayFbo, screenW, screenH);
}

// orig FUN_001113d0 (work/decompiled/001113d0.c), the part after the jitter: caster traversal and the edge ring.
void ShadowPass::render(alchemy::SceneRenderer& r, const alchemy::Mat4& camWorld, float eyeY,
                        const CasterDraw& drawCasters, unsigned displayFbo, int screenW, int screenH) {
    if (enabled == 0 || !postPassEnabled('s') || !postGl().init()) return;
    // centre = eye - camera back axis * 512 (DAT_0042c9f8), in X and Z (disassembly 0x111460..0x111498)
    constexpr float kAhead = 512.0f;
    centre_[0] = (0.0f + camWorld.m[12]) - camWorld.m[8] * kAhead;
    centre_[2] = (0.0f + camWorld.m[14]) - camWorld.m[10] * kAhead;
    centre_[1] = eyeY;
    PostGl::ensureTarget(gTarget, 256, 256, true);   // +0x214 / +0x20c filters 1 (LINEAR), +0x204 wrap 0 (CLAMP)
    // view: identity, FUN_0039e4e0(-pi/2 (0xbfc90fdb), m, axis (1,0,0)), FUN_0039da90(m, +0x18), inverse
    alchemy::Mat4 m = alchemy::Mat4::identity();
    {
        const float a = -1.5707964f;
        const float s = std::sin(a), c = std::cos(a);
        // orig FUN_0039ded0 with axis (1, 0, 0)
        m.m[0] = (1.0f - c) * 1.0f * 1.0f + c + 0.0f;
        m.m[1] = 0.0f;
        m.m[2] = 0.0f;
        m.m[4] = 0.0f;
        m.m[5] = c + 0.0f;
        m.m[6] = s * 1.0f + 0.0f;
        m.m[8] = 0.0f;
        m.m[9] = -(s * 1.0f);
        m.m[10] = c + 0.0f;
    }
    m.m[12] = centre_[0];
    m.m[13] = centre_[1];
    m.m[14] = centre_[2];
    alchemy::Mat4 view;
    if (!alchemy::invert(m, view)) return;
    // +0x3c: FUN_0039e690(-256, 256, -256, 256, 10, 700) (FUN_001101e0)
    const alchemy::Mat4 proj = orthoGs(-256.0f, 256.0f, -256.0f, 256.0f, 10.0f, 700.0f);
    // target +0x8, clear colour (0, 0, 0, 1), clear colour buffer
    PostGl::clear(gTarget, 0, 0, 0, 255);
    PostGl::bind(gTarget.fbo, gTarget.w, gTarget.h);
    // caster root attributes: fog (mode 0, 0..1, colour (1,1,1,1)) on, colour mask (1,1,1,0), scissor (1,1,254,254)
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    r.setFog(true, white, 0.0f, 1.0f);
    rlEnableScissorTest();
    rlScissor(1, 1, 254, 254);
    r.begin(view, proj);
    rlColorMask(true, true, true, false);
    // skin loop (task list 0x50b134, type < 3): appearance skin := "sh"; player attached skins removed; then
    // vt+0x60 traversal of the caster root; then skin list [0] and the attached skins back (model_fx.hpp)
    modelShadowCasterPassBegin();
    for (int pass = 0; pass < 2; ++pass) drawCasters(r, pass);
    modelShadowCasterPassEnd();
    r.end();
    rlDrawRenderBatchActive();
    rlDisableScissorTest();
    rlColorMask(true, true, true, true);
    // edge ring: ortho (-127.5, 128.5, 128.5, -127.5, -1, 1), view 0x4bc2e0, blend on, igMultiTextureExt ALPHA
    // (2,0,1,1,0x80) = Cd - Cs * Ad, texture off, stream +0x30 (10 vertices: odd ones colour (1,1,1,1)), 8 triangles
    const GsColor c0 = gsColor(0.0f, 0.0f, 0.0f, 1.0f), c1 = gsColor(1.0f, 1.0f, 1.0f, 1.0f);
    const float ring[10][2] = {{-96, -96}, {-128, -128}, {96, -96}, {128, -128}, {96, 96},
                               {128, 128}, {-96, 96}, {-128, 128}, {-96, -96}, {-128, -128}};
    Vtx tri[24];
    int n = 0;
    for (int i = 0; i + 2 < 10; ++i) {
        for (int k = 0; k < 3; ++k) {
            const int j = i + k;
            tri[n++] = Vtx{ring[j][0], ring[j][1], 0.0f, 0.0f, 0.0f, (j & 1) != 0 ? c1 : c0};
        }
    }
    PostGl::Params p;
    p.blend = PostGl::Blend::SubtractCs;
    p.mask = {true, true, true, true};
    p.mvp = orthoGs(-127.5f, 128.5f, 128.5f, -127.5f, -1.0f, 1.0f);   // 0xc2ff0000, 0x43008000
    postGl().draw(tri, n, p);
    postGl().restore();
    postDumpTarget(gTarget, "shadow");
    // projection and clear colour restored (+0x29c(0x4e7914)), display target again
    PostGl::bind(displayFbo, screenW, screenH);
    rendered_ = true;
}

void ShadowPass::drawReceivers(const alchemy::Mat4& view, const alchemy::Mat4& proj) {
    if (enabled == 0 || !rendered_ || tris_.empty() || gTarget.tex == 0) return;
    const float j = jitter;
    // texture matrices of the five attr sets: translation (-(cx - 256) +- j, -(cz - 256) +- j) then x 1/512
    const float jx[5] = {-j, j, -j, j, 0.0f};
    const float jz[5] = {-j, -j, j, j, 0.0f};
    const GsColor col = gsColor(0.75f, 0.75f, 0.75f, 1.0f);   // igColorAttr +0xe4 (0x3f400000, set every frame)
    PostGl::Params p;
    p.tex = gTarget.tex;
    p.blend = PostGl::Blend::SubtractFix;
    p.fix = 3;                       // igBlendFunctionAttr _blendConstant
    p.alphaTestNotZero = true;       // igAlphaFunctionAttr (5 = NOTEQUAL, 0)
    p.depthTest = true;
    p.mask = {true, true, true, false};
    p.mvp = alchemy::mul(view, proj);
    // PORT: only triangles that reach the 512x512 area around the centre are submitted (the clamped border of the
    // target is black, so the others subtract nothing).
    const float lo[2] = {centre_[0] - 256.0f - 3.0f, centre_[2] - 256.0f - 3.0f};
    const float hi[2] = {centre_[0] + 256.0f + 3.0f, centre_[2] + 256.0f + 3.0f};
    std::vector<Vtx> v;
    for (int pass = 0; pass < 5; ++pass) {
        const float tu = -(centre_[0] - 256.0f) + jx[pass];
        const float tv = -(centre_[2] - 256.0f) + jz[pass];
        v.clear();
        for (size_t t = 0; t + 9 <= tris_.size(); t += 9) {
            const float* q = &tris_[t];
            const float x0 = std::min({q[0], q[3], q[6]}), x1 = std::max({q[0], q[3], q[6]});
            const float z0 = std::min({q[2], q[5], q[8]}), z1 = std::max({q[2], q[5], q[8]});
            if (x1 < lo[0] || x0 > hi[0] || z1 < lo[1] || z0 > hi[1]) continue;
            for (int k = 0; k < 3; ++k) {
                const float x = q[k * 3], y = q[k * 3 + 1], z = q[k * 3 + 2];
                const float u = (x + tu) * 0.001953125f;   // 0x3b000000
                const float vv = (z + tv) * 0.001953125f;
                v.push_back(Vtx{x, y, z, u, 1.0f - vv, col});   // render-target texture: row 0 = bottom
            }
        }
        if (!v.empty()) postGl().draw(v.data(), static_cast<int>(v.size()), p);
    }
    postGl().restore();
    rlEnableDepthTest();   // still inside the 3-D pass
}

}  // namespace baroque::game::post
