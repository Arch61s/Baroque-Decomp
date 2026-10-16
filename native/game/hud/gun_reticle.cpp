// PORT-ONLY (not original): keyboard + mouse gun reticle. See game/hud/gun_reticle.hpp and game/gun_aim.hpp.
#include "game/hud/gun_reticle.hpp"

#include "game/character/character.hpp"
#include "game/engine.hpp"
#include "game/gun_aim.hpp"
#include "game/hud/hud_api.hpp"
#include "game/player_move.hpp"
#include "game/present/present_state.hpp"
#include "game/ui/menu.hpp"
#include "game/ui/message_box.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <cmath>

namespace baroque::game {

namespace {

// Everything that stops the free camera from taking input (game_scene.cpp freeCameraTakesInput) hides the reticle.
bool reticleShown(float alpha, const Chr*& player, Vec3f& pp) {
    if (!gunAimActive()) return false;
    if (engine().paused()) return false;
    const PlayerControl* ctl = playerCtl();
    if (ctl == nullptr || (ctl->flags & 0x10001u) != 0) return false;
    player = characters().player();
    if (player == nullptr || player->status.block.hp <= 0) return false;
    if (hudItemScreenOpen() || hudReadEnvironment().systemMenuOpen) return false;
    if (ui::menu().isOpen() || ui::messageBox().isOpen()) return false;
    const present::Presenter& P = present::presenter();
    float cw[4][4];
    int src = present::kCamGameplay;
    if (P.camera(alpha, cw, &src) && src != present::kCamGameplay) return false;   // event / talk camera drawn
    if (!P.playerPos(alpha, &pp)) pp = player->xf.m.pos;
    return true;
}

bool project(const GunAimView& v, const Vec3f& p, float W, float H, Vector2& out) {
    const float d[3] = {p[0] - v.eye[0], p[1] - v.eye[1], p[2] - v.eye[2]};
    const float x = d[0] * v.right[0] + d[1] * v.right[1] + d[2] * v.right[2];
    const float y = d[0] * v.up[0] + d[1] * v.up[1] + d[2] * v.up[2];
    const float z = -(d[0] * v.back[0] + d[1] * v.back[1] + d[2] * v.back[2]);
    if (z < 10.0f) return false;   // near plane 10
    const float aspect = W / H;
    out.x = (0.5f + 0.5f * x / (z * v.tanHalfY * aspect)) * W;
    out.y = (0.5f - 0.5f * y / (z * v.tanHalfY)) * H;
    return true;
}

// A bar with a dark outline (readable on light and dark scenes).
void bar(float x, float y, float w, float h, float o, Color fill, Color edge) {
    DrawRectangleRec({x - o, y - o, w + 2 * o, h + 2 * o}, edge);
    DrawRectangleRec({x, y, w, h}, fill);
}

void drawCrosshair(Vector2 c, float s, Color fill, Color edge) {
    const float o = 1.25f * s;        // outline
    const float gap = 6.0f * s, len = 9.0f * s, th = 2.4f * s;
    DrawCircleV(c, 2.4f * s + o, edge);
    DrawCircleV(c, 2.4f * s, fill);
    bar(c.x - th / 2, c.y - gap - len, th, len, o, fill, edge);   // up
    bar(c.x - th / 2, c.y + gap, th, len, o, fill, edge);         // down
    bar(c.x - gap - len, c.y - th / 2, len, th, o, fill, edge);   // left
    bar(c.x + gap, c.y - th / 2, len, th, o, fill, edge);         // right
}

void drawImpact(Vector2 c, float s, Color fill, Color edge) {
    const float r0 = 4.0f * s, r1 = 5.75f * s, o = 1.25f * s;
    DrawRing(c, r0 - o, r1 + o, 0.0f, 360.0f, 32, edge);
    DrawRing(c, r0, r1, 0.0f, 360.0f, 32, fill);
}

}  // namespace

void gunReticleDraw(int screenW, int screenH, float alpha) {
    // PORT: the reticle and the landing ring are not drawn; the camera-relative aim (gun_aim.cpp)
    // still applies to the shots. kDrawReticle = true brings the overlay back.
    constexpr bool kDrawReticle = false;
    if (!kDrawReticle) return;
    const Chr* player = nullptr;
    Vec3f pp{};
    if (!reticleShown(alpha, player, pp)) return;
    GunAimView v;
    if (!gunAimView(pp, v)) return;
    GunAimSolution sol;
    if (!gunAimPredict(*player, pp, sol)) return;
    const float W = float(screenW), H = float(screenH);
    Vector2 centre{};
    const Vec3f ahead{v.eye[0] + v.rayDir[0] * 100.0f, v.eye[1] + v.rayDir[1] * 100.0f, v.eye[2] + v.rayDir[2] * 100.0f};
    if (!project(v, ahead, W, H, centre)) return;
    float s = 1.35f * H / 1080.0f;   // ~1.35 px per 1080p pixel: a 50 px crosshair at 1080p
    if (s < 1.0f) s = 1.0f;
    const bool ammo = gunAimHasAmmo();
    const unsigned char a = ammo ? 235 : 110;
    const Color fill = sol.onEnemy ? Color{255, 96, 84, a} : Color{248, 248, 244, a};
    const Color edge{8, 8, 12, static_cast<unsigned char>(ammo ? 170 : 80)};

    rlDrawRenderBatchActive();
    rlViewport(0, 0, screenW, screenH);
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(0.0, screenW, screenH, 0.0, -1.0, 1.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlDisableDepthTest();
    rlSetBlendMode(RL_BLEND_ALPHA);
    // where the shot really lands, when that is not under the reticle (pitch limit / something in the way)
    // (the crosshair is dimmed then, so the ring reads as "the shot goes here")
    Vector2 hit{};
    bool diverted = false;
    if (sol.impactValid && project(v, sol.impact, W, H, hit)) {
        const float dx = hit.x - centre.x, dy = hit.y - centre.y;
        if (dx * dx + dy * dy > (9.0f * s) * (9.0f * s)) {
            drawImpact(hit, s, Color{255, 214, 120, a}, edge);
            diverted = true;
        }
    } else if (sol.clamped) {
        diverted = true;   // lands outside the view
    }
    Color f = fill, e = edge;
    if (diverted) {
        f.a = static_cast<unsigned char>(f.a * 0.5f);
        e.a = static_cast<unsigned char>(e.a * 0.5f);
    }
    drawCrosshair(centre, s, f, e);
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
}

namespace {
const bool gInstalled = [] {
    hudSetWindowOverlay(&gunReticleDraw);
    return true;
}();
}  // namespace

}  // namespace baroque::game
