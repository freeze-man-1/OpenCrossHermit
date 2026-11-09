// ============================================================================
// game_logic.cpp
// ============================================================================
#include "game_logic.hpp"
#include "exe_image.hpp"
#include "../../engine/engine_core.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace ch::logic {

namespace {
// Unit record and progress fields.
constexpr std::uint32_t kUnitJob = 6, kUnitStats = 0xc, kUnitRank = 0x50;
constexpr std::uint32_t kUnitSkills = 0x52, kUnitItems = 0x62, kUnitSlots = 0xb0;
constexpr std::uint32_t kJobLevel = 0xd8, kJobOpen = 0xf9, kScore = 0x11c;
constexpr std::uint32_t kItems = 0x7aacaa;   // u16 per item id (1-based)
constexpr std::uint32_t kSeen = 0x7d4f3c;
using roster::kInRoster, roster::kMembers, roster::kMemberCount, roster::kGuests, roster::kGuestCount, roster::kYear;
} // namespace

// The decompiled TW build's tables, the JP version's differing ones from
// its own exe (exe_image.hpp).
std::uint8_t sdU8(std::uint32_t va, GameVersion v) { return exe::logicData(v).bytes(va, 1)[0]; }
std::int16_t sdS16(std::uint32_t va, GameVersion v) {
    const auto b = exe::logicData(v).bytes(va, 2);
    return static_cast<std::int16_t>(b[0] | b[1] << 8);
}
std::int32_t sdS32(std::uint32_t va, GameVersion v) {
    const auto b = exe::logicData(v).bytes(va, 4);
    return static_cast<std::int32_t>(std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 |
                                     std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24);
}
std::span<const std::uint8_t> sdBytes(std::uint32_t va, std::size_t n, GameVersion v) {
    return exe::logicData(v).bytes(va, n);
}

// ----------------------------------------------------------------- flags ---
void setSeen(GameWork& w, int i)   { w.set8(kSeen + std::uint32_t(std::clamp(i, 0, 0x7f)), 1); }   // FUN_004e3170
bool seen(const GameWork& w, int i) { return w.u8(kSeen + std::uint32_t(std::clamp(i, 0, 0x7f))) != 0; }   // FUN_004e3210
void clearSeen(GameWork& w, int i) { w.set8(kSeen + std::uint32_t(std::clamp(i, 0, 0x7f)), 0); }   // FUN_004e31c0

bool inRoster(const GameWork& w, int id) {   // FUN_004d46e0
    return id >= 0 && id < 0xf0 && w.u8(kInRoster + std::uint32_t(id)) != 0;
}

// ------------------------------------------------------------------- rng ---
// FUN_004d1cb0 / FUN_004d1c60: the CRT rand() / srand() (game_logic.hpp).
namespace { std::uint32_t randSeed = 1; }
void srand(std::uint32_t seed) { randSeed = seed; }
int rand() {
    std::uint32_t& seed = randSeed;
    seed = seed * 214013u + 2531011u;
    return int(seed >> 16 & 0x7fff);
}

int rosterSlot(const GameWork& w, int id) {
    if (id == 0 || !inRoster(w, id)) return -1;
    if (id < 0x65) {
        for (int i = 0; i < w.s16(kMemberCount); ++i)
            if (w.s16At(kMembers, i) == id) return i;
    } else {
        for (int i = 0; i < w.s16(kGuestCount); ++i)
            if (w.s16At(kGuests, i) == id) return i;
    }
    return -1;
}

void adjustAffinity(GameWork& w, int a, int b, int delta) {
    if (a > 100) a -= 0x37;
    if (b > 100) b -= 0x37;
    const std::uint32_t at = 0x7d3d71 + std::uint32_t(b) + std::uint32_t(a) * 0x44;
    w.set8(at, std::clamp(w.s8(at) + delta, 1, 100));
}

int affinity(const GameWork& w, int a, int b) {
    if (a > 100) a -= 0x37;
    if (b > 100) b -= 0x37;
    return w.s8(0x7d3d71 + std::uint32_t(b) + std::uint32_t(a) * 0x44);
}

namespace {
// Weighted pick over `weights` (percent, summing to 100): FUN_004d48a0's loops.
template <std::size_t N>
std::size_t pickWeighted(const std::array<int, N>& weights) {
    const int r = rand() % 100;
    int acc = 0;
    std::size_t i = 0;
    while (i < N && (acc += weights[i]) <= r) ++i;
    return i;
}
} // namespace

