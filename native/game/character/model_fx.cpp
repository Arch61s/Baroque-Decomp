// Model wrapper presentation: material fader, shadow flags, part submission (see model_fx.hpp).
#include "game/character/model_fx.hpp"

#include "game/actor_anim.hpp"
#include "game/character/character.hpp"
#include "game/dungeon.hpp"
#include "game/event/event_manager.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/present/present_draw.hpp"

#include <cmath>
#include <memory>
#include <string_view>
#include <vector>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

// The engine clock lRam004e78e0 (ns) is also the igTransform input clock of the renderer.
const bool gSceneClock = (alchemy::setSceneTimeSource(&ActorAnim::clock), true);

// X: drop igBlendStateAttr / igAlphaStateAttr / igAlphaFunctionAttr (the FUN_0031afe0 scans + FUN_002e7f00 removals)
void dropBlendAlpha(alchemy::AttrOverride& o) {
    o.blendStateSet = false;
    o.alphaStateSet = false;
    o.alphaFuncSet = false;
}
void dropAlpha(alchemy::AttrOverride& o) {
    o.alphaStateSet = false;
    o.alphaFuncSet = false;
}

// orig FUN_0010af90(fader, mode, group): first call builds the two switch / override-set branches; every call sets
// the mode's attribute values and switch bits.
void faderSetup(MaterialFader& f, uint32_t mode) {
    if (!f.built) {
        f.built = true;
        f.x = alchemy::AttrOverride{};
        f.x.colorMaskSet = true;                       // FUN_00296030 vt+0x84(0, 0, 0, 0)
        f.x.colorMask = {false, false, false, false};
        f.x.materialModeSet = true;                    // FUN_00291680 +0xc = 0
        f.x.materialMode = 0;
        f.x.lightingSet = true;                        // FUN_00291e50 vt+0x84(1)
        f.x.lighting = true;
        f.y = alchemy::AttrOverride{};
        f.y.materialSet = true;                        // FUN_00291890 (values written by FUN_0010c520)
        f.y.materialModeSet = true;                    // FUN_00291680 +0xc = 0 / 2 (mode & 0x80)
        f.y.materialMode = (mode & 0x80u) != 0 ? 2 : 0;
        f.y.blendFuncSet = true;                       // FUN_00297470 vt+0x84(4) vt+0x88(1)
        f.y.blendSrc = 4;
        f.y.blendDst = 1;
        f.y.blendStateSet = true;                      // FUN_00296ca0 vt+0x84(1)
        f.y.blendEnabled = true;
        f.y.depthWriteSet = true;                      // FUN_00295020 vt+0x84(0)
        f.y.depthWrite = false;
        // FUN_003dee90(&n, group, 0): the model graph leaves the transform; it is drawn through X / Y only
    }
    f.flags = mode | (f.flags & 0x80000000u);
    const uint32_t m = f.flags & 3u;
    if (m == 3 || m == 2) {
        f.swA = true;
        f.swB = true;
        f.x.colorMask = {false, false, false, false};
        f.x.materialMode = 0;
        f.x.lighting = false;
        f.y.materialMode = 0;
        f.y.blendSrc = 4;
        f.y.blendDst = m == 2 ? 5 : 1;
        if (!f.x.blendStateSet) {                      // no igBlendStateAttr in X: FUN_00296ca0 vt+0x84(1)
            f.x.blendStateSet = true;
            f.x.blendEnabled = true;
        }
        if (!f.x.alphaStateSet) {                      // no igAlphaStateAttr in X: FUN_002988a0(1) + FUN_00298a70(5, 0.0)
            for (alchemy::AttrOverride* o : {&f.x, &f.y}) {
                o->alphaStateSet = true;
                o->alphaTest = true;
                o->alphaFuncSet = true;
                o->alphaFunc = 5;
                o->alphaRef = 0.0f;
            }
        }
    } else if (m == 1) {
        f.swA = true;
        f.swB = true;
        f.x.colorMask = {true, true, true, true};
        f.x.materialMode = 3;
        f.x.lighting = true;
        f.y.materialMode = 0;
        f.y.blendSrc = 4;
        f.y.blendDst = 1;
        dropBlendAlpha(f.x);
        dropAlpha(f.y);
    } else {
        f.swA = false;
        f.swB = true;
        f.y.blendSrc = 4;
        f.y.blendDst = 1;
        f.y.materialMode = (f.flags & 0x80u) != 0 ? 2 : 0;
        dropBlendAlpha(f.x);
        dropAlpha(f.y);
    }
}

