# Gameplay camera task (game/camera.*). Not linked into Baroque-Native yet.
set(BAROQUE_CAMERA_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/game/camera.cpp")

# Headless check: scripted player path with a never-hitting raycast; asserts no NaN, boom length == preset
# distance, reset/pitch clamp behaviour (see work/reports/CAMERA_NATIVE.md).
add_executable(Baroque-CameraCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/camera_check_main.cpp"
  ${BAROQUE_CAMERA_SOURCES})
target_include_directories(Baroque-CameraCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
  target_compile_options(Baroque-CameraCheck PRIVATE /W4 /O2 /utf-8)
endif()
