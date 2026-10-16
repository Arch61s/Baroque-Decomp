// Baroque-EffectsCheck: EXI particle runtime (native/game/effects, work/reports/EFFECTS_NATIVE.md).
//  1. Loads DATA/EXI/BQ.EXI, spawns every effect id in its own manager (fresh seed 0xbadface), runs the original
//     draw+update pass headless for N ticks and prints per-effect particle statistics plus a hash; the sweep is run
//     twice and must give identical results (determinism, no wall clock).
//  2. Renders selected effects (player attack / hit / item ids from ACT Exi_a/Exi_b and FUN_0014bc30) with the rlgl
//     backend into work/native_captures/effects_<id>.png: 4 frames per effect on a dark background, fixed camera
//     framed on the particle bounds of the middle frame.
// Usage: Baroque-EffectsCheck [ticks=120] [--list]
#include "game/effects/effects.hpp"
#include "game/effects/effects_gl.hpp"

#include "platform/platform.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace baroque::game;

namespace {

struct Stats {
    int ticksAlive = 0;   // ticks until the instance was freed (or N)
    int peak = 0;
    int at10 = 0, at30 = 0, at60 = 0;
    long long vertices = 0;
    int batches = 0;
};

// Row-vector (v * M) GL-style perspective and camera matrices.
ExiMat perspective(float fovyDeg, float aspect, float n, float f) {
    ExiMat m;
    for (float& v : m.m) v = 0.0f;
    const float t = 1.0f / std::tan(fovyDeg * 3.14159265f / 360.0f);
    m.m[0] = t / aspect;
    m.m[5] = t;
    m.m[10] = (f + n) / (n - f);
    m.m[11] = -1.0f;
    m.m[14] = 2.0f * f * n / (n - f);
    return m;
}

ExiView makeView(const float eye[3], const float target[3]) {
    ExiView v;
    float z[3] = {eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]};
    float l = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (float& c : z) c /= l;
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float x[3];
    exiCross(x, up, z);
    l = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (float& c : x) c /= l;
    float y[3];
    exiCross(y, z, x);
    ExiMat cam;
    exiIdentity(cam);
    for (int i = 0; i < 3; ++i) {
        cam.m[0 + i] = x[i];
        cam.m[4 + i] = y[i];
        cam.m[8 + i] = z[i];
        cam.m[12 + i] = eye[i];
    }
    exiInvert(v.view, cam);
    v.projection = perspective(37.0f, 512.0f / 448.0f, 10.0f, 20000.0f);
    v.fogStart = 10000.0f;  // far away: no distance fade in the check
    v.fogEnd = 20000.0f;
    return v;
}

// Captures vertices (bounds) without GL.
class BoundsSink final : public ExiBatchSink {
public:
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    int count = 0;
    void setState(const ExiRenderState&) override {}
    void draw(const ExiRenderState&, const ExiDrawVertex* v, int n) override {
        for (int i = 0; i < n; ++i) {
            const float p[3] = {v[i].x, v[i].y, v[i].z};
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], p[k]);
                hi[k] = std::max(hi[k], p[k]);
            }
        }
        count += n;
    }
};

Stats runEffect(const baroque::data::ExiFile*, EffectManager& mgr, int id, int ticks, const ExiView& view) {
    Stats s;
    const float pos[3] = {0.0f, 0.15f, 0.0f};
    const float scale[3] = {100.0f, 100.0f, 100.0f};
    const int handle = mgr.create(id, pos, nullptr, scale, nullptr);
    if (handle == 0) return s;
    for (int t = 0; t < ticks; ++t) {
        const ExiPassStats st = mgr.drawPass(view, nullptr);
        s.vertices += st.vertices;
        s.batches += st.batches;
        const ExiInstance* inst = mgr.find(handle);
        const int p = inst != nullptr ? inst->particles : 0;
        s.peak = std::max(s.peak, p);
        if (t == 10) s.at10 = p;
        if (t == 30) s.at30 = p;
        if (t == 60) s.at60 = p;
        if (inst == nullptr) break;
        s.ticksAlive = t + 1;
    }
    return s;
}

