#include "game/hud/hud.hpp"
#include "game/hud/message_log.hpp"
#include "game/items/item_world.hpp"
#include "game/render2d.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/texture_registry.hpp"
#include <cstdio>
#include <cstring>

namespace baroque::game {

namespace {
constexpr uint32_t kOpaque = 0x407f7f7f;   // every HUD sprite colour (alpha 0x40 = shown)

// 0x48ee80: HP digit rects {x, y, w, h} (VT = same x, y + 23) [V floats]
const float kHpDigitPos[7][4] = {{99, 26, 7, 12}, {107, 26, 7, 12}, {115, 26, 7, 12}, {124, 26, 5, 12}, {130, 26, 7, 12}, {138, 26, 7, 12}, {146, 26, 7, 12}};
// 0x48f500: depth digits [V floats]
const float kDepthDigitPos[8][4] = {{47, 72, 6, 12}, {53, 72, 7, 12}, {61, 72, 7, 12}, {69, 72, 7, 12}, {77, 72, 7, 12}, {85, 72, 7, 12}, {93, 72, 7, 12}, {101, 72, 7, 12}};
// 0x48eef0 / 0x48efc0: frame art TEX_UI01 {x, y, w, h} / {u0, v0, u1, v1} [V floats]
const float kFramePos[13][4] = {{0, 0, 23, 89}, {0, 89, 23, 39}, {0, 128, 12, 41}, {19, 128, 4, 12}, {23, 0, 112, 25}, {23, 25, 16, 52}, {47, 25, 23, 24},
                                {39, 49, 31, 28}, {35, 77, 17, 12}, {23, 89, 29, 24}, {70, 40, 30, 5}, {70, 63, 29, 5}, {52, 85, 35, 4}};
const float kFrameUv[13][4] = {{105, 27, 128, 116}, {11, 66, 34, 105}, {52, 25, 64, 66}, {48, 52, 52, 64}, {11, 0, 123, 25}, {64, 25, 80, 77}, {80, 25, 103, 49},
                               {65, 77, 96, 105}, {34, 90, 51, 102}, {34, 66, 63, 90}, {11, 52, 41, 57}, {11, 57, 40, 62}, {11, 62, 46, 66}};
// 0x48f170: sliding indicators {normal rect, shifted rect} / 0x48f230 UVs [V floats]
const float kSlidePos[6][2][4] = {{{39, 19, 9, 10}, {39, 22, 9, 10}}, {{20, 43, 10, 9}, {23, 43, 10, 9}}, {{56, 43, 10, 9}, {53, 43, 10, 9}},
                                  {{39, 66, 9, 10}, {39, 61, 9, 10}}, {{51, 83, 4, 7}, {79, 83, 4, 7}}, {{40, 94, 7, 4}, {40, 103, 7, 4}}};
const float kSlideUv[6][4] = {{87, 67, 96, 77}, {86, 58, 96, 67}, {86, 49, 96, 58}, {56, 95, 65, 105}, {83, 105, 87, 112}, {84, 112, 91, 116}};
// 0x48f090 / 0x48f100: item-slot frame (bottom right) [V floats]
const float kItemFramePos[7][4] = {{501, 302, 11, 119}, {471, 421, 41, 27}, {426, 436, 45, 12}, {366, 425, 60, 23}, {343, 442, 23, 6}, {488, 359, 13, 7}, {479, 331, 9, 53}};
const float kItemFrameUv[7][4] = {{0, 0, 11, 119}, {11, 25, 52, 52}, {83, 116, 128, 128}, {23, 105, 83, 128}, {0, 122, 23, 128}, {92, 109, 105, 116}, {96, 56, 105, 109}};
// 0x48f290: talk-mode strip {talk rect, normal rect (all zero)} / 0x48f430 UVs TEX_UI02 [V floats]
const float kTalkStripPos[13][4] = {{23, 112, 12, 24}, {35, 112, 8, 24}, {43, 112, 8, 24}, {51, 112, 8, 24}, {59, 112, 8, 24}, {67, 112, 8, 24}, {75, 112, 8, 24},
                                    {83, 112, 8, 24}, {91, 112, 8, 24}, {99, 112, 8, 24}, {107, 112, 8, 24}, {114, 112, 7, 24}, {115, 112, 13, 24}};
const float kTalkStripUv[13][4] = {{0, 0, 12, 24}, {12, 0, 20, 24}, {12, 0, 20, 24}, {12, 0, 20, 24}, {12, 0, 20, 24}, {12, 0, 20, 24}, {12, 0, 20, 24},
                                   {12, 0, 20, 24}, {12, 0, 20, 24}, {12, 0, 20, 24}, {12, 0, 20, 24}, {12, 0, 20, 24}, {20, 0, 33, 24}};
// 0x48ff90: ribbon frame; 0x48ffb0 / 0x490080: 13 ribbon parts TEX_UI02 [V floats]
const float kRibbonFrame[8] = {449, 370, 58, 10, 33, 13, 91, 23};
const float kRibbonPartPos[13][4] = {{450, 359, 11, 46}, {401, 359, 9, 46}, {410, 357, 40, 7}, {361, 405, 4, 25}, {495, 405, 4, 26}, {365, 405, 48, 4}, {413, 405, 40, 4},
                                     {453, 405, 42, 4}, {365, 427, 48, 4}, {453, 427, 42, 4}, {413, 427, 40, 14}, {410, 364, 40, 41}, {365, 409, 130, 18}};
const float kRibbonPartUv[13][4] = {{111, 0, 122, 46}, {102, 0, 111, 46}, {58, 42, 98, 49}, {0, 39, 4, 65}, {4, 39, 8, 65}, {0, 31, 48, 35}, {8, 39, 48, 43},
                                    {8, 43, 50, 47}, {0, 35, 48, 39}, {8, 47, 50, 51}, {8, 51, 48, 65}, {50, 42, 58, 50}, {50, 42, 58, 50}};
// 0x490150: 27 tick marks x 4 hint variants; 0x490810 UVs [V floats]
const float kRibbonTick[27][4][4] = {
    {{461, 381, 2, 21}, {461, 381, 0, 0}, {461, 381, 0, 0}, {461, 381, 2, 21}}, {{463, 381, 2, 21}, {463, 381, 0, 0}, {463, 381, 0, 0}, {463, 381, 2, 21}},
    {{465, 381, 2, 21}, {465, 381, 0, 0}, {465, 381, 0, 0}, {465, 381, 2, 21}}, {{467, 381, 2, 21}, {467, 381, 0, 0}, {467, 381, 0, 0}, {467, 381, 2, 21}},
    {{469, 381, 2, 21}, {469, 381, 0, 0}, {469, 381, 0, 0}, {469, 381, 2, 21}}, {{471, 381, 2, 21}, {471, 381, 0, 0}, {471, 381, 0, 0}, {471, 381, 2, 21}},
    {{473, 381, 2, 21}, {473, 381, 0, 0}, {473, 381, 0, 0}, {473, 381, 2, 21}}, {{475, 381, 2, 21}, {475, 381, 0, 0}, {475, 0, 0, 0}, {475, 381, 2, 21}},
    {{475, 381, 9, 21}, {461, 381, 9, 21}, {461, 381, 9, 21}, {475, 381, 9, 21}},
    {{385, 381, 2, 21}, {385, 381, 0, 0}, {385, 381, 0, 0}, {385, 381, 2, 21}}, {{387, 381, 2, 21}, {387, 381, 0, 0}, {387, 381, 0, 0}, {387, 381, 2, 21}},
    {{389, 381, 2, 21}, {389, 381, 0, 0}, {389, 381, 0, 0}, {389, 381, 2, 21}}, {{391, 381, 2, 21}, {391, 381, 0, 0}, {391, 381, 0, 0}, {391, 381, 2, 21}},
    {{393, 381, 2, 21}, {393, 381, 0, 0}, {393, 381, 0, 0}, {393, 381, 2, 21}}, {{395, 381, 2, 21}, {395, 381, 0, 0}, {395, 381, 0, 0}, {395, 381, 2, 21}},
    {{397, 381, 2, 21}, {397, 381, 0, 0}, {397, 381, 0, 0}, {397, 381, 2, 21}}, {{399, 381, 2, 21}, {399, 381, 0, 0}, {399, 381, 0, 0}, {399, 381, 2, 21}},
    {{401, 381, 2, 21}, {401, 381, 0, 0}, {401, 381, 0, 0}, {401, 381, 2, 21}},
    {{376, 381, 9, 21}, {392, 381, 9, 21}, {392, 381, 9, 21}, {376, 381, 9, 21}},
    {{422, 345, 26, 2}, {422, 345, 0, 0}, {422, 345, 26, 2}, {422, 345, 0, 0}}, {{422, 347, 26, 2}, {422, 347, 0, 0}, {422, 347, 26, 2}, {422, 347, 0, 0}},
    {{422, 349, 26, 2}, {422, 349, 0, 0}, {422, 349, 26, 2}, {422, 349, 0, 0}}, {{422, 351, 26, 2}, {422, 351, 0, 0}, {422, 351, 26, 2}, {422, 351, 0, 0}},
    {{422, 353, 26, 2}, {422, 353, 0, 0}, {422, 353, 26, 2}, {422, 353, 0, 0}}, {{422, 355, 26, 2}, {422, 355, 0, 0}, {422, 355, 26, 2}, {422, 355, 0, 0}},
    {{422, 357, 26, 2}, {422, 357, 0, 0}, {422, 357, 26, 2}, {422, 357, 0, 0}},
    {{422, 336, 26, 9}, {422, 348, 26, 9}, {422, 336, 26, 9}, {422, 348, 26, 9}}};
const float kRibbonTickUv[27][4] = {
    {91, 0, 93, 21}, {91, 0, 93, 21}, {91, 0, 93, 21}, {91, 0, 93, 21}, {91, 0, 93, 21}, {91, 0, 93, 21}, {91, 0, 93, 21}, {91, 0, 93, 21}, {93, 0, 102, 21},
    {100, 21, 102, 42}, {100, 21, 102, 42}, {100, 21, 102, 42}, {100, 21, 102, 42}, {100, 21, 102, 42}, {100, 21, 102, 42}, {100, 21, 102, 42}, {100, 21, 102, 42}, {100, 21, 102, 42},
    {91, 21, 100, 42},
    {50, 59, 76, 61}, {50, 59, 76, 61}, {50, 59, 76, 61}, {50, 59, 76, 61}, {50, 59, 76, 61}, {50, 59, 76, 61}, {50, 59, 76, 61}, {50, 50, 76, 59}};
// 0x4909c0: class icon UVs in TEX_UI05 (32x32 cells) [V floats]
const float kClassIconUv[14][4] = {{64, 0, 96, 32}, {96, 0, 128, 32}, {0, 32, 32, 64}, {32, 32, 64, 64}, {64, 32, 96, 64}, {96, 32, 128, 64}, {0, 64, 32, 96},
                                   {32, 64, 64, 96}, {64, 64, 96, 96}, {96, 64, 128, 96}, {0, 96, 32, 128}, {32, 96, 64, 128}, {64, 96, 96, 128}, {96, 96, 128, 128}};

HudSprite makeTbl(const float (&p)[4], const float (&uv)[4], float z, int tex) { return makeSprite(p[0] - 1.0f, p[1] - 1.0f, z, p[2], p[3], uv[0], uv[1], uv[2], uv[3], tex); }

// FUN_00166820 on the UI layer: argument order of the original (x, y, z, w, h, u0, v0, u1, tex, flag, [stack] v1)
ui::Sprite* spr(float x, float y, float z, float w, float h, float u0, float v0, float u1, int tex, int flag, float v1) {
    return ui::sprites().create(x, y, z, w, h, u0, v0, u1, v1, tex, flag);
}
// the inline unlink + FUN_001053d0 idiom
void kill(ui::Sprite*& s) { ui::sprites().destroyRef(s); }
void colour(ui::Sprite* s, uint32_t c) { if (s != nullptr) s->color = c; }

// orig FUN_00166eb0 (work/decompiled/00166eb0.c) on the UI layer: loads TEX_UI05 / TEX_UI04 on demand
ui::Sprite* createButtonGlyph(float x, float y, float z, float w, float h, int button, int flag) {
    int tex;
    if (button == 0x20 || button == 0x40 || button == 0x80 || button == 0x10) {
        if (!ui::textures().isLoaded(0x28)) ui::textures().load(0x28);
        tex = 0x28;
    } else {
        if (button != 8 && button != 2 && button != 4 && button != 1) return nullptr;
        if (!ui::textures().isLoaded(0x27)) ui::textures().load(0x27);
        tex = 0x27;
    }
    ui::Sprite* s = ui::sprites().alloc(tex, flag);
    if (s == nullptr) return nullptr;
    s->cx = w / 2.0f + x;
    s->cy = h / 2.0f + y;
    s->z = z;
    s->hw = w / 2.0f;
    s->hh = h / 2.0f;
    s->color = 0x888888;
    const int row = buttonGlyphRow(button);
    s->u0 = kButtonGlyphUv[row][0] / 128.0f;
    s->v0 = kButtonGlyphUv[row][1] / 128.0f;
    s->u1 = kButtonGlyphUv[row][2] / 128.0f;
    s->v1 = kButtonGlyphUv[row][3] / 128.0f;
    return s;
}
}  // namespace

// 0x491ef0: button glyph UV rows (0..3 TEX_UI05 circle/cross/square/triangle, 4..7 TEX_UI04 L1/R1/L2/R2) [V floats]
const float kButtonGlyphUv[8][4] = {{0, 0, 16, 17}, {16, 0, 32, 17}, {32, 0, 48, 17}, {48, 0, 64, 17}, {94, 26, 112, 41}, {94, 11, 112, 26}, {94, 58, 112, 75}, {94, 41, 112, 58}};

// the uVar5 selection of FUN_00166eb0
int buttonGlyphRow(int button) {
    int row = button == 0x40 ? 1 : 0;
    if (button == 0x80) row = 2;
    if (button == 0x10) row = 3;
    if (button == 8) row = 5;
    if (button == 4) row = 4;
    if (button == 2) row = 7;
    if (button == 1) row = 6;
    return row;
}

// orig FUN_00166820 (work/decompiled/00166820.c) as a record
HudSprite makeSprite(float x, float y, float z, float w, float h, float u0, float v0, float u1, float v1, int tex, float texW, float texH, int flag) {
    HudSprite s;
    s.live = true;
    s.tex = tex;
    s.flag = flag;
    s.cx = w / 2.0f + x;
    s.cy = h / 2.0f + y;
    s.z = z;
    s.hw = w / 2.0f;
    s.hh = h / 2.0f;
    s.colour = 0x7f7f7f;
    s.u0 = u0 / texW; s.v0 = v0 / texH; s.u1 = u1 / texW; s.v1 = v1 / texH;
    return s;
}

// orig FUN_00166eb0 (work/decompiled/00166eb0.c) as a record
HudSprite makeButtonGlyph(float x, float y, float z, float w, float h, int button, int flag) {
    HudSprite s;
    int tex;
    if (button == 0x20 || button == 0x40 || button == 0x80 || button == 0x10) tex = kTexUi05;
    else if (button == 8 || button == 2 || button == 4 || button == 1) tex = kTexUi04;
    else return s;
    s.live = true;
    s.tex = tex;
    s.flag = flag;
    s.cx = w / 2.0f + x; s.cy = h / 2.0f + y; s.z = z; s.hw = w / 2.0f; s.hh = h / 2.0f;
    s.colour = 0x888888;
    const int row = buttonGlyphRow(button);
    s.u0 = kButtonGlyphUv[row][0] / 128.0f; s.v0 = kButtonGlyphUv[row][1] / 128.0f;
    s.u1 = kButtonGlyphUv[row][2] / 128.0f; s.v1 = kButtonGlyphUv[row][3] / 128.0f;
    return s;
}

// =============================================================================================================
// Status HUD 0x5179c0
// =============================================================================================================

// orig FUN_0014ec20 (work/decompiled/0014ec20.c)
void StatusHud::resetCounters() {
    depthEased_ = 0;
    depthTarget_ = 0;
    level_ = 0;
    levelCache_ = 0;
    hpMax_ = 0;
    hpTarget_ = 0;
    hpEased_ = 0;
    vtMax_ = 0;
    vtTarget_ = 0;
    vtEased_ = 0;
    depthTarget_ = 0;
    depthEased_ = 0;
    texBack_ = -1;
    texHp_ = -1;
    texVt_ = -1;
    barsDirty_ = 0;
}

// orig FUN_0014ee70 (work/decompiled/0014ee70.c): FUN_00106350(0x4e6a68, w, h, 0, 1) x3
void StatusHud::createTextures() {
    texBack_ = ui::textures().createDynamic(4, 4);
    texHp_ = ui::textures().createDynamic(0x40, 4);
    texVt_ = ui::textures().createDynamic(0x40, 4);
}

// orig FUN_0014eef0 (work/decompiled/0014eef0.c): FUN_00105f00 x3
void StatusHud::releaseTextures() {
    ui::textures().release(texBack_);
    ui::textures().release(texHp_);
    ui::textures().release(texVt_);
}

// orig FUN_0014ecc0 (work/decompiled/0014ecc0.c)
void StatusHud::init() {
    if (player != nullptr) {
        level_ = player->level();             // status +0x48
        levelCache_ = player->level();
        hpTarget_ = player->hp();             // +0x4
        vtTarget_ = player->vt();             // +0xc
        hpEased_ = player->hp();
        vtEased_ = player->vt();
        hpMax_ = player->hpMaxBase();         // +0x0
        vtMax_ = player->vtMaxBase();         // +0x8
    }
    built_ = 0;
    // every sprite pointer is cleared (they were freed by FUN_0014f8c0 before the HUD can be rebuilt)
    talkMode_ = 0;
    hpBack_ = hpFill_ = vtBack_ = vtFill_ = nullptr;
    for (auto& s : hpDigits_) s = nullptr;
    for (auto& s : vtDigits_) s = nullptr;
    hpLabel_ = vtLabel_ = nullptr;
    for (auto& s : depthDigits_) s = nullptr;
    cubitLabel_ = nullptr;
    levelLabel_ = nullptr;
    levelDigits_[0] = levelDigits_[1] = nullptr;
    levelIcon_ = nullptr;
    iconIndex_ = -1;
    iconCounter_ = 0;
    for (auto& s : frame_) s = nullptr;
    for (auto& s : slide_) s = nullptr;
    for (auto& s : itemFrame_) s = nullptr;
    for (auto& s : talkStrip_) s = nullptr;
    talkButton_ = talkExtra_ = nullptr;
    hpBarState_ = 0;
    vtBarState_ = 0;
    fade_ = 0;
}

// orig FUN_00151960 (work/decompiled/00151960.c) + the FUN_00106650 upload every caller does when it returns 1
void StatusHud::uploadBar(int tex, uint32_t* px, int value, int max, int& state) {
    const float fm = static_cast<float>(max), fv = static_cast<float>(value);
    uint32_t rgb = 0;
    if (fm * 0.8f <= fv || value == 0) {
        if (state != 4) { state = 4; rgb = 0x44ff44; }
    } else if (fv < fm * 0.3f) {
        if (state != 1) { state = 1; rgb = 0x3333ff; }
    } else if (fv < fm * 0.5f) {
        if (state != 2) { state = 2; rgb = 0x3355ff; }
    } else if (fv < fm * 0.8f && state != 3) {
        state = 3; rgb = 0x55ffff;
    }
    if (rgb == 0) return;
    if (px != nullptr) {
        for (uint32_t row = 0; row < 4; ++row) {
            uint32_t a = 0x40;
            for (uint32_t x = 0; x < 0x40; ++x) {
                px[row * 0x40 + x] = (row == 0 || row == 3) ? rgb + 0x10000000u : rgb + a * 0x1000000u;
                a += 3;
            }
        }
    }
    ui::textures().dynamicUpload(tex);   // FUN_00106650(0x4e6a68, tex)
}

// orig FUN_0014ef40 (work/decompiled/0014ef40.c); stack arguments (v1, texW, texH) read from the disassembly
void StatusHud::build() {
    if (built_ != 0) return;
    init();
    built_ = 1;
    hpBarState_ = 0;
    vtBarState_ = 0;
    if (uint32_t* back = ui::textures().dynamicPixels(texBack_)) {
        for (uint32_t r = 0; r < 4; ++r)
            for (uint32_t c = 0; c < 4; ++c) back[r * 4 + c] = (r == 0 || r == 3) ? 0x7f222222u : 0x7f444444u;
        // UNVERIFIED: FUN_0014ef40 writes this buffer without calling FUN_00106650; the port uploads it here so the
        // GPU copy holds the written pixels.
        ui::textures().dynamicUpload(texBack_);
    }
    pxHp_ = ui::textures().dynamicPixels(texHp_);   // +0x150
    pxVt_ = ui::textures().dynamicPixels(texVt_);   // +0x154
    hpBack_ = ui::sprites().alloc(texBack_, 1);      // +0x1c
    hpFill_ = ui::sprites().alloc(texHp_, 2);        // +0x18
    uploadBar(texHp_, pxHp_, hpEased_, hpMax_, hpBarState_);
    vtBack_ = ui::sprites().alloc(texBack_, 1);      // +0x24
    vtFill_ = ui::sprites().alloc(texVt_, 2);        // +0x20
    uploadBar(texVt_, pxVt_, vtEased_, vtMax_, vtBarState_);
    layoutBars(false);
    for (int i = 0; i < 7; ++i) {
        const float* p = kHpDigitPos[i];
        hpDigits_[i] = spr(p[0] - 1.0f, p[1] - 1.0f, -1.0f, p[2], p[3], 0, 0, 0, 0x23, 1, 0);
        colour(hpDigits_[i], kOpaque);
        vtDigits_[i] = spr(p[0] - 1.0f, (p[1] + 23.0f) - 1.0f, -1.0f, p[2], p[3], 0, 0, 0, 0x23, 1, 0);
        colour(vtDigits_[i], kOpaque);
    }
    hpLabel_ = spr(76.0f, 24.0f, -1.0f, 19.0f, 13.0f, 0.0f, 11.0f, 18.0f, 0x23, 1, 24.0f);
    colour(hpLabel_, kOpaque);
    vtLabel_ = spr(76.0f, 47.0f, -1.0f, 18.0f, 13.0f, 18.0f, 11.0f, 35.0f, 0x23, 1, 24.0f);
    colour(vtLabel_, kOpaque);
    for (int i = 0; i < 8; ++i) {
        const float* p = kDepthDigitPos[i];
        depthDigits_[i] = spr(p[0] - 1.0f, p[1] - 1.0f, -1.0f, p[2], p[3], 0, 0, 0, 0x23, 1, 0);
        colour(depthDigits_[i], kOpaque);
    }
    cubitLabel_ = spr(109.0f, 73.0f, -1.0f, 24.0f, 10.0f, 0.0f, 48.0f, 24.0f, 0x23, 1, 58.0f);
    float z = -1.0f;
    colour(cubitLabel_, kOpaque);
    for (int i = 0; i < 13; ++i) {
        const float* p = kFramePos[i];
        frame_[i] = spr(p[0] - 1.0f, p[1] - 1.0f, z, p[2], p[3], kFrameUv[i][0], kFrameUv[i][1], kFrameUv[i][2], 0x24, 1, kFrameUv[i][3]);
        colour(frame_[i], kOpaque);
        z = z + 0.01f;
    }
    for (int i = 0; i < 6; ++i) {
        const float* p = kSlidePos[i][0];
        slide_[i] = spr(p[0] - 1.0f, p[1] - 1.0f, z, p[2], p[3], kSlideUv[i][0], kSlideUv[i][1], kSlideUv[i][2], 0x24, 1, kSlideUv[i][3]);
        colour(slide_[i], kOpaque);
        z = z + 0.01f;
    }
    for (int i = 0; i < 7; ++i) {
        const float* p = kItemFramePos[i];
        ui::AnchorScope rightFrame(kAnchorRight);   // PORT widescreen: the item-slot frame sits at the bottom-right edge
        itemFrame_[i] = spr(p[0] - 1.0f, p[1] - 1.0f, 1.0f, p[2], p[3], kItemFrameUv[i][0], kItemFrameUv[i][1], kItemFrameUv[i][2], 0x24, 1, kItemFrameUv[i][3]);
        colour(itemFrame_[i], kOpaque);
    }
    levelLabel_ = spr(21.0f, 14.0f, z, 15.0f, 13.0f, 36.0f, 12.0f, 51.0f, 0x23, 0, 25.0f);
    colour(levelLabel_, kOpaque);
    for (int i = 0; i < 2; ++i) {
        z = z + 0.01f;
        levelDigits_[i] = spr(10.0f, 16.0f, z, 11.0f, 23.0f, 0, 0, 0, 0x23, 1, 0);
        colour(levelDigits_[i], kOpaque);
    }
    levelDigitAlpha_ = 0x40;
    talkMode_ = 0;
    buildTalkStrip();
}

// orig FUN_0014f8c0 (work/decompiled/0014f8c0.c)
void StatusHud::release() {
    if (built_ == 0) return;
    kill(hpBack_);
    kill(hpFill_);
    kill(vtBack_);
    kill(vtFill_);
    for (int i = 0; i < 7; ++i) { kill(hpDigits_[i]); kill(vtDigits_[i]); }
    for (auto& s : depthDigits_) kill(s);
    kill(hpLabel_);
    kill(vtLabel_);
    kill(cubitLabel_);
    for (auto& s : frame_) kill(s);
    for (auto& s : slide_) kill(s);
    for (auto& s : itemFrame_) kill(s);
    kill(levelLabel_);
    kill(levelDigits_[0]);
    kill(levelDigits_[1]);
    kill(levelIcon_);
    releaseTalkStrip();
    built_ = 0;
}

// orig FUN_001508f0 (work/decompiled/001508f0.c)
void StatusHud::releaseTalkStrip() {
    for (auto& s : talkStrip_) kill(s);
    if (talkMode_ != 0) {
        kill(talkButton_);
        kill(talkExtra_);
    }
}

// orig FUN_00150680 (work/decompiled/00150680.c): mode 0 uses the second (all-zero) rect of each pair
void StatusHud::buildTalkStrip() {
    fade_ = 0;
    for (int i = 0; i < 13; ++i) {
        static const float kZero[4] = {0, 0, 0, 0};
        const float* p = talkMode_ == 0 ? kZero : kTalkStripPos[i];
        talkStrip_[i] = spr(p[0] - 1.0f, p[1] - 1.0f, static_cast<float>(i) * 0.01f + -1.0f, p[2], p[3],
                            kTalkStripUv[i][0], kTalkStripUv[i][1], kTalkStripUv[i][2], 0x25, 1, kTalkStripUv[i][3]);
        colour(talkStrip_[i], kOpaque);
    }
    if (talkMode_ != 0) {
        talkButton_ = createButtonGlyph(34.0f, 115.0f, -1.0f, 16.0f, 17.0f, buttons.talk, 1);
        colour(talkButton_, kOpaque);
    }
    if (talkMode_ == 1) {
        talkExtra_ = spr(53.0f, 117.0f, -1.0f, 57.0f, 13.0f, 33.0f, 0.0f, 93.0f, 0x25, 1, 13.0f);
        colour(talkExtra_, kOpaque);
    } else if (talkMode_ == 2) {
        talkExtra_ = spr(53.0f, 117.0f, -1.0f, 57.0f, 13.0f, 0.0f, 0.0f, 57.0f, 0x29, 1, 13.0f);   // texW/texH 64 (TEX_UI11)
        colour(talkExtra_, kOpaque);
    }
}

// orig FUN_00150610 (work/decompiled/00150610.c)
void StatusHud::setTalkMode(int mode) {
    releaseTalkStrip();
    talkMode_ = mode;
    buildTalkStrip();
}

// orig FUN_00150650 (work/decompiled/00150650.c)
void StatusHud::clearTalkMode() {
    releaseTalkStrip();
    talkMode_ = 0;
    buildTalkStrip();
}

// orig FUN_00151660 (work/decompiled/00151660.c): width = ((1-t)t + t + 0) * 275 with t = value / 999
void StatusHud::layoutBars(bool backOnly) {
    if (player != nullptr) {
        hpMax_ = player->hpMaxTotal();   // FUN_001343f0
        vtMax_ = player->vtMaxTotal();   // FUN_00134470
    }
    auto width = [](int v) {
        const float t = static_cast<float>(v) / 999.0f;
        return ((1.0f - t) * t + t + 0.0f) * 275.0f;
    };
    auto place = [](ui::Sprite* s, float w, float y, float z) {
        if (s == nullptr) return;
        s->cx = w * 0.5f + 100.0f;
        s->cy = y;
        s->z = z;
        s->hw = w * 0.5f;
        s->hh = 2.0f;
        s->color = kOpaque;
        s->u0 = 0; s->v0 = 0; s->u1 = 1.0f; s->v1 = 1.0f;
    };
    const float hpBackW = width(hpMax_), hpFillW = width(hpEased_);
    place(hpBack_, hpBackW, 41.0f, -0.9f);    // 0x42240000, 0xbf666666
    if (!backOnly) place(hpFill_, hpFillW, 41.0f, -0.8f);   // 0xbf4ccccd
    const float vtBackW = width(vtMax_), vtFillW = width(vtEased_);
    place(vtBack_, vtBackW, 63.0f, -0.9f);    // 0x427c0000
    if (!backOnly) place(vtFill_, vtFillW, 63.0f, -0.8f);
}

// orig FUN_00151440 (work/decompiled/00151440.c): HP / VT eased 3 per tick (clamped), depth 3 per tick (not clamped)
void StatusHud::ease() {
    bool dirty = false;
    if (hpTarget_ != hpEased_) {
        const int e = hpEased_;
        if (e < hpTarget_) {
            hpEased_ = static_cast<int>(static_cast<float>(e) + 3.0f);
            if (hpTarget_ < static_cast<int>(static_cast<float>(e) + 3.0f)) hpEased_ = hpTarget_;
        } else {
            hpEased_ = static_cast<int>(static_cast<float>(e) - 3.0f);
            if (static_cast<int>(static_cast<float>(e) - 3.0f) < hpTarget_) hpEased_ = hpTarget_;
        }
        uploadBar(texHp_, pxHp_, hpEased_, hpMax_, hpBarState_);
        dirty = true;
    }
    if (vtTarget_ != vtEased_) {
        const int e = vtEased_;
        if (e < vtTarget_) {
            vtEased_ = static_cast<int>(static_cast<float>(e) + 3.0f);
            if (vtTarget_ < static_cast<int>(static_cast<float>(e) + 3.0f)) vtEased_ = vtTarget_;
        } else {
            vtEased_ = static_cast<int>(static_cast<float>(e) - 3.0f);
            if (static_cast<int>(static_cast<float>(e) - 3.0f) < vtTarget_) vtEased_ = vtTarget_;
        }
        uploadBar(texVt_, pxVt_, vtEased_, vtMax_, vtBarState_);
        dirty = true;
    }
    if (depthTarget_ != depthEased_) {
        const float step = depthTarget_ < depthEased_ ? -3.0f : 3.0f;
        dirty = true;
        depthEased_ = static_cast<int>(static_cast<float>(depthEased_) + step);
    }
    if (dirty) layoutBars(false);
}

// orig FUN_00151be0 (work/decompiled/00151be0.c): formats 0x44be38 "%3d/%3d", 0x44be40 "%8d", 0x44be48 "%2d" [V]
void StatusHud::updateDigits() {
    if (hpDigits_[0] != nullptr) {
        char hp[16], vt[16];
        std::snprintf(hp, sizeof hp, "%3d/%3d", hpEased_, hpMax_);
        std::snprintf(vt, sizeof vt, "%3d/%3d", vtEased_, vtMax_);
        for (int i = 0; i < 7; ++i) {
            if (hpDigits_[i] != nullptr) setDigitGlyph(*hpDigits_[i], hp[i]);
            if (vtDigits_[i] != nullptr) setDigitGlyph(*vtDigits_[i], vt[i]);
        }
    }
    if (depthDigits_[0] != nullptr) {
        char d[24];
        std::snprintf(d, sizeof d, "%8d", -depthEased_);
        for (int i = 0; i < 8; ++i)
            if (depthDigits_[i] != nullptr) setDigitGlyph(*depthDigits_[i], d[i]);
    }
    float x = 31.0f;
    if (levelDigits_[0] != nullptr && player != nullptr) {
        level_ = player->level();   // status +0x48
        char l[16];
        std::snprintf(l, sizeof l, "%2d", level_);
        if (level_ < 10) x = x - 6.0f;
        for (int i = 0; i < 2; ++i) {
            ui::Sprite* s = levelDigits_[i];
            if (s == nullptr) continue;
            setLevelDigitGlyph(*s, l[i]);
            s->cx = x + static_cast<float>(i * 12) + 5.5f;
            s->cy = 47.5f;   // 0x423e0000
        }
    }
}

// orig FUN_00151e00 (work/decompiled/00151e00.c): level-change frame; TEX_UI34 frames 0..15 (4 per row, 32 px
// cells), TEX_UI35 frames 16..19 (2 per row); sprite (28, 31) 32x32, z 0, flag 0
void StatusHud::setLevelFrame(int frame) {
    if (iconIndex_ == frame) return;
    kill(levelIcon_);
    iconIndex_ = frame;
    if (frame == -1) return;
    if (frame < 0x10) {
        const int col = frame & 3, row = frame >> 2;
        levelIcon_ = spr(28.0f, 31.0f, 0.0f, 32.0f, 32.0f, static_cast<float>(col << 5), static_cast<float>(row << 5),
                         static_cast<float>(col << 5) + 32.0f, 0x2a, 0, static_cast<float>(row << 5) + 32.0f);
        colour(levelIcon_, kOpaque);
    } else if (frame < 0x14) {
        const int f = frame - 0x10, col = f & 1, row = f >> 1;
        levelIcon_ = spr(28.0f, 31.0f, 0.0f, 32.0f, 32.0f, static_cast<float>(col << 5), static_cast<float>(row << 5),
                         static_cast<float>(col << 5) + 32.0f, 0x2b, 0, static_cast<float>(row << 5) + 32.0f);
        colour(levelIcon_, kOpaque);
    }
}

// orig FUN_00151af0 (work/decompiled/00151af0.c)
void StatusHud::refreshBars(uint32_t which) {
    if (which == 0 || (which & 1) != 0) hpEased_ = hpTarget_;
    if (which == 0 || (which & 2) != 0) vtEased_ = vtTarget_;
    if (which == 0 || (which & 4) != 0) depthEased_ = depthTarget_;
    if (built_ == 0) return;
    layoutBars(false);
    uploadBar(texHp_, pxHp_, hpEased_, hpMax_, hpBarState_);
    uploadBar(texVt_, pxVt_, vtEased_, vtMax_, vtBarState_);
}

// orig FUN_00150ba0 (work/decompiled/00150ba0.c)
void StatusHud::tick(const HudVisibility& vis) {
    if (vis.sceneIsGame) {
        if (!vis.floorGenerated || vis.systemMenuOpen || vis.eventRunning || vis.fading || vis.dialogOpen) {
            if (built_ != 0) release();
        } else if (built_ == 0) {
            build();
        }
    } else if (built_ != 0) {
        release();
    }
    if (built_ == 0) return;
    if (fade_ < 0x40) {
        fade_ += 0x15;
        if (0x40 < fade_) fade_ = 0x40;
    }
    ease();
    updateDigits();
    if (!vis.talkActive || talkMode_ == 2) {
        if (!vis.talkActive && talkMode_ == 2) clearTalkMode();
    } else {
        setTalkMode(2);   // a1 = 2 at 0x150d2c [V disasm]
    }
    if (vis.floorGenerated) {
        iconCounter_ = static_cast<int8_t>(iconCounter_ + 1);
        if (iconIndex_ != -1 && 2 < iconCounter_) {
            iconCounter_ = 0;
            if (iconIndex_ + 1 < 0x14) setLevelFrame(iconIndex_ + 1);
            else setLevelFrame(-1);
        }
        if (iconIndex_ == -1 && levelCache_ != level_) {
            iconCounter_ = 0;
            setLevelFrame(0);
        }
        levelCache_ = level_;
        const int idx = iconIndex_;
        if (idx == -1 || 9 < idx) {
            if (idx == -1) levelDigitAlpha_ = 0x40;
            else if (0x10 < idx && levelDigitAlpha_ < 0x40) {
                levelDigitAlpha_ += 4;
                if (0x40 < levelDigitAlpha_) levelDigitAlpha_ = 0x40;
            }
        } else if (levelDigitAlpha_ != 0) {
            levelDigitAlpha_ -= 4;
        }
        colour(levelDigits_[0], levelDigitAlpha_ * 0x1000000u + 0x7f7f7f);
        colour(levelDigits_[1], levelDigitAlpha_ * 0x1000000u + 0x7f7f7f);
        if (0 <= iconIndex_ && iconIndex_ <= 9) {
            for (int i = 0; i < 6; ++i) {
                ui::Sprite* s = slide_[i];
                if (s == nullptr) continue;
                const float* n = kSlidePos[i][0];
                const float* m = kSlidePos[i][1];
                const float dw = (m[2] - n[2]) / 30.0f;
                const float dh = (m[3] - n[3]) / 30.0f;
                s->cx = s->cx + (m[0] - n[0]) / 30.0f + dw;
                s->cy = s->cy + (m[1] - n[1]) / 30.0f + dh;
                s->hw = s->hw + dw;
                s->hh = s->hh + dh;
            }
        } else if (9 < iconIndex_ && iconIndex_ < 0x14) {
            for (int i = 0; i < 6; ++i) {
                ui::Sprite* s = slide_[i];
                if (s == nullptr) continue;
                const float* n = kSlidePos[i][0];
                const float* m = kSlidePos[i][1];
                const float dw = (m[2] - n[2]) / 30.0f;
                const float dh = (m[3] - n[3]) / 30.0f;
                s->cx = s->cx - ((m[0] - n[0]) / 30.0f + dw);
                s->cy = s->cy - ((m[1] - n[1]) / 30.0f + dh);
                s->hw = s->hw - dw;
                s->hh = s->hh - dh;
            }
        }
    }
    if (!vis.floorGenerated || barsDirty_ == 0) {
        if (!vis.floorGenerated) barsDirty_ = 1;
    } else {
        layoutBars(false);
        uploadBar(texHp_, pxHp_, hpEased_, hpMax_, hpBarState_);
        uploadBar(texVt_, pxVt_, vtEased_, vtMax_, vtBarState_);
        barsDirty_ = 0;
    }
    colour(hpFill_, kOpaque);
    colour(hpBack_, kOpaque);
    colour(vtFill_, kOpaque);
    colour(vtBack_, kOpaque);
    colour(hpLabel_, kOpaque);
    colour(vtLabel_, kOpaque);
    for (int i = 0; i < 7; ++i) colour(hpDigits_[i], kOpaque);
    for (int i = 0; i < 7; ++i) colour(vtDigits_[i], kOpaque);
    for (auto* s : depthDigits_) colour(s, kOpaque);
    colour(cubitLabel_, kOpaque);
    for (auto* s : frame_) colour(s, kOpaque);
    for (auto* s : slide_) colour(s, kOpaque);
    for (auto* s : itemFrame_) colour(s, kOpaque);
    colour(levelLabel_, kOpaque);
    if (iconIndex_ == -1) {
        colour(levelDigits_[0], kOpaque);
        colour(levelDigits_[1], kOpaque);
    }
    if (levelIcon_ != nullptr) levelIcon_->color = kOpaque;
    const uint32_t fadeColour = fade_ * 0x1000000u + 0x7f7f7f;
    for (auto* s : talkStrip_) colour(s, fadeColour);
    if (talkButton_ != nullptr) talkButton_->color = fadeColour;
    if (talkExtra_ != nullptr) talkExtra_->color = fadeColour;
}

int StatusHud::spriteCount() const {
    int n = 0;
    auto c = [&](const ui::Sprite* s) { if (s != nullptr) ++n; };
    c(hpFill_); c(hpBack_); c(vtFill_); c(vtBack_); c(hpLabel_); c(vtLabel_); c(cubitLabel_); c(levelLabel_); c(levelIcon_);
    c(talkButton_); c(talkExtra_);
    for (auto* s : hpDigits_) c(s);
    for (auto* s : vtDigits_) c(s);
    for (auto* s : depthDigits_) c(s);
    for (auto* s : frame_) c(s);
    for (auto* s : slide_) c(s);
    for (auto* s : itemFrame_) c(s);
    for (auto* s : levelDigits_) c(s);
    for (auto* s : talkStrip_) c(s);
    return n;
}

// =============================================================================================================
// Item ribbon (part of 0x517b60)
// =============================================================================================================

// orig FUN_00152f40 (work/decompiled/00152f40.c)
void ItemRibbon::build(ItemSystem& sys) {
    if (visible_ && hideCountdown_ == -1) return;
    hideCountdown_ = -1;
    visible_ = true;
    int variant = 0;
    if (hintVariant_ == 1) variant = 1;
    else if (hintVariant_ == 3) variant = 2;
    else if (hintVariant_ == 4) variant = 3;
    float z = -0.5f;
    if (!frame_.live) {
        frame_ = makeSprite(kRibbonFrame[0] - 1.0f, kRibbonFrame[1] - 1.0f, -0.5f, kRibbonFrame[2], kRibbonFrame[3], kRibbonFrame[4], kRibbonFrame[5], kRibbonFrame[6], kRibbonFrame[7], kTexUi02);
        frame_.colour = kOpaque;
    }
    for (int i = 0; i < 13; ++i) {
        if (!parts_[i].live) { parts_[i] = makeTbl(kRibbonPartPos[i], kRibbonPartUv[i], z, kTexUi02); parts_[i].colour = kOpaque; }
        z += 0.01f;
    }
    for (int i = 0; i < 27; ++i) {
        if (!ticks_[i].live) { ticks_[i] = makeTbl(kRibbonTick[i][variant], kRibbonTickUv[i], z, kTexUi02); ticks_[i].colour = kOpaque; }
        z += 0.01f;
    }
    if (!icon_.live) { icon_ = makeSprite(413.0f, 367.0f, z, 32.0f, 32.0f, 0, 0, 0, 0, kTexUi05); icon_.colour = kOpaque; }
    if (hintVariant_ == 2 || hintVariant_ == 4) {
        if (!hintPrev_.live) {
            const float h = (buttons.slotPrev == 2 || buttons.slotPrev == 1) ? 17.0f : 15.0f;
            hintPrev_ = makeButtonGlyph(383.0f, 383.0f, z, 18.0f, h, buttons.slotPrev);
            hintPrev_.colour = kOpaque;
        }
        if (!hintNext_.live) {
            const float h = (buttons.slotNext == 2 || buttons.slotNext == 1) ? 17.0f : 15.0f;
            hintNext_ = makeButtonGlyph(458.0f, 383.0f, z, 18.0f, h, buttons.slotNext);
            hintNext_.colour = kOpaque;
        }
    }
    if ((hintVariant_ == 2 || hintVariant_ == 3) && !hintScreen_.live) {
        const float h = (buttons.itemScreen == 2 || buttons.itemScreen == 1) ? 17.0f : 15.0f;
        hintScreen_ = makeButtonGlyph(425.0f, 341.0f, z, 18.0f, h, buttons.itemScreen);
        hintScreen_.colour = kOpaque;
    }
    for (int i = 0; i < 2; ++i)
        if (!countDigits_[i].live) countDigits_[i] = makeSprite(static_cast<float>(i * 7) + 436.0f, 392.0f, z + 0.1f, 7.0f, 10.0f, 0, 0, 0, 0, kTexUi00);
    (void)sys;
}

// orig FUN_001567e0 (work/decompiled/001567e0.c)
void ItemRibbon::release() {
    if (!visible_) return;
    visible_ = false;
    frame_.live = false;
    for (auto& s : parts_) s.live = false;
    for (auto& s : ticks_) s.live = false;
    icon_.live = false;
    hintPrev_.live = hintNext_.live = hintScreen_.live = false;
    for (auto& s : countDigits_) s.live = false;
    name_.on = false;
}

// orig FUN_00153520 (work/decompiled/00153520.c)
void ItemRibbon::requestHide() {
    if (hideCountdown_ == -1) hideCountdown_ = 8;
}

// orig FUN_00152dc0 (2) / FUN_00152e20 (1) / FUN_00152e80 (3) / FUN_00152ee0 (4): release when shown, set the
// variant, rebuild (FUN_00152f40 also makes the ribbon visible when it was hidden)
void ItemRibbon::setHintVariant(ItemSystem& sys, int v) {
    if (visible_) release();
    hintVariant_ = v;
    build(sys);
}

// orig FUN_00153540 (work/decompiled/00153540.c); FUN_0015bfe0 clamps the shared cursor first
void ItemRibbon::updateHint(ItemSystem& sys, bool screenOpen) {
    Inventory& v = *sys.inventory;
    bool canThrow = false;
    if (-1 < cursor_ && v.equippedSlotOfIndex(cursor_) == -1)
        canThrow = MenuItemAction{}.canThrow(sys, cursor_);   // FUN_001d8cc0
    const int n = v.count;
    const int h = hintVariant_;
    if (!screenOpen || h == 1 || h == 0) {
        if (n == 0 && h != 1 && h != 0) setHintVariant(sys, 1);
        else if (!screenOpen && canThrow && n == 1 && h != 3) setHintVariant(sys, 3);
        else if (!screenOpen && canThrow && 1 < n && h != 2) setHintVariant(sys, 2);
        else if (screenOpen || canThrow || n < 2 || h == 4) {
            if (!screenOpen && !canThrow && n == 1 && h != 1) setHintVariant(sys, 1);
        } else {
            setHintVariant(sys, 4);
        }
    } else {
        setHintVariant(sys, 1);
    }
    lastCount_ = n;
}

// orig FUN_0015b2d0 (work/decompiled/0015b2d0.c)
void ItemRibbon::layout(ItemSystem& sys, bool menuOpen, Sprite2D& out) {
    if (hideCountdown_ == -1) {
        if (slide_ <= 128.0f) { slide_ += 16.0f; if (slide_ > 128.0f) slide_ = 128.0f; }
    } else if (slide_ > 0.0f) {
        if (!menuOpen) slide_ -= 16.0f; else slide_ = 0.0f;
    }
    frame_.cx = kRibbonFrame[2] / 2.0f + ((kRibbonFrame[0] + 128.0f) - slide_);
    for (int i = 0; i < 13; ++i) parts_[i].cy = ((kRibbonPartPos[i][1] + 128.0f) - slide_) + kRibbonPartPos[i][3] / 2.0f;
    int variant = 0;
    if (hintVariant_ == 1) variant = 1; else if (hintVariant_ == 3) variant = 2; else if (hintVariant_ == 4) variant = 3;
    for (int i = 0; i < 27; ++i) ticks_[i].cy = ticks_[i].hh + ((kRibbonTick[i][variant][1] + 128.0f) - slide_);
    if (hintPrev_.live) hintPrev_.cy = (512.0f - slide_) + 7.5f;
    if (hintNext_.live) hintNext_.cy = (512.0f - slide_) + 7.5f;
    if (hintScreen_.live) hintScreen_.cy = (470.0f - slide_) + 7.5f;
    name_.on = false;
    Inventory& v = *sys.inventory;
    if (cursor_ < 0 || cursor_ >= v.count) {
        icon_.colour = 0;
        countDigits_[0].colour = countDigits_[1].colour = 0x7f7f7f;
        return;
    }
    const Item* it = v.slots[cursor_];
    const int cls = it->cls < 14 ? it->cls : 0;
    icon_.colour = kOpaque;
    icon_.cy = (496.0f - slide_) + 16.0f;
    icon_.u0 = kClassIconUv[cls][0] / 128.0f; icon_.v0 = kClassIconUv[cls][1] / 128.0f;
    icon_.u1 = kClassIconUv[cls][2] / 128.0f; icon_.v1 = kClassIconUv[cls][3] / 128.0f;
    if (slide_ == 128.0f && (it->cls == kClsGN || (it->cls == kClsBN && it->identified()))) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%2d", it->count);   // 0x44bf70
        for (int i = 0; i < 2; ++i) {
            setDigitGlyph(countDigits_[i], buf[i]);
            countDigits_[i].colour = kOpaque;
            countDigits_[i].cy = (520.0f - slide_) + 3.0f;
        }
    } else {
        countDigits_[0].colour = countDigits_[1].colour = 0x7f7f7f;
    }
    if (slide_ != 128.0f) return;
    // item name over the name bar: 16x18 glyphs, colour DAT_0044c198, centred in the 130 px bar from x 365
    // (FUN_00112e80 globals: half 8x9, colour DAT_0044c198, layer flag 1, z 1.0); the width is measured on the name
    // (plus the rotten suffix 0x44bf78 / 0x44bf80), the text is formatted with str 0x248 or 0x44bf38 / 0x44bf40
    name_.on = true;
    name_.y = 537.0f - slide_;
    float w = 0.0f;
    if (!it->identified()) {
        const char* nm = itemDisplayName(*it, sys);
        const MsgArg a(nm);
        w = out.measureText(nm, 8.0f, true);
        formatMsg(name_.buf, sizeof name_.buf, sys.game->str(0x248), &a, 1);
    } else if (!(it->flags & kItemRotten)) {
        const char* nm = isParasiteHostWithParasite(*it) ? composedParasiteName(*it, sys) : sys.name(it->cls, it->idx);
        const MsgArg a(nm);
        w = out.measureText(nm, 8.0f, true);
        formatMsg(name_.buf, sizeof name_.buf, sys.game->str(0x248), &a, 1);
    } else if (sys.game->language() == 1) {
        const char* nm = sys.name(it->cls, it->idx);
        w = out.measureText(nm, 8.0f, true) + out.measureText("\x81\x45\x95\x85", 8.0f, true);    // 0x44bf78
        std::snprintf(name_.buf, sizeof name_.buf, "%s\x81\x45\x95\x85\n", nm);                     // 0x44bf38
    } else {
        const char* nm = sys.name(it->cls, it->idx);
        w = out.measureText(nm, 8.0f, true) + out.measureText("\x81\x45rotten", 8.0f, true);       // 0x44bf80
        std::snprintf(name_.buf, sizeof name_.buf, "%s\x81\x45rotten\n", nm);                       // 0x44bf40
    }
    name_.x = (130.0f - w) / 2.0f + 365.0f;
    name_.text = name_.buf;
}

