# PORT (not in the original): hold-to-skip for EVT events + its bottom-right "Hold START to skip" prompt
# (namespace baroque::game). Hooks: hud/pause.cpp pauseEventHoldHook (FUN_0013af80 host), event_manager.cpp
# eventEngineTick (after FUN_0016d3b0). Report: work/reports/EVENTS_NATIVE.md section 7.
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/event/event_skip_prompt.cpp")
