#pragma once
// Declarations for the .data tables generated into menu_tables.inc by tools/gen_menu_tables.py (one definition in
// menu_tables.cpp). Addresses and users are listed in the .inc.
namespace baroque::game::ui {
extern const char* const kNameEntryChars[4][106];   // PTR_DAT_0044ca70
extern const char kNameEntryDefault[];               // 0x44c260 (five ideographic spaces)
extern const char kNameEntrySpace[];                 // 0x44c270
extern const char kNameEntryHira[];                  // 0x44c238
extern const char kNameEntryKata[];                  // 0x44c240
extern const char kNameEntryAlpha[];                 // 0x44c248
extern const char kNameEntryEnd[];                   // 0x44c250
extern const float kKeyItemTextPos[8][4];            // 0x491e10
extern const float kKeyIconPos[8][2];                // 0x491e90
extern const float kKeyWindowY[8];                   // 0x491ed0
extern const float kButtonIconUv[8][4];              // 0x491ef0
extern const unsigned int kKeyChoiceFace[4];         // 0x44c120
extern const unsigned int kKeyChoiceShoulder[4];     // 0x44c130
extern const unsigned int kKeyChoicePairs[4];        // 0x44c140
extern const unsigned int kKeyIconsFace[4];          // 0x44c150
extern const unsigned int kKeyIconsPairs[4];         // 0x44c160
extern const unsigned int kKeyIconsShoulder[4];      // 0x44c170
extern const float kSubWinAPos[45][4];               // 0x492d40
extern const float kSubWinAUv[45][4];                // 0x493010
extern const float kSubWinAPos2[2][4];               // 0x4932e0
extern const float kSubWinAUv2[2][4];                // 0x493300
extern const float kSubWinBPos[43][4];               // 0x493320
extern const float kSubWinBUv[43][4];                // 0x4935d0
extern const float kSubWinBPos2[2][4];               // 0x493880
extern const float kSubWinBUv2[2][4];                // 0x4938a0
extern const float kRankHeaderPos[8][4];             // 0x499ad0
extern const float kRankHeaderUv[8][4];              // 0x499b50
extern const float kRankIconUv[2][4];                // 0x499bd0
extern const float kRankFramePos[17][4];             // 0x499bf0
extern const float kRankFrameUv[17][4];              // 0x499d00
extern const float kRankTabPos[2][4];                // 0x499e10
extern const float kRankTabUv[2][4];                 // 0x499e30
extern const float kRankDifficultyUv[4][4];          // 0x499e50
extern const float kRankDifficultyWidth[4];          // 0x499e90
extern const float kEventGroupOfs[12][2];            // 0x499ea0
extern const float kEventFramePos[18][4];            // 0x499f00
extern const float kEventFrameUv[18][4];             // 0x49a020
extern const float kEventThumbPos[36][4];            // 0x49a140
extern const float kEventThumbUv[36][4];             // 0x49a380
extern const int kEventThumbTex[36];                 // 0x49a5c0
extern const int kEventFlags[24];                    // 0x49a650
extern const char* const kItemCodes[0x143];          // PTR_DAT_00466f10
extern const char* const kItemCategories[14];        // PTR_DAT_00466520
extern const int kItemFlags[0x143];                  // 0x49b950
extern const float kItemIconUv[15][4];               // 0x49b860
extern const float kItemFramePos[0x8c][4];           // 0x49a6e0
extern const float kItemFrameUv[0x8c][4];            // 0x49afa0
extern const float kSubWinCPos[29][4];               // 0x4938c0
extern const float kSubWinCUv[29][4];                // 0x493a90
extern const float kSubWinCPart[2][4];               // 0x493c60
extern const float kSoundTitleUv[54][4];             // 0x49cdf0
extern const float kSoundLockedUv[2][4];             // 0x49d150
extern const float kSoundTabPos[4][4];               // 0x49d170
extern const float kSoundTabUv[4][4];                // 0x49d1b0
extern const float kChrFramePos[22][4];              // 0x49c340
extern const float kChrFrameUv[22][4];               // 0x49c4a0
extern const float kChrDigitUv[10][4];               // 0x49c2a0
extern const float kChrHelpX[2];                     // 0x469660
extern const float kChrHelpY[2];                     // 0x469668
extern const int kChrVoiceCount[50];                 // 0x49c600
extern const int kChrFirstVoice[50];                 // 0x49c6d0
extern const int kChrPictureTex[50][4];              // 0x49c7a0
extern const int kChrPictureFlag[50][4];             // 0x49cad0
}
