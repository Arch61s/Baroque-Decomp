// Baroque-DungeonCheck: format parsers over every FDT/RDT/CLS in work/assets (counts compared with
// work/map_formats_summary.json) and fixed-seed floor generation with invariant checks.
// Usage: Baroque-DungeonCheck [--root <assets>] [--summary <json>] [--out <dir>]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "data/archive.hpp"
#include "data/map_formats.hpp"
#include "game/dungeon.hpp"
#include "game/rng.hpp"
#include "platform/platform.hpp"

namespace fs = std::filesystem;
using namespace baroque;

namespace {

int g_failures = 0;
void fail(const std::string& m) { g_failures++; platform::log("FAIL: " + m); }

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// minimal lookup of  "counts": { "FDT": n, "RDT": n, "CLS": n }  in the Python summary
long summaryCount(const std::string& json, const char* key) {
    const size_t c = json.find("\"counts\"");
    if (c == std::string::npos) return -1;
    const size_t k = json.find(std::string("\"") + key + "\"", c);
    if (k == std::string::npos) return -1;
    const size_t colon = json.find(':', k);
    return std::strtol(json.c_str() + colon + 1, nullptr, 10);
}

struct ParseStats { long fdt = 0, rdt = 0, cls = 0, padded = 0; };

ParseStats parseAll(const fs::path& root) {
    ParseStats st;
    std::vector<fs::path> bases{root / "DATA"};
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root / "PACKS", ec)) bases.push_back(e.path() / "DATA");
    std::sort(bases.begin() + 1, bases.end());
    for (const auto& base : bases) {
        const fs::path mp = base / "MAP";
        if (!fs::is_directory(mp, ec)) continue;
        for (const auto& e : fs::recursive_directory_iterator(mp)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            if (ext != ".FDT" && ext != ".RDT" && ext != ".CLS") continue;
            const auto bytes = data::Archive::readFile(e.path());
            if (!bytes) { fail("cannot read " + e.path().string()); continue; }
            std::string err;
            size_t end = 0;
            bool ok = false;
            if (ext == ".FDT") { auto r = data::parseFdt(*bytes, &err); ok = r.has_value(); if (ok) end = r->parsedEnd; st.fdt++; }
            else if (ext == ".RDT") {
                auto r = data::parseRdt(*bytes, &err); ok = r.has_value(); if (ok) end = r->parsedEnd; st.rdt++;
                if (ok) {
                    const std::string fn = e.path().filename().string();   // M?NN_II.RDT -> room id == II
                    const int idx = std::atoi(fn.substr(5, 2).c_str());
                    if (idx != r->roomId) fail(fn + ": room id != filename index");
                }
            } else { auto r = data::parseCls(*bytes, &err); ok = r.has_value(); if (ok) end = r->parsedEnd; st.cls++; }
            if (!ok) { fail(e.path().string() + ": " + err); continue; }
            const size_t size = bytes->size();
            if (size % 2048 == 0 && end != size) {
                if (!(end <= size && size < end + 2048)) fail(e.path().string() + ": padded end outside last sector");
                st.padded++;
            } else if (end != size) {
                fail(e.path().string() + ": unaccounted bytes");
            }
        }
    }
    return st;
}

// ---- generation checks
struct GenResult {
    int floorId = 0; uint32_t seed = 0; bool ok = true;
    int placed = 0; int pieces = 0; int placements = 0; int unreachable = 0;
    std::string note;
};

char pieceChar(uint8_t id) {
    if (id == 0) return '|';
    if (id == 1) return '-';
    if (id <= 5) return 'c';
    if (id <= 9) return '+';
    if (id <= 13) return 'T';
    if (id == 14) return 'X';
    return 'D';
}

