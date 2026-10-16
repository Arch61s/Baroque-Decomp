// Baroque-AlchemyCheck: (a) load all .IGB files and compare class counts with work/reports/igb_classes.json,
// (b) render reference models to work/native_captures/*.png, (c) print per-model statistics.
//
// Options: --no-corpus, --no-render, --color-scale=<f> (vertex RGB scale in VU units, default 128/255 = engine default).
#include "alchemy/igb.hpp"
#include "alchemy/scene_render.hpp"
#include "alchemy/igb_views.hpp"
#include "alchemy/texture.hpp"
#include "platform/platform.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace baroque;
using namespace baroque::alchemy;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Minimal extraction from igb_classes.json: top-level "files"/"parsed" and the "class_object_counts" map.
struct Expected {
    int files = -1, parsed = -1;
    std::map<std::string, int> classCounts;
    bool ok = false;
};

Expected readExpected(const fs::path& json) {
    Expected e;
    std::ifstream in(json);
    if (!in) return e;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto intAfter = [&](const char* key) -> int {
        const size_t p = text.find(key);
        if (p == std::string::npos) return -1;
        const size_t c = text.find(':', p);
        if (c == std::string::npos) return -1;
        return std::atoi(text.c_str() + c + 1);
    };
    e.files = intAfter("\"files\"");
    e.parsed = intAfter("\"parsed\"");
    size_t p = text.find("\"class_object_counts\"");
    if (p == std::string::npos) return e;
    p = text.find('{', p);
    if (p == std::string::npos) return e;
    const size_t end = text.find('}', p);
    size_t q = p;
    while (true) {
        const size_t k0 = text.find('"', q);
        if (k0 == std::string::npos || k0 > end) break;
        const size_t k1 = text.find('"', k0 + 1);
        if (k1 == std::string::npos || k1 > end) break;
        const size_t c = text.find(':', k1);
        if (c == std::string::npos || c > end) break;
        e.classCounts[text.substr(k0 + 1, k1 - k0 - 1)] = std::atoi(text.c_str() + c + 1);
        q = text.find_first_of(",}", c);
        if (q == std::string::npos || text[q] == '}') break;
    }
    e.ok = !e.classCounts.empty();
    return e;
}

