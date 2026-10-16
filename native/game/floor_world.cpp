// Floor world: render models and collision pieces of a generated floor (see floor_world.hpp).
#include "game/floor_world.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace baroque::game {

using alchemy::Mat4;

namespace {

// Rotation about +Y in the row-vector convention (v' = v * M).
// UNVERIFIED: sign convention of PieceInstance::rotY as consumed by the original piece transform.
Mat4 rotationY(float rad) {
    const float c = std::cos(rad), s = std::sin(rad);
    Mat4 m = Mat4::identity();
    m.m[0] = c;
    m.m[2] = -s;
    m.m[8] = s;
    m.m[10] = c;
    return m;
}

}  // namespace

namespace {
bool debugBlend = [] {
    char* v = nullptr;
    size_t n = 0;
    const bool on = _dupenv_s(&v, &n, "BAROQUE_DEBUG_BLEND") == 0 && v != nullptr;
    std::free(v);
    return on;
}();
}  // namespace

const alchemy::SceneModel* FloorWorld::loadModel(const std::string& dataPath, const data::Archive& archive,
                                                  const alchemy::TextureLibrary* extraLib) {
    auto it = models_.find(dataPath);
    if (it != models_.end()) return it->second.model.get();
    Loaded l;
    const auto path = archive.resolve(dataPath);
    if (!path) {
        missing.push_back(dataPath);
        models_.emplace(dataPath, Loaded{});
        return nullptr;
    }
    l.file = std::make_unique<alchemy::IgbFile>();
    std::string err;
    if (!l.file->load(*path, &err)) {
        missing.push_back(dataPath + " (" + err + ")");
        models_.emplace(dataPath, Loaded{});
        return nullptr;
    }
    l.model = std::make_unique<alchemy::SceneModel>();
    std::vector<const alchemy::TextureLibrary*> libs;
    if (extraLib != nullptr) libs.push_back(extraLib);
    if (texLib_) libs.push_back(texLib_.get());
    l.model->build(*l.file, libs, nullptr);
    if (debugBlend)
        for (const auto& m : l.model->meshes())
            std::printf("  %s mesh tex=%s blend=%d (%d,%d) depthWrite=%d alphaTest=%d func=%d ref=%.2f tris=%zu\n", dataPath.c_str(),
                        m.state.textureName.c_str(), int(m.state.blendEnabled), m.state.blendSrc, m.state.blendDst,
                        int(m.state.depthWrite), int(m.state.alphaTestEnabled), m.state.alphaFunc, double(m.state.alphaRef),
                        m.indices.size() / 3);
    const alchemy::SceneModel* m = l.model.get();
    models_.emplace(dataPath, std::move(l));
    return m;
}

