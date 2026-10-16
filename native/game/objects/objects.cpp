// Dungeon object framework: class lookup of FUN_0012ce70 (J/O branch), dispatch of the class methods, the helpers the
// classes share (see objects.hpp). The class methods are in objects_classes*.cpp.
#include "game/objects/objects.hpp"
#include "game/objects/objects_internal.hpp"
#include "game/objects/objects_deep.hpp"
#include "game/effects/exi_math.hpp"

#include "game/dungeon.hpp"
#include "game/effects/effects.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/present/present_draw.hpp"
#include "game/sound_stub.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

// Factory table 0x44aa10, J/O entries (tags written by the static init FUN_004c3340, thunks 0x12d410..0x12d5c0;
// recovered by emulating the init, see OBJECTS_NATIVE.md). J030..J074 are the projectile classes (projectiles.cpp).
struct TableEntry {
    const char* tag;
    ObjClass cls;
};
constexpr TableEntry kTable[] = {
    {"J017", ObjClass::J017}, {"J018", ObjClass::J017}, {"J019", ObjClass::J017}, {"J020", ObjClass::J017},
    {"J021", ObjClass::J017}, {"J022", ObjClass::J017}, {"J023", ObjClass::J017}, {"J046", ObjClass::J017},
    {"J047", ObjClass::J017}, {"J039", ObjClass::J017}, {"J027", ObjClass::J017}, {"J028", ObjClass::J017},
    {"O00B", ObjClass::O00B}, {"O01B", ObjClass::O00B}, {"O02B", ObjClass::O02B}, {"O03B", ObjClass::O00B},
    {"O04B", ObjClass::O00B}, {"O00J", ObjClass::O00J}, {"O50J", ObjClass::O50J}, {"O50N", ObjClass::O50N},
    // projectiles (static init 0x4c3340: thunks 0x12d470..0x12d5c0 -> wrappers 0x12f340..0x12fc70)
    {"J030", ObjClass::Projectile}, {"J050", ObjClass::Projectile}, {"J051", ObjClass::Projectile},
    {"J052", ObjClass::Projectile}, {"J054", ObjClass::Projectile}, {"J055", ObjClass::Projectile},
    {"J056", ObjClass::Projectile}, {"J057", ObjClass::Projectile}, {"J058", ObjClass::Projectile},
    {"J059", ObjClass::Projectile}, {"J060", ObjClass::Projectile}, {"J061", ObjClass::Projectile},
    {"J062", ObjClass::Projectile}, {"J063", ObjClass::Projectile}, {"J064", ObjClass::Projectile},
    {"J065", ObjClass::Projectile}, {"J066", ObjClass::Projectile}, {"J068", ObjClass::Projectile},
    {"J069", ObjClass::Projectile}, {"J070", ObjClass::Projectile}, {"J071", ObjClass::Projectile},
    {"J072", ObjClass::Projectile}, {"J073", ObjClass::Projectile}, {"J074", ObjClass::Projectile},
};

uint32_t classIdOf(ObjClass c) {   // last vtable slot's return value
    switch (c) {
    case ObjClass::OA: return 0x132bb0;
    case ObjClass::OC: return 0x132bc0;
    case ObjClass::OC70: return 0x132bd0;
    case ObjClass::OD: return 0x132be0;
    case ObjClass::OE: return 0x132bf0;
    case ObjClass::OFGI: return 0x132c00;
    case ObjClass::OH: return 0x132c10;
    case ObjClass::OK: return 0x132c20;
    case ObjClass::OM: return 0x132c30;
    case ObjClass::ON40: return 0x132c40;
    case ObjClass::ON: return 0x132c50;
    case ObjClass::OL: return 0x132c60;
    case ObjClass::OO: return 0x132c70;
    case ObjClass::OP: return 0x132c80;
    case ObjClass::OQ: return 0x132c90;
    case ObjClass::O50N: return 0x12d410;
    case ObjClass::O50J: return 0x12d420;
    case ObjClass::O00J: return 0x12d430;
    case ObjClass::O02B: return 0x12d440;
    case ObjClass::O00B: return 0x12d450;
    case ObjClass::J017: return 0x12d460;
    case ObjClass::Projectile: return 0;   // last slot FUN_00132830.. (no list-0x50b000 body)
    default: return 0;
    }
}

