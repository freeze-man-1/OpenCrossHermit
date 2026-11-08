// ============================================================================
// unit_stats.cpp
// ============================================================================
#include "unit_stats.hpp"
#include "game_logic.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ch::stats {

using logic::sdS16;
using logic::sdS32;
using logic::sdU8;

namespace {
constexpr std::uint32_t kSkills = 0x6c2dc8;   // 0x48-byte skill records
constexpr std::uint32_t kItems  = 0x6d5118;   // 0x38-byte item records
constexpr std::uint32_t kJobs   = 0x6b2d8a;   // 0x40-byte job rows (offsets below from 0x6b2d8a)

// One `a + stat * b / 100.0` term of a skill handler: a and b are s16 at
// those skill-record offsets, stat a unit-record level byte, `out` the s16
// it writes in the 0x24-byte skill bonus.
struct Term { std::uint8_t a, b, stat, out; bool negate; };
struct Handler { int skill; std::vector<Term> terms; };

// The 24 handlers of DAT_00738998 (generated from the decompile; the four
// irregular ones, 0x11 / 0x1e / 0x51 / 0x53, are in skillEffect()).
const std::vector<Handler>& handlers() {
    static const std::vector<Handler> k{
    {0x01, {}},   // FUN_004db4c0
    {0x03, {{0x20, 0x22, 0xc, 0xa, false}, {0x24, 0x26, 0xc, 0x8, false}, {0x28, 0x2a, 0xc, 0xe, true}}},   // FUN_004db500
    {0x05, {{0x20, 0x22, 0xc, 0x10, false}}},   // FUN_004db690
    {0x08, {{0x20, 0x22, 0x14, 0xa, false}, {0x24, 0x26, 0x14, 0x8, false}, {0x28, 0x2a, 0x14, 0x6, false}, {0x2c, 0x2e, 0x14, 0xc, false}, {0x30, 0x32, 0x14, 0xe, false}}},   // FUN_004db730
    {0x09, {{0x20, 0x22, 0x14, 0x0, false}, {0x24, 0x26, 0x1c, 0x2, false}, {0x28, 0x2a, 0x14, 0x4, false}, {0x2c, 0x2e, 0x14, 0xa, false}, {0x30, 0x32, 0x14, 0x8, false}, {0x34, 0x36, 0x14, 0x6, false}}},   // FUN_004db930
    {0x0d, {{0x20, 0x22, 0x3c, 0x12, false}, {0x24, 0x26, 0x34, 0x14, false}}},   // FUN_004dbb80
    {0x12, {{0x20, 0x22, 0x1c, 0x0, false}, {0x24, 0x26, 0x1c, 0x2, false}, {0x28, 0x2a, 0x1c, 0x4, false}, {0x2c, 0x2e, 0x1c, 0xa, false}, {0x30, 0x32, 0x1c, 0x8, false}, {0x34, 0x36, 0x1c, 0x6, false}}},   // FUN_004dbdb0
    {0x1b, {{0x20, 0x22, 0x1c, 0x6, false}}},   // FUN_004dc000
    {0x1c, {{0x20, 0x22, 0x1c, 0x0, false}, {0x24, 0x26, 0x1c, 0x2, false}, {0x28, 0x2a, 0x1c, 0x4, false}, {0x2c, 0x2e, 0x1c, 0xa, false}, {0x30, 0x32, 0x1c, 0x8, false}, {0x34, 0x36, 0x1c, 0x6, false}}},   // FUN_004dc0a0
    {0x21, {{0x20, 0x22, 0x24, 0xc, false}, {0x24, 0x26, 0x24, 0xe, false}}},   // FUN_004dc390
    {0x26, {{0x20, 0x22, 0x24, 0xa, false}, {0x24, 0x26, 0x24, 0x8, false}, {0x28, 0x2a, 0x24, 0x6, false}, {0x2c, 0x2e, 0x24, 0xc, false}, {0x30, 0x32, 0x24, 0xe, false}}},   // FUN_004dc480
    {0x28, {{0x20, 0x26, 0x24, 0x12, false}, {0x24, 0x26, 0x24, 0x14, false}}},   // FUN_004dc680
    {0x38, {{0x20, 0x22, 0xc, 0xa, false}, {0x24, 0x26, 0x34, 0x8, false}, {0x28, 0x2a, 0x14, 0x6, false}, {0x2c, 0x2e, 0x2c, 0xc, false}, {0x30, 0x32, 0x3c, 0xe, false}}},   // FUN_004dc770
    {0x3d, {{0x20, 0x22, 0x2c, 0x1c, false}}},   // FUN_004dc970
    {0x3e, {}},   // FUN_004dca10
    {0x44, {{0x20, 0x22, 0x2c, 0x1e, false}}},   // FUN_004dca50
    {0x48, {{0x20, 0x22, 0x2c, 0x2, false}, {0x24, 0x26, 0x2c, 0x4, false}}},   // FUN_004dcaf0
    {0x4a, {{0x20, 0x22, 0x2c, 0x2, false}, {0x24, 0x26, 0x2c, 0x4, false}}},   // FUN_004dcbe0
    {0x4e, {{0x20, 0x22, 0x14, 0x0, false}}},   // FUN_004dccd0
    {0x54, {}},   // FUN_004dcec0
    };
    return k;
}

int clampInt(std::int64_t v, std::int64_t lo, std::int64_t hi) {   // FUN_004db650 / FUN_004d1ba0
    return static_cast<int>(std::clamp(v, lo, hi));
}

// __ftol: truncate toward zero.
int ftol(double v) { return static_cast<int>(std::trunc(v)); }

// The x87 term of every handler: fild a; fild stat; fimul b; fdiv 100.0f; faddp.
int term(GameVersion v, std::uint32_t rec, const Record& r, std::uint8_t a, std::uint8_t b, std::uint8_t stat) {
    return ftol(double(sdS16(rec + a, v)) + double(int(r.u8(stat)) * sdS16(rec + b, v)) / 100.0);
}

// Handler output (0x24 bytes) for one skill.
Bytes<0x24> skillEffect(int skill, const Record& r, GameVersion v) {
    Bytes<0x24> o;
    const std::uint32_t rec = kSkills + std::uint32_t(skill) * 0x48;
    const auto clamp16 = [](int x) { return clampInt(x, -32000, 32000); };
    switch (skill) {
        case 0x11:   // FUN_004dbc70 (its clamps to 0..999 are FUN_004dbd70: FUN_004db650(x, 0, hi))
            o.set16(0x12, clamp16(term(v, rec, r, 0x20, 0x26, 0x1c)));
            o.set16(0x14, 0);
            return o;
        case 0x1e:   // FUN_004dc2f0
            o.set16(0x12, 0);
            o.set16(0x14, clamp16(term(v, rec, r, 0x20, 0x22, 0x24)));
            return o;
        case 0x51:   // FUN_004dcd70 (FUN_004dbd70 to 0..0x7fffffff)
            o.set32(0x20, clampInt(sdS16(rec + 0x20, v), 0, 0x7fffffff));
            return o;
        case 0x53:   // FUN_004dcde0: the second result is computed and dropped
            o.set16(0x12, clamp16(term(v, rec, r, 0x20, 0x22, 0x3c)));
            o.set16(0x14, -32000);
            return o;
        default: break;
    }
    for (const Handler& h : handlers()) {
        if (h.skill != skill) continue;
        for (const Term& t : h.terms) {
            const int x = term(v, rec, r, t.a, t.b, t.stat);
            o.set16(t.out, clamp16(t.negate ? -x : x));
        }
    }
    return o;
}

// FUN_004dcf00 (param_4 = 0): add one skill's effect, s16 fields saturating
// at +-32000 and the s32 at +0x20 at 0x7fffffff / 0x80000030. Field +0x18
// is the skill record's +8, not the handler's.
void addSkill(Bonus& bonus, int skill, const Record& r, GameVersion v) {
    if (skill == 0 || skill == 0xffff) return;
    Bytes<0x24> e = skillEffect(skill, r, v);
    e.set16(0x18, sdS16(kSkills + std::uint32_t(skill) * 0x48 + 8, v));
    for (std::size_t f = 0; f < 0x20; f += 2) {
        const int s = bonus.s16(f) + e.s16(f);
        bonus.set16(f, s >= 32000 ? 32000 : s < -32000 ? -32000 : s);
    }
    const std::int64_t s = std::int64_t(bonus.s32(0x20)) + e.s32(0x20);
    bonus.set32(0x20, s >= 0x7fffffff ? 0x7fffffff : s < -0x7fffffd0 ? std::int64_t(std::int32_t(0x80000030u)) : s);
}

// FUN_004dd510 (param_4 = 0): add one item's effect into the bonus at +0x24.
void addItem(Bonus& bonus, int item, GameVersion v) {
    if (item == 0 || item == 0xffff) return;
    const std::uint32_t rec = kItems + std::uint32_t(item) * 0x38;
    constexpr std::size_t B = 0x24;
    const auto add16 = [&](std::size_t f, std::uint32_t src, int cap) {
        const std::uint32_t s = std::uint32_t(bonus.u16(B + f)) + std::uint16_t(sdS16(src, v));
        bonus.set16(B + f, s < std::uint32_t(cap) ? int(s) : cap);
    };
    const auto add8 = [&](std::size_t f, std::uint32_t src, int cap) {
        const int s = bonus.u8(B + f) + sdU8(src, v);
        bonus.set8(B + f, s < cap ? s : cap);
    };
    const auto sticky16 = [&](std::size_t f, std::uint32_t src) {   // 0xffff means unlimited and sticks
        const std::uint16_t cur = bonus.u16(B + f), add = std::uint16_t(sdS16(src, v));
        if (cur == 0xffff || add == 0xffff) bonus.set16(B + f, 0xffff);
        else bonus.set16(B + f, std::uint32_t(cur) + add < 0xffff ? cur + add : 0xffff);
    };
    add16(0x00, rec + 0x22, 999); add16(0x02, rec + 0x24, 999); add16(0x04, rec + 0x26, 999);
    add16(0x06, rec + 0x0c, 999); add16(0x08, rec + 0x0e, 999); add16(0x0a, rec + 0x10, 999);
    add16(0x0c, rec + 0x12, 999);
    add8(0x0e, rec + 0x18, 0x20);
    add16(0x10, rec + 0x1a, 999);
    add8(0x12, rec + 0x21, 0x7f); add8(0x13, rec + 0x1c, 0xff); add8(0x14, rec + 0x1e, 100);
    add8(0x15, rec + 0x1d, 0xff); add8(0x16, rec + 0x1f, 0xff); add8(0x17, rec + 0x20, 100);
    add8(0x1c, rec + 0x34, 100);
    add16(0x1e, rec + 0x30, 999); add16(0x20, rec + 0x32, 999);
    add8(0x22, rec + 0x28, 0x7f);
    sticky16(0x18, rec + 0x14);
    sticky16(0x1a, rec + 0x16);
    const std::int32_t cur = bonus.s32(B + 0x24), add = sdS32(rec + 0x2c, v);
    if (cur == 0xffff || add == 0xffff) bonus.set32(B + 0x24, 0xffff);
    else bonus.set32(B + 0x24, std::int64_t(cur) + add < 0xffff ? std::int64_t(cur) + add : 0xffff);
}
} // namespace

