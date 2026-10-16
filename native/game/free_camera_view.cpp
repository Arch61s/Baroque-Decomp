// PORT-ONLY: free mouse camera presentation (see free_camera.hpp / free_camera_view.hpp, CAMERA_NATIVE.md §7).
#include "game/free_camera_view.hpp"

#include "alchemy/scene_render.hpp"
#include "game/collision.hpp"
#include "game/floor_world.hpp"
#include "game/free_camera.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace baroque::game {
namespace {

// Occluders: the opaque, visible triangles of the drawn map models in world space, bucketed in an xz grid. The
// collision meshes (CLS) only hold floors and the lower part of the walls, so the camera also needs the render
// geometry (ceilings, upper walls, beams) to stay out of it.
constexpr float kCell = 256.0f;
constexpr float kRayRight = 10.0f;     // near-plane half width at near 10 up to ~21:9 Hor+ (5.9 at 16:9), + margin
constexpr float kRayUp = 6.0f;         // near-plane half height (3.3) + margin
constexpr float kBackOff = 4.0f;       // stop this far before the nearest hit along the boom
constexpr float kCollisionRadius = 5.0f;   // the original boom sweep radius (0x44b2a8)

struct Occluders {
    const FloorWorld* world = nullptr;
    std::vector<float> tris;                                     // 9 floats per triangle
    std::unordered_map<int64_t, std::vector<uint32_t>> cells;    // cell key -> triangle indices
    std::vector<uint32_t> stamp;                                 // per triangle, de-duplicates a query
    std::vector<uint64_t> key;                                   // PORT: per triangle, instance << 32 | mesh index
    uint32_t query = 0;
};
Occluders gOcc;

int64_t cellKey(int cx, int cz) { return (int64_t(cx) << 32) ^ int64_t(uint32_t(cz)); }
int cellOf(float v) { return int(std::floor(v / kCell)); }

void build(const FloorWorld* world) {
    gOcc = Occluders{};
    gOcc.world = world;
    if (world == nullptr) return;
    uint32_t instIndex = 0;
    world->forEachInstance([&instIndex](const alchemy::SceneModel& model, const alchemy::Mat4& inst) {
        const uint32_t ii = instIndex++;
        const std::vector<alchemy::Mesh>& meshes = model.meshes();
        for (size_t mi = 0; mi < meshes.size(); ++mi) {
            const alchemy::Mesh& m = meshes[mi];
            // PORT: blended meshes that write depth (alpha-textured walls, grates) occlude as well; only non-depth
            // writing ones (glows, effects) are skipped
            if (m.skinned || (m.state.blendEnabled && !m.state.depthWrite) || m.positions.empty() || m.indices.size() < 3) continue;
            if (!model.meshVisible(mi)) continue;
            const alchemy::Mat4 w = alchemy::mul(model.meshWorld(m), inst);
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                float t[9];
                for (int k = 0; k < 3; ++k) {
                    const size_t vi = size_t(m.indices[i + size_t(k)]) * 3;
                    if (vi + 2 >= m.positions.size()) { t[0] = NAN; break; }
                    alchemy::transformPoint(w, &m.positions[vi], &t[k * 3]);
                }
                if (std::isnan(t[0])) continue;
                const uint32_t id = uint32_t(gOcc.tris.size() / 9);
                gOcc.tris.insert(gOcc.tris.end(), t, t + 9);
                gOcc.key.push_back((uint64_t(ii) << 32) | uint64_t(mi));
                const float x0 = std::min({t[0], t[3], t[6]}), x1 = std::max({t[0], t[3], t[6]});
                const float z0 = std::min({t[2], t[5], t[8]}), z1 = std::max({t[2], t[5], t[8]});
                for (int cx = cellOf(x0); cx <= cellOf(x1); ++cx)
                    for (int cz = cellOf(z0); cz <= cellOf(z1); ++cz) gOcc.cells[cellKey(cx, cz)].push_back(id);
            }
        }
    });
    gOcc.stamp.assign(gOcc.tris.size() / 9, 0u);
}

