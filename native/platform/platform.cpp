#include "platform/platform.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <mutex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#pragma comment(lib, "user32.lib")
#endif
namespace baroque::platform {
namespace {
std::mutex gLogMutex;
#ifdef _WIN32
HANDLE gLogFile = INVALID_HANDLE_VALUE;
#endif
std::filesystem::path gLogPath;
const auto gStart = std::chrono::steady_clock::now();

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
bool usableStdHandle(DWORD which) {
    const HANDLE h = GetStdHandle(which);
    return h != nullptr && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
}
HANDLE openAppend(const std::filesystem::path& p) {
    return CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}
#endif
void writeLogFile(const std::string& line) {
#ifdef _WIN32
    if (gLogFile == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(gLogFile, line.data(), DWORD(line.size()), &written, nullptr);
#else
    (void)line;
#endif
}
}  // namespace

std::filesystem::path exeDir() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
        if (n == 0) return std::filesystem::current_path();
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path assetRoot() {
    const auto portable = exeDir() / "assets";
#if defined(BAROQUE_ASSET_ROOT) && !defined(BAROQUE_NO_DEV_ASSETS)
    std::error_code ec;
    return std::filesystem::exists(portable / "DATA", ec) ? portable : std::filesystem::path(BAROQUE_ASSET_ROOT);
#else
    return portable;
#endif
}

std::filesystem::path discRoot() { return assetRoot().parent_path() / "disc"; }

void log(const std::string& message) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    if (_fileno(stderr) >= 0) std::fputs((message + "\n").c_str(), stderr);
    if (!gLogPath.empty()) {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStart).count();
        char stamp[32];
        std::snprintf(stamp, sizeof stamp, "[%9.3f] ", t);
        writeLogFile(stamp + message + "\r\n");
    }
}

std::filesystem::path openLogFile(const std::filesystem::path& dir) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(gLogMutex);
    auto tryDir = [](const std::filesystem::path& d) -> bool {
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        const auto cur = d / "baroque.log", old = d / "baroque.old.log";
        if (std::filesystem::exists(cur, ec)) MoveFileExW(cur.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        const HANDLE h = openAppend(cur);
        if (h == INVALID_HANDLE_VALUE) return false;
        gLogFile = h;
        gLogPath = cur;
        return true;
    };
    bool ok = tryDir(dir);
    if (!ok) {
        wchar_t* local = nullptr;
        size_t len = 0;
        if (_wdupenv_s(&local, &len, L"LOCALAPPDATA") == 0 && local != nullptr) {
            ok = tryDir(std::filesystem::path(local) / "Baroque");
            std::free(local);
        }
    }
    if (!ok) return {};
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &now);
    char head[96];
    std::strftime(head, sizeof head, "Baroque log, started %Y-%m-%d %H:%M:%S\r\n", &lt);
    writeLogFile(head);
    return gLogPath;
#else
    (void)dir;
    return {};
#endif
}

std::filesystem::path logFilePath() { return gLogPath; }

void attachConsole(bool allocNew) {
#ifdef _WIN32
    if (usableStdHandle(STD_ERROR_HANDLE) && usableStdHandle(STD_OUTPUT_HANDLE)) return;   // console app / redirected
    if (GetConsoleWindow() != nullptr) return;                                            // already has a console
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !(allocNew && AllocConsole())) return;
    FILE* f = nullptr;
    if (!usableStdHandle(STD_OUTPUT_HANDLE) || _fileno(stdout) < 0) freopen_s(&f, "CONOUT$", "w", stdout);
    if (!usableStdHandle(STD_ERROR_HANDLE) || _fileno(stderr) < 0) freopen_s(&f, "CONOUT$", "w", stderr);
    if (_fileno(stdin) < 0) freopen_s(&f, "CONIN$", "r", stdin);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#else
    (void)allocNew;
#endif
}

void showError(const std::string& title, const std::string& message, bool dialog) {
    log("error: " + title + ": " + message);
#ifdef _WIN32
    if (dialog) MessageBoxW(nullptr, widen(message).c_str(), widen(title).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#else
    (void)dialog;
#endif
}

void installCrashLogging() {
#ifdef _WIN32
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        // "module+offset" survives ASLR, so a player's log maps onto the build's .map / .pdb
        auto where = [](DWORD64 pc, char* out, size_t n) {
            HMODULE mod = nullptr;
            char path[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(pc), &mod) && mod != nullptr) {
                GetModuleFileNameA(mod, path, MAX_PATH);
                const char* base = std::strrchr(path, '\\');
                std::snprintf(out, n, "%s+0x%llx", base ? base + 1 : path,
                              static_cast<unsigned long long>(pc - reinterpret_cast<DWORD64>(mod)));
            } else {
                std::snprintf(out, n, "0x%llx", static_cast<unsigned long long>(pc));
            }
        };
        char line[256], at[160];
        const EXCEPTION_RECORD* er = ep ? ep->ExceptionRecord : nullptr;
        where(er ? reinterpret_cast<DWORD64>(er->ExceptionAddress) : 0, at, sizeof at);
        std::snprintf(line, sizeof line, "crash: unhandled exception 0x%08lx at %p (%s)", er ? er->ExceptionCode : 0ul,
                      er ? er->ExceptionAddress : nullptr, at);
        log(line);
        if (er != nullptr && er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
            std::snprintf(line, sizeof line, "crash: %s address 0x%llx",
                          er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "execute",
                          static_cast<unsigned long long>(er->ExceptionInformation[1]));
            log(line);
        }
#if defined(_M_X64)
        if (ep != nullptr && ep->ContextRecord != nullptr) {   // call stack from the faulting context (x64 unwind data)
            CONTEXT ctx = *ep->ContextRecord;
            for (int frame = 0; frame < 24 && ctx.Rip != 0; ++frame) {
                where(ctx.Rip, at, sizeof at);
                std::snprintf(line, sizeof line, "crash:   #%02d %s", frame, at);
                log(line);
                DWORD64 imageBase = 0;
                PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
                if (fn == nullptr) {   // leaf function: the return address is on top of the stack
                    ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                    ctx.Rsp += 8;
                    continue;
                }
                void* handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
            }
        }
#endif
        return EXCEPTION_CONTINUE_SEARCH;
    });
#endif
    std::set_terminate([] {
        std::string what = "unknown";
        if (const std::exception_ptr e = std::current_exception()) {
            try { std::rethrow_exception(e); } catch (const std::exception& x) { what = x.what(); } catch (...) {}
        }
        log("crash: std::terminate (uncaught exception: " + what + ")");
        std::abort();
    });
    std::signal(SIGABRT, [](int) { log("crash: abort()"); });
}
}
