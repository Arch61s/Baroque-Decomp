// Dungeon object classes, second part (objects.hpp): O??M breakable walls, O??N doors, O40N..O42N lifts, O50N
// gate, O??O / O50J / O00J / O??P hazards, O??Q boss-child spawners with the O70C..O92C warp pads, O02B sinking gate,
// O00B..O04B levers. Fields by original offset (ObjData::I / F / P / M); PTMF slots hold the member address.
#include "game/objects/objects.hpp"
#include "game/objects/objects_internal.hpp"

#include "game/character/ai.hpp"
#include "game/character/combat.hpp"
#include "game/dungeon.hpp"
#include "game/dungeon_tables.hpp"
#include "game/event/event_manager.hpp"
#include "game/event/stage_script.hpp"
#include "game/flag_manager.hpp"
#include "game/game_world.hpp"
#include "game/hud/hud_api.hpp"
#include "game/scenes/game_scene.hpp"
#include "game/items/item.hpp"
#include "game/sound_stub.hpp"
#include "platform/platform.hpp"

#include <cmath>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

uint32_t tag4(const char* s) { return objTag(s); }
ObjData& od(Chr& c) { return *c.obj; }
Vec3f& pos(Chr& c) { return c.xf.m.pos; }

void axesMatrix(float m[16], const ChrTransform& t, float k, const Vec3f& p) {
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    for (int j = 0; j < 3; ++j) {
        m[j] = t.right[size_t(j)] * k;
        m[4 + j] = t.up[size_t(j)] * k;
        m[8 + j] = t.fwd[size_t(j)] * k;
        m[12 + j] = p[size_t(j)];
    }
}

// FUN_00142230(height, a, b, phys, owner): box shape; FUN_00142200(radius, height, phys, owner): cylinder
void shapeBox(Chr& c, float h, float a, float b) {
    c.shape.owner = &c;
    c.shape.height = h;
    c.shape.a = a;
    c.shape.b = b;
    c.shape.radius = std::sqrt(a * a + b * b);
    c.shape.top = c.shape.height + c.xf.m.pos[1];   // FUN_00142270
}
void shapeCylinder(Chr& c, float r, float h) {
    c.shape.owner = &c;
    c.shape.radius = r;
    c.shape.height = h;
    c.shape.a = -1.0f;
    c.shape.b = -1.0f;
    c.shape.top = c.shape.height + c.xf.m.pos[1];
}
void shapeTop(Chr& c) { c.shape.top = c.shape.height + c.xf.m.pos[1]; }   // FUN_00142270

// FUN_00124c70 room of the object's position -> FUN_001efad0(room)
void relinkRoom(Chr& c) {
    int room = -1;
    if (Dungeon* d = gameWorld().dungeon) {
        int x = 0, y = 0;
        room = roomOfCell(d->cellAtWorld(pos(c).data(), &x, &y));
    }
    aiRoomLinks(room);
}

// FUN_00112190(5, pos, 0, 100): the hit spark in front of the player (pos + fwd * 30, y + 80)
void playerHitSpark(const Chr& pl) {
    const ChrTransform& t = pl.xf.m;
    const Vec3f p{t.fwd[0] * 30.0f + t.pos[0] + 0.0f, t.pos[1] + 80.0f, t.fwd[2] * 30.0f + t.pos[2] + 0.0f};
    const Vec3f s{100.0f, 100.0f, 100.0f};
    fxSpawn(5, &p, nullptr, &s);
}

// FUN_0012c1c0 list (types 0..2)
template <class F> void forEachCharacter(F&& f) {
    for (Chr* e : characters().list())
        if (e->type <= kChrEnemy && e->obj == nullptr && e->item == nullptr) f(*e);
}

// FUN_0011e7c0: the extra-spawn entry of the floor (table 0x44a210, floor 0x7f with flag 0x3c1 -> entry 2)
const tables::ExtraSpawn* extraSpawn() {
    const int id = floorIdNow();
    if (id == 0x7f && flags().test(0x3c1)) return &tables::kExtraSpawns[2];
    for (uint32_t i = 0; i < 0x18; ++i)
        if (tables::kExtraSpawns[i].floorId == id) return &tables::kExtraSpawns[i];
    return nullptr;
}

// =================================================================================================================
// O??M: breakable walls (table 0x463520: MG06_05, MG06_06, MG04_01, MG05_01 by number). ctor FUN_001bd860
// (FUN_0012af10, physics FUN_001c0940 / FUN_00140230 at the position with radius 0 (0x463530), +0x2c hurt volume
// -1, +0x34 HP 100, +0x38 0), init FUN_001bd9d0 (+0x30 number, model mode 1 flags 0x44000011, count 0x53d810[n],
// box shape and hurt box 500 x table 0x499230 x 50), tick FUN_001bdae0, contact FUN_001bdbb0 (the player's ATK
// FUN_001344f0(st, -1), doubled for "O03M" on element 1; broken: SE 0x37, effect table 0x499240 (axes * 100),
// model fade FUN_0010a0a0(0x80, 10) and +0x38 = 10 ticks before it disappears).
// =================================================================================================================
const char* const kOMModel[4] = {"DATA/MAP/M06/MG06_05.IGB", "DATA/MAP/M06/MG06_06.IGB", "DATA/MAP/M04/MG04_01.IGB",
                                 "DATA/MAP/M05/MG05_01.IGB"};
constexpr float kOMWidth[4] = {200.0f, 400.0f, 600.0f, 600.0f};   // 0x499230
constexpr int kOMBreakFx[4] = {0xb0, 0xb1, 0xb2, 0xb2};             // 0x499240

