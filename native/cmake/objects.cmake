# Dungeon objects (factory table 0x44aa10 J/O classes, their type-5 helpers) and the item entities of the character
# task list (namespace baroque::game): native/game/objects. Report: work/reports/OBJECTS_NATIVE.md.
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/objects/objects.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/objects/objects_classes.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/objects/objects_classes2.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/objects/item_entity.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/objects/item_tasks.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/objects/projectiles.cpp")
