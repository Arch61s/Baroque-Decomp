// Draws of the screen-effects object 0x4e7b80 (see post_render.hpp, screen_effects.hpp, post_gl.hpp).
#include "game/post/post_render.hpp"

#include "game/effects/effects_gl.hpp"
#include "game/post/post_gl.hpp"
#include "game/post/screen_effects.hpp"
#include "game/ui/texture_registry.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace baroque::game::post {

namespace {

PostRenderer gRenderer;

constexpr int kTexSubtract = 0x1a;   // DATA/TEX_EN/SUBTRACT.PNG (0x4e6ad8)
constexpr int kTexNoise = 0x1b;      // DATA/TEX_EN/NOISE_T.PNG (0x4e6adc)

// GL objects of 0x4e7b80 (+0x8/+0xc render target and its texture, +0x18, +0x1c) plus the port's screen copy and the
// scratch copy that models the GS reading the target it draws into (bloom taps).
struct Gl {
    Target screen;    // PORT: resolved copy of the window's 3-D raster (source of the +0x284 copies)
    Target rt;        // +0x8 / +0xc: 128x128 render target (Wv/4 x 128)
    Target rtRead;    // PORT: the target's content as the next tap reads it
    Target tex6;      // +0x18: 128x128 texture (Wv/4 x 128)
    Target tex7;      // +0x1c: 256x256 texture (Wv/2 x 256)
    Target exi;       // PORT: 512x512 frame copy handed to the EXI renderer (texture slot 1)
};
Gl gGl;

struct Raster {
    int w = 0, h = 0;
    float px[2] = {1.0f, 1.0f};   // window pixels per original pixel (x: 4:3 width / 512, y: height / 448)
    int bloomW = 128, dofW = 256;
};

Raster raster(int w, int h) {
    Raster r;
    r.w = w;
    r.h = h;
    const float wv = 384.0f * static_cast<float>(w) / static_cast<float>(h);   // 512 at 4:3
    r.px[0] = static_cast<float>(h) / 384.0f;
    r.px[1] = static_cast<float>(h) / 448.0f;
    r.bloomW = std::max(1, static_cast<int>(std::lround(128.0f * wv / 512.0f)));
    r.dofW = std::max(1, static_cast<int>(std::lround(256.0f * wv / 512.0f)));
    return r;
}

// Unit quad of FUN_0010e910 / FUN_0010f8e0 (+0x5c / +0x74): positions (+-0.5, +-0.5, -0.5), UV (0..1) with v = 0 at
// y = -0.5, drawn as a 2-triangle strip in the ortho FUN_0039e690(-0.5, 0.5, 0.5, -0.5, 0, 1) (y down). The texture
// matrix u' = u * su + tu, v' = v * sv + tv (original orientation) is applied here; `flipV` converts to a GL
// render-target texture (row 0 = bottom).
struct TexXf { float su = 1.0f, sv = 1.0f, tu = 0.0f, tv = 0.0f; };

int unitQuad(Vtx out[6], const TexXf& m, bool flipV, GsColor c) {
    const float p[4][4] = {{-0.5f, -0.5f, 0.0f, 0.0f}, {-0.5f, 0.5f, 0.0f, 1.0f},
                           {0.5f, -0.5f, 1.0f, 0.0f}, {0.5f, 0.5f, 1.0f, 1.0f}};
    const int strip[6] = {0, 1, 2, 2, 1, 3};
    for (int i = 0; i < 6; ++i) {
        const float* q = p[strip[i]];
        const float u = q[2] * m.su + m.tu;
        const float v = q[3] * m.sv + m.tv;
        out[i] = Vtx{q[0], q[1], -0.5f, u, flipV ? 1.0f - v : v, c};
    }
    return 6;
}

const alchemy::Mat4& unitOrtho() {
    static const alchemy::Mat4 m = orthoGs(-0.5f, 0.5f, 0.5f, -0.5f, 0.0f, 1.0f);   // 0xbf000000.. FUN_0039e690
    return m;
}

// Draws texture `src` over the whole of the bound target with the unit quad.
void fullQuad(unsigned tex, bool srcIsTarget, const TexXf& m, GsColor c, PostGl::Params p) {
    Vtx v[6];
    unitQuad(v, m, srcIsTarget, c);
    p.tex = tex;
    p.mvp = unitOrtho();
    postGl().draw(v, 6, p);
}

// "+0x284(display target, texture, 0, 0, 0x200, 0x1c0)" (FUN_0037a100 -> orig FUN_0026bba0, work/decompiled/0026bba0.c,
// branch at 0x26c1f0 on the byte 0x4e5cc4 (-0x7eac($gp)): 1 in the ELF data and never written, so the biased branch
// runs): the 512x448 frame drawn as one GS sprite over the whole texture with
//   XY  (-8, -8) .. (w*16 - 8, h*16 - 8)          (sprite edges at -0.5 / size - 0.5: pixel i samples at i + 0.5)
//   U   0*16 + 0xb .. 512*16 + 5   = 0.6875 .. 512.3125 texels
//   V   (H - 0)*16 + 8 .. (H - 448)*16 + 8 = a 448-row span biased by +0.5 row (the copy is stored flipped; in
//       top-down frame rows the span is 0.5 .. 448.5)
// So destination pixel centre i maps linearly onto U 0.6875 .. 512.3125 (frame content 0.69 px to the left of a
// centred copy) and onto frame rows 0.5 .. 448.5. With GL's pixel-centre sampling that is exactly a quad with these
// UV spans, no extra GS sample-point shift (the XY bias already centres the pixels).
// UNVERIFIED: TEX1 filter of that sprite (bilinear assumed); the flip direction of the stored copy (only the size
// and sign of the 0.5-row bias depend on it).
void copyScreenTo(const Target& dst) {
    PostGl::bind(dst.fbo, dst.w, dst.h);
    PostGl::Params p;
    p.mask = {true, true, true, true};
    TexXf m;
    m.su = (512.3125f - 0.6875f) / 512.0f;   // 0x2005 - 0xb sixteenths over 512 texels
    m.tu = 0.6875f / 512.0f;
    m.sv = 1.0f;                             // 448 rows over the 448-row frame
    m.tv = 0.5f / 448.0f;
    fullQuad(gGl.screen.tex, true, m, GsColor{}, p);
}

void ensureTargets(const Raster& r) {
    PostGl::ensureTarget(gGl.screen, r.w, r.h, true);
    PostGl::ensureTarget(gGl.rt, r.bloomW, 128, true);      // FUN_0010db20: +0x98 {2, 0x80, 0x80, 0x18, ...}
    PostGl::ensureTarget(gGl.rtRead, r.bloomW, 128, true);
    PostGl::ensureTarget(gGl.tex6, r.bloomW, 128, true);    // +0x1c0(0x80, 0x80, 5, 3, 0, 1); filters 1/1, wrap 0/0
    PostGl::ensureTarget(gGl.tex7, r.dofW, 256, true);      // +0x1c0(0x100, 0x100, 5, 3, 0, 1)
}

}  // namespace

