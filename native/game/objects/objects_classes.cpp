// Dungeon object classes (objects.hpp): ctor / init (vt+0x1c) / tick (vt+0xc) / contact (vt+0x24) and the type-5
// helpers they spawn. Fields are addressed by their original offsets (ObjData::I / F / P / M); PTMF slots hold the
// original member-function address (0 = null) and are dispatched below. Strings and floats were read from the ELF
// (python tools/disasm.py, read_bytes / word); see work/reports/OBJECTS_NATIVE.md.
#include "game/objects/objects.hpp"
#include "game/objects/objects_internal.hpp"

#include "game/character/combat.hpp"
#include "game/flag_manager.hpp"
#include "game/hud/hud_api.hpp"
#include "game/items/chr_adapter.hpp"
#include "game/items/item_world.hpp"
#include "game/game_world.hpp"
#include "platform/platform.hpp"

#include <cmath>
#include <unordered_map>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

constexpr Vec3f kAxisX{1.0f, 0.0f, 0.0f};   // FUN_00206f90
constexpr Vec3f kAxisY{0.0f, 1.0f, 0.0f};   // FUN_00206f10
constexpr Vec3f kAxisZ{0.0f, 0.0f, 1.0f};   // FUN_00207010

uint32_t tag4(const char* s) { return objTag(s); }
char tagChar(uint32_t tag, int i) { return static_cast<char>((tag >> (8 * i)) & 0xff); }

ObjData& od(Chr& c) { return *c.obj; }
const ObjData& od(const Chr& c) { return *c.obj; }
Vec3f& pos(Chr& c) { return c.xf.m.pos; }

// FUN_0039d2e0 identity + rows: right * kr, up * ku, fwd * kf, position (the effect matrices of the classes)
void axesMatrix(float m[16], const Vec3f& right, float kr, const Vec3f& up, float ku, const Vec3f& fwd, float kf,
                const Vec3f& p) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    for (int k = 0; k < 3; ++k) {
        m[k] = right[size_t(k)] * kr;
        m[4 + k] = up[size_t(k)] * ku;
        m[8 + k] = fwd[size_t(k)] * kf;
        m[12 + k] = p[size_t(k)];
    }
}

// FUN_00112190(0, fx, pos, 0, (100, 100, 100, 1)): an effect at a position, default rotation, scale 100
int fxAt(int fx, const Vec3f& p) {
    const Vec3f s{100.0f, 100.0f, 100.0f};
    return fxSpawn(fx, &p, nullptr, &s);
}

// list 0x50a7b0 (FUN_0012c1c0 / FUN_0012c1d0): characters of types 0..2
template <class F> void forEachCharacter(F&& f) {
    for (Chr* e : characters().list())
        if (e->type <= kChrEnemy && e->obj == nullptr && e->item == nullptr) f(*e);
}
// list 0x50a830 (FUN_0012c1e0 / FUN_0012c1f0): item entities
template <class F> void forEachItem(F&& f) {
    for (Chr* e : characters().list())
        if (e->item != nullptr) f(*e);
}

// FUN_00128690(helper, 5, tag) + transform copy + FUN_002073e0: a type-5 helper linked into the task list
Chr* linkHelper(std::unique_ptr<Chr> h, uint32_t tag, const Chr& from, bool copyAxes) {
    Chr* c = characters().adopt(std::move(h));
    c->type = kChrEffect;
    c->id = tag;
    c->xf.m.pos = from.xf.m.pos;
    if (copyAxes) setAxes(c->xf.m, from.xf.m.fwd, from.xf.m.up, from.xf.m.right);   // FUN_00128740
    c->prevPos = c->xf.m.pos;
    c->taskFlags |= 1u;
    characters().link(c);                     // iRam0050b13c++, FUN_002073e0(h, 0x50b134, 0x50b138)
    return c;
}

std::unique_ptr<Chr> newHelper(uint32_t ctorAddr) {
    auto c = std::make_unique<Chr>();
    c->obj = std::make_shared<ObjData>();
    c->obj->cls = ObjClass::Helper;
    c->obj->helperKind = ctorAddr;
    c->xf = Transform{};                      // FUN_00128590
    resetAxes(c->xf.m);
    physInit(c->phys, &c->xf.m.pos, &c->shape, 0.0f);
    return c;
}

// =================================================================================================================
// J017 family (J017..J023, J027, J028, J039, J046, J047): ctor FUN_0012af10 (FUN_0012ae60 + model holder +0x24
// FUN_0012db80), init FUN_0012afe0, tick FUN_0012b600 (publish only). Strings 0x44a8b0.., table 0x48deb0
// {tag, mode, path} x 10 (tags filled once from 0x44a960.. / 0x44a900 / 0x44a908).
// =================================================================================================================
struct JModel {
    const char* tag;
    int mode;
    const char* path;
};
constexpr JModel kJModels[10] = {
    {"J017", 0, "DATA/OBJ/OBJ017.IGB"},      {"J018", 0, "DATA/MAP/M51/MG51_00.IGB"},
    {"J019", 0, "DATA/MAP/M50/MG50_00.IGB"}, {"J020", 0, "DATA/MAP/M56/MG56_00.IGB"},
    {"J021", 0, "DATA/MAP/M56/MG56_01.IGB"}, {"J046", 0, "DATA/MAP/M55/MG55_00.IGB"},
    {"J047", 0, "DATA/MAP/M55/MG55_01.IGB"}, {"J039", 1, "DATA/CHR/C14/OBJ039.IGB"},
    {"J027", 2, "DATA/MAP/M51/MG51_01.IGB"}, {"J028", 2, "DATA/MAP/M02/MG02_03.IGB"},
};

