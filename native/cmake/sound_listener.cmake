# Sound host listener source (head of FUN_001150b0) + FUN_00115470 area lookup (game/sound_listener.*).
# See work/reports/AUDIO_NATIVE.md "Listener".
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/sound_listener.cpp")