// FUN_002410d8(s, 0, 10) on a 2-char string: optional sign, decimal digits
int atoi2(char a, char b) {
    const char s[3] = {a, b, 0};
    int i = 0, sign = 1, v = 0;
    while (s[i] == ' ' || s[i] == '\t') ++i;
    if (s[i] == '-') { sign = -1; ++i; } else if (s[i] == '+') ++i;
    while (s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
    return sign * v;
}

}  // namespace

uint32_t objTag(const char* s) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24;
}
std::string tagString(uint32_t tag) {
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i) s[size_t(i)] = char((tag >> (8 * i)) & 0xff);
    return s;
}
int tagNumber(uint32_t tag) { return atoi2(static_cast<char>((tag >> 8) & 0xff), static_cast<char>((tag >> 16) & 0xff)); }

// uRam0050a610 as written by the dungeon module and the Game scene (game_scene.cpp floorFlagWord)
uint32_t floorFlagWord() {
    const Dungeon* d = gameWorld().dungeon;
    if (d == nullptr) return 0;
    const ItemWorldState& w = hudWorld();
    return (d->floorFlags() & ~w.clearedFloorFlags) | w.portFloorFlags;
}
int floorIdNow() { return gameWorld().dungeon != nullptr ? gameWorld().dungeon->floorId() : 0; }

// orig FUN_0012ce70 J/O branch: table lookup (FUN_0012ce70 loop over 0x44aa10), else for 'O' the 4th letter
std::unique_ptr<Chr> objectConstruct(uint32_t tag) {
    const char c0 = static_cast<char>(tag & 0xff);
    ObjClass cls = ObjClass::None;
    for (const TableEntry& e : kTable)
        if (objTag(e.tag) == tag) { cls = e.cls; break; }
    if (cls == ObjClass::None && c0 == 'O') {
        const char k = static_cast<char>((tag >> 24) & 0xff);
        const int n = tagNumber(tag);
        switch (k) {
        case 'A': cls = ObjClass::OA; break;                                    // FUN_0012e8f0
        case 'C': cls = (n < 0x46 || 0x5c < n) ? ObjClass::OC : ObjClass::OC70; break;   // FUN_0012e960 / FUN_0012e9d0
        case 'D': cls = ObjClass::OD; break;                                    // FUN_0012ea40
        case 'E': cls = ObjClass::OE; break;                                    // FUN_0012eb40
        case 'F': case 'G': case 'I': cls = ObjClass::OFGI; break;              // FUN_0012ebb0
        case 'H': cls = ObjClass::OH; break;                                    // FUN_0012ecb0
        case 'K': cls = ObjClass::OK; break;                                    // FUN_0012ed20
        case 'M': cls = ObjClass::OM; break;                                    // FUN_0012ed90
        case 'N': cls = (n < 0x28 || 0x2a < n) ? ObjClass::ON : ObjClass::ON40; break;   // FUN_0012ee70 / FUN_0012ee00
        case 'L': cls = ObjClass::OL; break;                                    // FUN_0012eee0
        case 'O': cls = ObjClass::OO; break;                                    // FUN_0012ef50
        case 'P': cls = ObjClass::OP; break;                                    // FUN_0012efc0
        case 'Q': cls = ObjClass::OQ; break;                                    // FUN_0012f030
        default: break;
        }
    }
    if (cls == ObjClass::None) {
        if (c0 == 'J' || c0 == 'O')
            // every tag of the factory table 0x44aa10 and every O 4th-letter class is constructed (OBJECTS_NATIVE.md);
            // anything else (J007..J009, the map-model tags J024..J026, O99Z) is null in FUN_0012ce70 too
            platform::log("obj: no class for '" + tagString(tag) + "' (null in FUN_0012ce70 as well)");
        return nullptr;
    }
    auto c = std::make_unique<Chr>();
    c->obj = std::make_shared<ObjData>();
    c->obj->cls = cls;
    c->obj->classId = classIdOf(cls);
    // FUN_00128590 (every object): transform scale 1, position 0, FUN_001286c0 axes; +0x24 = 0
    c->xf = Transform{};
    resetAxes(c->xf.m);
    c->flags24 = 0;
    physInit(c->phys, &c->xf.m.pos, &c->shape, 0.0f);
    c->id = tag;
    if (!objectsDeepConstruct(*c)) objClassConstruct(*c);   // objects_classes3.cpp classes first
    return c;
}

