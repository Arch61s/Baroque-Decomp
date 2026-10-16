// Scene-graph traversal (tools/igb_geom.py collect_meshes) and GS-faithful drawing on rlgl.
#include "alchemy/scene_render.hpp"
#include "alchemy/transform_seq.hpp"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace baroque::alchemy {

// ---------------------------------------------------------------------------------------------
// Mat4
// ---------------------------------------------------------------------------------------------

Mat4 Mat4::identity() {
    Mat4 r;
    r.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return r;
}
Mat4 Mat4::translation(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}
Mat4 Mat4::fromFloats(const float* f) {
    Mat4 r;
    for (size_t i = 0; i < 16; ++i) r.m[i] = f[i];
    return r;
}
Mat4 Mat4::perspective(float fovyDeg, float aspect, float zn, float zf) {
    Mat4 r;
    r.m.fill(0.0f);
    const float f = 1.0f / std::tan(fovyDeg * 3.14159265358979f / 360.0f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * zf * zn / (zn - zf);
    return r;
}
Mat4 Mat4::lookAt(const float eye[3], const float target[3], const float up[3]) {
    float fwd[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float l = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (l < 1e-6f) l = 1.0f;
    for (float& c : fwd) c /= l;
    float right[3] = {fwd[1] * up[2] - fwd[2] * up[1], fwd[2] * up[0] - fwd[0] * up[2], fwd[0] * up[1] - fwd[1] * up[0]};
    l = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (l < 1e-6f) l = 1.0f;
    for (float& c : right) c /= l;
    const float u[3] = {right[1] * fwd[2] - right[2] * fwd[1], right[2] * fwd[0] - right[0] * fwd[2], right[0] * fwd[1] - right[1] * fwd[0]};
    // camera-to-world (rows: right, up, back, eye) then invert
    Mat4 cam = identity();
    cam.m[0] = right[0]; cam.m[1] = right[1]; cam.m[2] = right[2];
    cam.m[4] = u[0]; cam.m[5] = u[1]; cam.m[6] = u[2];
    cam.m[8] = -fwd[0]; cam.m[9] = -fwd[1]; cam.m[10] = -fwd[2];
    cam.m[12] = eye[0]; cam.m[13] = eye[1]; cam.m[14] = eye[2];
    Mat4 view;
    if (!invert(cam, view)) view = identity();
    return view;
}

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[size_t(i * 4 + k)] * b.m[size_t(k * 4 + j)];
            r.m[size_t(i * 4 + j)] = s;
        }
    return r;
}

bool invert(const Mat4& in, Mat4& out) {
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            a[i][j] = in.m[size_t(i * 4 + j)];
            a[i][4 + j] = (i == j) ? 1.0 : 0.0;
        }
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-12) return false;
        if (p != c)
            for (int j = 0; j < 8; ++j) std::swap(a[c][j], a[p][j]);
        const double f = a[c][c];
        for (int j = 0; j < 8; ++j) a[c][j] /= f;
        for (int r = 0; r < 4; ++r) {
            if (r == c || a[r][c] == 0.0) continue;
            const double g = a[r][c];
            for (int j = 0; j < 8; ++j) a[r][j] -= g * a[c][j];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out.m[size_t(i * 4 + j)] = float(a[i][4 + j]);
    return true;
}

