# PORT: 16:9 title / menu backdrop art, read from <exe>/widescreen (game/render2d.cpp wideArt).
# Optional: without the folder the game shows the original 4:3 screens.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/widescreen")
  add_custom_command(TARGET Baroque-Native POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/resources/widescreen" "$<TARGET_FILE_DIR:Baroque-Native>/widescreen"
    COMMENT "widescreen backdrop art"
    VERBATIM)
endif()
