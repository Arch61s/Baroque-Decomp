# native/audio: BRQSOUND.SFS archive, Sony ADPCM, HD/BD bank, IOPSND/SPU2 model and the EE sound manager
# (object 0x4e9790 + FUN_0020fc20..FUN_00213840). Output through a raylib AudioStream (48 kHz stereo).
# The sources are NOT added to Baroque-Native yet: the game side currently runs on game/sound_stub.cpp.
# Install later with baroque::audio::installSoundSystem() (audio/sound_system.hpp) and add
# ${BAROQUE_AUDIO_SOURCES} to Baroque-Native.
set(BAROQUE_AUDIO_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/audio/adpcm.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/audio/sfs.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/audio/bank.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/audio/spu.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/audio/iop.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/audio/sound_system.cpp")

target_sources(Baroque-Native PRIVATE ${BAROQUE_AUDIO_SOURCES})

# Headless regression check (no audio device): table bytes vs the ELF, raw ADPCM decode vs tools/sound_formats.py
# (bit-exact), BGM 0 / SE / voice rendered through the mixer to work/native_captures/audio_*.wav.
add_executable(Baroque-AudioCheck
  "${CMAKE_CURRENT_SOURCE_DIR}/checks/audio_check_main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/game/sound_stub.cpp"
  ${BAROQUE_AUDIO_SOURCES})
target_include_directories(Baroque-AudioCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-AudioCheck PRIVATE raylib)
target_compile_definitions(Baroque-AudioCheck PRIVATE
  BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets"
  BAROQUE_WORK_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work"
  BAROQUE_PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
if(MSVC)
  target_compile_options(Baroque-AudioCheck PRIVATE /W4 /O2 /utf-8)
endif()