bool FloorWorld::build(const Dungeon& d, const data::Archive& archive, bool gpu, std::string* err) {
    (void)err;
    instances_.clear();
    meshes_.clear();
    scene_.pieces.clear();
    missing.clear();

    // ---- collision (FUN_00127210 / FUN_00127510)
    if (!isGeneratedFloor(d.floorId())) {
        if (d.roomCount() > 0 && d.room(0).cls) {
            meshes_.push_back(std::make_unique<CollisionMesh>(CollisionMesh::fromCls(*d.room(0).cls)));
            scene_.pieces.push_back(CollisionPiece{meshes_.back().get(), meshes_.back().get(), {0, 0, 0}, false});
        }
    } else {
        for (int i = 0; i < d.roomCount(); ++i) {
            const Room& r = d.room(i);
            if (!r.placed() || !r.cls) continue;
            meshes_.push_back(std::make_unique<CollisionMesh>(CollisionMesh::fromCls(*r.cls)));
            scene_.pieces.push_back(CollisionPiece{meshes_.back().get(), meshes_.back().get(), {r.originX(), 0.0f, r.originZ()}, false});
        }
        std::map<int, CollisionMesh*> mt;
        for (const PieceInstance& p : d.pieces()) {
            const int id = p.id & 0xff;
            if (id >= 19 || !d.mtCls(id)) continue;
            CollisionMesh*& m = mt[id];
            if (m == nullptr) {
                meshes_.push_back(std::make_unique<CollisionMesh>(CollisionMesh::fromCls(*d.mtCls(id))));
                m = meshes_.back().get();
            }
            // UNVERIFIED: pieces sharing a mesh share its query stamp/record; the original has one object per id too
            scene_.pieces.push_back(CollisionPiece{m, m, {p.x, p.y, p.z}, true});
        }
    }
    if (!gpu) return true;

    // ---- render
    texFile_ = std::make_unique<alchemy::IgbFile>();
    texLib_ = std::make_unique<alchemy::TextureLibrary>();
    if (const auto tp = archive.resolve(d.texturePath())) {
        std::string e;
        if (texFile_->load(*tp, &e)) texLib_->addFile(*texFile_, nullptr);
        else missing.push_back(d.texturePath() + " (" + e + ")");
    } else {
        missing.push_back(d.texturePath());
    }
    // FUN_0011fe00: the model of every placed room (room flags bit0); FUN_0011e520 puts it at the room origin on
    // generated floors and at the world origin otherwise (town / event / 99 maps)
    for (int i = 0; i < d.roomCount(); ++i) {
        const Room& r = d.room(i);
        if (!r.placed() || r.modelPath.empty()) continue;
        const alchemy::SceneModel* m = loadModel(r.modelPath, archive);
        if (m == nullptr) continue;
        const float ox = isGeneratedFloor(d.floorId()) ? r.originX() : 0.0f;
        const float oz = isGeneratedFloor(d.floorId()) ? r.originZ() : 0.0f;
        instances_.push_back(Instance{m, Mat4::translation(ox, 0.0f, oz)});
    }
    for (const PieceInstance& p : d.pieces()) {
        if (p.modelPath.empty()) continue;
        const alchemy::SceneModel* m = loadModel(p.modelPath, archive);
        if (m == nullptr) continue;
        instances_.push_back(Instance{m, alchemy::mul(rotationY(p.rotY), Mat4::translation(p.x, p.y, p.z))});
    }
    // FUN_00120f70 (work/decompiled/00120f70.c) map-model spawns J024 / J025 / J026: FUN_0011e2b0(path, pack, pos)
    // loads the IGB and adds a transform node (translation = spawn position, FUN_0039d980) under the scene root
    // 0x4e7900. J024 and J026 first load the texture set of their map (FUN_0011dfe0(0x36) / FUN_0011dfe0(0x35)
    // into the pack named by the tag); J025 has none (it relies on the floor's own textures).
    for (const Placement& pl : d.placements()) {
        if (pl.kind != Placement::Kind::ExtraModel || pl.note.empty()) continue;
        const alchemy::TextureLibrary* extra = nullptr;
        const char* texPath = nullptr;
        if (pl.tag == (uint32_t('J') | uint32_t('0') << 8 | uint32_t('2') << 16 | uint32_t('4') << 24))
            texPath = "DATA/MAP/M54/M54_TEX.IGB";   // FUN_0011dfe0(0x36)
        else if (pl.tag == (uint32_t('J') | uint32_t('0') << 8 | uint32_t('2') << 16 | uint32_t('6') << 24))
            texPath = "DATA/MAP/M53/M53_TEX.IGB";   // FUN_0011dfe0(0x35)
        if (texPath != nullptr) {
            ExtraTex& et = extraTex_[texPath];
            if (!et.lib) {
                et.file = std::make_unique<alchemy::IgbFile>();
                et.lib = std::make_unique<alchemy::TextureLibrary>();
                std::string e;
                if (const auto tp = archive.resolve(texPath); tp && et.file->load(*tp, &e)) et.lib->addFile(*et.file, nullptr);
                else missing.push_back(std::string(texPath) + (e.empty() ? "" : " (" + e + ")"));
            }
            extra = et.lib.get();
        }
        const alchemy::SceneModel* m = loadModel(pl.note, archive, extra);
        if (m == nullptr) continue;
        instances_.push_back(Instance{m, Mat4::translation(pl.x, pl.y, pl.z)});
    }
    return true;
}

// orig FUN_001b8440 (work/decompiled/001b8440.c) over the floor's models: name "Toggle" (0x462eb8) igSwitch ->
// _childMask = child 0 (param 0) or child 1 (param != 0); recursion over every child list
void FloorWorld::setToggle(int child) {
    for (auto& [path, l] : models_)
        if (l.model) l.model->setSwitchChild("Toggle", child);
}

void FloorWorld::draw(alchemy::SceneRenderer& r, int pass) const {
    int index = 0;
    for (const Instance& i : instances_) {
        r.setCutInstance(index++);   // PORT: see-through dither lookup (instance order = forEachInstance)
        if (pass == 0) r.drawOpaque(*i.model, i.world);
        else if (pass == 1) r.drawBlended(*i.model, i.world);
        else r.draw(*i.model, i.world);
    }
    r.setCutInstance(-1);
}

}  // namespace baroque::game
