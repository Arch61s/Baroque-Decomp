// VOICE LIST page (OPTION mode 3; vtable 0x4cf1f0; ctor FUN_001f8d00; open FUN_001f8f00, close FUN_001f90e0,
// is-active 0x1faec0 (+0x46c), update 0x1f9770 (title, FUN_001f9900, then the talk per-tick FUN_001a2110), input
// FUN_001f97d0, wants-next 0x1faed0 (+0x470), next page 0x1f9760 (= FUN_00163b10 OPTION)). EVENTS_EFFECTS_PROGRESSION
// 1.8: 18 rows x 13 voices of table 0x468b40 {flag, talk, speaker}; Cross on an unlocked voice replays the talk through
// FUN_001a73a0 (text + voice), column 13 shows the row's picture (four 256x256 textures 0x49c250[row]..+3) once all
// 13 voices of the row are unlocked. View globals: cRam004e5e18 column (0..13), cRam004e5e1c row (0..6),
// iRam0053e9c8 scroll (0..11).
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include "game/fade.hpp"
#include "game/flag_manager.hpp"
#include "game/options.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/talk/talk_manager.hpp"
#include "game/ui/gradient_texture.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/mouse_menu.hpp"
#include "game/ui/sprite.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include "game/ui/texture_registry.hpp"
#include <cstdlib>
#include <cstring>
namespace baroque::game::ui {
namespace {

struct VoiceCell { int flag; const char* talk; const char* speaker; };   // 0x468b40 {char* flag, char* talk, char* speaker}
// 0x468b40 (the flag strings are decimal ids, read with atoi FUN_002388f0)
const VoiceCell kVoices[18][13] = {
    {{3267, "C04A_V009", "C04"}, {3268, "C03_V009", "C03"}, {3269, "C02_V001", "C02"}, {3270, "C06_V101", "C06"},
     {3271, "C02_V213", "C02"}, {3272, "C13A_V022", "C13"}, {3273, "C10A_V000", "C10"}, {3274, "C09_V023", "C09"}, {3275, "C04A_V050", "C04"},
     {3276, "C24_V000", "C24"}, {3277, "C05_V027", "C05"}, {3278, "C17_V001", "C17"}, {3279, "C20_V011B", "C20"}},
    {{3280, "C04B_V007", "C04"}, {3281, "C03_V005", "C03"}, {3282, "C21_V002", "C21"}, {3283, "C08B_V003", "C08"},
     {3284, "C04A_V047", "C04"}, {3285, "C22B_V000", "C22"}, {3286, "C02_V005", "C02"}, {3287, "C11B_V128", "C11"}, {3288, "C06_V325", "C06"},
     {3289, "C03_V605", "C03"}, {3290, "C15A_V017", "C15"}, {3291, "C02_V208", "C02"}, {3292, "C14A_V001", "C14"}},
    {{3293, "C10A_V008", "C10"}, {3294, "C05_V024", "C05"}, {3295, "C14A_V019", "C14"}, {3296, "C12_V005", "C12"},
     {3297, "C24_V008", "C24"}, {3298, "C13A_V012", "C13"}, {3299, "C04A_V052", "C04"}, {3300, "C03_V000", "C03"}, {3301, "C04A_V045", "C04"},
     {3302, "C10A_V031B", "C10"}, {3303, "C11A_V039B", "C11"}, {3304, "C27_V000", "C27"}, {3305, "C06_V303", "C06"}},
    {{3306, "C24_V029", "C24"}, {3307, "C04A_V039", "C04"}, {3308, "C11A_V053", "C11"}, {3309, "C24_V050", "C24"},
     {3310, "C20_V013", "C20"}, {3311, "C23_V030", "C23"}, {3312, "C06_V301", "C06"}, {3313, "C14A_V011", "C14"}, {3314, "C05_V005", "C05"},
     {3315, "C03_V025", "C03"}, {3316, "C14A_V010", "C14"}, {3317, "C02_V007", "C02"}, {3318, "C13A_V024", "C13"}},
    {{3319, "C04B_V032", "C04"}, {3320, "C09_V028", "C09"}, {3321, "C15A_V001", "C15"}, {3322, "C02_V204", "C02"},
     {3323, "C07_V008B", "C07"}, {3324, "C22B_V007", "C22"}, {3325, "C05_V021", "C05"}, {3326, "C13B_V000", "C13"}, {3327, "C03_V001", "C03"},
     {3328, "C06_V322", "C06"}, {3329, "C04A_V037", "C04"}, {3330, "C15A_V020", "C15"}, {3331, "C10A_V007", "C10"}},
    {{3332, "C04A_V036", "C04"}, {3333, "C05_V003", "C05"}, {3334, "C23_V047", "C23"}, {3335, "C14A_V025", "C14"},
     {3336, "C03_V015", "C03"}, {3337, "C27_V003", "C27"}, {3338, "C11A_V040", "C11"}, {3339, "C08B_V004", "C08"}, {3340, "C24_V006", "C24"},
     {3341, "C02_V216", "C02"}, {3342, "C04A_V007", "C04"}, {3343, "C15A_V024B", "C15"}, {3344, "C24_V044", "C24"}},
    {{3345, "C03_V002", "C03"}, {3346, "C17_V004", "C17"}, {3347, "C09_V019", "C09"}, {3348, "C06_V307", "C06"},
     {3349, "C23_V057", "C23"}, {3350, "C13A_V018", "C13"}, {3351, "C14A_V013", "C14"}, {3352, "C04A_V027", "C04"}, {3353, "C09_V034", "C09"},
     {3354, "C12_V012", "C12"}, {3355, "C21_V006", "C21"}, {3356, "C03_V603", "C03"}, {3357, "C05_V018", "C05"}},
    {{3358, "C02_V405", "C02"}, {3359, "C22B_V008", "C22"}, {3360, "C09_V038", "C09"}, {3361, "C04A_V024", "C04"},
     {3362, "C23_V009", "C23"}, {3363, "C03_V003", "C03"}, {3364, "C11A_V025", "C11"}, {3365, "C04B_V029", "C04"}, {3366, "C24_V014", "C24"},
     {3367, "C12_V031", "C12"}, {3368, "C24_V028", "C24"}, {3369, "C02_V209", "C02"}, {3370, "C13A_V030", "C13"}},
    {{3371, "C10B_V000", "C10"}, {3372, "C15A_V022", "C15"}, {3373, "C24_V005", "C24"}, {3374, "C06_V308", "C06"},
     {3375, "C04B_V008", "C04"}, {3376, "C09_V003B", "C09"}, {3377, "C24_V031", "C24"}, {3378, "C05_V016", "C05"}, {3379, "C20_V019", "C20"},
     {3380, "C14A_V028", "C14"}, {3381, "C04A_V023", "C04"}, {3382, "C03_V604", "C03"}, {3383, "C10B_V001", "C10"}},
    {{3384, "C02_V408", "C02"}, {3385, "C11A_V042", "C11"}, {3386, "C17_V006", "C17"}, {3387, "C03_V004", "C03"},
     {3388, "C12_V008", "C12"}, {3389, "C20_V014", "C20"}, {3390, "C04A_V022", "C04"}, {3391, "C06_V328", "C06"}, {3392, "C07_V001", "C07"},
     {3393, "C08B_V005", "C08"}, {3394, "C14A_V017", "C14"}, {3395, "C10A_V016", "C10"}, {3396, "C11A_V036", "C11"}},
    {{3397, "C03_V011", "C03"}, {3398, "C24_V026", "C24"}, {3399, "C04A_V018", "C04"}, {3400, "C09_V002", "C09"},
     {3401, "C23_V000", "C23"}, {3402, "C06_V309", "C06"}, {3403, "C09_V045", "C09"}, {3404, "C02_V214", "C02"}, {3405, "C05_V013", "C05"},
     {3406, "C13B_V007", "C13"}, {3407, "C12_V030", "C12"}, {3408, "C06_V312B", "C06"}, {3409, "C24_V072", "C24"}},
    {{3410, "C02_V409", "C02"}, {3411, "C15A_V018B", "C15"}, {3412, "C13A_V025", "C13"}, {3413, "C03_V608", "C03"},
     {3414, "C02_V411", "C02"}, {3415, "C23_V002", "C23"}, {3416, "C24_V024", "C24"}, {3417, "C04B_V006", "C04"}, {3418, "C12_V001", "C12"},
     {3419, "C03_V006", "C03"}, {3420, "C09_V051", "C09"}, {3421, "C11A_V028", "C11"}, {3422, "C04A_V062", "C04"}},
    {{3423, "C14A_V002", "C14"}, {3424, "C24_V016", "C24"}, {3425, "C24_V002", "C24"}, {3426, "C22A_V000", "C22"},
     {3427, "C15A_V019", "C15"}, {3428, "C05_V011", "C05"}, {3429, "C03_V609", "C03"}, {3430, "C04A_V065", "C04"}, {3431, "C07_V002", "C07"},
     {3432, "C24_V041", "C24"}, {3433, "C02_V211", "C02"}, {3434, "C04B_V009", "C04"}, {3435, "C03_V752", "C03"}},
    {{3436, "C11A_V018", "C11"}, {3437, "C04B_V024", "C04"}, {3438, "C23_V001", "C23"}, {3439, "C03_V018", "C03"},
     {3440, "C13A_V000", "C13"}, {3441, "C24_V019", "C24"}, {3442, "C07_V003", "C07"}, {3443, "C14A_V033", "C14"}, {3444, "C06_V311", "C06"},
     {3445, "C04B_V000", "C04"}, {3446, "C23_V007", "C23"}, {3447, "C09_V001", "C09"}, {3448, "C24_V013", "C24"}},
    {{3449, "C04A_V008", "C04"}, {3450, "C05_V009", "C05"}, {3451, "C23_V026", "C23"}, {3452, "C24_V040", "C24"},
     {3453, "C23_V028", "C23"}, {3454, "C04B_V002", "C04"}, {3455, "C21_V004", "C21"}, {3456, "C20_V012", "C20"}, {3457, "C06_V327", "C06"},
     {3458, "C12_V027B", "C12"}, {3459, "C03_V008", "C03"}, {3460, "C11A_V016", "C11"}, {3461, "C14A_V034", "C14"}},
    {{3462, "C27_V006", "C27"}, {3463, "C23_V038", "C23"}, {3464, "C10A_V035", "C10"}, {3465, "C23_V061", "C23"},
     {3466, "C14A_V000", "C14"}, {3467, "C24_V018", "C24"}, {3468, "C09_V000", "C09"}, {3469, "C15A_V030", "C15"}, {3470, "C12_V009", "C12"},
     {3471, "C04B_V015", "C04"}, {3472, "C07_V006", "C07"}, {3473, "C03_V007", "C03"}, {3474, "C06_V310", "C06"}},
    {{3475, "C20_V015", "C20"}, {3476, "C04B_V018", "C04"}, {3477, "C11A_V015", "C11"}, {3478, "C12_V011", "C12"},
     {3479, "C13A_V010", "C13"}, {3480, "C03_V610", "C03"}, {3481, "C10A_V010", "C10"}, {3482, "C08B_V007", "C08"}, {3483, "C05_V007", "C05"},
     {3484, "C14A_V012", "C14"}, {3485, "C12_V023", "C12"}, {3486, "C06_V319", "C06"}, {3487, "C22A_V002", "C22"}},
    {{3488, "C20_V004B", "C20"}, {3489, "C03_V014", "C03"}, {3490, "C14A_V027", "C14"}, {3491, "C24_V036", "C24"},
     {3492, "C23_V012", "C23"}, {3493, "C04B_V026", "C04"}, {3494, "C13A_V031", "C13"}, {3495, "C17_V002", "C17"}, {3496, "C12_V003", "C12"},
     {3497, "C11B_V127", "C11"}, {3498, "C20_V016", "C20"}, {3499, "C02_V217", "C02"}, {3500, "C04A_V060", "C04"}},
};
// 0x49be60: cell background {x, y, w, h} per column (row y + 39 * row); UV 0x49bf30 {101, 9, 128, 42}, texture 0x44
const float kCellPos[13][4] = {{45, 64, 27, 33}, {75, 64, 27, 33}, {105, 64, 27, 33}, {135, 64, 27, 33}, {165, 64, 27, 33},
    {195, 64, 27, 33}, {225, 64, 27, 33}, {255, 64, 27, 33}, {285, 64, 27, 33}, {315, 64, 27, 33}, {345, 64, 27, 33},
    {375, 64, 27, 33}, {405, 64, 27, 33}};
const float kCellUv[4] = {101, 9, 128, 42};
// 0x49bf40: speaker icon {x, y, w, h} per column; 0x4674e0 speaker list, 0x49c010 icon UVs, 0x49c190 icon texture
const float kIconPos[13][4] = {{46, 66, 24, 28}, {76, 66, 24, 28}, {106, 66, 24, 28}, {136, 66, 24, 28}, {166, 66, 24, 28},
    {196, 66, 24, 28}, {226, 66, 24, 28}, {256, 66, 24, 28}, {286, 66, 24, 28}, {316, 66, 24, 28}, {346, 66, 24, 28},
    {376, 66, 24, 28}, {406, 66, 24, 28}};
const char* const kIconSpeakers[24] = {"C02", "C03", "C04", "C05", "C06", "C07", "C08", "C09", "C10", "C11", "C12", "C13",
    "C14", "C15", "C16", "C17", "C18", "C19", "C20", "C21", "C22", "C23", "C24", "C27"};
const float kIconUv[24][4] = {{0, 0, 24, 28}, {24, 0, 48, 28}, {48, 0, 72, 28}, {72, 0, 96, 28}, {96, 0, 120, 28},
    {0, 28, 24, 56}, {24, 28, 48, 56}, {48, 28, 72, 56}, {72, 28, 96, 56}, {96, 28, 120, 56}, {0, 56, 24, 84},
    {24, 56, 48, 84}, {48, 56, 72, 84}, {72, 56, 96, 84}, {96, 56, 120, 84}, {0, 84, 24, 112}, {24, 84, 48, 112},
    {48, 84, 72, 112}, {72, 84, 96, 112}, {96, 84, 120, 112}, {0, 0, 24, 28}, {24, 0, 48, 28}, {0, 28, 24, 56},
    {24, 28, 48, 56}};
const int kIconTex[24] = {69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 69, 70, 70, 70, 70};
// 0x49c1f0 / 0x49c220: the row's picture button (three pieces) {x, y, w, h} / {u0, v0, u1, v1}, texture 0x44
const float kButtonPos[3][4] = {{431, 66, 7, 29}, {438, 71, 15, 19}, {481, 71, 10, 19}};
const float kButtonUv[3][4] = {{26, 9, 36, 28}, {113, 42, 128, 61}, {36, 9, 43, 38}};
// 0x49c250: first texture id of each row's picture (4 consecutive ids, 2 x 2 quads of 256 x 256)
const int kPictureTex[18] = {71, 75, 79, 83, 87, 91, 95, 99, 103, 107, 111, 115, 119, 123, 127, 131, 135, 139};

signed char gVlCol = 0;   // cRam004e5e18
signed char gVlRow = 0;   // cRam004e5e1c
int gVlScroll = 0;        // iRam0053e9c8

int flagOf(int row, int col) { return kVoices[row][col].flag; }   // FUN_002388f0(*(0x468b40 + i*0xc))
bool talkOn() { return talkManager().active != 0; }                // iRam0053c6e8 (talk manager +8)

// orig FUN_001fa0e0 (work/decompiled/001fa0e0.c): index in the speaker list 0x4674e0, -1 when absent
int iconIndex(const char* speaker) {
    for (int i = 0; i < 24; ++i)
        if (std::strcmp(kIconSpeakers[i], speaker) == 0) return i;
    return -1;
}

class VoiceListPage final : public MenuPage {
public:
    // orig FUN_001f8d00 (work/decompiled/001f8d00.c): fields cleared, FUN_00115f40(snd, 1, 0x2000)
    VoiceListPage() { sound().setReverb(1, 0x2000); }
    const char* debugName() const override { return "VOICE LIST"; }

