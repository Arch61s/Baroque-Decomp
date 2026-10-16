#include "game/options.hpp"
#include "game/sound_stub.hpp"
#include <cstring>
namespace baroque::game {
namespace {
Options gOptions{};
Options gBackup{};
ButtonConfig gButtons{};
// PTR_FUN_0044c0d0: eight getters used by FUN_0015d990/FUN_0015da60 to read the button slots in the option order
// (0x15d7a0 -> 0x4e65d0, 0x15d7b0 -> 0x4e65c8, 0x15d7c0 -> 0x4e65c0, 0x15d7d0 -> 0x4e65d8, 0x15d7e0 -> 0x4e65f8,
//  0x1534f0 -> 0x4e65e8, 0x153510 -> 0x4e65f0, 0x153500 -> 0x4e65e0)
const int kKeyGetterSlots[8] = {2, 1, 0, 3, 7, 5, 6, 4};
}
const float kVolumeTable[11][3] = {   // 0x491cc0
    {0.0f, 0.0f, 0.0f}, {0.1f, 0.2f, 0.2f}, {0.25f, 0.28f, 0.28f}, {0.35f, 0.38f, 0.38f}, {0.45f, 0.47f, 0.47f},
    {0.55f, 0.55f, 0.55f}, {0.63f, 0.63f, 0.63f}, {0.7f, 0.7f, 0.7f}, {0.8f, 0.8f, 0.8f}, {0.9f, 0.9f, 0.9f}, {1.0f, 1.0f, 1.0f}};
Options& options() { return gOptions; }
Options& optionsBackup() { return gBackup; }
ButtonConfig& buttons() { return gButtons; }

// orig FUN_00100f10 (work/decompiled/00100f10.c)
void buttonDefaults() {
    gButtons.slot[0] = 0x20; gButtons.slot[1] = 0x40; gButtons.slot[2] = 0x80; gButtons.slot[3] = 0x10;
    gButtons.slot[4] = 8; gButtons.slot[5] = 2; gButtons.slot[6] = 4; gButtons.slot[7] = 1;
}
// orig FUN_001010f0 (not exported; semantics from FUN_00100f80 usage): slot whose mask equals `mask`
uint32_t* buttonSlotFor(uint32_t mask) {
    for (uint32_t& s : gButtons.slot) if (s == mask) return &s;
    return nullptr;
}
// orig FUN_00100f80 (work/decompiled/00100f80.c): swap the actions bound to masks a and b, keeping L1/R1 and L2/R2 pairs
void buttonSwap(uint32_t a, uint32_t b, long pairA, long pairB) {
    uint32_t* sa = buttonSlotFor(a);
    uint32_t* sb = buttonSlotFor(b);
    if (sa == nullptr || sb == nullptr) return;
    *sa = b;
    *sb = a;
    const bool lockOrThrow = (sa == &gButtons.slot[7] && sb != &gButtons.slot[5]) || (sa == &gButtons.slot[5] && sb != &gButtons.slot[7]);
    if (lockOrThrow) {
        pairA = (a == 8) ? 4 : (a == 2) ? 1 : (a == 4) ? 8 : 2;
        pairB = (b == 8) ? 4 : (b == 2) ? 1 : (b == 4) ? 8 : 2;
    }
    if (pairA != -1) {
        uint32_t* pa = buttonSlotFor(static_cast<uint32_t>(pairA));
        uint32_t* pb = buttonSlotFor(static_cast<uint32_t>(pairB));
        if (pa != nullptr && pb != nullptr) { *pa = static_cast<uint32_t>(pairB); *pb = static_cast<uint32_t>(pairA); }
        const uint32_t s4 = gButtons.slot[4];
        if (s4 == 4 || s4 == 1) { gButtons.slot[4] = gButtons.slot[6]; gButtons.slot[6] = s4; }
    }
}
// orig FUN_0015d990 (work/decompiled/0015d990.c): defaults
void optionDefaults() {
    gOptions.initialized = 0xff;
    gOptions.cursorPos = 1;
    gOptions.mono = 0;
    gOptions.bgmVol = 6;
    gOptions.seVol = 9;
    gOptions.voiceVol = 7;
    gOptions.vibrationOff = 0;
    gOptions.brightness = 10;
    gOptions.camera = 0;
    buttonDefaults();
    for (int i = 0; i < 8; ++i) gOptions.keys[i] = gButtons.slot[kKeyGetterSlots[i]];
}
// orig FUN_0015da60 (work/decompiled/0015da60.c): apply to the sound manager, pad vibration and button slots
void optionApply() {
    if (gOptions.initialized != 1) { optionDefaults(); gOptions.initialized = 1; }
    SoundManager& snd = sound();
    snd.setMono(gOptions.mono != 0);                                   // FUN_001161e0
    snd.setBgmVolume(kVolumeTable[gOptions.bgmVol][0]);                // FUN_001160e0
    snd.setSeVolume(kVolumeTable[gOptions.seVol][1]);                  // FUN_00116110
    snd.setVoiceVolume(kVolumeTable[gOptions.voiceVol][2]);            // FUN_00116140
    if (gOptions.vibrationOff == 0) gButtons.flags |= 1u;
    else gButtons.flags &= ~1u;                                        // + FUN_0020c540(pad, 0) stop actuators
    for (int i = 0; i < 7; ++i) {
        if (i < 6) buttonSwap(gButtons.slot[kKeyGetterSlots[i]], gOptions.keys[i], -1, -1);
        else buttonSwap(gButtons.slot[kKeyGetterSlots[6]], gOptions.keys[6], static_cast<long>(gButtons.slot[kKeyGetterSlots[7]]), static_cast<long>(gOptions.keys[7]));
    }
}
void optionBackupTake() { std::memcpy(&gBackup, &gOptions, sizeof(Options)); }
void optionBackupRestore() { std::memcpy(&gOptions, &gBackup, sizeof(Options)); }
}
