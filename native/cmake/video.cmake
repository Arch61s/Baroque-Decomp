# native/video: PSS cutscene demux (ours) + MPEG-2 decode through the local prebuilt FFmpeg.
# FFmpeg is used as LGPL: only libavcodec/libavutil/libswscale, dynamically linked (import libs + DLLs copied next
# to the EXE); no FFmpeg source is compiled in and no downloads are performed. Any FFmpeg 7.x shared (LGPL) Windows
# build works, e.g. a "win64-lgpl-shared-7.1" archive: include/, the import libraries in bin/ or lib/, DLLs in bin/.
set(BAROQUE_FFMPEG_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Simple 2000s DX  DECOMP/PingPong/build/toolchain/ThirdParty/ffmpeg-prefix/src/ffmpeg_external"
    CACHE PATH "Prebuilt FFmpeg tree with include/, bin/*.lib (or lib/*.lib) and bin/*.dll")
set(_baroque_ffmpeg_include "${BAROQUE_FFMPEG_ROOT}/include")
set(_baroque_ffmpeg_bin "${BAROQUE_FFMPEG_ROOT}/bin")
set(_baroque_ffmpeg_libdir "${_baroque_ffmpeg_bin}")
if(NOT EXISTS "${_baroque_ffmpeg_libdir}/avcodec.lib" AND EXISTS "${BAROQUE_FFMPEG_ROOT}/lib/avcodec.lib")
  set(_baroque_ffmpeg_libdir "${BAROQUE_FFMPEG_ROOT}/lib")
endif()

foreach(header libavcodec/avcodec.h libavutil/avutil.h libswscale/swscale.h)
  if(NOT EXISTS "${_baroque_ffmpeg_include}/${header}")
    message(FATAL_ERROR "FFmpeg header missing: ${_baroque_ffmpeg_include}/${header}\n"
                        "Set BAROQUE_FFMPEG_ROOT to a prebuilt FFmpeg tree (include/, bin/*.lib, bin/*.dll).")
  endif()
endforeach()

set(_baroque_ffmpeg_libs)
foreach(lib avcodec avutil swscale)
  if(NOT EXISTS "${_baroque_ffmpeg_libdir}/${lib}.lib")
    message(FATAL_ERROR "FFmpeg import library missing: ${_baroque_ffmpeg_libdir}/${lib}.lib")
  endif()
  list(APPEND _baroque_ffmpeg_libs "${_baroque_ffmpeg_libdir}/${lib}.lib")
endforeach()

# Load-time dependency closure of avcodec-61/avutil-59/swscale-8 (PE import tables of the maintainer's build). Another
# FFmpeg build has its own closure: then every DLL in its bin/ is copied.
set(_baroque_ffmpeg_dlls
    avcodec-61.dll avutil-59.dll swscale-8.dll swresample-5.dll
    libwebpmux.dll libwebp.dll libsharpyuv.dll
    jxl.dll jxl_cms.dll jxl_threads.dll brotlidec.dll brotlienc.dll brotlicommon.dll
    zlib1.dll)
foreach(dll ${_baroque_ffmpeg_dlls})
  if(NOT EXISTS "${_baroque_ffmpeg_bin}/${dll}")
    file(GLOB _baroque_ffmpeg_dlls RELATIVE "${_baroque_ffmpeg_bin}" "${_baroque_ffmpeg_bin}/*.dll")
    if(NOT _baroque_ffmpeg_dlls)
      message(FATAL_ERROR "No FFmpeg DLLs in ${_baroque_ffmpeg_bin}")
    endif()
    break()
  endif()
endforeach()

set(BAROQUE_VIDEO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/video/pss_demux.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/video/movie_decoder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/video/movie_player.cpp")

# Links FFmpeg into `target` and copies the DLLs beside its EXE after every build.
function(baroque_video_attach target)
  target_include_directories(${target} SYSTEM PRIVATE "${_baroque_ffmpeg_include}")
  target_link_libraries(${target} PRIVATE ${_baroque_ffmpeg_libs})
  if(MSVC)
    target_compile_options(${target} PRIVATE /external:W0)
  endif()
  foreach(dll ${_baroque_ffmpeg_dlls})
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_baroque_ffmpeg_bin}/${dll}" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endforeach()
endfunction()

target_sources(Baroque-Native PRIVATE ${BAROQUE_VIDEO_SOURCES})
baroque_video_attach(Baroque-Native)

# Headless regression check: demuxes every MOVIE_EN/*.PSS, decodes OPENING.PSS, writes captures.
add_executable(Baroque-VideoCheck
    "${CMAKE_CURRENT_SOURCE_DIR}/checks/video_check_main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/platform.cpp"
    ${BAROQUE_VIDEO_SOURCES})
target_include_directories(Baroque-VideoCheck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Baroque-VideoCheck PRIVATE raylib)
target_compile_definitions(Baroque-VideoCheck PRIVATE
    BAROQUE_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work/assets"
    BAROQUE_WORK_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../work")
if(MSVC)
  target_compile_options(Baroque-VideoCheck PRIVATE /W4 /O2 /utf-8)
endif()
baroque_video_attach(Baroque-VideoCheck)
