#pragma once
// Model presentation of the game's model wrapper (FUN_00109790 object, vtable 0x4cc990: +4 parent scene node, +8
// igSwitch root, +0x10 igTransform, +0x24 material fader, +0x2c flags) beyond the plain draw:
//   * material fader FUN_0010cb30 object (vtable 0x4cc9a8, 0x60 bytes): FUN_0010a0a0 start, FUN_0010a130 end,
//     FUN_0010a160 per publish (FUN_00109d40), set-up FUN_0010af90, colour step FUN_0010c470 / FUN_0010c520, removal
//     FUN_0010a060 -> FUN_0010c320. It re-parents the model graph under two igSwitch -> igOverrideAttrSet branches:
//       X (+0x1c): igColorMaskAttr, igMaterialModeAttr, igLightingStateAttr (+ igBlendStateAttr / igAlphaStateAttr /
//                  igAlphaFunctionAttr depending on the mode) - a depth-only pre-pass (modes 2/3) or the plain model
//                  (mode 1);
//       Y (+0x20): igMaterialAttr, igMaterialModeAttr, igBlendFunctionAttr, igBlendStateAttr(1),
//                  igDepthWriteStateAttr(0) - the faded material colour;
//     and FUN_0010af90 removes the model graph from the transform (FUN_003dee90(.., +0x10, 0)), so only X / Y draw.
//     Modes (flags & 3): 0 additive colour only (death fades, invisibility), 1 model + additive glow (hit tint),
//     2 alpha fade (SRC_ALPHA, 1-SRC_ALPHA), 3 additive translucent (ghost NPCs). 0x10 flicker (game RNG),
//     0x20 auto-remove when done, 0x80 Y material mode 2 instead of 0.
//   * shadow flag FUN_00109f10 / FUN_00109f70 (+0x2c bit 0x20000000 = linked to the shadow-caster root 0x4e79ec,
//     0x10000000 = never). The shadow pre-pass (game/post/shadow_pass, FUN_001113d0) draws exactly the linked
//     models (modelShadowCasterPassBegin / End below); the material fader unlinks a model while it runs.
#include "alchemy/scene_render.hpp"

#include <array>
#include <cstdint>

namespace baroque::game {

struct ChrModel;
struct ChrModelPart;
struct Chr;

// The model's own light (FUN_001099e0 mode 1 without 0x4000000: +0x14 igAttrSet -> +0x1c igLightStateSet holding
// +0x20 igLightStateAttr over the +0x18 igLightAttr; type 0 directional, ambient / diffuse / specular (0,0,0,1),
// direction (0,1,0), state disabled). FUN_0011f160 drives it from the room's RDT lights.
struct ModelLight {
    bool exists = false;                 // +0x14 attribute group built
    bool enabled = false;                // igLightStateAttr vt+0x84
    std::array<float, 4> diffuse{0.0f, 0.0f, 0.0f, 1.0f};   // igLightAttr vt+0x84
    std::array<float, 3> direction{0.0f, 1.0f, 0.0f};       // igLightAttr vt+0x94
};

struct MaterialFader {
    bool exists = false;                 // wrapper +0x24 != 0
    bool built = false;                  // +0x14 / +0x1c set (FUN_0010af90 first call)
    uint32_t flags = 0;                  // +0x04
    int64_t lastClock = 0;               // +0x08 (engine clock of the last step)
    int frames = 1;                      // +0x24
    int count = 0;                       // +0x28
    std::array<float, 4> from{};         // +0x2c
    std::array<float, 4> cur{0.0f, 0.0f, 0.0f, 1.0f};   // +0x3c (FUN_0010cb30: alpha 1)
    std::array<float, 4> to{};           // +0x4c
    bool swA = false, swB = false;       // child-mask bit 0 of the switches +0x14 / +0x18
    alchemy::AttrOverride x, y;          // attribute lists of the override sets +0x1c / +0x20
};

// orig FUN_0010a0a0(model, mode, frames, from (null = keep the current colour), to)
void modelFadeStart(ChrModel& m, uint32_t mode, int frames, const float* from, const float* to);
void modelFadeEnd(ChrModel& m);          // orig FUN_0010a130
void modelPublishFx(ChrModel& m);        // orig FUN_0010a160 (start of FUN_00109d40)
void modelShadowOn(ChrModel& m, bool clearNever);   // orig FUN_00109f10(model, param)
void modelShadowOff(ChrModel& m, bool never);       // orig FUN_00109f70(model, param)
// Read-only queries. A model is a shadow caster while it is linked to the caster root 0x4e79ec (+0x2c bit
// 0x20000000): FUN_001099e0 mode 1 links it, FUN_0010a0a0 (material fader start) unlinks it, FUN_0010a130 relinks.
bool modelShadowCaster(const ChrModel& m);
bool modelInFader(const ChrModel& m);              // wrapper +0x24 != 0 (fader between FUN_0010a0a0 and its removal)

// Shadow pre-pass caster traversal (FUN_001113d0, between the caster view set-up and the traversal of 0x4e79ec).
// While active, modelSubmitPart draws only caster models (modelShadowCaster) and applies the pass's swaps:
//   * every task of type < 3 whose model is loaded (+0x2c bit 0x80000000) draws the igSkin named "sh" of its actor
//     (0x42ca00, from the animation database's skin list model +0x38 -> +0x1c) instead of its appearance skin; the
//     pass restores skin list [0] afterwards (= the appearance skin in every CHR file);
//   * the player (0x50b144) draws without its appearance's attached skins (BODY / HAIR / coat parts, appearance
//     +0x10, removed for the pass with FUN_003c7cc0), plus the coat's shadow skin player +0x6c when set (CT01_KAGE /
//     CT06_KAGE, FUN_001b2230). Attached graphs (the HEAD on its joint) stay.
void modelShadowCasterPassBegin();
void modelShadowCasterPassEnd();
// PORT: whether a model of a task of type >= 3 (item entity, object) was drawn since the last reset (lets the shadow
// pass add those casters when its caller drew only the characters).
bool modelShadowCasterPassSawObjects(bool reset);

// Lit branch (render list 0x4e78e0 +0x14): models loaded by FUN_001099e0 in mode 1 (characters, heads, items,
// projectiles, O??M, the E19 shell) are lit by the render list's 5 light slots (event module renderLights(); slot 0
// directional, 1..4 point) and their own ModelLight; everything else (maps, mode-0 objects) is unlit.
void modelBeginDraw(const ChrModel& m, alchemy::SceneRenderer& r);   // sets the renderer's LightEnv for the model
void modelEndDraw(alchemy::SceneRenderer& r);
// orig FUN_0011f160 (floor context 0x50a350, called by FUN_0011edf0 each tick on generated floors): the nearest RDT
// light of the character's room within its radius drives the model light (colour faded over the last quarter of
// the radius, direction by type: table 0x48db90 or, type 6, from the light to the character).
void chrRoomLights();

// Submits one part of a model (main model, skins, attached parts) in the given pass (-1 all, 0 opaque, 1 blended)
// with the part's own override set, its E09-style glow copy and the model's fader branches.
void modelSubmitPart(const ChrModel& m, const ChrModelPart& part, const alchemy::Mat4& world,
                     alchemy::SceneRenderer& r, int pass);

}  // namespace baroque::game
