target_sources(Baroque-Native PRIVATE platform/platform.cpp
  platform/controls.cpp)   # PORT: PC control bindings (controls.ini), see work/reports/CONTROLS_NATIVE.md

# PORT (packaging, work/reports/PACKAGING.md): player-facing EXE start-up (log file, console attach, first-start data
# setup from the player's disc image), icon + version resource, UTF-8 manifest.
set(BAROQUE_PORT_VERSION "0.1.0" CACHE STRING "Version shown in Baroque.exe properties and baroque.log (MAJOR.MINOR.PATCH)")
option(BAROQUE_DEV_ASSET_FALLBACK
       "Fall back to the developer tree work/assets when <exe>/assets is missing (OFF for release packages)" ON)
string(REPLACE "." ";" _baroque_ver_parts "${BAROQUE_PORT_VERSION}")
list(LENGTH _baroque_ver_parts _baroque_ver_len)
if(NOT _baroque_ver_len EQUAL 3)
  message(FATAL_ERROR "BAROQUE_PORT_VERSION must be MAJOR.MINOR.PATCH, got ${BAROQUE_PORT_VERSION}")
endif()
list(GET _baroque_ver_parts 0 _baroque_ver_major)
list(GET _baroque_ver_parts 1 _baroque_ver_minor)
list(GET _baroque_ver_parts 2 _baroque_ver_patch)

target_sources(Baroque-Native PRIVATE platform/app_host.cpp platform/data_setup.cpp)
target_compile_definitions(Baroque-Native PRIVATE
  BAROQUE_VERSION_STRING="${BAROQUE_PORT_VERSION}"
  BAROQUE_VERSION_MAJOR=${_baroque_ver_major}
  BAROQUE_VERSION_MINOR=${_baroque_ver_minor}
  BAROQUE_VERSION_PATCH=${_baroque_ver_patch})
if(NOT BAROQUE_DEV_ASSET_FALLBACK)
  target_compile_definitions(Baroque-Native PRIVATE BAROQUE_NO_DEV_ASSETS)
endif()
if(WIN32)
  target_sources(Baroque-Native PRIVATE platform/baroque.rc platform/baroque.manifest)
  # The icon is made from the game's own title logo (tools/make_icon.py, from your extracted assets), so it is not in
  # the repository; without it the EXE gets the default icon.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/platform/baroque.ico")
    target_compile_definitions(Baroque-Native PRIVATE BAROQUE_HAS_ICON)
  endif()
endif()
# Two EXEs from one link: Baroque-Native.exe stays a console program (developer / agent runs: shells wait for it and
# read stderr), Baroque.exe beside it is the same image with the PE subsystem set to WINDOWS (no console window; the
# player EXE that tools/package_release.ps1 ships). main() is the entry point of both (mainCRTStartup).
if(MSVC)
  target_link_options(Baroque-Native PRIVATE "/ENTRY:mainCRTStartup")
  get_filename_component(_baroque_linker_dir "${CMAKE_LINKER}" DIRECTORY)
  find_program(BAROQUE_EDITBIN editbin HINTS "${_baroque_linker_dir}" NO_DEFAULT_PATH)
  find_program(BAROQUE_EDITBIN editbin)
  if(NOT BAROQUE_EDITBIN)
    message(FATAL_ERROR "editbin.exe not found next to ${CMAKE_LINKER} (needed to produce the GUI Baroque.exe)")
  endif()
  add_custom_command(TARGET Baroque-Native POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:Baroque-Native>" "$<TARGET_FILE_DIR:Baroque-Native>/Baroque.exe"
    COMMAND "${BAROQUE_EDITBIN}" /nologo /SUBSYSTEM:WINDOWS "$<TARGET_FILE_DIR:Baroque-Native>/Baroque.exe"
    COMMENT "Baroque.exe (GUI subsystem copy of Baroque-Native.exe)"
    VERBATIM)
endif()
