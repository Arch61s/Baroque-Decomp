#pragma once
// FFmpeg (libavcodec/libavutil/libswscale) headers with their warnings silenced for our /W4 build.
// Only these three LGPL libraries are used, dynamically linked (see native/cmake/video.cmake).
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
