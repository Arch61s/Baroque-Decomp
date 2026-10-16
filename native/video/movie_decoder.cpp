#include "video/movie_decoder.hpp"

#include "platform/platform.hpp"
#include "video/ffmpeg_headers.hpp"

#include <cmath>
#include <cstring>

namespace baroque::video {

MovieDecoder::MovieDecoder() = default;
MovieDecoder::~MovieDecoder() { close(); }

bool MovieDecoder::open(const std::filesystem::path& path, std::string* error) {
    close();
    std::string err;
    if (!demuxer_.open(path, &err)) {
        if (error) *error = err;
        return false;
    }
    av_log_set_level(AV_LOG_ERROR);
    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    if (!decoder) {
        if (error) *error = "libavcodec has no mpeg2video decoder";
        demuxer_.close();
        return false;
    }
    codec_ = avcodec_alloc_context3(decoder);
    parser_ = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!codec_ || !parser_ || !packet_ || !frame_) {
        if (error) *error = "libavcodec allocation failed";
        close();
        return false;
    }
    codec_->thread_count = 1;  // 512x448 MPEG-2 decodes far above real time single-threaded; keeps latency = 0 frames
    codec_->pkt_timebase = AVRational{1, 90000};
    if (avcodec_open2(codec_, decoder, nullptr) < 0) {
        if (error) *error = "avcodec_open2(mpeg2video) failed";
        close();
        return false;
    }
    return true;
}

void MovieDecoder::close() {
    if (sws_) sws_freeContext(sws_);
    sws_ = nullptr;
    swsWidth_ = swsHeight_ = 0;
    swsFormat_ = -1;
    if (parser_) av_parser_close(parser_);
    parser_ = nullptr;
    if (codec_) avcodec_free_context(&codec_);
    if (packet_) av_packet_free(&packet_);
    if (frame_) av_frame_free(&frame_);
    demuxer_.close();
    ready_.clear();
    audio_.clear();
    esOffset_ = 0;
    demuxEnded_ = parserFlushed_ = decoderDrained_ = false;
    width_ = height_ = 0;
    frameDuration_ = 1001.0 / 30000.0;
    frameDurationFromStream_ = false;
    clockOrigin_ = kNoPts;
    nextIndex_ = 0;
    lastTime_ = 0.0;
    haveLastTime_ = false;
    stats_ = MovieDecoderStats{};
    error_.clear();
}

bool MovieDecoder::nextFrame(MovieFrame& out) {
    while (ready_.empty()) {
        if (!pump()) return false;
    }
    out = std::move(ready_.front());
    ready_.pop_front();
    return true;
}

bool MovieDecoder::pump() {
    if (!codec_) return false;
    if (!demuxEnded_) {
        PssDemuxer::VideoPacket video;
        PssDemuxer::AudioBlock audio;
        const PssDemuxer::Event ev = demuxer_.next(video, audio);
        if (ev == PssDemuxer::Event::Video) {
            feedParser(video.data.data(), video.data.size(), video.pts, video.dts);
        } else if (ev == PssDemuxer::Event::Audio) {
            audio_.insert(audio_.end(), audio.stereo.begin(), audio.stereo.end());
        } else {
            demuxEnded_ = true;
        }
        return true;
    }
    if (!parserFlushed_) {
        feedParser(nullptr, 0, kNoPts, kNoPts);  // flush the last picture out of the parser
        parserFlushed_ = true;
        return true;
    }
    if (!decoderDrained_) {
        sendPacket(nullptr);  // enter draining mode
        receiveFrames();
        decoderDrained_ = true;
        return true;
    }
    return false;
}

