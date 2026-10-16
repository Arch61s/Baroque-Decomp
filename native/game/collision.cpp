// Engine sphere-sweep collision and physics object, translated (see collision.hpp).
#include "game/collision.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

#pragma fenv_access(on)

namespace baroque::game {

namespace {

CollisionScene* gScene = nullptr;

float dot3(const Vec3f& a, const Vec3f& b) { return a[2] * b[2] + a[0] * b[0] + a[1] * b[1]; }

// DAT_0044b2e8 / DAT_0044b2e0 / DAT_0044b2c8 (PLAYER.md §5; values read from the ELF).
constexpr float kLethargyScale = 0.5f;   // DAT_0044b2e8
constexpr float kWallBlend = 0.5f;       // DAT_0044b2e0
constexpr float kCos89 = 0.017452406f;   // floor/wall threshold (FUN_00141e40, FUN_00140970)
constexpr float kHalf = 0.5f;            // DAT_0044b2c8 (FUN_00140250 sweep bounding sphere)

float trunc6(float x);

}  // namespace

GlobalHits& globalHits() {
    static GlobalHits g;
    return g;
}

namespace {
std::function<void(std::vector<CollBody>&)>& defaultObjects() {
    static std::function<void(std::vector<CollBody>&)> f;
    return f;
}
}  // namespace
void setDefaultCollisionObjects(std::function<void(std::vector<CollBody>&)> f) { defaultObjects() = std::move(f); }
CollisionScene* collisionScene() { return gScene; }
void setCollisionScene(CollisionScene* scene) {
    gScene = scene;
    if (gScene != nullptr && !gScene->objects && defaultObjects()) gScene->objects = defaultObjects();
}

// orig FUN_00206ab0 (work/decompiled/00206ab0.c)
float normalize(Vec3f& v) {
    const float sq = v[2] * v[2] + v[0] * v[0] + v[1] * v[1];
    float len = 0.0f;
    if (sq != 0.0f) len = std::sqrt(sq);
    if (len == 0.0f) {
        v = {0.0f, 0.0f, 0.0f};
    } else {
        v[0] = v[0] / len;
        v[1] = v[1] / len;
        v[2] = v[2] / len;
    }
    return len;
}

// orig FUN_00206f10 / FUN_00206f50
const Vec3f& upVector() {
    static const Vec3f up{0.0f, 1.0f, 0.0f};
    return up;
}
const Vec3f& downVector() {
    static const Vec3f down{0.0f, -1.0f, 0.0f};
    return down;
}

// orig FUN_00206ca0: out = rotate `in` about +Y by rad. sin = FUN_002386a0, cos = FUN_00238400.
// UNVERIFIED: the PS2 libm sinf/cosf may differ from the host's in the last ulp.
void rotateY(float rad, Vec3f& out, const Vec3f& in) {
    const float s = std::sin(rad);
    const float c = std::cos(rad);
    const Vec3f src = in;
    out[0] = src[0] * c + src[2] * s;
    out[1] = src[1];
    out[2] = src[0] * -s + src[2] * c;
}

// orig FUN_0020b3d0: out = component of v along the plane with normal n (via n x v, then (n x v) x n).
bool slideDirection(Vec3f& out, const Vec3f& v, const Vec3f& n) {
    Vec3f c{n[1] * v[2] - n[2] * v[1], n[2] * v[0] - n[0] * v[2], n[0] * v[1] - n[1] * v[0]};
    normalize(c);
    if (c[0] == 0.0f && c[1] == 0.0f && c[2] == 0.0f) return false;
    out[0] = c[1] * n[2] - c[2] * n[1];
    out[1] = c[2] * n[0] - c[0] * n[2];
    out[2] = c[0] * n[1] - c[1] * n[0];
    normalize(out);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Collision mesh
// ---------------------------------------------------------------------------------------------
CollisionMesh CollisionMesh::fromCls(const data::Cls& cls) {
    CollisionMesh m;
    for (int i = 0; i < 3; ++i) m.cellSize[i] = cls.cellSize[i];
    m.n[0] = int32_t(cls.nx);
    m.n[1] = int32_t(cls.ny);
    m.n[2] = int32_t(cls.nz);
    m.origin[0] = cls.originX;
    m.origin[1] = cls.originY;
    m.origin[2] = cls.originZ;
    auto vec = [&](uint32_t i) { const data::ClsVec3& v = cls.vectors[i]; return Vec3f{v.x, v.y, v.z}; };
    auto vtx = [&](uint32_t i) { const data::ClsVec3& v = cls.vertices[i]; return Vec3f{v.x, v.y, v.z}; };
    m.faces.reserve(cls.faces.size());
    for (const data::ClsFace& f : cls.faces) {
        CollFace c;
        c.type = f.type;
        std::memcpy(c.attr, f.attr, sizeof(c.attr));
        c.normal = vec(f.normal);
        for (int k = 0; k < 3; ++k) {
            c.v[k] = vtx(f.verts[k]);
            c.edge[k].dir = vec(f.edges[k].vecA);
            c.edge[k].outward = vec(f.edges[k].vecB);
            c.edge[k].neighbour = f.edges[k].neighbor;
        }
        m.faces.push_back(c);
    }
    m.cells.resize(cls.cells.size());
    for (size_t i = 0; i < cls.cells.size(); ++i)
        for (uint32_t k = 0; k < cls.cells[i].count; ++k) m.cells[i].push_back(int(cls.faceRefs[cls.cells[i].firstRef + k]));
    return m;
}

// orig FUN_0020a050
void CollisionMesh::resetStamps() {
    stamp = 0;
    for (CollFace& f : faces) f.stamp = 0;
}

namespace {

// orig FUN_0020a110: next query stamp.
void nextStamp(CollisionMesh& m) {
    ++m.stamp;
    if (m.stamp == 0) m.resetStamps();
}

// orig FUN_0020a140 (record clear) / FUN_0020a150 (list counts clear)
void recordClear(HitRecord& r) {
    r.found = 0;
    r.l0.count = r.l1.count = r.l2.count = r.l3.count = 0;
}
void recordClearCounts(HitRecord& r) { r.l0.count = r.l1.count = r.l2.count = r.l3.count = 0; }

// orig FUN_0020b4e0: list = {item}
void listSet(HitList& l, const FaceHit& h) {
    if (l.cap != 0) {
        if (l.items.size() < l.cap) l.items.resize(l.cap);
        l.items[0] = h;
        l.count = 1;
    }
}
// orig FUN_0020b570: append if room
void listAppend(HitList& l, const FaceHit& h) {
    if (l.count < l.cap) {
        if (l.items.size() < l.cap) l.items.resize(l.cap);
        l.items[l.count++] = h;
    }
}

// orig FUN_0020a170: keep the nearest hits (ties within 0.01 accumulate).
bool recordFaceHit(HitRecord& r, const FaceHit& h, float dist) {
    if (r.found == 0) {
        r.found = 1;
        r.dist = dist;
        listSet(r.l0, h);
    } else {
        float d = dist - r.dist;
        if (d < 0.0f) d = -d;
        if (0.01f <= d) {
            if (r.dist < dist) return false;
            r.dist = dist;
            recordClearCounts(r);
            listSet(r.l0, h);
        } else {
            listAppend(r.l0, h);
        }
    }
    return true;
}

// orig FUN_0020a8a0: prepare the swept sphere for a mesh test.
void sweepBegin(Phys& p) {
    recordClear(p.world->rec);
    const Vec3f& pos = *p.pos;
    p.centre = {pos[0] + p.centreOffset[0], pos[1] + p.centreOffset[1], pos[2] + p.centreOffset[2]};
    p.dir = *p.move;
    p.length = normalize(p.dir);
    p.negDir = {-p.dir[0], -p.dir[1], -p.dir[2]};
    // phys+8 (sub-box) is always 0 for the physics object (FUN_0020a860), so the 0x53edb0 block is not taken.
}

// orig FUN_0020b260: sphere vs a vertex (edge end point); dist along the sweep to contact.
bool sweepVertex(const Phys& p, float& distOut, const Vec3f& vtx) {
    Vec3f d{vtx[0] - p.centre[0], vtx[1] - p.centre[1], vtx[2] - p.centre[2]};
    const float len = normalize(d);
    float proj = p.dir[2] * d[2] + p.dir[0] * d[0] + p.dir[1] * d[1];
    if (proj <= 0.0f) return false;
    proj = len * proj;
    float perp2 = (len * len + 0.0f) - proj * proj;
    float a = perp2 < 0.0f ? -perp2 : perp2;
    float back;
    if (a < 0.0001f) {
        back = std::sqrt(p.radius * p.radius);
    } else {
        const float perp = std::sqrt(perp2);
        if (p.radius < perp) return false;
        const float h = p.radius * p.radius - perp * perp;
        back = 0.0f;
        if (0.0f < h) back = std::sqrt(h);
    }
    if (p.length < proj - back) return false;
    distOut = proj - back;
    return true;
}

// orig FUN_0020adb0: swept sphere vs triangle. Returns hit distance and the edge that was crossed (or -1).
bool sweepFace(const Phys& p, float& distOut, int& edgeOut, const CollFace& f) {
    const float nx = f.normal[0], ny = f.normal[1], nz = f.normal[2];
    // approach speed = n . -dir, truncated to 6 decimals through (float)((double)(int64)(x * 1e6f) / 1e6)
    const float approach = trunc6(nz * p.negDir[2] + nx * p.negDir[0] + ny * p.negDir[1]);
    float t = 0.0f;
    if (approach <= 0.0f) return false;
    const Vec3f& v0 = f.v[0];
    if (nz * (p.centre[2] - v0[2]) + nx * (p.centre[0] - v0[0]) + ny * (p.centre[1] - v0[1]) + 0.0f + 0.0f < 0.0f) return false;
    // contact point candidate: sphere point closest to the plane
    float px = (p.centre[0] + 0.0f) - nx * p.radius;
    float py = (p.centre[1] + 0.0f) - ny * p.radius;
    float pz = (p.centre[2] + 0.0f) - nz * p.radius;
    const float h = (nz * pz + nx * px + ny * py + 0.0f + 0.0f) - (nz * v0[2] + nx * v0[0] + ny * v0[1] + 0.0f + 0.0f);
    if (h <= 0.0f) {
        px = px - nx * h;
        py = py - ny * h;
        pz = pz - nz * h;
    } else {
        t = h / approach;
        if (p.length < t) return false;
        px = px + p.dir[0] * t;
        py = py + p.dir[1] * t;
        pz = pz + p.dir[2] * t;
    }
    edgeOut = -1;
    const Vec3f* a = &f.v[0];
    for (int e = 0; e < 3; ++e) {
        const Vec3f* b = (e == 2) ? &f.v[0] : &f.v[e + 1];
        const CollEdge& ed = f.edge[e];
        const Vec3f& o = ed.outward;
        if (0.0f < o[2] * (pz - (*a)[2]) + o[0] * (px - (*a)[0]) + o[1] * (py - (*a)[1])) {
            edgeOut = e;
            const float ex = ed.dir[0], ey = ed.dir[1], ez = ed.dir[2];
            if (ez * (pz - (*a)[2]) + ex * (px - (*a)[0]) + ey * (py - (*a)[1]) + 0.0f + 0.0f < 0.0f)
                return sweepVertex(p, distOut, *a);
            if (0.0f < ez * (pz - (*b)[2]) + ex * (px - (*b)[0]) + ey * (py - (*b)[1]) + 0.0f + 0.0f)
                return sweepVertex(p, distOut, *b);
            // edge cylinder: plane through the edge containing the sweep direction
            Vec3f c1{(p.dir[1] * ez + 0.0f) - p.dir[2] * ey, (p.dir[2] * ex + 0.0f) - p.dir[0] * ez,
                     (p.dir[0] * ey + 0.0f) - p.dir[1] * ex};
            normalize(c1);
            const float off = (c1[2] * p.centre[2] + c1[0] * p.centre[0] + c1[1] * p.centre[1]) -
                              (c1[2] * (*a)[2] + c1[0] * (*a)[0] + c1[1] * (*a)[1]);
            if (p.radius < off) return false;
            Vec3f c2{(c1[1] * ed.dir[2] + 0.0f) - c1[2] * ed.dir[1], (c1[2] * ed.dir[0] + 0.0f) - c1[0] * ed.dir[2],
                     (c1[0] * ed.dir[1] + 0.0f) - c1[1] * ed.dir[0]};
            normalize(c2);
            float dist = (c2[2] * p.centre[2] + c2[0] * p.centre[0] + c2[1] * p.centre[1]) -
                         (c2[2] * (*a)[2] + c2[0] * (*a)[0] + c2[1] * (*a)[1]);
            if (dist < 0.0f) return false;
            const float rr = (p.radius * p.radius + 0.0f) - off * off;
            if (0.0f < rr) dist = dist - std::sqrt(rr);
            if (p.length < dist) return false;
            t = dist;
            break;
        }
        a = b;
    }
    distOut = t;
    return true;
}

// orig FUN_00140970 (phys vtable +8) -> FUN_0020ac30: face test; in ground-probe mode (flags bit16) faces whose
// normal is within 89 degrees of horizontal are skipped.
bool testFace(Phys& p, int faceIndex) {
    const CollFace& f = p.world->faces[size_t(faceIndex)];
    if ((p.flags & 0x10000) != 0) {
        if (dot3(f.normal, upVector()) < kCos89) return false;
    }
    float dist = 0.0f;
    int edge = -1;
    if (!sweepFace(p, dist, edge, f)) return false;
    return recordFaceHit(p.world->rec, FaceHit{faceIndex, edge >= 0 ? faceIndex : -1, edge}, dist);
}

// orig FUN_00209b20: cell index range of the box [mn, mx] extended by the move vector. Returns false if outside.
bool cellRange(int idx[6], const CollisionMesh& m, float mn[3], float mx[3], const Vec3f& mv) {
    for (int ax = 0; ax < 3; ++ax) {
        const float v = mv[size_t(ax)];
        if (v < 0.0f) mn[ax] = mn[ax] + v;
        else mx[ax] = mx[ax] + v;
        const int org = m.origin[ax];
        if (m.cellSize[ax] == 0.0f) {
            idx[ax] = org;
            idx[ax + 3] = org;
        } else {
            int lo;
            if (mn[ax] == 0.0f) {
                lo = org;
            } else {
                float q = mn[ax] / m.cellSize[ax];
                if (q < 0.0f) q = q - 1.0f;
                lo = org + int(q);
            }
            int hi;
            if (mx[ax] == 0.0f) {
                hi = org;
            } else {
                float q = mx[ax] / m.cellSize[ax];
                if (!(q < 0.0f)) q = q + 1.0f;
                hi = org + int(q);
            }
            idx[ax] = lo;
            idx[ax + 3] = hi;
        }
        if (!(idx[ax] < m.n[ax])) return false;
        if (idx[ax] < 0) idx[ax] = 0;
        if (idx[ax + 3] < 0) return false;
        if (!(idx[ax + 3] < m.n[ax])) idx[ax + 3] = m.n[ax] - 1;
    }
    return true;
}

// orig FUN_0020a9d0: test every cell overlapping the swept sphere's box. mode 7 = faces + dynamic lists
// (FUN_0020a770), mode 1 = faces only (FUN_0020a4a0); static CLS meshes have no dynamic lists.
bool meshTest(Phys& p, int mode) {
    (void)mode;
    CollisionMesh& m = *p.world;
    const float r = p.radius;
    float mn[3] = {p.centre[0] - r, p.centre[1] - r, p.centre[2] - r};
    float mx[3] = {p.centre[0] + r, p.centre[1] + r, p.centre[2] + r};
    int idx[6];
    if (!cellRange(idx, m, mn, mx, *p.move)) return false;
    nextStamp(m);
    bool hit = false;
    const int nx = m.n[0], nxz = m.n[0] * m.n[2];
    for (int iy = idx[1]; iy <= idx[4]; ++iy)
        for (int iz = idx[2]; iz <= idx[5]; ++iz)
            for (int ix = idx[0]; ix <= idx[3]; ++ix) {
                const size_t cell = size_t(ix + iy * nxz + iz * nx);
                if (cell >= m.cells.size()) continue;
                // orig FUN_0020a4a0: each face once per query (stamp at face+8)
                for (int fi : m.cells[cell]) {
                    CollFace& f = m.faces[size_t(fi)];
                    if (f.stamp != m.stamp) {
                        f.stamp = m.stamp;
                        hit = testFace(p, fi) || hit;
                    }
                }
            }
    return hit;
}

// orig FUN_0013f870: merge a mesh's nearest hits into the global list.
void mergeHits(const CollisionMesh& m, const Vec3f& offset) {
    GlobalHits& g = globalHits();
    const HitRecord& r = m.rec;
    if (g.count != 0) {
        float d = r.dist - g.nearest;
        if (d < 0.0f) d = -d;
        if (d < 0.01f) {
            g.nearest = r.dist;
        } else {
            if (g.nearest <= r.dist) return;
            g.count = 0;
            g.nearest = r.dist;
        }
    } else {
        g.nearest = r.dist;
    }
    for (uint32_t i = 0; i < r.l0.count; ++i) {
        GlobalHit h;
        h.mesh = &m;
        h.face = r.l0.items[i].face;
        h.edge = r.l0.items[i].edge;
        h.vec = offset;
        if (size_t(g.count) < g.list.size()) g.list[size_t(g.count)] = h;
        else g.list.push_back(h);
        ++g.count;
    }
}


// orig FUN_0013f990: merge a character/object hit (distance, owner phys, push normal) into the global list.
void addBodyHit(float dist, const CollBody& owner, const Vec3f& n) {
    GlobalHits& g = globalHits();
    if (g.count != 0) {
        float d = dist - g.nearest;
        if (d < 0.0f) d = -d;
        if (!(d < 0.01f)) {
            if (g.nearest <= dist) return;
            g.count = 0;
        }
    }
    GlobalHit h;
    h.mesh = nullptr;
    h.face = -1;   // +0 face* = 0
    h.edge = -1;   // +4 edge* = 0
    h.vec = n;
    h.owner = owner.phys;
    h.ownerId0 = owner.id0;   // read back by FUN_00141e40 as **(owner+0x5c)+0x1c
    g.nearest = dist;
    if (size_t(g.count) < g.list.size()) g.list[size_t(g.count)] = h;
    else g.list.push_back(h);
    ++g.count;
}

// (float)((double)(int64)(x * 1e6f) / 1e6): FUN_00246b90 (__fixsfdi) / FUN_00246748 (__floatdidf) / FUN_00247498
// (__divdf3) / FUN_002478d0 (__truncdfsf2). The double steps are libgcc soft-float (round to nearest even), so they
// run with MXCSR RC = nearest; the float multiply stays in PS2 rounding.
float trunc6(float x) {
    const int64_t n = int64_t(x * 1e+06f);
    const unsigned saved = _mm_getcsr();
    _mm_setcsr(saved & ~0x6000u);
    const float r = float(double(n) / 1000000.0);
    _mm_setcsr(saved);
    return r;
}

// Approach speed n . -dir in the mula/madda/madd order y, x, z (FUN_00140dd0 / FUN_00141370).
float approachSpeed(const Phys& p, const Vec3f& n) {
    return trunc6(((n[1] * p.negDir[1]) + n[0] * p.negDir[0]) + n[2] * p.negDir[2]);
}

// Shared head of FUN_00140dd0 / FUN_00141370: plane through `q` with normal `n`; false when the sweep moves away
// from it or starts behind it. gap = distance of the sphere's leading point to the plane.
bool planeGap(float r, const Phys& p, const Vec3f& n, const float* q, float& approach, float& gap) {
    approach = approachSpeed(p, n);
    if (approach <= 0.0f) return false;
    const Vec3f& s = *p.pos;
    float behind = n[1] * (s[1] - q[1]);
    behind = behind + n[0] * (s[0] - q[0]);
    behind = behind + n[2] * (s[2] - q[2]);
    if (behind < 0.0f) return false;
    const float lz = s[2] - n[2] * r, lx = s[0] - n[0] * r, ly = s[1] - n[1] * r;
    float a = n[1] * ly;
    a = a + n[0] * lx;
    a = a + n[2] * lz;
    float b = n[1] * q[1];
    b = b + n[0] * q[0];
    b = b + n[2] * q[2];
    gap = a - b;
    return true;
}

// orig FUN_00140dd0: sweep distance to the tangent plane of a cylinder (point q on it, normal n); -1 = no hit.
float cylinderPlaneDist(float r, const Phys& p, const Vec3f& n, const Vec3f& q) {
    float approach = 0.0f, gap = 0.0f;
    if (!planeGap(r, p, n, q.data(), approach, gap)) return -1.0f;
    if (gap <= 0.0f) return 0.0f;
    const float t = gap / approach;
    if (p.length < t) return -1.0f;
    return t;
}

// Box side normals FUN_00207010 (0,0,1), FUN_00206f90 (1,0,0), FUN_00207050 (0,0,-1), FUN_00206fd0 (-1,0,0).
enum BoxSide { kSidePosZ, kSidePosX, kSideNegZ, kSideNegX };
const Vec3f& sideNormal(BoxSide s) {
    static const Vec3f n[4] = {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}};
    return n[s];
}

// orig FUN_00141370: sweep distance to one side of a box; seg = {x0, y0, z0, x1, y1, z1} the side's end points.
float boxSideDist(float r, const Phys& p, BoxSide side, const float seg[6]) {
    const Vec3f& n = sideNormal(side);
    float approach = 0.0f, gap = 0.0f;
    if (!planeGap(r, p, n, seg, approach, gap)) return -1.0f;
    float t = 0.0f;
    if (0.0f < gap) {
        t = gap / approach;
        if (p.length < t) return -1.0f;
    }
    const Vec3f& s = *p.pos;
    const float z = s[2] + p.dir[2] * t;
    const float x = s[0] + p.dir[0] * t;
    switch (side) {
    case kSidePosZ:
        if (x + r < seg[0]) return -1.0f;
        if (seg[3] < x - r) return -1.0f;
        break;
    case kSidePosX:
        if (seg[2] < z - r) return -1.0f;
        if (z + r < seg[5]) return -1.0f;
        break;
    case kSideNegZ:
        if (seg[0] < x - r) return -1.0f;
        if (x + r < seg[3]) return -1.0f;
        break;
    case kSideNegX:
        if (z + r < seg[2]) return -1.0f;
        if (seg[5] < z - r) return -1.0f;
        break;
    }
    return t;
}

// orig FUN_00140ad0 (+ disasm 0x140ad0..0x140dc8): swept sphere (radius r, top `top`) vs an upright cylinder.
bool sweepCylinder(float r, float top, const Phys& p, const CollBody& ob) {
    const Phys& o = *ob.phys;
    const Vec3f& s = *p.pos;
    const Vec3f& mv = *p.move;
    const Vec3f end{s[0] + mv[0], s[1] + mv[1], s[2] + mv[2]};
    const Vec3f& op = *o.pos;
    const float dx = end[0] - op[0];
    const float dz = end[2] - op[2];
    const float rr = r + o.shape->radius;
    const float rr2 = rr * rr;
    if (!((0.0f + dx * dx) + dz * dz <= rr2)) return false;   // the end position must overlap
    if (o.shape->top < s[1]) return false;
    if (!(op[1] <= top)) return false;
    Vec3f d{p.dir[0], 0.0f, p.dir[2]};
    normalize(d);
    const Vec3f& up = upVector();
    Vec3f c{up[1] * d[2] - up[2] * d[1], up[2] * d[0] - up[0] * d[2], up[0] * d[1] - up[1] * d[0]};
    normalize(c);
    const float lat = (((c[1] * op[1]) + c[0] * op[0]) + c[2] * op[2]) - (((c[1] * s[1]) + c[0] * s[0]) + c[2] * s[2]);
    Vec3f n;
    if (lat == 0.0f) {
        n = {s[0] - op[0], 0.0f, s[2] - op[2]};
    } else {
        // contact on the cylinder seen along the move: offset `lat` sideways, sqrt(rr^2 - lat^2) back along d
        const float q = rr2 - lat * lat;
        float k = 0.0f;
        if (!(q <= 0.0f)) k = std::sqrt(q);
        const float px = (op[0] - c[0] * lat) - d[0] * k;
        const float pz = (op[2] - c[2] * lat) - d[2] * k;
        n = {px - op[0], 0.0f, pz - op[2]};
    }
    normalize(n);
    const float orad = o.shape->radius;
    const Vec3f q{op[0] + n[0] * orad, op[1] + n[1] * orad, op[2] + n[2] * orad};
    const float t = cylinderPlaneDist(r, p, n, q);
    if (t < 0.0f) return false;
    addBodyHit(t, ob, n);
    return true;
}

// orig FUN_00140f80 (+ disasm 0x140f80..0x14136c): swept sphere vs an upright box (half extents shape.a along the
// owner's right axis, shape.b along its forward axis), tested as the XZ bounding rectangle of the four side centres.
bool sweepBox(float r, float top, float half, const Phys& p, const CollBody& ob, const Vec3f& mid) {
    const Phys& o = *ob.phys;
    const Vec3f& op = *o.pos;
    const float a = o.shape->a, b = o.shape->b;
    const Vec3f& rt = *ob.right;
    const Vec3f& fw = *ob.fwd;
    const Vec3f pts[4] = {{op[0] + rt[0] * a, op[1] + rt[1] * a, op[2] + rt[2] * a},
                          {op[0] - rt[0] * a, op[1] - rt[1] * a, op[2] - rt[2] * a},
                          {op[0] + fw[0] * b, op[1] + fw[1] * b, op[2] + fw[2] * b},
                          {op[0] - fw[0] * b, op[1] - fw[1] * b, op[2] - fw[2] * b}};
    float maxX = pts[0][0], minX = pts[0][0], maxZ = pts[0][2], minZ = pts[0][2];
    for (int i = 1; i < 4; ++i) {
        const float x = pts[i][0];
        if (maxX < x) maxX = x;
        if (!(minX <= x)) minX = x;
        const float z = pts[i][2];
        if (maxZ < z) maxZ = z;
        if (!(minZ <= z)) minZ = z;
    }
    const float otop = o.shape->top;
    const float midY = 0.5f * (otop + op[1]);
    const float ey = otop - midY, ex = maxX - op[0], ez = maxZ - op[2];
    const float e2 = (ey * ey + ex * ex) + ez * ez;
    float reach = half + 0.0f;
    if (e2 != 0.0f) reach = half + std::sqrt(e2);
    const float my = mid[1] - midY, mx = mid[0] - op[0], mz = mid[2] - op[2];
    if (!((my * my + mx * mx) + mz * mz <= reach * reach)) return false;   // bounding spheres apart
    const Vec3f& s = *p.pos;
    if (otop < s[1]) return false;
    if (!(op[1] <= top)) return false;
    float seg[6] = {minX, 0.0f, maxZ, maxX, 0.0f, maxZ};
    float t = boxSideDist(r, p, kSidePosZ, seg);
    if (0.0f <= t) {
        addBodyHit(t, ob, sideNormal(kSidePosZ));
        return true;
    }
    seg[0] = maxX; seg[2] = maxZ; seg[3] = maxX; seg[5] = minZ;
    t = boxSideDist(r, p, kSidePosX, seg);
    if (0.0f <= t) {
        addBodyHit(t, ob, sideNormal(kSidePosX));
        return true;
    }
    seg[0] = maxX; seg[2] = minZ; seg[3] = minX; seg[5] = minZ;
    t = boxSideDist(r, p, kSideNegZ, seg);
    if (0.0f <= t) {
        addBodyHit(t, ob, sideNormal(kSideNegZ));
        return true;
    }
    seg[0] = minX; seg[2] = minZ; seg[3] = minX; seg[5] = maxZ;
    t = boxSideDist(r, p, kSideNegX, seg);
    if (0.0f <= t) {
        addBodyHit(t, ob, sideNormal(kSideNegX));
        return true;
    }
    return false;
}

// orig FUN_00140a10: dispatch on the other body's shape (radius 0 = no collision; a < 0 cylinder, a > 0 box).
bool sweepBody(float r, float top, float half, const Phys& p, const CollBody& ob, const Vec3f& mid) {
    const Shape* sh = ob.phys->shape;
    if (sh->radius == 0.0f) return false;
    const float a = sh->a;
    if (a < 0.0f) return sweepCylinder(r, top, p, ob);
    if (a <= 0.0f) return false;
    return sweepBox(r, top, half, p, ob, mid);
}

}  // namespace

// orig FUN_00140250 (work/decompiled/00140250.c + disasm 0x140250..0x140968).
bool collide(Phys& p, int mode) {
    GlobalHits& g = globalHits();
    g.count = 0;
    // FUN_00127210 == 0 (no room under the sweep): nothing is tested. Stand-in: no registered pieces.
    if (gScene == nullptr || gScene->pieces.empty()) return false;
    bool any = false;
    Vec3f& pos = *p.pos;
    const Vec3f saved = pos;
    for (CollisionPiece& piece : gScene->pieces) {
        // FUN_00127210/FUN_00127510 select the pieces whose AABB the swept sphere touches; every registered piece is
        // offered here and the cell range test rejects the rest (UNVERIFIED stand-in).
        p.world = piece.mesh;
        pos = {saved[0] - piece.offset[0], saved[1] - piece.offset[1], saved[2] - piece.offset[2]};
        sweepBegin(p);
        if (meshTest(p, mode)) {
            mergeHits(*piece.mesh, piece.offset);
            any = true;
        }
    }
    pos = saved;

    // Radius / top of the moving body: the shape's when it has an owner, else the physics radius above pos.
    const bool owned = p.shape != nullptr && p.shape->owner != nullptr;
    float rad, top;
    if (!owned) {
        rad = p.radius;
        top = pos[1] + rad;
    } else {
        top = p.shape->top;
        rad = p.shape->radius;
    }
    // Bounding sphere of the sweep: centre `mid` of [pos - dir*rad, pos + move + dir*rad], radius `half`.
    const Vec3f& mv = *p.move;
    const float rz = p.dir[2] * rad, rx = p.dir[0] * rad, ry = p.dir[1] * rad;
    const float sz = pos[2] - rz, sx = pos[0] - rx, sy = pos[1] - ry;
    const Vec3f mid{(sx + ((pos[0] + mv[0]) + rx)) * kHalf, (sy + ((pos[1] + mv[1]) + ry)) * kHalf,
                    (sz + ((pos[2] + mv[2]) + rz)) * kHalf};
    const float hy = mid[1] - sy, hx = mid[0] - sx, hz = mid[2] - sz;
    const float h2 = (hy * hy + hx * hx) + hz * hz;
    float half = 0.0f;
    if (h2 != 0.0f) half = std::sqrt(h2);

    static std::vector<CollBody> bodies;
    if (owned && mode == 7 && gScene->characters) {
        bodies.clear();
        gScene->characters(bodies);
        for (const CollBody& b : bodies) {
            if (b.solid && b.active && !b.removed && b.phys != nullptr && b.phys->shape != nullptr &&
                sweepBody(rad, top, half, p, b, mid))
                any = true;
        }
    }
    // Objects of the same room (cell low nibble), any mode and owner (FUN_00124c70 on both positions).
    if (gScene->cellAt && gScene->objects) {
        if (const uint32_t* cell = gScene->cellAt(pos)) {
            const uint32_t room = *cell & 0xfu;
            bodies.clear();
            gScene->objects(bodies);
            for (const CollBody& b : bodies) {
                if (b.removed || b.phys == nullptr || b.phys->shape == nullptr || b.pos == nullptr) continue;
                const uint32_t* oc = gScene->cellAt(*b.pos);
                if (oc == nullptr || (*oc & 0xfu) != room) continue;
                if (sweepBody(rad, top, half, p, b, mid)) any = true;
            }
        }
    }
    return any;
}

namespace {

// orig FUN_00142850 (disasm 0x142850): cylinder vs cylinder (also box vs box). out = self pos moved out to rr.
bool pushCylinders(const Phys& p, Vec3f& out, const CollBody& self, const CollBody& other) {
    const Vec3f& s = *self.pos;
    const Vec3f& o = *other.pos;
    const Shape& osh = *other.phys->shape;
    const float dz = o[2] - s[2];
    const float rr = p.shape->radius + osh.radius;
    const float dx = o[0] - s[0];
    const float d2 = (0.0f + dx * dx) + dz * dz;
    if (!(d2 <= rr * rr)) return false;
    if (osh.top < (*p.pos)[1]) return false;
    if (!((*other.phys->pos)[1] <= p.shape->top)) return false;
    const Vec3f& f = *self.fwd;
    if (d2 == 0.0f) {
        out = {s[0] + f[0] * rr, s[1] + f[1] * rr, s[2] + f[2] * rr};
    } else {
        const float d = std::sqrt(d2);
        const float k = d - rr;
        out = {s[0] + (dx / d) * k, s[1] + (0.0f / d) * k, s[2] + (dz / d) * k};
    }
    return true;
}

// orig FUN_00142990: self cylinder vs other box (other's forward/right axes, half extents b/a).
bool pushCylinderBox(const Phys& p, Vec3f& out, const CollBody& self, const CollBody& other) {
    const Vec3f& op = *other.pos;
    const Vec3f& fw = *other.fwd;
    const Vec3f& rt = *other.right;
    const Vec3f& sp = *self.pos;
    const Shape& osh = *other.phys->shape;
    const float rad = p.shape->radius;
    float ef = rad + osh.b;
    const float pf = (fw[2] * sp[2] + fw[0] * sp[0] + fw[1] * sp[1]) - (fw[2] * op[2] + fw[0] * op[0] + fw[1] * op[1]);
    const float apf = pf < 0.0f ? -pf : pf;
    if (ef < apf) return false;
    float er = rad + osh.a;
    const float pr = (rt[2] * sp[2] + rt[0] * sp[0] + rt[1] * sp[1]) - (rt[2] * op[2] + rt[0] * op[0] + rt[1] * op[1]);
    const float apr = pr < 0.0f ? -pr : pr;
    if (er < apr) return false;
    if (!((*p.pos)[1] <= osh.top)) return false;
    if (p.shape->top < (*other.phys->pos)[1]) return false;
    ef = ef - apf;
    er = er - apr;
    if (ef < er) {
        er = 0.0f;
        if (pf < 0.0f) ef = -ef;
    } else {
        ef = 0.0f;
        if (pr < 0.0f) er = -er;
    }
    out = {fw[0] * ef + sp[0] + 0.0f, fw[1] * ef + sp[1] + 0.0f, fw[2] * ef + sp[2] + 0.0f};
    out[0] = out[0] + rt[0] * er;
    out[1] = out[1] + rt[1] * er;
    out[2] = out[2] + rt[2] * er;
    return true;
}

// orig FUN_00142b70: self box vs other cylinder (self's forward/right axes).
bool pushBoxCylinder(const Phys& p, Vec3f& out, const CollBody& self, const CollBody& other) {
    const Vec3f& st = *self.pos;
    const Vec3f& fw = *self.fwd;
    const Vec3f& rt = *self.right;
    const Vec3f& op = *other.pos;
    const Shape& osh = *other.phys->shape;
    const float orad = osh.radius;
    float ef = p.shape->b + orad;
    const float pf = (fw[2] * op[2] + fw[0] * op[0] + fw[1] * op[1]) - (fw[2] * st[2] + fw[0] * st[0] + fw[1] * st[1]);
    const float apf = pf < 0.0f ? -pf : pf;
    if (ef < apf) return false;
    float er = p.shape->a + orad;
    const float pr = (rt[2] * op[2] + rt[0] * op[0] + rt[1] * op[1]) - (rt[2] * st[2] + rt[0] * st[0] + rt[1] * st[1]);
    const float apr = pr < 0.0f ? -pr : pr;
    if (er < apr) return false;
    if (!((*p.pos)[1] <= osh.top)) return false;
    if (p.shape->top < (*other.phys->pos)[1]) return false;
    ef = ef - apf;
    er = er - apr;
    if (ef < er) {
        er = 0.0f;
        if (pf < 0.0f) ef = -ef;
    } else {
        ef = 0.0f;
        if (pr < 0.0f) er = -er;
    }
    ef = -ef;
    out = {fw[0] * ef + st[0] + 0.0f, fw[1] * ef + st[1] + 0.0f, fw[2] * ef + st[2] + 0.0f};
    er = -er;
    out[0] = out[0] + rt[0] * er;
    out[1] = out[1] + rt[1] * er;
    out[2] = out[2] + rt[2] * er;
    return true;
}

int roomOf(const uint32_t* cell) {
    if (cell == nullptr) return -1;
    return (*cell & 0xf000u) == 0x1000u ? int(*cell & 0xfu) : -1;
}

}  // namespace

// orig FUN_00142290 (work/decompiled/00142290.c): average push target of every overlapping character in the same
// room (or either outside a room) within 4 cells; *move = average - pos.
bool characterPush(Phys& p) {
    Shape* sh = p.shape;
    if (sh == nullptr || sh->owner == nullptr || !(0.0f < sh->radius)) return false;
    if (gScene == nullptr || !gScene->characters) return false;
    static std::vector<CollBody> list;
    list.clear();
    gScene->characters(list);
    const CollBody* self = nullptr;
    for (const CollBody& b : list)
        if (b.owner == sh->owner) self = &b;
    if (self == nullptr || self->pos == nullptr) return false;   // port: the owner is always in list 0x50a7b0
    if (self->solid) return false;
    sh->top = sh->height + (*p.pos)[1];                          // FUN_00142270
    const int room = roomOf(self->cell);
    std::vector<Vec3f> outs;                                     // 0x50c550
    for (const CollBody& o : list) {
        if (o.owner == self->owner || !o.active || o.phys == nullptr || o.pos == nullptr) continue;
        const Shape* osh = o.phys->shape;
        if (osh == nullptr || osh->owner == nullptr || !(0.0f < osh->radius)) continue;
        const int orm = roomOf(o.cell);
        if (!(room < 0 || orm < 0 || room == orm)) continue;
        int dx = o.cellX - self->cellX;
        if (dx < 0) dx = -dx;
        if (!(dx < 4)) continue;
        int dy = o.cellY - self->cellY;
        if (dy < 0) dy = -dy;
        if (!(dy < 4)) continue;
        const float a = sh->a, oa = osh->a;
        bool hit = false;
        Vec3f out{};
        if (a < 0.0f && oa < 0.0f) hit = pushCylinders(p, out, *self, o);
        else if (a < 0.0f && 0.0f < oa) hit = pushCylinderBox(p, out, *self, o);
        else if (0.0f < a && oa < 0.0f) hit = pushBoxCylinder(p, out, *self, o);
        else if (0.0f < a && 0.0f < oa) hit = pushCylinders(p, out, *self, o);
        if (hit) outs.push_back(out);
    }
    if (outs.empty()) return false;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    for (const Vec3f& o : outs) {
        sx = sx + o[0];
        sy = sy + o[1];
        sz = sz + o[2];
    }
    const float fn = float(uint32_t(outs.size()));
    const Vec3f& pos = *p.pos;
    Vec3f& mv = *p.move;
    const float y = pos[1], z = pos[2];
    mv[0] = sx / fn - pos[0];
    mv[1] = sy / fn - y;
    mv[2] = sz / fn - z;
    return true;
}


namespace {

// orig FUN_001421a0: clear GroundInfo.
void groundClear(Phys& p) {
    p.ground.nWalls = 0;
    p.ground.nFloors = 0;
    p.ground.walls.fill(nullptr);
    p.ground.floors.fill(nullptr);
}

void landed(Phys& p) {  // orig FUN_00142140
    p.flags &= 0xfffffffcu;
    p.vy = 0.0f;
}
void fall(Phys& p) {    // orig FUN_00142180
    p.flags |= 1u;
    p.vy = 0.0f;
}

// orig FUN_00141950: vertical speed += gravity * 3, max 100.
float gravityStep(Phys& p) {
    const float v = p.gravity * 3.0f + p.vy + 0.0f;
    p.vy = v;
    if (100.0f < v) p.vy = 100.0f;
    return p.vy;
}

// orig FUN_00141dc0: one collision query; without a hit the whole move is applied.
bool sweepStep(Phys& p) {
    const bool hit = collide(p, 7);
    if (!hit) {
        Vec3f& pos = *p.pos;
        const Vec3f& mv = *p.move;
        pos[0] = pos[0] + mv[0];
        pos[1] = pos[1] + mv[1];
        pos[2] = pos[2] + mv[2];
    }
    return hit;
}

const CollFace& hitFace(const GlobalHit& h) { return h.mesh->faces[size_t(h.face)]; }

// orig FUN_00141e40: resolve the nearest hits into a new move direction (written to *move).
uint32_t resolveHits(Phys& p, Vec3f& mv, const Vec3f& original) {
    GlobalHits& g = globalHits();
    mv = p.dir;
    uint32_t r = 0;
    for (int i = 0; i < g.count; ++i) {
        const GlobalHit& h = g.list[size_t(i)];
        const Vec3f* n;
        Vec3f flat;
        uint32_t next;
        if (h.face < 0) {
            n = &h.vec;  // character/object push normal (FUN_0013f990)
            next = h.ownerId0 == 'O' ? (r | 1) : (r | 2);   // **(owner+0x5c)+0x1c == 'O': objects act as walls
        } else {
            const CollFace& f = hitFace(h);
            n = &f.normal;
            if (f.type != 'W') {
                next = r;
                if (f.type == 'F' && (p.flags & 1) != 0) next = r | 0x10;
            } else {
                next = r | 1;
                if (p.ground.nWalls < 4) p.ground.walls[p.ground.nWalls++] = &f;
                if (f.normal[1] != 0.0f) {
                    if (0.0f <= mv[1]) {
                        flat = {f.normal[0], 0.0f, f.normal[2]};
                        normalize(flat);
                        n = &flat;
                    } else if (kCos89 <= dot3(f.normal, upVector())) {
                        if ((p.flags & 1) != 0 && (p.flags & 2) == 0) next = r | 0x11;
                    } else {
                        slideDirection(mv, mv, f.normal);
                        mv[0] = mv[0] + f.normal[0];
                        mv[1] = mv[1] + f.normal[1];
                        mv[2] = mv[2] + f.normal[2];
                        mv[0] = mv[0] * kWallBlend;
                        mv[1] = mv[1] * kWallBlend;
                        mv[2] = mv[2] * kWallBlend;
                        normalize(mv);
                        r = next;
                        continue;
                    }
                }
            }
        }
        slideDirection(mv, mv, *n);
        r = next;
    }
    if (mv[2] * original[2] + mv[0] * original[0] + mv[1] * original[1] < 0.0f) mv = {0.0f, 0.0f, 0.0f};
    return r;
}

// orig FUN_001419a0: up to 5 sweep iterations, sliding along what was hit.
uint32_t sweep(Phys& p, const Vec3f& original) {
    Vec3f& mv = *p.move;
    const float sq = mv[2] * mv[2] + mv[0] * mv[0] + mv[1] * mv[1];
    float remaining = 0.0f;
    if (sq != 0.0f) remaining = std::sqrt(sq);
    uint32_t r = 0;
    int tries = 5;
    bool hit = sweepStep(p);
    GlobalHits& g = globalHits();
    while (true) {
        if (!hit) return r;
        if (0.01f < g.nearest) {
            remaining = remaining - g.nearest;
            Vec3f& pos = *p.pos;
            const float dz = p.dir[2] * g.nearest, dy = p.dir[1] * g.nearest;
            pos[0] = pos[0] + p.dir[0] * g.nearest;
            pos[1] = pos[1] + dy;
            pos[2] = pos[2] + dz;
            if (remaining < 0.01f) return r;
        } else {
            --tries;
            if (tries == 0) return r;
        }
        r |= resolveHits(p, mv, original);
        if ((r & 0x10) != 0) break;
        mv[0] = mv[0] * remaining;
        mv[1] = mv[1] * remaining;
        mv[2] = mv[2] * remaining;
        hit = sweepStep(p);
    }
    landed(p);
    return r;
}

// orig FUN_00141d40: push out of a wall under the feet by radius/2 along its horizontal normal.
void pushOutOfWall(Phys& p, const CollFace& wall) {
    Vec3f d{wall.normal[0], 0.0f, wall.normal[2]};
    normalize(d);
    const float s = p.radius * 0.5f;
    Vec3f& mv = *p.move;
    mv = {d[0] * s, d[1] * s, d[2] * s};
    sweepStep(p);
}

// orig FUN_00141b40: ground probe (sweep down by radius in probe mode).
uint32_t groundProbe(Phys& p) {
    const Vec3f& down = downVector();
    Vec3f& mv = *p.move;
    mv = {down[0] * p.radius, down[1] * p.radius, down[2] * p.radius};
    p.flags |= 0x10000u;
    const bool hit = collide(p, 1);
    p.flags &= 0xfffeffffu;
    if (!hit) {
        fall(p);
        return 0;
    }
    GlobalHits& g = globalHits();
    const CollFace* wallUnderFeet = nullptr;
    const CollFace* wall = nullptr;
    bool floor = false;
    for (int i = 0; i < g.count; ++i) {
        const GlobalHit& h = g.list[size_t(i)];
        if (h.face < 0) continue;
        const CollFace& f = hitFace(h);
        bool nextFloor = floor;
        if (f.type == 'F') {
            if (p.ground.nFloors < 4) p.ground.floors[p.ground.nFloors++] = &f;
            nextFloor = true;
            if (h.edge >= 0) {
                const int nb = f.edge[size_t(h.edge)].neighbour;
                if (nb >= 0 && h.mesh->faces[size_t(nb)].type == 'W') {
                    wallUnderFeet = &h.mesh->faces[size_t(nb)];
                    nextFloor = floor;
                }
            }
        } else if (f.type == 'W') {
            wall = &f;
        }
        floor = nextFloor;
    }
    if (!floor) {
        if (wall != nullptr) {
            fall(p);
            p.flags |= 2u;
        }
        if (wallUnderFeet != nullptr) {
            pushOutOfWall(p, *wallUnderFeet);
            return 0;
        }
    }
    Vec3f& pos = *p.pos;
    const float dz = p.dir[2] * g.nearest, dy = p.dir[1] * g.nearest;
    pos[0] = pos[0] + p.dir[0] * g.nearest;
    pos[1] = pos[1] + dy;
    pos[2] = pos[2] + dz;
    return 0x20;
}

}  // namespace

// orig FUN_00143150: temporary physics object (FUN_00140140), FUN_00140230 -> FUN_0020a860(pos=from, move=delta,
// radius, centre offset 0), FUN_00140250(mode 1); the shape owner is null so only meshes are tested.
bool raycast(const float from[3], const float delta[3], float radius, float* distOut) {
    Phys tmp;
    Vec3f pos{from[0], from[1], from[2]};
    Vec3f mv{delta[0], delta[1], delta[2]};
    Shape shape;
    tmp.pos = &pos;
    tmp.move = &mv;
    tmp.radius = radius;
    tmp.centreOffset = {0.0f, 0.0f, 0.0f};
    tmp.shape = &shape;
    const bool hit = collide(tmp, 1);
    if (hit && distOut != nullptr) *distOut = globalHits().nearest;  // FUN_001431e0
    return hit;
}

// orig FUN_00140140 (ctor) + FUN_0020a860 (shape/pointers) via FUN_00142e10: radius from PRMCHAR+0x98.
void physInit(Phys& p, Vec3f* pos, Shape* shape, float radius) {
    p = Phys{};
    p.pos = pos;
    p.move = &p.vel;
    p.radius = radius;
    p.centreOffset = {0.0f, radius, 0.0f};
    p.shape = shape;
    p.flags = 0;
    p.gravity = 1.0f;
    p.airTicks = 0;
    landed(p);
    groundClear(p);
}

// orig FUN_001416b0 (work/decompiled/001416b0.c)
uint32_t physMove(Phys& p) {
    groundClear(p);
    Vec3f& mv = *p.move;
    const Vec3f saved = mv;
    uint32_t r = 0;
    if ((p.flags & 1) != 0) {
        const float vy = gravityStep(p);
        mv[1] = mv[1] - vy;
    }
    if (mv[2] * mv[2] + mv[0] * mv[0] + mv[1] * mv[1] != 0.0f) r = sweep(p, saved);
    if (gScene != nullptr && gScene->characterPush && gScene->characterPush(p)) r = r | sweep(p, saved) | 2u;   // FUN_00142290
    if ((p.flags & 1) == 0 && p.gravity != 0.0f) {
        ++p.airTicks;
        float dist = 0.0f;
        if (gScene != nullptr && gScene->playerPos != nullptr) {
            const Vec3f& pp = *gScene->playerPos;
            const Vec3f& pos = *p.pos;
            const float dy = pp[1] - pos[1], dx = pp[0] - pos[0], dz = pp[2] - pos[2];
            const float sq = dz * dz + dx * dx + dy * dy + 0.0f + 0.0f;
            if (sq != 0.0f) dist = std::sqrt(sq);
        }
        const uint32_t t = p.airTicks;
        bool probe;
        if (dist < 1000.0f) probe = true;
        else if (t >= 2 && dist < 2000.0f) probe = true;
        else if (t >= 8 && dist < 4000.0f) probe = true;
        else if (t >= 0x10 && dist < 6000.0f) probe = true;
        else probe = 0x1f < t;
        if (probe) {
            r |= groundProbe(p);
            p.airTicks = 0;
        }
    }
    mv = saved;
    return r;
}

// orig FUN_00142e60 (work/decompiled/00142e60.c)
uint32_t physTick(Phys& p) {
    // orig FUN_00143060: ACT Move = rotateY(owner forward, angle) * speed
    Vec3f k{0.0f, 0.0f, 0.0f};
    if (p.actMoveSpeed != 0.0f && p.ownerForward != nullptr) {
        const Vec3f fwd{p.ownerForward[0], p.ownerForward[1], p.ownerForward[2]};
        rotateY((p.actMoveAngle * 3.1415927f) / 180.0f, k, fwd);
        k[0] = k[0] * p.actMoveSpeed;
        k[1] = k[1] * p.actMoveSpeed;
        k[2] = k[2] * p.actMoveSpeed;
    }
    p.vel[0] = p.moveVel[0] + k[0];
    p.vel[1] = p.moveVel[1] + k[1];
    p.vel[2] = p.moveVel[2] + k[2];
    if (p.ownerIsCharacter && !p.ownerInKnockbackOrPull && p.ownerLethargic) {
        p.vel[0] = p.vel[0] * kLethargyScale;
        p.vel[1] = p.vel[1] * kLethargyScale;
        p.vel[2] = p.vel[2] * kLethargyScale;
    }
    p.result = physMove(p);
    // FUN_00142fe0 publishes the player's position + (0, 500, 0) to 0x4e79cc = shadow pre-pass +0x1c (renderer +0xd4,
    // the shadow eye height used by FUN_001113d0) and the camera forward; game/post/shadow_pass takes the eye height
    // from the presented player position instead (not audio).
    if (p.actMoveAcc != 0.0f) {
        const float s = p.actMoveSpeed + p.actMoveAcc;
        p.actMoveSpeed = s;
        if (s < 0.0f) {
            p.actMoveAcc = 0.0f;
            p.actMoveSpeed = 0.0f;
        }
    }
    return p.result;
}

}  // namespace baroque::game