int unitVoiceLine(int unit, int category, int extra) {
    int c = -1;
    if (unit > 0 && unit < 0xd) c = unit;
    else if (unit == 0x23) c = 0xd;
    if (c < 1) return -1;
    const int base = (c - 1) * 0x28 + 1;
    int line = -1;
    switch (category) {
        case 0: {
            constexpr std::array<int, 2> lines{0, 1};
            line = (c - 1) * 0x28 + lines[std::min<std::size_t>(pickWeighted(std::array<int, 2>{0x1e, 0x46}), 1)];
            break;
        }
        case 1: {
            constexpr std::array<int, 9> t{-1, 5, 6, -1, 1, 2, -1, 3, 4};
            const std::size_t i = pickWeighted(std::array<int, 3>{0x14, 0x28, 0x28});
            if (t[i] != -1) line = base + t[extra < 4 ? i : extra < 6 ? i + 3 : i + 6];
            break;
        }
        case 2: {
            constexpr std::array<int, 6> t{-1, 9, -1, 7, -1, 8};
            const std::size_t i = pickWeighted(std::array<int, 2>{0x1e, 0x46});
            if (t[i] != -1) line = base + t[extra < 4 ? i : extra < 6 ? i + 2 : i + 4];
            break;
        }
        case 3: {
            constexpr std::array<int, 3> t{0x15, 0x13, 0x14};
            (void)rand();                     // the original rolls rand() % 100 even for one line
            line = base + t[extra < 4 ? 0 : extra < 6 ? 1 : 2];
            break;
        }
        case 4: {
            constexpr std::array<int, 6> t{10, 0xd, 0xb, 0xe, 0xc, 0xf};
            const std::size_t i = pickWeighted(std::array<int, 2>{0x32, 0x32});
            line = base + t[extra == 0 ? i : extra < 3 ? i + 2 : i + 4];
            break;
        }
        case 5: line = base + std::array<int, 2>{0x10, 0x1b}[pickWeighted(std::array<int, 2>{0x32, 0x32})]; break;
        case 6: line = base + std::array<int, 3>{0x1a, 0x1c, 0x12}[pickWeighted(std::array<int, 3>{0x1e, 0x28, 0x1e})]; break;
        case 7: line = base + std::array<int, 3>{0x1c, 0x16, 0x17}[pickWeighted(std::array<int, 3>{0x28, 0x1e, 0x1e})]; break;
        case 8: (void)rand(); line = base + 0x1f; break;    // rolled, one line
        case 9: {
            constexpr std::array<int, 3> t{-1, 0x22, 0x23};
            const std::size_t i = pickWeighted(std::array<int, 3>{0x28, 0x1e, 0x1e});
            if (t[i] != -1) line = base + t[i];
            break;
        }
        case 10: (void)rand(); line = base + 0x21; break;   // rolled, one line
        case 0xb: {
            constexpr std::array<int, 4> t{-1, 0x16, 0x1a, 0x1b};
            const std::size_t i = pickWeighted(std::array<int, 4>{0x19, 0x19, 0x19, 0x19});
            if (t[i] != -1) line = base + t[i];
            break;
        }
        default: break;
    }
    return line < 0 ? -1 : std::clamp(line, 0, 0x207);
}

std::string unitVoicePath(int index, GameVersion v) {
    const auto ptr = static_cast<std::uint32_t>(sdS32(0x624adc + std::uint32_t(index) * 4, v));
    std::string s;
    for (std::uint32_t a = ptr; char ch = char(sdU8(a, v)); ++a) s += ch;
    return s;
}

int expToLevel(std::int32_t exp, GameVersion v) {
    std::int32_t acc = 0;
    int i = 1;
    while ((acc += sdS32(0x6e4308 + std::uint32_t(i) * 4, v)) <= exp && i < 0x88) ++i;
    return std::clamp(i - 1, 1, 0x87);
}

std::array<std::int32_t, 8> lectureGain(const GameWork& w, GameVersion v, int unit, int lecture, int affinity) {
    const std::uint32_t rec = GameWork::unitRecord(unit);
    const std::uint32_t lec = 0x74bed0 + std::uint32_t(lecture) * 0x60;
    const UnitSummary sum = unitSummary(w, v, unit);
    // The original averages the trained stats' levels into a local it never
    // stores (it tests the still-zero average first), so the factor is 50.
    const int average = 0;
    std::array<std::int32_t, 8> out{};
    std::int32_t acc = 0, spill = 0, pool = 0;
    for (int c = 0; c < 8; ++c) {
        std::int32_t g = sdS32(lec + 0xc + std::uint32_t(c) * 4, v);
        if (g > 0) g = g * (affinity + 0x32) / 100 * (average + 0x32) / 100;
        if (c == 7) { pool = g; continue; }
        acc += g;
        std::int32_t room = sum.toCap - acc;
        if (room < 0) {
            room += g;
            std::int32_t lost = g;
            if (room < 0) g = 0;
            else { lost = g - room; g = room; }
            spill += lost / 2;
        } else {
            const auto cs = std::uint32_t(c) * 8;
            const StatProgress p = statProgress(w, w.u8(rec + 0xc + cs), w.s32(rec + 0x10 + cs));
            if (p.toMax < g) { spill += (g - p.toMax) / 2; g = p.toMax; }
        }
        out[std::size_t(c)] = g;
    }
    out[7] = pool + spill;
    return out;
}

