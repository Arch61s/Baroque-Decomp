#pragma once
// Draws of the screen-effects object 0x4e7b80 (state in screen_effects.hpp): pass e FUN_0010ede0 (distance blur),
// pass i FUN_0010e0c0 (bloom), pass k FUN_0010fb90 (film grain), in the 3-D raster of the window.
//
// Scaling (work/reports/POST_NATIVE.md §5): the original renders a 512x448 raster shown as 4:3. The port's 3-D
// raster is the whole window (W x H, any aspect). The passes keep the original relationship to that raster:
// one "original pixel" is H/448 window pixels tall and H/384 wide (4:3), the virtual raster is Wv = 384 * W / H by
// 448 (512 x 448 at 4:3). The bloom target stays 128 texels tall and Wv/4 wide (128 x 128 at 4:3), the effect-3
// copy 256 x Wv/2 (256 x 256 at 4:3), every texel offset of the original is kept in texels, NOISE_T keeps 4 tiles
// per 512 original pixels. So the result has the same look at any resolution (same blur radii, same grain size).
#include "alchemy/scene_render.hpp"

namespace baroque::game::post {

class PostRenderer {
public:
    // orig FUN_0010db20 GL part: textures 0x1a SUBTRACT.PNG / 0x1b NOISE_T.PNG (FUN_00106090), NOISE_T sampler.
    void construct();
    // orig FUN_0010def0 GL part: FUN_00105f00(0x1b), FUN_00105f00(0x1a); targets released.
    void destroy();
    // Pass e, FUN_0010ede0: needs the depth buffer of the scene; `proj` = the scene projection (view = identity),
    // `fovDeg` = engine +0x8c (iRam004e66cc, the horizontal FOV at 4:3).
    // Afterwards (every frame, also with the pass off) the frame as pass g (FUN_001120b0, EXI) finds it is handed to
    // the EXI renderer as its texture slot 1 (effectsRenderer().setScreenTexture, see feedExiScreen).
    void drawDistanceBlur(int screenW, int screenH, const alchemy::Mat4& proj, float fovDeg);
    // Pass i, FUN_0010e0c0.
    void drawBloom(int screenW, int screenH);
    // Pass k, FUN_0010fb90.
    void drawNoise(int screenW, int screenH);

private:
    void distanceBlur(int screenW, int screenH, const alchemy::Mat4& proj, float fovDeg);
    // EXI texture slot 1 (FUN_00389820(ctx, 1, 0), region 0..0x200 x 0..0x200 by FUN_0038afc0): the 512x448 frame
    // buffer read as a 512x512 texture by the OBJS with texture index 1 (UV mode 8). PORT: a copy of the frame taken
    // right before the EXI replay (the GS samples the live frame buffer, including EXI objects drawn earlier in the
    // same pass: not modelled).
    void feedExiScreen(int screenW, int screenH);
};

PostRenderer& postRenderer();

}  // namespace baroque::game::post
