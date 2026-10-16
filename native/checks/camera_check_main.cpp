// Baroque-CameraCheck: headless run of the gameplay camera (game/camera.*). A fake player walks a scripted path
// (straight +z, then a 90 degree turn to +x, then stands still), the raycast never hits. Prints eye/target every 10
// ticks and asserts: no NaN, boom length |eye - target| converges to the preset distance, eye height = target
// height, horizontal eye->player distance stays within dist +- lag, R3 reset puts the eye exactly `dist` behind
// the player, pitch clamps. Also a short phase with a hitting raycast (boom snaps in, eases out).
#include <cmath>
#include <cstdio>

#include "game/camera.hpp"
#include "game/pad.hpp"

using namespace baroque::game;

namespace {

int g_failures = 0;

void check(bool ok, const char* what, int tick) {
    if (!ok) {
        g_failures++;
        std::printf("FAIL tick %d: %s\n", tick, what);
    }
}

bool finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

float len3(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

bool cameraFinite(const Camera& c) {
    for (const auto& row : c.camWorld)
        for (float f : row)
            if (!std::isfinite(f)) return false;
    for (const auto& row : c.viewMatrix)
        for (float f : row)
            if (!std::isfinite(f)) return false;
    return finite3(c.eye) && finite3(c.target) && finite3(c.dir) && std::isfinite(c.pitch) &&
           std::isfinite(c.yawDelta) && std::isfinite(c.pitchRad);
}

}  // namespace

int main() {
    CameraWorld world;
    world.playerPos[0] = 0.0f;
    world.playerPos[1] = 0.0f;
    world.playerPos[2] = 0.0f;
    world.playerForward[0] = 0.0f;
    world.playerForward[1] = 0.0f;
    world.playerForward[2] = 1.0f;
    EngineProjection engine;
    float hitDistance = -1.0f;   // < 0: raycast never hits
    int transitions = 0;
    CameraHooks hooks;
    hooks.raycast = [&](const float*, const float*, float, float* distOut) {
        if (hitDistance < 0.0f) return false;
        *distOut = hitDistance;
        return true;
    };
    hooks.requestScreenTransition = [&]() { transitions++; };
    Camera cam(world, engine, hooks);

    const CameraTunables& preset = kCameraPresetDungeon;
    cam.applyPreset(preset);
    std::printf("init eye (%.3f %.3f %.3f) target (%.3f %.3f %.3f)\n", cam.eye[0], cam.eye[1], cam.eye[2],
                cam.target[0], cam.target[1], cam.target[2]);
    check(std::fabs(cam.eye[2] - -300.0f) < 1e-3f && std::fabs(cam.target[1] - 185.0f) < 1e-3f,
          "init places eye dist behind player at height", 0);

    const float speed = 12.0f;   // run speed (RUNTIME.md 2.5 base 12.0)
    float maxH = 0.0f;
    float minH = 1e9f;
    for (int t = 1; t <= 120; t++) {
        // scripted player: ticks 1..40 walk +z, 41..80 walk +x (90 degree turn), 81..120 stand still
        world.controlFlags = 0;
        if (t <= 40) {
            world.playerForward[0] = 0.0f;
            world.playerForward[2] = 1.0f;
            world.playerPos[2] += speed;
            world.controlFlags |= 0x10;   // moved this tick (FUN_0013cf90)
        } else if (t <= 80) {
            world.playerForward[0] = 1.0f;
            world.playerForward[2] = 0.0f;
            world.playerPos[0] += speed;
            world.controlFlags |= 0x10;
        }
        PadPort pad;   // sticks centred: inputFromPad must not change anything
        cam.inputFromPad(pad);
        cam.applyMouseInput();
        cam.tick();

        const float boom = len3(cam.eye[0] - cam.target[0], cam.eye[1] - cam.target[1], cam.eye[2] - cam.target[2]);
        const float hx = cam.eye[0] - world.playerPos[0];
        const float hz = cam.eye[2] - world.playerPos[2];
        const float horiz = std::sqrt(hx * hx + hz * hz);
        if (t >= 4) {
            maxH = std::fmax(maxH, horiz);
            minH = std::fmin(minH, horiz);
        }
        check(cameraFinite(cam), "NaN/inf in camera state", t);
        // The boom history 0x50b710..0x50b71c is a zero-initialised global that init/reset never clear, so on the
        // very first ticks the 4-tap average (dist,0,0,0) pulls the eye in (75, 150, 225) and it eases out; from
        // tick 4 the history is full and the boom must sit exactly at the preset distance.
        check(boom <= preset.dist + 0.01f, "boom length <= preset dist", t);
        if (t >= 4) {
            check(std::fabs(boom - preset.dist) < 0.01f, "boom length == preset dist", t);
            check(horiz <= preset.dist + preset.lag + 0.01f && horiz >= preset.dist - preset.lag - 0.01f,
                  "horizontal eye-player distance within dist +- lag", t);
        }
        check(std::fabs(cam.eye[1] - (world.playerPos[1] + preset.height)) < 0.01f, "eye height", t);
        if (t % 10 == 0 || t <= 4) {
            std::printf("tick %3d player (%7.2f %6.2f %7.2f) eye (%8.3f %7.3f %8.3f) target (%8.3f %7.3f %8.3f) "
                        "boom %.3f eye-player(h) %.3f pitch %.2f\n",
                        t, world.playerPos[0], world.playerPos[1], world.playerPos[2], cam.eye[0], cam.eye[1],
                        cam.eye[2], cam.target[0], cam.target[1], cam.target[2], boom, horiz, cam.pitch);
        }
    }
    std::printf("horizontal eye-player distance range over ticks 4..120: [%.3f, %.3f] (dist %.1f, lag %.1f)\n", minH,
                maxH, preset.dist, preset.lag);
    check(std::fabs(maxH - (preset.dist + preset.lag)) < 0.5f, "walking away converges to dist + lag", 120);

    // camera forward (+0x70) must point from target toward eye in xz, and the view matrix must invert camWorld
    {
        float prod[4][4] = {};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                for (int k = 0; k < 4; k++) prod[i][j] += cam.camWorld[i][k] * cam.viewMatrix[k][j];
        float err = 0.0f;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) err = std::fmax(err, std::fabs(prod[i][j] - (i == j ? 1.0f : 0.0f)));
        check(err < 1e-4f, "viewMatrix == inverse(camWorld)", 120);
    }

