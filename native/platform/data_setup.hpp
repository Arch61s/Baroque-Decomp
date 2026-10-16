#pragma once
// PORT (packaging): first-start game data setup from the player's own Baroque (USA) disc image. Nothing of the
// original game ships with the port; the EXE extracts what it reads from an ISO the player supplies:
//   DATA.DIR + DATA.BIN -> <dest>/assets/...   (same layout as tools/unpack_data.py -> work/assets)
//   MOVIE_EN/*, SOUND_EN/* -> <dest>/disc/...   (same files as work/disc)
// Standard library only; the progress window lives in platform/app_host.cpp.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace baroque::platform {
struct DataSetupProgress {
    std::atomic<uint64_t> doneBytes{0};
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<uint32_t> doneFiles{0};
    std::atomic<uint32_t> totalFiles{0};
    std::atomic<int> phase{0};             // 0 reading the image, 1 game data (DATA.BIN), 2 movies + sound, 3 done
    std::atomic<bool> cancel{false};
};
// What is missing under the current asset / disc roots (nothing when both are complete).
struct DataStatus {
    bool assets = false;   // assetRoot()/DATA present
    bool disc = false;     // discRoot()/SOUND_EN/BRQSOUND.SFS and discRoot()/MOVIE_EN/OPENING.PSS present
    bool complete() const { return assets && disc; }
};
DataStatus gameDataStatus();
// Disc images next to the EXE (*.iso, case-insensitive), largest first.
std::vector<std::filesystem::path> findDiscImages(const std::filesystem::path& dir);
// Extracts the missing parts into <dest>/assets and <dest>/disc (staged in *.partial folders, renamed when complete).
// Returns false with a player-readable *error on failure or cancel (partial output removed).
bool extractGameData(const std::filesystem::path& iso, const std::filesystem::path& dest, bool wantAssets,
                     bool wantDisc, DataSetupProgress& progress, std::string* error);
// PORT (voice language): 1 = Baroque USA (SLUS_217.14), 2 = Baroque Europe (SLES_552.16), 3 = Baroque Japan
// (SLPM_667.47), 0 = anything else.
int identifyDisc(const std::filesystem::path& iso);
// The Japanese voices for a retail USA disc, from the Japan disc: SOUND/BRQSOUND.SFS -> discRoot()/SOUND_JP/BRQSOUND.SFS
// (byte-identical to an undubbed image's SOUND_EN archive). The movies stay the USA ones: the Japan disc's are cut
// differently and their audio does not line up.
bool japaneseVoicesPresent();
bool extractJapaneseVoices(const std::filesystem::path& jpIso, DataSetupProgress& progress, std::string* error);
// The English dub, set up from the Europe disc (needs the USA movies in discRoot()/MOVIE_EN first):
//   SOUND_EN/BRQSOUND.SFS -> discRoot()/SOUND_EN_DUB/BRQSOUND.SFS (same 2120-entry layout as the USA archive)
//   MOVIE_P/X.PSS         -> discRoot()/MOVIE_EN_DUB/X.PSS only where its audio differs from the USA movie: the USA
//                            file with its audio payload bytes replaced in order by the Europe ones (the PAL video is
//                            25 fps, the audio track is the same 48 kHz PCM of identical length).
bool englishDubPresent();
bool extractEnglishDub(const std::filesystem::path& euIso, DataSetupProgress& progress, std::string* error);
}
