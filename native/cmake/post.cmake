# Post-processing passes of the game's frame (namespace baroque::game::post): screen-effects object 0x4e7b80
# (distance blur FUN_0010ede0, bloom FUN_0010e0c0, film grain FUN_0010fb90) and the player shadow pre-pass at
# renderer +0xd4 (FUN_001113d0). Report: work/reports/POST_NATIVE.md. Entry points: game/post/screen_effects.hpp,
# game/post/post_render.hpp, game/post/shadow_pass.hpp (wired in game/scenes/game_scene.cpp).
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/post/post_gl.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/post/screen_effects.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/post/post_render.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/post/shadow_pass.cpp")