    // R3 reset (FUN_0013f0b0 -> FUN_0013f560): player now faces +x, camera was dragged, so a reset happens
    {
        PadPort pad;
        pad.pressed = 0x400;
        cam.inputFromPad(pad);
        const float hx = cam.eye[0] - world.playerPos[0];
        const float hz = cam.eye[2] - world.playerPos[2];
        std::printf("after R3: eye (%.3f %.3f %.3f) eye-player(h) %.3f transitions %d\n", cam.eye[0], cam.eye[1],
                    cam.eye[2], std::sqrt(hx * hx + hz * hz), transitions);
        check(transitions == 1, "R3 reset requested a screen transition", 121);
        check(std::fabs(std::sqrt(hx * hx + hz * hz) - preset.dist) < 0.01f, "reset: eye exactly dist from player", 121);
        check(std::fabs(cam.eye[0] - (world.playerPos[0] - preset.dist)) < 0.01f, "reset: eye behind +x player", 121);
        cam.inputFromPad(pad);   // same forward, no manual rotation since: no second reset
        check(transitions == 1, "R3 again without turning does nothing", 121);
    }

    // mouse (port-only path): pitch clamps to [-45, 5]; yaw marks manual rotation and turns the camera
    {
        cam.addMouseDelta(0.0f, 1000.0f);   // mouse down -> pitch decreases like the stick
        cam.applyMouseInput();
        check(cam.pitch == -45.0f, "mouse pitch clamped to -45", 122);
        cam.addMouseDelta(0.0f, -1000.0f);
        cam.applyMouseInput();
        check(cam.pitch == 5.0f, "mouse pitch clamped to +5", 122);
        const float dir0x = cam.dir[0];
        const float dir0z = cam.dir[2];
        cam.addMouseDelta(10.0f, 0.0f);
        cam.applyMouseInput();
        check(cam.playerFwdAtReset[0] == 10000.0f, "yaw input sets the manual-rotation marker", 122);
        float turned = 0.0f;
        for (int i = 0; i < 30; i++) {
            cam.tick();
            check(cameraFinite(cam), "NaN after mouse yaw", 123 + i);
        }
        turned = std::atan2(dir0x * cam.dir[2] - dir0z * cam.dir[0], dir0x * cam.dir[0] + dir0z * cam.dir[2]);
        std::printf("mouse yaw 10 units -> camera turned %.3f deg over 30 ticks; pitch held %.2f\n",
                    turned * 57.29578f, cam.pitch);
        check(cam.pitch == 5.0f && cam.mousePitchHold, "mouse pitch is held (port-only free look)", 152);
    }

    // collision phase: raycast hits at 100 -> boom snaps in immediately, eases out over the 4-tap history
    {
        hitDistance = 100.0f;
        cam.tick();
        const float boomIn = len3(cam.eye[0] - cam.target[0], cam.eye[1] - cam.target[1], cam.eye[2] - cam.target[2]);
        hitDistance = -1.0f;
        float boomOut[4];
        for (float& b : boomOut) {
            cam.tick();
            b = len3(cam.eye[0] - cam.target[0], cam.eye[1] - cam.target[1], cam.eye[2] - cam.target[2]);
        }
        std::printf("collision: hit -> boom %.3f, then %.3f %.3f %.3f %.3f\n", boomIn, boomOut[0], boomOut[1],
                    boomOut[2], boomOut[3]);
        check(std::fabs(boomIn - 100.0f) < 0.01f, "boom snaps to hit distance", 153);
        check(std::fabs(boomOut[0] - 250.0f) < 0.01f && std::fabs(boomOut[3] - 300.0f) < 0.01f,
              "boom eases out through the 4-tap average", 157);
    }

    std::printf("projection after reset: fov %.1f near %.1f far %.1f\n", engine.fovDeg, engine.nearZ, engine.farZ);
    if (g_failures == 0) {
        std::printf("CAMERA CHECK PASSED\n");
        return 0;
    }
    std::printf("CAMERA CHECK FAILED (%d)\n", g_failures);
    return 1;
}
