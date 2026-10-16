# Items / inventory / HUD / in-game windows (namespace baroque::game): native/game/items and native/game/hud.
# Integration entry points for the Game scene: game/hud/hud_api.hpp (see work/reports/ITEMS_HUD_NATIVE.md).
set(BAROQUE_ITEMS_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/item.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/inventory.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/item_effects.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/item_world.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/item_screen.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/chr_adapter.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/game_adapter.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/hud.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/hud_api.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/ui_sprite2d.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/message_log.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/map2d.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/hud_map.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/pause.cpp")
target_sources(Baroque-Native PRIVATE ${BAROQUE_ITEMS_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/game/hud/hud_env.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/item_links_game.cpp")

# Headless check (hidden raylib window for the GL context): fake player Chr, HUD init + ticks, sprite count /
# positions / colours of the status HUD, inventory add / equip, message log, and a capture of the UI sprite layer
# rendered by game::renderer2d() into a 1440x1080 render texture -> work/native_captures/hud_status.png.
# Links the module without game/hud/hud_env.cpp (engine / scene / menu reads) and without character.cpp: the check
# provides hudReadEnvironment() and characters() itself, so the front-end scenes, menus, AI and video are not needed.
add_executable(Baroque-ItemsCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/items_check_main.cpp"
  ${BAROQUE_ITEMS_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  ${BAROQUE_DATA_SOURCES}
  ${BAROQUE_ALCHEMY_SOURCES}
  ${BAROQUE_DUNGEON_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/game/render2d.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/flag_manager.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/sound_stub.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/help_bar.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/sprite.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/strings.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/text.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/texture_registry.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/ui_tables.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/ui/menu_tables.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/collision.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/act_runner.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/actor_anim.cpp")
target_include_directories(Baroque-ItemsCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-ItemsCheck PRIVATE raylib)
target_compile_definitions(Baroque-ItemsCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets"
  BAROQUE_WORK_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work")
if(MSVC)
  target_compile_options(Baroque-ItemsCheck PRIVATE /W4 /O2 /utf-8)
endif()
