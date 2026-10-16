// GS-faithful rlgl helpers of the post passes (see post_gl.hpp).
#include "game/post/post_gl.hpp"

#include "platform/platform.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace baroque::game::post {

namespace {

PostGl gGl;

constexpr int kGlOne = 1;
constexpr int kGlSrcAlpha = 0x0302;
constexpr int kGlOneMinusSrcAlpha = 0x0303;
constexpr int kGlFuncAdd = 0x8006;
constexpr int kGlFuncReverseSubtract = 0x800B;
constexpr int kGlColorBufferBit = 0x4000;

const char* kVs = R"GLSL(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 uMvp;
out vec2 fragUV;
out vec4 fragColor;
void main() {
    fragUV = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = uMvp * vec4(vertexPosition, 1.0);
}
)GLSL";

// GS TFX MODULATE in byte units, alpha in 0x80 = 1.0 units for the GL blend factor; FIX path = the GS negative
// product (0 - Cs) * FIX >> 7 (arithmetic shift rounds towards -inf, hence ceil of the magnitude).
const char* kFs = R"GLSL(
#version 330
in vec2 fragUV;
in vec4 fragColor;
uniform sampler2D texture0;
uniform int uTexOn;
uniform int uTexAlpha8;
uniform vec2 uGsShift;
uniform float uFix;
uniform int uAlphaTest;
out vec4 finalColor;
void main() {
    vec4 cf = floor(fragColor * 255.0 + 0.5);
    vec3 cs;
    float as;
    if (uTexOn == 1) {
        vec2 uv = fragUV + uGsShift.x * dFdx(fragUV) + uGsShift.y * dFdy(fragUV);
        vec4 t = texture(texture0, uv);
        vec3 ct = floor(t.rgb * 255.0 + 0.5);
        float at = (uTexAlpha8 == 1) ? floor(t.a * 255.0 + 0.5) : 128.0;
        cs = min(floor(ct * cf.rgb / 128.0), vec3(255.0));
        as = min(floor(at * cf.a / 128.0), 255.0);
    } else {
        cs = min(cf.rgb, vec3(255.0));
        as = cf.a;
    }
    if (uAlphaTest == 1 && as == 0.0) discard;
    if (uFix > 0.0) cs = ceil(cs * uFix / 128.0);
    finalColor = vec4(cs / 255.0, min(as / 128.0, 1.0));
}
)GLSL";

}  // namespace

PostGl& postGl() { return gGl; }

