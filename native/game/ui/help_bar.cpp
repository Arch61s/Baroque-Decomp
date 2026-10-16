#include "game/ui/help_bar.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace baroque::game::ui {
namespace { HelpBar gHelpBar; }
HelpBar& helpBar() { return gHelpBar; }

// orig FUN_0015c490 (work/decompiled/0015c490.c). The formatted text (vsprintf into a 1160-byte buffer, FUN_00244ca0)
// is copied byte by byte, at most 0x401 bytes, each followed by a terminator.
void HelpBar::set(float x, float y, float w, float h, int slot, const char* fmt, ...) {
    clear(slot);
    Slot& s = s_[slot];
    s.x = x; s.y = y; s.w = w; s.h = h;
    s.visible = -1;
    s.enabled = 1;
    s.freeze = 0;
    s.f2950 = 0;
    s.speed = 1;
    s.alpha = 1.0f;
    s.alphaDelta = -1.0f;
    s.color = 0xbbbbbb;   // DAT_0044c190
    s.scaleW = 1.0f;
    s.scaleH = 1.0f;
    s.proportional = 1;
    char buf[1160];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    unsigned n = 0;
    for (const char* p = buf; *p != '\0' && n <= 0x400; ++p, ++n) { s.text[n] = *p; s.text[n + 1] = '\0'; }
}
// orig FUN_0015c3f0 (work/decompiled/0015c3f0.c)
void HelpBar::clear(int slot) {
    for (int i = 0; i < kSlots; ++i) {
        if (slot != -1 && slot != i) continue;
        Slot& s = s_[i];
        std::memset(s.text, 0, sizeof s.text);
        s.x = s.y = s.w = s.h = 0.0f;
        s.visible = 0;
        s.style = 0;
    }
}
// orig FUN_0015c5f0 (work/decompiled/0015c5f0.c)
bool HelpBar::isSet(int slot) const { return s_[slot].text[0] != '\0'; }
// orig FUN_0015c380 -> FUN_0015c650 (work/decompiled/0015c650.c); FUN_0015cec0 / FUN_0015d120 draw windows the
// front end never opens (+0x2a94 / +0x2b00 stay 0) and are not translated.
void HelpBar::update() {
    for (int i = 0; i < kSlots; ++i) {
        const Slot& s = s_[i];
        if (s.w != 0.0f && s.h != 0.0f && s.enabled != 0) {
            if (s.style == 0) drawSlot(i);
            else if (s.style == 1) drawSlotRotated(i);
        }
    }
}
// orig FUN_0015c710 (work/decompiled/0015c710.c): typewriter reveal with word wrap at the window width. After a line
// is drawn the buffer keeps its bytes (only the length restarts), so the width measured right after the flush is the
// drawn line's.
void HelpBar::drawSlot(int i) {
    Slot& s = s_[i];
    TextRenderer& t = text();
    const int len = static_cast<int>(std::strlen(s.text));
    float lineY = s.y;
    int bufLen = 0;
    float width = 0.0f;
    char line[130] = {};
    t.layerFlag = 1;
    t.proportional = s.proportional;
    if (s.visible < len + 1 && s.freeze == 0) s.visible += s.speed;
    if (len != -1) {
        const char* src = s.text;
        for (unsigned idx = 0; idx < static_cast<unsigned>(len) + 1u; ++idx) {
            line[bufLen] = src[idx];
            line[bufLen + 1] = '\0';
            bool flush = false;
            if (idx < static_cast<unsigned>(s.visible) && src[idx] != '\n' && src[idx] != '\0') {
                if (((idx + 1) & 1) == 0) {
                    if (s.w < width && src[idx + 1] != '\n') flush = true;
                    else ++bufLen;
                } else ++bufLen;
            } else flush = true;
            if (flush) {
                float a = s.alpha * 64.0f;
                if (a >= 2.1474836e+09f) a -= 2.1474836e+09f;
                t.color = s.color + (static_cast<uint32_t>(static_cast<int>(a)) << 24);
                t.halfH = s.scaleH * 9.0f;
                t.halfW = s.scaleW * 8.0f;
                t.x = s.x;
                t.z = 3.5f;   // 0x40600000
                t.y = lineY;
                t.draw("%s\n", line);
                if (language() == 1) lineY = s.scaleH * 9.0f * 2.0f + lineY + 0.0f;
                else lineY = s.scaleH * 9.0f * 2.0f * 0.85f + lineY + 0.0f;
                bufLen = 0;
                if (static_cast<unsigned>(s.visible) <= idx) break;
            }
            width = t.measure("%s\n", line);
            if (bufLen >= 126) break;   // PORT guard (the original's line buffer is 128 bytes)
        }
    }
    t.layerFlag = 1;
    t.color = 0x40bbbbbb;   // DAT_0044c198
    if (s.alphaDelta != -1.0f) {
        s.alpha += s.alphaDelta;
        if (1.0f < s.alpha) { s.alpha = 1.0f; s.alphaDelta = -1.0f; }
        else if (s.alpha < 0.0f) { s.alpha = 0.0f; s.alphaDelta = -1.0f; }
    }
    t.proportional = 1;
}
// orig FUN_0015cac0 (work/decompiled/0015cac0.c): the rotated variant (style 1, the EN talk window layout 2): lines
// are columns starting at the slot x and advancing in x, wrapped at the slot height, drawn with the text object in
// mode 1 (uRam004e7e90 = 1: rotated glyph quads of FUN_00112e80 on the polygon layer 0x4e6990).
void HelpBar::drawSlotRotated(int i) {
    Slot& s = s_[i];
    TextRenderer& t = text();
    const int len = static_cast<int>(std::strlen(s.text));
    float colX = s.x;
    int bufLen = 0;
    float width = 0.0f;
    char line[130] = {};
    t.layerFlag = 1;
    if (s.visible < len + 1 && s.freeze == 0) s.visible += s.speed;
    if (len != -1) {
        const char* src = s.text;
        for (unsigned idx = 0; idx < static_cast<unsigned>(len) + 1u; ++idx) {
            line[bufLen] = src[idx];
            line[bufLen + 1] = '\0';
            bool flush = false;
            if (idx < static_cast<unsigned>(s.visible) && src[idx] != '\n' && src[idx] != '\0') {
                if (((idx + 1) & 1) == 0) {
                    if (s.h < width && src[idx + 1] != '\n') flush = true;
                    else ++bufLen;
                } else ++bufLen;
            } else flush = true;
            if (flush) {
                float a = s.alpha * 64.0f;
                if (a >= 2.1474836e+09f) a -= 2.1474836e+09f;
                t.color = s.color + (static_cast<uint32_t>(static_cast<int>(a)) << 24);
                t.halfH = s.scaleH * 9.0f;
                t.halfW = s.scaleW * 8.0f;
                t.y = s.y;
                t.z = 3.5f;   // 0x40600000
                t.x = colX;
                t.mode = 1;             // uRam004e7e90 = 1
                t.draw("%s\n", line);
                t.mode = 0;
                if (language() == 1) colX = s.scaleH * 9.0f * 2.0f + colX + 0.0f;
                else if (language() == 0) colX = s.scaleH * 9.0f * 2.0f * 0.85f + colX + 0.0f;
                bufLen = 0;
                if (static_cast<unsigned>(s.visible) <= idx) break;
            }
            width = t.measure("%s\n", line);
            if (bufLen >= 126) break;   // PORT guard (128-byte line buffer)
        }
    }
    t.layerFlag = 1;
    t.color = 0x40bbbbbb;   // DAT_0044c198
    if (s.alphaDelta != -1.0f) {
        s.alpha += s.alphaDelta;
        if (1.0f < s.alpha) { s.alpha = 1.0f; s.alphaDelta = -1.0f; }
        else if (s.alpha < 0.0f) { s.alpha = 0.0f; s.alphaDelta = -1.0f; }
    }
}
}
