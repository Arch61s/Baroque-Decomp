# PORT-ONLY keyboard button prompts (game/ui/key_prompt.*): PS2 button glyphs are presented as keycaps of the bound
# PC key while keyboard / mouse is the last-used input device. Installs the label provider of game/render2d.cpp.
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/key_prompt.cpp")