void omCtor(Chr& c) {
    ObjData& o = od(c);
    c.effectSlot = -1;   // +0x2c
    o.I(0x34) = 100;
    o.I(0x38) = 0;
}
void omInit(Chr& c) {   // orig FUN_001bd9d0
    ObjData& o = od(c);
    const int n = tagNumber(c.id);
    o.I(0x30) = n;
    if (n < 0 || n > 3) {   // UNVERIFIED: the original indexes the 4-entry tables with any number
        platform::log("obj: " + tagString(c.id) + " number out of the O??M tables");
        return;
    }
    objLoadModel(c, kOMModel[n], 1, 0x44000011);
    modelShadowOff(c.model, true);   // FUN_00109f70(model, 1); count 0x53d810[n] + 1
    shapeBox(c, 500.0f, kOMWidth[n], 50.0f);                                  // FUN_00142230(500, w, 50, +0x28, obj)
    c.effectSlot = hitManager().addBox(500.0f, kOMWidth[n], 50.0f, &c);      // FUN_0014ac40 -> +0x2c
}
void omTick(Chr& c) {   // orig FUN_001bdae0
    if ((floorFlagWord() & 1u) == 0) return;
    ObjData& o = od(c);
    if (o.I(0x38) != 0) {
        o.I(0x38) = o.I(0x38) - 1;
        if (o.I(0x38) == 0) {
            shapeCylinder(c, 0.0f, 0.0f);   // FUN_00142200(0, 0, +0x28, obj)
            relinkRoom(c);
            entityRemove(c);
            return;
        }
    }
    shapeTop(c);
    objPublish(c);
}
int omContact(Chr& c, Chr* other, uint32_t m1) {   // orig FUN_001bdbb0
    ObjData& o = od(c);
    if (other == nullptr || other != characters().player()) return 0;
    if (o.I(0x34) == 0) return 0;
    int atk = statusAtk(other->status, -1);   // FUN_001344f0(st, -1)
    if (c.id == tag4("O03M") && (m1 & 1u) != 0) atk = atk << 1;   // 0x463550
    const int hp = o.I(0x34) - atk;
    o.I(0x34) = hp;
    if (hp < 1) {
        o.I(0x34) = 0;
        objSe3d(0x37, pos(c));
        const int n = o.I(0x30);
        const int h = fxSpawn((n >= 0 && n < 4) ? kOMBreakFx[n] : kOMBreakFx[0], nullptr, nullptr, nullptr);
        float m[16];
        axesMatrix(m, c.xf.m, 100.0f, pos(c));   // DAT_00463538 / 40 / 48 = 100
        fxSetMatrix(h, m);
        const float from[4] = {0.5f, 0.75f, 1.0f, 1.0f};
        const float to[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        modelFadeStart(c.model, 0x80, 10, from, to);   // FUN_0010a0a0(model, 0x80, 10, from, to): additive fade out
        o.I(0x38) = 10;
    }
    playerHitSpark(*other);
    return 1;
}

// =================================================================================================================
// O??N (not 40..42): doors (MG07_00, 0x463580). ctor FUN_001bdec0 (physics +0x34 radius 0 (0x463588), +0x38 hurt
// -1, PTMF +0x28 null, +0x3c 0), init FUN_001be030 (model, count 0x53d820, box 500 x 200 x 10, hurt box -> +0x38,
// +0x44 lift 0), tick FUN_001be0e0 (state, shape top, model at y + lift), contact FUN_001be180: struck by the
// player (spark 5) the door opens once: +0x40 = lift, SE 0xf; O33N / O35N / O36N / O37N (0x4635b0..) rise 100 and
// slam down (FUN_001be530 -> FUN_001be5a0: dust 0xa1 at pos +- fwd * 30 (DAT_004635f8 / 0x463600)); the others rise
// 415 and disappear (FUN_001be450; the shape is cleared after 90). Their hurt box is released and the room's AI
// links are rebuilt with the shape cleared (FUN_001efad0), then the box shape is restored until the door is up.
// =================================================================================================================
constexpr uint32_t kONRise = 0x1be450, kONHop = 0x1be530, kONSlam = 0x1be5a0;

void onCtor(Chr& c) {
    ObjData& o = od(c);
    c.effectSlot = -1;   // +0x38
    o.M(0x28) = 0;
    o.I(0x3c) = 0;
}
void onInit(Chr& c) {   // orig FUN_001be030
    objLoadModel(c, "DATA/MAP/M07/MG07_00.IGB", 0, 0x40000011);   // count 0x53d820 + 1
    shapeBox(c, 500.0f, 200.0f, 10.0f);
    c.effectSlot = hitManager().addBox(500.0f, 200.0f, 10.0f, &c);
    od(c).F(0x44) = 0.0f;
}
void onState(Chr& c) {
    ObjData& o = od(c);
    switch (o.M(0x28)) {
    case kONRise: {   // FUN_001be450
        o.F(0x44) = o.F(0x44) + 10.0f;
        if ((o.I(0x3c) & 2) == 0 && o.F(0x40) + 90.0f < o.F(0x44)) {
            shapeBox(c, 0.0f, 0.0f, 0.0f);
            o.I(0x3c) |= 2;
        }
        const float top = o.F(0x40) + 415.0f;
        if (top < o.F(0x44)) {
            o.F(0x44) = top;
            o.M(0x28) = 0;
            entityRemove(c);
        }
        break;
    }
    case kONHop: {   // FUN_001be530 (disasm)
        o.F(0x44) = o.F(0x44) + 10.0f;
        const float top = 100.0f + o.F(0x40);
        if (!(o.F(0x44) <= top)) {
            o.F(0x44) = top;
            o.M(0x28) = kONSlam;   // 0x4635d0
        }
        break;
    }
    case kONSlam: {   // FUN_001be5a0
        const float v = o.F(0x44) - 20.0f;
        o.F(0x44) = v;
        if (v < -30.0f) {
            o.F(0x44) = -30.0f;
            o.M(0x28) = 0;
            const ChrTransform& t = c.xf.m;
            float m[16];
            const int h1 = fxSpawn(0xa1, nullptr, nullptr, nullptr);
            axesMatrix(m, t, 100.0f, Vec3f{t.pos[0] + t.fwd[0] * 30.0f, t.pos[1] + t.fwd[1] * 30.0f, t.pos[2] + t.fwd[2] * 30.0f});
            fxSetMatrix(h1, m);
            const int h2 = fxSpawn(0xa1, nullptr, nullptr, nullptr);
            axesMatrix(m, t, 100.0f, Vec3f{t.pos[0] - t.fwd[0] * 30.0f, t.pos[1] - t.fwd[1] * 30.0f, t.pos[2] - t.fwd[2] * 30.0f});
            fxSetMatrix(h2, m);
        }
        break;
    }
    default: break;
    }
}
void onTick(Chr& c) {   // orig FUN_001be0e0
    if ((floorFlagWord() & 1u) == 0) return;
    if (od(c).M(0x28) != 0) onState(c);
    shapeTop(c);
    objPublishAt(c, Vec3f{pos(c)[0], od(c).F(0x44) + pos(c)[1], pos(c)[2]});
}
int onContact(Chr& c, Chr* other) {   // orig FUN_001be180
    ObjData& o = od(c);
    if (other == nullptr || other != characters().player()) return 0;
    playerHitSpark(*other);
    if ((o.I(0x3c) & 1) != 0) return 1;
    o.I(0x3c) |= 1;
    o.F(0x40) = o.F(0x44);
    objSe3d(0xf, pos(c));
    const uint32_t id = c.id;
    if (id == tag4("O33N") || id == tag4("O35N") || id == tag4("O36N") || id == tag4("O37N")) {
        o.M(0x28) = kONHop;   // 0x463590
    } else {
        o.M(0x28) = kONRise;  // 0x4635a0
        shapeBox(c, 0.0f, 0.0f, 0.0f);
        relinkRoom(c);
        shapeBox(c, 500.0f, 200.0f, 10.0f);
        hitManager().removeVictim(c.effectSlot);   // FUN_0014ace0(0x50c780, +0x38)
        c.effectSlot = -1;
    }
    return 1;
}

// =================================================================================================================
// O40N..O42N: lifts (table 0x463770 by the 3rd tag character: MG00_01, MG02_02, MG04_05). ctor FUN_001bfe20
// (physics +0x28 radius 0 (0x463780), +0x30 0), init FUN_001bff50 (model, count 0x53d838, box 500 x 100 x (10 for
// 2, else 15)), tick FUN_001c0010: while +0x30 bit 0 (set by FUN_001c00e0 when an O??Q spawner is cleared) it rises
// 10 per tick to y 300, then the room's AI links are rebuilt.
// =================================================================================================================
const char* const kON40Model[3] = {"DATA/MAP/M00/MG00_01.IGB", "DATA/MAP/M02/MG02_02.IGB", "DATA/MAP/M04/MG04_05.IGB"};

void on40Init(Chr& c) {   // orig FUN_001bff50
    ObjData& o = od(c);
    const char d = static_cast<char>((c.id >> 16) & 0xff);
    const int k = (d >= '0' && d <= '9') ? d - '0' : 0;   // FUN_002388f0 on the single character
    o.I(0x2c) = k;
    if (k < 0 || k > 2) return;   // UNVERIFIED: outside the 3-entry table
    objLoadModel(c, kON40Model[k], 0, 0x40000011);
    shapeBox(c, 500.0f, 100.0f, k == 2 ? 10.0f : 15.0f);
}
void on40Tick(Chr& c) {   // orig FUN_001c0010
    ObjData& o = od(c);
    if ((o.I(0x30) & 1) != 0) {
        pos(c)[1] = pos(c)[1] + 10.0f;
        if (300.0f < pos(c)[1]) {
            pos(c)[1] = 300.0f;
            o.I(0x30) &= ~1;
            relinkRoom(c);
        }
    }
    shapeTop(c);
    objPublish(c);
}
// orig FUN_001c00e0
void liftStart(Chr& c) {
    od(c).I(0x30) |= 1;
    objSe3d(0xf, pos(c));
}

// =================================================================================================================
// O50N: shutter (MG02_00, 0x463630). ctor FUN_001be820 (physics +0x34 radius 0 (0x463638), PTMF +0x28 =
// FUN_001bea90 (0x463640)), init FUN_001be960 (model, count 0x53d828, box 500 x 100 x 6, +0x38 0), tick FUN_001be9e0
// (state, shape top, model at y + 0x38 + 175). FUN_001bea90: flags 0x53 / 0x54 clear and 0xc9 set and the player
// within +-100 x / +-150 z -> SE 0xf, FUN_001bebc0: rises 10 per tick; above 480 the shape is cleared, AI relinked
// and the shutter removed.
// =================================================================================================================
constexpr uint32_t kO50NWait = 0x1bea90, kO50NRise = 0x1bebc0;

void o50nInit(Chr& c) {   // orig FUN_001be960
    objLoadModel(c, "DATA/MAP/M02/MG02_00.IGB", 0, 0x40000011);
    shapeBox(c, 500.0f, 100.0f, 6.0f);
    od(c).F(0x38) = 0.0f;
}
void o50nTick(Chr& c) {   // orig FUN_001be9e0
    if ((floorFlagWord() & 1u) == 0) return;
    ObjData& o = od(c);
    switch (o.M(0x28)) {
    case kO50NWait: {   // FUN_001bea90
        if (flags().test(0x53) || flags().test(0x54) || !flags().test(0xc9)) break;
        const Chr* pl = characters().player();
        if (pl == nullptr) break;
        const float px = pl->xf.m.pos[0], pz = pl->xf.m.pos[2];
        if (px <= pos(c)[0] + 100.0f && pos(c)[0] - 100.0f <= px && pz <= pos(c)[2] + 150.0f && pos(c)[2] - 150.0f <= pz) {
            objSe3d(0xf, pos(c));
            o.M(0x28) = kO50NRise;   // 0x463650
        }
        break;
    }
    case kO50NRise: {   // FUN_001bebc0
        const float v = o.F(0x38) + 10.0f;
        o.F(0x38) = v;
        if (480.0f < v) {
            shapeCylinder(c, 0.0f, 0.0f);
            relinkRoom(c);
            entityRemove(c);
        }
        break;
    }
    default: break;
    }
    shapeTop(c);
    objPublishAt(c, Vec3f{pos(c)[0], o.F(0x38) + pos(c)[1] + 175.0f, pos(c)[2]});
}

// =================================================================================================================
// O??O: invisible hazard (ctor FUN_001bf850 = FUN_00128590 only), tick FUN_001bf900: hurt sphere 20 at the position,
// element 2, m3 0x1000; +0x24 counter.
// =================================================================================================================
void ooTick(Chr& c) {
    if ((floorFlagWord() & 1u) == 0) return;
    hitManager().testPoint(20.0f, 0.0f, &c, 0, pos(c), 2, 0, 0x1000, 0, nullptr);
    od(c).I(0x24) = od(c).I(0x24) + 1;
}

// =================================================================================================================
// O50J: flame jet (no model). ctor FUN_001b96e0 (+0x24 = -30 rand(10), +0x28 effect -1), tick FUN_001b97e0: counts
// while the player is in the room or the flame burns; 300-tick cycle: ticks 51..150 the flame 0xa9 (axes * 100,
// DAT_00462fc0..0x462fd0) with SE 0x2c and 6 hurt spheres of 30 every 30 along the forward axis (hitId = counter,
// m3 0x1000); from 151 the flame is stopped (FUN_001126e0(h, 0, -1, -1)).
// =================================================================================================================
void o50jCtor(Chr& c) {
    od(c).I(0x24) = static_cast<int32_t>(gameWorld().rng.rand(10)) * -0x1e;   // FUN_002071e0(0x53eb20, 10)
    od(c).I(0x28) = -1;
}
void flameSpheres(Chr& c, int hitId) {
    const ChrTransform& t = c.xf.m;
    Vec3f p = t.pos;
    for (int i = 0; i < 6; ++i) {
        p = {p[0] + t.fwd[0] * 30.0f, p[1] + t.fwd[1] * 30.0f, p[2] + t.fwd[2] * 30.0f};
        hitManager().testPoint(30.0f, 0.0f, &c, hitId, p, 0, 0, 0x1000, 0, nullptr);
    }
}
void o50jTick(Chr& c) {
    if ((floorFlagWord() & 1u) == 0) return;
    ObjData& o = od(c);
    if (o.I(0x28) == -1 && !sameRoomAsPlayer(pos(c))) return;
    const int t = o.I(0x24) + 1;
    o.I(0x24) = t;
    if (t < 0) return;
    if (0x12c < t) {
        o.I(0x24) = 0;
        return;
    }
    if (0x96 < t) {
        if (o.I(0x28) != -1) {
            fxLoopOut(o.I(0x28), 0, -1, -1);
            o.I(0x28) = -1;
        }
        return;
    }
    if (t <= 0x32) return;
    if (o.I(0x28) == -1) {
        o.I(0x28) = fxSpawn(0xa9, nullptr, nullptr, nullptr);
        float m[16];
        axesMatrix(m, c.xf.m, 100.0f, pos(c));
        fxSetMatrix(o.I(0x28), m);
        objSe3d(0x2c, pos(c));
    }
    flameSpheres(c, o.I(0x24));
}

// =================================================================================================================
// O00J: fire statue (MG00_00, 0x462fa0). ctor FUN_001b91d0 (model, count 0x53d7f0, model vt+0x94(0) (animation
// stopped), +0x28 int64 clock = -(rand(10) * 30), +0x30 effect -1), tick FUN_001b9340: like O50J, but the cycle
// clock is the model's animation time in ns (+0x1fca055 per tick, wrapping at 0x2540be39d, model vt+0x9c sets it);
// frames 51..150 burn (effect 0x9d), from 151 the flame is stopped.
// =================================================================================================================
void o00jCtor(Chr& c) {
    objLoadModel(c, "DATA/MAP/M00/MG00_00.IGB", 0, 0x40000011);
    const int n = static_cast<int>(gameWorld().rng.rand(10));
    c.obj->clock = -static_cast<int64_t>(static_cast<uint32_t>(n * 0x1e));   // *(ulong *)(+0x28)
    od(c).I(0x30) = -1;
}
void o00jTick(Chr& c) {
    if ((floorFlagWord() & 1u) == 0) return;
    ObjData& o = od(c);
    if (o.I(0x30) == -1 && !sameRoomAsPlayer(pos(c))) {
        objPublish(c);
        return;
    }
    if (o.clock < 0) {
        o.clock = o.clock + 1;
    } else {
        // model vt+0x9c: animation time = +0x28 (UNVERIFIED: the clip played by the statue model, see objectPose)
        o.animTimeNs = o.clock;
        o.clock = o.clock + 0x1fca055;
        if (!(o.clock < 0x2540be39dLL)) o.clock = o.clock - 0x2540be39cLL;
        const int frame = static_cast<int>(o.clock / 0x1fca055);   // FUN_00244f38
        if (frame < 0x97) {
            if (0x32 < frame) {
                if (o.I(0x30) == -1) {
                    o.I(0x30) = fxSpawn(0x9d, nullptr, nullptr, nullptr);
                    float m[16];
                    axesMatrix(m, c.xf.m, 100.0f, pos(c));   // DAT_00462fa8 / b0 / b8
                    fxSetMatrix(o.I(0x30), m);
                    objSe3d(0x2c, pos(c));
                }
                flameSpheres(c, static_cast<int>(o.clock));   // hitId = low word of +0x28
            }
        } else if (o.I(0x30) != -1) {
            fxLoopOut(o.I(0x30), 0, -1, -1);
            o.I(0x30) = -1;
        }
    }
    objPublish(c);
}

// =================================================================================================================
// O??P: floor geysers (no model). ctor FUN_001bf260 (FUN_0012ae60, +0x24 = rand(0x3c)), tick FUN_001bf330: while
// the player is in the room, every 150 ticks: effect 0xab at (x, 0.1, z) scale (100, 100.1, 100), SE 0x49; every
// character of types 0/2 within +-200 x/z, +-20 y takes red * (1 + weak) * (0.1 hpMax + floorNumber) (min 1,
// FUN_00136580), FX 1 (FUN_0014c580) and a reaction 0 from the geyser (FUN_0012a710); lying HT / MT items in the
// same box are cooked (FUN_001d4860 "HT02" / "MT03").
// =================================================================================================================
void opCtor(Chr& c) { od(c).I(0x24) = static_cast<int32_t>(gameWorld().rng.rand(0x3c)); }
void opTick(Chr& c) {
    if ((floorFlagWord() & 1u) == 0) return;
    if (!sameRoomAsPlayer(pos(c))) return;
    ObjData& o = od(c);
    if (o.I(0x24) != 0) {
        o.I(0x24) = o.I(0x24) - 1;
        return;
    }
    o.I(0x24) = 0x96;
    const Vec3f fp{pos(c)[0], 0.1f, pos(c)[2]};
    const Vec3f s{100.0f, 100.1f, 100.0f};
    fxSpawn(0xab, &fp, nullptr, &s);
    objSe3d(0x49, pos(c));
    const Vec3f& q = pos(c);
    auto inBox = [&](const Vec3f& p) {
        return q[0] - 200.0f < p[0] && p[0] < q[0] + 200.0f && q[2] - 200.0f < p[2] && p[2] < q[2] + 200.0f &&
               p[1] < q[1] + 20.0f && q[1] - 20.0f < p[1];
    };
    forEachCharacter([&](Chr& e) {
        if ((e.taskFlags & 0x10000000u) != 0 || e.type == kChrNpc) return;
        if (!inBox(e.xf.m.pos)) return;
        const float red = victimReduction(e.status, nullptr);       // FUN_00135f00(st, 0)
        const float weak1 = elementWeakness(e.status, 1) + 1.0f;   // FUN_00135c50(st, 1) + 1
        const int hpMax = statusHpMax(e.status);                   // FUN_001343f0
        const float fl = static_cast<float>(static_cast<uint32_t>(floorNumber(floorIdNow())));
        int dmg = static_cast<int>(red * weak1 * (static_cast<float>(hpMax) * 0.1f + fl));   // UNVERIFIED: product order (pseudocode)
        if (dmg < 1) dmg = 1;
        applyDamage(e.status, nullptr, dmg, 0, nullptr, nullptr);   // FUN_00136580
        hitElementFx(e, nullptr, 1, nullptr);                       // FUN_0014c580(chr, 0, 1, 0)
        react(e, 0, q, false);                                      // FUN_0012a710(chr, 0, geyser pos, 0)
    });
    for (Chr* e : characters().list()) {
        if (e->item == nullptr || (e->taskFlags & 1u) == 0 || (e->taskFlags & 0x10000000u) != 0) continue;
        if (!inBox(e->xf.m.pos)) continue;
        Item& it = *e->item;
        if (it.cls == 3 && it.id != tag4("HT02")) changeItemId(it, tag4("HT02"), itemSystem());
        else if (it.cls == 4 && it.id != tag4("MT03")) changeItemId(it, tag4("MT03"), itemSystem());
    }
}

// =================================================================================================================
// O??Q: boss-child spawners (no model). ctor FUN_001c0110 (8 slots {entity +0x24, serial +0x28} cleared, +0x64 =
// -1, the extra spawn's model preloaded), trigger FUN_001c0240 (from the warp pads): +0x64 = 0, `count` entities of
// the floor's extra-spawn tag (FUN_0011e7c0) at the spawner facing a random yaw (FUN_001287a0), AI flag 0x100, slots
// filled. Tick FUN_001c03e0: slots whose entity left the task list are cleared; when none is left: on the random
// dungeon flags 0x3bd / 0x3bf / 0x3c2 / 0x3c4 by the tag (E24A / E27A / E25A / E26A), floor 0x8c the exit "PT00"
// at the first room + (800, 0, 1000); the lift of table 0x499250 ("O40N" x4, "O42N" x2, "O41N" x2 by the third tag
// character) is started (FUN_001c00e0) and the spawner removed.
// =================================================================================================================
void oqCtor(Chr& c) {
    ObjData& o = od(c);
    for (uint32_t i = 0; i < 8; ++i) {
        o.P(0x24 + i * 8) = nullptr;
        o.I(0x28 + i * 8) = 0;
    }
    o.I(0x64) = -1;
    // FUN_0010cbb0 / FUN_0010cf60(FUN_0012b620(tag)): resident model preload (synchronous loads: nothing to do)
}

// FUN_001287a0(ent, dir): forward = dir, right = up x fwd, up = fwd x right
void faceDir(ChrTransform& t, const Vec3f& d) {
    t.fwd = d;
    t.right = {t.up[1] * t.fwd[2] - t.up[2] * t.fwd[1], t.up[2] * t.fwd[0] - t.up[0] * t.fwd[2], t.up[0] * t.fwd[1] - t.up[1] * t.fwd[0]};
    normalize(t.right);
    t.up = {t.fwd[1] * t.right[2] - t.fwd[2] * t.right[1], t.fwd[2] * t.right[0] - t.fwd[0] * t.right[2], t.fwd[0] * t.right[1] - t.fwd[1] * t.right[0]};
    normalize(t.up);
}

void bossSpawnTrigger(Chr& q) {   // orig FUN_001c0240
    ObjData& o = od(q);
    o.I(0x64) = 0;
    const tables::ExtraSpawn* ex = extraSpawn();
    if (ex == nullptr) return;
    for (int n = ex->count; n != 0; --n) {
        Chr* e = characters().createTag(ex->tag, true);   // FUN_0012ce70(0x50b1a0, tag, 1)
        if (e == nullptr) return;
        e->xf.m.pos = pos(q);
        e->prevPos = e->xf.m.pos;
        const float r = static_cast<float>(static_cast<int>(gameWorld().rng.rand(0x8000)));
        Vec3f d;
        rotateY(((r / 32768.0f) * 359.0f * 3.1415927f) / 180.0f, d, Vec3f{0.0f, 0.0f, 1.0f});   // FUN_00206ca0
        faceDir(e->xf.m, d);
        const int k = o.I(0x64);
        o.I(0x64) = k + 1;
        if (k < 8) {
            o.P(0x24 + uint32_t(k) * 8) = e;
            o.I(0x28 + uint32_t(k) * 8) = static_cast<int32_t>(e->serial);
        }
        e->ai.flags |= 0x100u;   // AI component +0x14
    }
}

void oqTick(Chr& c) {   // orig FUN_001c03e0
    ObjData& o = od(c);
    if (o.I(0x64) == -1) return;
    for (uint32_t i = 0; i < 8; ++i) {
        Chr* e = o.P(0x24 + i * 8);
        if (e == nullptr) continue;
        bool alive = false;
        for (const Chr* t : characters().list())
            if ((t->taskFlags & 0x10000000u) == 0 && t == e && static_cast<uint32_t>(o.I(0x28 + i * 8)) == t->serial) {
                alive = true;
                break;
            }
        if (!alive) {
            o.P(0x24 + i * 8) = nullptr;
            o.I(0x64) = o.I(0x64) - 1;
        }
    }
    if (o.I(0x64) != 0) return;
    if (isRandomDungeon(floorIdNow())) {
        if (const tables::ExtraSpawn* ex = extraSpawn()) {
            if (ex->tag == tag4("E24A")) flags().set(0x3bd);
            else if (ex->tag == tag4("E27A")) flags().set(0x3bf);
            else if (ex->tag == tag4("E25A")) flags().set(0x3c2);
            else if (ex->tag == tag4("E26A")) flags().set(0x3c4);
        }
        if (floorIdNow() == 0x8c) {
            if (StageObject* st = stageCreate("PT00")) {   // FUN_00179f60("PT00")
                if (const Dungeon* d = gameWorld().dungeon) {
                    const Room& r0 = d->room(0);   // *0x50a640
                    st->pos[0] = static_cast<float>(r0.posX * 0x640 + 100) + 800.0f;
                    st->pos[2] = static_cast<float>(r0.posY * 0x640 + 100) + 1000.0f;
                    st->pos[1] = 0.0f;
                }
            }
        }
    }
    // table 0x499250: by the third tag character
    static const char* const kLift[8] = {"O40N", "O40N", "O40N", "O40N", "O42N", "O42N", "O41N", "O41N"};
    const char d = static_cast<char>((c.id >> 16) & 0xff);
    const int k = (d >= '0' && d <= '9') ? d - '0' : 0;
    if (k >= 0 && k < 8) {
        if (Chr* lift = findEntity(tag4(kLift[k]), kChrObject)) liftStart(*lift);
        // UNVERIFIED: FUN_001c00e0 on a null result (no such lift) dereferences null in the original
    }
    entityRemove(c);
}

// =================================================================================================================
// O70C..O92C: warp pads (FUN_001bf980 = FUN_0012ae60, no model), tick FUN_001bfa30: the player within +-100 x/z and
// +-20 y is moved onto the spawner of its range (70-72 "O00Q", 73-75 "O01Q", 76-78 "O02Q", 79-81 "O03Q", 82-84
// "O04Q", 85-86 "O05Q", 87-89 "O06Q", else "O07Q"), which is triggered (FUN_001c0240); SE 0x45.
// =================================================================================================================
void oc70Tick(Chr& c) {
    Chr* pl = characters().player();
    if ((floorFlagWord() & 1u) == 0 || pl == nullptr || (pl->taskFlags & 0x10000000u) != 0) return;
    const Vec3f& q = pos(c);
    const Vec3f& p = pl->xf.m.pos;
    if (!(q[0] - 100.0f < p[0] && p[0] < q[0] + 100.0f && q[2] - 100.0f < p[2] && p[2] < q[2] + 100.0f &&
          p[1] < q[1] + 20.0f && q[1] - 20.0f < p[1]))
        return;
    const int n = tagNumber(c.id);
    const char* t;
    if (n >= 0x46 && n <= 0x48) t = "O00Q";
    else if (n >= 0x49 && n <= 0x4b) t = "O01Q";
    else if (n >= 0x4c && n <= 0x4e) t = "O02Q";
    else if (n >= 0x4f && n <= 0x51) t = "O03Q";
    else if (n >= 0x52 && n <= 0x54) t = "O04Q";
    else if (n >= 0x55 && n <= 0x56) t = "O05Q";
    else if (n >= 0x57 && n <= 0x59) t = "O06Q";
    else t = "O07Q";
    Chr* sp = findEntity(tag4(t), kChrObject);
    if (sp == nullptr) return;
    bossSpawnTrigger(*sp);
    pl->xf.m.pos = sp->xf.m.pos;
    sound().playSe(1.0f, 0x45);   // FUN_00115390(1.0, 0x4e9790, 0x45)
}

// =================================================================================================================
// O02B: sinking gate (MG06_04, 0x462f50). ctor FUN_001b8e20 (model, count 0x53d7e8, physics +0x34 radius 0
// (0x462f58), PTMF +0x28 = FUN_001b90a0 (publish once)), init FUN_001b8f90 (box 490 x 100 x 100), tick FUN_001b8fc0
// (disasm): at y 0 with flag 0x48 (the levers O00B / O01B) -> SE 0x11, FUN_001b9100 (sinks 3 per tick to -500, then
// the room's AI links are rebuilt); state, shape top.
// =================================================================================================================
constexpr uint32_t kO02BShow = 0x1b90a0, kO02BSink = 0x1b9100;

void o02bCtor(Chr& c) {
    objLoadModel(c, "DATA/MAP/M06/MG06_04.IGB", 0, 0x40000011);
    od(c).M(0x28) = kO02BShow;   // 0x462f60
}
void o02bInit(Chr& c) { shapeBox(c, 490.0f, 100.0f, 100.0f); }   // FUN_00142230(0x43f50000, 100, 100, +0x34, obj)
void o02bTick(Chr& c) {
    if ((floorFlagWord() & 1u) == 0) return;
    ObjData& o = od(c);
    if (pos(c)[1] == 0.0f && flags().test(0x48)) {
        o.M(0x28) = kO02BSink;   // 0x462f70
        objSe3d(0x11, pos(c));
    }
    switch (o.M(0x28)) {
    case kO02BShow:   // FUN_001b90a0
        objPublish(c);
        o.M(0x28) = 0;
        break;
    case kO02BSink:   // FUN_001b9100
        pos(c)[1] = pos(c)[1] + -3.0f;
        if (pos(c)[1] < -500.0f) {
            pos(c)[1] = -500.0f;
            o.M(0x28) = 0;
            relinkRoom(c);
        }
        objPublish(c);
        break;
    default: break;
    }
    shapeTop(c);
}

// =================================================================================================================
// O00B / O01B / O03B / O04B: levers (MG06_07, 0x462eb0). Flag by tag (FUN_001b8710): O00B / O01B 0x48, O03B 0x49,
// O04B 0x4a. ctor FUN_001b85b0 (model, count 0x53d7e0, PTMF = FUN_001b8be0 (publish once), +0x34 -1, +0x40 SE 0),
// init FUN_001b8920 (hurt sphere 80 -> +0x34, +0x3c = flag, angle +0x38 = 45 / -30, FUN_00128d20), tick FUN_001b89d0
// (a 0x48 lever that is off follows flag 0x48: FUN_001b8c40), contact FUN_001b8aa0 (player: SE 0x2b; switch on
// FUN_001b8c40 or off FUN_001b8d30; a 0x48 lever only switches on), FUN_001b8c40 / FUN_001b8d30 turn 10 deg per tick
// to 45 / below -30 and set / clear the flag, then FUN_001b8830.
// =================================================================================================================
constexpr uint32_t kLeverShow = 0x1b8be0, kLeverOn = 0x1b8c40, kLeverOff = 0x1b8d30;

uint32_t leverFlag(const Chr& c) {   // orig FUN_001b8710 (strings 0x462ed0..0x462ee8)
    if (c.id == tag4("O00B") || c.id == tag4("O01B")) return 0x48;
    if (c.id == tag4("O03B")) return 0x49;
    if (c.id == tag4("O04B")) return 0x4a;
    return 0;
}
// orig FUN_00128d20(rad, ent): forward = (right x worldUp) rotated about right; up = fwd x right; fwd = right x up
void leverAngle(ChrTransform& t, float rad) {
    const Vec3f u{0.0f, 1.0f, 0.0f};
    const Vec3f v{t.right[1] * u[2] - t.right[2] * u[1], t.right[2] * u[0] - t.right[0] * u[2], t.right[0] * u[1] - t.right[1] * u[0]};
    float m[12] = {};
    axisAngle(m, rad, t.right);
    t.fwd = mulVec3(v, m);
    normalize(t.fwd);
    t.up = {t.fwd[1] * t.right[2] - t.fwd[2] * t.right[1], t.fwd[2] * t.right[0] - t.fwd[0] * t.right[2], t.fwd[0] * t.right[1] - t.fwd[1] * t.right[0]};
    normalize(t.up);
    t.fwd = {t.right[1] * t.up[2] - t.right[2] * t.up[1], t.right[2] * t.up[0] - t.right[0] * t.up[2], t.right[0] * t.up[1] - t.right[1] * t.up[0]};
    normalize(t.fwd);
}
// orig FUN_001b8830(lever, quiet): O03B: screen transition (FUN_00177cc0(0, 0x51b620, 10)) unless quiet, the
// "Toggle" nodes of the world scene (FUN_001b8430 -> FUN_001b8440, game_scene gameWorldToggle), looping SE
// 0x12 while on (handle +0x40); O04B: SE 0x30 when on.
void leverEffect(Chr& c, bool quiet) {
    ObjData& o = od(c);
    const uint32_t f = leverFlag(c);
    if (f == 0x49) {
        if (!quiet) eventManager().requestTransition(0.0f, 10);
        gameWorldToggle(o.I(0x3c) == 0 ? 1 : 0);   // FUN_001b8430(+0x3c == 0) -> FUN_001b8440(scene root, on)
        if (o.I(0x3c) == 0) {
            if (o.I(0x40) != 0) {
                objSeStop(o.I(0x40));   // FUN_00115820(snd, +0x40, 2)
                o.I(0x40) = 0;
            }
        } else if (o.I(0x40) == 0) {
            o.I(0x40) = static_cast<int32_t>(sound().playSe(1.0f, 0x12));   // FUN_00115390(1.0, snd, 0x12) -> +0x40
        }
    } else if (f == 0x4a && o.I(0x3c) != 0) {
        sound().playSe(1.0f, 0x30);
    }
}
void leverCtor(Chr& c) {
    objLoadModel(c, "DATA/MAP/M06/MG06_07.IGB", 0, 0x40000011);
    od(c).M(0x28) = kLeverShow;   // 0x462ec0
    c.effectSlot = -1;            // +0x34
    od(c).I(0x40) = 0;
}
void leverInit(Chr& c) {   // orig FUN_001b8920
    ObjData& o = od(c);
    c.effectSlot = hitManager().addSphere(80.0f, &c);   // FUN_0014ab40(80, 0x50c780, obj)
    o.I(0x3c) = flags().test(leverFlag(c)) ? 1 : 0;
    o.F(0x38) = o.I(0x3c) == 1 ? 45.0f : -30.0f;
    leverAngle(c.xf.m, (o.F(0x38) * 3.1415927f) / 180.0f);
}
void leverState(Chr& c) {
    ObjData& o = od(c);
    switch (o.M(0x28)) {
    case kLeverShow:   // FUN_001b8be0
        objPublish(c);
        o.M(0x28) = 0;
        break;
    case kLeverOn: {   // FUN_001b8c40
        const float a = o.F(0x38) + 10.0f;
        o.F(0x38) = a;
        if (45.0f < a) {
            o.F(0x38) = 45.0f;
            o.M(0x28) = 0;
            o.I(0x3c) = 1;
            flags().set(leverFlag(c));
            leverEffect(c, false);
        }
        leverAngle(c.xf.m, (o.F(0x38) * 3.1415927f) / 180.0f);
        objPublish(c);
        break;
    }
    case kLeverOff: {   // FUN_001b8d30
        const float a = o.F(0x38) - 10.0f;
        o.F(0x38) = a;
        if (a < -30.0f) {
            o.M(0x28) = 0;
            o.I(0x3c) = 0;
            flags().clear(leverFlag(c));   // FUN_00178a30
            leverEffect(c, false);
        }
        leverAngle(c.xf.m, (o.F(0x38) * 3.1415927f) / 180.0f);
        objPublish(c);
        break;
    }
    default: break;
    }
}
void leverTick(Chr& c) {   // orig FUN_001b89d0
    if ((floorFlagWord() & 1u) == 0) return;
    ObjData& o = od(c);
    if (leverFlag(c) == 0x48 && o.I(0x3c) == 0 && o.M(0x28) == 0 && flags().test(0x48)) o.M(0x28) = kLeverOn;   // 0x462ef0
    if (o.M(0x28) != 0) leverState(c);
}
int leverContact(Chr& c, Chr* other) {   // orig FUN_001b8aa0
    ObjData& o = od(c);
    if (other == nullptr || other != characters().player()) return 0;
    if (o.M(0x28) != 0) return 0;
    const uint32_t f = leverFlag(c);
    if (f == 0x48) {
        if (flags().test(0x48)) return 0;
        o.M(0x28) = kLeverOn;   // 0x462f00
    } else if (flags().test(f)) {
        o.M(0x28) = kLeverOff;  // 0x462f10
    } else {
        o.M(0x28) = kLeverOn;   // 0x462f20
    }
    objSe3d(0x2b, pos(c));
    return 0;
}

}  // namespace

