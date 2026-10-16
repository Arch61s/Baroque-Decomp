// Baroque-AnimCheck: native Enbaya decoder / pose verification.
//   (default)          decode every igAnimation in every .IGB to the end (structural check: no out-of-range
//                      reads, every section consumed exactly), then render posed contact sheets of C00A.
//   --dump OUT FILE..  write the same text dump as tools/enbaya_dump.py (bit-exact comparison).
//   (default)          also the igAnimationCombiner blend check (FUN_0010a8e0 sequence on C00A WT01 -> MV01).
//   --slerp-dump OUT   FUN_0039fe10 over a test grid (compare with tools/enbaya.py quat_slerp).
//   --no-corpus, --no-render, --no-blend
#include "alchemy/animation.hpp"
#include "alchemy/enbaya.hpp"
#include "alchemy/igb.hpp"
#include "alchemy/scene_render.hpp"
#include "alchemy/ps2_float.hpp"
#include "alchemy/texture.hpp"
#include "platform/platform.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#pragma fenv_access(on)

namespace fs = std::filesystem;
using namespace baroque;
using namespace baroque::alchemy;

namespace {

constexpr int64_t kFrameNs = enbaya::kFrameNs;

void hx(FILE* o, float v) { std::fprintf(o, " %08x", std::bit_cast<uint32_t>(v)); }

std::vector<int64_t> sampleTimes(int64_t durationNs) {
    const int64_t n = durationNs / kFrameNs + 1;
    return {0, kFrameNs / 2, 3 * kFrameNs + 12345, (n / 2) * kFrameNs + 777777, kFrameNs, 2 * kFrameNs / 3,
            (n - 1) * kFrameNs, durationNs + 5 * kFrameNs + 1, 7 * kFrameNs / 3, 0, n * kFrameNs * 3 + 999};
}

// First track's igEnbayaAnimationSource of an igAnimation (tools/enbaya.py EnbayaSource.from_object).
std::optional<enbaya::Source> firstSource(const IgbFile& f, const IgObject* a, std::string* err) {
    const auto tracks = f.listObjects(f.object(a->getRef("_trackList")));
    if (tracks.empty()) { *err = "no tracks"; return std::nullopt; }
    const IgObject* ts = f.object(tracks[0]->getRef("_source"));
    if (ts == nullptr || !ts->isA("igEnbayaTransformSource")) { *err = "EnbayaError"; return std::nullopt; }
    const IgObject* es = f.object(ts->getRef("_enbayaAnimationSource"));
    if (es == nullptr) { *err = "EnbayaError"; return std::nullopt; }
    const IgMemory* m = f.memory(es->getRef("_enbayaAnimationStream"));
    if (m == nullptr) { *err = "EnbayaError"; return std::nullopt; }
    std::array<int, 3> interp{1, 3, 1};
    interp = interpolationMethod(es);
    return enbaya::Source::create(m->data, es->getInt("_playMode", 0), interp, err);
}

bool dump(const fs::path& out, const std::vector<fs::path>& files) {
    FILE* o = nullptr;
    if (fopen_s(&o, out.string().c_str(), "wb") != 0 || o == nullptr) return false;
    for (const fs::path& p : files) {
        IgbFile f;
        std::string err;
        if (!f.load(p, &err)) { std::printf("load %s: %s\n", p.string().c_str(), err.c_str()); return false; }
        const std::string base = p.filename().string();
        int ai = 0;
        for (const IgObject* a : f.find("igAnimation")) {
            auto src = firstSource(f, a, &err);
            if (!src) { std::fprintf(o, "X %s %d %s\n", base.c_str(), ai++, "EnbayaError"); continue; }
            const int nb = src->header().boneCount;
            // decode_all
            std::vector<std::vector<std::array<float, 7>>> frames;
            {
                enbaya::Source& s = *src;
                ps2::Ps2FloatScope fp;
                enbaya::Decoder& d = s.decoder();
                d.reset();
                auto snap = [&] {
                    std::vector<std::array<float, 7>> fr;
                    for (const auto& b : d.bones()) {
                        const auto& q = b.q[size_t(d.cur())];
                        const auto& pp = b.p[size_t(d.cur())];
                        fr.push_back({q[0], q[1], q[2], q[3], pp[0], pp[1], pp[2]});
                    }
                    frames.push_back(std::move(fr));
                };
                snap();
                while (d.stepForward()) snap();
            }
            std::fprintf(o, "A %s %d %s %d %zu\n", base.c_str(), ai, std::string(a->getString("_name")).c_str(), nb, frames.size());
            for (size_t k = 0; k < frames.size(); ++k)
                for (size_t b = 0; b < frames[k].size(); ++b) {
                    std::fprintf(o, "F %zu %zu", k, b);
                    for (float v : frames[k][b]) hx(o, v);
                    std::fprintf(o, "\n");
                }
            auto src2 = firstSource(f, a, &err);
            std::vector<int> bones{0, 1, nb / 2, nb - 1};
            std::sort(bones.begin(), bones.end());
            bones.erase(std::unique(bones.begin(), bones.end()), bones.end());
            for (int64_t t : sampleTimes(a->getLong("_duration"))) {
                for (int b : bones) {
                    enbaya::Quat q;
                    enbaya::Vec3 tr;
                    bool hr = false, ht = false;
                    src2->evaluate(t, b, q, hr, tr, ht);
                    std::fprintf(o, "S %lld %d", static_cast<long long>(t), b);
                    if (hr) { hx(o, q.x); hx(o, q.y); hx(o, q.z); hx(o, q.w); } else std::fprintf(o, " - - - -");
                    if (ht) { hx(o, tr.x); hx(o, tr.y); hx(o, tr.z); } else std::fprintf(o, " - - -");
                    std::fprintf(o, "\n");
                }
            }
            ++ai;
        }
    }
    std::fclose(o);
    return true;
}

// --slerp-dump OUT: FUN_0039fe10 (AnimCombiner's quaternion blend) over a deterministic grid, one line per case
// "S <t> <a.xyzw> <b.xyzw> -> <out.xyzw>" in hex, for comparison with tools/enbaya.py quat_slerp.
bool slerpDump(const fs::path& out) {
    FILE* o = nullptr;
    if (fopen_s(&o, out.string().c_str(), "wb") != 0 || o == nullptr) return false;
    uint32_t seed = 0x12345678u;
    auto rnd = [&] {   // game LCG (rng.hpp constants), only to spread the test inputs
        seed = seed * 0x19660du + 0x3c6ef35fu;
        return float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f;
    };
    auto unit = [&] {
        std::array<float, 4> q{rnd(), rnd(), rnd(), rnd()};
        const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        for (float& x : q) x /= l;
        return q;
    };
    const float ws[] = {0.0f, 0.03333333f, 0.1666666f, 0.25f, 0.5f, 0.75f, 0.8333333f, 0.9999999f, 1.0f};
    for (int i = 0; i < 400; ++i) {
        const auto a = unit();
        auto b = unit();
        if (i % 4 == 1)   // nearly equal pair: the linear branch (1 - dot <= 0.08)
            for (size_t k = 0; k < 4; ++k) b[k] = a[k] + rnd() * 0.01f;
        for (float w : ws) {
            const auto r = quatSlerpPoly(w, a, b);
            std::fprintf(o, "S");
            hx(o, w);
            for (float v : a) hx(o, v);
            for (float v : b) hx(o, v);
            for (float v : r) hx(o, v);
            std::fprintf(o, "\n");
        }
    }
    std::fclose(o);
    return true;
}

// Combiner check: the game's FUN_0010a7f0 / FUN_0010a8e0 call sequence on C00A, then the FUN_003c48c0 ratio ramp,
// the FUN_003c5550 per-bone blend (recomputed independently from both clips) and the FUN_003c47e0 retirement.
// C00A.ACT: WT01_H1 starts with AnmChgEx("M005", 0.3, 0, 1.0), MV01_H1 with AnmChgEx("M025", 0.3, 0, 1.0).
constexpr const char* kBlendFrom = "M005";
constexpr const char* kBlendTo = "M025";
constexpr float kBlendSec = 0.3f;

bool blendCheck(const fs::path& assetRoot) {
    IgbFile f;
    std::string err;
    if (!f.load(assetRoot / "DATA/CHR/C00/C00A.IGB", &err)) { std::printf("[blend] load: %s\n", err.c_str()); return false; }
    const Skeleton sk = loadSkeletonBones(f, primarySkeleton(f));
    auto clips = loadClips(f, primarySkeleton(f), nullptr);
    auto find = [&](const char* n) {
        for (size_t i = 0; i < clips.size(); ++i)
            if (clips[i]->name == n) return int(i);
        return -1;
    };
    // the clips the player's idle (WT01_H1) and run (MV01_H1) actions select
    const int A = find(kBlendFrom), B = find(kBlendTo);
    if (A < 0 || B < 0 || sk.bones.empty()) {
        std::printf("[blend] C00A %s/%s or skeleton missing; clips:", kBlendFrom, kBlendTo);
        for (const auto& c : clips) std::printf(" %s", c->name.c_str());
        std::printf("\n");
        return false;
    }
    ps2::Ps2FloatScope fp;
    const int64_t F = kFrameNs;
    auto units = [](int64_t t, float s) { return int64_t(ps2::fmul(float(uint32_t(uint64_t(t) >> 18)), s)); };
    AnimCombiner c;
    c.init(&clips, &sk);
    bool ok = true;
    auto expect = [&](bool cond, const char* what) {
        if (!cond) std::printf("[blend] FAIL: %s\n", what);
        ok = ok && cond;
    };
    // FUN_0010a7f0 at T0
    int64_t now = 300 * F;
    c.add(A, 0, 4, 0, clips[size_t(A)]->priority);
    AnimState* sa = c.states()[0].get();
    sa->timeBias = units(now, sa->timeScale) * -0x40000;
    AnimCombiner::updateLocalTime(*sa, now);
    for (int i = 0; i < 10; ++i) {
        now += F;
        c.evaluate(now);
    }
    // FUN_0010a8e0(0.3, 1.0, actor, M025, 0)
    const int64_t blendNs = int64_t(ps2::fmul(1e+09f, kBlendSec));
    c.addWithTransition(0.0f, 1.0f, B, 1, 5, 2, clips[size_t(B)]->priority, blendNs, 0);
    AnimState* sb = c.states().back().get();
    sb->timeBias = units(now, sb->timeScale) * -0x40000;
    AnimCombiner::updateLocalTime(*sb, now);
    expect(c.states().size() == 2 && sb->status == 2 && sb->combineMode == 1 && sb->blendStartTime == 1,
           "state added pending (status 2, combine 1, start flag 1)");
    expect(blendNs == 300000000, "blend duration 0.3 s -> 300000000 ns");
    std::printf("[blend] C00A %s -> %s, blend %lld ns, %zu bones\n", clips[size_t(A)]->name.c_str(),
                clips[size_t(B)]->name.c_str(), static_cast<long long>(blendNs), sk.bones.size());
    int completedAt = -1;
    for (int k = 1; k <= 12 && ok; ++k) {
        now += F;
        // poses of both clips at their local times BEFORE the combiner update moves them, recomputed after
        const LocalPose& res = c.evaluate(now);
        const bool blending = c.states().size() == 2;
        if (k == 1) expect(sb->blendStartTime == now && sb->localTime == 0, "first update: blend starts now, B local time 0");
        float wExpect = 1.0f;
        if (!(now < sb->blendStartTime) && !(sb->blendStartTime + blendNs < now)) {
            wExpect = ps2::fdiv(floatdisf(now - sb->blendStartTime), floatdisf(blendNs));
            if (1.0f < wExpect) wExpect = 1.0f;
        }
        if (blending) {
            expect(sb->blendRatio == wExpect, "ratio = (now - start) / duration");
            LocalPose pa, pb;
            evaluateLocals(*clips[size_t(A)], sa->localTime, sk, pa);
            evaluateLocals(*clips[size_t(B)], sb->localTime, sk, pb);
            int bad = 0;
            for (size_t b = 0; b < sk.bones.size(); ++b) {
                if (c.boneEntryCount(int(b)) != 2) continue;
                const auto q = quatSlerpPoly(sb->blendRatio, pa.q[b], pb.q[b]);
                const float omw = ps2::fsub(1.0f, sb->blendRatio);
                for (size_t k2 = 0; k2 < 4; ++k2)
                    if (std::bit_cast<uint32_t>(q[k2]) != std::bit_cast<uint32_t>(res.q[b][k2])) ++bad;
                for (size_t k2 = 0; k2 < 3; ++k2) {
                    const float t = ps2::fmadd(ps2::fmul(sb->blendRatio, pb.t[b][k2]), pa.t[b][k2], omw);
                    if (std::bit_cast<uint32_t>(t) != std::bit_cast<uint32_t>(res.t[b][k2])) ++bad;
                }
            }
            expect(bad == 0, "per-bone blend = FUN_0039fe10(w, A, B) / w*B + A*(1-w), bit-exact");
            std::printf("[blend] frame %d: w %.7f (%08x)  B local %lld ns  A local %lld ns  bone mismatches %d\n", k,
                        double(sb->blendRatio), std::bit_cast<uint32_t>(sb->blendRatio),
                        static_cast<long long>(sb->localTime), static_cast<long long>(sa->localTime), bad);
        } else if (completedAt < 0) {
            completedAt = k;
            expect(c.states()[0].get() == sb && sb->combineMode == 0 && sb->blendRatio == 1.0f,
                   "on completion the old state is removed and B becomes the base");
            LocalPose pb;
            evaluateLocals(*clips[size_t(B)], sb->localTime, sk, pb);
            int bad = 0;
            for (size_t b = 0; b < sk.bones.size(); ++b)
                if (c.boneEntryCount(int(b)) == 1 && (pb.q[b] != res.q[b] || pb.t[b] != res.t[b])) ++bad;
            expect(bad == 0, "after completion the pose is B alone");
            std::printf("[blend] frame %d: blend complete, %zu state, pose = %s\n", k, c.states().size(),
                        clips[size_t(B)]->name.c_str());
        }
    }
    // 9 frames = 299999997 ns; __floatdisf rounds it to nearest (3e8), so the ratio reaches 1.0 on the 10th update
    expect(completedAt == 10, "0.3 s blend completes on the 10th combiner update (__floatdisf(9 * 33333333) = 3e8)");
    // FUN_0010a8e0 keeps at most four states: a fifth change first removes the oldest (FUN_003c6900)
    for (int i = 0; i < 5; ++i) {
        if (3 < c.states().size()) c.remove(c.states().front().get());
        c.addWithTransition(0.0f, 1.0f, i % 2 ? A : B, 1, 5, 2, 0, blendNs, 0);
    }
    expect(c.states().size() == 4, "state list capped at 4 by FUN_0010a8e0");
    int maxBase = 0;
    for (size_t b = 0; b < sk.bones.size(); ++b) maxBase = std::max(maxBase, c.boneBase(int(b)));
    expect(maxBase == 0, "no combine-mode-0 state left above index 0 -> base 0 (FUN_003c6900 recompute)");
    std::printf("[blend] %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

bool corpus(const fs::path& root) {
    const auto t0 = std::chrono::steady_clock::now();
    int files = 0, anims = 0, sources = 0, failed = 0, frames = 0;
    uint64_t oob = 0;
    int badCursor = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::toupper(static_cast<unsigned char>(c)));
        if (ext != ".IGB") continue;
        IgbFile f;
        std::string err;
        if (!f.load(e.path(), &err)) { ++failed; continue; }
        ++files;
        std::vector<int> done;
        for (const IgObject* es : f.find("igEnbayaAnimationSource")) {
            const IgMemory* m = f.memory(es->getRef("_enbayaAnimationStream"));
            if (m == nullptr) { ++failed; continue; }
            auto src = enbaya::Source::create(m->data, es->getInt("_playMode", 0), {1, 3, 1}, &err);
            if (!src) { ++failed; std::printf("  %s: %s\n", e.path().string().c_str(), err.c_str()); continue; }
            ++sources;
            ps2::Ps2FloatScope fp;
            enbaya::Decoder& d = src->decoder();
            d.reset();
            ++frames;
            while (d.stepForward()) ++frames;
            oob += d.outOfRangeReads();
            const auto rep = d.cursorReport();
            for (const auto& r : rep)
                if (r[0] != r[1]) { ++badCursor; break; }
        }
        anims += int(f.find("igAnimation").size());
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[enbaya] %d IGB files, %d igAnimation, %d sources decoded, %d frames, %d failed, %llu out-of-range reads, "
                "%d sources with unconsumed/overrun sections, %.2fs\n",
                files, anims, sources, frames, failed, static_cast<unsigned long long>(oob), badCursor, secs);
    return failed == 0;
}

struct Part {
    IgbFile file;
    std::unique_ptr<TextureLibrary> lib;
    std::unique_ptr<SceneModel> model;
};

bool renderSheet(const fs::path& assetRoot, const fs::path& outDir) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1920, 1080, "Baroque anim check");
    RenderTexture2D rt = LoadRenderTexture(1920, 1080);
    SceneRenderer renderer;
    if (!renderer.init()) return false;
    const char* paths[] = {"DATA/CHR/C00/C00A.IGB", "DATA/CHR/C00/C00A_BODY.IGB", "DATA/CHR/C00/C00A_HAIR.IGB", "DATA/CHR/C00/C00A_HEAD.IGB"};
    std::vector<std::unique_ptr<Part>> parts;
    for (const char* p : paths) {
        auto part = std::make_unique<Part>();
        std::string err;
        if (!part->file.load(assetRoot / p, &err)) { std::printf("load %s: %s\n", p, err.c_str()); return false; }
        part->lib = std::make_unique<TextureLibrary>();
        part->lib->addFile(part->file, nullptr);
        part->model = std::make_unique<SceneModel>();
        part->model->build(part->file, {part->lib.get()}, nullptr);
        parts.push_back(std::move(part));
    }
    const Skeleton& sk = parts[0]->model->skeleton();
    parts[1]->model->adoptSkeleton(sk);
    parts[2]->model->adoptSkeleton(sk);
    int headBone = -1;
    for (size_t b = 0; b < sk.bones.size(); ++b)
        if (sk.bones[b].name == "Head") headBone = int(b);
    std::vector<std::string> log;
    auto clips = loadClips(parts[0]->file, primarySkeleton(parts[0]->file), &log);
    for (const auto& l : log) std::printf("  note: %s\n", l.c_str());
    std::printf("[pose] C00A: %zu clips\n", clips.size());

    auto drawPose = [&](AnimClip& clip, int64_t t, int cx, int cy, int cw, int ch) {
        std::vector<Mat4> world;
        evaluatePose(clip, clip.startTimeNs + t, sk, world);
        parts[0]->model->setPose(world);
        parts[1]->model->setPose(world);
        parts[2]->model->setPose(world);
        // head part: its own skeleton in bind pose, placed at the animated Head bone (UNVERIFIED attachment)
        const Mat4 headPlace = headBone >= 0 ? world[size_t(headBone)] : Mat4::identity();
        const float eye[3] = {0.0f, 80.0f, 330.0f}, target[3] = {0.0f, 70.0f, 0.0f}, up[3] = {0, 1, 0};
        rlViewport(cx, cy, cw, ch);
        renderer.begin(Mat4::lookAt(eye, target, up), Mat4::perspective(45.0f, float(cw) / float(ch), 5.0f, 3000.0f));
        renderer.draw(*parts[0]->model, Mat4::identity());
        renderer.draw(*parts[1]->model, Mat4::identity());
        renderer.draw(*parts[2]->model, Mat4::identity());
        renderer.draw(*parts[3]->model, headPlace);
        renderer.end();
    };

    // Sheet 1: 12 clips at 40 % of their length. Sheet 2: one clip (the longest) over 12 frames.
    bool ok = true;
    for (int sheet = 0; sheet < 2; ++sheet) {
        BeginTextureMode(rt);
        ClearBackground(Color{48, 48, 56, 255});
        const int cols = 6, rows = 2, cw = 1920 / cols, ch = 1080 / rows;
        size_t longest = 0;
        for (size_t i = 0; i < clips.size(); ++i)
            if (clips[i]->durationNs > clips[longest]->durationNs) longest = i;
        std::vector<std::string> labels;
        for (int c = 0; c < cols * rows; ++c) {
            AnimClip* clip;
            int64_t t;
            if (sheet == 0) {
                if (size_t(c) >= clips.size()) break;
                clip = clips[size_t(c)].get();
                t = (clip->durationNs * 2 / 5) / kFrameNs * kFrameNs;
            } else {
                clip = clips[longest].get();
                t = clip->durationNs * c / (cols * rows) / kFrameNs * kFrameNs;
            }
            const int cx = (c % cols) * cw, cy = (rows - 1 - c / cols) * ch;  // GL viewport origin bottom-left
            drawPose(*clip, t, cx, cy, cw, ch);
            labels.push_back(clip->name + " f" + std::to_string(t / kFrameNs));
        }
        rlViewport(0, 0, 1920, 1080);
        for (size_t c = 0; c < labels.size(); ++c)
            DrawText(labels[c].c_str(), int(c % cols) * cw + 10, int(c / cols) * ch + 10, 24, RAYWHITE);
        EndTextureMode();
        Image img = LoadImageFromTexture(rt.texture);
        ImageFlipVertical(&img);
        const fs::path out = outDir / (sheet == 0 ? "anim_C00A_clips.png" : "anim_C00A_frames.png");
        ok = ExportImage(img, out.string().c_str()) && ok;
        UnloadImage(img);
        std::printf("[pose] -> %s\n", out.string().c_str());
    }
    parts.clear();
    renderer.shutdown();
    UnloadRenderTexture(rt);
    CloseWindow();
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path assetRoot = platform::assetRoot();
    if (argc >= 3 && std::strcmp(argv[1], "--dump") == 0) {
        std::vector<fs::path> files;
        for (int i = 3; i < argc; ++i) files.emplace_back(argv[i]);
        const bool ok = dump(argv[2], files);
        std::printf("dump: %s\n", ok ? "written" : "FAILED");
        return ok ? 0 : 1;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--selftest") == 0) {
        ps2::Ps2FloatScope fp;
        volatile float w = std::bit_cast<float>(0x3f7fffffu);
        const float acc = ps2::fmul(w, w);
        const float sq = ps2::fsqrt(acc);
        const float inv = ps2::fdiv(1.0f, sq);
        const float r = ps2::fmul(w, inv);
        std::printf("csr %08x acc %08x sqrt %08x inv %08x res %08x\n", _mm_getcsr(), std::bit_cast<uint32_t>(acc),
                    std::bit_cast<uint32_t>(sq), std::bit_cast<uint32_t>(inv), std::bit_cast<uint32_t>(r));
        return 0;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--slerp-dump") == 0) {
        const bool ok = slerpDump(argv[2]);
        std::printf("slerp dump: %s\n", ok ? "written" : "FAILED");
        return ok ? 0 : 1;
    }
    bool doCorpus = true, doRender = true, doBlend = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-corpus") == 0) doCorpus = false;
        if (std::strcmp(argv[i], "--no-render") == 0) doRender = false;
        if (std::strcmp(argv[i], "--no-blend") == 0) doBlend = false;
    }
    bool ok = true;
    if (doBlend) ok = blendCheck(assetRoot) && ok;
    if (doCorpus) ok = corpus(assetRoot) && ok;
    if (doRender) {
        const fs::path outDir = fs::path(BAROQUE_WORK_ROOT) / "native_captures";
        fs::create_directories(outDir);
        ok = renderSheet(assetRoot, outDir) && ok;
    }
    std::printf("anim check: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