// ribbon half of orig FUN_00156de0 (work/decompiled/00156de0.c): +0x18 countdown, build / hide / hint update,
// FUN_00157310 -> FUN_0015b2d0 layout, the +0x28 cursor bookkeeping at the end
void ItemRibbon::tick(ItemSystem& sys, const HudVisibility& vis, int screenPage, bool screenOpen, Sprite2D& out) {
    Inventory& v = *sys.inventory;
    if (-1 < hideCountdown_ && --hideCountdown_ == 0) {
        release();
        hideCountdown_ = -1;
    }
    const bool blocked = !vis.floorGenerated || vis.systemMenuOpen || vis.eventRunning || vis.talkSystemBusy || vis.fading;
    if (vis.sceneIsGame && !blocked) {
        if (!visible_ && screenPage == 0 && 0 < v.count) {
            build(sys);
            slide_ = 0.0f;
        } else if (visible_ && (screenPage == 1 || v.count == 0)) {
            requestHide();
            hintVariant_ = 0;
        } else if (screenPage != 1) {
            // FUN_0015bfe0 (pages -1 / 0 only) then FUN_00153540
            if (static_cast<unsigned>(screenPage + 1) < 2u && v.count <= cursor_) cursor_ = v.count - 1;
            updateHint(sys, screenOpen);
        }
    } else if (visible_) {
        requestHide();
        hintVariant_ = 0;
    }
    if (screenOpen || visible_ || vis.systemMenuOpen) {
        if (visible_) layout(sys, vis.systemMenuOpen, out);   // FUN_00157310 -> FUN_0015b2d0
    }
    // +0x28 bookkeeping (FUN_00156de0 tail; the original returns earlier when there is no player)
    if (cursor_ == -1 || v.count != 0) {
        if (cursor_ == -1 && !screenOpen && v.count != 0) cursor_ = 0;
        else if (v.count <= cursor_) cursor_ = v.count - 1;
    } else {
        cursor_ = -1;
    }
}

// orig FUN_00157410 / FUN_001575f0 closed-screen paths (work/decompiled/00157410.c, 001575f0.c)
void ItemRibbon::stepPrev(ItemSystem& sys) {
    const int n = sys.inventory->count;
    if (n < 2) return;
    cursor_--;
    if (cursor_ < 0) cursor_ = n - 1;
    sys.game->se(6);
}
void ItemRibbon::stepNext(ItemSystem& sys) {
    const int n = sys.inventory->count;
    if (n < 2) return;
    cursor_++;
    if (cursor_ >= n) cursor_ = 0;
    sys.game->se(6);
}

void ItemRibbon::draw(Sprite2D& out) const {
    if (!visible_) return;
    frame_.draw(out);
    for (const auto& s : parts_) s.draw(out);
    for (const auto& s : ticks_) s.draw(out);
    icon_.draw(out);
    hintPrev_.draw(out); hintNext_.draw(out); hintScreen_.draw(out);
    for (const auto& s : countDigits_) s.draw(out);
    if (name_.on && name_.text) out.drawText(name_.text, name_.x, name_.y, 1.0f, 8.0f, 9.0f, 0x40bbbbbb, true);
}

}  // namespace baroque::game
