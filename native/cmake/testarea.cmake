# Developer test area (port-only playground): tiled world + translated player movement/physics/ACT/camera.
add_executable(Baroque-TestArea
  "${CMAKE_CURRENT_SOURCE_DIR}/testarea/test_area_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/testarea/test_world.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/collision.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/player_move.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/act_runner.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/actor_anim.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/camera.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/pad.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/input.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/common.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/act.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/prm.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/text_tables.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/archive.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/map_formats.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/dungeon.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/dungeon_tables.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/floor_world.cpp"
  ${BAROQUE_ALCHEMY_SOURCES})
target_include_directories(Baroque-TestArea PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-TestArea PRIVATE raylib)
target_compile_definitions(Baroque-TestArea PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets")
if(MSVC)
  target_compile_options(Baroque-TestArea PRIVATE /W4 /O2 /utf-8)
endif()
