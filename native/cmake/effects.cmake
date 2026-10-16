# EXI particle-effect runtime (namespace baroque::game): native/game/effects. Report: work/reports/EFFECTS_NATIVE.md.
# Entry points for other modules: game/effects/effects.hpp (effects().spawn(...) = FUN_00112190 & co.) and
# game/effects/effects_gl.hpp (effectsDraw(view) = FUN_001120b0, once per tick in the 3-D pass).
set(BAROQUE_EFFECTS_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/game/effects/exi_math.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/effects/effects.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/effects/effects_draw.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/effects/effects_gl.cpp")
target_sources(Baroque-Native PRIVATE ${BAROQUE_EFFECTS_SOURCES})

# Headless check (hidden raylib window for the GL context): spawns every EFCS id, runs N ticks of the original
# draw+update pass and prints particle statistics (twice, hash must match), then renders selected effects into
# work/native_captures/effects_<id>.png.
add_executable(Baroque-EffectsCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/effects_check_main.cpp"
  ${BAROQUE_EFFECTS_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  ${BAROQUE_DATA_SOURCES})
target_include_directories(Baroque-EffectsCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-EffectsCheck PRIVATE raylib)
target_compile_definitions(Baroque-EffectsCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets"
  BAROQUE_WORK_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work")
if(MSVC)
  target_compile_options(Baroque-EffectsCheck PRIVATE /W4 /O2 /utf-8)
endif()
