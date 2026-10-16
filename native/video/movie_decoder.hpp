#pragma once
// baroque::video::MovieDecoder - PSS demux + MPEG-2 decode (libavcodec) + RGBA conversion (libswscale), no raylib.
//
// Pull model: nextFrame() returns decoded frames in display order with a display time on the movie clock; audio
// demuxed on the way accumulates in audioQueue() (interleaved stereo s16 at sampleRate()) for the caller to drain.
// Movie clock origin = first audio PTS (first video PTS when a movie has no audio); frame n is due at
//   time(n) = time(n-1) + frameDuration()        (exactly 1001/30000 s for the disc movies)
// re-anchored to the decoded PTS only when the stream's own timestamp disagrees by more than one frame (a gap in
// the source). MoviePlayer presents by comparing these times with the audio playback clock.
#include "video/pss_demux.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVCodecParserContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;

namespace baroque::video {

struct MovieFrame {
    std::vector<uint8_t> rgba;  // width*height*4
    int width = 0;
    int height = 0;
    int index = 0;        // display-order frame number
    int64_t pts = kNoPts;  // decoder timestamp (90 kHz) when known
    double time = 0.0;    // seconds on the movie clock
};

struct MovieDecoderStats {
    uint64_t framesDecoded = 0;
    uint64_t framesWithoutPts = 0;
    uint64_t ptsResyncs = 0;          // frames whose PTS disagreed with the 29.97 cadence by > 1 frame
    double maxPtsDeviation = 0.0;     // seconds, |pts time - cadence time| before resync
    uint64_t decodeErrors = 0;
};

class MovieDecoder {
public:
    MovieDecoder();
    ~MovieDecoder();
    MovieDecoder(const MovieDecoder&) = delete;
    MovieDecoder& operator=(const MovieDecoder&) = delete;

    bool open(const std::filesystem::path& path, std::string* error = nullptr);
    void close();
    bool isOpen() const { return codec_ != nullptr; }

    // Next frame in display order; false when the stream is fully decoded (all audio is then in audioQueue()).
    bool nextFrame(MovieFrame& out);

    std::vector<int16_t>& audioQueue() { return audio_; }
    bool demuxEnded() const { return demuxEnded_; }
    bool hasAudio() const { return demuxer_.audioHeader().valid; }
    uint32_t sampleRate() const { return demuxer_.audioHeader().valid ? demuxer_.audioHeader().rate : 48000; }
    int width() const { return width_; }    // 0 until the first frame
    int height() const { return height_; }
    double frameDuration() const { return frameDuration_; }
    int64_t clockOriginPts() const { return clockOrigin_; }
    const PssDemuxer& demuxer() const { return demuxer_; }
    const MovieDecoderStats& stats() const { return stats_; }
    const std::string& error() const { return error_; }

private:
    bool pump();  // demux/decode until a frame is ready or the stream ends; false when nothing more can come
    void feedParser(const uint8_t* data, size_t size, int64_t pts, int64_t dts);
    void sendPacket(AVPacket* packet);
    void receiveFrames();
    bool convertFrame(const AVFrame* frame, MovieFrame& out);
    void assignTime(MovieFrame& frame);

    PssDemuxer demuxer_;
    AVCodecContext* codec_ = nullptr;
    AVCodecParserContext* parser_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* sws_ = nullptr;
    int swsWidth_ = 0;
    int swsHeight_ = 0;
    int swsFormat_ = -1;

    std::deque<MovieFrame> ready_;
    std::vector<int16_t> audio_;
    int64_t esOffset_ = 0;
    bool demuxEnded_ = false;
    bool parserFlushed_ = false;
    bool decoderDrained_ = false;
    int width_ = 0;
    int height_ = 0;
    double frameDuration_ = 1001.0 / 30000.0;
    bool frameDurationFromStream_ = false;
    int64_t clockOrigin_ = kNoPts;
    int nextIndex_ = 0;
    double lastTime_ = 0.0;
    bool haveLastTime_ = false;
    MovieDecoderStats stats_;
    std::string error_;
};

}  // namespace baroque::video