// -------------------------------------------------------------- new game ---
// FUN_004e1b60 -> FUN_004e1be0 -> FUN_0049e930. The zero writes of the
// original are covered by the memsets; only the non-zero ones are listed.
void newGame(GameWork& w, GameVersion v) {
    w.fill(0x7e17e8, 0x12ca0, 0);
    w.fill(stage::kParty, 0x3700, 0);
    w.fill(stage::kBase, stage::kSize, 0);
    w.copy(0x7e17e8, sdBytes(0x6f5088, 0x12ca0, v));      // 65 unit templates (FUN_004e1be0)

    w.fill(0x7a50e8, 0x306d4, 0);
    w.set16(kYear, 1);
    w.setMonth(4); w.setWeek(0); w.setDay(1);
    w.set16(0x7a55f8, 1);
    for (int i = 0; i < 5; ++i) {                         // five groups
        const std::uint32_t g = groups::at(i);
        w.set16(g + groups::kLeader, -1);
        for (std::uint32_t k = 0; k < 4; ++k) w.set16(g + groups::kMembers + k * 2, -1);
        w.set8(g + 3, 1);                                 // 0x7aaa15: lecture flag
        w.set8(g + 4, 0xff);                              // 0x7aaa16
        w.set16(g + 6, -1);                               // 0x7aaa18..0x7aaa1e: the choice
        w.set16(g + 8, -1);
        w.set16(g + 0xa, -1);
        w.set16(g + 0xc, -1);
    }
    for (std::uint32_t i = 0; i < 5; ++i) {               // FUN_004d3d00
        for (std::uint32_t k = 0; k < 4; ++k) {
            w.set16(groups::kMemberSlots + i * 8 + k * 2, -1);
            w.set16(groups::kMemberIds + i * 8 + k * 2, -1);
        }
        w.set16(groups::kLeaderSlots + i * 2, -1);
        w.set16(groups::kLeaderIds + i * 2, -1);
    }
    w.copy(0x7d3d70, sdBytes(0x74e4b0, 0x43 * 0x44, v));
    for (std::uint32_t i = 0; i < 300; ++i) w.set8(0x7a54ca + i, 5);
    for (int u = 0; u < 0x2d; ++u)                        // starting job levels
        for (int j = 0; j < 0x20; ++j) {
            const int lv = sdS16(0x751242 + std::uint32_t(j) * 2 + std::uint32_t(u) * 0x42, v);
            w.set8(GameWork::unitProgress(u) + kJobOpen + std::uint32_t(j), lv == 100);
            w.set8(GameWork::unitProgress(u) + kJobLevel + std::uint32_t(j), std::clamp(lv, 0, 100));
        }
    joinRoster(w, v, 0x65, 0, -1);
    joinRoster(w, v, 3, 0, 0);
    joinRoster(w, v, 4, 0, 1);
    joinRoster(w, v, 9, 0, 2);
    for (int i = 0; i < 0x80; ++i) clearSeen(w, i);       // FUN_004e1ca0
    for (int i = 0; i <= 10; ++i) setSeen(w, i);
}

// FUN_004d31f0 + FUN_004d34a0 (FUN_0049e8f0, also run after a load).
void refreshEquipment(GameWork& w) {
    // FUN_004d31f0: equipped items whose owner left the roster or unequipped them go back to storage.
    for (std::uint32_t i = 0; i < 0x168; ++i) {
        const std::uint32_t at = kItems + 2 + i * 2;
        const std::uint16_t f = w.u16(at);
        const int state = f >> 8 & 0xf;
        if ((f & 1) != 1 || state <= 2 || state >= 5) continue;
        const int owner = f >> 1 & 0x7f;
        bool unequipped = true;
        if (inRoster(w, owner)) {
            for (std::uint32_t k = 0; k < 8; ++k)
                if (w.s16(GameWork::unitRecord(owner) + kUnitItems + k * 2) == int(i) + 1) {
                    unequipped = false;
                    w.set16(at, (w.u16(at) & 0xff01) | (owner & 0x7f) << 1);
                }
        }
        if (unequipped) w.set16(at, (w.u16(at) & 0xf0ff) | 0x100);
    }
    // FUN_004d34a0 -> FUN_004d33a0: skill slots in state 6 not in the unit's set become 5.
    for (int u = 1; u < 0x2d; ++u) {
        if (!inRoster(w, u)) continue;
        for (std::uint32_t s = 0; s < 0x54; ++s) {
            const std::uint32_t at = GameWork::unitRecord(u) + kUnitSlots + 8 + s * 0xc;
            if (w.u8(at) != 6) continue;
            bool loose = true;
            for (std::uint32_t k = 0; k < 8; ++k)
                if (w.s16(GameWork::unitRecord(u) + kUnitSkills + k * 2) == int(s) + 1) loose = false;
            if (loose) w.set8(at, 5);
        }
    }
}