// orig FUN_0010c470(fader, flags, frames, from, to)
void faderStart(MaterialFader& f, uint32_t flags, int frames, const float* from, const float* to) {
    f.flags = flags | (f.flags & 0x80000000u);
    if (frames == 0) frames = 1;
    f.count = 0;
    f.frames = frames;
    if (from != nullptr)
        for (size_t i = 0; i < 4; ++i) f.cur[i] = from[i];
    f.from = f.cur;
    for (size_t i = 0; i < 4; ++i) f.to[i] = to[i];
}

// orig FUN_0010c520 (work/decompiled/0010c520.c): one colour step per engine clock value. Returns 1 when the fade is
// complete and flagged 0x20 (auto-remove).
int faderStep(MaterialFader& f) {
    if (!f.built) return 0;
    uint32_t u;
    if (f.count == f.frames) {
        if ((f.flags & 0x20u) != 0) return 1;
        u = static_cast<uint32_t>(f.count);
    } else {
        if (f.lastClock == ActorAnim::clock()) return 0;
        f.lastClock = ActorAnim::clock();
        f.count = f.count + 1;
        u = static_cast<uint32_t>(f.count);
    }
    const float t = static_cast<float>(u) / static_cast<float>(static_cast<uint32_t>(f.frames));
    const float s = 1.0f - t;
    std::array<float, 4> c{f.to[0] * t + f.from[0] * s, f.from[1] * s + f.to[1] * t, f.from[2] * s + f.to[2] * t,
                           f.from[3] * s + f.to[3] * t};
    for (int i = 0; i < 3; ++i) {   // only r, g, b are folded / clamped
        if (c[size_t(i)] < 0.0f) c[size_t(i)] = -c[size_t(i)];
        if (1.0f < c[size_t(i)]) c[size_t(i)] = 1.0f;
    }
    float flick = 1.0f;
    if ((f.flags & 0x10u) != 0) {   // FUN_002071e0(0x53eb20, 0x8000)
        const float r = static_cast<float>(static_cast<int>(gameWorld().rng.rand(0x8000)));
        flick = 1.0f - (r / 32768.0f) * 0.3f;
    }
    const uint32_t m = f.flags & 3u;
    if (m == 3 || m == 2) {
        c[3] = c[3] * flick;
        f.y.diffuse = c;                     // material vt+0x88
        f.cur = c;
        // vt+0x90: ambient = the first light of the renderer's light list (iRam004e7964 +0x1c) at the meta offset of
        // iRam00558d28 (UNVERIFIED: the port has no lighting; the ambient value is not used)
        f.y.emission = {0.0f, 0.0f, 0.0f, 1.0f};   // vt+0x8c
    } else {
        c[3] = 1.0f;
        c[0] = c[0] * flick;
        c[1] = c[1] * flick;
        c[2] = c[2] * flick;
        f.cur = {c[0], c[1], c[2], 1.0f};
        f.y.diffuse = c;                     // vt+0x88
        f.y.ambient = c;                     // vt+0x90
        f.y.emission = {f.cur[0] * 0.5f, f.cur[1] * 0.5f, f.cur[2] * 0.5f, 1.0f};   // DAT_0042c940 = 0.5, vt+0x8c
    }
    f.flags &= 0xbfffffffu;
    if (m == 3 || m == 2) {
        if (c[3] != 0.0f) {
            f.swA = true;
            f.swB = true;
        } else {
            f.swA = false;
            f.swB = false;
            f.flags |= 0x40000000u;
        }
        return 0;
    }
    if (m == 1) return 0;
    if (c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f) {
        f.swA = false;
        f.swB = false;
        f.flags |= 0x40000000u;
    } else {
        f.swA = false;
        f.swB = true;
    }
    return 0;
}

}  // namespace

