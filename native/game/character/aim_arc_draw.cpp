// Throw-trajectory sprite 0x50b600 (FUN_00139f80 / FUN_0013a150 / FUN_0013a060, player_control.cpp) on the 3-D sprite
// layer 0x4e6a00. Translated draw path of FUN_00101bd0, after FUN_001075a0 (3-D scene):
//   FUN_00104ff0 -> FUN_00105380(0x4e6a00): stream vertex = node position (+0x1c, 0 here) + vertex (+0x28 = 0x50b240,
//     no +0x34 copy), colour per vertex (+0x30 = 0x50b510, flag bit 0 clear), 0x3c vertices;
//   FUN_00105030: FUN_001020a0 state push (alpha test GREATER AREF 0x3c808081, depth test on, cull off, VU1 colour
//     scale (1,1,1,2)), view := 0x4e7b30 (camera view), projection := 0x4e664c (engine), FUN_00105390(0x4e6a00) ->
//     FUN_00104000(1): one GS triangle strip per node (prim 4, count - 2 triangles), FUN_001026e0(node +8 = 0):
//     blend (4, 5) = SRC_ALPHA / INV_SRC_ALPHA with z-write on; FUN_001024b0 restores the state.
// FUN_001020a0 / FUN_00104000 are the same routines as the 2-D polygon layer (render2d.cpp FUN_001050f0 path), so the
// vertex colours follow render2d's untextured convention: GS units, rgb * 255 / 128 clamped, alpha 0x80 = opaque
// (UNVERIFIED there too: As > 0x80 clamped to 1.0; the arc's colours carry alpha 0xff).
#include "alchemy/scene_render.hpp"
#include "game/character/character.hpp"
#include "game/character/player_control.hpp"
#include "game/player_move.hpp"
#include "platform/platform.hpp"

#include "raylib.h"
#include "rlgl.h"

namespace baroque::game {

namespace {

const char* kVs = R"GLSL(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 uMvp;
out vec4 col;
void main() { col = vertexColor; gl_Position = uMvp * vec4(vertexPosition, 1.0); }
)GLSL";

// render2d.cpp kFs mode 0 (TME=0: colour only)
const char* kFs = R"GLSL(
#version 330
in vec4 col;
out vec4 finalColor;
void main() {
    vec4 cf = col * 255.0;                       // GS vertex colour units
    if (cf.a <= 2.0) discard;                    // FUN_001020a0: GREATER, AREF (int)(0x3c808081 * 128) = 2
    finalColor = vec4(min(min(cf.rgb, vec3(255.0)) / 255.0 * (255.0 / 128.0), vec3(1.0)), min(cf.a / 128.0, 1.0));
}
)GLSL";

struct ArcRenderer {
    unsigned program = 0;
    int locMvp = -1;
    int locs[RL_MAX_SHADER_LOCATIONS] = {};
    bool failed = false;
    bool init() {
        if (program != 0) return true;
        if (failed) return false;
        program = rlLoadShaderCode(kVs, kFs);
        if (program == 0) {
            failed = true;
            platform::log("aim arc: shader failed");
            return false;
        }
        locMvp = rlGetLocationUniform(program, "uMvp");
        for (int& l : locs) l = -1;   // raylib batch: attributes by name, its own MVP left unset (uMvp is ours)
        locs[SHADER_LOC_VERTEX_POSITION] = rlGetLocationAttrib(program, "vertexPosition");
        locs[SHADER_LOC_VERTEX_TEXCOORD01] = rlGetLocationAttrib(program, "vertexTexCoord");
        locs[SHADER_LOC_VERTEX_COLOR] = rlGetLocationAttrib(program, "vertexColor");
        return true;
    }
};
ArcRenderer gArc;

}  // namespace

void playerAimArcDraw(const alchemy::Mat4& view, const alchemy::Mat4& proj) {
    const PlayerControl* ctl = playerCtl();
    if (ctl == nullptr || !ctl->aimMarker || !ctl->aimArcValid) return;   // iRam0050b600 == 0: no node in 0x4e6a00
    if (!gArc.init()) return;
    rlDrawRenderBatchActive();
    // FUN_001020a0 / FUN_001026e0(0): depth test on, z-write on, no culling, blend (4, 5)
    rlEnableDepthTest();
    rlEnableDepthMask();
    rlDisableBackfaceCulling();
    rlEnableColorBlend();
    rlSetBlendMode(RL_BLEND_ALPHA);
    // PORT: the 3-D picture's destination alpha is left alone (the passes after this one read the window target)
    rlColorMask(true, true, true, false);
    rlEnableShader(gArc.program);
    rlSetUniformMatrix(gArc.locMvp, alchemy::toRaylib(alchemy::mul(view, proj)));   // 0x4e7b30 x 0x4e664c
    rlSetShader(gArc.program, gArc.locs);
    rlBegin(RL_TRIANGLES);
    rlSetTexture(0);
    // FUN_00104000: triangle strip of the 0x3c vertices (node position 0: the vertices are world space)
    for (int i = 0; i + 2 < 60; ++i) {
        for (int k = 0; k < 3; ++k) {
            const int vi = i + k;
            const uint32_t c = ctl->aimArcColours[vi];
            rlColor4ub(static_cast<unsigned char>(c & 0xff), static_cast<unsigned char>((c >> 8) & 0xff),
                       static_cast<unsigned char>((c >> 16) & 0xff), static_cast<unsigned char>(c >> 24));
            rlTexCoord2f(0.0f, 0.0f);
            rlVertex3f(ctl->aimArcVerts[vi][0], ctl->aimArcVerts[vi][1], ctl->aimArcVerts[vi][2]);
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault());
    // FUN_001024b0: restore (the caller's 3-D pass keeps the depth test on)
    rlColorMask(true, true, true, true);
    rlEnableBackfaceCulling();
}

}  // namespace baroque::game