// FUN_004d7700 (save) / FUN_004d75a0 + FUN_004d7410 (load). A save is the
// 0x306d4 bytes at 0x7a50e8: +0 s16 version 7, +4 u32 build stamp 0x10002,
// +0x2c s32 sum of all signed bytes (FUN_004d76b0, taken with the field zeroed). Before
// writing, the 65 unit records (0x7e17e8) are copied to 0x7bc6ac and the
// 0x11730 bytes at 0x7f7c18 to 0x7aaf7c; a load restores only the units
// (FUN_004e1c70, the other restore, is empty in this build).
namespace {
constexpr std::uint32_t kSaveBase = 0x7a50e8, kSaveSize = 0x306d4;
constexpr std::uint32_t kSaveSum = kSaveBase + 0x2c;                                   // 0x7a5114
// The blocks saved through a copy (see above).
constexpr std::uint32_t kUnitRecords = 0x7e17e8, kUnitCopy = 0x7bc6ac, kUnitBytes = 0x41 * 0x4a0;
constexpr std::uint32_t kGlobals = 0x7f7c18, kGlobalsCopy = 0x7aaf7c, kGlobalsBytes = 0x11730;
void copyBlock(GameWork& w, std::uint32_t to, std::uint32_t from, std::uint32_t n) {
    for (std::uint32_t i = 0; i < n; ++i) w.set8(to + i, w.u8(from + i));
}
std::int32_t saveChecksum(std::span<const std::uint8_t> b) {
    std::int32_t sum = 0;
    for (std::size_t i = 0; i < b.size(); ++i)
        if (i < 0x2c || i >= 0x30) sum += static_cast<std::int8_t>(b[i]);
    return sum;
}
} // namespace

std::vector<std::uint8_t> saveGame(GameWork& w) {
    w.set16(kSaveBase, 7);
    w.set32(kSaveBase + 4, 0x10002);                      // FUN_004e2c30
    copyBlock(w, kUnitCopy, kUnitRecords, kUnitBytes);
    copyBlock(w, kGlobalsCopy, kGlobals, kGlobalsBytes);
    w.set32(kSaveSum, 0);
    std::vector<std::uint8_t> out(kSaveSize);
    for (std::uint32_t i = 0; i < kSaveSize; ++i) out[i] = w.u8(kSaveBase + i);
    const std::int32_t sum = saveChecksum(out);
    w.set32(kSaveSum, sum);
    for (int k = 0; k < 4; ++k) out[0x2c + std::size_t(k)] = static_cast<std::uint8_t>(std::uint32_t(sum) >> (8 * k));
    return out;
}

namespace {
SaveProblem saveIntegrity(std::span<const std::uint8_t> b) {
    if (b.size() != kSaveSize) return SaveProblem::Size;
    const auto rd = [&](std::size_t o) { return std::uint32_t(b[o]) | std::uint32_t(b[o + 1]) << 8 |
                                                std::uint32_t(b[o + 2]) << 16 | std::uint32_t(b[o + 3]) << 24; };
    if ((b[0] | b[1] << 8) != 7 || rd(4) > 0x10002) return SaveProblem::Format;
    return static_cast<std::int32_t>(rd(0x2c)) == saveChecksum(b) ? SaveProblem::None : SaveProblem::Checksum;
}
} // namespace

bool saveValid(std::span<const std::uint8_t> b) { return saveIntegrity(b) == SaveProblem::None; }

int saveField16(std::span<const std::uint8_t> b, std::uint32_t va) {
    const std::size_t o = va - kSaveBase;
    return int(std::int16_t(b[o] | b[o + 1] << 8));
}

