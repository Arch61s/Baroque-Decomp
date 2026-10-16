#pragma once
// Player-side pieces of the save / rebirth flow (Baroque-Native only; the headless progress check does not link the
// character module): the 0x448-byte status record of the save slot (+0xa374) and the rebirth FUN_0011a8a0.
#include <cstdint>
namespace baroque::game {
struct StatusObj;
void playerRecordWrite(const StatusObj& s, uint8_t* dst0x448);   // orig FUN_00139750
void playerRecordRead(StatusObj& s, const uint8_t* src0x448);    // orig FUN_00139b40
void statusRevive(StatusObj& s);                                 // orig FUN_00133aa0
void playerRevive();                                             // orig FUN_0011a8a0
void playerStateReset();                                         // the inline state-machine reset of FUN_0011a8a0 / FUN_0011bad0
}
