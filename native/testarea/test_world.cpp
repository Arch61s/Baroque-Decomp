// Developer test world generator (see test_world.hpp).
#include "testarea/test_world.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace baroque::testarea {

namespace {

using V3 = std::array<float, 3>;

V3 sub(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 cross(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
float dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
V3 unit(V3 a) {
    const float l = std::sqrt(dot(a, a));
    if (l > 0.0f)
        for (float& x : a) x /= l;
    return a;
}

struct Builder {
    std::vector<WorldTri> tris;
    // triangle with its normal forced toward `want`
    void tri(V3 a, V3 b, V3 c, const V3& want, char type, int mat) {
        if (dot(cross(sub(b, a), sub(c, a)), want) < 0.0f) std::swap(b, c);
        tris.push_back(WorldTri{{a, b, c}, type, mat});
    }
    void quad(const V3& a, const V3& b, const V3& c, const V3& d, const V3& want, char type, int mat) {
        tri(a, b, c, want, type, mat);
        tri(a, c, d, want, type, mat);
    }
    // axis-aligned box: top face (floor type), four sides 'W'; outward normals. No bottom.
    void box(const V3& mn, const V3& mx, int topMat, int sideMat) {
        const float x0 = mn[0], y0 = mn[1], z0 = mn[2], x1 = mx[0], y1 = mx[1], z1 = mx[2];
        quad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {0, 1, 0}, 'F', topMat);
        quad({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, {0, 0, -1}, 'W', sideMat);
        quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1}, 'W', sideMat);
        quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {-1, 0, 0}, 'W', sideMat);
        quad({x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, {1, 0, 0}, 'W', sideMat);
    }
};

}  // namespace