SaveProblem saveCheck(std::span<const std::uint8_t> b) {
    if (const SaveProblem p = saveIntegrity(b); p != SaveProblem::None) return p;
    const auto s16 = [&](std::uint32_t va) { return saveField16(b, va); };
    // joinRoster / leaveRoster keep these in step: each id on its list once,
    // and flagged; nothing else flagged. Members are ids below 0x65.
    const auto flag = [&](int id) { return b[kInRoster - kSaveBase + std::size_t(id)]; };
    std::array<bool, 0xf0> listed{};
    const auto listOk = [&](std::uint32_t list, int count, int limit, int low, int high) {
        if (count < 0 || count > limit) return false;
        for (int i = 0; i < count; ++i) {
            const int id = s16(list + std::uint32_t(i) * 2);
            if (id < low || id > high || listed[std::size_t(id)] || flag(id) != 1) return false;
            listed[std::size_t(id)] = true;
        }
        return true;
    };
    if (!listOk(kMembers, s16(kMemberCount), 40, 1, 0x64) || !listOk(kGuests, s16(kGuestCount), 20, 0x65, 0xef))
        return SaveProblem::Contents;
    for (int id = 0; id < 0xf0; ++id)
        if (flag(id) != (listed[std::size_t(id)] ? 1 : 0)) return SaveProblem::Contents;
    // The list shows the year and month in two digits and the week in one.
    // Scripts can set the month, so these are wider than a normal game.
    const int year = s16(kYear), month = s16(roster::kMonth), week = s16(roster::kWeek);
    if (year < 0 || year > 99 || month < 1 || month > 99 || week < 0 || week > 9) return SaveProblem::Contents;
    return SaveProblem::None;
}

bool loadGame(GameWork& w, std::span<const std::uint8_t> b) {
    if (!saveValid(b)) return false;
    for (std::uint32_t i = 0; i < kSaveSize; ++i) w.set8(kSaveBase + i, b[i]);
    copyBlock(w, kUnitRecords, kUnitCopy, kUnitBytes);
    refreshEquipment(w);                                   // FUN_0049e8f0
    return true;
}

// FUN_004d3510
void advanceWeek(GameWork& w, GameVersion) {
    w.setWeek(w.week() + 1);
    if (w.week() > 5) { w.setMonth(w.month() + 1); w.setWeek(1); }
    w.set16(0x7a55fa, 0);
    w.setWorkRoomEventSeen(false);
    for (int i = 0; i < w.s16(kMemberCount); ++i) openJobs(w, w.s16At(kMembers, i));
    refreshEquipment(w);
    w.setAreaBgm(0xb);
    w.setAreaBgmOn(false);
}

// ---------------------------------------------------------------- roster ---
// FUN_004d3e90. party/slot place the unit in a formation (-1 = none).
void joinRoster(GameWork& w, GameVersion v, int id, int party, int slot) {
    if (id == 0 || inRoster(w, id) || id < 0 || id >= 0xf0) return;
    if (id < 0x65) {
        const int index = w.s16(kMemberCount);
        w.set16(kMemberCount, index + 1);
        w.set16At(kMembers, index, id);
        if (party >= 0 && slot >= 0) {
            w.set16(groups::kMemberSlots + std::uint32_t(party) * 8 + std::uint32_t(slot) * 2, index);
            w.set16(groups::kMemberIds + std::uint32_t(party) * 8 + std::uint32_t(slot) * 2, id);
            w.set16(groups::kBase + groups::kMembers + std::uint32_t(slot) * 2 + std::uint32_t(party) * groups::kStride, id);
        }
        const std::uint32_t rec = GameWork::unitRecord(id), prog = GameWork::unitProgress(id);
        for (std::uint32_t j = 0; j < 0x20; ++j) w.set8(prog + kJobOpen + j, 0);
        w.set8(prog + kJobOpen - 1 + std::uint32_t(w.s16(rec + kUnitJob)), 1);   // current job stays open
        for (std::uint32_t k = 0; k < 8; ++k) {          // equipped items: claim, or drop if taken
            const int item = w.s16(rec + kUnitItems + k * 2);
            if (item <= 0) continue;
            const std::uint32_t at = kItems + std::uint32_t(item) * 2;
            if ((w.u16(at) & 1) == 1) { w.set16(rec + kUnitItems + k * 2, 0); continue; }
            std::uint16_t f = static_cast<std::uint16_t>(w.u16(at) | 1);
            f = static_cast<std::uint16_t>((f & 0xf0ff) | 0x300);
            f = static_cast<std::uint16_t>((f & 0xff01) | (id & 0x7f) << 1);
            w.set16(at, f);
        }
        for (std::uint32_t k = 0; k < 8; ++k) {          // skills: set, unless past the first year
            const int s = w.s16(rec + kUnitSkills + k * 2);
            if (s <= 0) continue;
            const int year = w.s16(kYear);
            if (year < 0 || year > 1) w.set16(rec + kUnitSkills + k * 2, 0);
            else w.set8(rec + kUnitSlots + 8 + std::uint32_t(s - 1) * 0xc, 6);
        }
        w.set8(rec + kUnitRank, unitSummary(w, v, id).rank);
        openJobs(w, id);
    } else {
        const int index = w.s16(kGuestCount);
        w.set16(kGuestCount, index + 1);
        w.set16At(kGuests, index, id);
        if (party >= 0) {
            w.set16At(groups::kLeaderSlots, party, index);
            w.set16At(groups::kLeaderIds, party, id);
            w.set16(groups::kBase + std::uint32_t(party) * groups::kStride, id);
        }
    }
    w.set8(kInRoster + std::uint32_t(id), 1);             // FUN_004d4730
}