// orig FUN_00109f10
void modelShadowOn(ChrModel& m, bool clearNever) {
    if (clearNever) m.flags2c &= 0xefffffffu;
    if ((m.flags2c & 0x30000000u) == 0) m.flags2c |= 0x20000000u;   // FUN_003ded10(0x4e79ec, +8)
}

// orig FUN_00109f70
void modelShadowOff(ChrModel& m, bool never) {
    if ((m.flags2c & 0x20000000u) != 0) m.flags2c &= 0xdfffffffu;   // FUN_003df010(0x4e79ec, +8)
    if (never) m.flags2c |= 0x10000000u;
}

bool modelShadowCaster(const ChrModel& m) { return (m.flags2c & 0x20000000u) != 0; }
bool modelInFader(const ChrModel& m) { return m.fader.exists; }

namespace {

// Shadow pre-pass caster traversal state (FUN_001113d0, see model_fx.hpp).
struct CasterPass {
    bool active = false;
    const ChrModel* model = nullptr;   // model announced by the last modelBeginDraw
    bool swapSkin = false;             // its task is of type < 3 with the model loaded (the skin loop of FUN_001113d0)
    bool player = false;               // its task is the player 0x50b144 (attached skins removed)
    bool sawNonCharacter = false;      // PORT: a task of type >= 3 was announced since the last mark
    uint64_t frame = 0;
};
CasterPass gCaster;

// PORT: the "sh" skin model of an actor file (SceneModel::buildSkin), built on first use and dropped when unused.
// The "sh" skins have no texture (igTextureStateAttr off in every CHR file), so no texture library is bound.
struct ShSkin {
    const alchemy::IgbFile* file = nullptr;
    size_t slotCount = 0;                       // identity check of the file behind the pointer
    int skinSlot = -1;
    const alchemy::IgObject* skin = nullptr;
    std::unique_ptr<alchemy::SceneModel> model; // null: the file has no "sh" skin
    uint64_t lastUse = 0;
};
std::vector<ShSkin> gShSkins;

const alchemy::IgObject* findSkin(const alchemy::IgbFile& f, std::string_view name) {
    for (const alchemy::IgObject* db : f.find("igAnimationDatabase"))
        for (const alchemy::IgObject* s : f.listObjects(f.object(db->getRef("_skinList"))))
            if (s != nullptr && s->getString("_name") == name) return s;
    return nullptr;
}

// orig FUN_001113d0 skin loop: FUN_0023f3fc(0x42ca00 "sh", skin name) over (model +0x38)+0x1c, FUN_00326280(
// appearance +0xc, skin).
alchemy::SceneModel* shSkinFor(const ChrModelPart& part) {
    const alchemy::IgbFile* f = part.file.get();
    if (f == nullptr) return nullptr;
    for (ShSkin& s : gShSkins) {
        if (s.file != f) continue;
        const bool same = s.slotCount == f->slots().size() && (s.skinSlot < 0 || f->object(s.skinSlot) == s.skin);
        if (!same) {
            s.file = nullptr;   // another file now lives at this address
            continue;
        }
        s.lastUse = gCaster.frame;
        return s.model.get();
    }
    ShSkin s;
    s.file = f;
    s.slotCount = f->slots().size();
    s.lastUse = gCaster.frame;
    if (const alchemy::IgObject* sk = findSkin(*f, "sh")) {
        s.skin = sk;
        s.skinSlot = sk->slot;
        auto m = std::make_unique<alchemy::SceneModel>();
        if (m->buildSkin(*f, "sh", {}, nullptr)) s.model = std::move(m);
    }
    gShSkins.push_back(std::move(s));
    return gShSkins.back().model.get();
}

void casterAnnounce(const ChrModel& m) {
    gCaster.model = &m;
    gCaster.swapSkin = false;
    gCaster.player = false;
    for (const Chr* c : characters().list()) {
        if (&c->model != &m) continue;
        // task walk of FUN_001113d0: tasks flagged 0x10000000 are skipped; type < 3 with +0x2c bit 0x80000000
        gCaster.swapSkin = (c->taskFlags & 0x10000000u) == 0 && static_cast<uint32_t>(c->type) < 3u && m.loaded();
        gCaster.player = c == characters().player();
        if (static_cast<uint32_t>(c->type) >= 3u) gCaster.sawNonCharacter = true;
        break;
    }
}

}  // namespace

