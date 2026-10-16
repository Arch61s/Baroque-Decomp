#include "platform/settings.hpp"
#include "platform/platform.hpp"
#include "raylib.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
namespace baroque::platform {
std::filesystem::path settingsPath() {
    return std::filesystem::path(GetApplicationDirectory()) / "baroque_settings.ini";
}
Settings sanitize(Settings s) {
    if (s.height != 1080 && s.height != 1440 && s.height != 2160) s.height = 1080;
    if (s.fpsCap != 30 && s.fpsCap != 60 && s.fpsCap != 120) s.fpsCap = 60;
    return s;
}
Settings loadSettings() {
    Settings s;
    std::ifstream in(settingsPath());
    if (!in) return s;
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "height") s.height = std::atoi(val.c_str());
        else if (key == "aspect") s.widescreen = (val == "16:9");
        else if (key == "fps") s.fpsCap = std::atoi(val.c_str());
    }
    return sanitize(s);
}
bool saveSettings(const Settings& sIn) {
    const Settings s = sanitize(sIn);
    std::ofstream out(settingsPath(), std::ios::trunc);
    if (!out) { log("settings: cannot write " + settingsPath().string()); return false; }
    out << "height=" << s.height << "\naspect=" << (s.widescreen ? "16:9" : "4:3") << "\nfps=" << s.fpsCap << "\n";
    return true;
}
}