    // orig FUN_001f8f00 (work/decompiled/001f8f00.c; sprite v1 / texture sizes from the disasm 0x1f8f98..0x1f90a4)
    void open() override {
        makeFrame();
        makeTitle(str(5));
        isOpen = 1;
        wantsNext_ = 0;
        angle_ = 0;
        alpha_ = 0;
        if (options().cursorPos == 1) {
            gVlCol = 0;
            gVlScroll = 0;
            gVlRow = 0;
        }
        textures().load(0x44);
        textures().load(0x45);
        textures().load(0x46);
        cursor_ = sprites().create(43.0f, 62.0f, 0.0f, 30.0f, 36.0f, 0.0f, 1.0f, 32.0f, 32.0f, gradientTextureId(), 1);
        arrowUp_ = sprites().create(23.0f, 60.0f, 1.0f, 12.0f, 13.0f, 13.0f, 17.0f, 25.0f, 30.0f, 0x28, 1);
        arrowDown_ = sprites().create(23.0f, 321.0f, 1.0f, 13.0f, 13.0f, 0.0f, 17.0f, 13.0f, 30.0f, 0x28, 1);
        setScroll(gVlScroll);                                   // FUN_001f9e30
        rowsComplete();                                         // FUN_001fa860
        makeFrame2(3);
    }

