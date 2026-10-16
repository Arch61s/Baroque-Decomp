// PORT-only presentation settings (see present_config.hpp).
#include "game/present/present_config.hpp"

#include "platform/platform.hpp"

#include "raylib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace baroque::game::present {

namespace {

PortSettings gSettings;

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* kDefaultFile =
    "; Baroque native port - presentation settings (port only; the game's own options are in the save file)\n"
    "; fps: vsync | 30 | 60 | 120 | 144 | uncapped  (the game logic always runs at 29.97 ticks per second)\n"
    "fps = vsync\n"
    "; interpolation: on | off  (on: frames between two ticks are drawn in between; off: the latest tick)\n"
    "interpolation = on\n"
    "; hud: edge | classic  (wide windows: edge keeps each HUD element at its original distance from the nearest\n"
    ";      screen edge, unstretched; classic keeps the whole HUD in the centred 4:3 box)\n"
    "hud = edge\n"
    "; max_aspect: widest presented aspect ratio (width / height); wider windows get black side bars. 2.3333 = 21:9\n"
    "max_aspect = 2.3333\n"
    "; window_aspect: shape of the window at start (fitted to the monitor), e.g. 4:3, 16:10, 16:9\n"
    "window_aspect = 16:9\n";

std::filesystem::path gFile;   // PORT: the file loadPortSettings() read / created (savePortSettings target)

// "16:9" for the common ratios, else the number (max_aspect: "auto" for kAutoAspect)
std::string aspectText(float a, bool allowAuto) {
    if (allowAuto && a >= kAutoAspect - 0.01f) return "auto";
    static const struct { int w, h; } kRatios[] = {{4, 3}, {16, 10}, {16, 9}, {21, 9}, {32, 9}};
    for (const auto& r : kRatios)
        if (std::fabs(a - float(r.w) / float(r.h)) < 0.002f) return std::to_string(r.w) + ":" + std::to_string(r.h);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", a);
    return buf;
}

// the commented file with the values of `s` (kDefaultFile's layout)
std::string formatPortSettings(const PortSettings& s) {
    static const char* const kFps[] = {"vsync", "30", "60", "120", "144", "uncapped"};
    std::string t = kDefaultFile;
    auto set = [&t](const char* key, const std::string& value) {
        const std::string head = std::string("\n") + key + " = ";
        const size_t p = t.find(head);
        if (p == std::string::npos) return;
        const size_t v = p + head.size();
        t.replace(v, t.find('\n', v) - v, value);
    };
    set("fps", kFps[static_cast<int>(s.fps)]);
    set("interpolation", s.interpolation ? "on" : "off");
    set("hud", s.classicHud ? "classic" : "edge");
    set("max_aspect", aspectText(s.maxAspect, true));
    set("window_aspect", aspectText(s.windowAspect, false));
    t += "; window: windowed | borderless  (borderless = borderless fullscreen window on the current monitor)\n";
    t += std::string("window = ") + (s.borderless ? "borderless" : "windowed") + "\n";
    t += "; voice: japanese | english  (english needs the Europe disc image set up next to the game; PC SETTINGS switches it live)\n";
    t += std::string("voice = ") + (s.englishVoice ? "english" : "japanese") + "\n";
    // PORT: see-through of what hides the player
    t += "; see_through: object | silhouette  (keyboard + mouse camera; object: a wall piece, pillar or character between\n";
    t += ";      the camera and the player turns see-through as a whole; silhouette: only where it covers the player)\n";
    t += std::string("see_through = ") + (s.seeThroughSilhouette ? "silhouette" : "object") + "\n";
    return t;
}

}  // namespace

PortSettings& portSettings() { return gSettings; }

bool applyPortSetting(std::string_view keyIn, std::string_view valueIn) {
    const std::string key = lower(trim(keyIn));
    const std::string value = lower(trim(valueIn));
    PortSettings& s = gSettings;
    if (key == "fps") {
        if (value == "vsync") s.fps = FpsCap::VSync;
        else if (value == "30") s.fps = FpsCap::Fps30;
        else if (value == "60") s.fps = FpsCap::Fps60;
        else if (value == "120") s.fps = FpsCap::Fps120;
        else if (value == "144") s.fps = FpsCap::Fps144;
        else if (value == "uncapped" || value == "0" || value == "off") s.fps = FpsCap::Uncapped;
        else return false;
        return true;
    }
    if (key == "interpolation") {
        if (value == "on" || value == "1" || value == "true") s.interpolation = true;
        else if (value == "off" || value == "0" || value == "false") s.interpolation = false;
        else return false;
        return true;
    }
    if (key == "hud") {
        if (value == "edge") s.classicHud = false;
        else if (value == "classic" || value == "4:3") s.classicHud = true;
        else return false;
        return true;
    }
    if (key == "voice") {
        if (value == "japanese" || value == "jp" || value == "undub") s.englishVoice = false;
        else if (value == "english" || value == "en" || value == "dub") s.englishVoice = true;
        else return false;
        return true;
    }
    if (key == "window") {
        if (value == "windowed" || value == "window") s.borderless = false;
        else if (value == "borderless" || value == "fullscreen") s.borderless = true;
        else return false;
        return true;
    }
    if (key == "mute_unfocused" || key == "pause_unfocused" || key == "background_gamepad") {
        // PORT: removed options; older files may still carry them. The game always pauses and mutes
        // while another window is active, with every input blocked.
        return true;
    }
    if (key == "see_through") {   // PORT
        if (value == "object") s.seeThroughSilhouette = false;
        else if (value == "silhouette") s.seeThroughSilhouette = true;
        else return false;
        return true;
    }
    if (key == "max_aspect" && value == "auto") {
        s.maxAspect = kAutoAspect;
        return true;
    }
    if (key == "max_aspect" || key == "window_aspect") {
        float a = 0.0f;
        const size_t colon = value.find(':');
        if (colon != std::string::npos) {
            const float w = std::strtof(value.substr(0, colon).c_str(), nullptr);
            const float h = std::strtof(value.substr(colon + 1).c_str(), nullptr);
            if (h > 0.0f) a = w / h;
        } else {
            a = std::strtof(value.c_str(), nullptr);
        }
        if (!(a > 0.0f)) return false;
        if (key == "max_aspect") s.maxAspect = std::max(a, 4.0f / 3.0f);
        else s.windowAspect = std::max(a, 4.0f / 3.0f);
        return true;
    }
    return false;
}

