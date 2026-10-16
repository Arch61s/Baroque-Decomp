# Character status set FUN_00138850 (+ FUN_00137220 / FUN_00137540 / FUN_00137d40): flag hook FUN_00178890 and the
# save-record read FUN_00139b40 (game/character/status_set.*). Kept out of gameplay.cmake while combat.cpp is shared.
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/status_set.cpp")