TestWorld buildTestWorld() {
    TestWorld w;
    Builder b;
    const float E = 1200.0f;  // arena half extent
    const float wallH = 500.0f;
    // floor tiles (100 x 100, so collision faces stay small like the shipped meshes)
    for (float x = -E; x < E; x += 100.0f)
        for (float z = -E; z < E; z += 100.0f)
            b.quad({x, 0, z}, {x, 0, z + 100}, {x + 100, 0, z + 100}, {x + 100, 0, z}, {0, 1, 0}, 'F', 0);
    // boundary walls, facing inward
    b.quad({-E, 0, -E}, {E, 0, -E}, {E, wallH, -E}, {-E, wallH, -E}, {0, 0, 1}, 'W', 1);
    b.quad({-E, 0, E}, {E, 0, E}, {E, wallH, E}, {-E, wallH, E}, {0, 0, -1}, 'W', 1);
    b.quad({-E, 0, -E}, {-E, 0, E}, {-E, wallH, E}, {-E, wallH, -E}, {1, 0, 0}, 'W', 1);
    b.quad({E, 0, -E}, {E, 0, E}, {E, wallH, E}, {E, wallH, -E}, {-1, 0, 0}, 'W', 1);
    // raised platform (top at 300 = one dungeon level step); side walls leave openings where the stair ramp
    // (x 550..850 on the -z side) and the ramp (z 600..900 on the -x side) arrive.
    b.quad({400, 300, 400}, {400, 300, 1000}, {1000, 300, 1000}, {1000, 300, 400}, {0, 1, 0}, 'F', 3);
    b.quad({400, 0, 400}, {550, 0, 400}, {550, 300, 400}, {400, 300, 400}, {0, 0, -1}, 'W', 1);
    b.quad({850, 0, 400}, {1000, 0, 400}, {1000, 300, 400}, {850, 300, 400}, {0, 0, -1}, 'W', 1);
    b.quad({400, 0, 1000}, {1000, 0, 1000}, {1000, 300, 1000}, {400, 300, 1000}, {0, 0, 1}, 'W', 1);
    b.quad({1000, 0, 400}, {1000, 0, 1000}, {1000, 300, 1000}, {1000, 300, 400}, {1, 0, 0}, 'W', 1);
    b.quad({400, 0, 400}, {400, 0, 600}, {400, 300, 600}, {400, 300, 400}, {-1, 0, 0}, 'W', 1);
    b.quad({400, 0, 900}, {400, 0, 1000}, {400, 300, 1000}, {400, 300, 900}, {-1, 0, 0}, 'W', 1);
    // staircase up the platform's -z side: rendered as 12 steps (25 rise, 60 run) over a collision ramp,
    // like stair pieces whose collision is a slope.
    const float sx0 = 550.0f, sx1 = 850.0f, sz0 = 400.0f - 12.0f * 60.0f, sz1 = 400.0f;
    {
        // collision ramp: 'F' slope and the two side walls
        b.quad({sx0, 0, sz0}, {sx0, 300, sz1}, {sx1, 300, sz1}, {sx1, 0, sz0}, {0, 1, -0.4f}, 'F', -1);
        b.tri({sx0, 0, sz0}, {sx0, 300, sz1}, {sx0, 0, sz1}, {-1, 0, 0}, 'W', -1);
        b.tri({sx1, 0, sz0}, {sx1, 300, sz1}, {sx1, 0, sz1}, {1, 0, 0}, 'W', -1);
    }
    // ramp up the platform's -x side (smooth slope, rendered)
    const float rz0 = 600.0f, rz1 = 900.0f, rx0 = 400.0f - 720.0f, rx1 = 400.0f;
    b.quad({rx0, 0, rz0}, {rx1, 300, rz0}, {rx1, 300, rz1}, {rx0, 0, rz1}, {-0.4f, 1, 0}, 'F', 2);
    b.tri({rx0, 0, rz0}, {rx1, 300, rz0}, {rx1, 0, rz0}, {0, 0, -1}, 'W', 1);
    b.tri({rx0, 0, rz1}, {rx1, 300, rz1}, {rx1, 0, rz1}, {0, 0, 1}, 'W', 1);
    // pillar and a waist-high block
    b.box({-500, 0, -200}, {-300, 400, 0}, 4, 4);
    b.box({-800, 0, 400}, {-600, 120, 600}, 4, 4);

    // ---- collision mesh (everything except render-only stair steps)
    data::Cls& c = w.cls;
    std::map<std::array<float, 3>, uint32_t> vtxIndex, vecIndex;
    auto addVtx = [&](const V3& v) {
        auto it = vtxIndex.find(v);
        if (it != vtxIndex.end()) return it->second;
        const uint32_t i = uint32_t(c.vertices.size());
        c.vertices.push_back({v[0], v[1], v[2]});
        vtxIndex.emplace(v, i);
        return i;
    };
    auto addVec = [&](const V3& v) {
        auto it = vecIndex.find(v);
        if (it != vecIndex.end()) return it->second;
        const uint32_t i = uint32_t(c.vectors.size());
        c.vectors.push_back({v[0], v[1], v[2]});
        vecIndex.emplace(v, i);
        return i;
    };
    V3 mn{1e9f, 1e9f, 1e9f}, mx{-1e9f, -1e9f, -1e9f};
    for (const WorldTri& t : b.tris) {
        data::ClsFace f{};
        f.type = t.type;
        f.attr[0] = 0;
        const V3 n = unit(cross(sub(t.v[1], t.v[0]), sub(t.v[2], t.v[0])));
        f.normal = addVec(n);
        for (int k = 0; k < 3; ++k) {
            f.verts[k] = addVtx(t.v[size_t(k)]);
            const V3 d = unit(sub(t.v[size_t((k + 1) % 3)], t.v[size_t(k)]));
            f.edges[k].vecA = addVec(d);
            f.edges[k].vecB = addVec(unit(cross(d, n)));
            f.edges[k].neighbor = -1;
            for (int a = 0; a < 3; ++a) {
                mn[size_t(a)] = std::min(mn[size_t(a)], t.v[size_t(k)][size_t(a)]);
                mx[size_t(a)] = std::max(mx[size_t(a)], t.v[size_t(k)][size_t(a)]);
            }
        }
        c.faces.push_back(f);
    }
    // neighbours: faces sharing an edge (reverse vertex order)
    std::map<std::pair<uint32_t, uint32_t>, std::pair<uint32_t, int>> edgeOwner;
    for (uint32_t fi = 0; fi < c.faces.size(); ++fi)
        for (int k = 0; k < 3; ++k) edgeOwner[{c.faces[fi].verts[k], c.faces[fi].verts[(k + 1) % 3]}] = {fi, k};
    for (uint32_t fi = 0; fi < c.faces.size(); ++fi)
        for (int k = 0; k < 3; ++k) {
            auto it = edgeOwner.find({c.faces[fi].verts[(k + 1) % 3], c.faces[fi].verts[k]});
            if (it != edgeOwner.end()) c.faces[fi].edges[k].neighbor = int32_t(it->second.first);
        }
    c.vertexCount = uint32_t(c.vertices.size());
    c.vectorCount = uint32_t(c.vectors.size());
    c.faceCount = uint32_t(c.faces.size());
    c.bboxMin = {mn[0], mn[1], mn[2]};
    c.bboxMax = {mx[0], mx[1], mx[2]};
    c.cellSize[0] = 100.0f;
    c.cellSize[1] = 0.0f;
    c.cellSize[2] = 100.0f;
    const int gx0 = int(std::floor(mn[0] / 100.0f)), gz0 = int(std::floor(mn[2] / 100.0f));
    const int gx1 = int(std::ceil(mx[0] / 100.0f)), gz1 = int(std::ceil(mx[2] / 100.0f));
    c.nx = uint32_t(gx1 - gx0);
    c.ny = 1;
    c.nz = uint32_t(gz1 - gz0);
    c.originX = -gx0;
    c.originY = 0;
    c.originZ = -gz0;
    std::vector<std::vector<uint32_t>> cellFaces(size_t(c.nx) * c.nz);
    for (uint32_t fi = 0; fi < c.faces.size(); ++fi) {
        float fx0 = 1e9f, fx1 = -1e9f, fz0 = 1e9f, fz1 = -1e9f;
        for (int k = 0; k < 3; ++k) {
            const data::ClsVec3& v = c.vertices[c.faces[fi].verts[k]];
            fx0 = std::min(fx0, v.x); fx1 = std::max(fx1, v.x);
            fz0 = std::min(fz0, v.z); fz1 = std::max(fz1, v.z);
        }
        const int ix0 = std::max(0, int(std::floor(fx0 / 100.0f)) + c.originX - 1);
        const int ix1 = std::min(int(c.nx) - 1, int(std::floor(fx1 / 100.0f)) + c.originX);
        const int iz0 = std::max(0, int(std::floor(fz0 / 100.0f)) + c.originZ - 1);
        const int iz1 = std::min(int(c.nz) - 1, int(std::floor(fz1 / 100.0f)) + c.originZ);
        for (int iz = iz0; iz <= iz1; ++iz)
            for (int ix = ix0; ix <= ix1; ++ix) cellFaces[size_t(iz) * c.nx + size_t(ix)].push_back(fi);
    }
    c.cells.resize(cellFaces.size());
    for (size_t i = 0; i < cellFaces.size(); ++i) {
        c.cells[i].firstRef = uint32_t(c.faceRefs.size());
        c.cells[i].count = uint32_t(cellFaces[i].size());
        c.faceRefs.insert(c.faceRefs.end(), cellFaces[i].begin(), cellFaces[i].end());
    }
    c.cellCount = uint32_t(c.cells.size());

    // ---- render-only stair steps (collision is the ramp above)
    std::vector<WorldTri> render;
    for (const WorldTri& t : b.tris)
        if (t.material >= 0) render.push_back(t);
    Builder steps;
    for (int i = 0; i < 12; ++i) {
        const float z0 = sz0 + float(i) * 60.0f, top = 25.0f * float(i + 1);
        steps.box({sx0, 0, z0}, {sx1, top, z0 + 60.0f}, 2, 2);
    }
    render.insert(render.end(), steps.tris.begin(), steps.tris.end());
    w.tris = std::move(render);
    w.spawn = {0.0f, 0.0f, -600.0f};
    w.dummyPos = {-150.0f, 0.0f, -150.0f};
    return w;
}

}  // namespace baroque::testarea
