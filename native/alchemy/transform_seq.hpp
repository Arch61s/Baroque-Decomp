#pragma once
// igTransformSequence1_5 playback (the `_transformInput` of an igTransform node): keyed translation / rotation /
// scale lists evaluated into a matrix at the engine clock. Translated from the class's matrix getter FUN_003fccb0
// (vtable 0x4decf0), key bracket FUN_003fca50, matrix build FUN_003fc680 (+ FUN_003fc590 translation, FUN_003fc9b0
// rotation, FUN_0039fb80 quaternion matrix, FUN_003a0330 centre of rotation, FUN_0039dc30 scale).
// igTransform._target 9 nodes drive the texture matrix (animated UV scroll of maps / characters), target 0 the
// model matrix (e.g. the E27 coin spin).
#include "alchemy/igb.hpp"
#include "alchemy/scene_render.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace baroque::alchemy {

// TransformSequence is declared in alchemy/scene_render.hpp (SceneModel owns them).

}  // namespace baroque::alchemy
