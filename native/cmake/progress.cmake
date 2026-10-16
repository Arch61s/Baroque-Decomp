# Progression: talk condition predicates (FUN_001a0e60 + 81 block functions, lifted by tools/gen_progress_tables.py),
# talk selection (FUN_001a69e0 / FUN_001a6d00 / FUN_001a3cc0), save record / continue / database keep, run end
# (death, stairs, clear, hub return). Flag/counter managers and the save manager themselves are listed in game.cmake.
# Report: work/reports/PROGRESS_NATIVE.md.
set(BAROQUE_PROGRESS_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/game/progress/talk_predicates.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/progress/talk_select.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/progress/save_record.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/progress/progression.cpp")
target_sources(Baroque-Native PRIVATE ${BAROQUE_PROGRESS_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/game/progress/save_wiring.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/progress/player_progress.cpp")

# Headless check: predicate results for 8 flag states vs. a MIPS interpreter (checks/progress_expected.inc), every
# TLK file's talk index / condition / trigger flags, talk search per speaker, counter and run-reset rules, ranking
# insertion, and a byte-exact save file round trip (write -> read -> restore -> write) in a temp directory.
add_executable(Baroque-ProgressCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/progress_check_main.cpp"
  ${BAROQUE_PROGRESS_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/game/flag_manager.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/save_manager.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  ${BAROQUE_DATA_SOURCES}
  ${BAROQUE_DUNGEON_SOURCES})
target_include_directories(Baroque-ProgressCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-ProgressCheck PRIVATE raylib)
target_compile_definitions(Baroque-ProgressCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets")
if(MSVC)
  target_compile_options(Baroque-ProgressCheck PRIVATE /W4 /O2 /utf-8 /bigobj)
endif()
