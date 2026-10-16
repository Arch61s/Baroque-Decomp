# EVT event interpreter 0x51b620 (FUN_0016c6d0 ..), stage-script objects 0x53c690 (FUN_00179f60 .. FUN_0017a510) and
# the event mode FUN_0017a0c0 (namespace baroque::game). Report: work/reports/EVENTS_NATIVE.md.
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/event/event_manager.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/event/event_cues.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/event/event_services.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/event/stage_script.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/event/event_sound.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/event/event_effects.cpp")