void j017Init(Chr& c) {   // orig FUN_0012afe0
    const uint32_t id = c.id;
    if (id == tag4("J022")) {
        // FUN_0010cbb0 + FUN_0010cf60: texture packs M55_TEX / ME55_00 loaded (resident), the model ME55_00, then
        // J046 and J047 are created through the factory (FUN_0012ce70(0x50b1a0, tag, 1))
        objLoadModel(c, "DATA/MAP/M55/ME55_00.IGB", 0, 0x40000001);
        characters().createTag(tag4("J046"), true);
        characters().createTag(tag4("J047"), true);
        return;
    }
    if (id == tag4("J023")) {
        objLoadModel(c, "DATA/MAP/M99/MR99_00.IGB", 0, 0x40000001);
        return;
    }
    for (const JModel& m : kJModels) {
        if (tag4(m.tag) != id) continue;
        if (m.mode == 2) objLoadModel(c, m.path, 0, 0x48000031);
        else if (m.mode == 1) objLoadModel(c, m.path, 1, 0x40000031);
        else objLoadModel(c, m.path, 0, 0x40000031);
        return;
    }
}

// =================================================================================================================
// O??A: receptacles (collector boxes O02A.., the item teleporters O00A <-> O01A). ctor FUN_001b7680 (+0x24 radius,
// +0x28 partner, +0x2c flags = 0), init FUN_001b7740 (radius table 0x4991f0 by number), tick FUN_001b77a0.
// Strings 0x462e50 "O00A", 0x462e58 "O01A", 0x462e60 "O02A", 0x462e68 "O03A"; DAT_00462e48 = 30.0.
// =================================================================================================================
constexpr float kOARadius[16] = {200.0f, 200.0f, 100.0f, 110.0f, 175.0f, 70.0f, 70.0f, 60.0f,
                                 65.0f,  200.0f, 130.0f, 130.0f, 110.0f, 110.0f, 0.0f, 0.0f};

void oaInit(Chr& c) {   // orig FUN_001b7740
    const int n = tagNumber(c.id);
    od(c).F(0x24) = (n >= 0 && n < 16) ? kOARadius[n] : 0.0f;   // UNVERIFIED: indices past the 16-entry table
}

// orig FUN_001b7dd0: +0x28 = the first entity of list 0x50b000 (objects) with that id
void oaFindPartner(Chr& c, uint32_t tag) {
    for (Chr* e : characters().list()) {
        if (e->type != kChrObject || e->obj == nullptr) continue;
        if (e->id == tag) {
            od(c).P(0x28) = e;
            return;
        }
    }
}

// item +0x74: the receptacle a homing item flies to (written by FUN_001b77a0, not a member of the port's Item)
std::unordered_map<const Item*, Chr*>& homingTarget() {
    static std::unordered_map<const Item*, Chr*> m;
    return m;
}

Vec3 v3(const Vec3f& v) { return Vec3{v[0], v[1], v[2]}; }

// orig FUN_001b7e50(receptacle, item): O00A / O01A pass the item on (launched from the partner +0x28 in a random
// direction, 30 per tick); other boxes take it into the Collector's bag 0x53dc00 once (+0x2c bit 0), else throw it
// back at the player (30 / lift 1; "O09A" 50 / lift 2). Strings 0x462e78 "O09A", 0x462e80 "GN00"; DAT_00462e70 = 30.
void oaHandOver(Chr& c, Item& it) {
    ObjData& o = od(c);
    ItemSystem& sys = itemSystem();
    GameAccess& g = gameAdapter();
    CharacterAccess& self = chrAdapter(&c);
    const Chr* ent = itemEntityOf(&it);
    const Vec3f ipos = ent != nullptr ? ent->xf.m.pos : Vec3f{it.pos.x, it.pos.y, it.pos.z};
    const Vec3f s{100.0f, 100.0f, 100.0f};
    if (c.id == tag4("O00A") || c.id == tag4("O01A")) {
        g.message(0x25f, MsgArg(itemDisplayName(it, sys)));   // FUN_0015d480(FUN_00114da0(0x25f), FUN_001d3ba0(item))
        objSe3d(0xee, ipos);
        fxSpawn(0x9f, &ipos, nullptr, &s);
        const float r = static_cast<float>(static_cast<int>(gameWorld().rng.rand(0x8000)));
        Vec3f v;
        rotateY(((r / 32768.0f) * 359.0f * 3.1415927f) / 180.0f, v, kAxisZ);   // FUN_00206ca0(angle, &v, FUN_00207010())
        normalize(v);
        v[1] = 1.0f;
        normalize(v);
        v = {v[0] * 30.0f, v[1] * 30.0f, v[2] * 30.0f};
        it.flags |= kItemThrownBack;                                    // +0x70 |= 0x2000000
        const Chr* partner = o.P(0x28);
        const Vec3f from = partner != nullptr ? partner->xf.m.pos : pos(c);   // UNVERIFIED: null partner (crash in the original)
        launchItem(it, v3(from), v3(v), &self);                         // FUN_001d5780(item, partner pos, v, obj)
        return;
    }
    if ((o.I(0x2c) & 1) == 0 && collectorBag().put(&it, false, sys)) {   // FUN_001dad80(0x53dc00, item, 0)
        g.message(0x25f, MsgArg(itemDisplayName(it, sys)));
        objSe3d(0xee, ipos);
        fxSpawn(0x9f, &ipos, nullptr, &s);
        o.I(0x2c) |= 1;
        itemEntityUnlink(it);                                           // FUN_001d3b30
        flags().set(0x55);
        if (c.id == tag4("O09A") && it.id == tag4("GN00")) flags().set(0x3cd);
        counters().add(0x13, 1);                                        // FUN_00179570(0x53c5c0, 0x13, 1)
        g.setAchievementFlag(0x467);                                    // FUN_001793b0(0x53c3b0, 0x467)
        g.talkEventHandOver(&it, true);                                 // FUN_001a6230(0x53c6e0, item, 1)
        return;
    }
    g.message((o.I(0x2c) & 1) == 0 ? 0x2ee : 0x2dc);
    objSe3d(3, ipos);
    float speed = 30.0f, lift = 1.0f;
    if (c.id == tag4("O09A")) {
        speed = 50.0f;
        lift = 2.0f;
    }
    const Chr* pl = characters().player();
    Vec3f d = pl != nullptr ? Vec3f{pl->xf.m.pos[0] - pos(c)[0], pl->xf.m.pos[1] - pos(c)[1], pl->xf.m.pos[2] - pos(c)[2]}
                            : Vec3f{0.0f, 0.0f, 0.0f};
    if (pos(c)[1] < 100.0f) {
        normalize(d);
        d[1] = lift;
    } else {
        d[1] = 0.0f;
    }
    normalize(d);
    d = {d[0] * speed, d[1] * speed, d[2] * speed};
    it.flags |= kItemThrownBack;
    launchItem(it, v3(pos(c)), v3(d), &self);                           // FUN_001d5780(item, obj pos, d, obj)
    g.talkEventHandOver(&it, false);                                    // FUN_001a6230(0x53c6e0, item, 0)
}

