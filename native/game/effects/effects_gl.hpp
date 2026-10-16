#pragma once
// GS-faithful rlgl backend for the EXI draw pass (FUN_001e7e90 state set-up, FUN_001eaa10 render state,
// FUN_001ecec0 primitive submission) and the per-frame hook of FUN_001120b0.
//
// Pass state set by FUN_001e7e90 (igVisualContext vtable 0x4db6a0 slots): alpha test on (+0x328), func 4 = GREATER
// (+0x330 -> GS ATST table 0x484520[4] = 6), ref 0x3c808081 (+0x338: AREF = (int)(ref * 128) = 2), blending on
// (+0x238), cull off (+0x358), depth test / write per record, lighting / caps 0x12, 4, 7, 0xd off, texture units
// 1 and 2 off (FUN_001e8ab0), vertex colour scale mode 2 with (1, 1, 1, 0.5) (FUN_003841b0 / FUN_003841f0) and the
// texture wrap override REGION_REPEAT (FUN_0038b120(ctx, 1, 3)); all restored afterwards.
#include "game/effects/effects.hpp"

#include <cstdint>
#include <vector>

namespace baroque::game {

class EffectsRenderer final : public ExiBatchSink {
public:
    bool init();      // shader, dynamic vertex buffer, DATA/EXI/BQ.PNG (mgr+0xa0[0])
    void shutdown();
    // Texture slot 1 (FUN_00389820(ctx, 1, 0)): the frame copy sampled by OBJS whose texture index is 1 (UV mode 8
    // writes 0..1 / 0..0.875 = a 512x448 frame in a 512x512 texture, row 0 = top). 0 = not provided: such batches
    // are skipped (UNVERIFIED stand-in until the scene renderer provides the copy).
    void setScreenTexture(unsigned int textureId, int width, int height);
    bool ready() const { return program_ != 0; }

    void begin(const ExiView& view);
    void end();
    void setState(const ExiRenderState& state) override;
    void draw(const ExiRenderState& state, const ExiDrawVertex* verts, int count) override;

private:
    unsigned int program_ = 0;
    unsigned int vao_ = 0, vbo_ = 0, ebo_ = 0;
    unsigned int atlas_ = 0;
    int atlasW_ = 0, atlasH_ = 0;
    unsigned int screen_ = 0;
    int screenW_ = 0, screenH_ = 0;
    int locMvp_ = -1, locTex_ = -1, locTexOn_ = -1, locRegion_ = -1, locTexSize_ = -1, locTexReal_ = -1,
        locColorScale_ = -1, locAlphaRef_ = -1;
    std::vector<uint16_t> indices_;
    bool stateValid_ = false;
};

EffectsRenderer& effectsRenderer();

// Per-frame hook = FUN_001120b0 -> FUN_001e7e90(0x4e7ca0): draws every live effect and advances it one tick.
// Call once per game tick after the 3-D scene and the 3-D-projected sprites (FUN_001075a0 / FUN_00105030 /
// FUN_0010ede0 / FUN_0012a4e0) and before FUN_001aa530 (RUNTIME.md 4 pass g). Without a GL context call
// effects().manager().drawPass(view, nullptr) instead (identical update).
ExiPassStats effectsDraw(const ExiView& view);

// PORT: presentation split. The original submits the pass inside the engine tick, so it is drawn exactly once per
// tick. The port presents several host frames per tick (and 60/120 Hz later), so effectsTick() runs the identical
// FUN_001120b0 pass once per tick into a recording, and effectsReplay() draws that recording on every host frame
// (with the frame's own view/projection). effectsClearRecording() drops it (scene exit / no Game scene).
ExiPassStats effectsTick(const ExiView& view);
void effectsReplay(const ExiView& view);
// effectsReplay with the frame copy's projection for the UV-mode-8 objects (the 4:3 band of a wider window, see
// PostRenderer::feedExiScreen) while `view.projection` draws (Hor+).
void effectsReplay(const ExiView& view, const ExiMat& screenProjection);
void effectsClearRecording();

}  // namespace baroque::game
