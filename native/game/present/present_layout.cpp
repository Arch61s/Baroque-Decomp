// PORT widescreen layout (see present_layout.hpp).
#include "game/present/present_layout.hpp"

#include "game/present/present_config.hpp"
#include "game/render2d.hpp"

#include <algorithm>
#include <cmath>

namespace baroque::game::present {

ScreenLayout presentLayout(int screenW, int screenH) {
    ScreenLayout L;
    const Viewport43 box = viewport43(screenW, screenH);
    L.boxX = box.x;
    L.boxY = box.y;
    L.boxW = box.w;
    L.boxH = box.h;
    // area: the window, at most max_aspect wide, at least the 4:3 box
    L.x = box.x;
    L.y = box.y;
    L.w = box.w;
    L.h = box.h;
    if (screenH > 0 && box.w > 0 && box.w < screenW) {
        const float cap = std::max(portSettings().maxAspect, 4.0f / 3.0f);
        const int maxW = static_cast<int>(std::lround(static_cast<double>(screenH) * cap));
        L.w = std::clamp(maxW, box.w, screenW);
        L.w -= (L.w - box.w) & 1;   // the area is the box plus the same whole number of pixels on each side
        const int side = (L.w - box.w) / 2;
        L.x = box.x - side;
        L.y = 0;
        L.h = screenH;
        L.delta = static_cast<float>(side) * 512.0f / static_cast<float>(box.w);
    }
    L.hudDelta = portSettings().classicHud ? 0.0f : L.delta;
    return L;
}

alchemy::Mat4 presentProjection(float fovDeg, float nearZ, float farZ, float aspect) {
    // raylib's DEG2RAD / RAD2DEG expressions, so the 4:3 result is bit-identical to the pass before the split
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDeg2Rad = kPi / 180.0f, kRad2Deg = 180.0f / kPi;
    const float fovY = 2.0f * std::atan(std::tan(fovDeg * 0.5f * kDeg2Rad) / (4.0f / 3.0f)) * kRad2Deg;
    return alchemy::Mat4::perspective(fovY, aspect, nearZ, farZ);
}

}  // namespace baroque::game::present
