# TLK conversation runtime: talk manager 0x53c6e0 (FUN_001a1dc0 .. FUN_001aa1b0), NPC character classes
# (FUN_001b3220, FUN_001b39f0 .. FUN_001b7160) and the OPTION VOICE LIST page (mode 3, FUN_001f8d00 ..).
# Report: work/reports/TALK_NATIVE.md. The game binding (talk_wiring.cpp) and the dev option (talk_dev.cpp) are
# Baroque-Native only; the check binds its own TalkWorld.
set(BAROQUE_TALK_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/game/talk/talk_manager.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/talk/talk_select_rt.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/talk/talk_camera.cpp")
target_sources(Baroque-Native PRIVATE ${BAROQUE_TALK_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/game/talk/talk_wiring.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/talk/talk_dev.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/talk/autoplay_dev.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/talk/autoplay_mem.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/npc_class.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/page_voice_list.cpp")

# Headless check: every TLK through the manager's load / page parser / trigger expansion (EN + JP) against
# data::parseTlk, trigger histogram, scripted conversations with a fake world (pages, voices, triggers).
add_executable(Baroque-TalkCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/talk_check_main.cpp"
  ${BAROQUE_TALK_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/game/progress/talk_predicates.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/progress/talk_select.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/flag_manager.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/camera.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/help_bar.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/sprite.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/strings.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/text.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/render2d.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/texture_registry.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/ui_tables.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/archive.cpp"
  ${BAROQUE_DATA_SOURCES})
target_include_directories(Baroque-TalkCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-TalkCheck PRIVATE raylib)
target_compile_definitions(Baroque-TalkCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets")
if(MSVC)
  target_compile_options(Baroque-TalkCheck PRIVATE /W4 /O2 /utf-8 /bigobj)
endif()
