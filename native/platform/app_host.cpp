// PORT (packaging): start-up of the player-facing EXE (see app_host.hpp).
#include "platform/app_host.hpp"
#include "audio/sfs.hpp"            // PORT (voice language): which voice set is still missing
#include "platform/data_setup.hpp"
#include "platform/platform.hpp"
#include "raylib.h"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
namespace baroque::platform {
namespace {
namespace fs = std::filesystem;
bool gPlayerRun = true;

void raylibLog(int level, const char* text, va_list args) {
    char msg[1024];
    std::vsnprintf(msg, sizeof msg, text, args);
    const char* tag = level >= LOG_FATAL ? "FATAL" : level >= LOG_ERROR ? "ERROR" : level >= LOG_WARNING ? "WARNING"
                    : level >= LOG_INFO ? "INFO" : "DEBUG";
    log(std::string("raylib ") + tag + ": " + msg);
}

std::string missingDataText() {
    return "Baroque needs the game data from your own copy of Baroque (USA, PlayStation 2).\n\n"
           "Make an .iso image of your disc and put it in this folder:\n" + exeDir().string() +
           "\n\nThen start Baroque.exe again. On the first start the game copies what it needs from the image "
           "(about 2.5 GB, once); the .iso can be deleted afterwards.\n\n"
           "You can also drag the .iso file onto Baroque.exe.";
}

// Extraction on a worker thread; player runs watch it in a small window, developer runs log the progress.
// voices: 0 = the game data, 1 = the English voices from the Europe disc (extractEnglishDub), 2 = the Japanese voices
// from the Japan disc (extractJapaneseVoices).
bool runSetup(const fs::path& iso, const DataStatus& status, bool window, std::string* error, int voices = 0) {
    const bool dub = voices != 0;
    const char* lang = voices == 2 ? "Japanese" : "English";
    DataSetupProgress prog;
    bool ok = false;
    std::atomic<bool> done{false};
    if (dub) log(std::string("voice setup: ") + lang + " voices from " + iso.string());
    else log("data setup: extracting from " + iso.string() + (status.assets ? "" : " [game data]") +
             (status.disc ? "" : " [movies + sound]"));
    std::thread worker([&] {
        ok = voices == 2 ? extractJapaneseVoices(iso, prog, error) : dub ? extractEnglishDub(iso, prog, error) : extractGameData(iso, exeDir(), !status.assets, !status.disc, prog, error);
        done = true;
    });
    if (window) {
        SetConfigFlags(FLAG_VSYNC_HINT);
        InitWindow(760, 220, "Baroque - first start");
        SetTargetFPS(30);
        const std::string name = iso.filename().string();
        while (!done.load()) {
            if (WindowShouldClose()) prog.cancel = true;
            const uint64_t total = prog.totalBytes.load(), cur = prog.doneBytes.load();
            const float frac = total > 0 ? float(double(cur) / double(total)) : 0.0f;
            const int phase = prog.phase.load();
            const std::string copying = std::string("Copying the ") + lang + " voices...";
            const char* what = phase == 0 ? "Reading the disc image..." : phase == 1 ? "Copying game data..."
                             : dub ? copying.c_str() : "Copying movies and sound...";
            BeginDrawing();
            ClearBackground(Color{18, 16, 22, 255});
            DrawText("BAROQUE", 30, 22, 30, Color{214, 206, 190, 255});
            DrawText(voices == 2 ? "Preparing the Japanese voices from your Japan disc image (only once)."
                     : dub ? "Preparing the English voices from your Europe disc image (only once)."
                           : "Preparing the game data from your disc image (only on the first start).", 30, 66, 18, RAYWHITE);
            DrawText(name.c_str(), 30, 92, 16, GRAY);
            DrawRectangle(30, 128, 700, 22, Color{50, 46, 58, 255});
            DrawRectangle(30, 128, int(700.0f * frac), 22, Color{170, 150, 120, 255});
            DrawText(TextFormat("%s  %d%%  (%llu / %llu MB)", what, int(frac * 100.0f), (unsigned long long)(cur >> 20),
                                (unsigned long long)(total >> 20)), 30, 160, 16, RAYWHITE);
            DrawText(prog.cancel.load() ? "Cancelling..." : "Close this window to cancel.", 30, 188, 14, GRAY);
            EndDrawing();
        }
        worker.join();
        CloseWindow();
    } else {
        int lastPct = -10;
        while (!done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const uint64_t total = prog.totalBytes.load();
            const int pct = total > 0 ? int(prog.doneBytes.load() * 100 / total) : 0;
            if (pct >= lastPct + 10) { log("data setup: " + std::to_string(pct) + "%"); lastPct = pct; }
        }
        worker.join();
    }
    return ok;
}
}  // namespace

bool playerRun() { return gPlayerRun; }

bool initApp(int argc, char** argv, int* exitCode) {
    *exitCode = 0;
    bool consoleFlag = false;
    const char* setupIso = nullptr;   // --setup-data PATH (extract and exit)
    const char* droppedIso = nullptr; // an .iso dropped on the EXE (first positional argument)
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--", 2) == 0) gPlayerRun = false;
        if (std::strcmp(argv[i], "--console") == 0) consoleFlag = true;
        if (std::strcmp(argv[i], "--setup-data") == 0 && i + 1 < argc) setupIso = argv[i + 1];
        const size_t n = std::strlen(argv[i]);
        if (i == 1 && n > 4 && _stricmp(argv[i] + n - 4, ".iso") == 0) droppedIso = argv[i];
    }
    if (!gPlayerRun) attachConsole(consoleFlag);
    installCrashLogging();
    const fs::path logPath = openLogFile(exeDir());
    SetTraceLogCallback(raylibLog);
    std::string args;
    for (int i = 1; i < argc; ++i) args += std::string(" ") + argv[i];
    log("Baroque native port " BAROQUE_VERSION_STRING ", exe " + exeDir().string() + (args.empty() ? "" : ", args:" + args));
    log("asset root " + assetRoot().string() + ", disc " + discRoot().string() +
        (logPath.empty() ? ", no log file" : ", log " + logPath.string()));

