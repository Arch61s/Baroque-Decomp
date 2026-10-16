#pragma once
// PORT presentation (PRESENTATION_PLAN.md 2.3): draw entry points that take their transforms from the snapshot
// presenter (game/present/present_state.hpp) instead of the live Chr / prevPos. Defined next to the draws they mirror
// (character.cpp chrDrawAt, objects.cpp objectDrawAt, enemy_classes.cpp chrClassDrawAt / chrClassShellMatrix).
// None of them writes logic state.
#include "alchemy/scene_render.hpp"

#include <cstdint>
#include <vector>

namespace baroque::game {

struct Chr;

struct ChrDrawState {
    alchemy::Mat4 actor = alchemy::Mat4::identity();    // actor world matrix (axes x scale, position)
    const std::vector<alchemy::Mat4>* body = nullptr;   // body bone world pose for attach points (null = Chr::model.pose)
    bool hasShell = false;                              // E19 shell drawn at `shell`
    alchemy::Mat4 shell = alchemy::Mat4::identity();
    int64_t objAnimTime = -1;                           // O00J transform time (-1 = ObjData::animTimeNs as is)
};

// chrDraw (character.cpp) from a snapshot state; objects go to objectDrawAt
void chrDrawAt(const Chr& c, const ChrDrawState& s, alchemy::SceneRenderer& r, int pass);
// objectDraw (objects.cpp) at the given published matrix
void objectDrawAt(const Chr& c, const alchemy::Mat4& m, int64_t animTimeNs, alchemy::SceneRenderer& r, int pass);
// chrClassDraw (enemy_classes.cpp, E19 shell) at the given matrix
void chrClassDrawAt(const Chr& c, const alchemy::Mat4& shell, alchemy::SceneRenderer& r, int pass);
// the E19 shell draw matrix chrClassDraw would use (false: the class draws nothing)
bool chrClassShellMatrix(const Chr& c, alchemy::Mat4* out);
// Light-follows-camera (FUN_0013e340 tail): the presented camera's -forward replaces the main light slot's direction
// in modelBeginDraw (model_fx.cpp) while set; null = the logic value (renderLights()). The logic value is untouched.
void modelSetPresentLightDir(const float* dir3);

}  // namespace baroque::game
