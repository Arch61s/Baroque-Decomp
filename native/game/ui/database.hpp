#pragma once
// Persistent "database" data shown by the OPTION pages (SCENES.md 11, EVENTS_EFFECTS_PROGRESSION.md 3):
// ranking block 0x53deb0 (0xa28 bytes = 50 entries x 0x34; saved with the game, FUN_001f2720 restores it).
#include <array>
#include <cstdint>
namespace baroque::game::ui {
constexpr int kRankingBytes = 0xa28;
constexpr int kRankingEntries = 0x32;
constexpr int kRankingStride = 0x34;
std::array<uint8_t, kRankingBytes>& rankingBlock();   // 0x53deb0
void rankingClear();                                   // FUN_001f2700: memset(0x53deb0, 0, 0xa28)
int rankingCount();                                    // FUN_001f3190: entries whose first word is non-zero
class MenuPage;
MenuPage* newRankingPage();        // mode 0 (page_ranking.cpp)
MenuPage* newEventListPage();      // mode 1 (page_event_list.cpp)
MenuPage* newItemListPage();       // mode 2 (page_item_list.cpp)
MenuPage* newBaroqueListPage();    // mode 4 (page_baroque_list.cpp)
MenuPage* newSoundTestPage();      // mode 6 (page_sound_test.cpp)
MenuPage* newCharacterListPage();  // mode 5 (page_character_list.cpp)
int itemsAllSeen();                // DAT_00466498 (FUN_001f8b60)
}
