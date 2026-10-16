#pragma once
// Movie player struct 0x540930 (SCENES.md 0.9): FUN_00213730 alloc -> FUN_00213840 blocking play -> FUN_002137b0 free.
// The original blocks inside the scene's Update; the port runs the player as a per-tick state (begin/update/finished)
// so the host keeps rendering. This stub waits the movie's duration (or returns when START is pressed when the
// no-skip flag is 0). Real PSS playback replaces the stub behind the same interface.
#include <cstdint>
#include <string>
namespace baroque::video {
class MoviePlayer {
public:
    virtual ~MoviePlayer() = default;
    // FUN_00213730 + FUN_00213840 start: index into table 0x46dec0, noSkip (+8), volume 0..100 (BGM setting * 10)
    virtual void begin(int movieIndex, int noSkip, int volumePercent);
    // one engine tick of playback; `startPressed` is the pad START bit read by the skip callback FUN_00213990
    virtual void update(bool startPressed);
    virtual bool playing() const { return playing_; }
    virtual void end();                      // FUN_002137b0
    int movieIndex() const { return index_; }
    const char* fileName() const;            // "MOVIE_EN/<name>" per table 0x46dec0
    bool lastWasSkipped() const { return skipped_; }
    int ticksPlayed() const { return ticks_; }
    static int durationTicks(int movieIndex); // stub duration (30 ticks) -- real durations come from the PSS headers
protected:
    bool playing_ = false;
    bool skipped_ = false;
    int index_ = -1;
    int noSkip_ = 0;
    int volume_ = 0;
    int ticks_ = 0;
};
MoviePlayer& movie();
void setMoviePlayer(MoviePlayer* impl);   // nullptr restores the stub
// FUN_00119720 / FUN_002062c0 / FUN_0011dbc0 volume math: BGM setting (0..10) -> percent (clamped 0..100)
int movieVolumePercent(int bgmSetting);
}