    // orig FUN_001f90e0 (work/decompiled/001f90e0.c)
    void close() override {
        helpBar().clear(0);
        freeFrame2();
        freeFrame();
        isOpen = 0;
        sprites().destroyRef(cursor_);
        for (auto& row : cell_) for (Sprite*& s : row) sprites().destroyRef(s);
        for (auto& row : icon_) for (Sprite*& s : row) sprites().destroyRef(s);
        for (auto& row : button_) for (Sprite*& s : row) sprites().destroyRef(s);
        for (Sprite*& s : mark_) sprites().destroyRef(s);
        sprites().destroyRef(arrowUp_);
        sprites().destroyRef(arrowDown_);
        textures().release(0x44);
        textures().release(0x45);
        textures().release(0x46);
    }

    // orig 0x1f9770 (disassembled): title, FUN_001f9900 when settled, then FUN_001a2110(0x53c6e0)
    void update() override {
        if (isOpen == 0) return;
        animateTitle();                                         // FUN_00164dd0 (+ FUN_00164f30, empty)
        if (titleSettled != 0) draw();                          // FUN_001f9900
        if (talkWorld() != nullptr) talkManager().tick();       // FUN_001a2110(0x53c6e0)
    }

    // orig FUN_001f97d0 (work/decompiled/001f97d0.c)
    void input() override {
        if (isOpen == 0 || alpha_ <= 0x3f) return;
        const PadPort& p = pad().port[0];
        const uint32_t rep = p.repeat;                          // uRam004e6290
        if ((p.pressed & 0x40) != 0) {
            decide();                                           // FUN_001fac00
        } else if ((p.pressed & 0x20) != 0) {
            cancel();                                           // FUN_001fadd0
        } else {
            if ((rep & 0x8000) != 0 || (rep & 0x40000) != 0) left();          // FUN_001faaf0
            else if ((rep & 0x2000) != 0 || (rep & 0x80000) != 0) right();   // FUN_001fab70
            if ((rep & 0x1000) != 0 || (rep & 0x10000) != 0) up();            // FUN_001faa10
            else if ((rep & 0x4000) != 0 || (rep & 0x20000) != 0) down();    // FUN_001fa930
        }
    }