void objectInit(Chr& c) { objClassInit(c); }
void objectTick(Chr& c) {
    if (c.obj == nullptr) return;
    if (c.obj->cls == ObjClass::Helper) {
        if (!itemTaskTick(c)) helperTick(c);   // item trap / BN18 field records (item_tasks.cpp) first
    } else if (!objectsDeepTick(c)) {
        objClassTick(c);
    }
}
bool objectActive(const Chr& c) { return objClassActive(c); }
int objectContact(Chr& c, Chr* other, uint32_t m1, uint32_t m2, uint32_t m3) { return objClassContact(c, other, m1, m2, m3); }

// ---- presentation: the model at the matrix last published by FUN_00109d40 ----
void objectPose(Chr& c) {
    if (!c.model.loaded() || c.model.parts.empty()) return;
    if (c.model.skeleton() == nullptr || c.model.clips.empty()) return;
    c.model.anim.pose(c.model.pose);
    for (ChrModelPart& part : c.model.parts)
        if (part.model && part.attachBone.empty() && part.attachBoneIndex < 0) part.model->setPose(c.model.pose);
}

void objectDraw(const Chr& c, alchemy::SceneRenderer& r, int pass, float alpha) {
    (void)alpha;
    const ObjData& o = *c.obj;
    if (!o.published || o.hidden || c.model.parts.empty()) return;
    alchemy::Mat4 m = alchemy::Mat4::identity();
    const ChrTransform& t = o.pubXf;
    for (int k = 0; k < 3; ++k) {
        m.m[size_t(k)] = t.right[size_t(k)] * o.pubScale[0];
        m.m[size_t(4 + k)] = t.up[size_t(k)] * o.pubScale[1];
        m.m[size_t(8 + k)] = t.fwd[size_t(k)] * o.pubScale[2];
        m.m[size_t(12 + k)] = t.pos[size_t(k)];
    }
    modelBeginDraw(c.model, r);
    for (const ChrModelPart& part : c.model.parts) {
        if (!part.model || !part.visible) continue;
        // O00J: the statue's transform animation runs on the object's own clock (+0x28 through the model's vt+0x9c;
        // stopped at 0 by vt+0x94(0) in the ctor until the clock turns non-negative). UNVERIFIED: wrapper methods
        // not traced, modelled as the igTransform input time.
        if (o.cls == ObjClass::O00J) part.model->setTimeOverride(o.animTimeNs < 0 ? 0 : o.animTimeNs);
        modelSubmitPart(c.model, part, alchemy::mul(part.local, m), r, pass);   // + material fader (FUN_0010a0a0)
    }
    modelEndDraw(r);
}

// PORT: objectDraw from a presenter snapshot (game/present/present_draw.hpp): the published matrix and the O00J
// transform time come from the (interpolated) snapshot
void objectDrawAt(const Chr& c, const alchemy::Mat4& m, int64_t animTimeNs, alchemy::SceneRenderer& r, int pass) {
    const ObjData& o = *c.obj;
    if (!o.published || o.hidden || c.model.parts.empty()) return;
    modelBeginDraw(c.model, r);
    for (const ChrModelPart& part : c.model.parts) {
        if (!part.model || !part.visible) continue;
        if (o.cls == ObjClass::O00J) {
            const int64_t t = animTimeNs >= 0 ? animTimeNs : o.animTimeNs;
            part.model->setTimeOverride(t < 0 ? 0 : t);
        }
        modelSubmitPart(c.model, part, alchemy::mul(part.local, m), r, pass);
    }
    modelEndDraw(r);
}

