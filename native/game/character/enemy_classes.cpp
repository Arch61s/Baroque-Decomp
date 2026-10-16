// Character classes of the factory table 0x44aa10 (ENEMIES_COMBAT.md §1.3/§5, work/reports/ENEMIES_NATIVE.md):
// class lookup, model path FUN_0012b620 and the Activate (vt+0x30) / ClassTick (vt+0x34) methods of the nine bespoke
// enemy classes E01 E02 E05 E07 E09 E19 E22 E23 E27.
#include "game/character/character.hpp"

#include "game/character/ai.hpp"
#include "game/character/combat.hpp"
#include "game/character/npc_class.hpp"
#include "game/game_world.hpp"
#include "game/present/present_draw.hpp"
#include "platform/platform.hpp"

#include <bit>
#include <cmath>
#include <cstring>

namespace baroque::game {

namespace {

#include "game/character/trig_lut.inc"

constexpr uint32_t tag4(const char* s) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}

// Factory table 0x44aa10 (103 x {u32 tag, thunk}); tags written by the static init FUN_004c3340 from the strings at
// 0x44ad48.., thunks 0x12d5c0..0x12d6c0 jump to the ctor wrappers (disasm 0x12d5d0..0x12d6c0). The J/O entries
// (thunks 0x12d410..0x12d5c0) are object classes and are not listed here (no character class).
struct ClassEntry {
    const char* tag;
    ChrClass cls;
};
constexpr ClassEntry kClassTable[] = {
    {"C00A", ChrClass::Player},  {"C00B", ChrClass::Npc},     {"C00C", ChrClass::Npc},     {"C01A", ChrClass::Player},
    {"C01C", ChrClass::Npc},     {"C02A", ChrClass::Npc},     {"C02B", ChrClass::Npc},     {"C03A", ChrClass::NpcC03A},
    {"C03B", ChrClass::Npc},     {"C04A", ChrClass::Npc},     {"C04B", ChrClass::Npc},     {"C04C", ChrClass::Npc},
    {"C04D", ChrClass::Npc},     {"C04E", ChrClass::Npc},     {"C07A", ChrClass::Npc},     {"C08A", ChrClass::NpcC08},
    {"C08B", ChrClass::NpcC08},  {"C08C", ChrClass::NpcC08},  {"C09A", ChrClass::Npc},     {"C11A", ChrClass::Npc},
    {"C12A", ChrClass::NpcC12A}, {"C14A", ChrClass::Npc},     {"C14B", ChrClass::Npc},     {"C18A", ChrClass::NpcC18A},
    {"C19A", ChrClass::Npc},     {"C25A", ChrClass::Npc},     {"C25B", ChrClass::Npc},     {"C26A", ChrClass::Npc},
    {"C26B", ChrClass::Npc},     {"C27A", ChrClass::NpcC27A}, {"C28A", ChrClass::Npc},     {"E01A", ChrClass::E01},
    {"E01B", ChrClass::E01},     {"E01C", ChrClass::E01},     {"E02A", ChrClass::E02},     {"E02B", ChrClass::E02},
    {"E02C", ChrClass::E02},     {"E05A", ChrClass::E05},     {"E05B", ChrClass::E05},     {"E05C", ChrClass::E05},
    {"E07A", ChrClass::E07},     {"E07B", ChrClass::E07},     {"E07C", ChrClass::E07},     {"E09A", ChrClass::E09},
    {"E09B", ChrClass::E09},     {"E09C", ChrClass::E09},     {"E09D", ChrClass::E09},     {"E19A", ChrClass::E19},
    {"E19B", ChrClass::E19},     {"E19C", ChrClass::E19},     {"E22A", ChrClass::E22},     {"E22B", ChrClass::E22},
    {"E22C", ChrClass::E22},     {"E23A", ChrClass::E23},     {"E23B", ChrClass::E23},     {"E23C", ChrClass::E23},
    {"E27A", ChrClass::E27},     {"E27B", ChrClass::E27},     {"E27C", ChrClass::E27},
};

std::string idString(uint32_t id) {
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i) s[size_t(i)] = char((id >> (8 * i)) & 0xff);
    return s;
}

// FUN_0010aaf0 time / 0x1fca055 (FUN_00244f38): current animation frame number
int animFrame(const ChrModel& m) { return static_cast<int>(m.anim.currentTimeNs() / 0x1fca055); }

// FUN_0023f068: case-insensitive string compare (ctype table 0x49fbb1, upper -> lower), 0 when equal
bool sameNameNoCase(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        char x = a[i], y = b[i];
        if ('A' <= x && x <= 'Z') x = static_cast<char>(x + 0x20);
        if ('A' <= y && y <= 'Z') y = static_cast<char>(y + 0x20);
        if (x != y) return false;
    }
    return i == a.size() && b[i] == '\0';
}

// actor+0x48 (igAnimationDatabase skeleton) vt+0x70 = FUN_003f6990: joint index by name, -1 when absent
int jointIndex(const ChrModel& m, const char* name) {
    const alchemy::Skeleton* sk = m.skeleton();
    if (sk == nullptr) return -1;
    for (size_t i = 0; i < sk->bones.size(); ++i)
        if (sameNameNoCase(sk->bones[i].name, name)) return static_cast<int>(i);
    platform::log(std::string("chr: joint ") + name + " not in skeleton " + sk->name);
    return -1;
}

