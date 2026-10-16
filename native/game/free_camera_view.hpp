#pragma once
#include <cstdint>
#include <vector>
// PORT-ONLY: presented matrix of the free mouse camera (game/free_camera.hpp) with wall / ceiling occlusion.
// Baroque-Native only (needs the floor's render models); nothing here writes logic state.
namespace baroque::game {

class FloorWorld;

// Drop the cached occluder triangles (call when the floor world is rebuilt or destroyed).
void freeCameraWorldReset();
// PORT: true when the segment a -> b hits the drawn map geometry (see-through dither of the occluded player)
bool freeCameraSegmentBlocked(const FloorWorld* world, const float a[3], const float b[3]);
// PORT: the keys (instance << 32 | mesh index, FloorWorld instance order) of every map mesh the segment crosses
void freeCameraSegmentMeshes(const FloorWorld* world, const float a[3], const float b[3], std::vector<uint64_t>& keys);

// Camera +0x18 layout rows right / up / back / eye. playerPos = presented player position; height / dist /
// defaultPitchDeg = the original camera preset (FUN_0013e100 globals); dt = seconds since the last presented frame.
// world (may be null) supplies the occluding map triangles; the game's collision sweep FUN_00143150 is used as well.
void freeCameraMatrix(const FloorWorld* world, const float playerPos[3], float height, float dist, float defaultPitchDeg,
                      float dt, float out[4][4]);

}  // namespace baroque::game
