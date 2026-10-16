#pragma once
// Port settings stored beside the EXE (native/DESIGN.md principle 6). Not part of the original game.
#include <filesystem>
#include <string>
namespace baroque::platform {
struct Settings {
    int height = 1080;        // internal render target height: 1080, 1440 or 2160
    bool widescreen = true;   // true = 16:9 (4:3 content pillarboxed), false = 4:3
    int fpsCap = 60;          // display frames per second: 30, 60 or 120 (logic is always 30 ticks/s)
    int width() const { return widescreen ? height * 16 / 9 : height * 4 / 3; }
};
std::filesystem::path settingsPath();          // <exe dir>/baroque_settings.ini
Settings loadSettings();                       // defaults when the file is missing or malformed
bool saveSettings(const Settings& s);
Settings sanitize(Settings s);                 // clamps every field to the supported set
}