// orig FUN_001b77a0: O00A / O01A find their partner once; while flag 0x2c is clear, a receptacle other than
// O00A..O03A sets it when it is in the player's room (FUN_001b75d0). Items in flight (+0x70 0x1000000, not thrown
// back) within the radius are handed over; within 1.5 radius they home in (+0x70 0x4000000, +0x74 = box): state pop,
// FUN_00142140, steering 10 deg per tick toward the box (FUN_0039ddd0 0x3e32b8c2) at 30 per tick.
void oaTick(Chr& c) {
    ObjData& o = od(c);
    if (c.id == tag4("O00A")) {
        if (o.P(0x28) == nullptr) oaFindPartner(c, tag4("O01A"));
    } else if (c.id == tag4("O01A") && o.P(0x28) == nullptr) {
        oaFindPartner(c, tag4("O00A"));   // a1 still holds "O00A" from the first compare (disasm 0x1b7808 / 0x1b78b0)
    }
    if ((floorFlagWord() & 1u) == 0) return;
    if (!flags().test(0x2c)) {
        const uint32_t id = c.id;
        if (id != tag4("O00A") && id != tag4("O01A") && id != tag4("O02A") && id != tag4("O03A") &&
            sameRoomAsPlayer(pos(c)))
            flags().set(0x2c);
    }
    const float r = o.F(0x24);
    const float r15 = r * 1.5f * r * 1.5f;
    forEachItem([&](Chr& e) {
        if ((e.taskFlags & 1u) == 0 || (e.taskFlags & 0x10000000u) != 0) return;
        Item& it = *e.item;
        if ((it.flags & kItemInFlight) == 0 || (it.flags & kItemThrownBack) != 0) return;
        Vec3f d{e.xf.m.pos[0] - pos(c)[0], e.xf.m.pos[1] - pos(c)[1], e.xf.m.pos[2] - pos(c)[2]};
        const float dd = d[2] * d[2] + d[0] * d[0] + d[1] * d[1];
        if (dd < r * r) {
            it.flags &= ~0x4000000u;
            oaHandOver(c, it);
            return;
        }
        if (dd < r15 && (it.flags & 0x4000000u) == 0) {
            it.flags |= 0x4000000u;
            homingTarget()[&it] = &c;
        }
        const bool mine = (it.flags & 0x4000000u) != 0 && homingTarget()[&it] == &c;
        if (!mine) return;
        ItemStateSlot& s = it.state;   // +0x78: state pop request (+0x18 = -1, +8 = 0, +0xc = +0x10 = 1)
        if ((s.factory == 0 && s.pending == 0) || s.param0 < 2) {
            s.pending = -1;
            s.param0 = 1;
            s.param1 = 1;
        }
        it.physFlags &= ~3u;   // FUN_00142140(phys): +0x4c &= ~3, +0x54 = 0
        it.vy = 0.0f;
        d = {-d[0], -d[1], -d[2]};
        normalize(d);
        Vec3f v{it.moveVel.x, it.moveVel.y, it.moveVel.z};
        const float vl = normalize(v);
        if (!(vl < 0.1f)) {   // < 0.1: FUN_001d5850(item, 0) (its 30.0 result is not used)
            const float dot = v[1] * d[1] + v[0] * d[0] + v[2] * d[2];   // mula / madda / madd
            if (dot < 0.9f) {
                Vec3f ax{d[1] * v[2] - d[2] * v[1], d[2] * v[0] - d[0] * v[2], d[0] * v[1] - d[1] * v[0]};
                normalize(ax);
                float m[12] = {};
                axisAngle(m, 0.17453294f, ax);   // 0x3e32b8c2
                d = mulVec3(d, m);
            }
        }
        d = {d[0] * 30.0f, d[1] * 30.0f, d[2] * 30.0f};   // DAT_00462e48
        it.moveVel = {d[0], d[1], d[2]};
        it.pos = {it.pos.x + d[0], it.pos.y + d[1], it.pos.z + d[2]};
        e.xf.m.pos = {it.pos.x, it.pos.y, it.pos.z};       // FUN_00109d40 on the item's model
    });
}

// FUN_001d4860 in FUN_001bc620: fire turns HT (class 3) into "HT02", MT (class 4) into "MT03"
void objCookItem(Chr& e) {
    Item& it = *e.item;
    if (it.cls == 3 && it.id != tag4("HT02")) changeItemId(it, tag4("HT02"), itemSystem());
    else if (it.cls == 4 && it.id != tag4("MT03")) changeItemId(it, tag4("MT03"), itemSystem());
}

// =================================================================================================================
// O??C (number outside 70..92): floor switches. ctor FUN_001b9ac0 (FUN_0012af10, +0x30 = 0), init FUN_001b9bc0
// (+0x28 number, +0x2c model index, model table 0x463080, counter 0x53d7f8), tick FUN_001b9ca0. Stepping on it
// (any character of types 0/2 within +-100 x/z, +-20 y) flips the plate (FUN_00128c00(pi)), SE 0x1d, effect 0xd9
// and triggers the targets of its number (tags 0x463240..0x4632eb, FUN_001bb730); numbers 0x2d..0x2f switch their
// O??H targets off again when released (FUN_001bb800). DAT_00463098 / 0a0 / 0a8 = 100.0.
// =================================================================================================================
const char* const kOCModel[5] = {"DATA/MAP/M06/MG06_00.IGB", "DATA/MAP/M02/MG02_01.IGB", "DATA/MAP/M03/MG03_00.IGB",
                                 "DATA/MAP/M07/MG07_04.IGB", "DATA/MAP/M09/MG09_00.IGB"};

