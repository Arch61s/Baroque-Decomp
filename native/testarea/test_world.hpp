#pragma once
// Developer test world (port-only, not game content): a tiled floor, boundary walls, a staircase and a ramp up to a
// raised platform, and a pillar. Geometry is emitted twice from the same triangles:
//   * as a CLS collision mesh in the exact runtime conventions of the shipped .CLS files (verified on
//     MR00_00.CLS: normal = CCW cross, edge vecA = unit a->b, vecB = vecA x normal, 100-unit xz grid with
//     origin = -floor(bboxMin / 100)), so the translated sweep/probe code runs unmodified;
//   * as a render mesh with a procedural prototype-grid texture.
#include "data/map_formats.hpp"

#include <array>
#include <vector>

namespace baroque::testarea {

struct WorldTri {
    std::array<std::array<float, 3>, 3> v;  // counter-clockwise seen from the walkable/outer side
    char type;                              // 'F' floor, 'W' wall
    int material;                           // 0 floor, 1 wall, 2 stairs, 3 platform, 4 pillar
};

struct TestWorld {
    std::vector<WorldTri> tris;
    data::Cls cls;
    std::array<float, 3> spawn{0.0f, 0.0f, -400.0f};
    std::array<float, 3> dummyPos{0.0f, 0.0f, 300.0f};
};

TestWorld buildTestWorld();

}  // namespace baroque::testarea
