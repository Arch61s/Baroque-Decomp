#include <cstdio>
#include "video/movie_player.hpp"
#include "rlgl.h"

#include "platform/platform.hpp"

#include <algorithm>
#include <cstring>

namespace baroque::video {
namespace {

// raylib's AudioCallback has no user pointer; only one movie plays at a time (the original has a single
// player struct 0x540930 as well).
std::atomic<MoviePlayer*> g_activePlayer{nullptr};

constexpr double kAudioAheadSeconds = 0.5;  // keep this much PCM queued in the ring
constexpr double kVideoAheadSeconds = 0.5;  // and this much video decoded beyond the clock
constexpr double kRingSeconds = 2.0;
constexpr size_t kMaxQueuedFrames = 48;     // hard cap on decoded-but-not-due frames (RGBA 512x448 = 0.9 MB each)
constexpr int kFramesPerUpdate = 8;         // decode budget per update() so a slow frame cannot stall the host
constexpr int kFramesAtStart = 64;          // first update: prime audio + video ahead

}  // namespace

MoviePlayer::MoviePlayer() = default;
MoviePlayer::~MoviePlayer() { close(); }

uint32_t MoviePlayer::originalVolumeWord(int percent) {
    // orig FUN_00213840 (work/reports/SCENES.md §0.9): volume 0..100 -> (((v*64 - v) * 256 / 100) * 256) | 1
    const int v = std::clamp(percent, 0, 100);
    return (uint32_t(((v * 64 - v) * 256 / 100) * 256)) | 1u;
}

bool MoviePlayer::open(const std::filesystem::path& path, std::string* error) {
    close();
    std::string err;
    if (!decoder_.open(path, &err)) {
        if (error) *error = err;
        return false;
    }
    opened_ = true;
    // Decode the first frame now: the first pack carries the sce audio header, so the sample rate is known
    // before the audio stream is created. The PCM demuxed on the way stays in the decoder queue until update().
    MovieFrame first;
    if (decoder_.nextFrame(first)) {
        queue_.push_back(std::move(first));
    } else {
        decoderDone_ = true;
    }
    sampleRate_ = decoder_.sampleRate();
    ringFrames_ = size_t(kRingSeconds * double(sampleRate_));
    ring_.assign(ringFrames_ * 2, 0);
    ringWritten_.store(0);
    ringRead_.store(0);
    underruns_.store(0);
    callbacks_.store(0);
    silentCallbacks_.store(0);
    maxCallbackFrames_.store(0);
    lastCallbackFrames_.store(0);
    lastCallbackTime_.store(0.0);
    audioPrimed_.store(false);
    audioActive_ = false;
    if (decoder_.hasAudio() && IsAudioDeviceReady()) {
        stream_ = LoadAudioStream(sampleRate_, 16, 2);
        if (stream_.buffer != nullptr) {
            g_activePlayer.store(this);
            SetAudioStreamCallback(stream_, &MoviePlayer::audioCallback);
            SetAudioStreamVolume(stream_, volume_);
            audioActive_ = true;
            // Pre-roll in silence: the device sometimes catches up with a burst of periods when a stream first
            // starts; absorbing that before the first real sample keeps the clock from jumping at frame 0.
            PlayAudioStream(stream_);
            const double t0 = GetTime();
            while (silentCallbacks_.load() < 2 && GetTime() - t0 < 0.05) {}
        } else {
            platform::log("movie_player: audio stream unavailable, using wall clock");
        }
    }
    return true;
}

void MoviePlayer::close() {
    if (audioActive_) {
        StopAudioStream(stream_);
        UnloadAudioStream(stream_);  // takes the audio mutex: no callback runs after this returns
        g_activePlayer.store(nullptr);
        audioActive_ = false;
    }
    stream_ = AudioStream{};
    if (texture_.id != 0) UnloadTexture(texture_);
    texture_ = Texture2D{};
    decoder_.close();
    queue_.clear();
    current_ = MovieFrame{};
    haveCurrent_ = currentDirty_ = false;
    decoderDone_ = opened_ = finished_ = started_ = false;
    audioPrimed_.store(false);
    ring_.clear();
    ringFrames_ = 0;
    audioEndClock_ = -1.0;
    stats_ = MoviePlayerStats{};
}

void MoviePlayer::setVolumePercent(int percent) {
    volume_ = float(std::clamp(percent, 0, 100)) / 100.0f;
    if (audioActive_) SetAudioStreamVolume(stream_, volume_);
}

void MoviePlayer::skip() {
    if (!opened_ || finished_) return;
    finished_ = true;
    if (audioActive_) StopAudioStream(stream_);
}

// ---- audio ring --------------------------------------------------------------------------------------------------

size_t MoviePlayer::ringAvailable() const { return size_t(ringWritten_.load() - ringRead_.load()); }
size_t MoviePlayer::ringFree() const { return ringFrames_ - ringAvailable(); }

void MoviePlayer::pushAudio(const int16_t* stereo, size_t frames) {
    uint64_t w = ringWritten_.load();
    for (size_t i = 0; i < frames; ++i, ++w) {
        const size_t slot = size_t(w % ringFrames_) * 2;
        ring_[slot] = stereo[i * 2];
        ring_[slot + 1] = stereo[i * 2 + 1];
    }
    ringWritten_.store(w);
    stats_.audioFramesPushed += frames;
}

void MoviePlayer::pullAudio(int16_t* out, size_t frames) {
    uint64_t r = ringRead_.load();
    const uint64_t w = ringWritten_.load();
    size_t got = 0;
    for (; got < frames && r < w; ++got, ++r) {
        const size_t slot = size_t(r % ringFrames_) * 2;
        out[got * 2] = ring_[slot];
        out[got * 2 + 1] = ring_[slot + 1];
    }
    ringRead_.store(r);
    if (got < frames) {
        std::memset(out + got * 2, 0, (frames - got) * 2 * sizeof(int16_t));
        underruns_.fetch_add(1);
    }
    callbacks_.fetch_add(1);
    const uint32_t f32 = uint32_t(frames);
    if (f32 > maxCallbackFrames_.load()) maxCallbackFrames_.store(f32);
    lastCallbackFrames_.store(uint32_t(got));
    lastCallbackTime_.store(GetTime());
}

void MoviePlayer::audioCallback(void* buffer, unsigned int frames) {
    MoviePlayer* p = g_activePlayer.load();
    if (p == nullptr) {
        std::memset(buffer, 0, size_t(frames) * 2 * sizeof(int16_t));
        return;
    }
    if (!p->audioPrimed_.load()) {
        std::memset(buffer, 0, size_t(frames) * 2 * sizeof(int16_t));
        p->silentCallbacks_.fetch_add(1);
        return;
    }
    p->pullAudio(static_cast<int16_t*>(buffer), frames);
}

void MoviePlayer::drainPcm() {
    std::vector<int16_t>& pcm = decoder_.audioQueue();
    if (pcm.empty()) return;
    if (audioActive_) {
        const size_t frames = std::min(pcm.size() / 2, ringFree());
        if (frames == 0) return;
        pushAudio(pcm.data(), frames);
        pcm.erase(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(frames * 2));
    } else if (started_) {
        stats_.audioFramesPushed += pcm.size() / 2;  // no device: the wall clock is the master, PCM is discarded
        pcm.clear();
    }
}

double MoviePlayer::clock() const {
    if (!started_) return 0.0;
    if (audioActive_) {
        if (audioEndClock_ >= 0.0) return audioEndClock_ + (GetTime() - audioEndWall_);
        // Frames consumed by the device so far, interpolated with the wall clock since the last callback (bounded
        // by that callback's length) so the clock does not step in device-period quanta.
        const double consumed = double(ringRead_.load()) / double(sampleRate_);
        const double sinceCallback = GetTime() - lastCallbackTime_.load();
        const double quantum = double(lastCallbackFrames_.load()) / double(sampleRate_);
        return consumed + std::clamp(sinceCallback, 0.0, quantum);
    }
    return GetTime() - wallStart_;
}

// ---- decode ahead ------------------------------------------------------------------------------------------------

void MoviePlayer::fill(int budget) {
    const double now = clock();
    while (!decoderDone_ && budget > 0 && queue_.size() < kMaxQueuedFrames) {
        const bool audioAhead = !audioActive_ || decoder_.demuxEnded() ||
                                double(ringAvailable()) / double(sampleRate_) >= kAudioAheadSeconds;
        const bool videoAhead = !queue_.empty() && queue_.back().time >= now + kVideoAheadSeconds;
        if (audioAhead && videoAhead) break;
        if (audioActive_ && ringFree() < sampleRate_ / 10) break;  // ring nearly full: let it drain first
        MovieFrame f;
        if (!decoder_.nextFrame(f)) {
            decoderDone_ = true;
            break;
        }
        queue_.push_back(std::move(f));
        --budget;
        drainPcm();
    }
    drainPcm();
}

void MoviePlayer::present(double now) {
    // Newest frame whose time has come becomes current; older ones are dropped.
    int popped = 0;
    while (!queue_.empty() && (queue_.front().time <= now + 1e-6 || !haveCurrent_)) {
        current_ = std::move(queue_.front());
        queue_.pop_front();
        haveCurrent_ = true;
        currentDirty_ = true;
        ++popped;
    }
    if (popped > 1) stats_.framesDropped += uint64_t(popped - 1);
}

void MoviePlayer::uploadCurrent() {
    if (!currentDirty_) return;
    currentDirty_ = false;
    if (texture_.id == 0 || texture_.width != current_.width || texture_.height != current_.height) {
        if (texture_.id != 0) UnloadTexture(texture_);
        Image img{};
        img.data = current_.rgba.data();
        img.width = current_.width;
        img.height = current_.height;
        img.mipmaps = 1;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        texture_ = LoadTextureFromImage(img);
        SetTextureFilter(texture_, TEXTURE_FILTER_BILINEAR);
    } else {
        UpdateTexture(texture_, current_.rgba.data());
    }
    stats_.framesShown++;
}

void MoviePlayer::update() {
    if (!opened_ || finished_) return;
    if (!started_) {
        started_ = true;
        wallStart_ = GetTime();
        fill(kFramesAtStart);
        if (audioActive_) audioPrimed_.store(true);  // the stream is already running (silent pre-roll in open())
    } else {
        fill(kFramesPerUpdate);
    }
    const double now = clock();
    present(now);
    uploadCurrent();
    stats_.audioUnderruns = underruns_.load();
    stats_.audioFramesPlayed = ringRead_.load();
    stats_.audioCallbacks = callbacks_.load();
    stats_.maxCallbackFrames = maxCallbackFrames_.load();

    // Once every PCM frame has been played the clock continues on the wall clock (video tail after the audio).
    if (audioActive_ && audioEndClock_ < 0.0 && decoderDone_ && decoder_.audioQueue().empty() && ringAvailable() == 0) {
        audioEndClock_ = double(ringRead_.load()) / double(sampleRate_);
        audioEndWall_ = GetTime();
    }
    if (decoderDone_ && queue_.empty() && (!audioActive_ || audioEndClock_ >= 0.0)) {
        const double lastDue = haveCurrent_ ? current_.time + decoder_.frameDuration() : 0.0;
        if (clock() >= lastDue) {
            finished_ = true;
            if (audioActive_) StopAudioStream(stream_);
        }
    }
}

// ---- presentation ------------------------------------------------------------------------------------------------

Rectangle MoviePlayer::destinationRect(int screenW, int screenH) {
    // Largest 4:3 rectangle centred in the viewport (RUNTIME.md: 512x448 buffer displayed at 4:3, 1.25 px aspect).
    const float sw = float(screenW);
    const float sh = float(screenH);
    float w = sw;
    float h = sw * 3.0f / 4.0f;
    if (h > sh) {
        h = sh;
        w = sh * 4.0f / 3.0f;
    }
    return Rectangle{(sw - w) * 0.5f, (sh - h) * 0.5f, w, h};
}

void MoviePlayer::draw(int screenW, int screenH) const {
    if (texture_.id == 0) return;
    const Rectangle src{0.0f, 0.0f, float(texture_.width), float(texture_.height)};
    // PORT: own viewport / pixel ortho, independent of raylib's default matrices (which are stale after the window is
    // resized or after other passes); same push/pop pattern as the 2-D renderer.
    rlDrawRenderBatchActive();
    rlViewport(0, 0, screenW, screenH);
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(0.0, double(screenW), double(screenH), 0.0, -1.0, 1.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlSetBlendMode(RL_BLEND_ALPHA);
    DrawTexturePro(texture_, src, destinationRect(screenW, screenH), Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
}

}  // namespace baroque::video