struct SwitchTargets {
    int n1, n2;            // the numbers of the branch (n2 = alternative number or -1)
    const char* tags[8];
};
// the branches of FUN_001b9ca0 (number -> FUN_001bb730 targets), in the original test order
const SwitchTargets kSwitch[] = {
    {0x32, -1, {"O02I"}}, {0x31, -1, {"O01I"}}, {0x30, -1, {"O00I"}},
    {0x2f, -1, {"O10H", "O11H", "O12H"}}, {0x2e, -1, {"O04H", "O05H", "O06H"}}, {0x2d, -1, {"O00H", "O01H"}},
    {0x2c, -1, {"O04G"}}, {0x2b, -1, {"O03G"}}, {0x2a, -1, {"O02G"}}, {0x29, -1, {"O01G"}}, {0x28, -1, {"O00G"}},
    {0x23, -1, {"O05F"}}, {0x22, -1, {"O04F"}}, {0x21, -1, {"O03F"}}, {0x20, -1, {"O02F"}}, {0x1f, -1, {"O01F"}},
    {0x1e, -1, {"O00F"}},
    {0x1c, -1, {"O34E", "O35E", "O36E", "O37E", "O38E", "O39E"}},
    {0x1b, -1, {"O26E", "O27E", "O28E", "O29E", "O30E", "O31E", "O32E", "O33E"}},
    {0x1a, -1, {"O20E", "O21E", "O22E", "O23E", "O24E", "O25E"}},
    {0x19, -1, {"O08E", "O09E"}}, {0x18, -1, {"O06E", "O07E"}}, {0x17, -1, {"O04E", "O05E"}}, {0x16, -1, {"O03E"}},
    {0x15, -1, {"O02E"}}, {0x14, -1, {"O00E", "O01E"}},
    {0x61, 0xe, {"O17D", "O18D", "O19D"}}, {0x60, 0xd, {"O15D", "O16D"}}, {0xc, -1, {"O14D"}}, {0xb, -1, {"O13D"}},
    {10, -1, {"O12D"}}, {0x5f, 9, {"O09D", "O10D", "O11D"}}, {0x5e, 8, {"O07D", "O08D"}},
    {0x5d, 7, {"O04D", "O05D", "O06D"}}, {6, -1, {"O03D"}}, {5, -1, {"O02D"}},
};

void ocInit(Chr& c) {   // orig FUN_001b9bc0
    ObjData& o = od(c);
    const int n = tagNumber(c.id);
    o.I(0x28) = n;
    int k;
    if (n <= 0x1d || 0x5c < n) k = 0;
    else if (n < 0x28) k = 1;
    else if (n < 0x2d) k = 2;
    else if (n < 0x30) k = 3;
    else k = 4;
    o.I(0x2c) = k;
    objLoadModel(c, kOCModel[k], 0, 0x40000011);
    // iRam0053d7f8 + 1: per-floor gimmick count (read by the floor map / talk code, not modelled)
}

void triggerD(Chr& d);     // FUN_001bb840
void triggerE(Chr& e);     // FUN_001bc260
void triggerFGI(Chr& f);   // FUN_001bc490
void triggerH(Chr& h);     // FUN_001bcac0
void stopH(Chr& h);        // FUN_001bcee0

// orig FUN_001bb730(switch, tag): the first live object with that tag, by its class letter
void switchTarget(uint32_t tag) {
    const char k = tagChar(tag, 3);
    Chr* t = nullptr;
    if (k == 'D' || k == 'E' || k == 'F' || k == 'G' || k == 'I' || k == 'H') t = findEntity(tag, kChrObject);   // FUN_0012c780
    if (t == nullptr) return;   // UNVERIFIED: the original calls the trigger with a null object (crash) when absent
    if (k == 'D') triggerD(*t);
    else if (k == 'E') triggerE(*t);
    else if (k == 'G' || k == 'I' || k == 'F') triggerFGI(*t);
    else if (k == 'H') triggerH(*t);
}
// orig FUN_001bb800(switch, tag): O??H targets switched off
void switchRelease(uint32_t tag) {
    if (tagChar(tag, 3) != 'H') return;
    if (Chr* t = findEntity(tag, kChrObject)) stopH(*t);
}

void ocTick(Chr& c) {   // orig FUN_001b9ca0
    ObjData& o = od(c);
    if ((floorFlagWord() & 1u) == 0) return;
    if ((o.I(0x30) & 1) == 0) {
        if (!sameRoomAsPlayer(pos(c))) {   // FUN_001b75d0
            objPublish(c);
            return;
        }
    }
    bool on = false;
    forEachCharacter([&](Chr& e) {
        if (on || (e.taskFlags & 0x10000000u) != 0 || e.type == kChrNpc) return;
        const Vec3f& p = e.xf.m.pos;
        const Vec3f& q = pos(c);
        if (q[0] - 100.0f < p[0] && p[0] < q[0] + 100.0f && q[2] - 100.0f < p[2] && p[2] < q[2] + 100.0f &&
            p[1] < q[1] + 20.0f && q[1] - 20.0f < p[1])
            on = true;
    });
    if (on) {
        if ((o.I(0x30) & 1) != 0) {
            objPublish(c);
            return;
        }
        o.I(0x30) |= 1;
        objSe3d(0x1d, pos(c));
        const int h = fxSpawn(0xd9, nullptr, nullptr, nullptr);
        float m[16];
        const ChrTransform& t = c.xf.m;
        axesMatrix(m, t.right, 100.0f, t.up, 100.0f, t.fwd, 100.0f, Vec3f{t.pos[0], t.pos[1] + 11.0f, t.pos[2]});
        fxSetMatrix(h, m);
        rotateAboutRight(c.xf.m, 3.1415927f);   // FUN_00128c00(0x40490fdb)
        const int n = o.I(0x28);
        bool found = false;
        for (const SwitchTargets& s : kSwitch) {
            if (s.n1 != n && s.n2 != n) continue;
            for (const char* tg : s.tags)
                if (tg != nullptr) switchTarget(tag4(tg));
            found = true;
            break;
        }
        // numbers 1..4 -> "O01D", 0 -> "O00D"; the remaining numbers trigger nothing
        if (!found) {
            if (n == 4 || n == 3 || n == 2 || n == 1) switchTarget(tag4("O01D"));
            else if (n == 0) switchTarget(tag4("O00D"));
        }
    } else if ((o.I(0x30) & 1) != 0) {
        o.I(0x30) &= ~1;
        rotateAboutRight(c.xf.m, 3.1415927f);
        const int n = o.I(0x28);
        if (n == 0x2f) {
            switchRelease(tag4("O10H"));
            switchRelease(tag4("O11H"));
            switchRelease(tag4("O12H"));
        } else if (n == 0x2e) {
            switchRelease(tag4("O04H"));
            switchRelease(tag4("O05H"));
            switchRelease(tag4("O06H"));
        } else if (n == 0x2d) {
            switchRelease(tag4("O00H"));
            switchRelease(tag4("O01H"));
        }
    }
    objPublish(c);
}

