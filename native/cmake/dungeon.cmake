# Dungeon floor system: DATA.DIR archive view, FDT/RDT/CLS parsers, floor generator (game/dungeon.*).
set(BAROQUE_DUNGEON_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/data/archive.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/map_formats.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/dungeon.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/dungeon_tables.cpp")
target_sources(Baroque-Native PRIVATE ${BAROQUE_DUNGEON_SOURCES})

# Headless check: parses every FDT/RDT/CLS under work/assets, compares counts with work/map_formats_summary.json,
# generates floors with fixed seeds and asserts invariants. Links raylib only because platform.cpp needs it.
add_executable(Baroque-DungeonCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/dungeon_check_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  ${BAROQUE_DUNGEON_SOURCES})
target_include_directories(Baroque-DungeonCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-DungeonCheck PRIVATE raylib)
target_compile_definitions(Baroque-DungeonCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets"
  BAROQUE_WORK_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work")
if(MSVC)
  target_compile_options(Baroque-DungeonCheck PRIVATE /W4 /O2 /utf-8)
endif()