Record readRecord(const GameWork& w, int unit) {
    Record r;
    const std::uint32_t base = GameWork::unitRecord(unit);
    for (std::size_t i = 0; i < r.b.size(); ++i) r.b[i] = w.u8(base + std::uint32_t(i));
    return r;
}

Bonus equipmentBonus(const Record& r, GameVersion v) {
    Bonus bonus;
    for (std::size_t k = 0; k < 8; ++k) addSkill(bonus, r.u16(0x52 + k * 2), r, v);
    for (std::size_t k = 0; k < 8; ++k) addItem(bonus, r.u16(0x62 + k * 2), v);
    return bonus;
}

void derive(const Record& r, Derived& o, Bonus& B, GameVersion v) {
    B = equipmentBonus(r, v);
    bool hasSkill1 = false, hasSkill5 = false;
    for (std::size_t k = 0; k < 8; ++k) {
        const int s = r.s16(0x52 + k * 2);
        if (s == 1) hasSkill1 = true;
        else if (s == 5) hasSkill5 = true;
    }
    o.set16(0, clampInt(r.s16(0), 0, 0x3ff));
    o.set16(2, clampInt(r.s16(0), 0, 0x3ff));
    std::array<int, 7> st{};
    for (std::size_t i = 0; i < 7; ++i) {
        st[i] = clampInt(r.u8(0xc + i * 8), 1, 0x87);
        o.set8(4 + i, st[i]);
    }
    const int job = clampInt(r.s16(6), 0, 0x3ff);
    o.set16(0xc, job);
    const int rank = clampInt(r.u8(0x50), 0, 99);
    o.set8(0xe, rank);
    o.set8(0xf, 0);
    const std::uint32_t J = kJobs + std::uint32_t(job) * 0x40;   // job row, offsets from 0x6b2d8a
    const auto ju8 = [&](std::uint32_t off) { return int(sdU8(J + off, v)); };
    const auto ju16 = [&](std::uint32_t off) { return int(std::uint16_t(sdS16(J + off, v))); };
    o.set16(0x10, ju8(0x1e));
    o.set16(0x12, 0);
    // Short results go through a ushort before the clamp, as in the original.
    const auto s16of = [](std::int64_t x) { return int(std::int16_t(std::uint16_t(x))); };

    o.set16(0x14, clampInt(s16of(std::int32_t((st[5] * 3 + st[0] + st[1] + B.u16(0x2a) + B.s16(0x10)) * ju16(4)) / 100), 1, 999));
    o.set16(0x18, clampInt(s16of(ftol(double(ju16(6) - st[5]) - st[0] * 0.5 - B.u16(0x2c) - B.s16(0x12))), 0x1e, 999));
    o.set16(0x1a, clampInt(s16of(std::int32_t((st[6] * 3 + st[4] + st[3] + B.u16(0x2e) + B.s16(0x1e)) * ju16(8)) / 100), 1, 999));
    o.set16(0x1e, clampInt(s16of(ftol(double(ju16(0xa) - st[6]) - st[3] * 0.5 - B.u16(0x30) - B.s16(0x14))), 0x1e, 999));
    const std::int64_t time = std::int64_t(B.u16(0x3c)) * 0xe10 +
                              std::int64_t(sdS32(0x6e4528 + std::uint32_t(clampInt(rank, 0, 0x32)) * 4, v)) * 0x3c + B.s32(0x20);
    o.set32(0x20, clampInt(time, 1, 0x57030));
    o.set32(0x28, 0);
    o.set8(0x2c, clampInt(s16of(ftol(ju8(0x10) + st[2] * 0.1 + B.u8(0x32))), 0, 0x20));
    o.set16(0x2e, clampInt(s16of(sdS16(J + 0x12, v) + (ju16(0x12) >> 1) * st[1] / 100 + B.s16(0x34) + B.s16(0x1c)), 0, 999));
    o.set8(0x30, clampInt(ju8(0x14), 0, 7));

    // The job's basic attack is a skill record.
    const std::uint32_t W = kSkills + std::uint32_t(ju16(0x16)) * 0x48;
    const auto wu8 = [&](std::uint32_t off) { return int(sdU8(W + off, v)); };
    const auto ws16 = [&](std::uint32_t off) { return int(sdS16(W + off, v)); };
    o.set16(0x32, clampInt(std::uint16_t(ws16(0xa)), 0, 0xffff));
    o.set16(0x34, clampInt(std::uint16_t(ws16(0xe)), 0, 0xffff));
    o.set8(0x36, clampInt(wu8(0x10), 0, 0x7f));
    // Power (+0x3c) and attack (+0x3a) from the class's main stat.
    const auto power = [&](int stat) {
        o.set16(0x3c, clampInt(s16of(ws16(0x24) * stat / 100 + B.s16(0x28) + B.s16(4)), 0, 999));
        o.set16(0x3a, clampInt(s16of(ws16(0x20) * stat / 100 + B.s16(0x26) + B.s16(2) + (o.u16(0x3c) >> 1)), 0, 999));
    };
    switch (ju8(0) - 1) {
        case 0:
            o.set8(0x37, clampInt(std::uint16_t(wu8(0x12) + B.u8(0x36)), 0, 0x7f));
            o.set16(0x38, clampInt(s16of(ftol(wu8(0x14) + st[0] * 0.5 + B.u16(0x24) + B.s16(0))), 0, 999));
            power(st[0]);
            break;
        case 1:
            o.set8(0x37, clampInt(std::uint16_t(wu8(0x12) + B.u8(0x36)), 0, 0x7f));
            o.set16(0x38, clampInt(s16of(wu8(0x14) + st[1] + B.s16(0x24) + B.s16(0)), 0, 999));
            power(st[1]);
            break;
        case 2:
            o.set16(0x38, clampInt(s16of(ftol(wu8(0x14) + st[2] * 0.2 + B.u16(0x24) + B.s16(0))), 0, 999));
            if (r.u8(0x72) == 1) {
                o.set8(0x37, clampInt(s16of(ftol(wu8(0x12) + st[2] * 0.08 + st[0] * 0.03 + B.u8(0x36))), 0, 0x7f));
                power(st[2] + st[0] / 10);
            } else if (r.u8(0x72) == 2) {
                o.set8(0x37, clampInt(s16of(ftol(wu8(0x12) + st[2] * 0.08 + B.u8(0x36))), 0, 0x7f));
                power(st[2]);
            }
            break;
        case 3:
            o.set8(0x37, clampInt(s16of(ftol(wu8(0x12) + st[3] * 0.1 + B.u8(0x36))), 0, 0x7f));
            power(st[3]);
            o.set16(0x38, 999);
            break;
        case 4:
            o.set8(0x37, clampInt(s16of(ftol(wu8(0x12) + st[4] * 0.1 + B.u8(0x36))), 0, 0x7f));
            power(st[4]);
            o.set16(0x38, 999);
            break;
        default: break;
    }
    o.set16(0x3e, clampInt(ws16(2), 0, 999));
    o.set8(0x40, clampInt(wu8(5), 0, 7));
    o.set8(0x41, clampInt(B.s8(0x40), 0, 100));
    o.set16(0x42, clampInt(B.s16(0x42), 0, 999));
    o.set8(0x44, clampInt(wu8(0x16), 0, 0x7f));
    o.set8(0x45, clampInt(wu8(0x1a), 0, 0x7f));
    o.set32(0x48, clampInt(B.s32(0x48), 0, 0x7fffffff));
    o.set8(0x4c, clampInt(s16of(ftol(ju8(0x18) * st[1] / 100 + st[1] * 0.25 + B.u8(0x37) + B.s16(6))), 0, 0xff));
    o.set8(0x4d, clampInt(s16of(ftol(ju8(0x19) * st[0] / 100 + st[0] * 0.15 + B.u8(0x39) + B.s16(0xa))), 0, 0xff));
    o.set8(0x4e, clampInt(s16of(ftol(ju8(0x1a) + st[5] * 0.1 + st[2] * 0.05 + B.u8(0x38) + B.s16(8))), 0, 99));
    o.set8(0x4f, clampInt(s16of(ftol(ju8(0x1b) + st[4] * 0.15 + B.u8(0x3a) + B.s16(0xc))), 0, 0xff));
    o.set8(0x50, clampInt(s16of(ftol(ju8(0x1c) + st[6] * 0.05 + st[3] * 0.1 + B.u8(0x3b) + B.s16(0xe))), 0, 99));
    const int hp = o.s16(0x14);
    o.set16(0x52, clampInt(s16of(hasSkill1 ? ftol(hp * ju8(0x1f) / 100 + st[0] * 0.1) : ftol(hp * 0.1)), 0, 999));
    o.set16(0x54, hasSkill5 ? st[0] : 0);
    o.set16(0x16, o.s16(0x14));
    o.set16(0x1c, o.s16(0x1a));
    o.set32(0x24, o.s32(0x20));

    // Skill list: the job's skill slots, then item slots (| 0x8000).
    const int jobRec = r.s16(6);
    const int nSkills = sdU8(kJobs + 1 + std::uint32_t(jobRec) * 0x40, v);
    const int nItems = sdU8(kJobs + 2 + std::uint32_t(jobRec) * 0x40, v);
    for (int k = 0; k < nSkills; ++k) o.set16(0x84 + std::size_t(k) * 2, r.s16(0x52 + std::size_t(k) * 2));
    for (int k = 0; k < nItems; ++k) {
        const int item = r.u16(0x62 + std::size_t(k) * 2);
        o.set16(0x84 + std::size_t(nSkills + k) * 2, item == 0 ? 0 : item | 0x8000);
    }
    // +0x45: the strongest "0x19" effect among the equipped items.
    int best = 0, bestId = 0;
    for (std::size_t k = 0; k < 8; ++k) {
        const int item = r.u16(0x62 + k * 2);
        if (item == 0) continue;
        for (std::uint32_t j = 0; j < 3; ++j) {
            if (sdU8(kItems + 0x35 + j + std::uint32_t(item) * 0x38, v) != 0x19) continue;
            const int e = sdU8(kItems + 0x28 + std::uint32_t(item) * 0x38, v);
            const int p = sdU8(0x6f408b + std::uint32_t(e) * 0x10, v);
            if (best < p) { best = p; bestId = e; }
        }
    }
    o.set8(0x45, bestId);
}