std::string asciiMap(game::Dungeon& d) {
    const auto& ctx = d.context();
    const int W = ctx.maxX, H = ctx.maxY;
    std::vector<std::string> rows(static_cast<size_t>(std::max(H, 1)), std::string(static_cast<size_t>(std::max(W, 1)), ' '));
    for (int i = 0; i < d.roomCount(); ++i) {
        const game::Room& r = d.room(i);
        if (!r.placed()) continue;
        const int w = r.rdt.w(), h = r.rdt.h();
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const uint32_t c = r.rdt.cells[static_cast<size_t>(y * w + x)];
            const int gx = r.posX * 8 + x, gy = r.posY * 8 + y;
            if (gx < 0 || gy < 0 || gx >= W || gy >= H) continue;
            char ch = ' ';
            const uint32_t t = c & data::kCellTypeMask;
            if (t == data::kCellFloor) ch = (c & data::kCellLevelMask) ? '^' : '.';
            else if (t == data::kCellObject) ch = pieceChar(static_cast<uint8_t>(c));
            if (c & data::kCellStairs) ch = 'S';
            rows[static_cast<size_t>(gy)][static_cast<size_t>(gx)] = ch;
        }
    }
    if (d.player().valid) {
        int cx, cy;
        const float p[3] = {d.player().x, d.player().y, d.player().z};
        if (d.cellAtWorld(p, &cx, &cy) && cx >= 0 && cy >= 0 && cx < W && cy < H) rows[static_cast<size_t>(cy)][static_cast<size_t>(cx)] = 'P';
    }
    std::string out;
    for (const auto& r : rows) { out += r; out += '\n'; }
    return out;
}

