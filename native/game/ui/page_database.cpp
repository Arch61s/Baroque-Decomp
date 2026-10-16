// OPTION database pages (menu modes 0..6) and the persistent database data they show.
//   mode 0 RANKING        vtable 0x4cf160 ctor FUN_001f2480
//   mode 1 EVENT LIST     vtable 0x4cf190 ctor FUN_001f56d0
//   mode 2 ITEM LIST      vtable 0x4cf1c0 ctor FUN_001f6e20
//   mode 3 VOICE LIST     vtable 0x4cf1f0 ctor FUN_001f8d00
//   mode 4 BAROQUE LIST   vtable 0x4cf220 ctor FUN_001faee0
//   mode 5 CHARACTER LIST vtable 0x4cf250 ctor FUN_001fbce0
//   mode 6 SOUND TEST     vtable 0x4cf280 ctor FUN_001fe5a0
// Every page's next-page slot is `j FUN_00163b10` (back to the OPTION main page).
#include "game/ui/database.hpp"
#include "game/ui/menu.hpp"
#include "game/pad.hpp"
#include "game/sound_stub.hpp"
#include "game/ui/help_bar.hpp"
#include "game/ui/strings.hpp"
#include "game/ui/text.hpp"
#include <algorithm>
#include <cstring>
namespace baroque::game::ui {
MenuPage* newVoiceListPage();      // mode 3 (page_voice_list.cpp)
namespace {
std::array<uint8_t, kRankingBytes> gRanking{};

// Port-only placeholder for a database page whose content is not translated yet. UNVERIFIED: not original
// behaviour -- shows the page title and returns to the OPTION page on Circle (the original pages do the same on
// Circle: set wants-next, SE 8, next page FUN_00163b10).
class DatabaseStubPage final : public MenuPage {
public:
    explicit DatabaseStubPage(int mode) : mode_(mode) {}
    const char* debugName() const override { return "DATABASE (not translated)"; }
    void open() override {
        makeFrame();
        makeTitle(str(mode_ + 2));
        isOpen = 1;
        makeFrame2(0);
    }
    void close() override {
        freeFrame2();
        helpBar().clear(0);
        freeFrame();
        isOpen = 0;
    }
    void update() override {
        if (isOpen == 0) return;
        animateTitle();
        TextRenderer& t = text();
        t.layerFlag = 1;
        t.halfW = 8.0f;
        t.halfH = 9.0f;
        t.x = 63.0f;
        t.y = 200.0f;
        t.z = 1.0f;
        t.color = 0x40bbbbbb;
        t.draw("%s - not translated yet", str(mode_ + 2));
    }
    void input() override {
        if (isOpen != 0 && (pad().port[0].pressed & 0x20) != 0) {
            wantsNext_ = 1;
            sound().playSe(1.0f, 8);
        }
    }
    bool wantsNext() const override { return wantsNext_ != 0; }
    MenuPage* nextPage() override { return newOptionPage(); }
private:
    int mode_;
    int wantsNext_ = 0;
};
}  // namespace

std::array<uint8_t, kRankingBytes>& rankingBlock() { return gRanking; }
// orig FUN_001f2700 (work/decompiled/001f2700.c)
void rankingClear() { std::fill(gRanking.begin(), gRanking.end(), uint8_t{0}); }
// orig FUN_001f3190 (work/decompiled/001f3190.c)
int rankingCount() {
    int n = 0;
    for (int i = 0; i < kRankingEntries; ++i) {
        int32_t w;
        std::memcpy(&w, gRanking.data() + i * kRankingStride, 4);
        if (w != 0) ++n;
    }
    return n;
}

// the database cases of the FUN_001677c0 / FUN_00168560 switches
MenuPage* newDatabasePage(int mode) {
    switch (mode) {
    case 0: return newRankingPage();          // FUN_00169240
    case 1: return newEventListPage();        // FUN_001691f0
    case 2: return newItemListPage();         // FUN_001691a0
    case 4: return newBaroqueListPage();      // FUN_00169100
    case 3: return newVoiceListPage();        // FUN_00169150 (page_voice_list.cpp)
    case 5: return newCharacterListPage();    // FUN_001690b0 (page_character_list.cpp)
    case 6: return newSoundTestPage();        // FUN_00169060
    // UNVERIFIED: port-only placeholder page (no mode other than 0..6 reaches here).
    default: return new DatabaseStubPage(mode);
    }
}
}  // namespace baroque::game::ui
