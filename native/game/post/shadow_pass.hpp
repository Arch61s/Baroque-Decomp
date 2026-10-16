#pragma once
// Player shadow pre-pass object at renderer +0xd4 (0x4e79b4): init FUN_001101e0 (from the renderer init
// FUN_001085a0), per-frame pre-pass FUN_001113d0 (step 2 of the 3-D pass FUN_001075a0), on / off FUN_00111db0 /
// FUN_00111df0 (EVT ops 87 / 88). work/reports/POST_NATIVE.md §3.
//
// 1. Casters (models registered by FUN_00109f10 into the caster root +0x38 = 0x4e79ec: every model created with
//    FUN_001099e0(.., 1, ..), i.e. characters, item entities, dungeon objects, projectiles) are drawn from straight above into a 256x256 target (+0x8, 16-bit): ortho
//    (-256..256, -256..256, near 10, far 700) around a centre 512 units ahead of the camera (+0x18 / +0x20) at the
//    height player y + 500 (+0x1c, written by FUN_00142fe0 in the physics tick). The caster root's attributes make
//    them flat white silhouettes on black: fog on, colour (1,1,1,1), linear 0..1; lighting, blending, depth write
//    and texture matrix off; colour mask RGB; scissor (1, 1, 254, 254). Characters swap to their "sh" skin and the
//    player drops its attached skins for the traversal (model_fx.hpp modelShadowCasterPassBegin).
// 2. A gradient ring (10-vertex strip, inner square +-96 colour 0, outer +-128 colour 1) is subtracted from the
//    target (Cd - Cs * Ad) so the silhouettes fade out towards the border.
// 3. Consumer node +0x34 (inserted as child 1 of the scene root, after the scene group): five attr sets, each with a
//    texture matrix that maps world (x, z) to the target with a jitter of (-+j, -+j) / (0, 0), j = 2.75 / 3.0 by
//    frame parity, draw the receiver group 0x4e79b0 (renderer +0xd0): the floor triangles of the collision meshes
//    (FUN_001277c0 per room, FUN_00123280 corridor pieces) with UV = world (x, z). Each pass subtracts
//    texture x colour (0.75,0.75,0.75,1) x FIX 3/128 from the frame (igBlendFunctionAttr _eq 4, A/B/C/D 2/0/2/1,
//    _blendConstant 3), alpha test NOTEQUAL 0, depth write off, fog and lighting off.
#include "alchemy/scene_render.hpp"
#include "game/dungeon.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace baroque::game::post {

class ShadowPass {
public:
    uint8_t enabled = 1;        // +0x10 (FUN_001101e0 ends with 1; the renderer inserts the consumer node at init)
    uint32_t counter = 0;       // +0x14 frame counter (jitter parity)
    float jitter = 3.0f;        // PORT: jitter of the current tick's pre-pass (recorded by tick())

    void on();                  // orig FUN_00111db0
    void off();                 // orig FUN_00111df0
    // Per displayed frame (FUN_001113d0 head): +0x14 += 1, jitter 2.75 (even) / 3.0 (odd).
    void tick();

    // Receiver group 0x4e79b0 of a floor: FUN_001277c0 for every room collision, FUN_00123280 for corridor pieces.
    void buildReceivers(const Dungeon& d);
    void clearReceivers();

    // Pre-pass (FUN_001113d0, presentation): `camWorld` = camera world matrix rows (inverse of the Alchemy camera
    // matrix cam+0x64), `eyeY` = player y + 500 (+0x1c). drawCasters(renderer, pass) draws the casters with the
    // renderer set up by the pre-pass (pass 0 opaque, 1 blended). Leaves `displayFbo` bound with the viewport
    // (0, 0, screenW, screenH).
    using CasterDraw = std::function<void(alchemy::SceneRenderer&, int pass)>;
    void renderCasters(alchemy::SceneRenderer& r, const alchemy::Mat4& camWorld, float eyeY, const CasterDraw& drawCasters,
                       unsigned displayFbo, int screenW, int screenH);
    // The whole caster root 0x4e79ec: every task model linked to it (characters, item entities, dungeon objects
    // OBJ001.., projectiles: FUN_001099e0(.., 1, ..) / FUN_00109f10, unlinked by the material fader FUN_0010a0a0), drawn
    // with the presentation interpolation `alpha`, with the pass's skin swaps (model_fx.hpp). Preferred entry point.
    void renderCasters(alchemy::SceneRenderer& r, const alchemy::Mat4& camWorld, float eyeY, float alpha,
                       unsigned displayFbo, int screenW, int screenH);
    // Consumer node (+0x34) in the scene: draws the receivers five times into the bound framebuffer.
    void drawReceivers(const alchemy::Mat4& view, const alchemy::Mat4& proj);

private:
    void render(alchemy::SceneRenderer& r, const alchemy::Mat4& camWorld, float eyeY, const CasterDraw& drawCasters,
                unsigned displayFbo, int screenW, int screenH);
    float centre_[3] = {0.0f, 0.0f, 0.0f};   // +0x18 / +0x1c / +0x20
    bool rendered_ = false;                  // PORT: the target holds this frame's casters
    std::vector<float> tris_;                // receiver triangles (x, y, z per vertex)
};

ShadowPass& shadowPass();   // renderer 0x4e78e0 + 0xd4

}  // namespace baroque::game::post