// Nearest hit parameter t in [0, 1] of the segment a -> b against the occluders (two-sided Moller-Trumbore); 2 = none.
// PORT: with `keys`, the instance / mesh key of every triangle the segment crosses is appended as well.
float castSegment(const float a[3], const float b[3], std::vector<uint64_t>* keys = nullptr) {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float best = 2.0f;
    if (gOcc.tris.empty()) return best;
    if (++gOcc.query == 0) {
        std::fill(gOcc.stamp.begin(), gOcc.stamp.end(), 0u);
        gOcc.query = 1;
    }
    const int cx0 = cellOf(std::min(a[0], b[0])), cx1 = cellOf(std::max(a[0], b[0]));
    const int cz0 = cellOf(std::min(a[2], b[2])), cz1 = cellOf(std::max(a[2], b[2]));
    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cz = cz0; cz <= cz1; ++cz) {
            const auto it = gOcc.cells.find(cellKey(cx, cz));
            if (it == gOcc.cells.end()) continue;
            for (uint32_t id : it->second) {
                if (gOcc.stamp[id] == gOcc.query) continue;
                gOcc.stamp[id] = gOcc.query;
                const float* t = &gOcc.tris[size_t(id) * 9];
                const float e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
                const float e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
                const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
                const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
                if (std::fabs(det) < 1e-8f) continue;
                const float inv = 1.0f / det;
                const float s[3] = {a[0] - t[0], a[1] - t[1], a[2] - t[2]};
                const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
                if (u < 0.0f || u > 1.0f) continue;
                const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
                const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
                if (v < 0.0f || u + v > 1.0f) continue;
                const float tt = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
                if (tt >= 0.0f && tt < best) best = tt;
                if (keys != nullptr && tt >= 0.0f && tt <= 1.0f) keys->push_back(gOcc.key[id]);
            }
        }
    return best;
}

// Clear boom length from the pivot at (yaw, elevation): the render-geometry rays and the game's own boom sweep.
float clearBoom(const float pivot[3], float yaw, float elevDeg, float dist, float back[3], float right[3], float bu[3]) {
    const float kDeg = 3.14159265f / 180.0f;
    const float ce = std::cos(elevDeg * kDeg), se = std::sin(elevDeg * kDeg);
    const float sy = std::sin(yaw), cy = std::cos(yaw);
    back[0] = ce * sy; back[1] = se; back[2] = ce * cy;    // boom direction (unit)
    right[0] = cy; right[1] = 0.0f; right[2] = -sy;        // worldUp x back(yaw)
    bu[0] = back[1] * right[2] - back[2] * right[1];       // up across the boom
    bu[1] = back[2] * right[0] - back[0] * right[2];
    bu[2] = back[0] * right[1] - back[1] * right[0];
    // 1. render geometry: centre ray + four rays through the near-plane corners, parallel to the boom
    float want = dist;
    const float offs[5][2] = {{0.0f, 0.0f}, {kRayRight, kRayUp}, {-kRayRight, kRayUp}, {kRayRight, -kRayUp}, {-kRayRight, -kRayUp}};
    for (const auto& o : offs) {
        const float a[3] = {pivot[0] + right[0] * o[0] + bu[0] * o[1], pivot[1] + right[1] * o[0] + bu[1] * o[1],
                            pivot[2] + right[2] * o[0] + bu[2] * o[1]};
        const float b[3] = {a[0] + back[0] * dist, a[1] + back[1] * dist, a[2] + back[2] * dist};
        const float t = castSegment(a, b);
        if (t <= 1.0f) want = std::min(want, t * dist - kBackOff);
    }
    // 2. the game's own boom sweep (FUN_00143150, as FUN_0013e340: from the target, radius 5): collision-only walls.
    //    It writes the collision module's global hit list (0x50b730..0x50ba38); keep the tick's values intact.
    {
        GlobalHits saved = globalHits();
        const float delta[3] = {back[0] * dist, back[1] * dist, back[2] * dist};
        float hitDist = dist;
        if (raycast(pivot, delta, kCollisionRadius, &hitDist)) want = std::min(want, hitDist - kBackOff);
        globalHits() = saved;
    }
    return want;
}

}  // namespace

