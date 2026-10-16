# PORT-ONLY keyboard + mouse camera-relative attacks and aim assist (game/aim_assist.*; work/reports/CONTROLS_NATIVE.md
# "Aim assist"). The hook table is header-only (player_control.cpp calls it; empty in Baroque-PlayerControlCheck).
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/aim_assist.cpp")
