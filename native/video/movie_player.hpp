#pragma once
// baroque::video::MoviePlayer - presents a MOVIE_EN/*.PSS cutscene through raylib with audio-master A/V sync.
//
// Original player (work/reports/SCENES.md §0.9, struct 0x540930): FUN_00213730 allocates four 0x40000-byte buffers,
// FUN_00213840 plays the movie *synchronously* (the caller's Update does not return until the movie ends), and
// FUN_002137b0 frees the buffers. Skip callback FUN_00213990 polls the pad and stops the movie when START (0x800)
// is pressed unless the "no-skip" flag (+8) is set. Volume = BGM option (0..10) * 10, clamped 0..100.
// The native port cannot block the frame loop, so the scene calls update() once per display frame and leaves the
// frame finished() becomes true (the scene's own frame counter does not advance meanwhile, which matches the
// original's blocking behaviour). The scene translates START into skip().
//
// Sync model: audio is the master clock. Decoded stereo PCM goes into a ring buffer that a raylib AudioStream
// callback drains on the audio thread; the number of frames actually consumed defines the movie clock. Video frames
// are due at MovieDecoder times (exactly one 29.97 fps period apart) and the newest frame whose time <= clock is
// shown; when the ring runs dry the clock stalls (video waits for audio). Without an audio device (headless
// checks) the clock is the wall clock from the first update().
#include "video/movie_decoder.hpp"

#include "raylib.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace baroque::video {

struct MoviePlayerStats {
    uint64_t framesShown = 0;     // texture updates
    uint64_t framesDropped = 0;   // decoded frames skipped because a newer one was already due
    uint64_t audioUnderruns = 0;  // audio callback requests that found an empty ring (frames of silence inserted)
    uint64_t audioFramesPushed = 0;
    uint64_t audioFramesPlayed = 0;
    uint64_t audioCallbacks = 0;
    uint32_t maxCallbackFrames = 0;  // largest device request seen (the clock's quantum before interpolation)
};

class MoviePlayer {
public:
    MoviePlayer();
    ~MoviePlayer();
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;

    // Opens the movie and starts playback on the first update(). volumePercent follows the original 0..100 scale.
    bool open(const std::filesystem::path& path, std::string* error = nullptr);
    void close();
    bool isOpen() const { return opened_; }

    // Call once per display frame (any refresh rate). Decodes ahead, feeds audio, picks the frame for the clock.
    void update();

    // RGBA texture of the current frame (id 0 until the first frame is decoded). 512x448 for the disc movies.
    const Texture2D& texture() const { return texture_; }
    bool hasFrame() const { return texture_.id != 0; }
    int width() const { return decoder_.width(); }
    int height() const { return decoder_.height(); }

    bool finished() const { return finished_; }
    void skip();  // START: stops audio and marks the movie finished (FUN_00213990 behaviour)

    double clock() const;        // movie clock in seconds
    int currentFrameIndex() const { return current_.index; }
    double currentFrameTime() const { return current_.time; }
    bool audioDeviceUsed() const { return audioActive_; }
    const MoviePlayerStats& stats() const { return stats_; }
    const MovieDecoder& decoder() const { return decoder_; }

    // Volume as the original's 0..100 percentage (BGM option * 10). Applied to the audio stream immediately.
    void setVolumePercent(int percent);
    // orig FUN_00213840 volume word: ((v*64-v)*256/100)*256 | 1 for v in 0..100 (kept for reference/tests).
    static uint32_t originalVolumeWord(int percent);

    // Draws the current frame letterboxed as the original 4:3 raster (512x448 -> 640x448 pixel aspect, see
    // work/reports/RUNTIME.md §1.3) into a screenW x screenH viewport; black when no frame yet.
    void draw(int screenW, int screenH) const;
    static Rectangle destinationRect(int screenW, int screenH);

private:
    static void audioCallback(void* buffer, unsigned int frames);
    void pullAudio(int16_t* out, size_t frames);
    void fill(int budget);
    void drainPcm();
    void present(double now);
    void uploadCurrent();
    size_t ringAvailable() const;
    size_t ringFree() const;
    void pushAudio(const int16_t* stereo, size_t frames);

    MovieDecoder decoder_;
    std::deque<MovieFrame> queue_;  // decoded frames not yet due
    MovieFrame current_;
    bool haveCurrent_ = false;
    bool currentDirty_ = false;
    bool decoderDone_ = false;
    bool opened_ = false;
    bool finished_ = false;
    bool started_ = false;

    Texture2D texture_{};

    // Audio ring (interleaved stereo s16), single producer (update) / single consumer (audio thread).
    std::vector<int16_t> ring_;
    size_t ringFrames_ = 0;
    std::atomic<uint64_t> ringWritten_{0};
    std::atomic<uint64_t> ringRead_{0};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> callbacks_{0};
    std::atomic<uint64_t> silentCallbacks_{0};  // pre-roll callbacks served with silence before the first update()
    std::atomic<uint32_t> maxCallbackFrames_{0};
    std::atomic<uint32_t> lastCallbackFrames_{0};
    std::atomic<double> lastCallbackTime_{0.0};  // GetTime() when the audio thread last pulled from the ring
    AudioStream stream_{};
    bool audioActive_ = false;
    std::atomic<bool> audioPrimed_{false};
    uint32_t sampleRate_ = 48000;
    float volume_ = 1.0f;

    // Wall-clock fallback when there is no audio device, and continuation after the audio track ends.
    double wallStart_ = 0.0;
    double audioEndClock_ = -1.0;
    double audioEndWall_ = 0.0;

    MoviePlayerStats stats_;
};

}  // namespace baroque::video
