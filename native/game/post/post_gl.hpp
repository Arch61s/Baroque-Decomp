#pragma once
// GS-faithful rlgl helpers shared by the post passes (screen effects 0x4e7b80, shadow pre-pass renderer+0xd4).
//
// GS model (work/reports/ALCHEMY_NATIVE.md, render2d.hpp):
// * Colours are passed per vertex in GS bytes (RGBAQ). TFX MODULATE: Cv = Ct * Cf >> 7, Av = At * Af >> 7, i.e.
//   0x80 = 1.0; untextured primitives write Cf itself (0x80 = 128/255). gsColor() converts a float colour the way
//   the game sets it: igVisualContext +0xe8 (FUN_0037d510: FUN_003a0590 mode 1 = (int)(255 * c + 0.5) per byte,
//   alpha byte -> (a >> 1) + (a & 1)) and the VU1 vertex colour scale (+0x5a0, FUN_003841f0) on RGB.
// * Texture alpha: render-target copies are 24-bit (PSMCT24; the targets are created with 0x18 colour bits), their
//   alpha reads as 0x80 (UNVERIFIED: TEXA.TA0 assumed 0x80). PNG textures: the loader FUN_00377a00 halves 8-bit alpha
//   ((a >> 1) + (a & 1): FUN_00368f90 per RGBA row, FUN_00363ec0 for palette CLUTs), so an opaque PNG texel is 0x80
//   (NOISE_T: texAlpha8 = false). SUBTRACT.PNG is RGB (format 5, 24-bit).
// * Destination alpha of the 24-bit targets reads as 0x80 (GS rule for PSMCT24), so the custom blend
//   (A,B,C,D) = (0, Cs, Ad, Cd) of FUN_0037f960(ext, 2, 0, 1, 1, 0x80, 0) is Cd - Cs.
// * The GS samples a pixel at its integer (top-left) position, GL at the pixel centre: the shader moves the UV to
//   the GS sample point (half a pixel of the ORIGINAL 512x448 raster, see PostGl::Params::gsPixel).
// Render-target textures are GL textures (row 0 = bottom); PNG textures loaded by raylib have row 0 = top like the
// GS. Callers flip V for render targets (`1 - v`).
#include "alchemy/scene_render.hpp"

#include <array>
#include <cstdint>

namespace baroque::game::post {

struct GsColor { uint8_t r = 0x80, g = 0x80, b = 0x80, a = 0x80; };

// Engine default VU1 colour scale (FUN_003832e0: DAT_004e5bbc = 128/255).
inline constexpr float kVuScaleDefault = 128.0f / 255.0f;
// float colour (as given to igVisualContext +0xe8) -> GS bytes with the VU1 RGB scale `vu`
GsColor gsColor(float r, float g, float b, float a, float vu = kVuScaleDefault);

struct Target {
    unsigned fbo = 0, tex = 0;
    int w = 0, h = 0;
};

struct Vtx {
    float x, y, z;
    float u, v;
    GsColor c;
};

class PostGl {
public:
    enum class Blend { Off, Alpha45, Add41, SubtractCs, SubtractFix };
    struct Params {
        unsigned tex = 0;               // 0 = texture off (TME 0)
        bool texAlpha8 = false;         // texel alpha used as stored (PNG); false = 0x80 (24-bit copy)
        // GS sample point: shift in units of one destination pixel per ORIGINAL pixel (x, y); 0 = no shift.
        float gsPixel[2] = {0.0f, 0.0f};
        Blend blend = Blend::Off;
        int fix = 0;                    // GS ALPHA.FIX for Blend::SubtractFix
        bool alphaTestNotZero = false;  // ATST NOTEQUAL, AREF 0
        std::array<bool, 4> mask{true, true, true, false};
        bool depthTest = false;
        alchemy::Mat4 mvp = alchemy::Mat4::identity();   // row-vector clip = v * mvp
    };

    bool init();
    bool ready() const { return program_ != 0; }
    // Draws a triangle list with the GS model above into the currently bound framebuffer.
    void draw(const Vtx* v, int count, const Params& p);
    // Restores the raylib defaults the passes changed (blend alpha, mask, depth test off, depth mask on).
    void restore();

    static void ensureTarget(Target& t, int w, int h, bool linear);
    static void freeTarget(Target& t);
    static unsigned activeFramebuffer();
    static void bind(unsigned fbo, int w, int h);              // framebuffer + viewport
    // Same-size copy (GL blit) of framebuffer `src` into `dst` (also resolves the multisampled window buffer).
    static void blit(unsigned src, const Target& dst, int w, int h);
    static void clear(const Target& t, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

private:
    unsigned program_ = 0;
    int locMvp_ = -1, locTex_ = -1, locTexOn_ = -1, locTexAlpha_ = -1, locGsShift_ = -1, locFix_ = -1,
        locAlphaTest_ = -1;
    int* locs_ = nullptr;
};

PostGl& postGl();

// PORT debug switch: environment BAROQUE_POST=0 skips every post pass (comparison captures); BAROQUE_POST=<list>
// with any of the letters e (distance blur), i (bloom), k (grain), s (shadow) enables only those.
bool postPassEnabled(char pass);
// PORT debug: environment BAROQUE_POST_DUMP=<dir> writes intermediate targets (<dir>/<name>.png).
void postDumpTarget(const Target& t, const char* name);

// orig FUN_0039e690 (work/decompiled/0039e690.c): ortho(l, r, b, t, n, f) in the row-vector convention.
alchemy::Mat4 orthoGs(float l, float r, float b, float t, float n, float f);

}  // namespace baroque::game::post