    // PORT: mouse menu layer. Hover on a voice cell (or the row's picture button, column 13) = the cursor move of
    // left/right/up/down onto it (help + SE 6), under the guards of input() and the step functions (no move while the
    // picture is shown); a click is Cross (replay / show the picture). The scroll arrows are Up/Down (they scroll at
    // the edge row, as the pad does).
    void mouseInput() override {
        if (isOpen == 0 || alpha_ <= 0x3f || pictureShown()) return;
        const int h = mouse::hovered(this);
        if (h < 0 || h >= 0xe * 7) return;
        const signed char col = static_cast<signed char>(h % 0xe), row = static_cast<signed char>(h / 0xe);
        if (col == gVlCol && row == gVlRow) return;
        gVlCol = col;
        gVlRow = row;
        help();
        sound().playSe(1.0f, 6);
    }
    // PORT: the cells where draw() places the 30 x 36 cursor (x 43 + 30 col, button column at x 452; y 62 + 39 row) and
    // the scroll arrows while they are drawn (arrow sprites at (23, 60) / (23, 321), hit area enlarged as ITEM LIST).
    void mouseItems() override {
        if (isOpen == 0 || titleSettled == 0 || pictureShown()) return;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 0xe; ++c)
                mouse::item(this, c + r * 0xe, c < 0xd ? static_cast<float>(c) * 30.0f + 43.0f : 452.0f,
                            static_cast<float>(r) * 39.0f + 62.0f, 30.0f, 36.0f);
        if (gVlScroll >= 1) mouse::item(this, 0x100, 19.0f, 56.0f, 20.0f, 21.0f, platform::PAD_UP);
        if (gVlScroll < 0xb) mouse::item(this, 0x101, 19.0f, 317.0f, 21.0f, 21.0f, platform::PAD_DOWN);
    }