// FUN_001099e0(holder, path, mode, flags): the scene is loaded and attached under the holder (synchronous here).
// mode 1 with flags & 0x4000000 clear builds an extra attribute group (FUN_003bdac0..): not modelled (UNVERIFIED).
// External texture dictionaries (igExternalInfoEntry _infoName, e.g. "M06_TEX.IGB" next to the model): Alchemy
// resolves them through the resident texture packs; the port loads each pack once and keeps it.
const alchemy::TextureLibrary* externalTextures(const std::filesystem::path& file) {
    struct Pack {
        std::unique_ptr<alchemy::IgbFile> igb;
        std::unique_ptr<alchemy::TextureLibrary> lib;
    };
    static std::map<std::string, Pack> packs;
    const std::string key = file.generic_string();
    auto it = packs.find(key);
    if (it != packs.end()) return it->second.lib.get();
    Pack p;
    p.igb = std::make_unique<alchemy::IgbFile>();
    std::string err;
    if (!p.igb->load(file, &err)) {
        platform::log("obj: texture pack " + key + ": " + err);
        packs.emplace(key, Pack{});
        return nullptr;
    }
    p.lib = std::make_unique<alchemy::TextureLibrary>();
    p.lib->addFile(*p.igb, nullptr);
    const alchemy::TextureLibrary* lib = p.lib.get();
    packs.emplace(key, std::move(p));
    return lib;
}

bool objLoadModel(Chr& c, const std::string& path, int mode, uint32_t flags) {
    c.model.flags2c |= flags;
    if (mode == 0) c.model.flags2c |= 0x10000000u;   // FUN_001099e0: no shadow caster
    else modelShadowOn(c.model, false);              // FUN_00109f10(model, 0)
    c.model.lit = mode == 1 && (flags & 0x8000000u) == 0;   // parent 0x4e78f4 (lit) else 0x4e78ec / 0x4e78f0
    c.model.light.exists = mode != 0 && (flags & 0x4000000u) == 0;
    ChrModelPart part;
    const std::filesystem::path full = characters().assetRoot / path;
    if (!chrLoadPart(part, full, "")) return false;
    {   // rebuild with the external texture packs the scene refers to
        std::vector<const alchemy::TextureLibrary*> libs{part.lib.get()};
        std::set<std::string> seen;
        for (const alchemy::IgExternal* e : part.file->externals()) {
            if (e == nullptr || e->infoName.empty() || !seen.insert(e->infoName).second) continue;
            if (const alchemy::TextureLibrary* l = externalTextures(full.parent_path() / e->infoName)) libs.push_back(l);
        }
        if (libs.size() > 1) {
            part.model = std::make_unique<alchemy::SceneModel>();
            part.model->build(*part.file, libs, nullptr);
        }
    }
    c.model.parts.clear();
    c.model.parts.push_back(std::move(part));
    if (const alchemy::Skeleton* sk = &c.model.parts[0].model->skeleton(); sk != nullptr && sk->valid) {
        std::vector<std::string> log;
        c.model.clips = alchemy::loadClips(*c.model.parts[0].file, alchemy::primarySkeleton(*c.model.parts[0].file), &log);
        c.model.anim.init(&c.model.clips, c.model.skeleton());
    }
    c.model.flags2c |= 0x80000000u;   // FUN_00109c80 load callback
    return true;
}

// orig FUN_00109d40(holder, pos, scale, fwd, up, right): the node matrix (axes x scale, position) when loaded
void objPublish(Chr& c) { objPublishAt(c, c.xf.m.pos); }
void objPublishAt(Chr& c, const Vec3f& pos) {
    if ((c.model.flags2c & 0x80000000u) == 0) return;
    modelPublishFx(c.model);   // FUN_0010a160: material fader step
    ObjData& o = *c.obj;
    o.published = true;
    o.pubXf = c.xf.m;
    o.pubXf.pos = pos;
    o.pubScale = c.xf.scale;
}

