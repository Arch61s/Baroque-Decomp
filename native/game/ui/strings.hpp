#pragma once
// Localized string table 0x437480 (FUN_00114da0(id) = table[id*2 + lang]) and the language global uRam004e9768
// (FUN_00114c80: 0 = EN, 1 = JP). Strings are the original Shift-JIS bytes.
namespace baroque::game::ui {
int language();                 // FUN_00114c80
void setLanguage(int lang);
const char* str(int id);        // FUN_00114da0; "" for out-of-range ids
int stringCount();
void fontsLoad();               // FUN_00114ca0: release tex 0..0x19, load the fonts of the current language
}