// FUN_004d4250
void leaveRoster(GameWork& w, int id) {
    if (id == 0 || !inRoster(w, id)) return;
    if (id < 0x65) {
        const int n = w.s16(kMemberCount);
        int i = 0;
        while (i < n && w.s16At(kMembers, i) != id) ++i;
        for (; i < n - 1; ++i) w.set16At(kMembers, i, w.s16At(kMembers, i + 1));
        w.set16(kMemberCount, n - 1);
        for (std::uint32_t p = 0; p < 5; ++p)
            for (std::uint32_t k = 0; k < 4; ++k)
                if (w.s16(groups::kBase + groups::kMembers + k * 2 + p * groups::kStride) == id) w.set16(groups::kBase + groups::kMembers + k * 2 + p * groups::kStride, -1);
        for (std::uint32_t k = 0; k < 8; ++k) {          // equipped items go to storage
            const int item = w.s16(GameWork::unitRecord(id) + kUnitItems + k * 2);
            if (item <= 0) continue;
            const std::uint32_t at = kItems + std::uint32_t(item) * 2;
            if ((w.u16(at) & 1) == 1) w.set16(at, ((w.u16(at) | 1) & 0xf0ff) | 0x100);
        }
    } else {
        const int n = w.s16(kGuestCount);
        int i = 0;
        while (i < n && w.s16At(kGuests, i) != id) ++i;
        for (; i < n - 1; ++i) w.set16At(kGuests, i, w.s16At(kGuests, i + 1));
        for (std::uint32_t p = 0; p < 5; ++p)
            if (w.s16(groups::kBase + p * groups::kStride) == id) w.set16(groups::kBase + p * groups::kStride, -1);
        w.set16(kGuestCount, n - 1);
    }
    w.set8(kInRoster + std::uint32_t(id), 0);             // FUN_004d4770
}

// ------------------------------------------------------------------ jobs ---
// FUN_004d3aa0: job rows 1..30 at 0x738cd0 (0x32 bytes): +3 month, +4 week
// (0 = no date), +6 s16[7] stat minimums, +0x14 s16 stat total,
// +0x16 {u8 job, u8 level}[3] prerequisites. A dated job opens once its
// date has come (month * 6 + week, as the original) or when already
// mastered; any job opens when every requirement is met.
void openJobs(GameWork& w, int unit) {
    const std::uint32_t rec = GameWork::unitRecord(unit), prog = GameWork::unitProgress(unit);
    for (std::uint32_t c = 0; c < 0x1e; ++c) {
        const std::uint32_t job = 0x738cd0 + (c + 1) * 0x32;
        bool open = false;
        const int m = sdU8(job + 3), wk = sdU8(job + 4);
        if (m != 0 && wk != 0) {
            if (w.s8(prog + kJobLevel + c) == 100) open = true;
            else if (static_cast<std::int16_t>(m * 6 + wk) <= static_cast<std::int16_t>(w.month() * 6 + w.week())) open = true;
        }
        if (!open) {
            open = true;
            int total = 0;
            for (std::uint32_t s = 0; s < 7; ++s) {
                const int lv = w.u8(rec + kUnitStats + s * 8);
                total += lv;
                if (lv < sdS16(job + 6 + s * 2)) open = false;
            }
            if (total < sdS16(job + 0x14)) open = false;
            for (std::uint32_t k = 0; k < 3; ++k) {
                const int need = sdU8(job + 0x16 + k * 2);
                if (need != 0 && w.s8(prog + kJobLevel - 1 + std::uint32_t(need)) < int(sdU8(job + 0x17 + k * 2)))
                    open = false;
            }
        }
        if (open) w.set8(prog + kJobOpen + c, 1);
    }
}

// ------------------------------------------------------------- summaries ---
// FUN_004d56a0's level cap: 100, or 0x87 when the year is not 0 / 1.
int levelCap(const GameWork& w) {
    const int year = w.s16(kYear);
    return year < 0 || year > 1 ? 0x87 : 100;
}