void modelShadowCasterPassBegin() {
    gCaster.active = true;
    gCaster.model = nullptr;
    ++gCaster.frame;
}

bool modelShadowCasterPassSawObjects(bool reset) {
    const bool saw = gCaster.sawNonCharacter;
    if (reset) gCaster.sawNonCharacter = false;
    return saw;
}

void modelShadowCasterPassEnd() {
    gCaster.active = false;
    gCaster.model = nullptr;
    // PORT: release the skin models of files not drawn for 300 passes
    std::erase_if(gShSkins, [](const ShSkin& s) { return s.file == nullptr || s.lastUse + 300u < gCaster.frame; });
}

// orig FUN_0010a0a0 -> FUN_00109f70(model, 0), FUN_00109ff0 (FUN_0010cb30 + FUN_0010af90), FUN_0010c470, FUN_0010c520
void modelFadeStart(ChrModel& m, uint32_t mode, int frames, const float* from, const float* to) {
    modelShadowOff(m, false);
    MaterialFader& f = m.fader;
    if (!f.exists) {   // FUN_0010cb30: +4 = 0, +0x3c.. = (0, 0, 0, 1)
        f = MaterialFader{};
        f.exists = true;
    }
    faderSetup(f, mode);
    faderStart(f, mode, frames, from, to);
    faderStep(f);
}

// orig FUN_0010a130 -> FUN_00109f10(model, 0), FUN_0010a060 (FUN_0010c320: the model graph back under the transform)
void modelFadeEnd(ChrModel& m) {
    modelShadowOn(m, false);
    m.fader = MaterialFader{};
}

// orig FUN_0010a160
void modelPublishFx(ChrModel& m) {
    if (m.fader.exists && faderStep(m.fader) != 0) modelFadeEnd(m);
}

namespace {
alchemy::LightEnv gEnv;
bool gPresentLightSet = false;      // PORT: presentation override of the main light direction (present_draw.hpp)
float gPresentLightDir[3] = {0.0f, 0.0f, 0.0f};
}

void modelSetPresentLightDir(const float* dir3) {
    gPresentLightSet = dir3 != nullptr;
    if (dir3 != nullptr)
        for (int k = 0; k < 3; ++k) gPresentLightDir[k] = dir3[k];
}

// The lit branch's lights in the low-level slot order (render list lights first: they are enabled at floor setup,
// the model light later). Disabled lights are skipped (FUN_00380520 frees their low-level slot).
void modelBeginDraw(const ChrModel& m, alchemy::SceneRenderer& r) {
    if (gCaster.active) casterAnnounce(m);
    if (!m.lit) {
        r.setLighting(nullptr);
        return;
    }
    gEnv.lights.clear();
    const RenderLights& rl = renderLights();
    for (int i = 0; i < 5; ++i) {
        const LightSlot& s = rl.slot[i];
        if (s.enabled == 0) continue;   // igLightStateAttr vt+0x84 (FUN_00108b10); allocation (+0x50) is bookkeeping
        alchemy::SceneLight L;
        L.type = i == 0 ? 0 : 1;   // FUN_00108960: slot 0 +0xc = 0 (directional), slots 1..4 = 1 (point)
        for (int k = 0; k < 4; ++k) {
            L.ambient[size_t(k)] = s.ambient[k];
            L.diffuse[size_t(k)] = s.color[k];
            L.specular[size_t(k)] = s.specular[k];
        }
        for (int k = 0; k < 3; ++k) {
            L.direction[size_t(k)] = s.dir[k];
            L.position[size_t(k)] = s.pos[k];
            L.attenuation[size_t(k)] = s.atten[k];
        }
        if (gPresentLightSet && i == rl.mainSlot)   // PORT: light follows the presented (interpolated) camera
            for (int k = 0; k < 3; ++k) L.direction[size_t(k)] = gPresentLightDir[k];
        if (i != 0 && L.attenuation[0] == 0.0f && L.attenuation[1] == 0.0f && L.attenuation[2] == 0.0f)
            L.attenuation[0] = 1.0f;   // igLightAttr default (1, 0, 0) when the slot was never written
        gEnv.lights.push_back(L);
    }
    if (m.light.exists && m.light.enabled) {
        alchemy::SceneLight L;
        L.type = 0;
        L.diffuse = m.light.diffuse;
        L.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
        L.specular = {0.0f, 0.0f, 0.0f, 1.0f};
        L.direction = m.light.direction;
        gEnv.lights.push_back(L);
    }
    r.setLighting(&gEnv);
}

