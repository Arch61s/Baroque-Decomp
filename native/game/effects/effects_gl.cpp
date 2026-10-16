// rlgl backend of the EXI draw pass (see effects_gl.hpp).
#include "game/effects/effects_gl.hpp"

#include "platform/platform.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace baroque::game {

namespace {

EffectsRenderer gRenderer;

constexpr int kMaxIndices = kExiMaxVertices * 3;

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

// GS TFX MODULATE + TCC=1 (Cv = Ct*Cf>>7, Av = At*Af>>7) with bilinear filtering done per tap so the CLAMP
// REGION_REPEAT wrap ((texel & MSK) | FIX, FUN_0038af40) applies to each of the four texels like on the GS.
// Vertex colour: RGB = byte * scale.rgb in GS units (0x80 = 1.0); alpha = byte/255 * scale.a in 1.0 = 0x80 units
// (the VU1 alpha model of ALCHEMY_NATIVE.md "Vertex colour scale"; UNVERIFIED). Alpha test GREATER AREF.
const char* kFs = R"GLSL(
#version 330
in vec2 fragUV;
in vec4 fragColor;
uniform sampler2D tex;
uniform int texEnabled;
uniform ivec4 region;      // UMSK, UFIX, VMSK, VFIX (texels)
uniform vec2 texSize;      // GS TW/TH: texel = uv * texSize
uniform ivec2 texReal;     // size of the bound GL texture (fetch clamp)
uniform vec4 colorScale;   // FUN_003841f0 value of FUN_001e7e90 (1, 1, 1, 0.5)
uniform float alphaRef;    // GS AREF
out vec4 finalColor;
vec4 fetchTexel(ivec2 t) {
    t.x = (t.x & region.x) | region.y;
    t.y = (t.y & region.z) | region.w;
    t = clamp(t, ivec2(0), texReal - ivec2(1));
    return texelFetch(tex, t, 0);
}
void main() {
    vec3 cf = fragColor.rgb * 255.0 * colorScale.rgb;   // GS colour units
    float af = fragColor.a * colorScale.a;               // 1.0 = 0x80
    vec3 rgb;
    float a;
    if (texEnabled == 1) {
        vec2 st = fragUV * texSize - vec2(0.5);
        ivec2 i0 = ivec2(floor(st));
        vec2 f = st - floor(st);
        vec4 t00 = fetchTexel(i0);
        vec4 t10 = fetchTexel(i0 + ivec2(1, 0));
        vec4 t01 = fetchTexel(i0 + ivec2(0, 1));
        vec4 t11 = fetchTexel(i0 + ivec2(1, 1));
        vec4 ct = mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y);
        rgb = min(ct.rgb * cf / 128.0, vec3(1.0));
        a = (ct.a * 255.0 / 128.0) * af;
    } else {
        rgb = min(cf, vec3(255.0)) / 255.0;
        a = af;
    }
    if (!(floor(a * 128.0 + 0.001) > alphaRef)) discard;
    finalColor = vec4(rgb, min(a, 1.0));
}
)GLSL";

Matrix toRaylib(const ExiMat& r) {
    Matrix o;
    o.m0 = r.m[0]; o.m1 = r.m[1]; o.m2 = r.m[2]; o.m3 = r.m[3];
    o.m4 = r.m[4]; o.m5 = r.m[5]; o.m6 = r.m[6]; o.m7 = r.m[7];
    o.m8 = r.m[8]; o.m9 = r.m[9]; o.m10 = r.m[10]; o.m11 = r.m[11];
    o.m12 = r.m[12]; o.m13 = r.m[13]; o.m14 = r.m[14]; o.m15 = r.m[15];
    return o;
}

// orig FUN_0038af40 (work/decompiled/0038af40.c): width -> mask (power-of-two size - 1).
int regionMask(int lo, int hi) {
    uint32_t w = static_cast<uint32_t>(hi - lo);
    uint32_t m = 0;
    while ((w >>= 1) != 0) m = (m << 1) | 1u;
    return static_cast<int>(m);
}

}  // namespace

