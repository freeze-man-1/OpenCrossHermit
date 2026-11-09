// ============================================================================
// net_game.cpp — the network game's tables, rosters and battle hand-over
// ============================================================================
#include "net_game.hpp"
#include "../preparation/preparation_task.hpp"
#include "../state/exe_image.hpp"
#include "../state/game_logic.hpp"
#include "../state/unit_stats.hpp"
#include "../text/sjis.hpp"

#include <algorithm>

namespace ch::netgame {

namespace {
constexpr std::uint32_t kMapTable = 0x5ffcb0;      // 128 x {u8 coop, u8, s16 stage, s16 text, u8, u8 maxUnits}
constexpr std::uint32_t kMapTexts = 0x6001b0;      // 0x51 string pointers per text
constexpr std::uint32_t kNetUnits = 0x605f18;      // the eight ready-made units, 0xb0 each
} // namespace

std::string exeText(std::uint32_t va, GameVersion v) { return va ? exe::text(v, va) : std::string(); }

// (FUN_0044c270: the stage of a map entry, mapEntry().stage.)
MapEntry mapEntry(int index, GameVersion v) {
    MapEntry e;
    if (index < 0 || index >= kMapEntries) return e;
    const std::uint32_t r = kMapTable + std::uint32_t(index) * 8;
    e.coop = logic::sdU8(r, v) != 0;
    e.stage = logic::sdS16(r + 2, v);
    e.text = logic::sdS16(r + 4, v);
    e.maxUnits = logic::sdU8(r + 7, v);
    return e;
}

// FUN_0044bd70 / FUN_0044be10: the maps a room may choose, DAT_006000b0
// (2 x 64 words: versus 0..10 and co-operative 64..72 to begin with) plus
// any whose flag the loaded save has set (FUN_004e3210, 0x7d4f3c).
std::vector<int> mapList(bool coop, const GameWork& w, GameVersion v) {
    std::vector<int> out;
    const int first = coop ? kCoopBase : 0;
    for (int i = first; i < first + kCoopBase; ++i)
        if (logic::sdS16(0x6000b0 + std::uint32_t(i) * 2, v) != 0 || logic::seen(w, i)) out.push_back(i);
    return out;
}

// The texts: DAT_006001b0 of ch.exe (Big5), the JP exe's own copy at
// 0x4e67c8 (Shift-JIS).
static std::string mapText(int text, std::uint32_t k, GameVersion v) {
    const std::uint32_t table = v == GameVersion::Jp ? 0x4e67c8u : kMapTexts;
    return exeText(exe::own(v).u32(table + (std::uint32_t(text) * 0x51 + k) * 4), v);
}

std::string mapTitle(int index, GameVersion v) {
    const MapEntry e = mapEntry(index, v);
    if (e.text < 0 || e.text > 0x45) return {};
    return mapText(e.text, 0, v);
}

std::string mapLine(int index, int side, int line, GameVersion v) {
    const MapEntry e = mapEntry(index, v);
    if (e.text < 0 || e.text > 0x45 || side < 0 || side > 3 || line < 0 || line >= 0x14) return {};
    return mapText(e.text, 1 + std::uint32_t(side) * 0x14 + std::uint32_t(line), v);
}

std::vector<Record> defaultRoster(GameVersion v) {
    std::vector<Record> out(8);
    for (std::uint32_t i = 0; i < 8; ++i)
        for (std::uint32_t k = 0; k < kRecord; ++k) out[i][k] = logic::sdU8(kNetUnits + i * kRecord + k, v);
    return out;
}

// FUN_0044a6e0: the save's members (DAT_007a5210, DAT_007a5260), each made
// a battle record as for a stage (FUN_004b9340), at most 30.
// FUN_0044a7f0 orders them: the five groups' members first (DAT_007aaa22,
// 0x1c bytes a group, four words), then the other members.
std::vector<Record> saveRoster(const GameWork& w, GameVersion v) {
    std::vector<int> ids;
    for (std::uint32_t g = 0; g < 5; ++g)
        for (std::uint32_t k = 0; k < 4; ++k)
            if (const int id = w.s16(groups::kBase + groups::kMembers + k * 2 + g * groups::kStride); id != -1) ids.push_back(id);
    for (int i = 0; i < w.s16(roster::kMemberCount); ++i) {
        const int id = w.s16At(roster::kMembers, i);
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    std::vector<Record> out;
    const int n = std::min<int>(int(ids.size()), kRosterMax);
    for (int i = 0; i < n; ++i) {
        stats::Derived d;
        stats::Bonus b;
        stats::derive(stats::readRecord(w, ids[std::size_t(i)]), d, b, v);
        Record r{};
        for (std::size_t k = 0; k < kRecord; ++k) r[k] = d.b[k];
        out.push_back(r);
    }
    return out;
}

std::vector<std::uint8_t> encodeOptions(const Options& o) {
    std::vector<std::uint8_t> b{1, std::uint8_t(o.map)};
    b.insert(b.end(), o.side.begin(), o.side.end());
    return b;
}

bool decodeOptions(const std::vector<std::uint8_t>& b, Options& o) {
    if (b.size() < 6 || b[0] != 1 || b[1] >= kMapEntries) return false;
    o.map = b[1];
    for (int i = 0; i < 4; ++i) o.side[std::size_t(i)] = std::min<std::uint8_t>(b[std::size_t(2 + i)], 4);
    return true;
}

std::vector<std::uint8_t> encodePick(const std::vector<Record>& units) {
    std::vector<std::uint8_t> b{1, std::uint8_t(std::min<std::size_t>(units.size(), 20))};
    for (std::size_t i = 0; i < b[1]; ++i) b.insert(b.end(), units[i].begin(), units[i].end());
    return b;
}

std::vector<Record> decodePick(const std::vector<std::uint8_t>& b) {
    std::vector<Record> out;
    if (b.size() < 2 || b[0] != 1) return out;
    const std::size_t n = std::min<std::size_t>(b[1], (b.size() - 2) / kRecord);
    for (std::size_t i = 0; i < n; ++i) {
        Record r{};
        std::copy_n(b.begin() + std::ptrdiff_t(2 + i * kRecord), kRecord, r.begin());
        out.push_back(r);
    }
    return out;
}

bool writeBattleWork(GameContext& ctx, const net::Session& s) {
    GameWork& w = ctx.work;
    const GameVersion v = ctx.version;
    Options o;
    if (!decodeOptions(s.room().options, o)) return false;
    const MapEntry e = mapEntry(o.map, v);
    if (e.stage <= 0) return false;
    PreparationTask::loadStage(w, v, e.stage, true);   // FUN_004da860 -> FUN_004da8a0(stage, 1)
    // Every player's units, in slot order, each owned by its player's team
    // (+0xa4) and on the field (+0xf 1) to stand at that player's start.
    // (The original put our own first; one order for every machine keeps
    // the unit slots, which the commands name, the same everywhere.)
    std::vector<net::Session::Member> members = s.room().members;
    std::sort(members.begin(), members.end(), [](const auto& a, const auto& b) { return a.slot < b.slot; });
    w.fill(stage::kParty, 0x3700, 0);
    int count = 0;
    std::array<std::uint8_t, 4> present{};
    for (const auto& m : members) {
        if (m.slot < 0 || m.slot > 3) continue;
        present[std::size_t(m.slot)] = 1;
        std::vector<Record> units = decodePick(m.data);
        if (e.maxUnits > 0 && int(units.size()) > e.maxUnits) units.resize(std::size_t(e.maxUnits));
        for (Record& r : units) {
            if (count >= 0x50) break;
            r[0xa4] = std::uint8_t(m.slot);
            r[0xf] = 1;
            for (std::size_t k = 0; k < kRecord; ++k) w.set8(stage::kParty + std::uint32_t(count) * kRecord + std::uint32_t(k), r[k]);
            ++count;
        }
    }
    w.set8(stage::kPartyCount, std::uint8_t(count));
    w.set8(stage::kChained, 0);                                 // not chained on
    w.set8(stage::kSortie, 0);                                 // no sortie cards
    w.set8(stage::kNetSlot, std::uint8_t(s.room().you));        // our slot (FUN_0041ad60)
    w.set8(stage::kNetwork, 1);                                 // network play
    // DAT_007f44e4: the sides. A co-operative map puts everyone on side 0
    // (FUN_0044b950(1)); a random choice (4) is drawn from the relay's seed,
    // the same on every machine (FUN_0044baa0 drew it on the host).
    std::uint32_t seed = s.seed;
    const auto next = [&] { seed = seed * 0x343fdu + 0x269ec3u; return int(seed >> 16 & 0x7fff); };
    // FUN_00448920: the start cells go to the slots in a shuffled order
    // (10..29 swaps), drawn here from the relay's seed so all agree.
    std::array<std::uint8_t, 4> start{0, 1, 2, 3};
    for (int n = next() % 0x14 + 10; n > 0; --n) {
        const int a = next() & 3, b = next() & 3;   // in this order on every machine
        std::swap(start[std::size_t(a)], start[std::size_t(b)]);
    }
    for (int i = 0; i < 4; ++i) {
        int side = e.coop ? 0 : o.side[std::size_t(i)];
        if (side == 4) side = next() & 3;
        w.set8(stage::kSlotSide + std::uint32_t(i), std::uint8_t(side));
        w.set8(stage::kSlotStart + std::uint32_t(i), start[std::size_t(i)]);   // each slot's start cell
        w.set8(stage::kSlotPresent + std::uint32_t(i), present[std::size_t(i)]);
    }
    w.set8(stage::kRoundFlag, e.coop ? 1 : 0);
    // (Debug: CH_NET_FROMSAVE treats the roster as a save's, to test the results' path.)
    w.set8(stage::kNetSaveUnits, s.rosterFromSave || SDL_getenv("CH_NET_FROMSAVE") ? 1 : 0);   // +0x1a99: we brought a save's units
    w.set32(stage::kNetSeed, std::int32_t(s.seed));
    if (SDL_getenv("CH_NET_TRACE"))
        SDL_Log("[net] battle: map entry %d (stage %d, %s), %d units, we are slot %d", o.map, e.stage,
                e.coop ? "co-operative" : "versus", count, s.room().you);
    return true;
}

} // namespace ch::netgame
