# PORT-ONLY keyboard + mouse gun reticle and reticle aim (game/gun_aim.*, game/hud/gun_reticle.*;
# work/reports/CONTROLS_NATIVE.md "Gun reticle"). Baroque-Native only: the J030 launch (projectiles.cpp) and the
# attack start (aim_assist.cpp) call it; the reticle installs itself as the HUD window overlay (hud_api.cpp).
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/gun_aim.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/gun_reticle.cpp")
