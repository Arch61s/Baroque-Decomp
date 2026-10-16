# Player controller FUN_0013b0d0 + callees (game/character/player_control.*). Not linked into Baroque-Native here:
# the Game scene owner adds game/character/player_control.cpp next to character.cpp in its own cmake file.
#
# Headless check: hand-built C00A player Chr + DATA/ACTION/C00A.ACT, scripted pad input; verifies the chosen ACT
# actions and move velocities per tick (see work/reports/PLAYER_CONTROL_NATIVE.md).
add_executable(Baroque-PlayerControlCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/player_control_check_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/player_control.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/player_move.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/act_runner.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/actor_anim.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/collision.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/camera.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/pad.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/options.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/flag_manager.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/sound_stub.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/input.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/common.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/act.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/prm.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/text_tables.cpp"
  ${BAROQUE_ALCHEMY_SOURCES})
target_include_directories(Baroque-PlayerControlCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-PlayerControlCheck PRIVATE raylib)
target_compile_definitions(Baroque-PlayerControlCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets")
if(MSVC)
  target_compile_options(Baroque-PlayerControlCheck PRIVATE /W4 /O2 /utf-8)
endif()
