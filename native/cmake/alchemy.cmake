set(BAROQUE_ALCHEMY_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/alchemy/igb.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/alchemy/igb_elf_fields.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/alchemy/texture.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/alchemy/scene_render.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/alchemy/transform_seq.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/alchemy/enbaya.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/alchemy/animation.cpp")
target_sources(Baroque-Native PRIVATE ${BAROQUE_ALCHEMY_SOURCES})

# Headless regression check: loads every .IGB (counts vs work/reports/igb_classes.json) and renders
# reference models to work/native_captures/*.png.
add_executable(Baroque-AlchemyCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/alchemy_check_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  ${BAROQUE_ALCHEMY_SOURCES})
target_include_directories(Baroque-AlchemyCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-AlchemyCheck PRIVATE raylib)
target_compile_definitions(Baroque-AlchemyCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets"
  BAROQUE_WORK_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work")
if(MSVC)
  target_compile_options(Baroque-AlchemyCheck PRIVATE /W4 /O2 /utf-8)
endif()

# Enbaya decoder / pose check: corpus-wide structural decode, bit-exact dump vs tools/enbaya_dump.py
# (--dump OUT FILE...), posed contact sheets work/native_captures/anim_*.png.
add_executable(Baroque-AnimCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/anim_check_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  ${BAROQUE_ALCHEMY_SOURCES})
target_include_directories(Baroque-AnimCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-AnimCheck PRIVATE raylib)
target_compile_definitions(Baroque-AnimCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets"
  BAROQUE_WORK_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work")
if(MSVC)
  target_compile_options(Baroque-AnimCheck PRIVATE /W4 /O2 /utf-8)
endif()