PostRenderer& postRenderer() { return gRenderer; }

void PostRenderer::construct() {
    postGl().init();
    ui::TextureRegistry& t = ui::textures();
    t.load(kTexSubtract);                   // FUN_00106090(0x4e6a68, 0x1a, 1)
    t.load(kTexNoise);                      // FUN_00106090(0x4e6a68, 0x1b, 1)
    if (const Texture2D* n = t.texture(kTexNoise)) {
        // NOISE_T igTextureAttr: vt+0x84(1) mag filter LINEAR, vt+0x8c(1) / vt+0x90(1) wrap S / T REPEAT
        rlTextureParameters(n->id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
        rlTextureParameters(n->id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_REPEAT);
        rlTextureParameters(n->id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_REPEAT);
    }
}

void PostRenderer::destroy() {
    ui::textures().release(kTexNoise);      // FUN_00105f00(0x4e6a68, 0x1b)
    ui::textures().release(kTexSubtract);   // FUN_00105f00(0x4e6a68, 0x1a)
    PostGl::freeTarget(gGl.screen);
    PostGl::freeTarget(gGl.rt);
    PostGl::freeTarget(gGl.rtRead);
    PostGl::freeTarget(gGl.tex6);
    PostGl::freeTarget(gGl.tex7);
    effectsRenderer().setScreenTexture(0, 0, 0);
    PostGl::freeTarget(gGl.exi);
}

// orig FUN_0010ede0 (work/decompiled/0010ede0.c; plane loop checked in the disassembly 0x10f4a0..0x10f8a4: view =
// identity, vertices in eye space, x = -d * tan(fov / 2), y = x / 1.3333334 with d = +0x60 * k < 0).
void PostRenderer::drawDistanceBlur(int screenW, int screenH, const alchemy::Mat4& proj, float fovDeg) {
    distanceBlur(screenW, screenH, proj, fovDeg);
    feedExiScreen(screenW, screenH);
}

// The EXI UVs of mode 8 are the 4:3 projection of the frame (effectView) mapped to 0..1 x 0..0.875 of a 512x512
// texture: texel row 0 = top of the frame. The copy keeps that layout: the window's 4:3 band (the whole window at
// 4:3; the port's wider rasters share the vertical FOV) scaled into rows 0..447. Texel alpha 0x80 like every 24-bit
// frame copy here (UNVERIFIED: TEXA.TA0; the slot-1 PSM is not traced).
void PostRenderer::feedExiScreen(int screenW, int screenH) {
    if (!postGl().ready() || screenW <= 0 || screenH <= 0) return;
    const unsigned display = PostGl::activeFramebuffer();
    PostGl::ensureTarget(gGl.screen, screenW, screenH, true);
    PostGl::ensureTarget(gGl.exi, 512, 512, true);
    PostGl::blit(display, gGl.screen, screenW, screenH);
    PostGl::clear(gGl.exi, 0, 0, 0, 0x80);
    PostGl::bind(gGl.exi.fbo, gGl.exi.w, gGl.exi.h);
    const float band = std::min(1.0f, (4.0f / 3.0f) * static_cast<float>(screenH) / static_cast<float>(screenW));
    const float u0 = 0.5f - band * 0.5f, u1 = 0.5f + band * 0.5f;
    const GsColor c{};   // 0x80: MODULATE leaves the texel as is
    // x 0..512, y 0..448 (texel rows); the window texture has row 0 = bottom, so frame row 0 (top) is v = 1
    const Vtx quad[4] = {{0.0f, 0.0f, 0.0f, u0, 1.0f, c}, {512.0f, 0.0f, 0.0f, u1, 1.0f, c},
                         {0.0f, 448.0f, 0.0f, u0, 0.0f, c}, {512.0f, 448.0f, 0.0f, u1, 0.0f, c}};
    const Vtx tri[6] = {quad[0], quad[1], quad[2], quad[2], quad[1], quad[3]};
    PostGl::Params p;
    p.tex = gGl.screen.tex;
    p.mask = {true, true, true, false};
    p.mvp = orthoGs(0.0f, 512.0f, 0.0f, 512.0f, -1.0f, 1.0f);
    postGl().draw(tri, 6, p);
    postGl().restore();
    postDumpTarget(gGl.exi, "exi_screen");
    PostGl::bind(display, screenW, screenH);
    effectsRenderer().setScreenTexture(gGl.exi.tex, gGl.exi.w, gGl.exi.h);
}

void PostRenderer::distanceBlur(int screenW, int screenH, const alchemy::Mat4& proj, float fovDeg) {
    const ScreenEffects::Frame& f = screenEffects().frame;
    if (!f.dof || !postGl().ready() || !postPassEnabled('e')) return;
    const unsigned display = PostGl::activeFramebuffer();   // +0x4 display target
    const Raster r = raster(screenW, screenH);
    ensureTargets(r);
    // +0x284(display, +0x1c, 0, 0, 0x200, 0x1c0)
    PostGl::blit(display, gGl.screen, r.w, r.h);
    copyScreenTo(gGl.tex7);
    // target +0x8 (FUN_0039e690 unit ortho, identity view and texture matrix, texture matrix on)
    PostGl::bind(gGl.rt.fbo, gGl.rt.w, gGl.rt.h);
    PostGl::Params p;
    p.mask = {true, true, true, true};   // UNVERIFIED: colour mask not set by the pass (inherited state)
    p.gsPixel[0] = 1.0f;
    p.gsPixel[1] = 1.0f;
    // colour (1,1,1,1), blend off (+0x238(0)), texture +0x1c, stream +0x5c
    fullQuad(gGl.tex7.tex, true, TexXf{}, gsColor(1.0f, 1.0f, 1.0f, 1.0f), p);
    // blend (4,5) on, FUN_003841f0 VU scale (0.55, 0.55, 0.55, 1); four offset copies (translation in UV of the
    // 256-wide texture: 1/128 = 2 texels, kept in texels for other widths)
    p.blend = PostGl::Blend::Alpha45;
    constexpr float kVu55 = 0.55f;   // 0x3f0ccccd
    const float du = 2.0f / static_cast<float>(gGl.tex7.w), dv = 2.0f / static_cast<float>(gGl.tex7.h);
    struct Step { float a, tu, tv; };
    const Step steps[4] = {{0.5f, 0.0f, 0.0f}, {0.4f, du, 0.0f}, {0.3f, 0.0f, dv}, {0.2f, du, dv}};   // 0x3ecccccd ..
    for (const Step& s : steps) {
        TexXf m;
        m.tu = s.tu;
        m.tv = s.tv;
        fullQuad(gGl.tex7.tex, true, m, gsColor(1.0f, 1.0f, 1.0f, s.a, kVu55), p);
    }
    postDumpTarget(gGl.screen, "dof_screen");
    postDumpTarget(gGl.tex7, "dof_tex7");
    postDumpTarget(gGl.rt, "dof_rt");
    // back to the display target: depth test on (+0x368(1)), depth write off (+0x378(0)), fog off (+0x388(0)),
    // texture modulate on, blend (4,5), colour mask RGB, texture +0xc, view identity, scene projection
    PostGl::bind(display, r.w, r.h);
    PostGl::Params q;
    q.tex = gGl.rt.tex;
    q.blend = PostGl::Blend::Alpha45;
    q.depthTest = true;
    q.mask = {true, true, true, false};
    q.gsPixel[0] = r.px[0];
    q.gsPixel[1] = r.px[1];
    q.mvp = proj;
    // DAT_0042c9c0[6] depth factors, DAT_0042c9e0[6] alphas
    const float kDepth[6] = {-0.98f, -0.9f, -0.8f, -0.7f, -0.6f, -0.5f};
    const float kAlpha[6] = {0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.05f};
    const float t = std::tan((((180.0f - fovDeg) / 2.0f) * 3.1415927f) / 180.0f);   // FUN_00238788
    const float aspect = static_cast<float>(r.w) / static_cast<float>(r.h);
    for (int i = 0; i < 6; ++i) {
        const float d = f.dofDist * kDepth[i];   // f24
        const float x = -d / t;                  // f23 (= |d| tan(fov / 2))
        const float y = (d / t) / 1.3333334f;    // f22 (negative)
        // PORT: the plane spans the port's aspect (x = |y| * W / H); identical to the original at 4:3
        const float xw = -y * aspect;
        (void)x;
        const GsColor c = gsColor(1.0f, 1.0f, 1.0f, kAlpha[i], kVu55);
        // v0 (-x, -y, d) uv (0,0), v1 (-x, y, d) uv (0,1), v2 (x, -y, d) uv (1,0), v3 (x, y, d) uv (1,1)
        const Vtx quad[4] = {{-xw, -y, d, 0.0f, 1.0f, c}, {-xw, y, d, 0.0f, 0.0f, c},
                             {xw, -y, d, 1.0f, 1.0f, c}, {xw, y, d, 1.0f, 0.0f, c}};
        const Vtx tri[6] = {quad[0], quad[1], quad[2], quad[2], quad[1], quad[3]};
        postGl().draw(tri, 6, q);
    }
    postGl().restore();
}

// orig FUN_0010e0c0 (work/decompiled/0010e0c0.c)
void PostRenderer::drawBloom(int screenW, int screenH) {
    const ScreenEffects::Frame& f = screenEffects().frame;
    const Texture2D* sub = ui::textures().texture(kTexSubtract);
    if (!f.bloom || !postGl().ready() || sub == nullptr || !postPassEnabled('i')) return;
    const unsigned display = PostGl::activeFramebuffer();
    const Raster r = raster(screenW, screenH);
    ensureTargets(r);
    // +0x284(display, +0x18, 0, 0, 0x200, 0x1c0)
    PostGl::blit(display, gGl.screen, r.w, r.h);
    copyScreenTo(gGl.tex6);
    // target +0x8; colour mask (1,1,1,0); texture +0x18 drawn with the inherited blend state.
    // UNVERIFIED: the inherited state (blend / colour left by the passes before) is taken as a plain copy.
    PostGl::bind(gGl.rt.fbo, gGl.rt.w, gGl.rt.h);
    PostGl::Params p;
    p.gsPixel[0] = 1.0f;
    p.gsPixel[1] = 1.0f;
    p.mask = {true, true, true, false};
    fullQuad(gGl.tex6.tex, true, TexXf{}, GsColor{}, p);
    // depth off, TFX modulate, texture on, blend on, igMultiTextureExt ALPHA (2,0,1,1,0x80) = Cd - Cs * Ad,
    // colour (+0x24 x4), SUBTRACT.PNG
    p.blend = PostGl::Blend::SubtractCs;
    p.texAlpha8 = true;
    fullQuad(sub->id, false, TexXf{}, gsColor(f.bloomSub, f.bloomSub, f.bloomSub, f.bloomSub), p);
    p.texAlpha8 = false;
    postDumpTarget(gGl.tex6, "bloom_tex6");
    postDumpTarget(gGl.rt, "bloom_sub");
    // colour mask (1,1,1,1); +0x284(+0x8, +0x18, 0, 0, 0x80, 0x80): same-size copy
    PostGl::blit(gGl.rt.fbo, gGl.tex6, gGl.rt.w, gGl.rt.h);
    // blend (4,1) then texture +0x18 and blend (4,5); clear colour (0,0,0,1), clear colour buffer
    PostGl::clear(gGl.rt, 0, 0, 0, 255);
    PostGl::bind(gGl.rt.fbo, gGl.rt.w, gGl.rt.h);
    // colour (1,1,1,0.5); one draw per channel: masks (0,0,1,1), (0,1,0,1), (1,0,0,1)
    p.blend = PostGl::Blend::Alpha45;
    const GsColor half = gsColor(1.0f, 1.0f, 1.0f, 0.5f);
    const std::array<bool, 4> masks[3] = {{false, false, true, true}, {false, true, false, true}, {true, false, false, true}};
    for (const auto& m : masks) {
        p.mask = m;
        fullQuad(gGl.tex6.tex, true, TexXf{}, half, p);
    }
    p.mask = {true, true, true, true};
    // colour white; blend (4,1); texture matrix on; colour (0.75, 0.75, 0.75, 0.25);
    // 3 rings x 6 taps of the target into itself, ring radius 1.5 / 3.0 / 4.5 texels
    p.blend = PostGl::Blend::Add41;
    const GsColor tap = gsColor(0.75f, 0.75f, 0.75f, 0.25f);   // 0x3f400000, 0x3e800000
    float radius = 1.5f;
    for (int ring = 0; ring < 3; ++ring) {
        const float base = static_cast<float>(ring) * 0.3488889f;
        for (int k = 0; k < 6; ++k) {
            const float a = static_cast<float>(k) * 1.0466667f + base + 0.0f;
            const float s = std::sin(a);   // FUN_002386a0
            const float c = std::cos(a);   // FUN_00238400
            // translation (r sin / 128 + 1/128, r cos / 128 + 1/256) in UV of the 128x128 target = texels
            TexXf m;
            m.tu = (radius * s + 1.0f) / static_cast<float>(gGl.rt.w);
            m.tv = (radius * c + 0.5f) / static_cast<float>(gGl.rt.h);
            // PORT: the GS reads the target it is drawing into (texture +0xc = target +0x8); each tap reads the
            // result of the previous taps (copy), the read-after-write inside one GS draw is not modelled.
            PostGl::blit(gGl.rt.fbo, gGl.rtRead, gGl.rt.w, gGl.rt.h);
            PostGl::bind(gGl.rt.fbo, gGl.rt.w, gGl.rt.h);
            fullQuad(gGl.rtRead.tex, true, m, tap, p);
        }
        radius = radius + 1.5f;
    }
    postDumpTarget(gGl.rt, "bloom_rt");
    // texture matrix off; colour white; display target; depth write off, depth test off; texture +0xc;
    // colour (+0x30 x4); blend (4,1) still set
    PostGl::bind(display, r.w, r.h);
    PostGl::Params q;
    q.blend = PostGl::Blend::Add41;
    q.mask = {true, true, true, false};   // PORT: the display is 24-bit (no alpha written)
    q.gsPixel[0] = r.px[0];
    q.gsPixel[1] = r.px[1];
    fullQuad(gGl.rt.tex, true, TexXf{}, gsColor(f.bloomAdd, f.bloomAdd, f.bloomAdd, f.bloomAdd), q);
    postGl().restore();
}

// orig FUN_0010fb90 (work/decompiled/0010fb90.c): texture matrix = FUN_0039d920(translation (x/64, y/64, 0)) then
// FUN_0039dc10 diagonal (4, 4, 0): u' = 4u + x/64, v' = 4v + y/64 (NOISE_T is 64x64: 4 tiles across the screen).
void PostRenderer::drawNoise(int screenW, int screenH) {
    const ScreenEffects::Frame& f = screenEffects().frame;
    const Texture2D* noise = ui::textures().texture(kTexNoise);
    if (!f.noise || !postGl().ready() || noise == nullptr || !postPassEnabled('k')) return;
    const Raster r = raster(screenW, screenH);
    PostGl::Params p;
    // colour (1,1,1,+0xb8), depth test off, depth write off, colour mask (1,1,1,0), texture on, modulate, blend (4,5)
    p.blend = PostGl::Blend::Alpha45;
    p.mask = {true, true, true, false};
    // NOISE_T.PNG is an 8-bit palette PNG without tRNS. The PNG loader FUN_00377a00 builds its CLUT with alpha 0xFF
    // (png_get_valid(tRNS) false) and configures it through igClut vt+0x60 = FUN_00363bc0, which ends in FUN_00363e70
    // -> FUN_00363ec0: for format 7 every entry's alpha becomes (a >> 1) + (a & 1) and _isAlphaScaled (+0x1d) is set.
    // So every texel has At = 0x80 and As = 0x80 * Af >> 7 = Af: the shader's fixed 0x80 texel alpha.
    p.texAlpha8 = false;
    p.gsPixel[0] = r.px[0];
    p.gsPixel[1] = r.px[1];
    TexXf m;
    m.su = 4.0f * (384.0f * static_cast<float>(r.w) / static_cast<float>(r.h)) / 512.0f;   // 4 per 512 original px
    m.sv = 4.0f;
    m.tu = f.noiseX / 64.0f;
    m.tv = f.noiseY / 64.0f;
    fullQuad(noise->id, false, m, gsColor(1.0f, 1.0f, 1.0f, f.noiseAlpha), p);
    postGl().restore();
}

}  // namespace baroque::game::post
