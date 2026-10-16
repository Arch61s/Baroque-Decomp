# PORT-ONLY free mouse camera presentation (game/free_camera.hpp state is header-only; the view with the map-triangle
# occlusion needs the floor's render models). See work/reports/CAMERA_NATIVE.md §7.
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/free_camera_view.cpp")
