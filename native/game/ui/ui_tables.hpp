#pragma once
// Declarations for the .data tables generated into ui_tables.inc by tools/gen_strings.py (one definition in ui_tables.cpp).
namespace baroque::game::ui {
extern const char* const kTexturePaths[286][2];        // 0x48a280
extern const unsigned short kGlyphCodesEN[115];         // 0x48bf00
extern const unsigned short kGlyphCodesJP[1344];        // 0x48b470
extern const float kGlyphMetricsEN[345][3];             // 0x48bff0 {xofs, yofs, advance}
extern const unsigned short kAsciiMapCodes[27];         // 0x42ca40..0x42cb10
extern const unsigned short kNameEntryPages[4][106];    // PTR_DAT_0044ca70
extern const char* const kMovieNames[29];               // 0x46dec0
extern const float kTitleLogoPos[5][4];                 // 0x48c680
extern const float kTitleLogoUv[5][4];                  // 0x48c6d0
extern const float kTitleCopyrightJP[8];                // 0x48c720
extern const float kTitleCopyrightPosEN[4][4];          // 0x48c740
extern const float kTitleCopyrightUvEN[4][4];           // 0x48c780
extern const float kTitlePressStart[8];                 // 0x48c7c0
extern const float kTitleLetterUv[26][4];               // 0x492090
extern const float kMenuFramePos[22][4];                // 0x493c80
extern const float kMenuFrameUv[22][4];                 // 0x493de0
extern const float kMenuFrame2Pos[61][24];              // 0x493f40 (6 variants x 4 per sprite)
extern const float kMenuFrame2Uv[61][4];                // 0x495620
extern const float kMenuHelpBarPos[6][4];               // 0x4959f0
extern const float kMenuHelpBarUv[4];                   // 0x495a50
extern const float kNameEntryTabPos[4][4];              // 0x495a60
extern const float kNameEntryTabUv[4][4];               // 0x495aa0
extern const float kConfigBarPos[9][4];                 // 0x44bfb0
extern const float kConfigBarUv[9][4];                  // 0x44c040
extern const float kConfigRowPos[10][2];                // 0x491c70
extern const float kFileSlotPos[66][8];                 // 0x49d260
extern const float kFileSlotUv[66][4];                  // 0x49daa0
extern const float kFileCursorPos[3][4];                // 0x49dec0
extern const float kFileCursorUv[3][4];                 // 0x49def0
extern const float kFileDungeonIconUv[10][4];           // 0x49df20
extern const float kFileDifficultyUv[4][4];             // 0x49dfd0
extern const float kFileDifficultyWidth[4];             // 0x49e010
}