void refreshItemState(GameWork& w, GameVersion v, std::uint32_t rec, int item) {
    const std::uint32_t at = 0x7aacaa + std::uint32_t(item) * 2;
    const std::uint32_t it = kItems + std::uint32_t(item) * 0x38;
    const int job = w.u16(rec + 6);
    const bool jobOk = sdU8(0x6e45f8 + std::uint32_t(std::uint16_t(sdS16(it + 2, v))) + std::uint32_t(job) * 0xf, v) != 0;
    const bool rankOk = sdU8(it + 0xa, v) <= w.u8(rec + 0x50);
    const bool ok = jobOk && rankOk;
    const int cur = w.u16(at);
    const auto set = [&](int state) { w.set16(at, (cur & 0xf0ff) | state << 8); };
    switch ((cur >> 8 & 0xf) - 1) {
        case 0: if (!ok) set(2); break;
        case 1: if (ok) set(1); break;
        case 2: if (!ok) set(4); break;
        case 3: if (ok) set(3); break;
        default: break;
    }
}

void refreshSkillState(GameWork& w, GameVersion v, std::uint32_t rec, int skill) {
    const int job = w.s16(rec + 6);
    const std::uint32_t slot = rec + 0xb0 + std::uint32_t(skill - 1) * 0xc + 8;
    const std::uint32_t S = kSkills + std::uint32_t(skill) * 0x48;
    const std::uint32_t row = 0x6e4158 + std::uint32_t(job) * 0xd;   // per job: level allowed per category
    const int cls = sdU8(kJobs + std::uint32_t(job) * 0x40, v);
    constexpr std::array<int, 13> kClassOf{0, 1, 2, 3, 3, 4, 4, 5, 5, 5, 5, 5, 0};
    std::array<int, 0x15> perCategory{};
    for (int k = 0; k < sdU8(kJobs + 1 + std::uint32_t(job) * 0x40, v); ++k) {
        const int s2 = w.s16(rec + 0x52 + std::uint32_t(k) * 2);
        if (s2 != 0) ++perCategory[std::size_t(sdU8(kSkills + 4 + std::uint32_t(s2) * 0x48, v)) % perCategory.size()];
    }
    const int cat = sdU8(S + 4, v) - 1;
    const int excluded = sdU8(0x6e4158 + std::uint32_t(cat + 1), v) - 1;   // row 0: the category this one excludes
    bool ok = true;
    if (sdU8(row + std::uint32_t(cat + 1), v) == 0) ok = false;
    else if (sdU8(row + std::uint32_t(cat + 1), v) < sdU8(S + 7, v)) ok = false;
    else if (excluded < 0 || perCategory[std::size_t(excluded + 1) % perCategory.size()] == 0) {
        if (sdU8(S + 6, v) == 2 && cls != kClassOf[std::min<std::size_t>(sdU8(S + 4, v), 12)]) ok = false;
    } else {
        ok = false;
    }
    switch (w.u8(slot)) {
        case 2: if (!ok) w.set8(slot, 4); break;
        case 3: if (!ok) w.set8(slot, 5); break;
        case 4: if (ok) w.set8(slot, 2); break;
        case 5: if (ok) w.set8(slot, 3); break;
        default: break;
    }
}

} // namespace ch::stats