    bool wantsNext() const override { return wantsNext_ != 0; }   // 0x1faed0
    MenuPage* nextPage() override { return newOptionPage(); }       // 0x1f9760: j FUN_00163b10

private:
    bool pictureShown() const { return picture_[0][0] != nullptr; }   // FUN_001fa850: +0x818 != 0

    // orig FUN_001f9e30 (work/decompiled/001f9e30.c) -> FUN_001fa0d0 (empty) + FUN_001f9e70
    void setScroll(int s) {
        gVlScroll = s;
        // orig FUN_001f9e70 (work/decompiled/001f9e70.c + disasm 0x1f9e70..0x1fa0cc)
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 13; ++c) {
                const float* p = kCellPos[c];
                cell_[r][c] = sprites().create(p[0], p[1] + static_cast<float>(r * 0x27), 1.0f, p[2], p[3], kCellUv[0],
                                               kCellUv[1], kCellUv[2], kCellUv[3], 0x44, 1);
            }
        refreshIcons();                                         // FUN_001fa160
        for (int r = 0; r < 7; ++r)
            for (int k = 0; k < 3; ++k) {
                const float* p = kButtonPos[k];
                const float* uv = kButtonUv[k];
                button_[r][k] = sprites().create(p[0], p[1] + static_cast<float>(r * 0x27), 1.0f, p[2], p[3], uv[0], uv[1],
                                                 uv[2], uv[3], 0x44, 1);
            }
        for (int r = 0; r < 7; ++r)
            mark_[r] = sprites().create(453.0f, static_cast<float>(0x40 + r * 0x27), 1.0f, 28.0f, 33.0f, 72.0f, 9.0f,
                                        101.0f, 42.0f, 0x44, 1);
    }