void loadPortSettings(const std::filesystem::path& file, bool writeDefaults) {
    gFile = file;
    std::ifstream in(file);
    if (!in) {
        if (writeDefaults) {
            std::ofstream out(file);
            if (out) out << kDefaultFile;
        }
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        const size_t cmt = line.find_first_of(";#");
        if (cmt != std::string::npos) line.resize(cmt);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            if (!trim(line).empty()) platform::log("baroque_port.ini: ignored line '" + trim(line) + "'");
            continue;
        }
        const std::string_view sv(line);
        if (!applyPortSetting(sv.substr(0, eq), sv.substr(eq + 1)))
            platform::log("baroque_port.ini: ignored '" + trim(line) + "'");
    }
}

int fpsTarget(const PortSettings& s) {
    switch (s.fps) {
    case FpsCap::Fps30: return 30;   // the original's frame rate (29.97 ticks; interpolation covers the difference)
    case FpsCap::Fps60: return 60;
    case FpsCap::Fps120: return 120;
    case FpsCap::Fps144: return 144;
    default: return 0;
    }
}

bool wantsVsync(const PortSettings& s) { return s.fps == FpsCap::VSync; }

bool savePortSettings() {
    if (gFile.empty()) return false;
    std::ofstream out(gFile, std::ios::trunc | std::ios::binary);
    if (!out) {
        platform::log("baroque_port.ini: cannot write " + gFile.string());
        return false;
    }
    out << formatPortSettings(gSettings);
    return static_cast<bool>(out);
}

void applyPortDisplay() {
    if (!IsWindowReady() || IsWindowState(FLAG_WINDOW_HIDDEN)) return;   // --shot / --shot-window: keep the capture setup
    const PortSettings& s = gSettings;
    if (wantsVsync(s)) {
        if (!IsWindowState(FLAG_VSYNC_HINT)) SetWindowState(FLAG_VSYNC_HINT);       // glfwSwapInterval(1)
    } else if (IsWindowState(FLAG_VSYNC_HINT)) {
        ClearWindowState(FLAG_VSYNC_HINT);                                           // glfwSwapInterval(0)
    }
    SetTargetFPS(fpsTarget(s));
    applyWindowMode();   // PORT (window focus): not ToggleBorderlessWindowed (TOPMOST), see present_config.hpp
}

namespace {
bool gBorderlessOn = false;                                // our borderless state (raylib's flag stays clear)
int gSavedX = 0, gSavedY = 0, gSavedW = 0, gSavedH = 0;   // windowed client rectangle before borderless

void fitToMonitor(int mon) {
    const Vector2 mp = GetMonitorPosition(mon);
    const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    if (mw <= 0 || mh <= 0) return;
    SetWindowPosition(int(mp.x), int(mp.y));
    SetWindowSize(mw, mh);
}
}  // namespace

bool borderlessActive() { return gBorderlessOn; }

void applyWindowMode() {
    if (!IsWindowReady()) return;
    // a raylib borderless window (ToggleBorderlessWindowed) is left first: it is TOPMOST and keeps its own rectangle
    if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();
    if (IsWindowState(FLAG_WINDOW_TOPMOST)) ClearWindowState(FLAG_WINDOW_TOPMOST);   // never above other applications
    const bool want = gSettings.borderless;
    if (want == gBorderlessOn) {
        refitBorderless();
        return;
    }
    if (IsWindowMinimized()) return;   // applied by the next call (Alt+Enter and the page need the focus anyway)
    if (want) {
        if (IsWindowMaximized()) RestoreWindow();
        const Vector2 p = GetWindowPosition();
        gSavedX = int(p.x);
        gSavedY = int(p.y);
        gSavedW = GetScreenWidth();
        gSavedH = GetScreenHeight();
        const int mon = GetCurrentMonitor();
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        fitToMonitor(mon);
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        if (gSavedW > 0 && gSavedH > 0) {
            SetWindowSize(gSavedW, gSavedH);   // size first, then position (raylib's note in ToggleBorderlessWindowed)
            SetWindowPosition(gSavedX, gSavedY);
        }
    }
    gBorderlessOn = want;
    platform::log(std::string("window: ") + (want ? "borderless " : "windowed ") + std::to_string(GetScreenWidth()) + "x" +
                  std::to_string(GetScreenHeight()) + (IsWindowState(FLAG_WINDOW_TOPMOST) ? ", TOPMOST" : ", not topmost") +
                  (IsWindowState(FLAG_WINDOW_UNDECORATED) ? ", undecorated" : ", decorated"));
}

void refitBorderless() {
    if (!gBorderlessOn || !IsWindowReady() || IsWindowMinimized()) return;
    const int mon = GetCurrentMonitor();
    const Vector2 mp = GetMonitorPosition(mon), wp = GetWindowPosition();
    if (int(wp.x) != int(mp.x) || int(wp.y) != int(mp.y) || GetScreenWidth() != GetMonitorWidth(mon) ||
        GetScreenHeight() != GetMonitorHeight(mon))
        fitToMonitor(mon);
}

}  // namespace baroque::game::present