void modelEndDraw(alchemy::SceneRenderer& r) { r.setLighting(nullptr); }

// orig FUN_0011f160 (work/decompiled/0011f160.c). Room lights: RDT records {type, x, y, z, r, g, b, radius} at the
// room's position (room +0xc / +0xe * 8 * 200, no +100); direction table 0x48db90 (static init 0x4c3290):
// 0 (0,-1,0), 1 (0,1,0), 2 (0,0,1), 3 (-1,0,0), 4 (1,0,0), 5 (0,0,-1); 6 = normalise(character - light).
void chrRoomLights() {
    static const float kDirs[8][3] = {{0, -1, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 0}, {0, 0, 0}};
    const Dungeon* d = gameWorld().dungeon;
    if (d == nullptr || (d->floorFlags() & 1u) == 0) return;   // uRam0050a610 & 1
    for (Chr* c : characters().list()) {
        if ((c->taskFlags & 0x10000000u) != 0) continue;
        if (static_cast<uint32_t>(c->type) >= 3u || c->cell == nullptr) continue;
        const uint32_t word = *c->cell;
        if ((word & 0xf000u) != 0x1000u) continue;
        const int room = static_cast<int>(word & 0xfu);
        if (room >= d->roomCount()) continue;
        ModelLight& ml = c->model.light;
        if (!ml.exists) continue;   // FUN_0031afe0(parent, igLightStateSet meta 0x55df60)
        const Room& rm = d->room(room);
        const float ox = static_cast<float>(static_cast<int>(rm.posX)) * 8.0f * 200.0f;
        const float oz = static_cast<float>(static_cast<int>(rm.posY)) * 8.0f * 200.0f;
        int count = rm.rdt.lightCount;
        if (d->fdt().mapSet == 6 && !flags().test(0x49)) {   // *pcRam0050a620 == 6 && FUN_00178a80(0x53c3b0, 0x49) == 0
            const uint8_t k = rm.rdt.roomId;
            if (k == 0x12 || k == 0x11 || k == 0x10 || k == 6) count = 0;
            else if (k == 0xe || k == 0) count = 1;
        }
        const Vec3f& p = c->xf.m.pos;
        const data::RdtLight* best = nullptr;
        float bestD = 99999.0f;
        for (int i = 0; i < count && i < static_cast<int>(rm.rdt.lights.size()); ++i) {
            const data::RdtLight& L = rm.rdt.lights[size_t(i)];
            const float dy = p[1] - L.y;
            const float dx = p[0] - (L.x + ox);
            const float dz = p[2] - (L.z + oz);
            float dist = dz * dz + dx * dx + dy * dy;
            dist = dist == 0.0f ? 0.0f : std::sqrt(dist);
            dist = dist - L.radius;
            if (!(bestD <= dist)) {
                bestD = dist;
                best = &L;
            }
        }
        if (best == nullptr) {
            ml.enabled = false;   // state vt+0x84(0)
            continue;
        }
        const float vy = p[1] - best->y;
        const float vx = p[0] - (best->x + ox);
        const float vz = p[2] - (best->z + oz);
        float dist = vz * vz + vx * vx + vy * vy;
        dist = dist == 0.0f ? 0.0f : std::sqrt(dist);
        if (!(dist <= best->radius)) {
            ml.enabled = false;
            continue;
        }
        ml.enabled = true;
        float f = 1.0f;
        const float quarter = best->radius * 0.25f;
        const float inside = best->radius - dist;
        if (inside < quarter) f = inside / quarter;
        ml.diffuse = {best->r * f, best->g * f, best->b * f, 1.0f};
        if (best->type == 6) {
            float v[3] = {vx, vy, vz};
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);   // FUN_00206ab0
            if (l > 0.0f)
                for (float& x : v) x /= l;
            ml.direction = {v[0], v[1], v[2]};
        } else {
            const uint32_t t = best->type < 8 ? best->type : 0;   // UNVERIFIED: the original indexes the table freely
            ml.direction = {kDirs[t][0], kDirs[t][1], kDirs[t][2]};
        }
    }
}