void transformPoint(const Mat4& m, const float in[3], float out[3]) {
    const float x = in[0], y = in[1], z = in[2];
    out[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
    out[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
    out[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
}
void transformVector(const Mat4& m, const float in[3], float out[3]) {
    const float x = in[0], y = in[1], z = in[2];
    out[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z;
    out[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z;
    out[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z;
}

// raylib's rlSetUniformMatrix uploads {m0,m1,m2,m3, m4,...} column-major, so field m_i = our m[i] makes
// GL column j = our row j: the GL column-vector matrix is the transpose of our row-vector matrix.
Matrix toRaylib(const Mat4& r) {
    Matrix o;
    o.m0 = r.m[0]; o.m1 = r.m[1]; o.m2 = r.m[2]; o.m3 = r.m[3];
    o.m4 = r.m[4]; o.m5 = r.m[5]; o.m6 = r.m[6]; o.m7 = r.m[7];
    o.m8 = r.m[8]; o.m9 = r.m[9]; o.m10 = r.m[10]; o.m11 = r.m[11];
    o.m12 = r.m[12]; o.m13 = r.m[13]; o.m14 = r.m[14]; o.m15 = r.m[15];
    return o;
}

// ---------------------------------------------------------------------------------------------
// SceneModel
// ---------------------------------------------------------------------------------------------

struct SceneModel::Ctx {
    Mat4 world = Mat4::identity();
    Mat4 actorBase = Mat4::identity();
    RenderState state;
    std::vector<int> paletteToBm;   // from the nearest igBlendMatrixSelect
    bool inActor = false;
    std::vector<std::pair<int, int>> switchPath;   // igSwitch ancestors (slot, child index)
    std::vector<int> path;          // child indices from the scene root / skinned graph
    int geoXform = -1;              // innermost animated model-matrix igTransform
    Mat4 geoPost = Mat4::identity(), sinceXform = Mat4::identity();
    int selectBone = -1;            // innermost igModelViewMatrixBoneSelect (_matrixIndex)
    Mat4 selectBase = Mat4::identity();   // ctx.world right below it (bind pose of that bone)
};

namespace {

constexpr int kStreamPos = 0, kStreamNrm = 1, kStreamCol = 2, kStreamUv0 = 11;
constexpr int kPrimTriangleStrip = 4, kPrimTriangles = 3;

float rdf(const uint8_t* p) {
    float f;
    std::memcpy(&f, p, 4);
    return f;
}
uint32_t rdu32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool readMatrix(const IgObject* o, const char* field, Mat4& out) {
    float f[16];
    if (!o->getFloats(field, f, 16)) return false;
    out = Mat4::fromFloats(f);
    return true;
}

}  // namespace

SceneModel::~SceneModel() {
    seqs_.clear();
    for (Mesh& m : meshes_) {
        if (m.vao != 0) --gLiveMeshes;   // PORT diagnostics (texture.hpp gpuLiveCounts)
        if (m.vao != 0) rlUnloadVertexArray(m.vao);
        for (unsigned b : {m.vboPos, m.vboNrm, m.vboCol, m.vboUv, m.ebo})
            if (b != 0) rlUnloadVertexBuffer(b);
    }
}

bool SceneModel::build(const IgbFile& file, const std::vector<const TextureLibrary*>& libs, std::vector<std::string>* log) {
    file_ = &file;
    libs_ = libs;
    log_ = log;
    meshes_.clear();
    xforms_.clear();
    seqs_.clear();
    switches_.clear();
    visited_.assign(file.slots().size(), 0);

    // skeleton: igAnimationDatabase._skeletonList[0] (igActor._animationDatabase is null in the corpus)
    for (const IgObject* db : file.find("igAnimationDatabase")) {
        const auto skels = file.listObjects(file.object(db->getRef("_skeletonList")));
        if (!skels.empty()) {
            loadSkeleton(skels[0]);
            break;
        }
    }

    std::vector<const IgObject*> roots;
    for (const IgObject* si : file.find("igSceneInfo")) {
        if (const IgObject* sg = file.object(si->getRef("_sceneGraph"))) roots.push_back(sg);
        for (const IgObject* o : file.listObjects(file.object(si->getRef("_sceneGraphList")))) roots.push_back(o);
    }
    for (const IgObject* r : roots) visit(r, Ctx{});
    if (meshes_.empty()) {
        // part files (e.g. C00A_BODY.IGB) carry only an igAnimationDatabase whose skins the game attaches
        for (const IgObject* db : file.find("igAnimationDatabase"))
            for (const IgObject* skin : file.listObjects(file.object(db->getRef("_skinList")))) visit(skin, Ctx{});
    }
    if (meshes_.empty()) {
        // geometry not reachable from any scene graph or skin list (never the case in the corpus's scene
        // files; skins outside the appearance, e.g. the "sh" shadow skin, are intentionally not drawn here)
        for (const IgObject* g : file.find("igGeometry"))
            if (visited_[size_t(g->slot)] == 0) visit(g, Ctx{});
    }

    resetPose();
    for (Mesh& m : meshes_) uploadMesh(m);
    return true;
}

// The game swaps the actor's skin with FUN_00326280(appearance +0xc, skin) where skin is found by name in the
// animation database's skin list (model +0x38 -> +0x1c _skinList, names compared with FUN_0023f3fc; FUN_001113d0).
bool SceneModel::buildSkin(const IgbFile& file, const std::string& skinName,
                           const std::vector<const TextureLibrary*>& libs, std::vector<std::string>* log) {
    const IgObject* found = nullptr;
    for (const IgObject* db : file.find("igAnimationDatabase")) {
        for (const IgObject* skin : file.listObjects(file.object(db->getRef("_skinList")))) {
            if (skin != nullptr && skin->getString("_name") == skinName) {
                found = skin;
                break;
            }
        }
        if (found != nullptr) break;
    }
    if (found == nullptr) return false;
    skinOverride_ = found;
    build(file, libs, log);
    skinOverride_ = nullptr;
    return !meshes_.empty();
}

void SceneModel::loadSkeleton(const IgObject* sk) {
    const IgbFile& f = *file_;
    skeleton_ = Skeleton{};
    skeleton_.name = std::string(sk->getString("_name"));
    const auto infos = f.listObjects(f.object(sk->getRef("_boneInfoList")));
    const IgMemory* tr = f.memory(sk->getRef("_boneTranslationArray"));
    const IgMemory* inv = f.memory(sk->getRef("_invJointArray"));
    const int jc = sk->getInt("_jointCount", 0);
    for (size_t i = 0; i < infos.size(); ++i) {
        Bone b;
        b.name = std::string(infos[i]->getString("_name"));
        b.parent = infos[i]->getInt("_parentIdx", -1);
        b.bmIdx = infos[i]->getInt("_bmIdx", -1);
        b.flags = infos[i]->getInt("_flags", 0);
        if (tr != nullptr && tr->size >= (i + 1) * 12)
            for (size_t k = 0; k < 3; ++k) b.translation[k] = rdf(&tr->data[i * 12 + k * 4]);
        skeleton_.bones.push_back(std::move(b));
    }
    if (inv != nullptr && jc > 0 && inv->size >= size_t(jc) * 64) {
        for (int j = 0; j < jc; ++j) skeleton_.invBind.push_back(Mat4::fromFloats(reinterpret_cast<const float*>(&inv->data[size_t(j) * 64])));
    }
    skeleton_.bmToBone.assign(skeleton_.invBind.size(), -1);
    skeleton_.bindWorld.assign(skeleton_.bones.size(), Mat4::identity());
    std::vector<int> done(skeleton_.bones.size(), 0);
    // bind world = inverse(inverse-bind) when the bone has a blend matrix; otherwise accumulate the
    // local translations (UNVERIFIED: assumes identity local rotation for attachment-only bones).
    for (size_t pass = 0; pass < skeleton_.bones.size() + 1; ++pass) {
        bool progress = false;
        for (size_t i = 0; i < skeleton_.bones.size(); ++i) {
            if (done[i]) continue;
            const Bone& b = skeleton_.bones[i];
            if (b.bmIdx >= 0 && size_t(b.bmIdx) < skeleton_.invBind.size()) {
                Mat4 w;
                if (!invert(skeleton_.invBind[size_t(b.bmIdx)], w)) w = Mat4::identity();
                skeleton_.bindWorld[i] = w;
                skeleton_.bmToBone[size_t(b.bmIdx)] = int(i);
                done[i] = 1;
                progress = true;
            } else if (b.parent < 0 || size_t(b.parent) >= skeleton_.bones.size()) {
                skeleton_.bindWorld[i] = Mat4::translation(b.translation[0], b.translation[1], b.translation[2]);
                done[i] = 1;
                progress = true;
            } else if (done[size_t(b.parent)]) {
                skeleton_.bindWorld[i] = mul(Mat4::translation(b.translation[0], b.translation[1], b.translation[2]), skeleton_.bindWorld[size_t(b.parent)]);
                done[i] = 1;
                progress = true;
            }
        }
        if (!progress) break;
    }
    skeleton_.valid = !skeleton_.bones.empty();
}

void SceneModel::visit(const IgObject* node, Ctx ctx) {
    if (node == nullptr) return;
    if (visited_[size_t(node->slot)] != 0) return;
    visited_[size_t(node->slot)] = 1;
    const IgbFile& f = *file_;

    Mat4 lm;
    if (node->isA("igTransform") && readMatrix(node, "_m", lm)) {
        // _target (+0x60): 9 = texture matrix (UV animation under igTextureMatrixStateAttr), else the model matrix.
        const int target = node->getInt("_target", 0);
        const IgObject* input = f.object(node->getRef("_transformInput"));
        if (target == 9 || input != nullptr) {
            XformNode x;
            x.slot = node->slot;
            x.target = target;
            x.m = lm;
            x.current = lm;
            x.path = ctx.path;
            if (input != nullptr) {
                TransformSequence seq;
                if (seq.load(f, input, log_)) {
                    x.seq = int(seqs_.size());
                    seqs_.push_back(std::move(seq));
                }
            }
            xforms_.push_back(std::move(x));
            if (target == 9) {
                ctx.state.texXform = int(xforms_.size()) - 1;
            } else {
                ctx.geoXform = int(xforms_.size()) - 1;
                ctx.geoPost = ctx.world;
                ctx.sinceXform = Mat4::identity();
                ctx.world = mul(lm, ctx.world);
            }
        } else {
            ctx.world = mul(lm, ctx.world);
            if (ctx.geoXform >= 0) ctx.sinceXform = mul(lm, ctx.sinceXform);
        }
    }
    if (node->isA("igActor")) {
        if (readMatrix(node, "_transform", lm)) ctx.world = mul(lm, ctx.world);
        ctx.actorBase = ctx.world;
        ctx.inActor = true;
    }
    if (node->isA("igModelViewMatrixBoneSelect")) {
        // UNVERIFIED: selects the actor's bone matrix cache entry [_matrixIndex] as the model matrix.
        const int idx = node->getInt("_matrixIndex", -1);
        if (skeleton_.valid && idx >= 0 && size_t(idx) < skeleton_.bindWorld.size()) {
            ctx.world = mul(skeleton_.bindWorld[size_t(idx)], ctx.inActor ? ctx.actorBase : Mat4::identity());
            // the subtree follows that bone of the posed skeleton (skinMesh), e.g. C08A's StickShape under
            // "Right_WEAPON00_Attach" (bone-local vertices): drawn at the bind pose it floated beside the NPC
            ctx.selectBone = idx;
            ctx.selectBase = ctx.world;
        }
    }
    if (node->isA("igBlendMatrixSelect")) {
        ctx.paletteToBm.clear();
        if (const IgObject* il = f.object(node->getRef("_blendMatrixIndices"))) {
            if (const IgMemory* d = f.memory(il->getRef("_data"))) {
                const int count = il->getInt("_count", int(d->size / 4));
                for (int i = 0; i < count && size_t(i) * 4 + 4 <= d->size; ++i) ctx.paletteToBm.push_back(int(rdu32(&d->data[size_t(i) * 4])));
            }
        }
        if (readMatrix(node, "_skeletonTransform", lm)) ctx.world = mul(lm, ctx.world);  // identity in the corpus
    }
    if (node->isA("igAttrSet")) {
        for (const IgObject* a : f.listObjects(f.object(node->getRef("_attributes")))) applyAttr(a, ctx, node);
        if (node->isA("igStaticGraph")) visit(f.object(node->getRef("_transformGraph")), ctx);
    }
    // children (igSwitch: child i drawn when _childMask bit i is set; setSwitchChild rewrites the mask)
    if (node->isA("igSwitch")) {
        // _childMask (igBitMask: _data words, bit i = child i drawn)
        uint32_t mask = 0xffffffffu;
        if (const IgObject* bm = f.object(node->getRef("_childMask")))
            if (const IgMemory* d = f.memory(bm->getRef("_data")))
                if (d->size >= 4) mask = rdu32(&d->data[0]);
        switches_.push_back(SwitchNode{node->slot, std::string(node->getString("_name")), -1, mask});
        int ci = 0;
        for (const IgObject* c : f.listObjects(f.object(node->getRef("_childList")))) {
            Ctx cc = ctx;
            cc.path.push_back(ci);
            cc.switchPath.emplace_back(node->slot, ci++);
            visit(c, cc);
        }
    } else {
        int ci = 0;
        for (const IgObject* c : f.listObjects(f.object(node->getRef("_childList")))) {
            Ctx cc = ctx;
            cc.path.push_back(ci++);
            visit(c, cc);
        }
    }
    if (node->isA("igActor")) {
        if (const IgObject* app = f.object(node->getRef("_appearance"))) {
            const IgObject* skin = skinOverride_ != nullptr ? skinOverride_ : f.object(app->getRef("_skin"));
            if (skin != nullptr) visit(skin, ctx);
            for (const IgObject* s : f.listObjects(f.object(app->getRef("_attachedSkins")))) visit(s, ctx);
            for (const IgObject* g : f.listObjects(f.object(app->getRef("_attachedGraphs")))) visit(g, ctx);
        }
    }
    if (node->isA("igSkin")) {
        Ctx cc = ctx;
        cc.path.clear();   // transform paths of skinned graphs are relative to the graph (FUN_001c1a70 walks them)
        visit(f.object(node->getRef("_skinnedGraph")), cc);
    }
}

const GpuTexture* SceneModel::resolveTexture(const IgObject* bind, std::string& nameOut) {
    const IgbFile& f = *file_;
    const int ts = bind->getRef("_texture");
    const Slot* s = f.slot(ts);
    if (s == nullptr) return nullptr;
    if (s->kind == SlotKind::Object && s->object->isA("igTextureAttr")) {
        if (const IgObject* img = f.object(s->object->getRef("_image"))) nameOut = std::string(img->getString("_pNameString"));
        for (const TextureLibrary* lib : libs_)
            if (const GpuTexture* t = lib->findByAttr(f, ts)) return t;
        return nullptr;
    }
    if (s->kind == SlotKind::External) {
        // igExternalInfoEntry key "<image name> <texture attr params>" resolved by FUN_002f3bf0 through the
        // igTextureInfo of `_infoName` (e.g. M00_TEX.IGB) -- matched here by image name.
        nameOut = s->external->keyName();
        for (const TextureLibrary* lib : libs_)
            if (const GpuTexture* t = lib->findByName(nameOut)) return t;
    }
    return nullptr;
}

void SceneModel::applyAttr(const IgObject* a, Ctx& ctx, const IgObject* node) {
    const IgbFile& f = *file_;
    RenderState& st = ctx.state;
    if (a->isA("igGeometryAttr")) {
        addGeometry(node, a, ctx);
    } else if (a->isA("igGeometrySetAttr")) {
        if (a->getBool("_drawState", true))
            for (const IgObject* ga : f.listObjects(f.object(a->getRef("_geometryAttrList"))))
                if (ga->isA("igGeometryAttr")) addGeometry(node, ga, ctx);
    } else if (a->isA("igTextureBindAttr")) {
        if (a->getInt("_unitID", 0) == 0) {
            std::string name;
            st.texture = resolveTexture(a, name);
            st.textureName = name;
            if (st.texture == nullptr && !name.empty() &&
                std::find(missingTextures_.begin(), missingTextures_.end(), name) == missingTextures_.end())
                missingTextures_.push_back(name);
        }
    } else if (a->isA("igTextureStateAttr")) {
        if (a->getInt("_unitID", 0) == 0) st.textureEnabled = a->getBool("_enabled", true);
    } else if (a->isA("igBlendStateAttr")) {
        st.blendEnabled = a->getBool("_enabled", false);
    } else if (a->isA("igBlendFunctionAttr")) {
        st.blendSrc = a->getInt("_src", 4);
        st.blendDst = a->getInt("_dst", 5);
        st.blendConstant = uint8_t(a->getUInt("_blendConstant", 0x80));
    } else if (a->isA("igAlphaStateAttr")) {
        st.alphaTestEnabled = a->getBool("_enabled", false);
    } else if (a->isA("igAlphaFunctionAttr")) {
        st.alphaFunc = a->getInt("_func", 7);
        st.alphaRef = a->getFloat("_refValue", 0.0f);
    } else if (a->isA("igDepthWriteStateAttr")) {
        st.depthWrite = a->getBool("_enabled", true);
    } else if (a->isA("igCullFaceAttr")) {
        st.cullEnabled = a->getBool("_enabled", false);
        st.cullMode = a->getInt("_mode", 0);
    } else if (a->isA("igColorAttr")) {
        float c[4];
        if (a->getFloats("_color", c, 4)) st.color = {c[0], c[1], c[2], c[3]};
    } else if (a->isA("igTextureMatrixStateAttr")) {
        if (a->getInt("_unitID", 0) == 0) st.texMatrixEnabled = a->getBool("_enabled", false);
    } else if (a->isA("igTextureMatrixAttr")) {
        if (a->getInt("_unitID", 0) == 0) readMatrix(a, "_m", st.texMatrix);
    } else if (a->isA("igVertexBlendStateAttr")) {
        st.vertexBlend = a->getBool("_enabled", false);
    } else if (a->isA("igFogStateAttr")) {
        st.fogEnabled = a->getBool("_enabled", false);
    } else if (a->isA("igMaterialAttr")) {
        float c[4];
        if (a->getFloats("_diffuse", c, 4)) st.diffuse = {c[0], c[1], c[2], c[3]};
        if (a->getFloats("_ambient", c, 4)) st.ambient = {c[0], c[1], c[2], c[3]};
        if (a->getFloats("_emission", c, 4)) st.emission = {c[0], c[1], c[2], c[3]};
    } else if (a->isA("igMaterialModeAttr")) {
        st.materialMode = a->getInt("_mode", 3);
    } else if (a->isA("igLightingStateAttr")) {
        st.lighting = a->getBool("_enabled", true);
    } else if (a->isA("igColorMaskAttr")) {
        st.colorMask = {a->getBool("_red", true), a->getBool("_green", true), a->getBool("_blue", true), a->getBool("_alpha", true)};
    }
    // igShadeModelAttr is always Gouraud in the corpus. Lighting: the files carry no igLightAttr; the game adds its
    // lights at run time (SceneRenderer::setLighting, FUN_00108960 / FUN_001099e0).
}

void SceneModel::addGeometry(const IgObject* node, const IgObject* ga, const Ctx& ctx) {
    const IgbFile& f = *file_;
    const IgObject* va = f.object(ga->getRef("_vertexArray"));
    if (va == nullptr) return;
    if (f.object(ga->getRef("_indexArray")) != nullptr) {
        if (log_) log_->push_back("indexed geometry (igGeometryAttr._indexArray) not supported, slot " + std::to_string(ga->slot));
        return;
    }
    Mesh mesh;
    mesh.name = std::string(node->getString("_name"));
    if (mesh.name.empty()) mesh.name = "geometry" + std::to_string(node->slot);
    mesh.geometrySlot = node->slot;
    mesh.vertexArraySlot = va->slot;
    mesh.primType = ga->getInt("_primType", kPrimTriangleStrip);
    mesh.state = ctx.state;
    mesh.world = ctx.world;
    mesh.switchPath = ctx.switchPath;
    mesh.geoXform = ctx.geoXform;
    mesh.geoPre = ctx.sinceXform;
    mesh.geoPost = ctx.geoPost;
    mesh.paletteToBm = ctx.paletteToBm;
    if (ctx.selectBone >= 0) {
        Mat4 invBase;
        if (invert(ctx.selectBase, invBase)) {
            mesh.selectBone = ctx.selectBone;
            mesh.selectLocal = mul(ctx.world, invBase);   // transforms between the bone select and the geometry
        }
    }
    if (const Value* vf = va->find("_vertexFormat")) mesh.vertexFormat = vf->u32();

    const uint32_t n = va->getUInt("_numVerts", 0);
    const IgMemory* vd = f.memory(va->getRef("_vdata"));
    if (vd == nullptr || n == 0) return;
    for (size_t i = 0; i < vd->refSlots.size(); ++i) {
        const IgMemory* m = f.memory(vd->refSlots[i]);
        if (m == nullptr) continue;
        if (int(i) == kStreamPos && m->size >= n * 12) {
            mesh.positions.resize(size_t(n) * 3);
            for (size_t k = 0; k < size_t(n) * 3; ++k) mesh.positions[k] = rdf(&m->data[k * 4]);
        } else if (int(i) == kStreamNrm && m->size >= n * 12) {
            mesh.normals.resize(size_t(n) * 3);
            for (size_t k = 0; k < size_t(n) * 3; ++k) mesh.normals[k] = rdf(&m->data[k * 4]);
        } else if (int(i) == kStreamCol && m->size >= n * 4) {
            mesh.colors.assign(m->data.begin(), m->data.begin() + size_t(n) * 4);
        } else if (int(i) == kStreamUv0 && m->size >= n * 8) {
            mesh.uvs.resize(size_t(n) * 2);
            for (size_t k = 0; k < size_t(n) * 2; ++k) mesh.uvs[k] = rdf(&m->data[k * 4]);
        }
    }
    if (mesh.positions.empty()) return;
    const IgMemory* bw = f.memory(va->getRef("_blendWeights"));
    const IgMemory* bi = f.memory(va->getRef("_blendIndices"));
    if (bw != nullptr && bi != nullptr && bw->size % (n * 4) == 0 && bi->size % n == 0) {
        const uint32_t kw = bw->size / (n * 4);
        const uint32_t ki = bi->size / n;
        if (kw == ki && kw > 0 && kw <= 4) {
            mesh.influences = int(kw);
            mesh.weights.resize(size_t(n) * kw);
            for (size_t k = 0; k < size_t(n) * kw; ++k) mesh.weights[k] = rdf(&bw->data[k * 4]);
            mesh.boneIndices.assign(bi->data.begin(), bi->data.begin() + size_t(n) * ki);
            mesh.skinned = !mesh.paletteToBm.empty();  // skeleton may be adopted later (adoptSkeleton)
        }
    }

    // primitives (tools/igb_geom.py geometry_faces / strips_to_tris)
    std::vector<uint32_t> lengths;
    const IgObject* pl = f.object(ga->getRef("_stripLengths"));
    if (pl == nullptr) pl = f.object(ga->getRef("_primLengths"));
    if (pl != nullptr) {
        const IgMemory* ld = f.memory(pl->getRef("_lengthData"));
        const uint32_t cnt = pl->getUInt("_numStrips", 0);
        if (ld != nullptr)
            for (uint32_t i = 0; i < cnt && size_t(i) * 4 + 4 <= ld->size; ++i) lengths.push_back(rdu32(&ld->data[size_t(i) * 4]));
    }
    const uint32_t off = ga->getUInt("_offset", 0);
    auto samePos = [&](uint32_t x, uint32_t y) {
        return mesh.positions[x * 3] == mesh.positions[y * 3] && mesh.positions[x * 3 + 1] == mesh.positions[y * 3 + 1] &&
               mesh.positions[x * 3 + 2] == mesh.positions[y * 3 + 2];
    };
    auto emit = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (a >= n || b >= n || c >= n) return;
        if (a == b || b == c || a == c) return;
        if (samePos(a, b) || samePos(b, c) || samePos(a, c)) return;  // zero-area strip bridges
        mesh.indices.push_back(uint16_t(a));
        mesh.indices.push_back(uint16_t(b));
        mesh.indices.push_back(uint16_t(c));
    };
    if (mesh.primType == kPrimTriangleStrip) {
        if (lengths.empty()) lengths.push_back(n - off);
        uint32_t base = off;
        for (uint32_t len : lengths) {
            for (uint32_t k = 0; k + 2 < len; ++k) {
                const uint32_t a = base + k, b = base + k + 1, c = base + k + 2;
                if (k & 1) emit(a, c, b); else emit(a, b, c);
            }
            base += len;
        }
    } else if (mesh.primType == kPrimTriangles) {
        for (uint32_t i = off; i + 2 < n; i += 3) emit(i, i + 1, i + 2);
    } else {
        if (log_) log_->push_back("unsupported primitive type " + std::to_string(mesh.primType));
        return;
    }
    if (n > 65535) {
        if (log_) log_->push_back("vertex array too large for 16-bit indices: " + std::to_string(n));
        return;
    }
    meshes_.push_back(std::move(mesh));
}

void SceneModel::skinMesh(Mesh& mesh, const std::vector<Mat4>& boneWorld) {
    const size_t n = mesh.positions.size() / 3;
    mesh.skinnedPositions = mesh.positions;
    mesh.skinnedNormals = mesh.normals;
    if (!mesh.skinned || mesh.influences == 0) {
        // rigid geometry under igModelViewMatrixBoneSelect: the bone's posed matrix replaces its bind matrix.
        // Drawn with world = L * B * A (L below the select, B bind, A actor), so v' = v * L * P * B^-1 * L^-1.
        const int b = mesh.selectBone;
        if (b < 0 || size_t(b) >= boneWorld.size() || size_t(b) >= skeleton_.bindWorld.size()) return;
        Mat4 invB, invL;
        if (!invert(skeleton_.bindWorld[size_t(b)], invB) || !invert(mesh.selectLocal, invL)) return;
        const Mat4 x = mul(mul(mul(mesh.selectLocal, boneWorld[size_t(b)]), invB), invL);
        for (size_t v = 0; v < n; ++v) {
            transformPoint(x, &mesh.positions[v * 3], &mesh.skinnedPositions[v * 3]);
            if (!mesh.normals.empty()) {
                float tn[3];
                transformVector(x, &mesh.normals[v * 3], tn);
                const float l = std::sqrt(tn[0] * tn[0] + tn[1] * tn[1] + tn[2] * tn[2]);
                for (int c = 0; c < 3; ++c) mesh.skinnedNormals[v * 3 + size_t(c)] = l > 1e-6f ? tn[c] / l : tn[c];
            }
        }
        return;
    }
    std::vector<Mat4> palette(mesh.paletteToBm.size(), Mat4::identity());
    for (size_t i = 0; i < palette.size(); ++i) {
        const int bm = mesh.paletteToBm[i];
        if (bm < 0 || size_t(bm) >= skeleton_.invBind.size()) continue;
        const int bone = skeleton_.bmToBone[size_t(bm)];
        if (bone < 0 || size_t(bone) >= boneWorld.size()) continue;
        palette[i] = mul(skeleton_.invBind[size_t(bm)], boneWorld[size_t(bone)]);
    }
    const size_t k = size_t(mesh.influences);
    for (size_t v = 0; v < n; ++v) {
        float p[3] = {0, 0, 0}, nn[3] = {0, 0, 0};
        float wsum = 0.0f;
        for (size_t j = 0; j < k; ++j) {
            const float w = mesh.weights[v * k + j];
            if (w == 0.0f) continue;
            const size_t pi = mesh.boneIndices[v * k + j];
            if (pi >= palette.size()) continue;
            float tp[3], tn[3];
            transformPoint(palette[pi], &mesh.positions[v * 3], tp);
            for (int c = 0; c < 3; ++c) p[c] += w * tp[c];
            if (!mesh.normals.empty()) {
                transformVector(palette[pi], &mesh.normals[v * 3], tn);
                for (int c = 0; c < 3; ++c) nn[c] += w * tn[c];
            }
            wsum += w;
        }
        if (wsum > 0.0f) {
            for (int c = 0; c < 3; ++c) mesh.skinnedPositions[v * 3 + size_t(c)] = p[c];
            if (!mesh.normals.empty()) {
                const float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                for (int c = 0; c < 3; ++c) mesh.skinnedNormals[v * 3 + size_t(c)] = l > 1e-6f ? nn[c] / l : nn[c];
            }
        }
    }
}

void SceneModel::setPose(const std::vector<Mat4>& boneWorld) {
    for (Mesh& m : meshes_) {
        skinMesh(m, boneWorld);
        if (m.vboPos != 0) {
            rlUpdateVertexBuffer(m.vboPos, m.skinnedPositions.data(), int(m.skinnedPositions.size() * sizeof(float)), 0);
            if (m.vboNrm != 0 && !m.skinnedNormals.empty())
                rlUpdateVertexBuffer(m.vboNrm, m.skinnedNormals.data(), int(m.skinnedNormals.size() * sizeof(float)), 0);
        }
    }
}

void SceneModel::resetPose() { setPose(skeleton_.bindWorld); }

void SceneModel::adoptSkeleton(const Skeleton& actorSkeleton) {
    skeleton_ = actorSkeleton;
    resetPose();
}

void SceneModel::uploadMesh(Mesh& mesh) {
    const size_t n = mesh.positions.size() / 3;
    mesh.vao = rlLoadVertexArray();
    if (mesh.vao != 0) ++gLiveMeshes;   // PORT diagnostics (texture.hpp gpuLiveCounts)
    rlEnableVertexArray(mesh.vao);
    mesh.vboPos = rlLoadVertexBuffer(mesh.skinnedPositions.data(), int(n * 3 * sizeof(float)), mesh.skinned || mesh.selectBone >= 0);
    rlSetVertexAttribute(0, 3, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute(0);
    if (!mesh.normals.empty()) {
        mesh.vboNrm = rlLoadVertexBuffer(mesh.skinnedNormals.data(), int(n * 3 * sizeof(float)), mesh.skinned || mesh.selectBone >= 0);
        rlSetVertexAttribute(1, 3, RL_FLOAT, false, 0, 0);
        rlEnableVertexAttribute(1);
    } else {
        rlDisableVertexAttribute(1);
    }
    if (!mesh.colors.empty()) {
        mesh.vboCol = rlLoadVertexBuffer(mesh.colors.data(), int(n * 4), false);
        rlSetVertexAttribute(2, 4, RL_UNSIGNED_BYTE, true, 0, 0);
        rlEnableVertexAttribute(2);
    } else {
        rlDisableVertexAttribute(2);
    }
    if (!mesh.uvs.empty()) {
        mesh.vboUv = rlLoadVertexBuffer(mesh.uvs.data(), int(n * 2 * sizeof(float)), false);
        rlSetVertexAttribute(3, 2, RL_FLOAT, false, 0, 0);
        rlEnableVertexAttribute(3);
    } else {
        rlDisableVertexAttribute(3);
    }
    mesh.ebo = rlLoadVertexBufferElement(mesh.indices.data(), int(mesh.indices.size() * sizeof(uint16_t)), false);
    rlDisableVertexArray();
}

SceneModel::Stats SceneModel::stats() const {
    Stats s;
    std::vector<const GpuTexture*> seen;
    for (const Mesh& m : meshes_) {
        s.meshes += 1;
        s.vertices += int(m.positions.size() / 3);
        s.triangles += int(m.indices.size() / 3);
        if (m.state.texture != nullptr && std::find(seen.begin(), seen.end(), m.state.texture) == seen.end()) seen.push_back(m.state.texture);
    }
    s.texturesBound = int(seen.size());
    s.missingTextures = missingTextures_;
    return s;
}

bool SceneModel::bounds(float bmin[3], float bmax[3]) const {
    bool any = false;
    for (const Mesh& m : meshes_) {
        const std::vector<float>& p = m.skinnedPositions.empty() ? m.positions : m.skinnedPositions;
        for (size_t v = 0; v + 2 < p.size(); v += 3) {
            float w[3];
            transformPoint(m.world, &p[v], w);
            if (!any) {
                for (int c = 0; c < 3; ++c) bmin[c] = bmax[c] = w[c];
                any = true;
            } else {
                for (int c = 0; c < 3; ++c) {
                    bmin[c] = std::min(bmin[c], w[c]);
                    bmax[c] = std::max(bmax[c], w[c]);
                }
            }
        }
    }
    return any;
}

// ---------------------------------------------------------------------------------------------
// SceneRenderer
// ---------------------------------------------------------------------------------------------

namespace {

const char* kVertexShader = R"GLSL(
#version 330
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec2 vertexTexCoord;
uniform mat4 mvp;
uniform mat4 modelView;
uniform mat4 model;
uniform mat4 texMatrix;
uniform int texMatrixEnabled;
// VU1 directional-light stages (SceneRenderer::drawFiltered): colours in VU units (255 = vertex byte 0xFF)
uniform int lit;            // 0 unlit (vertex colour), 1 lit
uniform int litMode;        // 0 material colours (stage L2150), 5 ambient+diffuse track the vertex colour (L25a8),
                            // 4 diffuse tracks the vertex colour
uniform vec3 lDir[4];       // unit vectors toward the lights (0 for unused slots)
uniform vec4 lCol[4];       // light colour constants (VU block +6..+9)
uniform vec3 lBase;         // +0xe (ambient sum)
uniform vec3 lEmis;         // +0xf (emission * 255, tracking modes)
uniform float lAlpha;       // non-tracking alpha (light colour 0 .w = material alpha * 128)
// lit == 2: local (point) light path 0x100 (LIGHT_LOCAL_OBJ_1_* / LIGHT_LOCAL_OBJ_N_INFINITE_4_* stages)
uniform int pCount;         // point lights (list 0x555c60, <= 8)
uniform vec3 pPos[8];       // light position (+0x70)
uniform vec3 pAtt[8];       // attenuation (+0x50): constant, linear, quadratic
uniform vec3 pDif[8];       // light diffuse (+0x20)
uniform vec3 pAmb[8];       // light ambient (+0x10)
uniform vec4 mA;            // material ambient * 255
uniform vec4 mD;            // material diffuse * 255, w = alpha * 128
uniform vec3 mE;            // material emission * 255
uniform ivec3 track;        // colour material: ambient / diffuse / emission replaced by the vertex colour
uniform vec3 aSum;          // global ambient + sum of the directional lights' ambient
uniform int pSingle;        // single-light stage (LIGHT_LOCAL_OBJ_1_*): clamp (255, 255, 255, 128)
out vec4 fragColor;
out vec2 fragUV;
out float fragDepth;
out vec4 fragClip;          // PORT: see-through dither (screen position of the fragment)
out float fragWorldY;       // PORT: see-through dither (floor filter)
void main() {
    gl_Position = mvp * vec4(vertexPosition, 1.0);
    fragClip = gl_Position;
    fragWorldY = (model * vec4(vertexPosition, 1.0)).y;
    vec4 vp = modelView * vec4(vertexPosition, 1.0);
    fragDepth = -vp.z;
    if (lit != 0) {
        vec3 n = normalize((model * vec4(vertexNormal, 0.0)).xyz);
        vec4 d = max(vec4(dot(n, lDir[0]), dot(n, lDir[1]), dot(n, lDir[2]), dot(n, lDir[3])), vec4(0.0));
        vec3 sum = d.x * lCol[0].xyz + d.y * lCol[1].xyz + d.z * lCol[2].xyz + d.w * lCol[3].xyz;
        vec4 vc = vertexColor * 255.0;
        vec4 o;
        if (lit == 2) {
            // LIGHT_LOCAL_OBJ_* stages: out = E' + A' * G + sum_dir D' * Ld * max(N.L, 0) + A' * sum_dir La
            //   + sum_point (D' * Ld * max(N.L, 0) + A' * La) / (k0 + k1 d + k2 d^2), L = normalise(Lpos - P);
            // A' / D' / E' are the material colours or the vertex colour (MC_A / MC_D / MC_AD / MC_E).
            vec3 A = track.x == 1 ? vc.rgb : mA.rgb;
            vec3 D = track.y == 1 ? vc.rgb : mD.rgb;
            vec3 E = track.z == 1 ? vc.rgb : mE;
            float al = track.y == 1 ? vc.a : mD.a;
            vec3 P = (model * vec4(vertexPosition, 1.0)).xyz;
            vec3 acc = E + A * aSum + D * sum;
            for (int i = 0; i < pCount; ++i) {
                vec3 v = pPos[i] - P;
                float dl = length(v);
                vec3 L = dl > 0.0 ? v / dl : vec3(0.0);
                float at = pAtt[i].x + pAtt[i].y * dl + pAtt[i].z * dl * dl;
                acc += (D * pDif[i] * max(dot(n, L), 0.0) + A * pAmb[i]) / at;
            }
            o = vec4(acc, al);
            o = min(o, pSingle == 1 ? vec4(255.0, 255.0, 255.0, 128.0) : vec4(255.0));
        } else {
            if (litMode == 5) { o = vec4((lBase + sum) * vc.rgb + lEmis, vc.a); }        // LIGHT_INFINITE_4_MC_AD
            else if (litMode == 4) { o = vec4(lBase + sum * vc.rgb, vc.a); }           // LIGHT_INFINITE_4_MC_D
            else if (litMode == 1) { o = vec4(lBase * vc.rgb + sum + lEmis, lAlpha); } // LIGHT_INFINITE_4_MC_A
            else if (litMode == 2) { o = vec4(lBase + sum + vc.rgb, lAlpha); }         // LIGHT_INFINITE_4_MC_E
            else { o = vec4(lBase + sum, lAlpha); }                                    // LIGHT_INFINITE_4
            o = min(o, vec4(255.0));                 // MINIi 255
        }
        // final stage: colour buffer x VU1 constant 0x1c (frag shader colorScale); rgb in vertex-byte units,
        // alpha in GS units (0x80 = 1.0)
        fragColor = vec4(o.rgb / 255.0, o.a / 128.0);
    } else {
        fragColor = vertexColor;
    }
    vec2 uv = vertexTexCoord;
    if (texMatrixEnabled == 1) { vec4 t = texMatrix * vec4(uv, 0.0, 1.0); uv = t.xy; }
    fragUV = uv;
}
)GLSL";

// GS fixed function: TFX MODULATE (Ct*Cf>>7, At*Af>>7) with 0x80 = 1.0, ATEST on the 8-bit alpha against
// AREF = (int)(ref*128) (FUN_003837c0), then alpha blending by the ALPHA register (set from GL factors).
const char* kFragmentShader = R"GLSL(
#version 330
in vec4 fragColor;
in vec2 fragUV;
in float fragDepth;
uniform sampler2D tex;
uniform int texEnabled;
uniform vec4 colorScale;    // SceneRenderer::vertexColorScale (VU units, see scene_render.hpp)
uniform int alphaTest;
uniform int alphaFunc;      // 0 NEVER 1 LESS 2 EQUAL 3 LEQUAL 4 GREATER 5 NOTEQUAL 6 GEQUAL 7 ALWAYS
uniform float alphaRef;     // GS AREF (0..255 integer, 0x80 = 1.0)
uniform int fogEnabled;
uniform vec4 fogColor;
uniform vec2 fogRange;      // near, far
// PORT: see-through (SceneRenderer::CutParams).
// - World meshes, object mode: an occluding mesh fades out by alpha (cutAlpha, drawn in the blended pass by
//   drawFiltered); nothing is dithered. Geometry nearer than cutNear.x to the eye is dropped.
// - Characters (cutDither 1): a 4x4 ordered dither of cutDotPx-pixel cells, density max(eye-near fade from
//   cutChrNear.y down to cutChrNear.x, cutStrength = their occluder strength).
// - Silhouette mode (cutMode 2): world fragments nearer than cutDepth where the player mask covers them are dropped.
// Only fragments above cutFloorY are touched (floors stay).
in vec4 fragClip;
in float fragWorldY;
uniform int cutMode;
uniform float cutStrength;
uniform float cutDepth;
uniform float cutFloorY;
uniform float cutDotPx;
uniform vec2 cutNear;
uniform sampler2D cutMask;
uniform vec2 cutMaskSize;
uniform int maskMode;       // 1: write the player mask (white)
uniform int cutDither;
uniform vec2 cutChrNear;
uniform float cutAlpha;
uniform int cutPart;        // 0 whole draw, 1 far part only, 2 near part faded, 3 near part faded in place
const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
out vec4 finalColor;
void main() {
    if (cutMode != 0 && fragWorldY > cutFloorY) {
        if (cutDither == 1) {
            float cut = max(1.0 - smoothstep(cutChrNear.x, cutChrNear.y, fragDepth), cutStrength);
            ivec2 q = ivec2(floor(gl_FragCoord.xy / cutDotPx)) & 3;
            if ((kBayer[q.y * 4 + q.x] + 0.5) / 16.0 < cut) discard;
        } else {
            if (fragDepth < cutNear.x) discard;
            bool nearPart = fragDepth < cutDepth;
            if (cutPart == 1 && nearPart) discard;
            if (cutPart == 2 && !nearPart) discard;
            if (cutMode == 2 && fragDepth < cutDepth && cutStrength * texture(cutMask, gl_FragCoord.xy / cutMaskSize).r >= 0.5)
                discard;
        }
    }
    vec4 cf = vec4(fragColor.rgb * (255.0 / 128.0) * colorScale.rgb, fragColor.a * colorScale.a);
    vec4 ct = (texEnabled == 1) ? texture(tex, fragUV) : vec4(1.0);
    vec4 o = ct * cf;
    if (alphaTest == 1) {
        float av = floor(o.a * 128.0 + 0.001);
        bool pass = true;
        if (alphaFunc == 0) pass = false;
        else if (alphaFunc == 1) pass = av < alphaRef;
        else if (alphaFunc == 2) pass = av == alphaRef;
        else if (alphaFunc == 3) pass = av <= alphaRef;
        else if (alphaFunc == 4) pass = av > alphaRef;
        else if (alphaFunc == 5) pass = av != alphaRef;
        else if (alphaFunc == 6) pass = av >= alphaRef;
        if (!pass) discard;
    }
    o = min(o, vec4(1.0));
    if (maskMode == 1) { finalColor = vec4(1.0); return; }   // PORT: player mask pass
    if (fogEnabled == 1) {
        float f = clamp((fogRange.y - fragDepth) / max(fogRange.y - fogRange.x, 1e-3), 0.0, 1.0);
        o.rgb = mix(fogColor.rgb, o.rgb, f);
    }
    if (cutPart >= 2 && fragDepth < cutDepth && fragWorldY > cutFloorY) o.a *= cutAlpha;   // PORT: occluder fade
    finalColor = o;
}
)GLSL";

int glBlendFactor(int alchemy) {
    switch (alchemy) {
    case 0: return RL_ZERO;
    case 1: return RL_ONE;
    case 2: return RL_SRC_COLOR;
    case 3: return RL_ONE_MINUS_SRC_COLOR;
    case 4: return RL_SRC_ALPHA;
    case 5: return RL_ONE_MINUS_SRC_ALPHA;
    case 6: return RL_DST_ALPHA;
    case 7: return RL_ONE_MINUS_DST_ALPHA;
    case 8: return RL_DST_COLOR;
    case 9: return RL_ONE_MINUS_DST_COLOR;
    case 10: return RL_SRC_ALPHA_SATURATE;
    case 11: return RL_CONSTANT_COLOR;
    default: return RL_ONE;
    }
}

}  // namespace

bool SceneRenderer::init() {
    if (program_ != 0) return true;
    program_ = rlLoadShaderCode(kVertexShader, kFragmentShader);
    if (program_ == 0) return false;
    locMvp_ = rlGetLocationUniform(program_, "mvp");
    locModelView_ = rlGetLocationUniform(program_, "modelView");
    locTexMatrix_ = rlGetLocationUniform(program_, "texMatrix");
    locTexMatrixEnabled_ = rlGetLocationUniform(program_, "texMatrixEnabled");
    locTex_ = rlGetLocationUniform(program_, "tex");
    locTexEnabled_ = rlGetLocationUniform(program_, "texEnabled");
    locColorScale_ = rlGetLocationUniform(program_, "colorScale");
    locAlphaTest_ = rlGetLocationUniform(program_, "alphaTest");
    locAlphaFunc_ = rlGetLocationUniform(program_, "alphaFunc");
    locAlphaRef_ = rlGetLocationUniform(program_, "alphaRef");
    locFogEnabled_ = rlGetLocationUniform(program_, "fogEnabled");
    locFogColor_ = rlGetLocationUniform(program_, "fogColor");
    locFogRange_ = rlGetLocationUniform(program_, "fogRange");
    locModel_ = rlGetLocationUniform(program_, "model");
    locLit_ = rlGetLocationUniform(program_, "lit");
    locLitMode_ = rlGetLocationUniform(program_, "litMode");
    locLDir_ = rlGetLocationUniform(program_, "lDir");
    locLCol_ = rlGetLocationUniform(program_, "lCol");
    locLBase_ = rlGetLocationUniform(program_, "lBase");
    locLEmis_ = rlGetLocationUniform(program_, "lEmis");
    locLAlpha_ = rlGetLocationUniform(program_, "lAlpha");
    locPCount_ = rlGetLocationUniform(program_, "pCount");
    locPPos_ = rlGetLocationUniform(program_, "pPos");
    locPAtt_ = rlGetLocationUniform(program_, "pAtt");
    locPDif_ = rlGetLocationUniform(program_, "pDif");
    locPAmb_ = rlGetLocationUniform(program_, "pAmb");
    locMA_ = rlGetLocationUniform(program_, "mA");
    locMD_ = rlGetLocationUniform(program_, "mD");
    locME_ = rlGetLocationUniform(program_, "mE");
    locTrack_ = rlGetLocationUniform(program_, "track");
    locASum_ = rlGetLocationUniform(program_, "aSum");
    locPSingle_ = rlGetLocationUniform(program_, "pSingle");
    locCutMode_ = rlGetLocationUniform(program_, "cutMode");
    locCutStrength_ = rlGetLocationUniform(program_, "cutStrength");
    locCutDepth_ = rlGetLocationUniform(program_, "cutDepth");
    locCutFloorY_ = rlGetLocationUniform(program_, "cutFloorY");
    locCutDotPx_ = rlGetLocationUniform(program_, "cutDotPx");
    locCutNear_ = rlGetLocationUniform(program_, "cutNear");
    locCutMask_ = rlGetLocationUniform(program_, "cutMask");
    locCutMaskSize_ = rlGetLocationUniform(program_, "cutMaskSize");
    locMaskMode_ = rlGetLocationUniform(program_, "maskMode");
    locCutDither_ = rlGetLocationUniform(program_, "cutDither");
    locCutChrNear_ = rlGetLocationUniform(program_, "cutChrNear");
    locCutAlpha_ = rlGetLocationUniform(program_, "cutAlpha");
    locCutPart_ = rlGetLocationUniform(program_, "cutPart");
    return true;
}

void SceneRenderer::shutdown() {
    if (program_ != 0) rlUnloadShaderProgram(program_);
    program_ = 0;
}

void SceneRenderer::setFog(bool enabled, const float color[4], float nearVal, float farVal) {
    fogEnabled_ = enabled;
    for (int i = 0; i < 4; ++i) fogColor_[size_t(i)] = color[i];
    fogNear_ = nearVal;
    fogFar_ = farVal;
}

void SceneRenderer::begin(const Mat4& view, const Mat4& proj) {
    view_ = view;
    proj_ = proj;
    rlDrawRenderBatchActive();
    rlEnableShader(program_);
    const int zero = 0;
    rlSetUniform(locTex_, &zero, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(locColorScale_, vertexColorScale.data(), RL_SHADER_UNIFORM_VEC4, 1);
    const float range[2] = {fogNear_, fogFar_};
    rlSetUniform(locFogColor_, fogColor_.data(), RL_SHADER_UNIFORM_VEC4, 1);
    rlSetUniform(locFogRange_, range, RL_SHADER_UNIFORM_VEC2, 1);
    rlEnableDepthTest();
    rlEnableDepthMask();
}

void SceneRenderer::draw(const SceneModel& model, const Mat4& modelMatrix, const AttrOverride* ovr) {
    drawFiltered(model, modelMatrix, -1, ovr);
}

// FUN_001b8440 writes the switch's _childMask: child 0 -> bit 0 set / bit 1 cleared, child 1 -> bit 1 set / bit 0
// cleared; -1 = every child.
int SceneModel::setSwitchChild(const std::string& name, int child) {
    int n = 0;
    for (SwitchNode& s : switches_)
        if (s.name == name) {
            s.activeChild = child;
            if (child < 0) s.mask = 0xffffffffu;
            else s.mask = (s.mask | (1u << child)) & ~(1u << (child ^ 1));
            ++n;
        }
    return n;
}

// igSwitch traversal: child i is drawn when bit i of _childMask is set (initial mask from the file).
bool SceneModel::meshVisible(size_t meshIndex) const {
    for (const auto& [slot, child] : meshes_[meshIndex].switchPath)
        for (const SwitchNode& s : switches_)
            if (s.slot == slot && child < 32 && (s.mask & (1u << child)) == 0) return false;
    return true;
}

int SceneModel::findTransformByPath(const std::vector<int>& path) const {
    for (size_t i = 0; i < xforms_.size(); ++i)
        if (xforms_[i].path == path) return int(i);
    return -1;
}

void SceneModel::detachTransformInput(int index) {
    if (index < 0 || size_t(index) >= xforms_.size()) return;
    xforms_[size_t(index)].detached = true;
    xforms_[size_t(index)].current = xforms_[size_t(index)].m;
}

// orig FUN_0039d980(m, v): row 3 (translation) = v
void SceneModel::setTransformTranslation(int index, float x, float y, float z) {
    if (index < 0 || size_t(index) >= xforms_.size()) return;
    XformNode& n = xforms_[size_t(index)];
    n.m.m[12] = x;
    n.m.m[13] = y;
    n.m.m[14] = z;
    if (n.detached || n.seq < 0) n.current = n.m;
}

// igTransform traversal: a transform with an input takes the input's matrix at the scene time (the sequence getter
// FUN_003fccb0); without one (or detached) its own _m.
void SceneModel::animate(int64_t timeNs) const {
    if (timeNs == animTime_) return;
    animTime_ = timeNs;
    for (XformNode& n : xforms_) {
        if (n.seq >= 0 && !n.detached) n.current = seqs_[size_t(n.seq)].evaluate(timeNs);
        else n.current = n.m;
    }
}

const Mat4& SceneModel::meshTexMatrix(const Mesh& m) const {
    if (m.state.texXform >= 0 && size_t(m.state.texXform) < xforms_.size()) return xforms_[size_t(m.state.texXform)].current;
    return m.state.texMatrix;
}

Mat4 SceneModel::meshWorld(const Mesh& m) const {
    if (m.geoXform < 0 || size_t(m.geoXform) >= xforms_.size()) return m.world;
    return mul(mul(m.geoPre, xforms_[size_t(m.geoXform)].current), m.geoPost);
}

namespace {
int64_t (*gSceneTime)() = nullptr;
}
void setSceneTimeSource(int64_t (*fn)()) { gSceneTime = fn; }
int64_t sceneTime() { return gSceneTime != nullptr ? gSceneTime() : 0; }

void AttrOverride::apply(RenderState& st) const {
    if (colorMaskSet) st.colorMask = colorMask;
    if (blendStateSet) st.blendEnabled = blendEnabled;
    if (blendFuncSet) {
        st.blendSrc = blendSrc;
        st.blendDst = blendDst;
    }
    if (depthWriteSet) st.depthWrite = depthWrite;
    if (alphaStateSet) st.alphaTestEnabled = alphaTest;
    if (alphaFuncSet) {
        st.alphaFunc = alphaFunc;
        st.alphaRef = alphaRef;
    }
    if (materialSet) {
        st.diffuse = diffuse;
        st.ambient = ambient;
        st.emission = emission;
    }
    if (materialModeSet) st.materialMode = materialMode;
    if (lightingSet) st.lighting = lighting;
}

AttrOverride AttrOverride::compose(const AttrOverride& inner) const {
    AttrOverride r = inner;
    if (colorMaskSet) { r.colorMaskSet = true; r.colorMask = colorMask; }
    if (blendStateSet) { r.blendStateSet = true; r.blendEnabled = blendEnabled; }
    if (blendFuncSet) { r.blendFuncSet = true; r.blendSrc = blendSrc; r.blendDst = blendDst; }
    if (depthWriteSet) { r.depthWriteSet = true; r.depthWrite = depthWrite; }
    if (alphaStateSet) { r.alphaStateSet = true; r.alphaTest = alphaTest; }
    if (alphaFuncSet) { r.alphaFuncSet = true; r.alphaFunc = alphaFunc; r.alphaRef = alphaRef; }
    if (materialSet) { r.materialSet = true; r.diffuse = diffuse; r.ambient = ambient; r.emission = emission; }
    if (materialModeSet) { r.materialModeSet = true; r.materialMode = materialMode; }
    if (lightingSet) { r.lightingSet = true; r.lighting = lighting; }
    return r;
}

void SceneRenderer::drawFiltered(const SceneModel& model, const Mat4& modelMatrix, int pass, const AttrOverride* ovr) {
    model.animate(model.timeOverride() >= 0 ? model.timeOverride() : sceneTime());
    for (size_t mi = 0; mi < model.meshes().size(); ++mi) {
        const Mesh& m = model.meshes()[mi];
        if (m.vao == 0 || m.indices.empty()) continue;
        if (!model.meshVisible(mi)) continue;
        RenderState eff;
        const RenderState* stp = &m.state;
        if (ovr != nullptr) {
            eff = m.state;
            ovr->apply(eff);
            stp = &eff;
        }
        // PORT: see-through fade of an occluding world mesh (object mode). Only its part in front of the player
        // ("near": nearer than cutDepth, above cutFloorY) fades: an opaque mesh draws its far part in the opaque pass
        // (cutPart 1) and its near part with the blended geometry at alpha 1 - fade without depth writes (cutPart 2,
        // skipped once fully faded); an already blended mesh fades its near part in place (cutPart 3).
        float fade = 0.0f;
        int part = 0;
        if (cut_.enabled && cutAllowed_ && !maskMode_ && !cut_.silhouette && cutInstance_ >= 0 && cutMeshes_ != nullptr) {
            const auto it = cutMeshes_->find((uint64_t(uint32_t(cutInstance_)) << 32) | uint64_t(mi));
            if (it != cutMeshes_->end()) fade = it->second;
        }
        if (fade > 0.001f && pass >= 0) {
            if (stp->blendEnabled) {
                part = 3;
            } else if (pass == 0) {
                part = 1;
            } else {
                if (fade >= 0.99f) continue;
                part = 2;
                if (stp != &eff) { eff = *stp; stp = &eff; }
                eff.blendEnabled = true;
                eff.blendSrc = 4;   // SRC_ALPHA
                eff.blendDst = 5;   // ONE_MINUS_SRC_ALPHA
                eff.depthWrite = false;
            }
        }
        const RenderState& st = *stp;
        if (pass == 0 && st.blendEnabled) continue;
        if (pass == 1 && !st.blendEnabled) continue;
        const Mat4 mv = mul(mul(model.meshWorld(m), modelMatrix), view_);
        const Mat4 mvp = mul(mv, proj_);
        rlSetUniformMatrix(locMvp_, toRaylib(mvp));
        rlSetUniformMatrix(locModelView_, toRaylib(mv));
        rlSetUniformMatrix(locTexMatrix_, toRaylib(model.meshTexMatrix(m)));
        rlSetUniformMatrix(locModel_, toRaylib(mul(model.meshWorld(m), modelMatrix)));
        uploadLighting(st, model.meshWorld(m), modelMatrix);
        rlColorMask(st.colorMask[0], st.colorMask[1], st.colorMask[2], st.colorMask[3]);
        const int tme = st.texMatrixEnabled ? 1 : 0;
        rlSetUniform(locTexMatrixEnabled_, &tme, RL_SHADER_UNIFORM_INT, 1);
        const int texOn = (st.textureEnabled && st.texture != nullptr) ? 1 : 0;
        rlSetUniform(locTexEnabled_, &texOn, RL_SHADER_UNIFORM_INT, 1);
        const int at = st.alphaTestEnabled ? 1 : 0;
        rlSetUniform(locAlphaTest_, &at, RL_SHADER_UNIFORM_INT, 1);
        rlSetUniform(locAlphaFunc_, &st.alphaFunc, RL_SHADER_UNIFORM_INT, 1);
        const float aref = float(int(st.alphaRef * 128.0f));  // FUN_003837c0: (int)(ref * 128.0)
        rlSetUniform(locAlphaRef_, &aref, RL_SHADER_UNIFORM_FLOAT, 1);
        const int fog = fogEnabled_ ? 1 : 0;  // placeholder: igFogStateAttr per node is st.fogEnabled
        rlSetUniform(locFogEnabled_, &fog, RL_SHADER_UNIFORM_INT, 1);
        {   // PORT: see-through / player mask
            const int mode = (cut_.enabled && cutAllowed_ && !maskMode_) ? (cut_.silhouette ? 2 : 1) : 0;
            rlSetUniform(locCutMode_, &mode, RL_SHADER_UNIFORM_INT, 1);
            const int mm = maskMode_ ? 1 : 0;
            rlSetUniform(locMaskMode_, &mm, RL_SHADER_UNIFORM_INT, 1);
            const float alpha = 1.0f - fade;
            rlSetUniform(locCutAlpha_, &alpha, RL_SHADER_UNIFORM_FLOAT, 1);
            rlSetUniform(locCutPart_, &part, RL_SHADER_UNIFORM_INT, 1);
            if (mode != 0) {
                const bool chr = cutInstance_ < 0;   // not a world draw: a character (dithered)
                const int dither = chr ? 1 : 0;
                const float strength = chr ? cutUniform_ : cut_.silhouetteStrength;
                const float floorY = chr ? -1.0e9f : cut_.floorY;
                rlSetUniform(locCutDither_, &dither, RL_SHADER_UNIFORM_INT, 1);
                rlSetUniform(locCutStrength_, &strength, RL_SHADER_UNIFORM_FLOAT, 1);
                rlSetUniform(locCutDepth_, &cut_.depth, RL_SHADER_UNIFORM_FLOAT, 1);
                rlSetUniform(locCutFloorY_, &floorY, RL_SHADER_UNIFORM_FLOAT, 1);
                rlSetUniform(locCutDotPx_, &cut_.dotPx, RL_SHADER_UNIFORM_FLOAT, 1);
                rlSetUniform(locCutNear_, cut_.nearFade, RL_SHADER_UNIFORM_VEC2, 1);
                rlSetUniform(locCutChrNear_, cut_.chrNearFade, RL_SHADER_UNIFORM_VEC2, 1);
                if (cut_.silhouette && cut_.maskTex != 0) {
                    const int one = 1;
                    rlSetUniform(locCutMask_, &one, RL_SHADER_UNIFORM_INT, 1);
                    rlSetUniform(locCutMaskSize_, cut_.maskSize, RL_SHADER_UNIFORM_VEC2, 1);
                    rlActiveTextureSlot(1);
                    rlEnableTexture(cut_.maskTex);
                }
            }
        }
        rlActiveTextureSlot(0);
        if (texOn) rlEnableTexture(st.texture->id); else rlDisableTexture();

        if (st.blendEnabled) {
            rlEnableColorBlend();
            rlSetBlendFactors(glBlendFactor(st.blendSrc), glBlendFactor(st.blendDst), RL_FUNC_ADD);
            rlSetBlendMode(RL_BLEND_CUSTOM);
            // rlSetBlendMode flushes raylib's internal batch when the mode changes, which unbinds the current
            // program: rebind ours (uniform values live in the program object and survive).
            rlEnableShader(program_);
        } else {
            rlDisableColorBlend();
        }
        if (st.depthWrite) rlEnableDepthMask(); else rlDisableDepthMask();
        if (st.cullEnabled) {
            rlEnableBackfaceCulling();
            rlSetCullFace(st.cullMode == 0 ? RL_CULL_FACE_BACK : RL_CULL_FACE_FRONT);  // UNVERIFIED mapping
        } else {
            rlDisableBackfaceCulling();
        }

        rlEnableVertexArray(m.vao);
        if (m.colors.empty()) rlSetVertexAttributeDefault(2, st.color.data(), RL_SHADER_ATTRIB_VEC4, 4);
        if (m.normals.empty()) {
            const float nrm[3] = {0.0f, 0.0f, 1.0f};
            rlSetVertexAttributeDefault(1, nrm, RL_SHADER_ATTRIB_VEC3, 3);
        }
        if (m.uvs.empty()) {
            const float uv[2] = {0.0f, 0.0f};
            rlSetVertexAttributeDefault(3, uv, RL_SHADER_ATTRIB_VEC2, 2);
        }
        rlDrawVertexArrayElements(0, int(m.indices.size()), nullptr);
        rlDisableVertexArray();
    }
}

// Per-vertex lighting of the lit branch, translated from the Alchemy PS2 low-level light module and its VU1 stages.
// Stage names / feature masks come from the microcode descriptor string at 0x471fa0 (parsed by FUN_0024b7f0); the
// stage code follows in the same order in the microcode blob 0x40cd00..0x427f40 (tools/vudis.py), see
// work/reports/MATERIALS_NATIVE.md section 8.
//  * FUN_00259100 picks the light path from the enabled low-level lights (0x555c70[8]): directional ones (type 0, at
//    most 4) go to list 0x555c58, local ones (type 1 / 2) to 0x555c60. No local light and <= 4 lights: path 0x80
//    (LIGHT_INFINITE_4*), program = table 0x475280[(specular << 3) | mode]; local lights, all lights counted == 1:
//    path 0x100 single (LIGHT_LOCAL_OBJ_1_*), else (< 9 local, no spot): path 0x100 multi
//    (LIGHT_LOCAL_OBJ_N_INFINITE_4_*), program = table 0x4752c0[mode | 8 (multi) | 0x10 (linear attenuation only)].
//    Specular programs need material specular != 0 (+0x44 bit 0): all 2011 igMaterialAttr in the corpus have
//    specular (0, 0, 0, a), so they are never selected.
//  * material: igMaterialAttr apply FUN_00381220 (+0 ambient, +0x10 diffuse, +0x20 specular, +0x30 emission);
//    colour material = cap 0xc (FUN_003813c0: on unless igMaterialModeAttr is 0), low-level mode = table 0x4845a0
//    {0,1,4,5,3,2}[mode] (1 ambient, 2 emission, 3 specular, 4 diffuse, 5 ambient+diffuse track the vertex colour);
//    program index: 0 plain, 1 _MC_E, 2 _MC_A, 3 _MC_D, 4 _MC_AD (mode 3 without specular = plain).
//  * path 0x80 VU block (FUN_00259a10 tail + FUN_0025b760): +0..+2 light directions (SoA, -direction), +6..+9 light
//    colours, +0xe base (ambient sum, clamped to 255 on the CPU), +0xf emission.
//    LIGHT_INFINITE_4        out = min(base + sum_i max(N.L_i, 0) C_i, 255), alpha C_0.w (= Dm.a * 128)
//    LIGHT_INFINITE_4_MC_AD  out = min((base + sum) * vc + E, 255), alpha vc.a
//    LIGHT_INFINITE_4_MC_E   out = min(base + sum + vc, 255), alpha C_0.w
//    LIGHT_INFINITE_4_MC_A   out = min(base * vc + sum + E, 255), alpha C_0.w
//    LIGHT_INFINITE_4_MC_D   out = min(base + sum * vc, 255), alpha vc.a
//  * path 0x100 VU block (FUN_0025b3d0 -> FUN_0025b9f0 / FUN_0025bdd0): +0 emission (+ sum Am * La of the
//    directional lights when ambient is not tracked), +1 material ambient, +2 material diffuse, +3 global ambient,
//    per local light {attenuation, position, ambient, diffuse} (or {position, diffuse | k1} when every light has
//    linear-only attenuation and no ambient), then the directional block. All variants compute
//    out = E' + A' G + sum_dir (D' Ld max(N.L, 0) + A' La) + sum_local (D' Ld max(N.L, 0) + A' La) / (k0 + k1 d + k2 d^2)
//    with A' / D' / E' = material * 255 or the vertex colour, alpha D'.w; the single-light stage clamps to
//    (255, 255, 255, 128), the others to 255.
// UNVERIFIED: the space of the local-light distance (the 0x100 path works in object space: positions as stored, light
// position through the inverse model matrix 0x555d00); the port measures it in world space, equal unless the model
// matrix scales. Paths 0x2000 / 0x4000 / 0x8000 (> 4 directional, > 8 local, spot lights) are not translated: the
// game's lights (render list slots: 0 directional, 1..4 point; the model light: directional) never select them.
void SceneRenderer::uploadLighting(const RenderState& st, const Mat4& meshWorld, const Mat4& modelMatrix) {
    (void)meshWorld;
    (void)modelMatrix;
    int lit = (lightEnv_ != nullptr && st.lighting) ? 1 : 0;
    if (lit == 0) {
        rlSetUniform(locLit_, &lit, RL_SHADER_UNIFORM_INT, 1);
        return;
    }
    static const int kModeTable[6] = {0, 1, 4, 5, 3, 2};   // 0x4845a0
    const int igMode = st.materialMode;
    const bool cm = igMode != 0;                           // cap 0xc
    const int ll = (igMode >= 0 && igMode < 6) ? kModeTable[igMode] : 0;
    const bool b8 = ll == 1 || ll == 5, b3 = ll != 4, b4 = ll != 5, b6 = ll != 2;
    const LightEnv& env = *lightEnv_;
    const std::array<float, 4>& am = st.ambient;
    const std::array<float, 4>& dm = st.diffuse;
    const std::array<float, 4>& em = st.emission;
    const std::array<float, 4>& g = env.globalAmbient;
    // FUN_00259100: split the enabled lights (slot order)
    const SceneLight* dirL[4] = {};
    const SceneLight* locL[8] = {};
    int nDir = 0, nLoc = 0, total = 0;
    for (const SceneLight& L : env.lights) {
        ++total;
        if (L.type == 0) {
            if (nDir < 4) dirL[nDir++] = &L;
        } else if (nLoc < 8) {
            locL[nLoc++] = &L;
        }
    }
    if (nLoc > 0) lit = 2;
    rlSetUniform(locLit_, &lit, RL_SHADER_UNIFORM_INT, 1);
    float dirs[4][3] = {};
    float cols[4][4] = {};
    for (int n = 0; n < nDir; ++n) {
        const SceneLight& L = *dirL[n];
        float d[3] = {-L.direction[0], -L.direction[1], -L.direction[2]};
        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l > 0.0f)
            for (float& c : d) c /= l;   // FUN_00380850 normalises the stored -direction
        for (int k = 0; k < 3; ++k) dirs[n][k] = d[k];
    }
    const int mode = !cm ? 0 : (ll == 5 ? 5 : ll == 4 ? 4 : ll == 1 ? 1 : ll == 2 ? 2 : 0);
    rlSetUniform(locLitMode_, &mode, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(locLDir_, &dirs[0][0], RL_SHADER_UNIFORM_VEC3, 4);
    if (lit == 2) {
        // path 0x100 (FUN_0025b9f0 / FUN_0025bdd0 packing, LIGHT_LOCAL_OBJ_* stages)
        float aSum[3];
        for (int k = 0; k < 3; ++k) aSum[k] = g[size_t(k)];
        for (int n = 0; n < nDir; ++n) {
            for (int k = 0; k < 3; ++k) {
                cols[n][k] = dirL[n]->diffuse[size_t(k)];
                aSum[k] = aSum[k] + dirL[n]->ambient[size_t(k)];
            }
        }
        float pPos[8][3] = {}, pAtt[8][3] = {}, pDif[8][3] = {}, pAmb[8][3] = {};
        for (int i = 0; i < nLoc; ++i) {
            const SceneLight& L = *locL[i];
            for (int k = 0; k < 3; ++k) {
                pPos[i][k] = L.position[size_t(k)];
                pAtt[i][k] = L.attenuation[size_t(k)];
                pDif[i][k] = L.diffuse[size_t(k)];
                pAmb[i][k] = L.ambient[size_t(k)];
            }
        }
        const float mA[4] = {am[0] * 255.0f, am[1] * 255.0f, am[2] * 255.0f, am[3] * 255.0f};
        const float mD[4] = {dm[0] * 255.0f, dm[1] * 255.0f, dm[2] * 255.0f, dm[3] * 128.0f};
        const float mE[3] = {em[0] * 255.0f, em[1] * 255.0f, em[2] * 255.0f};
        const int track[3] = {cm && (ll == 1 || ll == 5) ? 1 : 0, cm && (ll == 4 || ll == 5) ? 1 : 0,
                              cm && ll == 2 ? 1 : 0};
        const int single = total == 1 ? 1 : 0;
        rlSetUniform(locLCol_, &cols[0][0], RL_SHADER_UNIFORM_VEC4, 4);
        rlSetUniform(locPCount_, &nLoc, RL_SHADER_UNIFORM_INT, 1);
        rlSetUniform(locPPos_, &pPos[0][0], RL_SHADER_UNIFORM_VEC3, 8);
        rlSetUniform(locPAtt_, &pAtt[0][0], RL_SHADER_UNIFORM_VEC3, 8);
        rlSetUniform(locPDif_, &pDif[0][0], RL_SHADER_UNIFORM_VEC3, 8);
        rlSetUniform(locPAmb_, &pAmb[0][0], RL_SHADER_UNIFORM_VEC3, 8);
        rlSetUniform(locMA_, mA, RL_SHADER_UNIFORM_VEC4, 1);
        rlSetUniform(locMD_, mD, RL_SHADER_UNIFORM_VEC4, 1);
        rlSetUniform(locME_, mE, RL_SHADER_UNIFORM_VEC3, 1);
        rlSetUniform(locTrack_, track, RL_SHADER_UNIFORM_IVEC3, 1);
        rlSetUniform(locASum_, aSum, RL_SHADER_UNIFORM_VEC3, 1);
        rlSetUniform(locPSingle_, &single, RL_SHADER_UNIFORM_INT, 1);
        return;
    }
    // path 0x80 (FUN_00259a10 tail + FUN_0025b760). UNVERIFIED: more than 4 lights would take path 0x2000.
    float base[4];
    if (!cm || (!b8 && b6)) {
        for (int k = 0; k < 4; ++k) base[k] = (em[size_t(k)] + am[size_t(k)] * g[size_t(k)]) * 255.0f;
    } else if (b8) {
        for (int k = 0; k < 4; ++k) base[k] = g[size_t(k)];
    } else {
        for (int k = 0; k < 4; ++k) base[k] = am[size_t(k)] * 255.0f * g[size_t(k)];
    }
    for (int n = 0; n < nDir; ++n) {
        const SceneLight& L = *dirL[n];
        // ambient: base += La * (Am * 255) (material) or + La (tracked)
        for (int k = 0; k < 4; ++k) {
            const float la = L.ambient[size_t(k)];
            if (!cm || !b8) base[k] = base[k] + la * (am[size_t(k)] * 255.0f);
            else base[k] = base[k] + la;
        }
        if (!cm || (b3 && b4)) {
            for (int k = 0; k < 3; ++k) cols[n][k] = dm[size_t(k)] * 255.0f * L.diffuse[size_t(k)];
            cols[n][3] = dm[3] * 128.0f;
        } else {
            for (int k = 0; k < 3; ++k) cols[n][k] = L.diffuse[size_t(k)];
            cols[n][3] = L.diffuse[3];
        }
    }
    for (int i = nDir; i < 4; ++i) {   // unused slot: (0, 0, 0, Dm.w * 128)
        cols[i][3] = dm[3] * 128.0f;
    }
    float lb[3], le[3] = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < 3; ++k) lb[k] = base[k] <= 255.0f ? base[k] : 255.0f;
    if (cm && b8)
        for (int k = 0; k < 3; ++k) le[k] = em[size_t(k)] * 255.0f;
    rlSetUniform(locLCol_, &cols[0][0], RL_SHADER_UNIFORM_VEC4, 4);
    rlSetUniform(locLBase_, lb, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(locLEmis_, le, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(locLAlpha_, &cols[0][3], RL_SHADER_UNIFORM_FLOAT, 1);
}

void SceneRenderer::end() {
    rlColorMask(true, true, true, true);
    rlDisableVertexArray();
    rlDisableTexture();
    rlDisableShader();
    // restore raylib defaults
    rlEnableColorBlend();
    rlSetBlendMode(RL_BLEND_ALPHA);
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
    rlSetCullFace(RL_CULL_FACE_BACK);
}

}  // namespace baroque::alchemy