// FUN_004d56a0: exp per level from 0x6e4308, up to levelCap.
StatProgress statProgress(const GameWork& w, int level, std::int32_t exp) {
    const auto expAt = [](int k) { return sdS32(0x6e4308 + std::uint32_t(k) * 4); };
    const int cap = levelCap(w);
    StatProgress p;
    for (int k = 0; k <= cap; ++k) p.capTotal += expAt(k);
    p.toMax = p.capTotal - exp;
    const int next = level + 1;
    for (int k = 0; k < next; ++k) p.levelStart += expAt(k);
    p.levelEnd = p.levelStart + expAt(next);
    p.levelSpan = expAt(next);
    p.toNext = p.levelEnd - exp;
    const std::int32_t into = exp - p.levelStart;
    if (into >= 0) {
        p.percent = std::clamp<std::int32_t>(into * 100 / (p.levelSpan == 0 ? 1 : p.levelSpan), 0, 100);
        p.intoLevel = into;
    }
    return p;
}

int rankForTotal(std::int32_t total, GameVersion v) {
    int c = 2;
    while (c < 0x33 && sdS32(0x6252fc + std::uint32_t(c) * 4, v) <= total) ++c;
    return c - 1;
}

// FUN_004d58e0 + FUN_004d5850 (equipment value) + FUN_004d5590 (rank).
UnitSummary unitSummary(const GameWork& w, GameVersion v, int unit) {
    const std::uint32_t rec = GameWork::unitRecord(unit);
    UnitSummary s;
    for (std::uint32_t i = 0; i < 7; ++i) {
        const std::int32_t exp = w.s32(rec + 0x10 + i * 8);
        s.stats[i] = statProgress(w, w.u8(rec + kUnitStats + i * 8), exp);
        s.statExp += exp;
    }
    for (std::uint32_t k = 0; k < 0x54; ++k) {
        const int state = w.u8(rec + kUnitSlots + 8 + k * 0xc);
        if (state == 3 || state == 5 || state == 6) s.equipValue += sdS32(0x6c2e08 + (k + 1) * 0x48);
    }
    s.total = s.statExp + s.equipValue;
    s.toCap = std::max<std::int32_t>(0, sdS32(0x6253c4, v) - s.total);
    s.rank = static_cast<std::uint8_t>(rankForTotal(s.total, v));
    s.score = w.s32(GameWork::unitProgress(unit) + kScore);
    return s;
}

// ------------------------------------------------------- story values ---
namespace {
struct ValueBlock { std::uint32_t base, s16, u8, bits; std::uint32_t limit[4]; };
constexpr ValueBlock kGlobalValues{0x7f7c18, 0x100, 0x200, 0x300, {64, 128, 256, 2048}};
ValueBlock mapBlock(const GameWork& w, int map) {
    if (map == -1 || map == 0xffff) map = w.s16(stage::kMap);
    return {0x7f8020 + std::uint32_t(map) * 0xb0, 0x28, 0x50, 0x78, {10, 20, 40, 320}};
}
bool mapOk(const GameWork& w, int map) {
    if (map == -1 || map == 0xffff) map = w.s16(stage::kMap);
    return map >= 0 && map < 400;
}
std::int32_t readBlock(const GameWork& w, const ValueBlock& b, int type, std::uint32_t i) {
    if (type < 0 || type > 3 || i >= b.limit[type]) return 0;
    switch (type) {
        case 0: return w.s32(b.base + 4 * i);
        case 1: return w.s16(b.base + b.s16 + 2 * i);
        case 2: return static_cast<std::int8_t>(w.u8(b.base + b.u8 + i));
        default: return std::int32_t(w.u32(b.base + b.bits + 4 * (i >> 5)) >> (31 - (i & 31)) & 1);
    }
}
void writeBlock(GameWork& w, const ValueBlock& b, int type, std::uint32_t i, std::int32_t v) {
    if (type < 0 || type > 3 || i >= b.limit[type]) return;
    switch (type) {
        case 0: w.set32(b.base + 4 * i, v); break;
        case 1: w.set16(b.base + b.s16 + 2 * i, static_cast<std::int16_t>(v)); break;
        case 2: w.set8(b.base + b.u8 + i, static_cast<std::uint8_t>(v)); break;
        default: {
            const std::uint32_t at = b.base + b.bits + 4 * (i >> 5), bit = 1u << (31 - (i & 31));
            w.set32(at, std::int32_t(v != 0 ? (w.u32(at) | bit) : (w.u32(at) & ~bit)));
            break;
        }
    }
}
} // namespace

std::int32_t globalValue(const GameWork& w, int type, std::uint32_t index) { return readBlock(w, kGlobalValues, type, index); }
void setGlobalValue(GameWork& w, int type, std::uint32_t index, std::int32_t value) { writeBlock(w, kGlobalValues, type, index, value); }
std::int32_t mapValue(const GameWork& w, int map, int type, std::uint32_t index) {
    return mapOk(w, map) ? readBlock(w, mapBlock(w, map), type, index) : 0;
}
void setMapValue(GameWork& w, int map, int type, std::uint32_t index, std::int32_t value) {
    if (mapOk(w, map)) writeBlock(w, mapBlock(w, map), type, index, value);
}
namespace {
constexpr ValueBlock kBattleValues{0x809348, 0x28, 0x50, 0x78, {10, 20, 40, 320}};
}
std::int32_t battleValue(const GameWork& w, int type, std::uint32_t index) { return readBlock(w, kBattleValues, type, index); }
void setBattleValue(GameWork& w, int type, std::uint32_t index, std::int32_t value) {
    writeBlock(w, kBattleValues, type, index, value);
}