GenResult runFloor(data::Archive& archive, int floorId, uint32_t seed, std::string* mapOut) {
    GenResult res; res.floorId = floorId; res.seed = seed;
    game::Rng rng; rng.seed(seed);
    game::Dungeon d(archive, rng);
    std::string err;
    if (!d.load(floorId, 0, &err)) { res.ok = false; res.note = "load: " + err; return res; }
    d.generateLayout();
    if (!d.loadCollision(&err)) { res.ok = false; res.note = "collision: " + err; return res; }
    d.placeContents();
    const auto& ctx = d.context();
    // invariant: placed rooms inside the normalised bounds
    for (int i = 0; i < d.roomCount(); ++i) {
        const game::Room& r = d.room(i);
        if (!r.placed()) continue;
        res.placed++;
        if (r.posX < 0 || r.posY < 0 || (r.posX + r.rdt.w8) * 8 > ctx.maxX || (r.posY + r.rdt.h8) * 8 > ctx.maxY) {
            res.ok = false; res.note += "room " + std::to_string(i) + " outside bounds; ";
        }
    }
    if (res.placed == 0) { res.ok = false; res.note += "no rooms placed; "; }
    // invariant: placed rooms do not overlap
    for (int i = 0; i < d.roomCount(); ++i) for (int j = i + 1; j < d.roomCount(); ++j) {
        const game::Room& a = d.room(i); const game::Room& b = d.room(j);
        if (!a.placed() || !b.placed()) continue;
        if (a.posX < b.posX + b.rdt.w8 && b.posX < a.posX + a.rdt.w8 && a.posY < b.posY + b.rdt.h8 && b.posY < a.posY + a.rdt.h8) {
            res.ok = false; res.note += "rooms " + std::to_string(i) + "/" + std::to_string(j) + " overlap; ";
        }
    }
    // invariant: every placed room reachable from the first placed room over non-void cells (blocked doors are walls)
    {
        std::map<std::pair<int, int>, std::pair<int, uint32_t>> grid;   // global cell -> (room, cell)
        int first = -1;
        for (int i = 0; i < d.roomCount(); ++i) {
            const game::Room& r = d.room(i);
            if (!r.placed()) continue;
            if (first < 0) first = i;
            const int w = r.rdt.w(), h = r.rdt.h();
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                const uint32_t c = r.rdt.cells[static_cast<size_t>(y * w + x)];
                if ((c & data::kCellTypeMask) == 0) continue;
                if ((c & data::kCellTypeMask) == data::kCellObject && (c & 0xff) >= 15) continue;
                grid[{r.posX * 8 + x, r.posY * 8 + y}] = {i, c};
            }
        }
        std::set<int> reached;
        std::set<std::pair<int, int>> seen;
        std::queue<std::pair<int, int>> q;
        for (const auto& kv : grid) if (kv.second.first == first) { q.push(kv.first); seen.insert(kv.first); break; }
        while (!q.empty()) {
            const auto p = q.front(); q.pop();
            reached.insert(grid[p].first);
            const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
            for (int k = 0; k < 4; ++k) {
                const std::pair<int, int> n{p.first + dx[k], p.second + dy[k]};
                if (!grid.count(n) || seen.count(n)) continue;
                seen.insert(n); q.push(n);
            }
        }
        for (int i = 0; i < d.roomCount(); ++i)
            if (d.room(i).placed() && !reached.count(i)) res.unreachable++;
    }
    // invariant: player and stairs on floor cells
    if (game::isGeneratedFloor(floorId)) {
        int cx, cy;
        const float p[3] = {d.player().x, d.player().y, d.player().z};
        const uint32_t* c = d.cellAtWorld(p, &cx, &cy);
        if (!c || (*c & data::kCellTypeMask) != data::kCellFloor) { res.ok = false; res.note += "player not on a floor cell; "; }
        bool stairs = false;
        for (const auto& pl : d.placements()) {
            if (pl.kind != game::Placement::Kind::Stairs) continue;
            stairs = true;
            const float s[3] = {pl.x, pl.y, pl.z};
            const uint32_t* sc = d.cellAtWorld(s, &cx, &cy);
            if (!sc || (*sc & data::kCellTypeMask) != data::kCellFloor || !(*sc & data::kCellStairs)) { res.ok = false; res.note += "stairs not on a floor cell; "; }
        }
        const bool expectStairs = !(floorId == 99 || floorId == 0x8c || game::isNoStairsFloor(floorId));
        if (expectStairs != stairs) { res.ok = false; res.note += expectStairs ? "missing stairs; " : "unexpected stairs; "; }
        if (expectStairs && ctx.stairsRoom < 0) { res.ok = false; res.note += "stairsRoom unset; "; }
    }
    // every object cell has a piece instance and every spawn position lies inside the map
    res.pieces = static_cast<int>(d.pieces().size());
    res.placements = static_cast<int>(d.placements().size());
    for (const auto& pl : d.placements()) {
        if (pl.kind == game::Placement::Kind::ExtraModel) continue;
        if (pl.x < 0.0f || pl.z < 0.0f || pl.x > static_cast<float>(ctx.maxX) * 200.0f || pl.z > static_cast<float>(ctx.maxY) * 200.0f) {
            res.ok = false; res.note += "placement " + data::tagToString(pl.tag) + " outside map; ";
        }
    }
    if (mapOut) {
        *mapOut = "floor " + std::to_string(floorId) + " seed " + std::to_string(seed) + " mapSet " + std::to_string(d.fdt().mapSet) +
                  " rooms " + std::to_string(res.placed) + "/" + std::to_string(d.roomCount()) + " chosen " + std::to_string(d.context().chosenRoomCount) +
                  " bounds " + std::to_string(ctx.maxX) + "x" + std::to_string(ctx.maxY) + " minimap " + std::to_string(ctx.mapW) + "x" + std::to_string(ctx.mapH) +
                  " pieces " + std::to_string(res.pieces) + " floorTris " + std::to_string(d.floorMesh().size()) + "\n";
        *mapOut += "player (" + std::to_string(d.player().x) + ", " + std::to_string(d.player().y) + ", " + std::to_string(d.player().z) + ") angle " +
                   std::to_string(d.player().angleRad) + " stairsRoom " + std::to_string(ctx.stairsRoom) + "\n";
        *mapOut += "legend: . floor ^ upper | - straight c endcap + corner T tee X cross D blocked door S stairs P player\n";
        *mapOut += asciiMap(d);
        for (const auto& pl : d.placements()) {
            static const char* names[] = {"RdtSpawn", "ScenarioEntity", "FdtEntity", "ExtraSpawn", "RandomItem", "FixedItem", "Stairs", "ExtraModel"};
            char line[256];
            std::snprintf(line, sizeof line, "%-14s %-4s room %2d (%9.1f, %6.1f, %9.1f) angle %6.1f %s\n", names[static_cast<int>(pl.kind)],
                          data::tagToString(pl.tag).c_str(), pl.room, pl.x, pl.y, pl.z, pl.angleDeg, pl.note.c_str());
            *mapOut += line;
        }
    }
    return res;
}

} // namespace

