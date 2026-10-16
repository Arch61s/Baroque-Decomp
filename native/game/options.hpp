#pragma once
// Option data block 0x51af30 (0x3c bytes, saved in every slot) and its backup copy 0x51af70 (SCENES.md 11.2).
// Button configuration slots 0x4e65c0..0x4e65f8 (FUN_00100f10 / FUN_00100f80 / FUN_001010f0).
#include <cstdint>
namespace baroque::game {
struct Options {
    uint8_t initialized;   // +0x00  0xff after FUN_0015d990, 1 after FUN_0015da60 applied once
    uint32_t cursorPos;    // +0x04  0 "Memory" / 1 "Default Settings"
    uint32_t mono;         // +0x08  0 Stereo / 1 Mono
    int8_t bgmVol;         // +0x0c  0..10
    int8_t seVol;          // +0x0d
    int8_t voiceVol;       // +0x0e
    uint32_t vibrationOff; // +0x10  0 ON / 1 OFF
    uint32_t keys[8];      // +0x14  button masks per action (see kKeyGetterSlots)
    int8_t brightness;     // +0x34  0..10
    uint32_t camera;       // +0x38  bit0 up/down reversed, bit1 left/right reversed, bit2 first person
};
struct ButtonConfig {      // 0x4e65c0.. : eight 8-byte slots holding a button mask each
    uint32_t slot[8];      // [0]=0x4e65c0 (0x20 fixed: talk/cancel), [1]=0x4e65c8 attack 0x40, [2]=0x4e65d0 special 0x80,
                           // [3]=0x4e65d8 item screen 0x10, [4]=0x4e65e0 next slot 8, [5]=0x4e65e8 throw 2,
                           // [6]=0x4e65f0 prev slot 4, [7]=0x4e65f8 lock-on 1
    uint32_t flags;        // 0x4e6580 bit0 = vibration enabled
};
Options& options();         // 0x51af30
Options& optionsBackup();   // 0x51af70
ButtonConfig& buttons();
void buttonDefaults();                                   // FUN_00100f10
uint32_t* buttonSlotFor(uint32_t mask);                  // FUN_001010f0 (returns the slot holding `mask`, or nullptr)
void buttonSwap(uint32_t a, uint32_t b, long pairA, long pairB);   // FUN_00100f80
void optionDefaults();                                   // FUN_0015d990
void optionApply();                                      // FUN_0015da60 (first call also loads defaults)
void optionBackupTake();                                 // the 17-word copy in FUN_00160680
void optionBackupRestore();                              // the copy back in FUN_00160930 / FUN_00163730
extern const float kVolumeTable[11][3];                  // 0x491cc0 BGM/SE/voice volume per step
}