// FUN_0039dbc0(matrix, xyz): scale part of a matrix object (the modifiers' matrices carry no rotation)
void setScale(alchemy::Mat4& m, float x, float y, float z) {
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
}
// FUN_0039da90(matrix, xyz): translation part
void setTranslation(alchemy::Mat4& m, float x, float y, float z) {
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
}

// FUN_003bf580 + FUN_003a4140 + FUN_0031c3b0(actor+0x30 -> +0x34, modifier): new joint modifier, identity matrix
int addJointModifier(ChrModel& m, int joint) {
    BoneMod b;
    b.joint = joint;
    m.boneMods.push_back(b);
    return static_cast<int>(m.boneMods.size()) - 1;
}

// FUN_00325420(0x55adf0, path, "igSceneInfo") resource lookup, else FUN_0010cf60(.., path, 0,0,0,0x11) load
// (synchronous here); the part is then linked into the actor's scene (FUN_003c7cb0 / FUN_003c7cf0 / joint attach).
bool loadClassPart(Chr& c, const std::string& rel, const char* jointName, bool shareSkeleton) {
    ChrModelPart part;
    if (!chrLoadPart(part, characters().assetRoot / rel, "")) return false;
    if (jointName != nullptr) {
        part.attachBoneIndex = jointIndex(c.model, jointName);
    }
    if (shareSkeleton && !part.model->skeleton().valid)
        if (const alchemy::Skeleton* sk = c.model.skeleton()) part.model->adoptSkeleton(*sk);
    c.model.parts.push_back(std::move(part));
    c.cd.partIndex = static_cast<int>(c.model.parts.size()) - 1;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// E01
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001c0cf0 (string 0x4637c8 "Dummy")
void e01Activate(Chr& c) {
    c.cd.modIndex = addJointModifier(c.model, jointIndex(c.model, "Dummy"));
    c.cd.f48 = 0.01f;    // 0x3c23d70a
    c.cd.f4c = 0.05f;    // 0x3d4ccccd
    c.flags24 |= 1u;
}

// orig FUN_001c0db0: the "Dummy" joint is scaled in x, shrinking during animations 10 and 0 and growing otherwise
void e01Tick(Chr& c) {
    ChrClassData& d = c.cd;
    const int idx = c.model.anim.currentClip();   // actor +0x58
    float target;
    if (idx == 10 || idx == 0) {
        target = 0.0f;
    } else {
        target = 1.0f;
        d.f4c = 0.05f;
    }
    if (target == 0.0f) {
        d.f48 = d.f48 - d.f4c;
        d.f4c = d.f4c + 0.005f;
        if (!(target <= d.f48)) d.f48 = target;
    } else {
        d.f48 = d.f48 + 0.0025f;
        const float s = d.f48 * 1.3f;
        d.f48 = s;
        if (target < s) d.f48 = target;
    }
    if (d.modIndex >= 0) setScale(c.model.boneMods[size_t(d.modIndex)].m, d.f48, 1.0f, 1.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// E02
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001c1140: DATA/CHR/%c%c%c/OBJ067.IGB (FUN_001c0ed0, format 0x4637d0) linked into the actor's scene
// (FUN_003c7cb0) under an igOverrideAttrSet (FUN_003adbf0, +0x40) with the attributes, in list order:
// [0] igBlendStateAttr (FUN_00296ca0) vt+0x84(0), [1] igMaterialAttr (FUN_00291890) diffuse vt+0x88 (0.8,0.8,0.8,1),
// ambient vt+0x90 (0,0,0,1), [2] igMaterialModeAttr (FUN_00291680) +0xc = 3, [3] igDepthWriteStateAttr
// (FUN_00295020) vt+0x84(1), [4] igBlendFunctionAttr (FUN_00297470) (4, 1).
void e02Activate(Chr& c) {
    const std::string id = c.idString();
    if (loadClassPart(c, "DATA/CHR/" + id.substr(0, 3) + "/OBJ067.IGB", nullptr, true)) {
        ChrModelPart& p = c.model.parts[size_t(c.cd.partIndex)];
        p.hasOverride = true;
        alchemy::AttrOverride& o = p.ovr;
        o.blendStateSet = true;
        o.blendEnabled = false;
        o.materialSet = true;
        o.diffuse = {0.8f, 0.8f, 0.8f, 1.0f};   // 0x3f4ccccd
        o.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
        o.emission = {0.0f, 0.0f, 0.0f, 1.0f};  // igMaterialAttr default (registration FUN_00291970)
        o.materialModeSet = true;
        o.materialMode = 3;
        o.depthWriteSet = true;
        o.depthWrite = true;
        o.blendFuncSet = true;
        o.blendSrc = 4;
        o.blendDst = 1;
    }
    c.cd.f4c = 0.8f;   // material diffuse r=g=b
    c.flags24 |= 1u;
}

// orig FUN_001c14e0: during animation 5 frames 50..130 the overlay turns additive (blend on, material mode 0, depth
// write off) and its material diffuse (pfVar6 = material + the _diffuse meta offset 0x10, iRam00558c68) fades out by
// 0.1 per tick; otherwise it fades back to 0.8, and passing 0.8 restores blend off / mode 3 / depth write on (ambient
// (0,0,0,1) first). After each step g = b = r and the ambient is set to the diffuse (vt+0x90).
void e02Tick(Chr& c) {
    ChrClassData& d = c.cd;
    float& v = d.f4c;   // diffuse[0]
    alchemy::AttrOverride* o = nullptr;
    if (d.partIndex >= 0 && static_cast<size_t>(d.partIndex) < c.model.parts.size() && c.model.parts[size_t(d.partIndex)].hasOverride)
        o = &c.model.parts[size_t(d.partIndex)].ovr;
    auto setMaterial = [&]() {
        if (o == nullptr) return;
        o->diffuse = {v, v, v, o->diffuse[3]};
        o->ambient = o->diffuse;   // vt+0x90(material, diffuse)
    };
    if (c.model.anim.currentClip() == 5) {
        if (50.0f < static_cast<float>(animFrame(c.model)) && static_cast<float>(animFrame(c.model)) < 130.0f) {
            if (v <= 0.0f) return;
            if (o != nullptr) {
                o->blendEnabled = true;    // [0] vt+0x84(1)
                o->materialMode = 0;       // [2] +0xc = 0
                o->depthWrite = false;     // [3] vt+0x84(0)
            }
            const float f = v;
            v = f - 0.1f;
            if (f - 0.1f < 0.0f) v = 0.0f;
            setMaterial();
            return;
        }
    }
    if (v < 0.8f) {
        const float f = v + 0.1f;
        v = f;
        if (0.8f < f) {
            v = 0.8f;
            if (o != nullptr) {
                o->ambient = {0.0f, 0.0f, 0.0f, 1.0f};
                o->blendEnabled = false;
                o->materialMode = 3;
                o->depthWrite = true;
            }
        } else if (o != nullptr) {
            o->blendEnabled = true;
            o->materialMode = 0;
            o->depthWrite = false;
        }
        setMaterial();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// E05 / E27: projectile models preloaded into the resource cache (used later by the ACT projectile commands)
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001c18b0: FUN_00325420(.., "DATA/CHR/E05/E05_ICICLE.IGB" 0x463810, "igSceneInfo") else FUN_0010cf60 load.
// orig FUN_001c3fa0: same for DATA/CHR/%c%c%c/%c%c%c%c_COIN.IGB (FUN_001c3e70, format 0x463940).
// The port has no shared resource cache: the preload has no observable effect (UNVERIFIED: the projectile commands
// load the model themselves).
void preloadActivate(Chr& c) { c.flags24 |= 1u; }

// ---------------------------------------------------------------------------------------------------------------
// E07
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001c1a70: +0x40 = the igTransform reached by the fixed child chain of the actor's skinned graph
// (child 1 "PromotedAttr" / 0 igBlendMatrixSelect / 1 "mat_E07_caterpillar" / 0): a _target 9 (texture matrix)
// transform; FUN_00326280(+0x68, 0) removes its igTransformSequence input. Stored here as index + 1 in i40.
void e07Activate(Chr& c) {
    c.cd.i40 = 0;
    if (!c.model.parts.empty() && c.model.parts[0].model) {
        alchemy::SceneModel& sm = *c.model.parts[0].model;
        const int k = sm.findTransformByPath({1, 0, 1, 0});
        if (k >= 0) {
            sm.detachTransformInput(k);
            c.cd.i40 = k + 1;
        } else {
            platform::log("chr: E07 caterpillar transform not found (UNVERIFIED chain)");
        }
    }
    c.flags24 |= 1u;
}

// orig FUN_0039e160(m, &a, &b, &c): the second output (b) of the rotation decomposition = asin(-row0.z) of the
// orthonormalised rotation (row 0 flipped when the basis is left-handed); FUN_00238860 asinf.
float decompPitch(const alchemy::Mat4& mat) {
    const float* p = mat.m.data();
    float r0[3] = {p[0], p[1], p[2]}, r1[3] = {p[4], p[5], p[6]}, r2[3] = {p[8], p[9], p[10]};
    if (p[15] != 1.0f) {
        const float k = 1.0f / p[15];
        for (int i = 0; i < 3; ++i) { r0[i] *= k; r1[i] *= k; r2[i] *= k; }
    }
    float l = 1.0f / std::sqrt(r0[2] * r0[2] + r0[0] * r0[0] + r0[1] * r0[1]);
    for (float& x : r0) x *= l;
    float d = -(r0[2] * r1[2] + r0[0] * r1[0] + r0[1] * r1[1]);
    for (int i = 0; i < 3; ++i) r1[i] = d * r0[i] + r1[i] + 0.0f;
    l = 1.0f / std::sqrt(r1[2] * r1[2] + r1[0] * r1[0] + r1[1] * r1[1]);
    for (float& x : r1) x *= l;
    d = -(r0[2] * r2[2] + r0[0] * r2[0] + r0[1] * r2[1]);
    float t[3];
    for (int i = 0; i < 3; ++i) t[i] = d * r0[i] + r2[i] + 0.0f;
    const float e = -(r1[2] * t[2] + r1[0] * t[0] + r1[1] * t[1]);
    for (int i = 0; i < 3; ++i) r2[i] = e * r1[i] + t[i] + 0.0f;
    l = 1.0f / std::sqrt(r2[2] * r2[2] + r2[0] * r2[0] + r2[1] * r2[1]);
    for (float& x : r2) x *= l;
    const float cx[3] = {r1[1] * r2[2] - r1[2] * r2[1], r1[2] * r2[0] - r1[0] * r2[2], r1[0] * r2[1] - r1[1] * r2[0]};   // FUN_003a01f0
    if (r0[2] * cx[2] + r0[0] * cx[0] + r0[1] * cx[1] < 0.0f) r0[2] = -r0[2];
    float z = -r0[2];
    if (z <= -1.0f) z = -1.0f;
    if (1.0f <= z) z = 1.0f;
    return std::asin(z);
}

// orig FUN_001c1b00 (ClassTick): v = sin(pitch of the "Tire2" (0x463840) joint matrix / 1.5) (FUN_00206900),
// v < 0 -> v + 1 else 1 - v; translation (0, v, 0) of the caterpillar's texture transform (FUN_0039d980): the tread
// scrolls with the wheel. UNVERIFIED: the bone matrix cache entry is taken as the joint's actor-space matrix.
void e07Tick(Chr& c) {
    if (c.cd.i40 == 0 || c.model.parts.empty() || !c.model.parts[0].model) return;
    const int j = jointIndex(c.model, "Tire2");
    const std::vector<alchemy::Mat4>& pose = chrTickPose(c);
    if (j < 0 || static_cast<size_t>(j) >= pose.size()) return;
    float v = ps2Sin(decompPitch(pose[size_t(j)]) / 1.5f);
    if (v < 0.0f) v = v + 1.0f;
    else v = 1.0f - v;
    c.model.parts[0].model->setTransformTranslation(c.cd.i40 - 1, 0.0f, v, 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// E09
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001c1c30: "DATA/CHR/%c%c%c/%c%c%c%c_HEAD.IGB" (0x463860) with E09B -> E09A (strings 0x463848/0x463850)
std::string e09HeadPath(uint32_t id) {
    if (id == tag4("E09B")) id = tag4("E09A");
    const std::string s = idString(id);
    return "DATA/CHR/" + s.substr(0, 3) + "/" + s + "_HEAD.IGB";
}

// orig FUN_001c1f70: head model on the "Head_Joint" joint (+0x44 joint attach over the +0x40 group), joint modifier on
// "Head_joint" (+0x48/+0x4c), swell +0x50 = 1, +0x54 = 0. The group's second child is an igSwitch (child-mask bit 0
// cleared) over an igOverrideAttrSet holding the head graph again: igMaterialAttr diffuse / ambient (0,0,0,1),
// igMaterialModeAttr 0, igBlendStateAttr(1), igBlendFunctionAttr (4, 1), igDepthWriteStateAttr(0) - the swell glow.
void e09Activate(Chr& c) {
    if (loadClassPart(c, e09HeadPath(c.id), "Head_Joint", false)) {
        ChrModelPart& p = c.model.parts[size_t(c.cd.partIndex)];
        p.hasGlow = true;
        p.glowOn = false;
        alchemy::AttrOverride& g = p.glow;
        g.materialSet = true;
        g.diffuse = {0.0f, 0.0f, 0.0f, 1.0f};
        g.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
        g.emission = {0.0f, 0.0f, 0.0f, 1.0f};
        g.materialModeSet = true;
        g.materialMode = 0;
        g.blendStateSet = true;
        g.blendEnabled = true;
        g.blendFuncSet = true;
        g.blendSrc = 4;
        g.blendDst = 1;
        g.depthWriteSet = true;
        g.depthWrite = false;
    }
    c.cd.modIndex = addJointModifier(c.model, jointIndex(c.model, "Head_joint"));
    c.cd.f50 = 1.0f;
    c.cd.i54 = 0;
    c.flags24 |= 1u;
}

// orig FUN_001c2450: animation 8 swells the head (scale toward 2.0 with a 0.1/-0.08 shiver), otherwise it relaxes
// toward 1.0; below 1.1 the glow is hidden and the joint matrix is left as it was.
void e09Tick(Chr& c) {
    ChrClassData& d = c.cd;
    const float target = c.model.anim.currentClip() == 8 ? 2.0f : 1.0f;
    if (target == 1.0f) {
        d.i54 = 0;
        d.f50 = d.f50 - (d.f50 - target) / 3.5f;
    } else {
        if (target <= d.f50) d.f50 = target;
        if (d.i54 == 0) {
            d.i54 = 1;
            d.f50 = d.f50 + 0.1f;
        } else {
            d.i54 = 0;
            d.f50 = d.f50 - 0.08f;
        }
    }
    ChrModelPart* head = (d.partIndex >= 0 && static_cast<size_t>(d.partIndex) < c.model.parts.size())
                             ? &c.model.parts[size_t(d.partIndex)] : nullptr;
    if (d.f50 < 1.1f) {   // glow switch bit0 cleared
        if (head != nullptr) head->glowOn = false;
        return;
    }
    // glow switch bit0 set; every igMaterialAttr of the set: diffuse (e, e*0.2, e*0.1, 1) (vt+0x88), ambient
    // (e, e*0.5, e*0.2, 1) (vt+0x90), e = swell - 1
    if (head != nullptr && head->hasGlow) {
        head->glowOn = true;
        const float e = d.f50 - 1.0f;
        const float e2 = e * 0.2f;
        head->glow.diffuse = {e, e2, e * 0.1f, 1.0f};
        head->glow.ambient = {e, e * 0.5f, e2, 1.0f};
    }
    if (d.modIndex < 0) return;
    alchemy::Mat4& m = c.model.boneMods[size_t(d.modIndex)].m;
    setScale(m, d.f50, d.f50, d.f50);
    const float e = d.f50 - 1.0f;
    setTranslation(m, e * -10.0f, 0.0f, e * 35.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// E19
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001c2760: "DATA/CHR/%c%c%c/OBJ068%c.IGB" (0x4638c0)
std::string e19ShellPath(uint32_t id) {
    const std::string s = idString(id);
    return "DATA/CHR/" + s.substr(0, 3) + "/OBJ068" + s.substr(3, 1) + ".IGB";
}

// orig FUN_001c28a0: FUN_001099e0(+0x40 second actor, OBJ068%c, 1, +0x80 = 0x40000011)
void e19Activate(Chr& c) {
    ChrClassData& d = c.cd;
    d.shell = std::make_unique<ChrModel>();
    ChrModelPart part;
    if (chrLoadPart(part, characters().assetRoot / e19ShellPath(c.id), "")) {
        d.shell->parts.push_back(std::move(part));
        std::vector<std::string> log;
        d.shell->clips = alchemy::loadClips(*d.shell->parts[0].file, alchemy::primarySkeleton(*d.shell->parts[0].file), &log);
        d.shell->anim.init(&d.shell->clips, d.shell->skeleton());
        actorLoadDefaultAnim(d.shell->anim, d.shell->clips, *d.shell->parts[0].file);   // FUN_0010a4d0: first animation
        d.shell->flags2c |= d.shellFlags | 0x80000000u;   // FUN_00109c80 load callback
        d.shell->lit = true;                               // FUN_001099e0 mode 1
        d.shell->light.exists = (d.shellFlags & 0x4000000u) == 0;
    }
    c.flags24 |= 1u;
}

// orig FUN_0010ac60(actor): fewer than two combiner states (not blending) and the current state's local time is at
// least one animation length past the start ((localTime - keyFrameTimeOffset - startTime) / duration != 0).
bool shellAnimDone(const ChrModel& sh) {
    if (sh.anim.combiner().states().size() >= 2) return false;
    const alchemy::AnimState* st = sh.anim.currentState();
    if (st == nullptr || st->clip < 0 || static_cast<size_t>(st->clip) >= sh.clips.size()) return false;
    const alchemy::AnimClip& a = *sh.clips[size_t(st->clip)];
    if (a.durationNs == 0) return false;   // UNVERIFIED: the original returns its uninitialised argument here
    return (st->localTime - a.keyFrameTimeOffsetNs - a.startTimeNs) / a.durationNs != 0;   // FUN_003fb270
}

// orig FUN_001c28f0: the OBJ068 shell follows the body (MODE 0) and mirrors its animation; when the body plays
// animation 14 or 9 the shell finishes its animation, then collapses (y scale shrinks, sinks) and hides.
void e19Tick(Chr& c) {
    ChrClassData& d = c.cd;
    if (!d.shell) return;
    ChrModel& sh = *d.shell;
    // FUN_001075a0 -> FUN_00108d20 (3-D pass of the previous frame): the shell actor's combiner at the clock
    sh.anim.update();
    const int ph = d.shellPhase;   // +0x88
    if (ph != 0x1e) {
        if (ph == 0x14) {
            d.shellScale[1] = d.shellScale[1] + d.shellVs;
            d.shellXf.pos[1] = d.shellXf.pos[1] + d.shellVy;
            if (-0.05f < d.shellVs) d.shellVs = d.shellVs - 0.0025f;
            if (d.shellScale[1] < 0.025f) {
                d.shellScale[1] = 0.025f;   // 0x3ccccccd
                d.shellPhase = 0x1e;
                if (!sh.parts.empty()) sh.parts[0].visible = false;   // node flag bit0 cleared
            }
        } else if (ph == 10) {
            if (shellAnimDone(sh)) {
                d.shellPhase = 0x14;
                d.shellVs = 0.0f;
                d.shellVy = d.shellVy - 0.2f;
            }
        } else if (ph == 0) {
            if (((c.ai.flags & 6u) >> 1) == 0) {   // FUN_001f1730(ai, 0x1000008) == 0
                d.shellScale = c.xf.scale;          // +0x44..+0x7c = transform +0..+0x38
                d.shellXf = c.xf.m;
            }
            if (c.model.loaded() && sh.loaded()) {
                const int idx = c.model.anim.currentClip();
                if (d.shellAnim != idx) {
                    d.shellAnim = idx;
                    // FUN_0010a8e0(0.2, body speed (+0x4c), shell, shell animation [idx], body frame)
                    // UNVERIFIED: an index past the shell's animation list (the original reads past the list) is
                    // skipped
                    if (idx >= 0 && static_cast<size_t>(idx) < sh.clips.size())
                        sh.anim.change(0.2f, c.model.anim.speed(), idx, animFrame(c.model));
                }
                if (c.model.anim.currentClip() == 0xe || c.model.anim.currentClip() == 9) d.shellPhase = 10;
            }
        }
    }
    // FUN_00109d40(shell, pos, scale, fwd, up, right) while the shell node is visible; with the player's status bit
    // 0x10 the shell is published at scale 0.01
    if (!sh.parts.empty() && sh.parts[0].visible && characters().player() != nullptr) {
        d.shellDrawTiny = (characters().player()->status.block.statusMask & 0x10u) != 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// E22
// ---------------------------------------------------------------------------------------------------------------
// orig FUN_001c2c60: "DATA/CHR/%c%c%c/OBJ063%c.IGB" (0x4638e0)
std::string e22BubblePath(uint32_t id) {
    const std::string s = idString(id);
    return "DATA/CHR/" + s.substr(0, 3) + "/OBJ063" + s.substr(3, 1) + ".IGB";
}

// orig FUN_001c3020 (+ ctor FUN_001c2cc0: +0x40 matrix scale (0,0,0), +0x4c = 0): bubble on the "Bubble" joint
void e22Activate(Chr& c) {
    c.cd.f4c = 0.0f;
    if (loadClassPart(c, e22BubblePath(c.id), "Bubble", false)) {
        alchemy::Mat4& m = c.model.parts[size_t(c.cd.partIndex)].local;
        setScale(m, 0.0f, 0.0f, 0.0f);
    }
    c.cd.i48 = 0;
    c.flags24 |= 1u;
}

// joints of the six limb modifiers (table 0x463920)
constexpr int kE22Joints[6] = {9, 0x15, 0xb, 0x17, 0xd, 0x19};

// orig FUN_001c3190
void e22Tick(Chr& c) {
    ChrClassData& d = c.cd;
    ChrModel& m = c.model;
    int st = d.i48;
    if (st != 400) {
        if (st == 0) {
            // six joint modifiers at scale 0 (hidden limbs)
            d.modIndex = static_cast<int>(m.boneMods.size());
            for (int i = 0; i < 6; ++i) {
                const int k = addJointModifier(m, kE22Joints[i]);
                setScale(m.boneMods[size_t(k)].m, 0.0f, 0.0f, 0.0f);
                d.i48 = 5000;
            }
        } else if (m.anim.currentClip() == 0) {
            if (st != 5000) {
                d.i48 = 5000;
                for (int i = 0; i < 6; ++i)
                    if (d.modIndex >= 0 && size_t(d.modIndex + i) < m.boneMods.size())
                        setScale(m.boneMods[size_t(d.modIndex + i)].m, 0.0f, 0.0f, 0.0f);
            }
        } else {
            if (st == 5000) {
                d.i48 = 100;
                d.f50 = 0.01f;   // 0x3c23d70a
            }
            st = d.i48;
            uint32_t k;
            if (st == 100) k = 0;
            else if (st == 200) k = 2;
            else if (st == 300) k = 4;
            else k = 6;
            if (k != 6) {
                for (uint32_t i = k; static_cast<int>(i) < static_cast<int>(k + 2); ++i) {
                    if (d.modIndex < 0 || size_t(d.modIndex) + i >= m.boneMods.size()) continue;
                    alchemy::Mat4& mm = m.boneMods[size_t(d.modIndex) + i].m;
                    const float t = d.f50;
                    float f;
                    if ((i & 1) == 0) f = 10.0f * t + ps2Cos(t * 3.1415927f * 120.0f) / 12.0f + 0.0f;
                    else f = 10.0f * t + ps2Sin(t * 3.1415927f * 120.0f) / 12.0f + 0.0f;
                    float g = 0.1f;
                    if (0.1f <= f) g = f;
                    f = 1.0f;
                    if (g <= 1.0f) f = g;
                    if (t < 0.1f) setScale(mm, f, f, f);
                    else setScale(mm, 1.0f, 1.0f, 1.0f);
                }
                if ((d.i48 != 300 && d.f50 < 0.1f) || (d.i48 == 300 && d.f50 < 0.2f)) {
                    d.f50 = d.f50 + 0.005f;
                } else {
                    d.f50 = 0.01f;
                    if (d.i48 == 100) d.i48 = 200;
                    else if (d.i48 == 200) d.i48 = 300;
                    else if (d.i48 == 300) {
                        d.i48 = 400;
                        m.boneMods.clear();   // modifier list (actor+0x30)+0x34 released and emptied
                        d.modIndex = -1;
                    }
                }
            }
        }
    }
    // LAB_001c3798: bubble scale by animation (6: inflate between frames 35..85, burst at 85..88; 5: burst 23.5..27)
    const int idx = m.anim.currentClip();
    int mode;
    if (idx == 0) {
        mode = 1;
    } else if (idx == 6) {
        const float f = static_cast<float>(animFrame(m));
        if (88.0f < f) mode = 2;
        else if (85.0f < f) mode = 0;
        else if (34.0f < f) mode = 3;
        else mode = 2;
    } else if (idx == 5) {
        const float f = static_cast<float>(animFrame(m));
        if (27.0f < f) mode = 2;
        else if (23.5f < f) mode = 0;
        else mode = 2;
    } else if (idx == 9) {
        mode = 4;
    } else {
        mode = 2;
    }
    float& s = d.f4c;
    if (mode == 3) {
        if (s < 2.5f) {
            const float n = s + 0.035f;
            s = n;
            if (2.5f < n) s = 2.5f;   // 0x40200000
        }
    } else if (mode == 2) {
        const float f = s;
        if (f < 1.0f) {
            s = f + 0.05f;
            if (1.0f < f + 0.05f) s = 1.0f;
        } else if (1.0f < f) {
            s = f - 0.05f;
            if (f - 0.05f < 0.0f) s = 0.0f;
        }
    } else if (mode == 1) {
        if (0.0f < s) {
            const float n = s - 0.05f;
            s = n;
            if (n < 0.0f) s = 0.0f;
        }
    } else if (mode == 0) {
        s = 0.0f;
    }
    if (d.partIndex >= 0) setScale(m.parts[size_t(d.partIndex)].local, s, s, s);
}

// ---------------------------------------------------------------------------------------------------------------
// E23
// ---------------------------------------------------------------------------------------------------------------
}  // namespace

// orig FUN_0012ad50 -> FUN_0012adb0 / FUN_0012ab10: effect `fx` on the floor under the character (the first ground
// floor face, or a 300-unit radius-1 probe down from pos+10); one rand(0x167) yaw draw when the effect is made.
void groundEffect(Chr& c, int fx) {
    Vec3f at = c.xf.m.pos;
    if (c.phys.ground.nFloors == 0) {
        const float from[3] = {at[0], at[1] + 10.0f, at[2]};
        const float delta[3] = {0.0f, -300.0f, 0.0f};   // 0xc3960000
        float dist = 0.0f;
        if (!raycast(from, delta, 1.0f, &dist)) return;   // FUN_00143150(.., 0x44a760 = 1.0)
        at = {from[0], from[1] - dist, from[2]};
    }
    const uint32_t yaw = gameWorld().rng.rand(0x167);   // FUN_002071e0(0x53eb20, 0x167)
    // UNVERIFIED: the effect basis is built from the floor face normal (FUN_00112770); the hook takes a yaw only
    const Vec3f rot{0.0f, static_cast<float>(yaw), 0.0f};
    if (combatHooks().effect) combatHooks().effect(fx, Vec3f{at[0], at[1] + 0.15f, at[2]}, &rot, 100.0f);
}

namespace {

// orig FUN_001c3b90 (strings 0x463938 "DD02"): on the ground the collision shape is cleared; when the player comes
// within 30 units the character spends all its HP, marks the DD02 death (+0x44) and plays DD02_E<MODE+1>.
// Off the ground the shape is restored to a 25 x 50 cylinder.
void e23Tick(Chr& c) {
    const StateMachine& sm = c.sm;
    if (sm.next == StateId::None && !sm.popRequested && sm.curFactory == StateId::DeathA) return;   // LAB_0012a700
    if (sm.next == StateId::None && !sm.popRequested && sm.curFactory == StateId::DeathB) return;   // LAB_0012a6f0
    if ((c.status.block.statusMask & 0x10000u) != 0) return;
    ChrClassData& d = c.cd;
    if ((c.phys.result & 0x20u) == 0) {
        if (d.i40 != 0) {
            d.i40 = 0;
            // FUN_00142200(25.0, 50.0, phys, chr)
            c.shape.owner = &c;
            c.shape.radius = 25.0f;
            c.shape.height = 50.0f;
            c.shape.a = -1.0f;
            c.shape.b = -1.0f;
            c.shape.top = c.shape.height + c.xf.m.pos[1];
        }
        return;
    }
    if (d.i40 == 0) {
        d.i40 = 1;
        c.shape.owner = &c;   // FUN_00142200(0, 0, phys, chr)
        c.shape.radius = 0.0f;
        c.shape.height = 0.0f;
        c.shape.a = -1.0f;
        c.shape.b = -1.0f;
        c.shape.top = c.shape.height + c.xf.m.pos[1];
    }
    const bool dead = (c.type == kChrPlayerType || c.type == kChrEnemy) && c.status.block.hp == 0;
    Chr* pl = characters().player();
    if (dead || pl == nullptr) return;
    const Vec3f& a = c.xf.m.pos;
    const Vec3f& b = pl->xf.m.pos;
    const float dz = a[2] - b[2], dy = a[1] - b[1], dx = a[0] - b[0];
    const float d2 = dz * dz + dx * dx + dy * dy;
    float dist = 0.0f;
    if (d2 != 0.0f) dist = std::sqrt(d2);
    if (!(dist < 30.0f)) return;
    groundEffect(c, 0x61);                                    // FUN_0012ad50(chr, 0x61) (+ FUN_001126e0 on it)
    statusAddHp(c.status, -statusHpMax(c.status));            // FUN_001349e0(st, -FUN_001343f0(st))
    c.deathVariant = 1;                                       // +0x44
    c.phys.flags &= 0xfffffffcu;                              // FUN_00142140
    c.phys.vy = 0.0f;
    const uint32_t mode = (c.ai.flags & 6u) >> 1;
    c.act.start(std::string("DD02_E") + static_cast<char>('1' + mode));   // FUN_00143aa0
}

}  // namespace

float ps2Sin(float a) { return std::bit_cast<float>(kSinLut[static_cast<int>(a * 162.97466f) & 0x3ff]); }
float ps2Cos(float a) { return std::bit_cast<float>(kCosLut[static_cast<int>(a * 162.97466f) & 0x3ff]); }

ChrClass chrClassOf(uint32_t id) {
    for (const ClassEntry& e : kClassTable)
        if (tag4(e.tag) == id) return e.cls;
    return ChrClass::Generic;   // FUN_0012e880
}

// orig FUN_0012b620 (strings 0x44a6d0 E03B, 0x44a6d8 E09B, 0x44a9a0 E03A, 0x44a9a8 E09A, 0x44a9b0 E18B, 0x44a9b8 E18A)
std::string chrModelPath(uint32_t id) {
    if (id == tag4("E03B")) id = tag4("E03A");
    else if (id == tag4("E09B")) id = tag4("E09A");
    else if (id == tag4("E18B")) id = tag4("E18A");
    const std::string s = idString(id);
    return "DATA/CHR/" + s.substr(0, 3) + "/" + s + ".IGB";   // 0x44a9c0
}

void chrClassActivate(Chr& c) {
    switch (c.cls) {
    case ChrClass::E01: e01Activate(c); break;       // FUN_001c0cf0
    case ChrClass::E02: e02Activate(c); break;       // FUN_001c1140
    case ChrClass::E05: preloadActivate(c); break;   // FUN_001c18b0
    case ChrClass::E07: e07Activate(c); break;       // FUN_001c1a70
    case ChrClass::E09: e09Activate(c); break;       // FUN_001c1f70
    case ChrClass::E19: e19Activate(c); break;       // FUN_001c28a0
    case ChrClass::E22: e22Activate(c); break;       // FUN_001c3020
    case ChrClass::E27: preloadActivate(c); break;   // FUN_001c3fa0
    default:                                         // NPC classes (npc_class.cpp), else FUN_001299f0 / FUN_001b1350
        if (!npcClassActivate(c)) chrActivate(c);
        break;
    }
}

void chrClassTick(Chr& c) {
    switch (c.cls) {
    case ChrClass::E01: e01Tick(c); break;   // FUN_001c0db0
    case ChrClass::E02: e02Tick(c); break;   // FUN_001c14e0
    case ChrClass::E07: e07Tick(c); break;   // FUN_001c1b00 (tread texture scroll)
    case ChrClass::E09: e09Tick(c); break;   // FUN_001c2450
    case ChrClass::E19: e19Tick(c); break;   // FUN_001c28f0
    case ChrClass::E22: e22Tick(c); break;   // FUN_001c3190
    case ChrClass::E23: e23Tick(c); break;   // FUN_001c3b90
    default: npcClassTick(c); break;         // NPC classes (npc_class.cpp); FUN_00129c50 (empty) otherwise
    }
}

void chrClassPose(Chr& c) {
    if (c.cls != ChrClass::E19 || !c.cd.shell) return;
    ChrModel& sh = *c.cd.shell;
    if (!sh.loaded() || sh.parts.empty() || !sh.parts[0].model) return;
    sh.anim.pose(sh.pose);
    if (!sh.pose.empty()) sh.parts[0].model->setPose(sh.pose);
}

void chrClassDraw(const Chr& c, alchemy::SceneRenderer& r, int pass, float alpha) {
    (void)alpha;
    if (c.cls != ChrClass::E19 || !c.cd.shell) return;
    const ChrClassData& d = c.cd;
    const ChrModel& sh = *d.shell;
    if (!sh.loaded() || sh.parts.empty() || !sh.parts[0].model || !sh.parts[0].visible) return;
    const Vec3f sc = d.shellDrawTiny ? Vec3f{0.01f, 0.01f, 0.01f} : d.shellScale;   // 0x3c23d70a
    alchemy::Mat4 m = alchemy::Mat4::identity();
    for (int k = 0; k < 3; ++k) {
        m.m[size_t(k)] = d.shellXf.right[size_t(k)] * sc[0];
        m.m[size_t(4 + k)] = d.shellXf.up[size_t(k)] * sc[1];
        m.m[size_t(8 + k)] = d.shellXf.fwd[size_t(k)] * sc[2];
        m.m[size_t(12 + k)] = d.shellXf.pos[size_t(k)];
    }
    modelBeginDraw(sh, r);
    modelSubmitPart(sh, sh.parts[0], m, r, pass);
    modelEndDraw(r);
}

// PORT: the matrix chrClassDraw builds for the E19 shell (presenter snapshot, game/present/present_draw.hpp)
bool chrClassShellMatrix(const Chr& c, alchemy::Mat4* out) {
    if (c.cls != ChrClass::E19 || !c.cd.shell) return false;
    const ChrClassData& d = c.cd;
    const ChrModel& sh = *d.shell;
    if (!sh.loaded() || sh.parts.empty() || !sh.parts[0].model) return false;
    const Vec3f sc = d.shellDrawTiny ? Vec3f{0.01f, 0.01f, 0.01f} : d.shellScale;   // 0x3c23d70a
    alchemy::Mat4 m = alchemy::Mat4::identity();
    for (int k = 0; k < 3; ++k) {
        m.m[size_t(k)] = d.shellXf.right[size_t(k)] * sc[0];
        m.m[size_t(4 + k)] = d.shellXf.up[size_t(k)] * sc[1];
        m.m[size_t(8 + k)] = d.shellXf.fwd[size_t(k)] * sc[2];
        m.m[size_t(12 + k)] = d.shellXf.pos[size_t(k)];
    }
    *out = m;
    return true;
}

// PORT: chrClassDraw at a snapshot matrix
void chrClassDrawAt(const Chr& c, const alchemy::Mat4& shell, alchemy::SceneRenderer& r, int pass) {
    if (c.cls != ChrClass::E19 || !c.cd.shell) return;
    const ChrModel& sh = *c.cd.shell;
    if (!sh.loaded() || sh.parts.empty() || !sh.parts[0].model || !sh.parts[0].visible) return;
    modelBeginDraw(sh, r);
    modelSubmitPart(sh, sh.parts[0], shell, r, pass);
    modelEndDraw(r);
}

}  // namespace baroque::game