void freeCameraWorldReset() { gOcc = Occluders{}; }

bool freeCameraSegmentBlocked(const FloorWorld* world, const float a[3], const float b[3]) {
    if (gOcc.world != world) build(world);
    return castSegment(a, b) <= 1.0f;
}

void freeCameraSegmentMeshes(const FloorWorld* world, const float a[3], const float b[3], std::vector<uint64_t>& keys) {
    if (gOcc.world != world) build(world);
    castSegment(a, b, &keys);
}

void freeCameraMatrix(const FloorWorld* world, const float playerPos[3], float height, float dist, float defaultPitchDeg,
                      float dt, float out[4][4]) {
    FreeCamera& c = freeCamera();
    if (gOcc.world != world) build(world);
    const float kDeg = 3.14159265f / 180.0f;
    const float pivot[3] = {playerPos[0], playerPos[1] + height, playerPos[2]};
    const float sy = std::sin(c.yaw), cy = std::cos(c.yaw);
    float back[3], right[3], bu[3];
    // PORT: the boom never shortens below kFreeCamMinFrac * dist (the character never fills the
    // screen) and never lifts; walls the eye then sits in or behind are thinned by the see-through dither
    // (SceneRenderer::setCut). Only the floor is a hard limit: a low eye stays kFreeCamFloorClear above the feet.
    float want = clearBoom(pivot, c.yaw, c.elevDeg, dist, back, right, bu);
    want = std::max(want, kFreeCamMinFrac * dist);
    if (back[1] < -0.01f) want = std::min(want, (height - kFreeCamFloorClear) / -back[1]);
    want = std::min(std::max(want, kFreeCamMinDist), dist);
    // ease in and out, gently; on the way in the eye may trail up to kFreeCamClipSlack * dist past the hit, so walls
    // and bumps glide (the eye passes briefly into them) instead of snapping
    if (c.shownDist < 0.0f) {
        c.shownDist = want;
    } else {
        const float rate = want < c.shownDist ? kFreeCamEaseInPerSec : kFreeCamEaseOutPerSec;
        float k = 1.0f - std::exp(-rate * (dt > 0.0f ? dt : 0.0f));
        if (k > 1.0f) k = 1.0f;
        c.shownDist += (want - c.shownDist) * k;
        const float maxDist = want + kFreeCamClipSlack * dist;
        if (c.shownDist > maxDist) c.shownDist = maxDist;
        if (back[1] < -0.01f) c.shownDist = std::min(c.shownDist, (height - kFreeCamFloorClear) / -back[1]);
    }
    const float d = c.shownDist;
    const float eye[3] = {pivot[0] + back[0] * d, pivot[1] + back[1] * d, pivot[2] + back[2] * d};
    // view: from the eye toward the look-at point (pivot lowered by dist * tan(-default pitch), where the original's
    // tilted view line meets the player). Rows right / up / f, f = unit(eye - lookAt) (camera +0x70 convention); right
    // comes from the yaw (f lies in the vertical plane of back(yaw), so it stays perpendicular even looking down).
    const float drop = defaultPitchDeg < 0.0f ? dist * std::tan(-defaultPitchDeg * kDeg) : 0.0f;
    float f[3] = {eye[0] - pivot[0], eye[1] - (pivot[1] - drop), eye[2] - pivot[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl > 0.0f) {
        f[0] /= fl;
        f[1] /= fl;
        f[2] /= fl;
    } else {
        f[0] = sy;
        f[1] = 0.0f;
        f[2] = cy;
    }
    const float up[3] = {f[1] * right[2] - f[2] * right[1], f[2] * right[0] - f[0] * right[2],
                         f[0] * right[1] - f[1] * right[0]};
    const float* rows[3] = {right, up, f};
    for (int r = 0; r < 3; ++r) {
        out[r][0] = rows[r][0];
        out[r][1] = rows[r][1];
        out[r][2] = rows[r][2];
        out[r][3] = 0.0f;
    }
    out[3][0] = eye[0];
    out[3][1] = eye[1];
    out[3][2] = eye[2];
    out[3][3] = 1.0f;
}

}  // namespace baroque::game
