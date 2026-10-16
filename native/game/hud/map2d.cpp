#include "game/hud/map2d.hpp"
#include "game/ui/sprite.hpp"
#include <cmath>

namespace baroque::game {

namespace {
int pow2AtLeast(int v) {   // FUN_00126120 ladder: <9 -> 8, <0x11 -> 16, ... <0x201 -> 512, else the value itself
    if (v < 9) return 8;
    if (v < 0x11) return 0x10;
    if (v < 0x21) return 0x20;
    if (v < 0x41) return 0x40;
    if (v < 0x81) return 0x80;
    if (v < 0x101) return 0x100;
    if (v < 0x201) return 0x200;
    return v;
}
// 0x44a5e0 player triangle / 0x44a570 target quad (polygon layer 0x4e6990, [V floats])
const PolyVert kMarkerTri[4] = {{0, 6, 0}, {0, 6, 0}, {-3, -6, 0}, {3, -6, 0}};
const PolyVert kTargetQuad[4] = {{-4, -4, 0}, {4, -4, 0}, {-4, 4, 0}, {4, 4, 0}};
// PORT: the same markers at the corner map's half scale
const PolyVert kMarkerTriSmall[4] = {{0, 3.5f, 0}, {0, 3.5f, 0}, {-2, -3.5f, 0}, {2, -3.5f, 0}};
const PolyVert kTargetQuadSmall[4] = {{-2.5f, -2.5f, 0}, {2.5f, -2.5f, 0}, {-2.5f, 2.5f, 0}, {2.5f, 2.5f, 0}};
// 0x44a5a0 / 0x44a5c0 corridor reveal offsets [V words]
const int kDx[5] = {0, -1, 0, 1, 0};
const int kDy[5] = {0, 0, -1, 0, 1};
// PORT: corner mini map box in the 512x448 HUD raster (left, bottom; 2 px per cell, at most 64 x 52 cells)
constexpr int kCornerLeft = 20, kCornerBottom = 432, kCornerCellsW = 64, kCornerCellsH = 52, kCornerPx = 2;
}  // namespace

// orig FUN_00126120 (work/decompiled/00126120.c)
void Map2D::createTexture(Map2DSource& src, Sprite2D& out) {
    if (texture_ != -1) return;
    texW_ = pow2AtLeast(src.gridW());
    texH_ = pow2AtLeast(src.gridH());
    texture_ = out.createDynamicTexture(texW_, texH_);   // FUN_00106350(0x4e6a68, w, h, 0, 1)
    pixels_.assign(static_cast<size_t>(texW_) * static_cast<size_t>(texH_), 0u);   // FUN_00106630 buffer cleared
    scale_ = 2.0f;   // 0x40000000
    cx_ = 0x100;
    cy_ = 0xe0;
}

// orig FUN_00126cc0 (work/decompiled/00126cc0.c). The caller sets flag 99 (FUN_00178890(0x53c3b0, 99)) when this
// returns true, before the sprite exists in the original; nothing reads the flag in between.
bool Map2D::show(Map2DSource& src, int floorId) {
    if (shown_ || floorId == 0x5c) return false;
    // PORT: the original indexes the texture table with +0xb4 = -1 before FUN_00126120 ran and then writes through
    // the null sprite FUN_00107130 returns; the port declines instead.
    if (texture_ == -1) return false;
    gridW_ = src.gridW();
    gridH_ = src.gridH();
    shown_ = true;          // +0xa8 = sprite (texture bucket, layer flag 0), centre (+0xbc, +0xc0), z -1.0
    alpha_ = 0x40;          // +0x28 = 0x40ffffff
    // +0xac marker: FUN_00114c30 node of 0x4e6990, FUN_001051d0(node, 0x44a5e0, 0, 0, 4, 0x50a6e0), colour
    // 0xff0000ff, flags |= 1
    marker_ = polygons().create();
    polygonSetVerts(*marker_, kMarkerTri, nullptr, 0, 4, markerVerts_);
    marker_->colour = 0xff0000ffu;
    marker_->flags = static_cast<uint16_t>(marker_->flags | 1u);
    return true;
}

// orig FUN_00126fb0 (work/decompiled/00126fb0.c): frees +0xa8, +0xac and +0xb0 when the map sprite exists
void Map2D::hide() {
    if (!shown_) return;
    shown_ = false;
    polygons().destroyRef(marker_);       // FUN_001053a0
    polygons().destroyRef(targetPoly_);
}

// orig FUN_00126300 (work/decompiled/00126300.c)
void Map2D::reset(Sprite2D& out) {
    hide();
    if (texture_ != -1) {
        out.releaseDynamicTexture(texture_);   // FUN_00105f00(0x4e6a68, +0xb4)
        texture_ = -1;
    }
    pixels_.clear();
    // +0xcc = 0, +0xd0 = 0: the target cell lives in the dungeon module (FloorContext::stairsCellX / Z)
}

// orig FUN_00126380 (work/decompiled/00126380.c)
void Map2D::tick(Map2DSource& src, Sprite2D& out) {
    if (texture_ == -1) return;
    if (!src.hasPlayer()) return;
    if (shown_ && corner_) cornerTick(src);
    if (shown_ && !corner_) {
        if (marker_ != nullptr) { marker_->anchor = kAnchorCentre; marker_->src = kMarkerTri; }
        if (targetPoly_ != nullptr) { targetPoly_->anchor = kAnchorCentre; targetPoly_->src = kTargetQuad; }
        // map alpha: idle 30 ticks -> +8 up to 0x40; moving -> -8 down to 0x10 (then wait 0x1e ticks)
        uint32_t a = alpha_;
        if (src.playerIdle()) {
            if (0 < idleCounter) idleCounter--;
        } else {
            idleCounter = -1;
        }
        if (idleCounter == 0) {
            a += 8;
            if (0x40 < a) a = 0x40;
        } else if (idleCounter < 0) {
            a -= 8;
            if (static_cast<int>(a) < 0x10) {
                a = 0x10;
                idleCounter = 0x1e;
            }
        }
        alpha_ = a;
        // player marker rotation: FUN_00206d20(&rot, forward) -> rot.y = atan2f(f.x, f.z) in degrees [V: $f12 = +0,
        // $f13 = +8 at 0x206d38; FUN_00238878 -> FUN_002363c8 is fdlibm atan2f(y = $f12, x = $f13)]; the marker
        // gets (0, 0, -rot.y) through FUN_00105240
        const Vec3 f = src.playerForward();
        const float yawDeg = (std::atan2(f.x, f.z) * 180.0f) / 3.1415927f;
        const float markerRot[3] = {0.0f, 0.0f, -yawDeg};
        if (marker_ != nullptr) polygonRotate(*marker_, markerRot);
        int halfW = gridW_;   // (short) +0x20 / 2 rounded toward zero
        if (halfW < 0) halfW = halfW + 1;
        halfW = halfW >> 1;
        int halfH = gridH_;
        if (halfH < 0) halfH = halfH + 1;
        halfH = halfH >> 1;
        const int step = static_cast<int>(scale_) * 2;
        const int px = src.playerCellX(), py = src.playerCellY();
        const int my0 = cy_ + step * (py - halfH);
        const int mx0 = cx_ + step * (px - halfW);
        if (mx0 + 2 < 0x6a) cx_ = cx_ + (0x6a - (mx0 + 2));
        else if (0x196 < mx0 + 2) cx_ = cx_ - (mx0 + -0x194);
        if (my0 + 2 < 0x7c) cy_ = cy_ + (0x7c - (my0 + 2));
        else if (0x144 < my0 + 2) cy_ = cy_ - (my0 + -0x142);
        if (marker_ != nullptr) {
            marker_->x = static_cast<float>(cx_ + static_cast<int>(scale_) * 2 * (px - halfW) + 2);
            marker_->y = static_cast<float>(cy_ + static_cast<int>(scale_) * 2 * (py - halfH) + 2);
            marker_->z = 0.0f;
        }
        if (src.targetMarkerShown()) {
            if (targetPoly_ == nullptr) {   // +0xb0: FUN_001051d0(node, 0x44a570, 0, 0, 4, 0x50a6a0), colour 0xffff0000
                targetPoly_ = polygons().create();
                polygonSetVerts(*targetPoly_, kTargetQuad, nullptr, 0, 4, targetVerts_);
                targetPoly_->colour = 0xffff0000u;
                targetPoly_->flags = static_cast<uint16_t>(targetPoly_->flags | 1u);
            }
            const int old = targetSpin;
            targetSpin = targetSpin + 3;
            targetPoly_->x = static_cast<float>(cx_ + (src.targetCellX() - halfW) * static_cast<int>(scale_) * 2 + 2);
            targetPoly_->y = static_cast<float>(cy_ + (src.targetCellY() - halfH) * static_cast<int>(scale_) * 2 + 2);
            targetPoly_->z = 0.0f;
            if (0x167 < targetSpin) targetSpin = old + -0x165;
            const float spin[3] = {0.0f, 0.0f, static_cast<float>(targetSpin)};
            polygonRotate(*targetPoly_, spin);
        }
    }
    uint32_t* pcell = src.playerCell();
    if (pcell == nullptr || *pcell == 0 || (*pcell & 0x80000000u) != 0) return;
    const uint32_t pc = *pcell;
    // pixel of texture cell (x, y): FUN_00106630 buffer, row stride +0xc4. PORT: writes outside the texture are skipped
    auto pixel = [this](int x, int y) -> uint32_t* {
        if (x < 0 || y < 0 || x >= texW_ || y >= texH_) return nullptr;
        return &pixels_[static_cast<size_t>(y) * static_cast<size_t>(texW_) + static_cast<size_t>(x)];
    };
    if ((pc & 0xf000) == 0x1000) {
        // the player's room: every floor cell becomes seen (white, '2'..'5' room types dark green, the target cell
        // blue); object cells 2..5 become partially seen (magenta unless already seen)
        char type = 0;
        int w = 0, h = 0, rx = 0, ry = 0;
        uint32_t* grid = nullptr;
        if (!src.roomInfo(static_cast<int>(pc & 0xf), type, w, h, rx, ry, grid) || grid == nullptr) return;
        for (int row = 0; row < h; ++row) {
            const int ty = ry * 8 + row;
            for (int col = 0; col < w; ++col) {
                uint32_t& c = grid[row * w + col];
                const int tx = rx * 8 + col;
                const uint32_t v = c;
                if ((v & 0xf000) == 0x1000) {
                    c = v | 0x80000000u;
                    uint32_t* p = pixel(tx, ty);
                    if (p != nullptr) {
                        if (type == '5' || type == '4' || type == '3' || type == '2') *p = 0x40408000u;
                        else *p = 0x40ffffffu;
                        if (tx == src.targetCellX() && ty == src.targetCellY()) *p = 0x40ff0000u;
                    }
                } else if ((v & 0xf000) == 0x2000) {
                    const uint32_t o = v & 0xff;
                    if (o == 5 || o == 4 || o == 3 || o == 2) {
                        const uint32_t old = c;
                        c = old | 0x40000000u;
                        if ((old & 0x80000000u) == 0) {
                            if (uint32_t* p = pixel(tx, ty)) *p = 0x40ff00ffu;
                        }
                    }
                }
            }
        }
    } else {
        // corridor: the player's cell becomes seen (yellow, green when +0xf00 bits are set), the four neighbours
        // partially seen (magenta); corridor pieces 0xf..0x12 stay hidden
        for (int k = 0; k < 5; ++k) {
            const int x = kDx[k] + src.playerCellX(), y = kDy[k] + src.playerCellY();
            uint32_t* cp = src.cell(x, y);
            if (cp == nullptr) continue;
            const uint32_t c = *cp;
            if ((c & 0x80000000u) != 0) continue;
            if ((c & 0xf000) == 0x1000) {
                *cp = c | 0x40000000u;
                if (uint32_t* p = pixel(x, y)) *p = 0x40ff00ffu;
            }
            const uint32_t c2 = *cp;
            if ((c2 & 0xf000) == 0x2000) {
                const uint32_t o = c2 & 0xff;
                if (o != 0x12 && o != 0x11 && o != 0x10 && o != 0xf) {
                    uint32_t* p = pixel(x, y);
                    if (k == 0) {
                        *cp = c2 | 0x80000000u;
                        if (p != nullptr) *p = (c2 & 0xf00) == 0 ? 0x40ffff00u : 0x4000ff00u;
                    } else {
                        *cp = c2 | 0x40000000u;
                        if ((c2 & 0x80000000u) == 0 && p != nullptr) *p = 0x40ff00ffu;
                    }
                }
            }
        }
    }
    out.updateDynamicTexture(texture_, pixels_.data());   // FUN_00106650(0x4e6a68, +0xb4)
}

// PORT: corner mini map. The window of cells around the player (clamped to the grid) is drawn 2 px per cell with
// its bottom-left at (kCornerLeft, kCornerBottom); the markers follow, the target marker only while inside the window.
void Map2D::cornerTick(Map2DSource& src) {
    alpha_ = 0x40;   // the original's idle alpha, kept while moving
    idleCounter = 0;
    winW_ = gridW_ < kCornerCellsW ? gridW_ : kCornerCellsW;
    winH_ = gridH_ < kCornerCellsH ? gridH_ : kCornerCellsH;
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    const int px = src.playerCellX(), py = src.playerCellY();
    winX_ = clampi(px - winW_ / 2, 0, gridW_ - winW_);
    winY_ = clampi(py - winH_ / 2, 0, gridH_ - winH_);
    const int top = kCornerBottom - winH_ * kCornerPx;
    const Vec3 f = src.playerForward();
    const float yawDeg = (std::atan2(f.x, f.z) * 180.0f) / 3.1415927f;
    if (marker_ != nullptr) {
        marker_->src = kMarkerTriSmall;
        const float markerRot[3] = {0.0f, 0.0f, -yawDeg};
        polygonRotate(*marker_, markerRot);
        marker_->x = static_cast<float>(kCornerLeft + (px - winX_) * kCornerPx + 1);
        marker_->y = static_cast<float>(top + (py - winY_) * kCornerPx + 1);
        marker_->z = 0.0f;
        marker_->anchor = kAnchorLeft;
    }
    const int tx = src.targetCellX(), ty = src.targetCellY();
    const bool inWin = tx >= winX_ && tx < winX_ + winW_ && ty >= winY_ && ty < winY_ + winH_;
    if (!src.targetMarkerShown() || !inWin) { polygons().destroyRef(targetPoly_); return; }
    if (targetPoly_ == nullptr) {
        targetPoly_ = polygons().create();
        polygonSetVerts(*targetPoly_, kTargetQuad, nullptr, 0, 4, targetVerts_);
        targetPoly_->colour = 0xffff0000u;
        targetPoly_->flags = static_cast<uint16_t>(targetPoly_->flags | 1u);
    }
    targetPoly_->src = kTargetQuadSmall;
    targetPoly_->x = static_cast<float>(kCornerLeft + (tx - winX_) * kCornerPx + 1);
    targetPoly_->y = static_cast<float>(top + (ty - winY_) * kCornerPx + 1);
    targetPoly_->z = 0.0f;
    targetPoly_->anchor = kAnchorLeft;
    targetSpin = targetSpin + 3;
    if (0x167 < targetSpin) targetSpin -= 0x168;
    const float spin[3] = {0.0f, 0.0f, static_cast<float>(targetSpin)};
    polygonRotate(*targetPoly_, spin);
}

// The +0xa8 sprite as FUN_00126cc0 / FUN_00126380 leave it: centre (+0xbc, +0xc0), z -1.0, half size grid * +0xb8,
// UVs 0..grid / texture, colour alpha << 24 | 0xffffff, texture bucket (layer flag 0). The markers are persistent
// nodes of the polygon layer 0x4e6990 (drawn by Renderer2D before the sprite layer, FUN_001050f0).
void Map2D::draw(Sprite2D& out) const {
    if (!shown_ || texture_ == -1) return;
    if (corner_) {   // PORT: corner window, anchored to the left edge
        ui::AnchorScope anchor(kAnchorLeft);
        const float hw = static_cast<float>(winW_ * kCornerPx) * 0.5f, hh = static_cast<float>(winH_ * kCornerPx) * 0.5f;
        out.drawSprite(texture_, static_cast<float>(winX_) / static_cast<float>(texW_), static_cast<float>(winY_) / static_cast<float>(texH_),
                       static_cast<float>(winX_ + winW_) / static_cast<float>(texW_),
                       static_cast<float>(winY_ + winH_) / static_cast<float>(texH_), static_cast<float>(kCornerLeft) + hw,
                       static_cast<float>(kCornerBottom) - hh, -1.0f, hw, hh, (alpha_ << 24) | 0xffffffu, 0);
        return;
    }
    const float hw = static_cast<float>(gridW_) * scale_, hh = static_cast<float>(gridH_) * scale_;
    out.drawSprite(texture_, 0.0f, 0.0f, static_cast<float>(gridW_) / static_cast<float>(texW_),
                   static_cast<float>(gridH_) / static_cast<float>(texH_), static_cast<float>(cx_), static_cast<float>(cy_),
                   -1.0f, hw, hh, (alpha_ << 24) | 0xffffffu, 0);
}

}  // namespace baroque::game
