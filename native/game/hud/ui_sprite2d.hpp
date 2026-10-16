#pragma once
// Sprite2D on top of the game's UI layer 0x4e68c0 (game/ui/sprite.hpp), text object 0x4e7e90 (game/ui/text.hpp) and
// texture registry 0x4e6a68 (game/ui/texture_registry.hpp). Every drawSprite() becomes a UI sprite that lives until
// the next beginFrame() (called at the top of hudGameTick), i.e. for exactly one presented frame like the glyph
// sprites of the text object (FUN_00112c60). Port bridge for the windows that keep HudSprite records (item ribbon,
// item screen, 2D map, message box); the status HUD uses persistent UI sprites directly.
#include "game/hud/sprite2d.hpp"
#include <vector>

namespace baroque::game {
namespace ui { struct Sprite; }

class UiSprite2D : public Sprite2D {
public:
    void beginFrame();   // destroys last tick's transient sprites
    void drawSprite(int texSlot, float u0, float v0, float u1, float v1, float cx, float cy, float z, float hw,
                    float hh, uint32_t colourABGR, int layerFlag = 1) override;
    void drawText(const char* sjis, float x, float y, float z, float halfW, float halfH, uint32_t colourABGR, bool proportional) override;
    float measureText(const char* sjis, float halfW, bool proportional) override;
    int createDynamicTexture(int w, int h) override;
    void updateDynamicTexture(int slot, const uint32_t* rgbaPixels) override;
    void releaseDynamicTexture(int slot) override;
    int liveCount() const { return static_cast<int>(live_.size()); }
private:
    std::vector<ui::Sprite*> live_;
};
UiSprite2D& uiSprite2D();

}  // namespace baroque::game