// orig FUN_0012c780(0x50b130, tag, type): the list of the type's kind (FUN_00128080); characters (kind 0) of type
// 0 / 2 with HP 0 are skipped; first live entity with that type and id.
Chr* findEntity(uint32_t tag, int type) {
    const bool charKind = type <= kChrEnemy;
    for (Chr* e : characters().list()) {
        if (e->type != type) continue;
        if (charKind && (e->type == 0 || e->type == 2) && e->obj == nullptr && e->item == nullptr && e->status.block.hp == 0) continue;
        if ((e->taskFlags & 0x10000000u) == 0 && e->id == tag) return e;
    }
    return nullptr;
}

int roomOfCell(const uint32_t* cell) {
    if (cell == nullptr) return -1;
    return (*cell & 0xf000u) == 0x1000u ? static_cast<int>(*cell & 0xfu) : -1;
}

// orig FUN_001b75d0: the position's room (FUN_00124c70) equals the player's room (cell +0x30)
bool sameRoomAsPlayer(const Vec3f& pos) {
    const Chr* pl = characters().player();
    if (pl == nullptr || pl->cell == nullptr) return false;
    const int pr = roomOfCell(pl->cell);
    if (pr < 0) return false;
    Dungeon* d = gameWorld().dungeon;
    if (d == nullptr) return false;
    int x = 0, y = 0;
    const uint32_t* c = d->cellAtWorld(pos.data(), &x, &y);
    if (c == nullptr) return false;
    return pr == roomOfCell(c);
}

// orig FUN_00207350
void entityRemove(Chr& c) {
    if ((c.taskFlags & 1u) != 0) c.taskFlags |= 0x30000000u;
}

// orig FUN_00128c00(rad, ent): rotate forward about the right axis, rebuild up = fwd x right... (see pseudocode)
void rotateAboutRight(ChrTransform& t, float rad) {
    float m[12] = {};
    axisAngle(m, rad, t.right);
    t.fwd = mulVec3(t.fwd, m);
    normalize(t.fwd);
    t.up[0] = t.fwd[1] * t.right[2] - t.fwd[2] * t.right[1];
    t.up[1] = t.fwd[2] * t.right[0] - t.fwd[0] * t.right[2];
    t.up[2] = t.fwd[0] * t.right[1] - t.fwd[1] * t.right[0];
    normalize(t.up);
    t.fwd[0] = t.right[1] * t.up[2] - t.right[2] * t.up[1];
    t.fwd[1] = t.right[2] * t.up[0] - t.right[0] * t.up[2];
    t.fwd[2] = t.right[0] * t.up[1] - t.right[1] * t.up[0];
    normalize(t.fwd);
}

// orig FUN_001289c0(rad, ent): rotate up about the forward axis, rebuild right = up x fwd, up = fwd x right
void rotateAboutForward(ChrTransform& t, float rad) {
    float m[12] = {};
    axisAngle(m, rad, t.fwd);
    t.up = mulVec3(t.up, m);
    normalize(t.up);
    t.right[0] = t.up[1] * t.fwd[2] - t.up[2] * t.fwd[1];
    t.right[1] = t.up[2] * t.fwd[0] - t.up[0] * t.fwd[2];
    t.right[2] = t.up[0] * t.fwd[1] - t.up[1] * t.fwd[0];
    normalize(t.right);
    t.up[0] = t.fwd[1] * t.right[2] - t.fwd[2] * t.right[1];
    t.up[1] = t.fwd[2] * t.right[0] - t.fwd[0] * t.right[2];
    t.up[2] = t.fwd[0] * t.right[1] - t.fwd[1] * t.right[0];
    normalize(t.up);
}

// orig FUN_00128740(ent, fwd, up, right)
void setAxes(ChrTransform& t, const Vec3f& fwd, const Vec3f& up, const Vec3f& right) {
    t.fwd = fwd;
    t.up = up;
    t.right = right;
}