void modelSubmitPart(const ChrModel& m, const ChrModelPart& part, const alchemy::Mat4& world,
                     alchemy::SceneRenderer& r, int pass) {
    if (!part.model) return;
    if (&part == m.coatShadow.get()) {
        // player +0x6c (FUN_001b2230): FUN_001113d0 attaches it to the appearance for the caster traversal only
        // (FUN_003c7cb0 at 0x111868, FUN_003c7cc0 after the pass); skinned on the actor's skeleton like the coat
        if (!gCaster.active || !gCaster.player || &m != gCaster.model || !modelShadowCaster(m)) return;
        if (m.pose.empty()) part.model->resetPose();
        else part.model->setPose(m.pose);
        r.drawPass(*part.model, world, pass, nullptr);
        return;
    }
    const alchemy::SceneModel* draw = part.model.get();
    // appearance skin of the body: the "lod" skin when the tick's FUN_00108d20 swapped it in (character.hpp
    // chrSkinLodTick; the pre-pass traverses the skin left by the previous pass, the scene the one of this tick)
    if (part.lodModel && &part == &m.parts.front() && (gCaster.active ? m.lodCaster : m.lodDraw)) draw = part.lodModel.get();
    if (gCaster.active) {   // shadow pre-pass FUN_001113d0: only the models linked to the caster root 0x4e79ec
        if (!modelShadowCaster(m)) return;
        if (&m == gCaster.model) {
            const bool first = &part == &m.parts.front();   // the actor (appearance skin)
            // player: appearance +0x10 attached skins removed for the pass (FUN_003c7cc0 loop at 0x111818)
            if (gCaster.player && !first && !part.actor && part.attachBone.empty() && part.attachBoneIndex < 0) return;
            // the coat shadow skin player +0x6c (FUN_003c7cb0 at 0x111868) is ChrModel::coatShadow (see above)
            if (first && gCaster.swapSkin) {
                if (alchemy::SceneModel* sh = shSkinFor(part)) {
                    if (m.pose.empty()) sh->resetPose();
                    else sh->setPose(m.pose);
                    draw = sh;
                }
            }
        }
    }
    const alchemy::AttrOverride* own = part.hasOverride ? &part.ovr : nullptr;
    auto submit = [&](const alchemy::AttrOverride* inner) {
        if (!m.fader.exists || !m.fader.built) {
            r.drawPass(*draw, world, pass, inner);
            return;
        }
        // the fader's override sets sit above the whole model: they win over the part's own sets
        for (int b = 0; b < 2; ++b) {
            if (!(b == 0 ? m.fader.swA : m.fader.swB)) continue;
            const alchemy::AttrOverride& fo = b == 0 ? m.fader.x : m.fader.y;
            const alchemy::AttrOverride eff = inner != nullptr ? fo.compose(*inner) : fo;
            r.drawPass(*draw, world, pass, &eff);
        }
    };
    submit(own);
    if (part.hasGlow && part.glowOn) {   // E09: igSwitch (bit 0) -> igOverrideAttrSet -> the same graph
        const alchemy::AttrOverride g = own != nullptr ? part.glow.compose(*own) : part.glow;
        submit(&g);
    }
}

}  // namespace baroque::game
