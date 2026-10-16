#include "video/movie_stub.hpp"
#include "platform/platform.hpp"
namespace baroque::video {
namespace {
const char* const kMovieNames[29] = {"OPENING.PSS", "PLAYABLE.PSS", "E01.PSS", "E51.PSS", "E52.PSS", "E53.PSS", "E03.PSS",
    "E04.PSS", "E05.PSS", "E06.PSS", "E62.PSS", "E71.PSS", "E72.PSS", "E08.PSS", "E82.PSS", "E09.PSS", "E10.PSS", "E83.PSS",
    "E11.PSS", "E12.PSS", "E13.PSS", "E14.PSS", "E15.PSS", "OPENING.PSS", "PLAYABLE.PSS", "EV18.PSS", "EV15B.PSS",
    "EV19A.PSS", "EV19B.PSS"};   // table 0x46dec0
MoviePlayer gStub;
MoviePlayer* gImpl = &gStub;
}
MoviePlayer& movie() { return *gImpl; }
void setMoviePlayer(MoviePlayer* impl) { gImpl = impl ? impl : &gStub; }
int movieVolumePercent(int bgmSetting) {
    int v = bgmSetting * 10;
    if (v > 100) v = 100;
    if (v < 0) v = 0;
    return v;
}
int MoviePlayer::durationTicks(int) { return 30; }   // stub: one second
void MoviePlayer::begin(int movieIndex, int noSkip, int volumePercent) {
    index_ = movieIndex; noSkip_ = noSkip; volume_ = volumePercent; ticks_ = 0; skipped_ = false; playing_ = true;
    platform::log(std::string("[movie-stub] play ") + fileName() + " noSkip=" + std::to_string(noSkip));
}
void MoviePlayer::update(bool startPressed) {
    if (!playing_) return;
    ++ticks_;
    if (noSkip_ == 0 && startPressed) { skipped_ = true; playing_ = false; return; }   // FUN_00213990 skip check
    if (ticks_ >= durationTicks(index_)) playing_ = false;
}
void MoviePlayer::end() { playing_ = false; }
const char* MoviePlayer::fileName() const {
    return (index_ >= 0 && index_ < 29) ? kMovieNames[index_] : "?";
}
}