EffectsRenderer& effectsRenderer() { return gRenderer; }

bool EffectsRenderer::init() {
    if (program_ != 0) return true;
    program_ = rlLoadShaderCode(kVs, kFs);
    if (program_ == 0) return false;
    locMvp_ = rlGetLocationUniform(program_, "mvp");
    locTex_ = rlGetLocationUniform(program_, "tex");
    locTexOn_ = rlGetLocationUniform(program_, "texEnabled");
    locRegion_ = rlGetLocationUniform(program_, "region");
    locTexSize_ = rlGetLocationUniform(program_, "texSize");
    locTexReal_ = rlGetLocationUniform(program_, "texReal");
    locColorScale_ = rlGetLocationUniform(program_, "colorScale");
    locAlphaRef_ = rlGetLocationUniform(program_, "alphaRef");

    vao_ = rlLoadVertexArray();
    rlEnableVertexArray(vao_);
    vbo_ = rlLoadVertexBuffer(nullptr, int(kExiMaxVertices * sizeof(ExiDrawVertex)), true);
    rlSetVertexAttribute(0, 3, RL_FLOAT, false, int(sizeof(ExiDrawVertex)), int(offsetof(ExiDrawVertex, x)));
    rlEnableVertexAttribute(0);
    rlSetVertexAttribute(1, 2, RL_FLOAT, false, int(sizeof(ExiDrawVertex)), int(offsetof(ExiDrawVertex, u)));
    rlEnableVertexAttribute(1);
    rlSetVertexAttribute(2, 4, RL_UNSIGNED_BYTE, true, int(sizeof(ExiDrawVertex)), int(offsetof(ExiDrawVertex, rgba)));
    rlEnableVertexAttribute(2);
    indices_.assign(size_t(kMaxIndices), 0);
    ebo_ = rlLoadVertexBufferElement(indices_.data(), int(indices_.size() * sizeof(uint16_t)), true);
    rlDisableVertexArray();

    // DATA/EXI/BQ.PNG: 256x512 palette PNG with tRNS; the PNG alpha stays 8-bit in GS units (render2d.hpp).
    const auto path = platform::assetRoot() / "DATA" / "EXI" / "BQ.PNG";
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        platform::log("effects: cannot open " + path.string());
        return false;
    }
    Image img = LoadImageFromMemory(".png", bytes.data(), int(bytes.size()));
    if (img.data == nullptr) return false;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    atlas_ = rlLoadTexture(img.data, img.width, img.height, img.format, 1);
    atlasW_ = img.width;
    atlasH_ = img.height;
    UnloadImage(img);
    return atlas_ != 0;
}

void EffectsRenderer::shutdown() {
    if (atlas_ != 0) rlUnloadTexture(atlas_);
    if (ebo_ != 0) rlUnloadVertexBuffer(ebo_);
    if (vbo_ != 0) rlUnloadVertexBuffer(vbo_);
    if (vao_ != 0) rlUnloadVertexArray(vao_);
    if (program_ != 0) rlUnloadShaderProgram(program_);
    atlas_ = ebo_ = vbo_ = vao_ = program_ = 0;
}

void EffectsRenderer::setScreenTexture(unsigned int textureId, int width, int height) {
    screen_ = textureId;
    screenW_ = width;
    screenH_ = height;
}

void EffectsRenderer::begin(const ExiView& view) {
    rlDrawRenderBatchActive();
    rlEnableShader(program_);
    ExiMat mvp;
    exiMul(mvp, view.view, view.projection);
    rlSetUniformMatrix(locMvp_, toRaylib(mvp));
    const int zero = 0;
    rlSetUniform(locTex_, &zero, RL_SHADER_UNIFORM_INT, 1);
    const float scale[4] = {1.0f, 1.0f, 1.0f, 0.5f};  // FUN_001e7e90: 0x3f800000 x3, 0x3f000000
    rlSetUniform(locColorScale_, scale, RL_SHADER_UNIFORM_VEC4, 1);
    const float aref = float(int(exiBitsToFloat(0x3c808081u) * 128.0f));  // FUN_003837e0: (int)(ref * 128) = 2
    rlSetUniform(locAlphaRef_, &aref, RL_SHADER_UNIFORM_FLOAT, 1);
    rlDisableBackfaceCulling();  // vt+0x358(0)
    stateValid_ = false;
}