bool corpusCheck(const fs::path& assetRoot, const fs::path& jsonPath) {
    std::vector<fs::path> files;
    for (const auto& ent : fs::recursive_directory_iterator(assetRoot))
        if (ent.is_regular_file() && lower(ent.path().extension().string()) == ".igb") files.push_back(ent.path());
    std::sort(files.begin(), files.end());
    std::map<std::string, int> counts;
    int parsed = 0;
    std::vector<std::string> errors;
    const auto t0 = std::chrono::steady_clock::now();
    for (const fs::path& p : files) {
        IgbFile f;
        std::string err;
        if (!f.load(p, &err)) {
            errors.push_back(p.string() + ": " + err);
            continue;
        }
        ++parsed;
        for (const auto& [cls, n] : f.classCounts()) counts[cls] += n;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[corpus] %zu IGB files under %s, %d parsed, %zu errors, %.2fs\n", files.size(), assetRoot.string().c_str(), parsed, errors.size(), secs);
    for (size_t i = 0; i < errors.size() && i < 20; ++i) std::printf("  ERR %s\n", errors[i].c_str());
    const Expected e = readExpected(jsonPath);
    if (!e.ok) {
        std::printf("[corpus] FAIL: could not read %s\n", jsonPath.string().c_str());
        return false;
    }
    bool ok = errors.empty() && int(files.size()) == e.files && parsed == e.parsed;
    if (!ok) std::printf("  file count: got %zu/%d, expected %d/%d\n", files.size(), parsed, e.files, e.parsed);
    int total = 0;
    for (const auto& [cls, n] : e.classCounts) {
        auto it = counts.find(cls);
        const int got = it == counts.end() ? 0 : it->second;
        total += got;
        if (got != n) {
            std::printf("  MISMATCH %-40s expected %7d got %7d\n", cls.c_str(), n, got);
            ok = false;
        }
    }
    for (const auto& [cls, n] : counts)
        if (e.classCounts.count(cls) == 0) {
            std::printf("  UNEXPECTED class %s (%d objects)\n", cls.c_str(), n);
            ok = false;
        }
    std::printf("[corpus] %zu classes, %d objects: %s\n", e.classCounts.size(), total, ok ? "PASS" : "FAIL");
    return ok;
}

struct Part {
    const char* path;
    const char* attachBone;  // nullptr: model space; otherwise placed at this bone of the first part's skeleton
};

struct Capture {
    const char* label;
    std::vector<Part> parts;
    const char* texturePath;  // shared texture file (igTextureInfo), may be nullptr
    const char* pngName;
    float camDir[3];          // direction from the target to the eye (outside views)
    float distanceScale;
    bool inside;              // camera inside the bounds looking across (rooms)
};

struct LoadedPart {
    IgbFile file;
    std::unique_ptr<TextureLibrary> lib;
    std::unique_ptr<SceneModel> model;
    Mat4 placement = Mat4::identity();
};

bool renderCapture(const Capture& cap, const fs::path& assetRoot, const fs::path& outDir, SceneRenderer& renderer, RenderTexture2D& rt) {
    std::string err;
    IgbFile texFile;
    std::unique_ptr<TextureLibrary> sharedLib;
    if (cap.texturePath != nullptr) {
        if (!texFile.load(assetRoot / cap.texturePath, &err)) {
            std::printf("[render] %s: texture file load failed: %s\n", cap.label, err.c_str());
            return false;
        }
        sharedLib = std::make_unique<TextureLibrary>();
        sharedLib->addFile(texFile, nullptr);
    }
    std::vector<std::unique_ptr<LoadedPart>> parts;
    for (const Part& p : cap.parts) {
        auto lp = std::make_unique<LoadedPart>();
        if (!lp->file.load(assetRoot / p.path, &err)) {
            std::printf("[render] %s: load failed: %s\n", p.path, err.c_str());
            return false;
        }
        lp->lib = std::make_unique<TextureLibrary>();
        std::vector<std::string> log;
        lp->lib->addFile(lp->file, &log);
        std::vector<const TextureLibrary*> libs{lp->lib.get()};
        if (sharedLib) libs.push_back(sharedLib.get());
        lp->model = std::make_unique<SceneModel>();
        lp->model->build(lp->file, libs, &log);
        for (const std::string& l : log) std::printf("  note: %s: %s\n", p.path, l.c_str());
        if (p.attachBone != nullptr && !parts.empty()) {
            // check-only convenience: place a part file at a bone of the base model's bind pose
            const Skeleton& sk = parts.front()->model->skeleton();
            for (size_t b = 0; b < sk.bones.size(); ++b)
                if (sk.bones[b].name == p.attachBone) lp->placement = sk.bindWorld[b];
        }
        parts.push_back(std::move(lp));
    }

    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
    bool any = false;
    for (const auto& lp : parts) {
        float pmin[3], pmax[3];
        if (!lp->model->bounds(pmin, pmax)) continue;
        float cmin[3], cmax[3];
        transformPoint(lp->placement, pmin, cmin);
        transformPoint(lp->placement, pmax, cmax);
        for (int c = 0; c < 3; ++c) {
            const float lo = std::min(cmin[c], cmax[c]), hi = std::max(cmin[c], cmax[c]);
            if (!any) { bmin[c] = lo; bmax[c] = hi; } else { bmin[c] = std::min(bmin[c], lo); bmax[c] = std::max(bmax[c], hi); }
        }
        any = true;
    }
    if (!any) {
        std::printf("[render] %s: no geometry\n", cap.label);
        return false;
    }
    const float center[3] = {(bmin[0] + bmax[0]) * 0.5f, (bmin[1] + bmax[1]) * 0.5f, (bmin[2] + bmax[2]) * 0.5f};
    const float ext[3] = {bmax[0] - bmin[0], bmax[1] - bmin[1], bmax[2] - bmin[2]};
    const float radius = std::max(1.0f, 0.5f * std::sqrt(ext[0] * ext[0] + ext[1] * ext[1] + ext[2] * ext[2]));
    float eye[3], target[3], fov = 45.0f, zn, zf;
    if (cap.inside) {
        eye[0] = center[0] - 0.38f * ext[0];
        eye[1] = bmin[1] + 0.38f * ext[1];
        eye[2] = center[2] - 0.38f * ext[2];
        target[0] = center[0] + 0.3f * ext[0];
        target[1] = bmin[1] + 0.25f * ext[1];
        target[2] = center[2] + 0.3f * ext[2];
        fov = 60.0f;
        zn = std::max(0.05f, radius * 0.005f);
        zf = radius * 6.0f;
    } else {
        float dir[3] = {cap.camDir[0], cap.camDir[1], cap.camDir[2]};
        const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (float& c : dir) c /= dl;
        const float dist = radius * cap.distanceScale;
        for (int c = 0; c < 3; ++c) { eye[c] = center[c] + dir[c] * dist; target[c] = center[c]; }
        zn = std::max(0.05f, dist * 0.01f);
        zf = dist + radius * 4.0f;
    }
    const float up[3] = {0.0f, 1.0f, 0.0f};  // igSceneInfo._upVector is (0,1,0) in every file
    const Mat4 view = Mat4::lookAt(eye, target, up);
    const Mat4 proj = Mat4::perspective(fov, float(rt.texture.width) / float(rt.texture.height), zn, zf);

    BeginTextureMode(rt);
    ClearBackground(Color{48, 48, 56, 255});
    renderer.begin(view, proj);
    for (const auto& lp : parts) renderer.draw(*lp->model, lp->placement);
    renderer.end();
    EndTextureMode();

    Image img = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&img);
    const fs::path out = outDir / cap.pngName;
    const bool saved = ExportImage(img, out.string().c_str());
    UnloadImage(img);

    std::printf("[render] %s -> %s%s\n", cap.label, out.string().c_str(), saved ? "" : " (WRITE FAILED)");
    for (size_t i = 0; i < parts.size(); ++i) {
        const SceneModel::Stats st = parts[i]->model->stats();
        std::printf("    %-28s %2d meshes %6d vertices %6d triangles %2d textures bound (%zu loaded) %zu missing%s\n",
                    cap.parts[i].path, st.meshes, st.vertices, st.triangles, st.texturesBound, parts[i]->lib->count(), st.missingTextures.size(),
                    st.missingTextures.empty() ? "" : ":");
        for (const std::string& m : st.missingTextures) std::printf("      missing texture %s\n", m.c_str());
        const Skeleton& sk = parts[i]->model->skeleton();
        if (sk.valid) std::printf("      skeleton '%s': %zu bones, %zu joint matrices\n", sk.name.c_str(), sk.bones.size(), sk.invBind.size());
        for (const IgObject* dbo : parts[i]->file.find("igAnimationDatabase")) {
            const views::AnimationDatabase db(dbo);
            const auto anims = db.animations();
            size_t tracks = 0;
            for (const IgObject* a : anims) tracks += views::Animation(a).tracks().size();
            std::printf("      animation database: %zu animations, %zu tracks, %zu skins\n", anims.size(), tracks, db.skins().size());
        }
    }
    std::printf("    bounds [%.1f %.1f %.1f] .. [%.1f %.1f %.1f]\n", bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2]);
    return saved;
}

}  // namespace