// =================================================================================================================
// O??D: trap launchers (no model, no tick). Triggered by a switch: FUN_001bb840 creates the projectile helper
// FUN_001c0990 (ctor FUN_001bb990: +0x24 delay 0x2d, +0x28 effect -1, +0x2c SE handle 0; vtable 0x4ce420, tick
// FUN_001bbaa0) with tag "O00D" (number < 0xc) or "O01D" (0x4630b0 / 0x4630b8) at the launcher's transform.
// =================================================================================================================
constexpr uint32_t kHelperDart = 0x1bb990;   // ctor FUN_001bb990 (tick FUN_001bbaa0)
constexpr uint32_t kHelperBurst = 0x1bc540;  // ctor FUN_001bc540 (tick FUN_001bc620)

void triggerD(Chr& d) {   // orig FUN_001bb840
    const int n = tagNumber(d.id);
    auto h = newHelper(kHelperDart);         // FUN_001c0990
    h->obj->I(0x24) = 0x2d;
    h->obj->I(0x28) = -1;
    h->obj->I(0x2c) = 0;
    linkHelper(std::move(h), n < 0xc ? tag4("O00D") : tag4("O01D"), d, true);
}

// orig FUN_001bbaa0: after the delay a dart effect (0x9e "O00D" / 0xa0) flies 30 per tick along the forward axis;
// a wall (FUN_00143150, radius 10) or a hurt volume (FUN_0014ad00 radius 10, element 1 for "O?0?" tags, else 2)
// ends it with the burst 0x2d / 0x28 and the helper is removed (FUN_00207350). DAT_004632f0 = 30, 0x4632f8 = 10,
// DAT_00463300..0x463340 = 100.
void dartTick(Chr& c) {
    ObjData& o = od(c);
    const bool zero = tagChar(c.id, 2) == '0';
    if (o.I(0x24) != 0) {
        const int left = o.I(0x24) - 1;
        o.I(0x24) = left;
        if (left == 0) {
            objSe3d(0xc, pos(c));
            if (zero) {
                o.I(0x28) = fxSpawn(0x9e, nullptr, nullptr, nullptr);
                o.I(0x2c) = objSe3d(0xb, pos(c), 101.0f);   // UNVERIFIED: vol 0x42ca0000 (101.0) as passed in f12
            } else {
                o.I(0x28) = fxSpawn(0xa0, nullptr, nullptr, nullptr);
                o.I(0x2c) = objSe3d(0x31, pos(c), 101.0f);
            }
        }
        return;
    }
    const ChrTransform& t = c.xf.m;
    const Vec3f step{t.fwd[0] * 30.0f, t.fwd[1] * 30.0f, t.fwd[2] * 30.0f};
    float m[16];
    if (raycast(t.pos.data(), step.data(), 10.0f, nullptr)) {   // FUN_00143150(pos, step, 10)
        const int h = fxSpawn(zero ? 0x2d : 0x28, nullptr, nullptr, nullptr);
        axesMatrix(m, t.right, 100.0f, t.up, 100.0f, t.fwd, 100.0f, t.pos);
        fxSetMatrix(h, m);
        entityRemove(c);
        return;
    }
    const uint32_t m1 = zero ? 1u : 2u;
    if (hitManager().testPoint(10.0f, 0.0f, &c, 0, t.pos, m1, 0, 0, 0, nullptr)) {   // FUN_0014ad00
        const int h = fxSpawn(zero ? 0x2d : 0x28, nullptr, nullptr, nullptr);
        axesMatrix(m, t.right, 100.0f, t.up, 100.0f, t.fwd, 100.0f, t.pos);
        fxSetMatrix(h, m);
        entityRemove(c);
        return;
    }
    c.xf.m.pos = {t.pos[0] + step[0], t.pos[1] + step[1], t.pos[2] + step[2]};
    axesMatrix(m, t.right, 100.0f, t.up, 100.0f, t.fwd, 100.0f, c.xf.m.pos);
    fxSetMatrix(o.I(0x28), m);
    objSeMove(o.I(0x2c), c.xf.m.pos);   // FUN_00116260(snd, +0x2c, pos): the looping SE follows the dart
}


// =================================================================================================================
// O??E: sliding blades (MG06_02). ctor FUN_001bbfc0 (FUN_0012af10, model, counter 0x53d800, PTMF +0x2c null,
// +0x4c = 0), init FUN_001bc100 (+0x28 number; 0x14..0x27 rotated 3pi/2 about the forward axis), tick FUN_001bc170
// (state, then the blade's hurt sphere 80 at pos - right * 200 (DAT_00463378), m3 0x1002, +0x4c counter).
// Trigger FUN_001bc260: base +0x38 = pos, +0x44 extension 0, PTMF FUN_001bc300 (0x463380), SE 0x32.
// States (disasm): FUN_001bc300 extend +30 to 200 then +0x48 = 30, FUN_001bc3b0; FUN_001bc3b0 wait;
// FUN_001bc3f0 retract -20 to 0 then null. pos = base - right * extension in each.
// =================================================================================================================
constexpr uint32_t kOEExtend = 0x1bc300, kOEWait = 0x1bc3b0, kOERetract = 0x1bc3f0;