void MovieDecoder::feedParser(const uint8_t* data, size_t size, int64_t pts, int64_t dts) {
    const uint8_t* in = data;
    int remaining = static_cast<int>(size);
    const int64_t avPts = pts == kNoPts ? AV_NOPTS_VALUE : pts;
    const int64_t avDts = dts == kNoPts ? AV_NOPTS_VALUE : dts;
    do {
        uint8_t* outData = nullptr;
        int outSize = 0;
        const int used = av_parser_parse2(parser_, codec_, &outData, &outSize, in, remaining, avPts, avDts, esOffset_);
        if (used < 0) {
            stats_.decodeErrors++;
            break;
        }
        in += used;
        remaining -= used;
        esOffset_ += used;
        if (outSize > 0) {
            packet_->data = outData;
            packet_->size = outSize;
            packet_->pts = parser_->pts;
            packet_->dts = parser_->dts;
            sendPacket(packet_);
            receiveFrames();
            packet_->data = nullptr;
            packet_->size = 0;
        }
        if (size == 0) break;  // flush call: one pass
    } while (remaining > 0);
}

void MovieDecoder::sendPacket(AVPacket* packet) {
    const int rc = avcodec_send_packet(codec_, packet);
    if (rc < 0 && rc != AVERROR_EOF && rc != AVERROR(EAGAIN)) stats_.decodeErrors++;
}

void MovieDecoder::receiveFrames() {
    for (;;) {
        const int rc = avcodec_receive_frame(codec_, frame_);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return;
        if (rc < 0) {
            stats_.decodeErrors++;
            return;
        }
        MovieFrame mf;
        if (convertFrame(frame_, mf)) {
            assignTime(mf);
            ready_.push_back(std::move(mf));
        }
        av_frame_unref(frame_);
    }
}

bool MovieDecoder::convertFrame(const AVFrame* frame, MovieFrame& out) {
    if (frame->width <= 0 || frame->height <= 0) return false;
    if (!sws_ || swsWidth_ != frame->width || swsHeight_ != frame->height || swsFormat_ != frame->format) {
        if (sws_) sws_freeContext(sws_);
        sws_ = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width,
                              frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        swsWidth_ = frame->width;
        swsHeight_ = frame->height;
        swsFormat_ = frame->format;
        if (!sws_) {
            if (error_.empty()) error_ = "sws_getContext failed";
            return false;
        }
    }
    if (width_ == 0) {
        width_ = frame->width;
        height_ = frame->height;
        if (codec_->framerate.num > 0 && codec_->framerate.den > 0) {
            frameDuration_ = double(codec_->framerate.den) / double(codec_->framerate.num);
            frameDurationFromStream_ = true;
        }
    }
    out.width = frame->width;
    out.height = frame->height;
    out.rgba.resize(size_t(frame->width) * size_t(frame->height) * 4);
    uint8_t* dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
    const int dstStride[4] = {frame->width * 4, 0, 0, 0};
    sws_scale(sws_, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
    out.pts = frame->pts != AV_NOPTS_VALUE ? frame->pts : (frame->best_effort_timestamp != AV_NOPTS_VALUE
                                                               ? frame->best_effort_timestamp
                                                               : kNoPts);
    return true;
}

void MovieDecoder::assignTime(MovieFrame& frame) {
    frame.index = nextIndex_++;
    stats_.framesDecoded++;
    if (clockOrigin_ == kNoPts) {
        const PssStats& st = demuxer_.stats();
        clockOrigin_ = st.firstAudioPts != kNoPts ? st.firstAudioPts : (frame.pts != kNoPts ? frame.pts : 0);
    }
    double t = haveLastTime_ ? lastTime_ + frameDuration_ : 0.0;
    if (!haveLastTime_ && frame.pts != kNoPts) t = double(frame.pts - clockOrigin_) / 90000.0;
    if (frame.pts == kNoPts) {
        stats_.framesWithoutPts++;
    } else if (haveLastTime_) {
        const double ptsTime = double(frame.pts - clockOrigin_) / 90000.0;
        const double dev = std::fabs(ptsTime - t);
        if (dev > stats_.maxPtsDeviation) stats_.maxPtsDeviation = dev;
        if (dev > frameDuration_) {
            stats_.ptsResyncs++;
            t = ptsTime;
        }
    }
    frame.time = t;
    lastTime_ = t;
    haveLastTime_ = true;
}

}  // namespace baroque::video
