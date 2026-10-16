// ---- hand-copied small tables (tools/disasm.py word reads; tag words are built at runtime by the owning function)
namespace baroque::game::tables {
namespace {
constexpr uint32_t T(const char (&s)[5]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(s[0])) | (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(s[3])) << 24);
}
}
// 0x48dbe0: flag ids {192,184,174,186,178,182,190,180,176,188,5}; tags from 0x44a488.. in FUN_00120f70
const FlagTag kC05Table[11] = {
    {192, T("C05A")}, {184, T("C05B")}, {174, T("C05F")}, {186, T("C05K")}, {178, T("C05E")}, {182, T("C05I")},
    {190, T("C05G")}, {180, T("C05C")}, {176, T("C05H")}, {188, T("C05J")}, {5, T("C05D")}};
// 0x44a500: 02 1f .. LQ22 (1950, 0, 1450) ; 06 1e .. MT00 (550, 0, 400)
const FixedItem kFixedItems[2] = {{2, 0x1f, T("LQ22"), 1950.0f, 0.0f, 1450.0f}, {6, 0x1e, T("MT00"), 550.0f, 0.0f, 400.0f}};
// 0x48dca0: PT10 (-584.74, -2100) PT11 (1600, -2600) PT12 (-780, 2700)
const TownStairs kTownStairs[3] = {{T("PT10"), -584.739990234375f, -2100.0f}, {T("PT11"), 1600.0f, -2600.0f}, {T("PT12"), -780.0f, 2700.0f}};
// 0x44a210 (24 entries); tag words written by FUN_0011e7c0 on first use
const ExtraSpawn kExtraSpawns[24] = {
    {127, T("E24A"), 3}, {137, T("E27A"), 3}, {127, T("E25A"), 3}, {140, T("E26A"), 3}, {440, T("E25A"), 3}, {455, T("E27B"), 3},
    {456, T("E27B"), 3}, {493, T("E26C"), 3}, {469, T("E26B"), 3}, {444, T("E26A"), 3}, {489, T("E25C"), 3}, {445, T("E26A"), 3},
    {483, T("E25C"), 3}, {426, T("E24A"), 3}, {430, T("E24A"), 3}, {433, T("E27A"), 3}, {498, T("E26C"), 3}, {438, T("E27A"), 3},
    {439, T("E25A"), 3}, {446, T("E24B"), 3}, {460, T("E25B"), 3}, {466, T("E26B"), 3}, {471, T("E24C"), 3}, {479, T("E27C"), 3}};
// 0x463eb0 -> strings (15 entries; the 16th pointer is not a prefix)
const char* const kItemPrefixes[15] = {"SD", "CT", "WG", "HT", "MT", "BX", "LQ", "ST", "TL", "TP", "BN", "WM", "GN", "IS", "CC"};
}
