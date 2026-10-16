#pragma once
#include <filesystem>
#include <string>
namespace baroque::platform {
// Directory of the running EXE (wide-path safe; GetModuleFileNameW on Windows).
std::filesystem::path exeDir();
// <exe>/assets when it holds DATA/, else the developer tree BAROQUE_ASSET_ROOT (work/assets) unless the build was
// configured with BAROQUE_DEV_ASSET_FALLBACK=OFF (release packages: <exe>/assets only).
std::filesystem::path assetRoot();
// Original disc files that are not inside DATA.BIN (MOVIE_EN/*.PSS, SOUND_EN/BRQSOUND.SFS):
// assetRoot().parent_path() / "disc", i.e. <exe>/disc in a package and work/disc in the developer tree.
std::filesystem::path discRoot();
// One line to stderr (when the process has one) and to the log file opened by openLogFile().
void log(const std::string& message);
// PORT (packaging): starts <dir>/baroque.log for this run; the previous run's log is kept as baroque.old.log.
// Falls back to %LOCALAPPDATA%/Baroque when <dir> is not writable. Returns the path in use (empty on failure).
std::filesystem::path openLogFile(const std::filesystem::path& dir);
std::filesystem::path logFilePath();
// PORT (packaging): the release EXE is a GUI-subsystem program (no console window). When it has no usable std
// handles (started from Explorer, or from a console that does not redirect them), this attaches stdout/stderr to the
// parent process's console; allocNew = open a new console window when there is no parent console (--console).
// Started with redirected handles (pipes / files, e.g. Git Bash or `> log 2>&1`) the inherited handles are kept.
void attachConsole(bool allocNew);
// PORT (packaging): logs `message` and, when `dialog`, shows it in a message box (player-facing errors).
void showError(const std::string& title, const std::string& message, bool dialog);
// PORT (packaging): unhandled SEH exceptions / std::terminate / abort write a line to the log before the process ends.
void installCrashLogging();
}