uint64_t fnv(uint64_t h, long long v) {
    for (int i = 0; i < 8; ++i) {
        h ^= static_cast<uint64_t>((v >> (8 * i)) & 0xff);
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t sweep(const std::vector<uint8_t>& exi, int ticks, bool print, int* effectsWithParticles, int* nonEmpty) {
    const ExiView view = makeView(std::array<float, 3>{0.0f, 200.0f, 600.0f}.data(),
                                  std::array<float, 3>{0.0f, 50.0f, 0.0f}.data());
    uint64_t h = 1469598103934665603ull;
    EffectManager probe;
    probe.load(exi);
    const int n = probe.effectCount();
    *effectsWithParticles = 0;
    *nonEmpty = 0;
    for (int id = 0; id < n; ++id) {
        EffectManager mgr;
        mgr.load(exi);
        const Stats s = runEffect(nullptr, mgr, id, ticks, view);
        if (s.peak > 0) ++*effectsWithParticles;
        if (s.vertices > 0) ++*nonEmpty;
        h = fnv(h, s.ticksAlive);
        h = fnv(h, s.peak);
        h = fnv(h, s.at10);
        h = fnv(h, s.at30);
        h = fnv(h, s.at60);
        h = fnv(h, s.vertices);
        h = fnv(h, mgr.seed());
        if (print)
            std::printf("effect %3d: alive %3d ticks, peak %3d particles, t10 %3d t30 %3d t60 %3d, %7lld vertices, "
                        "%5d batches\n", id, s.ticksAlive, s.peak, s.at10, s.at30, s.at60, s.vertices, s.batches);
    }
    return h;
}

// Hashes every submitted vertex (and state change) of a pass.
class HashSink final : public ExiBatchSink {
public:
    uint64_t h = 1469598103934665603ull;
    long long verts = 0;
    void mix(const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    void setState(const ExiRenderState& s) override { mix(&s.object, sizeof s.object); }
    void draw(const ExiRenderState& s, const ExiDrawVertex* v, int n) override {
        mix(&s.object, sizeof s.object);
        mix(v, sizeof(ExiDrawVertex) * size_t(n));
        verts += n;
    }
};

// PORT presentation split: drawPass with a recording + replay with the same view must submit exactly the vertices of
// the direct pass (every effect, 60 ticks); a replay with another camera must stay within the budget.
bool replayCheck(const std::vector<uint8_t>& exi) {
    const ExiView view = makeView(std::array<float, 3>{0.0f, 200.0f, 600.0f}.data(),
                                  std::array<float, 3>{0.0f, 50.0f, 0.0f}.data());
    const ExiView other = makeView(std::array<float, 3>{500.0f, 150.0f, -300.0f}.data(),
                                   std::array<float, 3>{0.0f, 50.0f, 0.0f}.data());
    EffectManager probe;
    probe.load(exi);
    int mismatches = 0;
    long long total = 0;
    for (int id = 0; id < probe.effectCount(); ++id) {
        EffectManager a, b;
        a.load(exi);
        b.load(exi);
        const float pos[3] = {0.0f, 0.15f, 0.0f};
        const float scale[3] = {100.0f, 100.0f, 100.0f};
        a.create(id, pos, nullptr, scale, nullptr);
        b.create(id, pos, nullptr, scale, nullptr);
        HashSink ha, hb;
        for (int t = 0; t < 60; ++t) {
            a.drawPass(view, &ha);
            ExiRecording rec;
            b.drawPass(view, nullptr, &rec);
            b.replay(view, rec, &hb);
            HashSink ho;
            const ExiPassStats so = b.replay(other, rec, &ho);
            if (so.vertices > kExiMaxVertices) ++mismatches;
        }
        total += ha.verts;
        if (ha.h != hb.h || ha.verts != hb.verts) {
            ++mismatches;
            std::printf("replay mismatch: effect %d (%lld / %lld vertices)\n", id, ha.verts, hb.verts);
        }
    }
    std::printf("replay: %d effects x 60 ticks, %lld vertices, recorded+replayed == direct pass: %s\n",
                probe.effectCount(), total, mismatches == 0 ? "yes" : "NO");
    return mismatches == 0;
}

struct Capture {
    int id;
    float tag;
    const char* what;
};

bool renderEffect(const std::vector<uint8_t>& exi, const Capture& c, const fs::path& out) {
    const int frames[4] = {3, 8, 15, 26};
    const int W = 512, H = 448;
    // Pass 1 (headless): bounds at the middle frames to frame the camera.
    BoundsSink bounds;
    {
        EffectManager mgr;
        mgr.load(exi);
        const float pos[4] = {0.0f, 0.15f, 0.0f, 0.0f};
        const float scale[3] = {100.0f, 100.0f, 100.0f};
        const int h = mgr.create(c.id, pos, nullptr, scale, nullptr);
        mgr.setTag(c.tag, h);
        const ExiView v0 = makeView(std::array<float, 3>{0.0f, 300.0f, 900.0f}.data(),
                                    std::array<float, 3>{0.0f, 0.0f, 0.0f}.data());
        for (int t = 0; t <= frames[3]; ++t) mgr.drawPass(v0, (t == frames[1] || t == frames[2]) ? &bounds : nullptr);
    }
    float centre[3] = {0.0f, 50.0f, 0.0f};
    float radius = 150.0f;
    if (bounds.count > 0) {
        for (int k = 0; k < 3; ++k) centre[k] = 0.5f * (bounds.lo[k] + bounds.hi[k]);
        const float dx = bounds.hi[0] - bounds.lo[0], dy = bounds.hi[1] - bounds.lo[1], dz = bounds.hi[2] - bounds.lo[2];
        radius = std::max(40.0f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    const float dist = radius / std::tan(18.5f * 3.14159265f / 180.0f) * 1.1f;
    const float eye[3] = {centre[0], centre[1] + dist * 0.35f, centre[2] + dist * 0.94f};
    const ExiView view = makeView(eye, centre);

    EffectManager& mgr = effects().manager();
    mgr.killAll();
    const float pos[4] = {0.0f, 0.15f, 0.0f, 0.0f};
    const float scale[3] = {100.0f, 100.0f, 100.0f};
    const int h = mgr.create(c.id, pos, nullptr, scale, nullptr);
    mgr.setTag(c.tag, h);
    Image sheet = GenImageColor(W * 4 / 2, H / 2, Color{0, 0, 0, 255});
    RenderTexture2D rt = LoadRenderTexture(W, H);
    int frameIdx = 0;
    int drawnTotal = 0;
    for (int t = 0; t <= frames[3]; ++t) {
        const bool capture = t == frames[frameIdx];
        BeginTextureMode(rt);
        ClearBackground(Color{14, 14, 22, 255});
        rlDrawRenderBatchActive();
        const ExiPassStats st = effectsDraw(view);
        EndTextureMode();
        if (capture) {
            drawnTotal += st.vertices;
            Image img = LoadImageFromTexture(rt.texture);
            ImageFlipVertical(&img);
            ImageResize(&img, W / 2, H / 2);
            ImageDraw(&sheet, img, Rectangle{0, 0, float(W / 2), float(H / 2)},
                      Rectangle{float(frameIdx * W / 2), 0, float(W / 2), float(H / 2)}, WHITE);
            UnloadImage(img);
            ++frameIdx;
        }
    }
    UnloadRenderTexture(rt);
    const bool ok = ExportImage(sheet, out.string().c_str());
    UnloadImage(sheet);
    std::printf("capture effect %3d (%s, tag %.0f): frames 3/8/15/26, %d vertices drawn, radius %.0f -> %s %s\n", c.id,
                c.what, double(c.tag), drawnTotal, double(radius), out.string().c_str(), ok ? "OK" : "FAILED");
    mgr.killAll();
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    int ticks = 120;
    bool list = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quiet") == 0) list = false;
        else ticks = std::max(1, std::atoi(argv[i]));
    }
    const auto exiPath = baroque::platform::assetRoot() / "DATA" / "EXI" / "BQ.EXI";
    std::ifstream f(exiPath, std::ios::binary);
    std::vector<uint8_t> exi((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (exi.empty()) {
        std::printf("FAIL: cannot read %s\n", exiPath.string().c_str());
        return 1;
    }
    {
        EffectManager probe;
        if (!probe.load(exi)) {
            std::printf("FAIL: BQ.EXI did not load\n");
            return 1;
        }
        const auto& fl = probe.file();
        std::printf("BQ.EXI: %zu EFCS, %zu SEQS, %zu EMTS, %zu OBJS, %zu RSQS, %zu KFMS keys, %zu VTXS\n",
                    fl.effects.size(), fl.sequences.size(), fl.emitters.size(), fl.objects.size(), fl.rsqs.size(),
                    fl.keyframes.size(), fl.vertices.size());
    }
    int withParticles = 0, nonEmpty = 0, withParticles2 = 0, nonEmpty2 = 0;
    const uint64_t h1 = sweep(exi, ticks, list, &withParticles, &nonEmpty);
    const uint64_t h2 = sweep(exi, ticks, false, &withParticles2, &nonEmpty2);
    std::printf("sweep: %d ticks, %d effects emitted particles, %d drew vertices, hash %016llx / rerun %016llx %s\n",
                ticks, withParticles, nonEmpty, static_cast<unsigned long long>(h1), static_cast<unsigned long long>(h2),
                h1 == h2 ? "(deterministic)" : "(MISMATCH)");
    if (h1 != h2) return 1;
    if (!replayCheck(exi)) {
        std::printf("FAIL\n");
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 480, "Baroque effects check");
    if (!effects().init() || !effectsRenderer().init()) {
        std::printf("FAIL: effects init\n");
        CloseWindow();
        return 1;
    }
    const fs::path outDir = fs::path(BAROQUE_WORK_ROOT) / "native_captures";
    fs::create_directories(outDir);
    const Capture caps[] = {
        {5, 30.0f, "hit spark FUN_0014bc30"},
        {0x16, 30.0f, "hit FUN_0014bc30"},
        {2, -30.0f, "hit FUN_0014b840 id 2"},
        {223, 0.0f, "C00A AT05_S3 Exi_b"},
        {16, 0.0f, "C00A AT01_G1 Exi_b"},
        {0x14, 0.0f, "item FUN_001c41e0"},
    };
    bool ok = true;
    for (const Capture& c : caps) {
        char name[64];
        std::snprintf(name, sizeof(name), "effects_%03d.png", c.id);
        ok = renderEffect(exi, c, outDir / name) && ok;
    }
    effectsRenderer().shutdown();
    CloseWindow();
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