    // orig FUN_001fa160 (work/decompiled/001fa160.c + disasm 0x1fa280..0x1fa360)
    void refreshIcons() {
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 13; ++c) {
                sprites().destroyRef(icon_[r][c]);
                const VoiceCell& v = kVoices[r + gVlScroll][c];
                const int k = iconIndex(v.speaker);
                if (k < 0) continue;   // UNVERIFIED: -1 indexes before the tables (never: every speaker is listed)
                const float* p = kIconPos[c];
                const float* uv = kIconUv[k];
                icon_[r][c] = sprites().create(p[0], p[1] + static_cast<float>(r * 0x27), 1.0f, p[2], p[3], uv[0], uv[1],
                                               uv[2], uv[3], kIconTex[k], 1);
                if (icon_[r][c] == nullptr) continue;
                const uint32_t rgb = flags().test(static_cast<uint32_t>(v.flag)) ? 0x7f7f7fu : 0x1f1f1fu;
                icon_[r][c]->color = alpha_ * 0x1000000u + rgb;
            }
    }

    // orig FUN_001fa860 (work/decompiled/001fa860.c): a row is complete when all 13 flags are set
    void rowsComplete() {
        for (int r = 0; r < 18; ++r) {
            complete_[r] = 0;
            bool all = true;
            for (int c = 0; c < 13; ++c)
                if (!flags().test(static_cast<uint32_t>(flagOf(r, c)))) {
                    all = false;
                    break;
                }
            if (all) complete_[r] = 1;
        }
    }

    // orig FUN_001f9d70 (work/decompiled/001f9d70.c): the voice number in the help bar (format 0x469638 "NO.%d" in
    // full-width Shift-JIS)
    void help() {
        if (talkOn()) return;
        if (gVlCol < 13) {
            helpBar().set(36.0f, 351.0f, 320.0f, 72.0f, 0, "\x82\x6d\x82\x6e\x81\x44%d",
                          static_cast<int>(gVlCol) + (static_cast<int>(gVlRow) + gVlScroll) * 0xd + 1);
            helpBar().setSpeed(0, 4);                           // uRam005181bc = 4
        } else {
            helpBar().clear(0);
        }
    }

    // orig FUN_001faaf0 / FUN_001fab70 / FUN_001faa10 / FUN_001fa930
    void left() {
        if (isOpen == 0 || pictureShown() || static_cast<signed char>(gVlCol - 1) < 0) return;
        gVlCol = static_cast<signed char>(gVlCol - 1);
        help();
        sound().playSe(1.0f, 6);
    }
    void right() {
        if (isOpen == 0 || pictureShown() || 0xd < static_cast<signed char>(gVlCol + 1)) return;
        gVlCol = static_cast<signed char>(gVlCol + 1);
        help();
        sound().playSe(1.0f, 6);
    }
    void up() {
        if (isOpen == 0 || pictureShown()) return;
        if (static_cast<signed char>(gVlRow - 1) < 0) {
            if (0 < gVlScroll) {
                --gVlScroll;
                rowsComplete();
                refreshIcons();
                help();
                sound().playSe(1.0f, 6);
            }
        } else {
            gVlRow = static_cast<signed char>(gVlRow - 1);
            help();
            sound().playSe(1.0f, 6);
        }
    }
    void down() {
        if (isOpen == 0 || pictureShown()) return;
        if (static_cast<signed char>(gVlRow + 1) < 7) {
            gVlRow = static_cast<signed char>(gVlRow + 1);
            help();
            sound().playSe(1.0f, 6);
        } else if (gVlScroll < 0xb) {
            ++gVlScroll;
            rowsComplete();
            refreshIcons();
            help();
            sound().playSe(1.0f, 6);
        }
    }

    // orig FUN_001fac00 (work/decompiled/001fac00.c)
    void decide() {
        if (isOpen == 0) return;
        const int row = gVlRow + gVlScroll;
        if (!pictureShown() && gVlCol == '\r') {
            if (complete_[row] == 0) {
                sound().playSe(1.0f, 3);
            } else {
                helpBar().clear(0);
                if (talkWorld() != nullptr) talkManager().end();   // FUN_001a6710(0x53c6e0)
                showPicture(row);                               // FUN_001fa410
                sound().playSe(1.0f, 8);
                fade().set(0, 0, 0);                            // FUN_00106c00(0x4e7890, 0, 0, 0)
                fade().set(0x80000000u, 0x1e, 0);
            }
            return;
        }
        if (pictureShown()) return;
        const VoiceCell& v = kVoices[row][static_cast<int>(gVlCol)];
        if (!flags().test(static_cast<uint32_t>(v.flag))) {
            sound().playSe(1.0f, 3);
            return;
        }
        helpBar().clear(0);
        if (talkWorld() != nullptr) talkManager().playByName(v.talk);   // FUN_001a73a0(0x53c6e0, talk)
    }

    // orig FUN_001fadd0 (work/decompiled/001fadd0.c)
    void cancel() {
        if (isOpen == 0) return;
        if (!pictureShown()) {
            if (!talkOn()) {
                wantsNext_ = 1;
                sound().playSe(1.0f, 8);
            } else {
                talkManager().end();
                sound().playSe(1.0f, 2);
            }
            return;
        }
        if (talkWorld() != nullptr) talkManager().end();
        hidePicture();                                          // FUN_001fa690
        sound().playSe(1.0f, 2);
        fade().set(0x80000000u, 0, 0);
        fade().set(0, 0x1e, 0);
    }

    // orig FUN_001fa410 (work/decompiled/001fa410.c): 2 x 2 quads of 256 x 256 at z 5 over the screen
    void showPicture(int row) {
        pictureRow_ = row;
        textures().loadRange(kPictureTex[row], kPictureTex[row] + 3);   // FUN_00106120
        int id = 0;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                Sprite* s = sprites().alloc(kPictureTex[pictureRow_] + id, 1);
                ++id;
                picture_[i][j] = s;
                if (s == nullptr) continue;   // UNVERIFIED: the original writes through a null sprite
                s->cx = (static_cast<float>(i) * 256.0f - 1.0f) + 128.0f;
                s->cy = (static_cast<float>(j) * 256.0f - 1.0f) + 128.0f;
                s->z = 5.0f;
                s->hw = 128.0f;
                s->hh = 128.0f;
                s->color = 0x7f7f7f;
                s->u0 = 0.0f;
                s->v0 = 0.0f;
                s->u1 = 1.0f;
                s->v1 = 1.0f;
            }
        fade().set(0, 0, 0);
        fade().set(0x80000000u, 0x1e, 0);
        alpha_ = 0;
    }

    // orig FUN_001fa690 (work/decompiled/001fa690.c)
    void hidePicture() {
        int id = 0;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                sprites().destroyRef(picture_[i][j]);
                textures().release(id + kPictureTex[pictureRow_]);
                ++id;
            }
        fade().set(0, 0, 0);
        fade().set(0x80000000u, 0x1e, 0);
        alpha_ = 0;
    }

    // orig FUN_001f9900 (work/decompiled/001f9900.c)
    void draw() {
        angle_ += 8;
        if (0x168 < angle_) angle_ = -360;
        // two FUN_00206940 calls whose results are unused
        if (alpha_ < 0x40 && fade().finished()) {
            alpha_ += 6;
            if (0x40 < alpha_) alpha_ = 0x40;
        }
        if (!helpBar().isSet(0) && alpha_ == 0x40 && !talkOn()) help();
        const uint32_t c = alpha_ * 0x1000000u + 0x7f7f7fu;
        if (cursor_ != nullptr) {
            if (gVlCol < '\r') cursor_->cx = static_cast<float>(gVlCol) * 30.0f + 43.0f + cursor_->hw;
            else cursor_->cx = cursor_->hw + 452.0f;
            cursor_->cy = static_cast<float>(gVlRow) * 39.0f + 62.0f + cursor_->hh;
            cursor_->color = c;
        }
        for (int r = 0; r < 7; ++r)
            for (int k = 0; k < 13; ++k) {
                if (cell_[r][k] != nullptr) cell_[r][k]->color = c;
                const bool on = flags().test(static_cast<uint32_t>(flagOf(r + gVlScroll, k)));
                if (icon_[r][k] != nullptr) icon_[r][k]->color = alpha_ * 0x1000000u + (on ? 0x7f7f7fu : 0x1f1f1fu);
            }
        for (int r = 0; r < 7; ++r)
            for (int k = 0; k < 3; ++k)
                if (button_[r][k] != nullptr) button_[r][k]->color = complete_[r + gVlScroll] == 0 ? 0u : c;
        for (int r = 0; r < 7; ++r)
            if (mark_[r] != nullptr)
                mark_[r]->color = alpha_ * 0x1000000u + (complete_[r + gVlScroll] == 0 ? 0x3f3f3fu : 0x7f7f7fu);
        if (pictureShown())
            for (auto& row : picture_)
                for (Sprite* s : row)
                    if (s != nullptr) s->color = c;
        if (arrowUp_ != nullptr) arrowUp_->color = gVlScroll < 1 ? 0u : c;
        if (arrowDown_ != nullptr) arrowDown_->color = gVlScroll < 0xb ? c : 0u;
        text().layerFlag = 1;
        text().color = 0x40bbbbbb;                              // DAT_0044c198
    }

    int wantsNext_ = 0;              // +0x470
    int angle_ = 0;                  // +0x474
    uint32_t alpha_ = 0;             // +0x478
    int complete_[18] = {};          // +0x47c
    Sprite* cursor_ = nullptr;       // +0x4c4
    Sprite* cell_[7][13] = {};       // +0x4c8 (column stride 0x1c, row stride 4)
    Sprite* icon_[7][13] = {};       // +0x634
    Sprite* button_[7][3] = {};      // +0x7a0
    Sprite* mark_[7] = {};           // +0x7f4
    Sprite* arrowUp_ = nullptr;      // +0x810
    Sprite* arrowDown_ = nullptr;    // +0x814
    Sprite* picture_[2][2] = {};     // +0x818
    int pictureRow_ = 0;             // +0x828
};
}  // namespace
MenuPage* newVoiceListPage() { return new VoiceListPage(); }   // FUN_00169150 (ctor FUN_001f8d00)
}  // namespace baroque::game::ui
