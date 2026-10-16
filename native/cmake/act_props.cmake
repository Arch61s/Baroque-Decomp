# ACT presentation / prop commands (namespace baroque::game): weapon trails Track / TrackEx / TrackEnd (FUN_00145560,
# trail objects FUN_001aa880..FUN_001ab430, draw pass FUN_001aa530), head look-at NTurnPos / NTurnAct / NTurnEnd
# (FUN_001456f0..FUN_001459f0), C12A ShowBox / HideBox, item props ObjThrow / DropItem / ClearItm / GetItem / ShowItem /
# HideItem. Notes in STATUS.md (2026-10-10, ACT props).
target_sources(Baroque-Native PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/act_props.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/character/act_trail.cpp")
