// Movie service over video::MoviePlayer (see movie_service.hpp).
#include "game/movie_service.hpp"
#include "audio/sfs.hpp"

#include "raylib.h"

#include "game/pad.hpp"
#include "game/present/present_config.hpp"
#include "game/ui/mouse_menu.hpp"
#include "platform/input.hpp"
#include "platform/platform.hpp"
#include "video/movie_player.hpp"

#include <memory>
#include <string>

namespace baroque::game {

namespace {
std::unique_ptr<video::MoviePlayer> gPlayer;
int gSkippable = 1;   // struct +8
// table 0x46dec0 (SCENES.md 0.9); EV19A/EV19B are not on the disc
const char* const kMovies[] = {"OPENING.PSS", "PLAYABLE.PSS", "E01.PSS", "E51.PSS", "E52.PSS", "E53.PSS", "E03.PSS",
                               "E04.PSS", "E05.PSS", "E06.PSS", "E62.PSS", "E71.PSS", "E72.PSS", "E08.PSS", "E82.PSS",
                               "E09.PSS", "E10.PSS", "E83.PSS", "E11.PSS", "E12.PSS", "E13.PSS", "E14.PSS", "E15.PSS",
                               "OPENING.PSS", "PLAYABLE.PSS", "EV18.PSS", "EV15B.PSS", "EV19A.PSS", "EV19B.PSS"};
}  // namespace

const char* movieFileName(int index) {
    if (index < 0 || index >= int(sizeof(kMovies) / sizeof(kMovies[0]))) return "";
    return kMovies[index];
}

// FUN_00119720: iVar3 = bgm*10; clamp 100 / 0
int movieVolumePercent(int bgmSetting) {
    int v = bgmSetting * 10;
    if (100 < v) v = 100;
    if (v < 0) v = 0;
    return v;
}

bool movieBegin(int index, int skippable, int volumePercent) {
    ui::mouse::clearMovieClick();          // PORT: a click from the previous screen must not skip the new movie
    movieEnd();
    gSkippable = skippable;
    // cdrom0:\MOVIE_EN\%s;1 -> <work>/disc/MOVIE_EN (the PSS files are ISO9660 files, not in DATA.BIN)
    auto path = platform::assetRoot().parent_path() / "disc" / "MOVIE_EN" / movieFileName(index);
    // PORT (voice language): MOVIE_EN_DUB holds the USA movies whose audio track differs from the Europe disc's,
    // remuxed with the English audio (platform/data_setup.cpp); the others are identical and stay in MOVIE_EN.
    // Used only with the Europe dub over an undubbed image; the Japanese voices keep the USA movies (the Japan disc's
    // movies are cut differently, their audio does not line up)
    if (audio::europeVoicesActive()) {
        const auto dub = platform::assetRoot().parent_path() / "disc" / "MOVIE_EN_DUB" / movieFileName(index);
        std::error_code ec;
        if (std::filesystem::exists(dub, ec)) path = dub;
    }
    gPlayer = std::make_unique<video::MoviePlayer>();
    std::string err;
    if (!gPlayer->open(path, &err)) {
        platform::log("movie: cannot open " + path.string() + ": " + err);
        gPlayer.reset();
        return false;
    }
    gPlayer->setVolumePercent(volumePercent);
    return true;
}

bool movieUpdate() {
    if (!gPlayer) return false;
    // FUN_00213990 skip callback: polls the pad itself (no engine tick runs during the blocking play) and stops the
    // movie on START (0x800); FUN_00213840 removes that callback (FUN_00216300) when +8 == 0
    pad().update(platform::pollInput());
    // PORT: a left click also skips (same as START; movies skip on START in the original)
    // PORT: skippable 2 (front-end movies) also takes Cross (Space); story movies keep START / click
    const uint32_t skipBits = gSkippable == 2 ? (0x800u | 0x40u) : 0x800u;
    if (gSkippable != 0 && ((pad().port[0].pressed & skipBits) != 0 || ui::mouse::consumeMovieClick())) gPlayer->skip();
    gPlayer->update();
    return !gPlayer->finished();
}

void movieEnd() { gPlayer.reset(); }

const video::MoviePlayer* movieActive() { return gPlayer.get(); }

}  // namespace baroque::game
