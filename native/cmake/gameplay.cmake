# Gameplay runtime in Baroque-Native: Game scene, characters, collision/physics, player movement, ACT player,
# actor animation, camera, floor world (namespace baroque::game). player controller FUN_0013b0d0 (player_control.cpp).
set(BAROQUE_GAMEPLAY_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/game/scenes/game_scene.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/character.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/enemy_classes.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/ai.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/combat.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/collision.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/player_move.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/act_runner.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/actor_anim.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/camera.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/floor_world.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/player_control.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/player_control_wiring.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/aim_arc_draw.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/act_effects.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/player_event.cpp")
target_sources(Baroque-Native PRIVATE ${BAROQUE_GAMEPLAY_SOURCES})
