#pragma once
// Message log and centre line of the text-window object 0x5180f0 (ITEMS_HUD_MENUS.md 6.3).
//   log:    printf FUN_0015d480, shift FUN_0015d630, draw FUN_0015d120, clear FUN_0015d0a0 (fields +0x2b00..)
//   centre: draw FUN_0015cec0, clear FUN_0015ce70 (fields +0x2a90..+0x2afc)
// The ten text slots of the same object (FUN_0015c490 / FUN_0015c3f0 / FUN_0015c650) are ui::HelpBar
// (game/ui/help_bar.hpp); FUN_0015c380 -> FUN_0015c3b0 runs slots, centre line, log in that order.
// Static ctor FUN_004c7cd0 -> FUN_0015c2f0: +0 = 1, FUN_0015ce70, FUN_0015d0a0, +0x2b00 = 1.
#include "game/items/char_access.hpp"
#include <cstdarg>
#include <cstddef>

namespace baroque::game {

class MessageLog {
public:
    static constexpr int kLines = 6;
    static constexpr int kLineBytes = 0x81;
    MessageLog();                                     // orig FUN_0015c2f0
    void print(const char* fmt, ...);                 // orig FUN_0015d480(0x5180f0, fmt, ...)
    void printV(const char* fmt, va_list ap);
    void printFormatted(const char* text);            // FUN_0015d480 after its vsprintf (GameAccess::message path)
    void drawLog();                                   // orig FUN_0015d120 (also advances timers / reveal)
    void drawCentre();                                // orig FUN_0015cec0
    void clear();                                     // orig FUN_0015d0a0
    void clearCentre();                               // orig FUN_0015ce70
    int active = 1;                                   // +0x0000 (FUN_0015c380 runs FUN_0015c3b0 only when != 0)
    float centreX = 32.0f;                            // +0x2a90
    float centreOn = 0.0f;                            // +0x2a94 (tested as a float; writer not traced)
    int centreReveal = 0;                             // +0x2a98
    char centre[0x61] = {};                           // +0x2a9c
    int enabled = 1;                                  // +0x2b00 (FUN_00160af0 hides the log while paused)
    float x = 24.0f, y = 290.0f;                      // +0x2b04 / +0x2b08 (0x41c00000 / 0x43910000)
    int timer[kLines] = {};                           // +0x2b0c
    int reveal = 0;                                   // +0x2b24
    char line[kLines][kLineBytes] = {};               // +0x2b28 (line 0 = newest, drawn lowest)
private:
    void shift();                                     // orig FUN_0015d630
    void commit(const char* buf);                     // the part of FUN_0015d480 after vsprintf
};

// snprintf over MsgArg (ints and strings; the formats the item code uses: %s %d %3d %2d %02d %c)
int formatMsg(char* out, size_t cap, const char* fmt, const MsgArg* args, int argCount);

}  // namespace baroque::game
