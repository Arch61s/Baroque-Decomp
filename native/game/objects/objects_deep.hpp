#pragma once
// Object classes of the factory FUN_0012ce70 that are placed only beyond floors 100-130 (second dungeon / scenario
// floors F411, F422, F423, F427, F428, F450, F451): objects_classes3.cpp. Report: work/reports/OBJECTS_NATIVE.md
// (section "2026-10-10 deep-floor classes").
//
// The class lookup (objects.cpp objectConstruct) already maps the tags to their ObjClass; these entry points run the
// class methods for the classes translated here and return false for every other class, so objects.cpp calls them
// first / alongside its own dispatch.
#include "game/objects/objects.hpp"

namespace baroque::game {

bool objectsDeepConstruct(Chr& c);   // class ctor (after FUN_00128590); true = class handled here
bool objectsDeepTick(Chr& c);        // vt+0xc; true = class handled here
bool objectsDeepDestroy(Chr& c);     // class destructor (vtable +8) side effects; true = class handled here

}  // namespace baroque::game