    // PORT (voice language): the USA disc is required and gives the game data plus its voices (English on a retail
    // disc, Japanese on an undubbed image). Once that is in place, a second disc image (passed, dropped or next to the
    // EXE) adds the other language: the Japan disc the Japanese voices, the Europe disc the English dub. The USA image
    // is preferred for the game data whatever the file sizes.
    const auto images = findDiscImages(exeDir());
    fs::path given = setupIso != nullptr ? fs::path(setupIso) : droppedIso != nullptr ? fs::path(droppedIso) : fs::path();
    const int givenKind = given.empty() ? 0 : identifyDisc(given);
    const bool givenIsVoiceDisc = givenKind == 2 || givenKind == 3;
    auto setupVoices = [&]() -> bool {
        if (!gameDataStatus().complete()) return true;
        bool ok = true;
        for (const int kind : {3, 2}) {
            const bool english = kind == 2;
            if (audio::voicesAvailable(english)) continue;
            fs::path src = givenKind == kind ? given : fs::path();
            for (size_t i = 0; src.empty() && i < images.size(); ++i)
                if (identifyDisc(images[i]) == kind) src = images[i];
            if (src.empty()) continue;
            std::string err;
            if (!runSetup(src, gameDataStatus(), gPlayerRun, &err, english ? 1 : 2)) {
                log("voice setup: failed: " + err);
                const std::string lang = english ? "English" : "Japanese", other = english ? "Japanese" : "English";
                if (err != "cancelled")
                    showError("Baroque - " + lang + " voices", "Could not prepare the " + lang + " voices from\n" +
                              src.string() + "\n\n" + err + "\n\nThe game runs with the " + other + " voices.", gPlayerRun);
                ok = false;
            }
        }
        return ok;
    };
    const DataStatus status = gameDataStatus();
    if (status.complete() && setupIso == nullptr) {
        setupVoices();
        return true;
    }
    if (status.complete()) {
        log("data setup: game data already present");
        if (givenIsVoiceDisc) *exitCode = setupVoices() ? 0 : 3;
        return false;
    }
    fs::path iso;
    if (!given.empty() && !givenIsVoiceDisc) iso = given;
    for (size_t i = 0; iso.empty() && i < images.size(); ++i)
        if (identifyDisc(images[i]) == 1) iso = images[i];
    for (size_t i = 0; iso.empty() && i < images.size(); ++i)   // an unrecognised image: the setup names the problem
        if (identifyDisc(images[i]) == 0) iso = images[i];
    if (iso.empty()) {
        showError("Baroque - game data needed", missingDataText(), gPlayerRun);
        *exitCode = 2;
        return false;
    }
    std::string error;
    if (!runSetup(iso, status, gPlayerRun, &error)) {
        if (error != "cancelled")
            showError("Baroque - setup failed", "Could not prepare the game data from\n" + iso.string() + "\n\n" + error +
                                                 "\n\nDetails are in baroque.log.", gPlayerRun);
        else log("data setup: cancelled");
        *exitCode = 3;
        return false;
    }
    log("data setup: complete");
    setupVoices();
    if (setupIso != nullptr) return false;   // --setup-data: extraction only
    if (!gameDataStatus().complete()) {
        showError("Baroque - setup failed", "The game data is still incomplete after the setup. Details are in baroque.log.",
                  gPlayerRun);
        *exitCode = 3;
        return false;
    }
    return true;
}
}