int main(int argc, char** argv) {
    bool doCorpus = true, doRender = true;
    float colorScale = 128.0f / 255.0f;  // FUN_003832e0 default
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-corpus") == 0) doCorpus = false;
        if (std::strcmp(argv[i], "--no-render") == 0) doRender = false;
        if (std::strncmp(argv[i], "--color-scale=", 14) == 0) colorScale = float(std::atof(argv[i] + 14));
    }
    const fs::path assetRoot = platform::assetRoot();
    const fs::path workRoot = fs::path(BAROQUE_WORK_ROOT);
    const fs::path outDir = workRoot / "native_captures";
    std::error_code ec;
    fs::create_directories(outDir, ec);

    bool ok = true;
    if (doCorpus) ok = corpusCheck(assetRoot, workRoot / "reports" / "igb_classes.json") && ok;

    if (doRender) {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(1920, 1080, "Baroque alchemy check");
        RenderTexture2D rt = LoadRenderTexture(1920, 1080);
        SceneRenderer renderer;
        if (!renderer.init()) {
            std::printf("[render] shader compile failed\n");
            ok = false;
        } else {
            renderer.vertexColorScale = {colorScale, colorScale, colorScale, 1.0f};
            const std::vector<Capture> captures = {
                {"map (outside)", {{"DATA/MAP/M00/MR00_00.IGB", nullptr}}, "DATA/MAP/M00/M00_TEX.IGB", "MR00_00.png", {0.75f, 0.9f, 1.0f}, 1.6f, false},
                {"map (inside)", {{"DATA/MAP/M00/MR00_00.IGB", nullptr}}, "DATA/MAP/M00/M00_TEX.IGB", "MR00_00_inside.png", {0, 0, 1}, 1.0f, true},
                {"actor C00A", {{"DATA/CHR/C00/C00A.IGB", nullptr}}, nullptr, "C00A.png", {0.45f, 0.25f, 1.0f}, 2.3f, false},
                {"actor C00A assembled",
                 {{"DATA/CHR/C00/C00A.IGB", nullptr}, {"DATA/CHR/C00/C00A_BODY.IGB", nullptr}, {"DATA/CHR/C00/C00A_HAIR.IGB", nullptr},
                  {"DATA/CHR/C00/C00A_HEAD.IGB", "Head"}},
                 nullptr, "C00A_assembled.png", {0.45f, 0.25f, 1.0f}, 2.3f, false},
                {"object OBJ002", {{"DATA/OBJ/OBJ002.IGB", nullptr}}, nullptr, "OBJ002.png", {0.6f, 0.5f, 1.0f}, 2.3f, false},
            };
            for (const Capture& c : captures) ok = renderCapture(c, assetRoot, outDir, renderer, rt) && ok;
            renderer.shutdown();
        }
        UnloadRenderTexture(rt);
        CloseWindow();
    }
    std::printf("alchemy check: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