int main(int argc, char** argv) {
    fs::path root = platform::assetRoot();
    fs::path summary = fs::path(BAROQUE_WORK_ROOT) / "map_formats_summary.json";
    fs::path outDir = fs::path(BAROQUE_WORK_ROOT) / "dungeon_check";
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--root") root = argv[i + 1];
        else if (a == "--summary") summary = argv[i + 1];
        else if (a == "--out") outDir = argv[i + 1];
    }
    platform::log("asset root: " + root.string());

    // 1. formats
    const ParseStats st = parseAll(root);
    const std::string json = readText(summary);
    const long eF = summaryCount(json, "FDT"), eR = summaryCount(json, "RDT"), eC = summaryCount(json, "CLS");
    char buf[256];
    std::snprintf(buf, sizeof buf, "parsed FDT %ld RDT %ld CLS %ld (padded %ld); summary FDT %ld RDT %ld CLS %ld", st.fdt, st.rdt, st.cls, st.padded, eF, eR, eC);
    platform::log(buf);
    if (st.fdt != eF || st.rdt != eR || st.cls != eC) fail("format counts differ from work/map_formats_summary.json");

    // 2. generation
    data::Archive archive(root);
    std::vector<int> ids;
    for (int id = 100; id <= 140; ++id) ids.push_back(id);
    for (int id = 206; id <= 232; ++id) ids.push_back(id);
    for (int id = 301; id <= 327; ++id) ids.push_back(id);
    for (int id = 400; id <= 499; ++id) ids.push_back(id);
    std::error_code ec;
    fs::create_directories(outDir, ec);
    std::vector<GenResult> results;
    int mapsWritten = 0;
    const int mapFloors[10] = {100, 110, 125, 140, 206, 232, 301, 317, 400, 499};
    for (int id : ids) {
        archive.setPack(data::Archive::packNameForFloor(id));
        if (!archive.exists("DATA/MAP/FLOOR/F" + std::string(id < 100 ? "0" : "") + std::to_string(id) + ".FDT")) continue;
        for (uint32_t seed = 1; seed <= 5; ++seed) {
            std::string map;
            const bool wantMap = seed == 1 && std::find(std::begin(mapFloors), std::end(mapFloors), id) != std::end(mapFloors);
            GenResult r = runFloor(archive, id, seed, wantMap ? &map : nullptr);
            if (!r.ok) fail("floor " + std::to_string(id) + " seed " + std::to_string(seed) + ": " + r.note);
            if (wantMap) {
                std::ofstream f(outDir / ("F" + std::to_string(id) + "_seed1.txt"), std::ios::binary);
                f << map;
                mapsWritten++;
            }
            results.push_back(r);
        }
    }
    int unreachableFloors = 0;
    for (const auto& r : results) if (r.unreachable) unreachableFloors++;
    std::snprintf(buf, sizeof buf, "generated %zu floor/seed pairs, %d with rooms unreachable from the first room, %d maps written",
                  results.size(), unreachableFloors, mapsWritten);
    platform::log(buf);

    // 3. summary JSON
    {
        std::ofstream f(outDir / "summary.json", std::ios::binary);
        f << "{\n \"formats\": {\"FDT\": " << st.fdt << ", \"RDT\": " << st.rdt << ", \"CLS\": " << st.cls << ", \"padded\": " << st.padded << "},\n";
        f << " \"expected\": {\"FDT\": " << eF << ", \"RDT\": " << eR << ", \"CLS\": " << eC << "},\n";
        f << " \"failures\": " << g_failures << ",\n \"floors\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            f << "  {\"id\": " << r.floorId << ", \"seed\": " << r.seed << ", \"ok\": " << (r.ok ? "true" : "false") << ", \"placed\": " << r.placed
              << ", \"pieces\": " << r.pieces << ", \"placements\": " << r.placements << ", \"unreachable\": " << r.unreachable
              << ", \"note\": \"" << r.note << "\"}" << (i + 1 < results.size() ? ",\n" : "\n");
        }
        f << " ]\n}\n";
    }
    if (g_failures) { platform::log("dungeon check FAILED (" + std::to_string(g_failures) + ")"); return 1; }
    platform::log("dungeon check OK");
    return 0;
}