// orig FUN_001eaa10 (work/decompiled/001eaa10.c, disasm 0x1eaa10)
void EffectsRenderer::setState(const ExiRenderState& s) {
    if (s.depthTest) rlEnableDepthTest();
    else rlDisableDepthTest();
    if (s.depthWrite) rlEnableDepthMask();
    else rlDisableDepthMask();
    rlEnableColorBlend();
    if (s.blend == 2) rlSetBlendFactors(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);   // (4,5) (Cs-Cd)As+Cd
    else if (s.blend == 1) rlSetBlendFactors(RL_SRC_ALPHA, RL_ONE, RL_FUNC_REVERSE_SUBTRACT); // GS (2,0,0,1): Cd-Cs*As
    else rlSetBlendFactors(RL_SRC_ALPHA, RL_ONE, RL_FUNC_ADD);                                // (4,1) Cs*As+Cd
    rlSetBlendMode(RL_BLEND_CUSTOM);
    rlEnableShader(program_);  // rlSetBlendMode may flush raylib's batch and unbind the program
    int region[4] = {0x1ff, 0, 0x1ff, 0};
    float texSize[2] = {512.0f, 512.0f};
    int texReal[2] = {1, 1};
    int texOn = s.textureOff ? 0 : 1;
    unsigned int texId = 0;
    if (texOn) {
        if (s.textureIndex == 1) {
            // FUN_00389820(ctx, 1, 0) + FUN_0038afc0(ctx, 1, 0, 0x200, 0, 0x200)
            texId = screen_;
            texReal[0] = screenW_;
            texReal[1] = screenH_;
        } else if (s.textureIndex == 0) {
            // vt+0x1e0(mgr+0xa0[0]) + FUN_0038afc0(ctx, tex, u0, u1, v0, v1)
            texId = atlas_;
            texReal[0] = atlasW_;
            texReal[1] = atlasH_;
            texSize[0] = float(atlasW_);
            texSize[1] = float(atlasH_);
            region[0] = regionMask(s.uvWindow[0], s.uvWindow[1]);
            region[1] = s.uvWindow[0];
            region[2] = regionMask(s.uvWindow[2], s.uvWindow[3]);
            region[3] = s.uvWindow[2];
        }
    }
    stateValid_ = !(texOn && texId == 0);
    rlSetUniform(locTexOn_, &texOn, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(locRegion_, region, RL_SHADER_UNIFORM_IVEC4, 1);
    rlSetUniform(locTexSize_, texSize, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(locTexReal_, texReal, RL_SHADER_UNIFORM_IVEC2, 1);
    rlActiveTextureSlot(0);
    if (texId != 0) rlEnableTexture(texId);
    else rlDisableTexture();
}

// orig FUN_001ecec0 (work/decompiled/001ecec0.c): triangle list or equal-length strips (converted to triangles).
void EffectsRenderer::draw(const ExiRenderState& s, const ExiDrawVertex* verts, int count) {
    if (!stateValid_ || count <= 0) return;
    rlUpdateVertexBuffer(vbo_, verts, int(count * sizeof(ExiDrawVertex)), 0);
    int n = 0;
    if (s.triangleList) {
        const int tris = count / 3;
        for (int i = 0; i < tris * 3; ++i) indices_[size_t(n++)] = uint16_t(i);
    } else if (s.vertsPerPrim >= 3) {
        const int len = s.vertsPerPrim;
        const int strips = count / len;
        for (int k = 0; k < strips; ++k) {
            const int b = k * len;
            for (int i = 0; i + 2 < len && n + 3 <= kMaxIndices; ++i) {
                indices_[size_t(n++)] = uint16_t(b + i);
                indices_[size_t(n++)] = uint16_t(b + i + 1);
                indices_[size_t(n++)] = uint16_t(b + i + 2);
            }
        }
    }
    if (n == 0) return;
    rlEnableVertexArray(vao_);
    rlUpdateVertexBufferElements(ebo_, indices_.data(), int(n * sizeof(uint16_t)), 0);
    rlDrawVertexArrayElements(0, n, nullptr);
    rlDisableVertexArray();
}

void EffectsRenderer::end() {
    rlDisableVertexArray();
    rlDisableTexture();
    rlDisableShader();
    rlEnableColorBlend();
    rlSetBlendMode(RL_BLEND_ALPHA);
    rlEnableDepthTest();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

// orig FUN_001120b0 (work/decompiled/001120b0.c) -> FUN_001e7e90(0x4e7ca0)
ExiPassStats effectsDraw(const ExiView& view) {
    EffectsRenderer& r = effectsRenderer();
    if (!r.ready()) return effects().manager().drawPass(view, nullptr);
    r.begin(view);
    const ExiPassStats st = effects().manager().drawPass(view, &r);
    r.end();
    return st;
}

namespace {

// PORT: the tick's FUN_001e7e90 pass as build attempts (effects.hpp ExiRecording), drawn by effectsReplay.
ExiRecording gRecording;

}  // namespace

ExiPassStats effectsTick(const ExiView& view) {
    gRecording.clear();
    // PORT diagnostic: BAROQUE_EXI_LOG=1 lists the live instances (effect id, position, time) every 30 ticks.
    static const bool exiLog = [] {
        char* v = nullptr;
        size_t n = 0;
        const bool on = _dupenv_s(&v, &n, "BAROQUE_EXI_LOG") == 0 && v != nullptr && v[0] == '1';
        std::free(v);
        return on;
    }();
    static int exiLogTick = 0;
    if (exiLog && (exiLogTick++ % 30) == 0) {
        ExiMat cam;
        if (!exiInvert(cam, view.view)) exiIdentity(cam);
        for (const ExiInstance* i = effects().manager().firstInstance(); i != nullptr; i = i->next) {
            char line[200];
            std::snprintf(line, sizeof line, "exi: id %d effect %d pos (%.1f, %.1f, %.1f) time %d camera (%.0f, %.0f, %.0f)",
                          i->id, i->effect, i->matrix.m[12], i->matrix.m[13], i->matrix.m[14], i->time, cam.m[12],
                          cam.m[13], cam.m[14]);
            platform::log(line);
        }
    }
    return effects().manager().drawPass(view, nullptr, &gRecording);
}

// The particles are rebuilt (FUN_001ebd90) with the presented camera: the original builds them with the camera of the
// frame it draws (*(mgr+0x1c) = 0x4e7b30), so billboards, the tag offset, the distance fade and the screen-copy UVs
// (UV mode 8) follow the drawn view. Building them with the tick's camera instead left billboards facing the logic
// camera and the mode-8 UVs sampling the wrong part of the frame copy whenever the presented camera differs (free
// mouse camera, interpolated frames). `screenProjection` = the 4:3 projection of the frame copy (feedExiScreen band).
void effectsReplay(const ExiView& view, const ExiMat& screenProjection) {
    EffectsRenderer& r = effectsRenderer();
    if (!r.ready() || gRecording.empty()) return;
    r.begin(view);
    ExiView build = view;
    build.projection = screenProjection;   // vt+0x310(0) of FUN_001ebd90's mode 8 (MVP keeps view.projection)
    effects().manager().replay(build, gRecording, &r);
    r.end();
}

void effectsReplay(const ExiView& view) { effectsReplay(view, view.projection); }

void effectsClearRecording() { gRecording.clear(); }

}  // namespace baroque::game