void objClassConstruct2(Chr& c) {
    switch (od(c).cls) {
    case ObjClass::OM: omCtor(c); break;
    case ObjClass::ON: onCtor(c); break;
    case ObjClass::ON40: od(c).I(0x30) = 0; break;          // FUN_001bfe20
    case ObjClass::O50N: od(c).M(0x28) = kO50NWait; break;  // FUN_001be820 (0x463640)
    case ObjClass::O50J: o50jCtor(c); break;
    case ObjClass::O00J: o00jCtor(c); break;
    case ObjClass::OP: opCtor(c); break;
    case ObjClass::OQ: oqCtor(c); break;
    case ObjClass::O02B: o02bCtor(c); break;
    case ObjClass::O00B: leverCtor(c); break;
    default: break;   // OO FUN_001bf850, OC70 FUN_001bf980: no fields
    }
}

void objClassInit2(Chr& c) {
    switch (od(c).cls) {
    case ObjClass::OM: omInit(c); break;
    case ObjClass::ON: onInit(c); break;
    case ObjClass::ON40: on40Init(c); break;
    case ObjClass::O50N: o50nInit(c); break;
    case ObjClass::O02B: o02bInit(c); break;
    case ObjClass::O00B: leverInit(c); break;
    default: break;   // FUN_001286b0 (nop) for the others
    }
}

void objClassTick2(Chr& c) {
    switch (od(c).cls) {
    case ObjClass::OM: omTick(c); break;
    case ObjClass::ON: onTick(c); break;
    case ObjClass::ON40: on40Tick(c); break;
    case ObjClass::O50N: o50nTick(c); break;
    case ObjClass::OO: ooTick(c); break;
    case ObjClass::O50J: o50jTick(c); break;
    case ObjClass::O00J: o00jTick(c); break;
    case ObjClass::OP: opTick(c); break;
    case ObjClass::OQ: oqTick(c); break;
    case ObjClass::OC70: oc70Tick(c); break;
    case ObjClass::O02B: o02bTick(c); break;
    case ObjClass::O00B: leverTick(c); break;
    default: break;
    }
}

int objClassContact2(Chr& c, Chr* other, uint32_t m1) {
    switch (od(c).cls) {
    case ObjClass::OM: return omContact(c, other, m1);
    case ObjClass::ON: return onContact(c, other);
    case ObjClass::O00B: return leverContact(c, other);
    default: return 0;
    }
}

void helperTick2(Chr& c) { (void)c; }

}  // namespace baroque::game