void oeCtor(Chr& c) {   // orig FUN_001bbfc0
    objLoadModel(c, "DATA/MAP/M06/MG06_02.IGB", 0, 0x40000011);   // PTR 0x463370 -> 0x463350
    od(c).M(0x2c) = 0;
    od(c).I(0x4c) = 0;
}
void oeInit(Chr& c) {   // orig FUN_001bc100
    const int n = tagNumber(c.id);
    od(c).I(0x28) = n;
    if (0x13 < n && n < 0x28) rotateAboutForward(c.xf.m, 4.712389f);   // FUN_001289c0(0x4096cbe4)
}
void oeSlide(Chr& c) {   // pos = base - right * extension (adda 0 + base, msub right * d)
    ObjData& o = od(c);
    const float d = o.F(0x44);
    const ChrTransform& t = c.xf.m;
    c.xf.m.pos = {o.F(0x38) - t.right[0] * d, o.F(0x3c) - t.right[1] * d, o.F(0x40) - t.right[2] * d};
}
void oeState(Chr& c) {
    ObjData& o = od(c);
    switch (o.M(0x2c)) {
    case kOEExtend: {   // FUN_001bc300
        const float d = o.F(0x44) + 30.0f;
        o.F(0x44) = d;
        if (!(d <= 200.0f)) {
            o.F(0x44) = 200.0f;
            o.I(0x48) = 0x1e;
            o.M(0x2c) = kOEWait;
        }
        oeSlide(c);
        break;
    }
    case kOEWait:   // FUN_001bc3b0
        o.I(0x48) = o.I(0x48) - 1;
        if (o.I(0x48) == 0) o.M(0x2c) = kOERetract;
        break;
    case kOERetract: {   // FUN_001bc3f0
        const float d = o.F(0x44) - 20.0f;
        o.F(0x44) = d;
        if (d < 0.0f) {
            o.F(0x44) = 0.0f;
            o.M(0x2c) = 0;
        }
        oeSlide(c);
        break;
    }
    default: break;
    }
}
void oeTick(Chr& c) {   // orig FUN_001bc170
    ObjData& o = od(c);
    if (o.M(0x2c) != 0) {   // FUN_0040caf0(+0x2c) -> FUN_0040ca60
        oeState(c);
        const ChrTransform& t = c.xf.m;
        const Vec3f p{t.pos[0] - t.right[0] * 200.0f, t.pos[1] - t.right[1] * 200.0f, t.pos[2] - t.right[2] * 200.0f};
        hitManager().testPoint(80.0f, 0.0f, &c, 0, p, 0, 0, 0x1002, 0, nullptr);   // FUN_0014ad00
        o.I(0x4c) = o.I(0x4c) + 1;
    }
    objPublish(c);
}
void triggerE(Chr& e) {   // orig FUN_001bc260
    ObjData& o = od(e);
    if (o.M(0x2c) != 0) return;
    o.F(0x38) = e.xf.m.pos[0];
    o.F(0x3c) = e.xf.m.pos[1];
    o.F(0x40) = e.xf.m.pos[2];
    o.F(0x44) = 0.0f;
    o.M(0x2c) = kOEExtend;   // 0x463380
    objSe3d(0x32, e.xf.m.pos);
}

// =================================================================================================================
// O??F / O??G / O??I: burst emitters (no model, no tick). Trigger FUN_001bc490: helper FUN_001c0a00 (ctor
// FUN_001bc540: +0x24 effect -1; vtable 0x4ce3f0, tick FUN_001bc620) with the emitter's own tag at its transform.
// =================================================================================================================
void triggerFGI(Chr& f) {   // orig FUN_001bc490
    auto h = newHelper(kHelperBurst);
    h->obj->I(0x24) = -1;
    linkHelper(std::move(h), f.id, f, true);
}

// orig FUN_001bc620: the burst effect (G 0xa3 + SE 0x34, I 0xa5 + SE 0xba, F 0xa2 + SE 0xba) at the position;
// while it exists a hurt sphere of 500 (element 1 for F / I, m3 0x1000) and, for F / I, the lying items of classes
// HT (3) / MT (4) within 500 that are not "HT02" / "MT03" (0x4633b0 / 0x4633b8) become those (FUN_001d4860).
void burstTick(Chr& c) {
    ObjData& o = od(c);
    const char k = tagChar(c.id, 3);
    if (o.I(0x24) == -1) {
        const Vec3f rot{0.0f, 0.0f, 0.0f};
        const Vec3f s{100.0f, 100.0f, 100.0f};
        if (k == 'G') {
            o.I(0x24) = fxSpawn(0xa3, &pos(c), &rot, &s);
            objSe3d(0x34, pos(c));
        } else if (k == 'I') {
            o.I(0x24) = fxSpawn(0xa5, &pos(c), &rot, &s);
            objSe3d(0xba, pos(c));
        } else if (k == 'F') {
            o.I(0x24) = fxSpawn(0xa2, &pos(c), &rot, &s);
            objSe3d(0xba, pos(c));
        }
    }
    if (!fxExists(o.I(0x24))) {
        entityRemove(c);
        return;
    }
    const bool fire = k == 'I' || k == 'F';
    hitManager().testPoint(500.0f, 0.0f, &c, 0, pos(c), fire ? 1u : 0u, 0, 0x1000, 0, nullptr);
    if (!fire) return;
    forEachItem([&](Chr& e) {
        if ((e.taskFlags & 1u) == 0 || (e.taskFlags & 0x10000000u) != 0) return;
        const Vec3f& p = e.xf.m.pos;
        const float dz = p[2] - pos(c)[2], dy = p[1] - pos(c)[1], dx = p[0] - pos(c)[0];
        if (!(dy * dy + dx * dx + dz * dz < 250000.0f)) return;   // mula dy, madda dx, madd dz (0x1bc8e0)
        objCookItem(e);   // FUN_001d4860 on HT (+0x24 == 3, id != HT02) / MT (4, id != MT03)
    });
}

