#include "game/hud/message_log.hpp"
#include "game/ui/text.hpp"
#include <cstdio>
#include <cstring>

namespace baroque::game {

namespace {
constexpr uint32_t kLogRgb = 0xbbbbbb;        // DAT_0044c190 [V]
constexpr uint32_t kTextDefault = 0x40bbbbbb; // DAT_0044c198 [V]
}

int formatMsg(char* out, size_t cap, const char* fmt, const MsgArg* args, int argCount) {
    size_t o = 0;
    int ai = 0;
    auto put = [&](char c) { if (o + 1 < cap) out[o++] = c; };
    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') { put(*p); continue; }
        const char* spec = p;
        ++p;
        char flags[16];
        int fl = 0;
        while (*p && std::strchr("0123456789-+ #.", *p) && fl < 14) flags[fl++] = *p++;
        flags[fl] = 0;
        const char conv = *p;
        if (conv == 0) break;
        if (conv == '%') { put('%'); continue; }
        char sub[32];
        char tmp[0x200];
        const MsgArg a = ai < argCount ? args[ai++] : MsgArg{};
        if (conv == 's') {
            std::snprintf(sub, sizeof sub, "%%%ss", flags);
            std::snprintf(tmp, sizeof tmp, sub, a.kind == MsgArg::Str && a.s ? a.s : "");
        } else if (conv == 'c') {
            tmp[0] = static_cast<char>(a.i);
            tmp[1] = 0;
        } else if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X') {
            std::snprintf(sub, sizeof sub, "%%%s%c", flags, conv);
            std::snprintf(tmp, sizeof tmp, sub, a.i);
        } else {
            std::snprintf(tmp, sizeof tmp, "%.*s", static_cast<int>(p - spec + 1), spec);
        }
        for (const char* t = tmp; *t; ++t) put(*t);
    }
    if (cap) out[o < cap ? o : cap - 1] = 0;
    return static_cast<int>(o);
}

// orig FUN_0015c2f0 (work/decompiled/0015c2f0.c)
MessageLog::MessageLog() {
    active = 1;
    clearCentre();
    clear();
    enabled = 1;
}

// orig FUN_0015d630 (work/decompiled/0015d630.c): line[i] <- line[i-1] (0x81 bytes) and timer[i] <- timer[i-1],
// i = 5..1
void MessageLog::shift() {
    for (int i = kLines - 1; i >= 1; --i) {
        std::memcpy(line[i], line[i - 1], kLineBytes);
        timer[i] = timer[i - 1];
    }
}

// orig FUN_0015d480 (work/decompiled/0015d480.c)
void MessageLog::print(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printV(fmt, ap);
    va_end(ap);
}

void MessageLog::printV(const char* fmt, va_list ap) {
    shift();
    char buf[0xb0];                               // acStack_e0 (0x81 bytes cleared)
    std::memset(buf, 0, sizeof buf);
    std::vsnprintf(buf, 0x81, fmt, ap);           // FUN_00244ca0 (vsprintf)
    commit(buf);
}

void MessageLog::printFormatted(const char* text) {
    shift();
    char buf[0xb0];
    std::memset(buf, 0, sizeof buf);
    std::snprintf(buf, 0x81, "%s", text);
    commit(buf);
}

void MessageLog::commit(const char* buf) {
    // the text up to '\n' replaces line 0 (an empty message leaves line 0 as it was after the shift)
    unsigned n = 0;
    do {
        const char c = buf[n];
        if (c == '\0' || c == '\n') break;
        line[0][n] = c;
        if (n + 1 < static_cast<unsigned>(kLineBytes)) line[0][n + 1] = 0;   // the original writes +0x2b29+n
        ++n;
    } while (n < 0x81);
    reveal = 0;
    timer[0] = 0x5a;
    if (buf[n] == '\n') {
        char rest[0x90];                          // acStack_170 (0x81 bytes cleared)
        std::memset(rest, 0, sizeof rest);
        unsigned m = 0;
        const char* p = buf + n + 1;
        do {
            const char c = *p;
            if (c == '\0' || c == '\n') break;
            rest[m] = c;
            ++m;
            ++p;
        } while (m < 0x81);
        if (2 < std::strlen(rest)) print(rest);   // FUN_0015d480(obj, rest): the remainder is the next format
    }
}

// orig FUN_0015d0a0 (work/decompiled/0015d0a0.c)
void MessageLog::clear() {
    for (int i = 0; i < kLines; ++i) {
        std::memset(line[i], 0, kLineBytes);
        timer[i] = 0;
    }
    x = 24.0f;    // 0x41c00000
    y = 290.0f;   // 0x43910000
    reveal = 0;
}

// orig FUN_0015ce70 (work/decompiled/0015ce70.c)
void MessageLog::clearCentre() {
    std::memset(centre, 0, sizeof centre);
    centreOn = 0.0f;
    centreReveal = 0;
    centreX = 32.0f;   // 0x42000000
}

// orig FUN_0015cec0 (work/decompiled/0015cec0.c): centre line at (+0x2a90, 405), 4 chars/tick typewriter (cap 0x60)
void MessageLog::drawCentre() {
    if (centreOn == 0.0f) return;
    centreReveal += 4;
    unsigned shown = static_cast<unsigned>(centreReveal);
    if (0x61 < shown) shown = 0x60;
    char partial[0x70];
    std::memset(partial, 0, sizeof partial);
    std::memcpy(partial, centre, shown < sizeof centre ? shown : sizeof centre);
    ui::TextRenderer& t = ui::text();
    t.color = kTextDefault;
    t.halfW = 8.0f;     // 0x41000000
    t.halfH = 9.0f;     // 0x41100000
    t.x = centreX;
    t.y = 405.0f;       // 0x43ca8000
    t.z = 3.5f;         // 0x40600000
    t.draw("%s\n", partial);   // 0x44bfa8
}

// orig FUN_0015d120 (work/decompiled/0015d120.c)
void MessageLog::drawLog() {
    if (enabled == 0 || timer[0] == 0) return;
    x = 24.0f;
    y = 290.0f;
    reveal += 4;
    unsigned shown = static_cast<unsigned>(reveal);
    if (0x81 < shown) shown = 0x80;
    char partial[0x90];                           // auStack_90: the first `shown` bytes of line 0
    std::memset(partial, 0, sizeof partial);
    std::memcpy(partial, line[0], shown);
    ui::TextRenderer& t = ui::text();
    t.layerFlag = 1;     // uRam004e96b0
    t.halfW = 8.0f;      // 0x41000000
    t.halfH = 9.0f;      // 0x41100000
    t.y = y;
    t.x = x;
    t.z = 3.5f;          // 0x40600000
    for (int i = 0; i < kLines; ++i) {
        const unsigned tm = static_cast<unsigned>(timer[i]);
        if (tm != 0) {
            int alpha;
            if (tm < 6) alpha = static_cast<int>(tm << 3);
            else if (tm < 0x52) alpha = 0x40;
            else alpha = (0x5a - static_cast<int>(tm)) * 8;
            t.color = kLogRgb + static_cast<uint32_t>(alpha) * 0x1000000u;
            t.x = x;
            t.y = (y + 0.0f) - static_cast<float>(i) * 18.0f;
            t.z = 3.5f;
            t.draw("%s\n", i == 0 ? partial : line[i]);   // 0x44bfa8
            timer[i] = timer[i] + -1;
        }
    }
    if (timer[0] == 0) clear();
    t.layerFlag = 1;
    t.color = kTextDefault;
}

}  // namespace baroque::game
