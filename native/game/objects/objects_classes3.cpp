// Object classes placed only beyond floors 100-130 (objects_deep.hpp). Fields are addressed by their original
// offsets (ObjData::I / F / P); floats and strings were read from the ELF (python tools/disasm.py: word / read_bytes).
// Report: work/reports/OBJECTS_NATIVE.md, section "2026-10-10 deep-floor classes".
#include "game/objects/objects_deep.hpp"
#include "game/objects/objects_internal.hpp"

#include "game/character/combat.hpp"

#pragma fenv_access(on)

namespace baroque::game {

namespace {

constexpr Vec3f kAxisX{1.0f, 0.0f, 0.0f};   // FUN_00206f90
constexpr Vec3f kAxisY{0.0f, 1.0f, 0.0f};   // FUN_00206f10
constexpr Vec3f kAxisZ{0.0f, 0.0f, 1.0f};   // FUN_00207010

ObjData& od(Chr& c) { return *c.obj; }
Vec3f& pos(Chr& c) { return c.xf.m.pos; }

// FUN_0039d2e0 identity, then rows right * kr, up * ku, fwd * kf and the position (effect matrix of the classes)
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

// =================================================================================================================
// O??K: moving electric beam posts (MG07_02), placed in pairs O00K-O01K, O02K-O03K, O04K-O05K, O06K-O07K (floors
// F411, F422, F423, F427, F428, F450, F451). Factory FUN_0012ed20 -> vtable 0x4cd740: ctor FUN_001bd090, dtor
// FUN_00131db0 -> FUN_001bd120, init FUN_001bd1f0 (empty), tick FUN_001bd200, active FUN_0012d390 (model loaded),
// contact FUN_00129140 (default), class id 0x132c20 (no list-0x50b000 body).
// Fields: +0x28 partner post (entity), +0x2c beam effect (-1 none), +0x30 looping SE handle (0 none), +0x34 hit
// counter, +0x38 base y, +0x3c height offset, +0x40 vertical speed.
// The posts with an odd second digit (O01K/O03K/O05K/O07K) only publish their model; the even post finds its partner
// (tags 0x463480 "O01K", 0x463488 "O03K", 0x463490 "O05K", 0x463498 "O07K"), then while the player is in its room
// moves both posts up and down together (speed 2 per tick, offset 0..950; 0..500 for the fourth pair), with hurt
// spheres of 50 every 100 along the beam (m1 4, m3 0x1000), beam effect 0xa6 and looping SE 0x35 at the middle.
// DAT_00463458 = 100.0 (hit step), DAT_00463460 = 0.5 (middle), DAT_00463468 / 470 / 478 = 100.0 (effect scale).
// =================================================================================================================
constexpr const char* kOKModel = "DATA/MAP/M07/MG07_02.IGB";   // PTR 0x463450

void okCtor(Chr& c) {   // orig FUN_001bd090 (after FUN_0012af10: model holder +0x24)
    objLoadModel(c, kOKModel, 0, 0x40000011);   // FUN_001099e0(+0x24, path, 0, 0x40000011)
    // iRam0053d808 + 1: model reference count (FUN_001bd120 releases the IGB at 0; resource cache not modelled)
    ObjData& o = od(c);
    o.P(0x28) = nullptr;
    o.I(0x2c) = -1;
    o.I(0x30) = 0;
    o.I(0x34) = 0;
}

// orig FUN_001bd120 (via FUN_00131db0): reference count - 1 (FUN_0010cd50 unload at 0, not modelled), beam effect
// stopped (FUN_00112560) when != -1, looping SE stopped (FUN_00115820(0x4e9790, handle, 2)) when != 0, base dtor
// FUN_0012af60.
void okDtor(Chr& c) {
    ObjData& o = od(c);
    if (o.I(0x2c) != -1) fxStop(o.I(0x2c));
    if (o.I(0x30) != 0) objSeStop(o.I(0x30));
}

void okTick(Chr& c) {   // orig FUN_001bd200
    if ((floorFlagWord() & 1u) == 0) return;
    ObjData& o = od(c);
    const uint32_t digit = (c.id >> 16) & 0xffu;   // second digit of the number
    if (digit == 0x31 || digit == 0x33 || digit == 0x35 || digit == 0x37) {
        objPublish(c);   // FUN_00109d40(+0x24, pos, scale, fwd, up, right)
        return;
    }
    const Vec3f here = pos(c);
    if (!sameRoomAsPlayer(here)) {   // FUN_001b75d0
        if (o.I(0x2c) != -1) {
            fxStop(o.I(0x2c));   // FUN_00112560
            o.I(0x2c) = -1;
        }
        if (o.I(0x30) != 0) {
            objSeStop(o.I(0x30));   // FUN_00115820(0x4e9790, handle, 2)
            o.I(0x30) = 0;
        }
        objPublish(c);
        return;
    }
    if (o.P(0x28) == nullptr) {
        const char* partner;
        if (digit == 0x30) partner = "O01K";        // 0x463480
        else if (digit == 0x32) partner = "O03K";   // 0x463488
        else if (digit == 0x34) partner = "O05K";   // 0x463490
        else partner = "O07K";                      // 0x463498
        o.P(0x28) = findEntity(objTag(partner), kChrObject);   // FUN_0012c780(0x50b130, tag, 4)
        o.F(0x38) = pos(c)[1];
        o.F(0x3c) = 0.0f;
        o.F(0x40) = 2.0f;   // 0x40000000
        return;             // no publish on this tick (original)
    }
    // height offset: bounce between 0 and the pair's limit (0x1bd494..0x1bd560)
    o.F(0x3c) = o.F(0x3c) + o.F(0x40);
    if (o.F(0x40) < 0.0f) {
        if (o.F(0x3c) < 0.0f) {
            o.F(0x3c) = 0.0f;
            o.F(0x40) = -o.F(0x40);
        }
    } else {
        float limit;
        if (digit == 0x30) limit = 950.0f;        // 0x446d8000
        else if (digit == 0x32) limit = 950.0f;
        else if (digit == 0x34) limit = 950.0f;
        else limit = 500.0f;                      // 0x43fa0000
        if (!(o.F(0x3c) <= limit)) {              // c.le.s / bc1t
            o.F(0x3c) = limit;
            o.F(0x40) = -o.F(0x40);
        }
    }
    Chr& p = *o.P(0x28);
    const float y = o.F(0x38) + o.F(0x3c);
    pos(c)[1] = y;
    p.xf.m.pos[1] = y;
    // hurt spheres of 50 every 100 from this post towards the partner
    const uint32_t hitId = static_cast<uint32_t>(o.I(0x34)) & 0xfffffff8u;
    Vec3f at = pos(c);
    Vec3f d{p.xf.m.pos[0] - at[0], p.xf.m.pos[1] - at[1], p.xf.m.pos[2] - at[2]};
    const float len = normalize(d);   // FUN_00206ab0
    for (int k = 0; k < static_cast<int>(len); k += 100) {
        // FUN_0014ad00(50.0, 0, 0x50c780, this, id, &at, 4, 0, 0x1000, 0)
        hitManager().testPoint(50.0f, 0.0f, &c, static_cast<int>(hitId), at, 4, 0, 0x1000, 0, nullptr);
        at = {at[0] + d[0] * 100.0f, at[1] + d[1] * 100.0f, at[2] + d[2] * 100.0f};   // DAT_00463458
    }
    o.I(0x34) = o.I(0x34) + 1;
    if (o.I(0x2c) == -1) {
        o.I(0x2c) = fxSpawn(0xa6, nullptr, nullptr, nullptr);   // FUN_00112190(0, 0xa6, 0, 0, 0)
        o.I(0x30) = objSe3d(0x35, pos(c), 1.0f);                 // FUN_00115470(1.0, 0x4e9790, 0x35, pos)
    }
    const Vec3f& a = pos(c);
    const Vec3f& b = p.xf.m.pos;
    const Vec3f mid{(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};   // DAT_00463460
    objSeMove(o.I(0x30), mid);   // FUN_00116260(0x4e9790, handle, mid)
    float m[16];
    axesMatrix(m, kAxisX, 100.0f, kAxisY, 100.0f, kAxisZ, 100.0f, mid);
    fxSetMatrix(o.I(0x2c), m);   // FUN_00112770
    objPublish(c);
}

}  // namespace

bool objectsDeepConstruct(Chr& c) {
    if (c.obj == nullptr) return false;
    switch (c.obj->cls) {
    case ObjClass::OK: okCtor(c); return true;
    default: return false;
    }
}

bool objectsDeepTick(Chr& c) {
    if (c.obj == nullptr) return false;
    switch (c.obj->cls) {
    case ObjClass::OK: okTick(c); return true;
    default: return false;
    }
}

bool objectsDeepDestroy(Chr& c) {
    if (c.obj == nullptr) return false;
    switch (c.obj->cls) {
    case ObjClass::OK: okDtor(c); return true;
    default: return false;
    }
}

}  // namespace baroque::game
