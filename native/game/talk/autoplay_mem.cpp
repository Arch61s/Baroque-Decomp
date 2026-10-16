// PORT test aid (autoplay_dev.cpp): process memory / handle counters through the OS, kept in its own translation unit
// so <windows.h> never meets raylib's names. Not game logic.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif
#include <cstddef>

namespace baroque::game {

bool autoplayProcessMemory(size_t* workingSet, size_t* privateBytes, unsigned long* handles, unsigned long* gdi,
                           unsigned long* user) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof pmc;
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc))
        return false;
    *workingSet = pmc.WorkingSetSize;
    *privateBytes = pmc.PrivateUsage;
    DWORD h = 0;
    GetProcessHandleCount(GetCurrentProcess(), &h);
    *handles = h;
    *gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    *user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    return true;
#else
    *workingSet = *privateBytes = 0;
    *handles = *gdi = *user = 0;
    return false;
#endif
}

}  // namespace baroque::game
