#include "game/hud/ui_sprite2d.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include <cstring>

namespace baroque::game {

namespace {
UiSprite2D gUiSprite2D;
}
UiSprite2D& uiSprite2D() { return gUiSprite2D; }

void UiSprite2D::beginFrame() {
    for (ui::Sprite* s : live_) ui::sprites().destroy(s);
    live_.clear();
}

void UiSprite2D::drawSprite(int texSlot, float u0, float v0, float u1, float v1, float cx, float cy, float z, float hw,
                            float hh, uint32_t colourABGR, int layerFlag) {
    // FUN_00107130 / FUN_001070a0 / FUN_00107180 / FUN_00107070 (alloc + link), then the fields FUN_00166820 writes
    ui::Sprite* s = ui::sprites().alloc(texSlot, layerFlag);
    if (s == nullptr) return;
    s->cx = cx; s->cy = cy; s->z = z; s->hw = hw; s->hh = hh; s->color = colourABGR;
    s->u0 = u0; s->v0 = v0; s->u1 = u1; s->v1 = v1;
    live_.push_back(s);
}

// FUN_00112e80(0x4e7e90, "%s", text) with the globals 0x4e9698.. set like the callers do
void UiSprite2D::drawText(const char* sjis, float x, float y, float z, float halfW, float halfH, uint32_t colourABGR, bool proportional) {
    ui::TextRenderer& t = ui::text();
    t.x = x; t.y = y; t.z = z; t.halfW = halfW; t.halfH = halfH; t.color = colourABGR;
    t.layerFlag = 1;
    t.proportional = proportional ? 1 : 0;
    t.draw("%s", sjis);
}

// FUN_00113cf0
float UiSprite2D::measureText(const char* sjis, float halfW, bool proportional) {
    ui::TextRenderer& t = ui::text();
    const float keepW = t.halfW;
    const int keepP = t.proportional;
    t.halfW = halfW;
    t.proportional = proportional ? 1 : 0;
    const float w = t.measure("%s", sjis);
    t.halfW = keepW;
    t.proportional = keepP;
    return w;
}

// FUN_00106350(0x4e6a68, w, h, 0, 1)
int UiSprite2D::createDynamicTexture(int w, int h) { return ui::textures().createDynamic(w, h); }

// FUN_00106630 buffer write + FUN_00106650 upload
void UiSprite2D::updateDynamicTexture(int slot, const uint32_t* rgbaPixels) {
    uint32_t* px = ui::textures().dynamicPixels(slot);
    if (px == nullptr || rgbaPixels == nullptr) return;
    const size_t n = static_cast<size_t>(ui::textures().width(slot)) * static_cast<size_t>(ui::textures().height(slot));
    if (px != rgbaPixels) std::memcpy(px, rgbaPixels, n * sizeof(uint32_t));
    ui::textures().dynamicUpload(slot);
}

void UiSprite2D::releaseDynamicTexture(int slot) { ui::textures().release(slot); }

}  // namespace baroque::game