// ---- effect runtime (game/effects) and the sound manager ----
int fxSpawn(int fx, const Vec3f* pos, const Vec3f* rotDeg, const Vec3f* scale) {
    return effects().spawn(0.0f, fx, pos != nullptr ? pos->data() : nullptr, rotDeg != nullptr ? rotDeg->data() : nullptr,
                           scale != nullptr ? scale->data() : nullptr);
}
bool fxExists(int h) { return effects().exists(h); }
void fxKill(int h) { effects().kill(h); }
void fxStop(int h) { effects().stop(h); }
void fxSetMatrix(int h, const float m[16]) {
    ExiMat e;
    for (int i = 0; i < 16; ++i) e.m[size_t(i)] = m[i];
    effects().setMatrix(h, e);
}
void fxSetPosition(int h, const Vec3f& pos) { effects().setPosition(h, pos.data()); }
void fxAddRotation(int h, const Vec3f& rotDeg) { effects().addRotation(h, rotDeg.data()); }
void fxLoopOut(int h, int16_t count, int16_t end, int restart) { effects().loopOut(h, count, end, restart); }
// FUN_00115470 returns the sound-manager handle (stopped later with FUN_00115820, moved with FUN_00116260).
int objSe3d(int id, const Vec3f& pos, float vol) { return static_cast<int>(sound().playSe3d(vol, id, pos.data())); }
void objSeStop(int handle) {   // FUN_00115820(0x4e9790, handle, 2)
    sound().stopSe(static_cast<uint32_t>(handle), 2);
}
// Class destructors (vtable +8) with presentation / sound side effects, run when the record is deleted:
//   FUN_001bb9f0 dart helper: flying effect stopped (FUN_00112560 when +0x28 != -1), looping SE stopped
//     (FUN_00115820(snd, +0x2c, 2) when non-zero), base FUN_00128610;
//   FUN_001b8650 O00B levers: model refcount 0x53d7e0, hurt volume FUN_0014ace0 (done by the reaper), looping SE
//     +0x40 stopped (FUN_00115820(snd, +0x40, 2)).
void objectDestroy(Chr& c) {
    if (c.obj == nullptr || objectsDeepDestroy(c) || itemTaskDestroy(c)) return;
    ObjData& o = *c.obj;
    if (o.cls == ObjClass::Helper && o.helperKind == 0x1bb990u) {
        if (o.I(0x28) != -1) fxStop(o.I(0x28));
        if (o.I(0x2c) != 0) objSeStop(o.I(0x2c));
    } else if (o.cls == ObjClass::O00B) {
        if (o.I(0x40) != 0) objSeStop(o.I(0x40));
    } else if (o.cls == ObjClass::Projectile) {
        projDestroy(c);   // J030 FUN_001c4150: flying effect +0x2c stopped (projectiles.cpp)
    }
}
void objectLaunch(Chr& c, Chr& owner, const Vec3f& pos, int bone) {
    if (c.obj != nullptr && c.obj->cls == ObjClass::Projectile) projLaunch(c, owner, pos, bone);
}
void objSeMove(int handle, const Vec3f& pos) { sound().setSePosition(static_cast<uint32_t>(handle), pos.data()); }   // FUN_00116260

// List 0x50b000 bodies tested by FUN_00140250: classes 0x12d440 / 0x132c50 / 0x12d410 (phys +0x34) and 0x132c30 /
// 0x132c40 (phys +0x28); the port keeps that physics object in Chr::phys / Chr::shape.
void objectCollisionBodies(std::vector<CollBody>& out) {
    for (Chr* c : characters().list()) {
        if (c->obj == nullptr || c->type != kChrObject) continue;
        const uint32_t id = c->obj->classId;
        if (id != 0x12d440 && id != 0x132c50 && id != 0x12d410 && id != 0x132c30 && id != 0x132c40) continue;
        CollBody b;
        b.owner = c;
        b.phys = &c->phys;
        b.pos = &c->xf.m.pos;
        b.right = &c->xf.m.right;
        b.fwd = &c->xf.m.fwd;
        b.id0 = static_cast<char>(c->id & 0xff);
        b.active = objectActive(*c);
        b.removed = (c->taskFlags & 0x10000000u) != 0;
        b.solid = false;
        b.cell = c->cell;
        b.cellX = c->cellX;
        b.cellY = c->cellY;
        out.push_back(b);
    }
}

namespace {
const bool gCollisionObjects = (setDefaultCollisionObjects(objectCollisionBodies), true);
}  // namespace

}  // namespace baroque::game