// PORT debug: environment BAROQUE_POST_DUMP=<dir> writes the intermediate targets of the last presented frame.
void postDumpTarget(const Target& t, const char* name) {
    static const std::string dir = [] {
        char* v = nullptr;
        size_t n = 0;
        std::string out;
        if (_dupenv_s(&v, &n, "BAROQUE_POST_DUMP") == 0 && v != nullptr) out = v;
        std::free(v);
        return out;
    }();
    if (dir.empty() || t.tex == 0) return;
    rlDrawRenderBatchActive();
    void* px = rlReadTexturePixels(t.tex, t.w, t.h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (px == nullptr) return;
    Image img{px, t.w, t.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    ImageFlipVertical(&img);
    ExportImage(img, (dir + "/" + name + ".png").c_str());
    UnloadImage(img);
}


bool postPassEnabled(char pass) {
    static const std::string sel = [] {
        char* v = nullptr;
        size_t n = 0;
        std::string out = "eiks";
        if (_dupenv_s(&v, &n, "BAROQUE_POST") == 0 && v != nullptr) out = v;
        std::free(v);
        return out;
    }();
    return sel.find(pass) != std::string::npos;
}

GsColor gsColor(float r, float g, float b, float a, float vu) {
    // FUN_003a0590 mode 1 (case at 0x3a0860): (int)(c * 255.0 + 0.5) per component
    auto byte = [](float c) {
        int v = static_cast<int>(c * 255.0f + 0.5f);
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    };
    // VU1 colour scale on RGB. UNVERIFIED: float -> integer conversion of the microcode taken as truncation.
    auto scaled = [&](float c) {
        const int v = static_cast<int>(static_cast<float>(byte(c)) * vu);
        return static_cast<uint8_t>(v > 255 ? 255 : v);
    };
    // FUN_0037d510: alpha byte -> (a >> 1) + (a & 1) (0xFF -> 0x80)
    const int ab = byte(a);
    GsColor c;
    c.r = scaled(r);
    c.g = scaled(g);
    c.b = scaled(b);
    c.a = static_cast<uint8_t>((ab >> 1) + (ab & 1));
    return c;
}

alchemy::Mat4 orthoGs(float l, float r, float b, float t, float n, float f) {
    alchemy::Mat4 m;
    float k = 1.0f / (r - l);
    m.m[0] = k * 2.0f;
    m.m[4] = 0.0f;
    m.m[8] = 0.0f;
    m.m[12] = k * -(r + l);
    k = 1.0f / (t - b);
    m.m[1] = 0.0f;
    m.m[5] = k * 2.0f;
    m.m[9] = 0.0f;
    m.m[13] = k * -(t + b);
    m.m[2] = 0.0f;
    m.m[6] = 0.0f;
    k = 1.0f / (f - n);
    m.m[10] = k * -2.0f;
    m.m[14] = k * -(f + n);
    m.m[3] = 0.0f;
    m.m[7] = 0.0f;
    m.m[11] = 0.0f;
    m.m[15] = 1.0f;
    return m;
}

bool PostGl::init() {
    if (program_ != 0) return true;
    program_ = rlLoadShaderCode(kVs, kFs);
    if (program_ == 0) {
        platform::log("post: shader failed");
        return false;
    }
    locMvp_ = rlGetLocationUniform(program_, "uMvp");
    locTex_ = rlGetLocationUniform(program_, "texture0");
    locTexOn_ = rlGetLocationUniform(program_, "uTexOn");
    locTexAlpha_ = rlGetLocationUniform(program_, "uTexAlpha8");
    locGsShift_ = rlGetLocationUniform(program_, "uGsShift");
    locFix_ = rlGetLocationUniform(program_, "uFix");
    locAlphaTest_ = rlGetLocationUniform(program_, "uAlphaTest");
    // raylib batch locations: vertex attributes by their default names, MVP left unset (uMvp is ours), sampler 0
    static int locs[RL_MAX_SHADER_LOCATIONS];
    for (int& l : locs) l = -1;
    locs[SHADER_LOC_VERTEX_POSITION] = rlGetLocationAttrib(program_, "vertexPosition");
    locs[SHADER_LOC_VERTEX_TEXCOORD01] = rlGetLocationAttrib(program_, "vertexTexCoord");
    locs[SHADER_LOC_VERTEX_COLOR] = rlGetLocationAttrib(program_, "vertexColor");
    locs[SHADER_LOC_MAP_DIFFUSE] = locTex_;
    locs_ = locs;
    return true;
}

void PostGl::draw(const Vtx* v, int count, const Params& p) {
    if (program_ == 0 || count <= 0) return;
    rlDrawRenderBatchActive();
    // render state
    rlColorMask(p.mask[0], p.mask[1], p.mask[2], p.mask[3]);
    if (p.depthTest) rlEnableDepthTest();
    else rlDisableDepthTest();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();   // GS primitives are not culled (culling is VU1 microcode of the scene path)
    switch (p.blend) {
        case Blend::Off:
            rlDisableColorBlend();
            break;
        case Blend::Alpha45:     // (Cs - Cd) * As + Cd
            rlEnableColorBlend();
            rlSetBlendFactors(kGlSrcAlpha, kGlOneMinusSrcAlpha, kGlFuncAdd);
            rlSetBlendMode(RL_BLEND_CUSTOM);
            break;
        case Blend::Add41:       // Cs * As + Cd
            rlEnableColorBlend();
            rlSetBlendFactors(kGlSrcAlpha, kGlOne, kGlFuncAdd);
            rlSetBlendMode(RL_BLEND_CUSTOM);
            break;
        case Blend::SubtractCs:  // (0 - Cs) * Ad + Cd with Ad = 0x80
        case Blend::SubtractFix: // (0 - Cs) * FIX + Cd (the product is formed in the shader)
            rlEnableColorBlend();
            rlSetBlendFactors(kGlOne, kGlOne, kGlFuncReverseSubtract);
            rlSetBlendMode(RL_BLEND_CUSTOM);
            break;
    }
    rlEnableShader(program_);
    rlSetUniformMatrix(locMvp_, alchemy::toRaylib(p.mvp));
    const int texOn = p.tex != 0 ? 1 : 0;
    const int texAlpha = p.texAlpha8 ? 1 : 0;
    const int at = p.alphaTestNotZero ? 1 : 0;
    // GS sample point = GL pixel centre + (-0.5, +0.5) original pixels (top-left; window y points up)
    const float shift[2] = {-0.5f * p.gsPixel[0], 0.5f * p.gsPixel[1]};
    const float fix = p.blend == Blend::SubtractFix ? static_cast<float>(p.fix) : 0.0f;
    rlSetUniform(locTexOn_, &texOn, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(locTexAlpha_, &texAlpha, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(locGsShift_, shift, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(locFix_, &fix, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(locAlphaTest_, &at, RL_SHADER_UNIFORM_INT, 1);
    rlSetShader(program_, locs_);
    rlBegin(RL_TRIANGLES);
    rlSetTexture(p.tex);   // after rlBegin: a mode change starts a draw call with the default texture
    for (int i = 0; i < count; ++i) {
        rlColor4ub(v[i].c.r, v[i].c.g, v[i].c.b, v[i].c.a);
        rlTexCoord2f(v[i].u, v[i].v);
        rlVertex3f(v[i].x, v[i].y, v[i].z);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetTexture(0);
    rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
}

void PostGl::restore() {
    rlDrawRenderBatchActive();
    rlColorMask(true, true, true, true);
    rlEnableColorBlend();
    rlSetBlendMode(RL_BLEND_ALPHA);
    rlDisableDepthTest();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void PostGl::ensureTarget(Target& t, int w, int h, bool linear) {
    if (t.fbo != 0 && t.w == w && t.h == h) return;
    const unsigned prev = activeFramebuffer();   // before the unload: rlUnloadFramebuffer leaves framebuffer 0 bound
    freeTarget(t);
    t.tex = rlLoadTexture(nullptr, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    const int filter = linear ? RL_TEXTURE_FILTER_LINEAR : RL_TEXTURE_FILTER_NEAREST;
    rlTextureParameters(t.tex, RL_TEXTURE_MAG_FILTER, filter);
    rlTextureParameters(t.tex, RL_TEXTURE_MIN_FILTER, filter);
    rlTextureParameters(t.tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters(t.tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    t.fbo = rlLoadFramebuffer();
    rlFramebufferAttach(t.fbo, t.tex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(t.fbo)) platform::log("post: incomplete framebuffer");
    t.w = w;
    t.h = h;
    rlEnableFramebuffer(prev);   // rlFramebufferAttach / rlFramebufferComplete leave framebuffer 0 bound
}

void PostGl::freeTarget(Target& t) {
    if (t.fbo != 0) rlUnloadFramebuffer(t.fbo);
    if (t.tex != 0) rlUnloadTexture(t.tex);
    t = Target{};
}

unsigned PostGl::activeFramebuffer() { return rlGetActiveFramebuffer(); }

void PostGl::bind(unsigned fbo, int w, int h) {
    rlDrawRenderBatchActive();
    rlEnableFramebuffer(fbo);
    rlViewport(0, 0, w, h);
}

void PostGl::blit(unsigned src, const Target& dst, int w, int h) {
    rlDrawRenderBatchActive();
    rlBindFramebuffer(RL_READ_FRAMEBUFFER, src);
    rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, dst.fbo);
    rlBlitFramebuffer(0, 0, w, h, 0, 0, w, h, kGlColorBufferBit);
    rlEnableFramebuffer(src);
}

void PostGl::clear(const Target& t, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rlDrawRenderBatchActive();
    rlColorMask(true, true, true, true);
    rlClearColor(r, g, b, a);
    rlEnableFramebuffer(t.fbo);
    rlViewport(0, 0, t.w, t.h);
    rlClearScreenBuffers();
}

}  // namespace baroque::game::post
