#pragma once
// PORT-only presentation settings (PRESENTATION_PLAN.md P4/P5), kept in <exe>/baroque_port.ini. They never touch the
// game's saved options block (0x3c bytes of the save, game/options.hpp) and never reach the game logic.
//   fps            = vsync | 60 | 120 | 144 | uncapped     presented frame-rate cap (logic always 29.97 Hz)
//   interpolation  = on | off                             draw between the last two ticks (off: the latest tick)
//   hud            = edge | classic                       16:9: HUD elements keep their distance to the nearest
//                                                         screen edge (edge) or stay in the 4:3 box (classic)
//   voice          = japanese | english                   voice archive (english = the Europe disc's dub, set up
//                                                         from its .iso into disc/SOUND_EN_DUB); PC SETTINGS switches it live
//   max_aspect     = 2.3333                               widest presented aspect (21:9); wider is pillarboxed
//   window_aspect  = 16:9                                 shape of the initial window (fitted to the monitor)
//   window         = windowed | borderless                borderless = borderless fullscreen window on the monitor
//                                                         (never topmost, never exclusive; Alt+Enter toggles + saves)
//   mute_unfocused = off | on                             PORT (window focus): no sound while another window is active
//   pause_unfocused = off | on                            game logic stops while another window is active (movies
//                                                         keep playing); implies mute
//   background_gamepad = off | on                         gamepad still drives the game while another window is active
// The PC SETTINGS page (game/ui/page_pc_settings.cpp) edits these live and saves them with savePortSettings().
#include <filesystem>
#include <string>
#include <string_view>

namespace baroque::game::present {

enum class FpsCap { VSync, Fps30, Fps60, Fps120, Fps144, Uncapped };

struct PortSettings {
    FpsCap fps = FpsCap::VSync;
    bool interpolation = true;
    bool classicHud = false;
    bool englishVoice = true;            // voice = english (the USA disc's language; Japanese needs the Japan disc)
    float maxAspect = 21.0f / 9.0f;
    float windowAspect = 16.0f / 9.0f;   // initial window shape (window_aspect; clamped to [4:3, max_aspect])
    bool borderless = false;             // window = borderless (borderless fullscreen window)
    bool seeThroughSilhouette = false;   // PORT: see_through = object | silhouette
};
// max_aspect = auto: no practical cap (the presented area follows the window up to 32:9)
constexpr float kAutoAspect = 32.0f / 9.0f;

PortSettings& portSettings();
// Reads `file` (missing keys keep their defaults); writes a commented default file when it does not exist and
// writeDefaults is set. Unknown keys / values are logged and ignored.
void loadPortSettings(const std::filesystem::path& file, bool writeDefaults);
// One "key = value" assignment (also the --port-set KEY=VALUE test option); false when not understood.
bool applyPortSetting(std::string_view key, std::string_view value);
int fpsTarget(const PortSettings& s);   // SetTargetFPS value (0 = none)
bool wantsVsync(const PortSettings& s);
// Settings page API: rewrite the file loadPortSettings() read with the current values (commented format; false when
// no file was loaded or it cannot be written) and apply fps cap / vsync / window mode to the live window (no-op while
// the window is hidden, i.e. in --shot runs). fps, interpolation, hud and max_aspect are read live by the presenter.
bool savePortSettings();
void applyPortDisplay();
// PORT (window focus): window = windowed / borderless on the live window, also on a hidden one (the --window-test path;
// applyPortDisplay() calls it for visible windows). Borderless = undecorated window covering the current monitor,
// NOT raylib's ToggleBorderlessWindowed (that one sets GLFW_FLOATING = HWND_TOPMOST, so other applications stayed
// behind the game after Alt+Tab / Win). Leaving it restores the window rectangle from before. Any TOPMOST flag is
// cleared. refitBorderless(): a borderless window whose rectangle no longer matches its monitor (resolution change
// while in the background) is fitted again; no-op when windowed.
void applyWindowMode();
void refitBorderless();
bool borderlessActive();

}  // namespace baroque::game::present
