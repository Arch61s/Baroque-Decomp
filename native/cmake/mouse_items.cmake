# PORT: mouse layer of the in-game item screen (game/items/item_screen_mouse.cpp, MOUSE_MENUS_NATIVE.md). Baroque-Native
# only: it needs game/ui/mouse_menu.cpp, which Baroque-ItemsCheck does not link (the hooks stay null there).
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/items/item_screen_mouse.cpp")
