#pragma once
// PORT (packaging): process start-up of Baroque-Native (not part of the original game).
//   - console: the release EXE has no console window; developer/test options (any "--" argument) attach to the
//     parent console when the std handles are not already redirected, --console opens one (platform::attachConsole)
//   - log file <exe>/baroque.log (previous run kept as baroque.old.log), raylib messages and crashes included
//   - game data: when <exe>/assets or <exe>/disc is missing, extracts it from the player's disc image
//     (an .iso next to the EXE, an .iso dropped on the EXE, or --setup-data PATH) with a progress window
namespace baroque::platform {
// Returns false when the process should exit now with *exitCode (data missing, setup failed, --setup-data done).
bool initApp(int argc, char** argv, int* exitCode);
// True when the EXE was started without developer/test options (errors are shown in message boxes).
bool playerRun();
}
