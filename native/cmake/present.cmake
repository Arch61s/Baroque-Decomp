# PORT presentation layer (namespace baroque::game::present, work/reports/PRESENTATION_PLAN.md): tick snapshots and
# interpolation between them (present_state), widescreen layout of the 3-D / 2-D passes (present_layout) and the
# port-only settings file <exe>/baroque_port.ini (present_config). Draw entry points: game/present/present_draw.hpp.
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/present/present_config.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/present/present_layout.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/present/present_state.cpp")