// FUN_004e1a10: 0x7f7c18 (0x11730: the values, FUN_004e1ad0's first 0x400
// and FUN_004e1b20's 0x7f801c block with them; FUN_004e3120 clears the
// byte at 0x7f8018 and calls FUN_0049e8b0, which does nothing), the units
// 0x7e17e8 (0x12ca0), the party records 0x7f4518 (0x3700), the stage block
// 0x7f4488 (0x8c), then the player's name at 0x809694 (0x100).
void bootReset(GameWork& w) {
    w.fill(0x7f7c18, 0x11730, 0);
    w.fill(0x7f801c, 0x11304, 0);
    w.fill(0x7e17e8, 0x12ca0, 0);
    w.fill(stage::kParty, 0x3700, 0);
    w.fill(stage::kBase, stage::kSize, 0);
    w.fill(0x809694, 0x100, 0);
    static constexpr char kName[] = "PlayerName";
    for (std::uint32_t i = 0; i < sizeof kName; ++i) w.set8(0x809694 + i, std::uint8_t(kName[i]));
}

// ------------------------------------------------------ chained rounds ---
namespace {
constexpr std::uint32_t kRoundMembers = 0x8093fa, kRoundCount = 0x8093f8, kRound = 0x80967a;
using stage::kParty;
}

int roundFlag(const GameWork& w) { return w.s16(stage::kRoundFlag); }

void addRoundStage(GameWork& w) {
    const auto add16 = [&](std::uint32_t at, int v) { w.set16(at, static_cast<std::int16_t>(std::min(w.s16(at) + v, 0x7fff))); };
    for (std::uint32_t i = 0; i < w.u8(stage::kPartyCount); ++i) {
        const std::uint32_t r = kRoundMembers + i * 8, p = kParty + i * 0xb0;
        add16(r, w.s16(p + 0xa8));
        add16(r + 2, w.s16(p + 0xaa));
        add16(r + 4, w.s16(p + 0xac));
        w.set8(r + 6, static_cast<std::uint8_t>(std::min(static_cast<std::int8_t>(w.u8(r + 6)) + static_cast<std::int8_t>(w.u8(p + 0xae)), 0x7f)));
    }
    static constexpr int kPoints[6] = {0, 10, 7, 5, 1, 0};
    const int rank = w.s16(stage::kRank);
    add16(kRound + 0x10, rank >= 0 && rank <= 5 ? kPoints[rank] : 0);
    if (w.s16(kRound + 0x14) < w.s16(stage::kEnemiesGone)) w.set16(kRound + 0x14, w.s16(stage::kEnemiesGone));
    add16(kRound + 0x16, w.s16(stage::kWorth));
    for (std::uint32_t k = 0; k < 0x10; ++k)
        if (w.u8(stage::kPrizes + k) != 0) w.set8(kRound + k, w.u8(kRound + k) | w.u8(stage::kPrizes + k));
    w.set16(kRoundCount, static_cast<std::int16_t>(w.s16(kRoundCount) + 1));
}

void finishRound(GameWork& w) {
    for (std::uint32_t i = 0; i < w.u8(stage::kPartyCount); ++i) {
        const std::uint32_t r = kRoundMembers + i * 8, p = kParty + i * 0xb0;
        w.set16(p + 0xa8, w.s16(r));
        w.set16(p + 0xaa, w.s16(r + 2));
        w.set16(p + 0xac, w.s16(r + 4));
        w.set8(p + 0xae, w.u8(r + 6));
    }
    const int stages = w.s16(kRoundCount);
    const int avg = stages != 0 ? w.s16(kRound + 0x10) / stages : 0;
    w.set16(stage::kRank, static_cast<std::int16_t>(avg >= 10 ? 1 : avg >= 7 ? 2 : avg >= 5 ? 3 : avg >= 1 ? 4 : 5));
    w.set16(stage::kTimeGrade, w.s16(kRound + 0x12));
    w.set16(stage::kEnemiesGone, w.s16(kRound + 0x14));
    w.set16(stage::kWorth, w.s16(kRound + 0x16));
    for (std::uint32_t k = 0; k < 0x10; ++k) w.set8(stage::kPrizes + k, w.u8(kRound + k));
}

} // namespace ch::logic