// =================================================================================================================
// O??H: electric posts. ctor FUN_001bc9e0 (FUN_0012ae60; +0x24 partner 0, +0x28 effect -1, +0x2c SE 0, +0x30 0),
// tick FUN_001bcf40. Trigger FUN_001bcac0 links the partner (number -> tag, 0x4633e0..), SE 0x35 at the middle and
// the beam effect 0xa4 (axes X/Y/Z * 100 at the middle, DAT_004633c0 = 0.5); FUN_001bcee0 switches it off.
// =================================================================================================================
void ohCtor(Chr& c) {
    ObjData& o = od(c);
    o.P(0x24) = nullptr;
    o.I(0x28) = -1;
    o.I(0x2c) = 0;
    o.I(0x30) = 0;
}
void triggerH(Chr& c) {   // orig FUN_001bcac0
    ObjData& o = od(c);
    if (o.I(0x28) != -1) return;
    const int n = tagNumber(c.id);
    const char* partner;
    if (n == 0) partner = "O02H";
    else if (n == 1) partner = "O03H";
    else if (n == 4) partner = "O07H";
    else if (n == 5) partner = "O08H";
    else if (n == 6) partner = "O09H";
    else if (n == 10) partner = "O13H";
    else if (n == 0xb) partner = "O14H";
    else partner = "O15H";
    Chr* p = findEntity(tag4(partner), kChrObject);   // FUN_0012c780
    o.P(0x24) = p;
    if (p == nullptr) return;   // UNVERIFIED: the original dereferences the null partner
    const Vec3f& a = pos(c);
    const Vec3f& b = p->xf.m.pos;
    const Vec3f mid{(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
    o.I(0x2c) = objSe3d(0x35, mid);
    o.I(0x28) = fxSpawn(0xa4, nullptr, nullptr, nullptr);
    float m[16];
    axesMatrix(m, kAxisX, 100.0f, kAxisY, 100.0f, kAxisZ, 100.0f, mid);
    fxSetMatrix(o.I(0x28), m);
}
void stopH(Chr& c) {   // orig FUN_001bcee0
    ObjData& o = od(c);
    if (o.I(0x28) != -1) {
        fxStop(o.I(0x28));   // FUN_00112560
        o.I(0x28) = -1;
    }
    if (o.I(0x2c) != 0) {
        objSeStop(o.I(0x2c));   // FUN_00115820(snd, handle, 2)
        o.I(0x2c) = 0;
    }
}
void ohTick(Chr& c) {   // orig FUN_001bcf40: hurt spheres of 50 every 100 along the beam (m1 4, m3 0x1000)
    ObjData& o = od(c);
    if (o.I(0x28) == -1 || o.P(0x24) == nullptr) return;
    const uint32_t hitId = static_cast<uint32_t>(o.I(0x30)) & 0xfffffff8u;
    Vec3f p = pos(c);
    const Vec3f& q = o.P(0x24)->xf.m.pos;
    Vec3f d{q[0] - p[0], q[1] - p[1], q[2] - p[2]};
    const float len = normalize(d);
    for (int k = 0; k < static_cast<int>(len); k += 100) {
        hitManager().testPoint(50.0f, 0.0f, &c, static_cast<int>(hitId), p, 4, 0, 0x1000, 0, nullptr);
        p = {p[0] + d[0] * 100.0f, p[1] + d[1] * 100.0f, p[2] + d[2] * 100.0f};   // DAT_00463420 = 100
    }
    o.I(0x30) = o.I(0x30) + 1;
}

// =================================================================================================================
// O??L: stalactites (MG05_02). ctor FUN_001bec50 (FUN_0012af10, PTMF +0x28 = FUN_001bee30 (0x463688), +0x34 =
// rand(0x3c)), init FUN_001bed70 (model, counter 0x53d830), tick FUN_001bedc0 (state, publish; floor flag 1 only).
// States: FUN_001bee30 idle (drip 0xa7 every 30 + 2 rand(0x1e) ticks while the player is in the room; a character
// of types 0/2 within 150 in x/z starts the fall: +0x38 base y, +0x3c delay 30, +0x40 speed 0, FUN_001bf0a0),
// FUN_001bf0a0 fall (speed -1 per tick, hurt sphere 30 element 2; on a hit or below y 0: burst 0x28, back to
// base + 100, FUN_001bf1f0), FUN_001bf1f0 regrow (-0.1 per tick down to the base, then idle).
// =================================================================================================================
constexpr uint32_t kOLIdle = 0x1bee30, kOLFall = 0x1bf0a0, kOLRegrow = 0x1bf1f0;

void olCtor(Chr& c) {
    od(c).M(0x28) = kOLIdle;
    od(c).I(0x34) = static_cast<int32_t>(gameWorld().rng.rand(0x3c));   // FUN_002071e0(0x53eb20, 0x3c)
}
void olInit(Chr& c) { objLoadModel(c, "DATA/MAP/M05/MG05_02.IGB", 0, 0x40000011); }   // counter 0x53d830 + 1
void olState(Chr& c) {
    ObjData& o = od(c);
    switch (o.M(0x28)) {
    case kOLIdle: {   // FUN_001bee30
        if (!sameRoomAsPlayer(pos(c))) return;
        if (o.I(0x34) == 0) {
            o.I(0x34) = static_cast<int32_t>(gameWorld().rng.rand(0x1e)) * 2 + 0x1e;
            Vec3f p{pos(c)[0], 0.1f, pos(c)[2]};
            p[0] = p[0] + (static_cast<float>(static_cast<int>(gameWorld().rng.rand(0x14))) - 10.0f);
            p[2] = p[2] + (static_cast<float>(static_cast<int>(gameWorld().rng.rand(0x14))) - 10.0f);
            const Vec3f s{100.0f, 100.1f, 100.0f};   // (0x42c80000, 0x42c83333, 0x42c80000)
            fxSpawn(0xa7, &p, nullptr, &s);
        } else {
            o.I(0x34) = o.I(0x34) - 1;
        }
        forEachCharacter([&](Chr& e) {
            if ((e.taskFlags & 0x10000000u) != 0 || e.type == kChrNpc) return;
            const float dx = e.xf.m.pos[0] - pos(c)[0], dz = e.xf.m.pos[2] - pos(c)[2];
            if (dz * dz + dx * dx + 0.0f < 22500.0f) {
                o.M(0x28) = kOLFall;   // 0x463698
                o.F(0x38) = pos(c)[1];
                o.I(0x3c) = 0x1e;
                o.F(0x40) = 0.0f;
            }
        });
        break;
    }
    case kOLFall: {   // FUN_001bf0a0
        if (o.I(0x3c) != 0) {
            o.I(0x3c) = o.I(0x3c) - 1;
            return;
        }
        const float v = o.F(0x40) - 1.0f;
        o.F(0x40) = v;
        pos(c)[1] = pos(c)[1] + v;
        const bool hit = hitManager().testPoint(30.0f, 0.0f, &c, 0, pos(c), 2, 0, 0, 0, nullptr);
        if (!hit && 0.0f <= pos(c)[1]) return;
        fxAt(0x28, pos(c));
        o.M(0x28) = kOLRegrow;   // 0x4636a8
        pos(c)[1] = o.F(0x38) + 100.0f;
        break;
    }
    case kOLRegrow: {   // FUN_001bf1f0
        pos(c)[1] = pos(c)[1] + -0.1f;   // 0xbdcccccd
        if (pos(c)[1] < o.F(0x38)) {
            pos(c)[1] = o.F(0x38);
            o.M(0x28) = kOLIdle;   // 0x4636b8
        }
        break;
    }
    default: break;
    }
}
void olTick(Chr& c) {   // orig FUN_001bedc0
    if ((floorFlagWord() & 1u) == 0) return;
    if (od(c).M(0x28) != 0) olState(c);
    objPublish(c);
}

}  // namespace

// The class-method tables (vtables 0x4cd4d0..0x4cd8f0)
void objClassConstruct(Chr& c) {
    ObjData& o = od(c);
    switch (o.cls) {
    case ObjClass::J017: break;                   // FUN_0012af10: model holder only
    case ObjClass::Projectile: projConstruct(c); break;
    case ObjClass::OA:                            // FUN_001b7680
        o.F(0x24) = 0.0f;
        o.P(0x28) = nullptr;
        o.I(0x2c) = 0;
        break;
    case ObjClass::OC: o.I(0x30) = 0; break;      // FUN_001b9ac0
    case ObjClass::OE: oeCtor(c); break;
    case ObjClass::OH: ohCtor(c); break;
    case ObjClass::OL: olCtor(c); break;
    default: objClassConstruct2(c); break;
    }
}

void objClassInit(Chr& c) {
    switch (od(c).cls) {
    case ObjClass::J017: j017Init(c); break;
    case ObjClass::Projectile: break;             // vt+0x1c 0x1c41d0.. (empty)
    case ObjClass::OA: oaInit(c); break;
    case ObjClass::OC: ocInit(c); break;
    case ObjClass::OE: oeInit(c); break;
    case ObjClass::OL: olInit(c); break;
    case ObjClass::OD:
    case ObjClass::OFGI:
    case ObjClass::OH: break;                     // FUN_001286b0 (nop)
    default: objClassInit2(c); break;
    }
}

void objClassTick(Chr& c) {
    switch (od(c).cls) {
    case ObjClass::J017: objPublish(c); break;    // FUN_0012b600
    case ObjClass::Projectile: projTick(c); break;
    case ObjClass::OA: oaTick(c); break;
    case ObjClass::OC: ocTick(c); break;
    case ObjClass::OE: oeTick(c); break;
    case ObjClass::OH: ohTick(c); break;
    case ObjClass::OL: olTick(c); break;
    case ObjClass::OD:
    case ObjClass::OFGI: break;                   // FUN_00132030 / FUN_00131f20 (nop)
    default: objClassTick2(c); break;
    }
}

// vt+0x20: FUN_0012a5d0 (1) for O??A/D/FGI/H/O/P/Q, O50J, J017 family; FUN_0012d390 (model loaded: holder +0x2c bit
// 31) for the others
bool objClassActive(const Chr& c) {
    switch (od(c).cls) {
    case ObjClass::OA: case ObjClass::OC70: case ObjClass::OD: case ObjClass::OFGI: case ObjClass::OH:
    case ObjClass::OO: case ObjClass::OP: case ObjClass::OQ: case ObjClass::O50J:
        return true;
    case ObjClass::Helper: return true;           // 0x4ce420 / 0x4ce3f0: FUN_0012a5d0
    case ObjClass::Projectile: return true;       // FUN_0012a5d0
    default: return (c.model.flags2c & 0x80000000u) != 0;   // FUN_0012d390
    }
}

int objClassContact(Chr& c, Chr* other, uint32_t m1, uint32_t m2, uint32_t m3) {
    (void)m2;
    (void)m3;
    switch (od(c).cls) {
    case ObjClass::OM: case ObjClass::ON: case ObjClass::O00B: return objClassContact2(c, other, m1);
    default: return chrContactDefault(c, other) ? 1 : 0;   // FUN_00129140 (type 4/5 -> 0)
    }
}

void helperTick(Chr& c) {
    switch (od(c).helperKind) {
    case kHelperDart: dartTick(c); break;         // vtable 0x4ce420 +0xc FUN_001bbaa0
    case kHelperBurst: burstTick(c); break;       // vtable 0x4ce3f0 +0xc FUN_001bc620
    default: helperTick2(c); break;
    }
}

}  // namespace baroque::game
