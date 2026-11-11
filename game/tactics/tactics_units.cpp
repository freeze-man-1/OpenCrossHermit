// ============================================================================
// tactics_units.cpp
// ============================================================================
#include "tactics_units.hpp"
#include "tactics_hud.hpp"
#include "../audio.hpp"
#include "../option/options.hpp"
#include "../state/exe_image.hpp"
#include "../state/game_logic.hpp"
#include "../ysub/ysub.hpp"
#include "../text/sjis.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <initializer_list>
#include <map>
#include <tuple>

namespace ch {

namespace {
// The battle's string tables, ch.exe / JP exe addresses. FUN_0049c9b0's
// help lines are rows of four pointers: 0x18 panel fields, 0x15 bar
// buttons, 100 skills. FUN_00467eb0 renders the others into text sprites:
// attack kinds (FUN_004671f0), unit names by kind (FUN_00467380), job
// names (FUN_00467520) and condition names by type (FUN_004676c0).
struct StringTable { std::uint32_t tw, jp; std::size_t count; };
constexpr StringTable kHelpPanel{0x61a4a8, 0x5088a0, 0x18 * 4}, kHelpBar{0x61a628, 0x508a20, 0x15 * 4},
    kHelpSkill{0x61a778, 0x508b70, 100 * 4};
constexpr StringTable kAttackKinds{0x617f40, 0x5039b0, 0x40}, kUnitNames{0x6162ac, 0x501d1c, 0x200},
    kJobNames{0x616aac, 0x50251c, 0x200}, kConditionNames{0x617aac, 0x50351c, 0x100};

const std::vector<std::string>& strings(GameVersion v, const StringTable& t) {
    return exe::strings(v, v == GameVersion::Jp ? t.jp : t.tw, t.count);
}

// The sprite texts, trailing blanks dropped (they are drawn centred).
const std::vector<std::string>& spriteTexts(GameVersion v, const StringTable& t) {
    static std::map<std::pair<int, std::uint32_t>, std::vector<std::string>> cache;
    static int generation = textGeneration();
    if (generation != textGeneration()) {
        generation = textGeneration();
        cache.clear();
    }
    auto& out = cache[{int(v), t.tw}];
    if (out.empty()) {
        out = strings(v, t);
        for (auto& x : out)
            while (!x.empty() && (x.back() == ' ' || x.back() == '\r' || x.back() == '\n')) x.pop_back();
    }
    return out;
}

constexpr std::uint8_t  kHudBank = 0x5a;            // data\Tactics\common.bin
constexpr std::uint32_t kTemplates     = 0x6f5088;   // 0x4a0-byte unit records, id < 200
constexpr std::uint32_t kEnemyTemplates = 0x72ed88;  // 0x30-byte summaries, id >= 200
constexpr std::uint32_t kJobRows       = 0x6b2d88;   // 0x40 bytes per kind
constexpr std::uint32_t kSkillRecords  = 0x6c2dc8;   // 0x48 bytes
// A skill row's fields (offsets into the 0x48 bytes).
constexpr std::uint32_t kSkillEffect = 2;           // s16: 5 heals
constexpr std::uint32_t kSkillType = 5;             // u8: 2 a spell, 3 the job's skill instead
constexpr std::uint32_t kSkillMpCost = 8;           // s16
constexpr std::uint32_t kSkillAim = 0x18;           // u8: what it is aimed at (4 a cell)
constexpr std::uint32_t kSkillAimRule = 0x19;       // u8
constexpr std::uint32_t kSkillAdded = 0x1a;         // u8: the added effect
constexpr std::uint32_t skillRow(int id) { return kSkillRecords + std::uint32_t(id) * 0x48; }
constexpr std::uint32_t kRankTime      = 0x6e4528;
constexpr std::uint32_t kMoveTypes     = 0x738c10;
constexpr std::uint32_t kEnemyTables   = 0x6a7f6c;   // u32 per map
constexpr std::uint32_t kEnemyCounts   = 0x6a85ac;   // s16 per map
constexpr std::uint32_t kGraphics      = 0x610538;   // {char* path, s16 file, u8 palette kind, s8 palette} x 0x200
constexpr std::uint32_t kCharPalettes  = 0x618090;   // s8 [kind * 0x28 + character]
constexpr std::uint32_t kDirColumn     = 0x610510;   // u8 [dir]
constexpr std::uint32_t kClipRows      = 0x60e850;   // u32 per file + 8: {u8 clip, u8 flip} [anim * 8 + column]
constexpr std::uint32_t kClips         = 0x60f070;   // u32 per file + 8: u16 set << 8 | no
constexpr std::uint32_t kRelations     = 0x618568;   // s8 [viewer * 16 + team]
constexpr std::uint32_t kJobSkill      = 0x6b2da0;   // s16 [kind * 0x20]: the job's own skill
constexpr std::uint32_t kSkillAnims    = 0x611538;   // 0x10 bytes per skill: s8 cast, attack, after clips
constexpr std::uint32_t kFallAnims     = 0x6149d0;   // 10 bytes per kind: s8 fall clip, ...
constexpr std::uint32_t kRoutes        = 0x6acb88;   // u32 per map: {s32 count, u32 rows} per route
constexpr std::uint32_t kStepOffsets   = 0x5ff95c;   // {s16 dx, dy} per numpad direction
constexpr std::uint32_t kWanderTurns   = 0x618cc8;   // s8 [3][12]: straight on, turned one way, the other
constexpr std::uint32_t kFleeTurns     = 0x618dc8;   // s8 [dir * 3 + 0..2]: away from the threat

std::uint32_t sdU32(std::uint32_t va, GameVersion v) { return static_cast<std::uint32_t>(logic::sdS32(va, v)); }
std::uint16_t sdU16(std::uint32_t va, GameVersion v) { return static_cast<std::uint16_t>(logic::sdS16(va, v)); }
int sdS8(std::uint32_t va, GameVersion v) { return static_cast<std::int8_t>(logic::sdU8(va, v)); }

std::string sdString(std::uint32_t va, GameVersion v) {
    std::string s;
    for (std::uint32_t i = 0; i < 0x40; ++i) {
        const char c = static_cast<char>(logic::sdU8(va + i, v));
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}

// FUN_004e0a00
void clampDerived(stats::Derived& d) {
    const auto u16max = [&](std::size_t o, int hi) { if (d.u16(o) > hi) d.set16(o, hi); };
    const auto s16range = [&](std::size_t o, int lo, int hi) { d.set16(o, std::clamp<int>(d.s16(o), lo, hi)); };
    const auto s32range = [&](std::size_t o, std::int32_t lo, std::int32_t hi) { d.set32(o, std::clamp(d.s32(o), lo, hi)); };
    const auto u8max = [&](std::size_t o, int hi) { if (d.u8(o) > hi) d.set8(o, hi); };
    u16max(0, 0x3ff);
    u16max(2, 0x3ff);
    for (std::size_t o = 4; o <= 0xa; ++o) {
        if (d.u8(o) == 0) d.set8(o, 1);
        else u8max(o, 0x87);
    }
    u16max(0xc, 0x3ff);
    u8max(0xe, 0x32);
    u8max(0xf, 7);
    s16range(0x14, 1, 999);
    s16range(0x16, 0, 999);
    s16range(0x18, 1, 999);
    s16range(0x1a, 1, 999);
    s16range(0x1c, 0, 999);
    s16range(0x1e, 1, 999);
    s32range(0x20, 300, 0x57030);
    s32range(0x24, 0, 0x57030);
    s32range(0x28, 300, 0x57030);
    u8max(0x2c, 0x20);
    u16max(0x2e, 999);
    u8max(0x30, 7);
    u8max(0x36, 0x7f);
    u8max(0x37, 0x7f);
    for (std::size_t o = 0x38; o <= 0x3e; o += 2) u16max(o, 999);
    u8max(0x40, 7);
    u8max(0x41, 100);
    u16max(0x42, 999);
    u8max(0x44, 0x7f);
    u8max(0x45, 0x7f);
    s32range(0x48, 0, 0x57030);
    u8max(0x4e, 100);
    u8max(0x50, 100);
    u16max(0x52, 999);
    u16max(0x54, 999);
    u16max(0x58, 0x3ff);
    u16max(0x5a, 0x3ff);
    s32range(0x5c, 0, 0x57030);
    u16max(0x66, 0x3ff);
    u16max(0x68, 0x3ff);
    s32range(0x6c, 0, 0x57030);
    u16max(0x72, 0x3ff);
    u16max(0x74, 0x3ff);
    if (static_cast<std::uint32_t>(d.s32(0x78)) > 0x57030) d.set32(0x78, 0x57030);
    s32range(0x80, 0, 0x57030);
    u8max(0x9d, 1);
    u8max(0x9e, 7);
    u8max(0x9f, 3);
    u8max(0xa0, 3);
    u8max(0xa4, 5);
}
} // namespace

// ------------------------------------------------------------ records ---
namespace tactics {

bool traceUnits() {
    static const bool on = SDL_getenv("CH_UNITS") != nullptr;
    return on;
}

Compact compactRecord(int id, GameVersion v) {
    Compact c;
    if (id >= 200) {
        const std::uint32_t r = kEnemyTemplates + std::uint32_t(id - 200) * 0x30;
        for (std::uint32_t k = 0; k < 0x30; ++k) c.set8(k, logic::sdU8(r + k, v));
        return c;
    }
    const std::uint32_t t = kTemplates + std::uint32_t(id) * 0x4a0;
    c.set16(0, logic::sdS16(t, v));
    c.set16(2, logic::sdS16(t + 6, v));
    c.set8(4, logic::sdU8(t + 8, v));
    for (std::uint32_t s = 0; s < 7; ++s) c.set8(5 + s, logic::sdU8(t + 0xc + 8 * s, v));   // stat levels
    c.set8(0xc, logic::sdU8(t + 0x50, v));
    c.set8(0x2e, logic::sdU8(t + 0x72, v));
    return c;
}

void deriveCompact(const Compact& c, stats::Derived& o, GameVersion v) {
    const std::uint32_t job = kJobRows + std::uint32_t(c.s16(2)) * 0x40;
    const std::uint32_t sk  = skillRow(logic::sdS16(job + 0x18, v));
    const auto J  = [&](std::uint32_t off) { return int(logic::sdU8(job + off, v)); };
    const auto JS = [&](std::uint32_t off) { return int(logic::sdS16(job + off, v)); };
    const auto JU = [&](std::uint32_t off) { return int(sdU16(job + off, v)); };
    const auto K  = [&](std::uint32_t off) { return int(logic::sdU8(sk + off, v)); };
    const auto KS = [&](std::uint32_t off) { return int(logic::sdS16(sk + off, v)); };
    const auto KU = [&](std::uint32_t off) { return int(sdU16(sk + off, v)); };
    const int l14 = c.u8(5), l18 = c.u8(6), l1c = c.u8(7), l20 = c.u8(8), l24 = c.u8(9), l28 = c.u8(0xa), l2c = c.u8(0xb);
    const int kind = J(2) - 1;                     // 0..9, 5..9 repeat 0..4
    const int k5 = kind >= 0 && kind < 10 ? kind % 5 : -1;
    const int sub = c.u8(0x2e);                    // +0x2e: kind 2 variant 1..3

    o = {};
    o.set16(0, c.s16(0));
    o.set16(2, c.s16(0));
    for (std::size_t s = 0; s < 7; ++s) o.set8(4 + s, c.u8(5 + s));
    o.set16(0xc, c.s16(2));
    o.set8(0xe, c.u8(0xc));
    o.set8(0xf, 1);
    const int hp = std::int16_t(l28 * 300 / 100 + l14 + l18);
    o.set16(0x14, hp * JU(6) / 100);
    o.set16(0x16, o.s16(0x14));
    o.set16(0x18, JS(8) - l28 - l14 * 0x32 / 100);
    o.set16(0x1a, l2c * 300 / 100 + l24 + l20);
    o.set16(0x1c, o.s16(0x1a));
    o.set16(0x1e, JS(0xc) - l2c - l20 * 0x32 / 100);
    o.set32(0x20, std::int64_t(logic::sdS32(kRankTime + std::uint32_t(o.u8(0xe)) * 4, v)) * 0x3c);
    o.set32(0x24, o.s32(0x20));
    o.set32(0x28, (c.u8(0xc) * 10 / 100 + 10) * 0xe10);
    o.set8(0x2c, J(0x12) + l1c * 10 / 100);
    o.set16(0x2e, JS(0x14) + ((JU(0x14) >> 1) * l18) / 100);
    o.set8(0x30, logic::sdU8(kMoveTypes + std::uint32_t(J(0x16)), v));
    o.set16(0x32, KS(10));
    o.set16(0x34, KS(0xe));
    o.set8(0x36, K(0x10));

    int t = 0;                                     // local_10, carried between the switches
    switch (k5) {
        case 0: case 1: t = K(0x12); break;
        case 2: case 3: t = K(0x12) + l20 * 10 / 100; break;
        case 4:         t = K(0x12) + l24 * 10 / 100; break;
        default: SDL_Log("[tactics] unit kind %d: bad class %d", c.s16(2), kind + 1); break;
    }
    o.set8(0x37, t);
    if (K(0x18) == 6) o.set8(0x37, K(0x16));
    switch (k5) {
        case 0: t = K(0x14) + l14 * 0x32 / 100; break;
        case 1: t = K(0x14) + l18; break;
        case 2: t = K(0x14) + l1c * 0x4b / 100; break;
        case 3: case 4: t = 100; break;
        default: break;
    }
    o.set16(0x38, t);
    const auto power = [&](int base) {
        switch (k5) {
            case 0: return base * l14 / 100;
            case 1: return base * l18 / 100;
            case 2:
                if (sub == 2) return base * l1c / 100;
                if (sub == 1 || sub == 3) return base * (l1c + l14 / 10) / 100;
                return t;                          // the original asserts and keeps local_10
            case 3: return base * l20 / 100;
            case 4: return base * l24 / 100;
            default: return t;
        }
    };
    t = power(KS(0x20));
    o.set16(0x3a, t);
    t = power(KS(0x24));
    o.set16(0x3c, t);
    o.set16(0x3e, KS(2));
    o.set8(0x40, K(5));
    o.set8(0x41, 100);
    o.set16(0x42, KS(0x28) + KS(0x2a) * 0x32 / 100);
    o.set8(0x44, K(0x16));
    o.set8(0x45, K(0x1a));
    if (k5 >= 0) o.set32(0x48, KU(0x1c) + KU(0x1e) * 0x32 / 100);
    o.set8(0x4c, J(0x1a) * l18 / 100 + l18 * 0x19 / 100);
    o.set8(0x4d, J(0x1b) * l14 / 100 + l14 * 0xf / 100);
    o.set8(0x4e, J(0x1c) + l28 * 10 / 100 + l1c * 5 / 100);
    o.set8(0x4f, J(0x1d) + l24 * 0xf / 100);
    o.set8(0x50, J(0x1e) + l2c * 5 / 100 + l20 * 10 / 100);
    o.set16(0x52, o.s16(0x14) * J(0x21) / 100);
    for (std::size_t k = 0; k < 8; ++k) o.set16(0x84 + 2 * k, c.s16(0xe + 2 * k));   // skills
    o.set8(0xa4, 5);
    clampDerived(o);
    if (o.u8(0x37) > 0x20) o.set8(0x37, 0x20);
    if (o.u8(0x44) > 0x20) o.set8(0x44, 0x20);
}

void enemyData(std::uint32_t rec, stats::Derived& out, GameVersion v) {
    const auto b = [&](std::uint32_t off) { return int(logic::sdU8(rec + off, v)); };
    deriveCompact(compactRecord(logic::sdS16(rec, v), v), out, v);
    out.set8(0xf, b(2));
    out.set8(0x94, b(3));
    out.set8(0x96, b(4));
    out.set8(0x97, b(5));
    out.set8(0x98, b(6));
    out.set8(0x9b, b(7));
    out.set8(0x9c, b(8));
    out.set8(0x9d, b(9));
    out.set8(0x9e, b(0xa));
    out.set8(0x9f, b(0xb));
    out.set8(0xa0, b(0xc));
    out.set8(0xa2, b(0xd));
    out.set8(0xa3, b(0xe));
    out.set16(0xa8, logic::sdS16(rec + 0x10, v));
    const int team = b(3);
    if (team != 0 && (team < 4 || team > 7)) SDL_Log("[tactics] enemy record %06x: team %d", rec, team);
    out.set8(0xa4, team);
}

} // namespace tactics

// -------------------------------------------------------------- units ---
namespace {
constexpr std::uint32_t kDirByAngleA = 0x618da4;   // u8 [angle / 11]: numpad direction
constexpr std::uint32_t kDirByAngleB = 0x618d80;   //   the diagonal-leaning table

// The angle of (dx, dy) in degrees, 0..360, as FUN_00470250 takes it: the
// CRT's double atan2 (FUN_005711b0) over the float 0.017453289, kept as a
// float. Double, not float, maths: the network peers (native and web) must
// agree on every truncation, and the double libm results round to the same
// float far more reliably than atan2f does across C libraries.
float degrees(int dy, int dx) {
    float deg = static_cast<float>(std::atan2(double(dy), double(dx)) / double(0.017453289f));
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

// The numpad direction for a step of (dx, dy) cells: the angle in 11-degree
// steps, from the straight-leaning table near the axes.
std::uint8_t facing(int dx, int dy, GameVersion v) {
    const float deg = degrees(dy, dx);
    const int angle = static_cast<int>(deg) % 360;
    const std::uint32_t table = angle % 0x5a < 0x1e || angle % 0x5a >= 0x3d ? kDirByAngleA : kDirByAngleB;
    return logic::sdU8(table + std::uint32_t(angle / 0xb), v);
}

// FUN_0043b8b0(.., 0): the walking speed rate on the cell, 0 counting as 0x32.
int speedRate(const TacticsMap& map, int cx, int cy, int type) {
    const int rate = map.moveRate(cx, cy, type);
    return rate == 0 ? 0x32 : rate;
}
} // namespace

TacticsUnits::~TacticsUnits() {
    for (const Unit& u : units_) {
        if (u.used) anm_.freeRoot(u.node);
        for (const AnmPlayer::NodeId n : u.fx) if (n != AnmPlayer::kNone) anm_.freeRoot(n);
    }
    if (marker_ != AnmPlayer::kNone) anm_.freeRoot(marker_);
    if (groundMark_ != AnmPlayer::kNone) anm_.freeRoot(groundMark_);
    if (panelNode_ != AnmPlayer::kNone) anm_.freeRoot(panelNode_);
    panelNode_ = AnmPlayer::kNone;
    panelShown_ = -1;
    // FUN_004665c0 frees the script effects' ten players (FUN_0040ac90).
    for (const ScriptEffect& f : scriptFx_) if (f.node != AnmPlayer::kNone) anm_.freeRoot(f.node);
    for (const Flare& f : flares_) if (f.node != AnmPlayer::kNone) anm_.freeRoot(f.node);   // FUN_00465e60 -> FUN_00493860
    // FUN_00465e60 -> FUN_00466f00 frees the 0x200 model slots, FUN_004662b0
    // the panel's ten sprite objects and the two effect sets (+0x2a6e8).
    // Each unit's player object is a DxAnim wrapper: FUN_00464c60 makes
    // it (no bank, kind -1), FUN_00464d70 sets its bank and kind (with the
    // kind's clip tables, PTR_DAT_0060e850 / PTR_DAT_0060f070, read in
    // playAnim), FUN_00464cc0 / FUN_00464d30 tear it down and FUN_004663b0
    // is its deleting destructor; the port's AnmPlayer nodes.
    for (const auto& s : sets_) ctx_.sprites->unloadBank(s->bank);
    if (effect_.loaded()) ctx_.sprites->unloadBank(kEffectBank);
}

// FUN_0046a950 / FUN_0046af50 / FUN_0046aa40 (the viewer's side to a
// team, +0x115aac): 0 our own side, 4 an ally (table value 0), 1..3 the
// table's (1, 2 enemies, 3 the neutral side). FUN_00467eb0 fills +0x115aac
// from DAT_00618568 (FUN_00468bc0 -> FUN_00468cd0); in network play
// FUN_00468c00 takes DAT_00618668 with the players of one side allied.
// (FUN_00469a30, asked in the skill and scan states, is always 0.)
// FUN_00468bc0 / FUN_00468c00: the battle's table (+0x115bac), the story's
// (DAT_00618568) or in network play DAT_00618668 with the players who
// chose one side (DAT_007f44e4) made allies (0).
void TacticsUnits::loadRelations() {
    const GameWork& w = ctx_.work;
    const std::uint32_t table = network_ ? 0x618668u : kRelations;
    for (std::uint32_t i = 0; i < 0x100; ++i) relations_[i] = static_cast<std::int8_t>(logic::sdU8(table + i, ctx_.version));
    if (!network_) return;
    for (std::uint32_t a = 0; a < 4; ++a)
        for (std::uint32_t b = 0; b < 4; ++b)
            if (a != b && w.u8(stage::kSlotSide + a) == w.u8(stage::kSlotSide + b)) relations_[a * 0x10 + b] = 0;
}

int TacticsUnits::relationCode(int from, int to) const {
    if ((from & 0xf) == (to & 0xf)) return 0;
    const int raw = relations_[std::size_t((from & 0xf) * 0x10 + (to & 0xf))];
    return raw == 0 ? 4 : raw >= 1 && raw <= 3 ? raw : 0;
}

int TacticsUnits::relation(int team) const { return relationCode(viewer_, team); }

// FUN_00467eb0 clears the unit work, the lists and the counters (here
// the fields' defaults and build()); FUN_00467db0 at construction sets
// the check word +0x2e70c to 0x87654321 and +0x2e710 to 0, then calls the
// empty FUN_00467d50.
void TacticsUnits::build(const TacticsMap& map, int mapId, bool fixedParty) {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    loadRelations();
    // FUN_00468910, the battle's setup, maps onto build(): FUN_0049a480
    // places the viewer's members whose +0xf is 1 near the start (FUN_00452fc0
    // sets that below), FUN_00437a70 / FUN_0044e850 size the two route
    // searches (tactics_path, graph_ here), FUN_0046d860 the roster faces
    // (addFace), FUN_0049aa60 / FUN_00493ed0 the per-unit and per-group
    // lists (Unit's own fields), FUN_0046b210 the count of our units and
    // FUN_0048cd30 the command queue (empty).
    map_ = &map;
    mapId_ = mapId;
    // FUN_004653d0's FUN_00494cf0 (nothing), FUN_00494d20 and
    // FUN_00496390(0): no popups, not a replay.
    clearPopups();
    replay_ = false;
    clearGroups();                                 // FUN_00465e60 -> FUN_00493e60
    if (!graph_.load(map.vpt(), map.cellsW(), map.cellsH()) && !map.vpt().empty())
        SDL_Log("[tactics] map %d: the route graph (vpt) does not load", mapId);
    // FUN_00466000: the effect animations; set 1 clip 0x41 rings the selection.
    try {
        effect_.load(ctx_.readData("data\\DxAnim\\Efct.bin"), kEffectBank, *ctx_.sprites);
        marker_ = anm_.newRoot(effect_);
        anm_.play(marker_, 1, 0x41, 0);
        groundMark_ = anm_.newRoot(effect_);
        groundX_ = groundY_ = -1;
        // FUN_00466000 -> FUN_004937a0: the flares, all off.
        for (Flare& f : flares_) {
            f = Flare{};
            f.node = anm_.newRoot(effect_);
        }
        nextFlare_ = 0;
    } catch (const std::exception& ex) {
        SDL_Log("[tactics] Efct.bin: %s", ex.what());
    }
    // FUN_00452040 / FUN_00452130 and the copy in FUN_00452fc0. Those off
    // the field (+0xf 0, state 0xb) wait for their appear orders or a call.
    // (Network play brings every player's units, up to 0x50: FUN_00448a30.)
    party_ = std::min<int>(w.u8(stage::kPartyCount), network_ ? 0x50 : 0x14);
    // FUN_00452fc0 sets the party's +0xf in the work first. Without the
    // sortie (stage row +4, 0x7f448f): a stage whose 0x7f44b8 is set, when
    // not chained on from the last (0x7f448c), puts our members on the
    // field (1), to stand at the start (FUN_0049a480 below). With it: a
    // fresh stage takes them all off (0); a chained one takes off those not
    // fallen (+0xf below 3, or 5) and gives those not out of action (4)
    // 0x2a30 of stamina (+0x24, at most +0x20).
    const bool chained = w.u8(stage::kChained) != 0;
    for (int i = 0; i < party_ && !network_; ++i) {   // (network play: net_game set them on the field)
        const std::uint32_t r = stage::kParty + std::uint32_t(i) * 0xb0;
        if (w.u8(r + 0xa4) >= 4) continue;
        if (w.u8(stage::kSortie) == 0) {
            if (w.u8(stage::kPartyOut) != 0 && !chained) w.set8(r + 0xf, 1);
        } else if (!chained) {
            w.set8(r + 0xf, 0);
        } else {
            const int at = w.u8(r + 0xf);
            if (at < 3 || at == 5) w.set8(r + 0xf, 0);
            if (w.u8(r + 0xf) != 4) w.set32(r + 0x24, std::min(w.s32(r + 0x24) + 0x2a30, w.s32(r + 0x20)));
        }
    }
    for (int i = 0; i < party_; ++i) {
        stats::Derived d;
        for (std::uint32_t k = 0; k < 0xb0; ++k) d.set8(k, w.u8(stage::kParty + std::uint32_t(i) * 0xb0 + k));
        if (!fixedParty) {
            d.set8(0x9b, 0);
            d.set8(0x9c, 0);
            d.set8(0x9f, 0);
            d.set8(0xa0, 0);
        }
        create(i, d);
        if (!network_ || units_[std::size_t(i)].team() == viewer_) addFace(units_[std::size_t(i)]);
    }
    // FUN_0049a480: the viewer's members (FUN_0046ab20: the team and a named
    // character, FUN_00468e10) still marked on the field (+0xf 1)
    // to the free cells nearest the start, within its range (+0x9b / +0x9c).
    // In network play FUN_00468910 does it for each player present
    // (DAT_007f44d4): their team's members go round the start cell their
    // slot was given (DAT_007f44f4: 0x7f4492 + 4 n), the same on every machine.
    appearTurn_ = 0;
    const auto placeTeam = [&](int team, int sx, int sy) {
        appearGiven_.clear();                      // FUN_0049a480's FUN_0043d4e0
        for (int i = 0; i < party_; ++i) {
            Unit& u = units_[std::size_t(i)];
            const int kind = u.kind();
            if (!map_ || u.team() != team || kind < 1 || kind > 0x2d || u.data.u8(0xf) != 1) continue;
            const tactics::Cell c = appearCell(sx, sy, w.u8(stage::kStartRange), u.moveType());
            if (c.first < 0) continue;
            u.data.set8(0x9b, c.first);
            u.data.set8(0x9c, c.second);
            setPosition(u, c.first, c.second);
        }
    };
    if (!network_) {
        placeTeam(viewer_, w.s16(stage::kStartX), w.s16(stage::kStartY));
    } else {
        for (int s = 0; s < 4; ++s) {
            if (w.u8(stage::kSlotPresent + std::uint32_t(s)) == 0) continue;
            const std::uint32_t at = std::uint32_t(std::min<int>(w.u8(stage::kSlotStart + std::uint32_t(s)), 3)) * 4;
            placeTeam(s, w.s16(stage::kStartX + at), w.s16(stage::kStartY + at));
        }
    }
    // FUN_00453540
    std::uint32_t table = sdU32(kEnemyTables + std::uint32_t(mapId) * 4, v);
    int enemies = logic::sdS16(kEnemyCounts + std::uint32_t(mapId) * 2, v);
    // Maps 12, 19, 23.. (and +100, +200) point at one record in game RAM
    // (0x7e11e0 + 0x58 k) that nothing in the decompile writes; not ported.
    if (table >= 0x7a4e00) {
        SDL_Log("[tactics] map %d: enemy record at %06x is filled at run time; none placed", mapId, table);
        enemies = 0;
    }
    for (int k = 0; k < enemies && 0xf9 - k >= party_; ++k) {
        stats::Derived d;
        const std::uint32_t rec = table + std::uint32_t(k) * 0x54;
        tactics::enemyData(rec, d, v);
        create(0xf9 - k, d);
        // FUN_004da710: the record's AI part.
        Unit& u = units_[std::size_t(0xf9 - k)];
        for (std::uint32_t i = 0; i < 0x40; ++i) u.ai.set8(i, logic::sdU8(rec + 0x14 + i, v));
        if (tactics::traceUnits())
            SDL_Log("[tactics] enemy %d: kind %d team %d group %d %s at %d,%d", u.slot, u.data.u16(0xc), u.team(),
                    u.group(), u.onField() ? "on field" : "waiting", u.cx(), u.cy());
    }
    SDL_Log("[tactics] %d party units, %d enemy records, %zu graphics sets", party_, enemies, sets_.size());
}

// FUN_004680b0 (the parts that matter before the battle runs).
void TacticsUnits::create(int slot, const stats::Derived& d) {
    Unit& u = units_[std::size_t(slot)];
    u = Unit{};
    u.used = true;
    u.slot = slot;
    u.data = d;
    u.data.set16(0xa6, -1);
    u.data.set16(0xa8, 0);
    u.data.set16(0xaa, 0);
    u.data.set16(0xac, 0);
    u.data.set8(0xae, 0);
    face(u, 2);
    // FUN_00474fb0: the AI work (+0x290) cleared; teams 0..3 (the players'
    // sides) stand ready (+0x9f 2, +0xa0 0) with the AI record of
    // DAT_00618c48 (look every 0x1e frames, in sight, the nearest), the
    // rest take DAT_00618c88 (+0x17 1), which an enemy's own record then
    // replaces (FUN_004750b0 -> FUN_004da710, in build()). Row +0x18 is
    // always 0 after the clear.
    if (u.data.u8(0xa4) < 4) {
        u.data.set8(0x9f, 2);
        u.data.set8(0xa0, 0);
    }
    {
        const std::uint32_t table = u.data.u8(0xa4) < 4 ? 0x618c48 : 0x618c88;
        for (std::uint32_t i = 0; i < 0x40; ++i) u.ai.set8(i, logic::sdU8(table + i, ctx_.version));
    }
    u.holdOut = u.human() ? 900 : 600;             // FUN_004680b0: +0x4fe
    // FUN_00475370: on the field the unit stands (2), off it waits (0xb).
    // (A unit with a named character and FUN_00468eb0 starts in 0x19, the
    // same appear steps; nothing starts on the field with one here.)
    setState(u, u.onField() ? kStand : kOffField);
    // FUN_004680b0's tail: buildings hold their ground (+0x9f 2); gates and
    // bases stand in state 0x1c instead.
    if (structure(u)) {
        u.data.set8(0x9f, 2);
    } else if (u.kind() >= 0x19b && u.kind() <= 0x19e) {
        setState(u, kBuilding);
    } else if (base(u)) {
        setState(u, kBuilding);
        u.data.set8(0x9f, 2);
    }
    // FUN_00467020
    const int kind = u.data.u16(0xc) & 0x1ff;
    const std::uint32_t g = kGraphics + std::uint32_t(kind) * 8;
    const int file = logic::sdS16(g + 4, ctx_.version);
    const int palKind = logic::sdU8(g + 6, ctx_.version);
    int palette = 0;
    if (palKind == 1) {
        palette = static_cast<std::int8_t>(logic::sdU8(g + 7, ctx_.version));
    } else if (palKind == 2) {
        const int ch = u.kind();
        if (ch <= 0x22 && kind <= 0x27) {
            const int p = static_cast<std::int8_t>(logic::sdU8(kCharPalettes + std::uint32_t(ch + kind * 0x28), ctx_.version));
            if (p != 0) palette = p + 1;
        }
    }
    u.set = animSet(file, palette);
    if (u.set >= 0) u.node = anm_.newRoot(sets_[std::size_t(u.set)]->anm);
    // FUN_00467020 -> FUN_0046bd30: the state's clip (FUN_0046bcc0) from the
    // start: appearing 0x12, standing 1, holding 0x18, walking 3, state 5 2.
    if (u.node != AnmPlayer::kNone) {
        switch (u.state) {
            case kAppear: case kAppear2: playAnim(u, 0x12); break;
            case kStand: playAnim(u, 1); break;
            case kHold: playAnim(u, 0x18); break;
            case kMove: playAnim(u, 3); break;
            case 5: playAnim(u, 2); break;
            default: break;
        }
    }
    setPosition(u, u.data.u8(0x9b), u.data.u8(0x9c));
    // FUN_004680b0, after FUN_004750b0 -> FUN_004da710 cleared it: the post
    // (+0x2e8) is the cell the unit starts on (FUN_00468690 -> FUN_0046b2c0,
    // 0xff / 0xff off the field), so a computer's unit on "defensive"
    // (+0x9f 1) walks on only while it can still see where it started
    // (FUN_0047dec0). Only the computer's units on the field are given one
    // here: those off it would hold 0xff / 0xff, and the party is placed
    // after this (FUN_0049a480).
    if (!u.human() && u.onField()) u.post = {u.cx(), u.cy()};
    if (tactics::traceUnits())
        SDL_Log("[tactics] unit %3d id %3d kind %3d team %d field %d file %3d pal %2d set %2d at %d,%d HP %d speed %d",
                slot, u.data.s16(0), kind, u.team(), u.data.u8(0xf), file, palette, u.set, u.cx(), u.cy(),
                u.maxHp(), u.data.u16(0x2e));
}

void TacticsUnits::setPosition(Unit& u, int cx, int cy) {
    u.x = (cx * TacticsMap::kCellW + TacticsMap::kCellW / 2) * 0x10000;
    u.y = (cy * TacticsMap::kCellH + TacticsMap::kCellH / 2) * 0x10000;
}

// FUN_00466e90: one DxAnim set per (file, palette), shared; FUN_00466b40
// takes the next free one of the original's 0x200 slots (+0x2a6f0), here
// one of kMaxSets sprite banks. FUN_00466c80 fills the slot: the file
// (PTR_s_data_DxAnim_b1a_bin_00610538), the palette, a serial (+0x2e6f0,
// which FUN_00452fc0 starts at 0x6e through FUN_00466b00),
// loaded through FUN_00464eb0 (FUN_00464de0 for one without a palette).
// (FUN_00466da0 looks a slot up by file and palette, FUN_00466e20 by file.)
int TacticsUnits::animSet(int file, int palette) {
    for (std::size_t i = 0; i < sets_.size(); ++i)
        if (sets_[i]->file == file && sets_[i]->palette == palette) return int(i);
    if (int(sets_.size()) >= kMaxSets) {
        SDL_Log("[tactics] more than %d unit graphics sets", kMaxSets);
        return -1;
    }
    auto s = std::make_unique<AnimSet>();
    s->file = file;
    s->palette = palette;
    s->bank = static_cast<std::uint16_t>(kFirstBank + sets_.size());
    const std::string path = sdString(sdU32(kGraphics + std::uint32_t(file) * 8, ctx_.version), ctx_.version);
    try {
        s->anm.load(ctx_.readData(path), s->bank, *ctx_.sprites, palette);
    } catch (const std::exception& ex) {
        SDL_Log("[tactics] %s: %s", path.c_str(), ex.what());
        return -1;
    }
    sets_.push_back(std::move(s));
    return int(sets_.size() - 1);
}

// FUN_0046bcc0: the clip for `anim` facing the unit's direction (FUN_00465040
// through the per-file tables), restarting the step timer.
void TacticsUnits::playAnim(Unit& u, int anim) {
    u.animEnd = false;
    u.timer = 0;
    u.clip = anim;
    if (u.set < 0 || u.node == AnmPlayer::kNone) return;
    const GameVersion v = ctx_.version;
    const std::uint32_t id = std::uint32_t(sets_[std::size_t(u.set)]->file + 8);
    const std::uint32_t rows = sdU32(kClipRows + id * 4, v), clips = sdU32(kClips + id * 4, v);
    const std::uint32_t e = rows + (std::uint32_t(logic::sdU8(kDirColumn + u.dir, v)) + std::uint32_t(anim) * 8) * 2;
    const std::uint16_t clip = sdU16(clips + std::uint32_t(logic::sdU8(e, v)) * 2, v);
    if (tactics::traceUnits() && (structure(u) || base(u) || u.kind() >= 0x19b))
        SDL_Log("[tactics] tick %llu unit %d (kind %d) anim %d -> set %d clip %d", static_cast<unsigned long long>(ctx_.engine.tickCount()), u.slot, u.kind(), anim, clip >> 8, clip & 0xff);
    anm_.play(u.node, clip >> 8, clip & 0xff, logic::sdU8(e + 1, v) & 3);
}

// FUN_00475370. (FUN_00475270 sets only the next state, +0x292, which the
// port writes as u.next = ..; it asserts 6 has a skill, +0x4f2, and 0x1d a
// target, +0x4ee.)
void TacticsUnits::setState(Unit& u, int state) {
    u.state = static_cast<std::uint8_t>(state);
    u.next = kNoState;
    u.sub = 0;
}

// FUN_0046b470's loop over the units -> FUN_0046b760.
void TacticsUnits::update() {
    // FUN_004678c0: the hold FUN_00467850 asked for. Its FUN_0043b250 (1
    // locks, 0 frees) sets the map's own input locks: +0x26595 the key
    // scrolling (FUN_0043b8b0), 96 the right click (FUN_0043b7b0), 97 the
    // minimap (FUN_004406f0), 98 the box (FUN_0043c9c0). The port's map
    // input all runs under inputOpen_, which the same hold sets; the
    // sortie's mode 2 (all but the box) is its own loop (FUN_00499da0).
    if (holdKind_ == 1) {
        frozen_ = !holdOn_;
        inputOpen_ = holdOn_;
        timersWanted_ = holdOn_ ? 1 : 0;
    } else if (holdKind_ == 2 && holdOn_) {
        frozen_ = false;
        inputOpen_ = false;
        timersWanted_ = 0;
    }
    holdKind_ = -1;
    // FUN_00493630 here does nothing (an emptied debug hook; FUN_004935f0
    // clears its word DAT_007a49ec at construction, FUN_004936a0 is its
    // empty draw and FUN_00493670 its click test, always 0).
    trimSelection();                               // units that left the field or vanished
    // FUN_004727f0 (from FUN_0046b470): the selection ring's player steps.
    if (marker_ != AnmPlayer::kNone && anm_.update(marker_)) anm_.play(marker_, 1, 0x41, 0);
    // FUN_00472960 (from FUN_0046b470): the ground mark runs its clip once.
    if (groundX_ >= 0 && groundMark_ != AnmPlayer::kNone && anm_.update(groundMark_)) groundX_ = groundY_ = -1;
    // FUN_0046b470 -> FUN_0046e730 rebuilds each frame's unit lists (on the
    // field, enemies there FUN_0046a550, ours, ours and our allies'
    // FUN_0046a5e0, and those off it FUN_004695d0); the port walks units_.
    for (Unit& u : units_) {
        if (!u.used) continue;
        if (!frozen_) applyChanges(u);            // FUN_0046b760 -> FUN_00493450
        if (u.onField()) {
            tickStatus(u);
            regenerate(u);
            if (!frozen_) checkCollapse(u);       // FUN_0046bc20, while the battle runs (+0x108b39)
            // FUN_00472770
            if (u.hp() < 1 && u.state != kFall && u.next != kFall) u.next = kFall;
        }
        if (!frozen_) runOrders(u);               // FUN_0046b760 -> FUN_0048e2a0
        if (!frozen_ || u.state == kAppear) {
            think(u);
            const int kind = u.kind();
            if (kind > 0x182 && kind < 0x197 && !frozen_) occupy(u);   // FUN_00475700's tail
        }
        u.animEnd = u.node == AnmPlayer::kNone || anm_.update(u.node);
        u.hitSignal = u.node != AnmPlayer::kNone && anm_.takeSignals(u.node).hit;
        updateEffects(u);
        ++u.timer;
    }
    // FUN_00466910's update half (FUN_00466880 from FUN_0046b470 steps the
    // ten players and counts their frames, +0xa).
    for (ScriptEffect& f : scriptFx_)
        if (f.on && anm_.update(f.node)) f.on = false;
    // FUN_0046b470 -> FUN_00493a30: a flare loops its clip for 300 frames.
    for (Flare& f : flares_) {
        if (!f.on) continue;
        if (anm_.update(f.node)) {
            if (f.side == 0) anm_.play(f.node, 0, 0x2d, 0);
            else anm_.play(f.node, 3, 0x87, 0);
        }
        if (--f.life < 1) f.on = false;
    }
    tickPopups();
}

// FUN_00475700 -> FUN_004751f0 (the pending state first), then the state.
void TacticsUnits::think(Unit& u) {
    if (u.next != kNoState) {
        if (tactics::traceUnits()) SDL_Log("[tactics] unit %d: state %d -> %d at %d,%d", u.slot, u.state, u.next, u.cx(), u.cy());
        u.state = u.next;
        u.next = kNoState;
        u.sub = 0;
        // FUN_004756a0: the cast effect (slot 1) stops and the unit's queued
        // search is dropped (FUN_00437d90; the port searches at once). Set
        // directly, FUN_00475370 only drops the search (FUN_00475650).
        playEffect(u, 1, -1);
    }
    switch (u.state) {
        case kAppear:    appearState(u); break;
        case kStand:     standState(u); break;
        case kHold:      holdState(u); break;
        case kMove:      moveState(u); break;
        case kSkill:     skillState(u); break;
        case kFall:      fallState(u); break;
        case kChoose:    chooseState(u); break;
        case kRoute:     routeState(u); break;
        case kGuard:     guardState(u); break;
        case kCapture:   captureState(u); break;
        case kAmbush:    ambushState(u); break;
        case kWander:    wanderState(u); break;
        case kSummon:    summonState(u); break;
        case kRouteWait: routeWaitState(u); break;
        case kOffField:  offFieldState(u); break;
        case kWithdraw:  withdrawState(u); break;
        case kBuilding:  buildingState(u); break;
        case kFlee:      fleeState(u); break;
        case kStagger:   staggerState(u); break;
        case kKnock:     knockState(u); break;
        case kGuardHit:  guardHitState(u); break;
        case kCollapse:  collapseState(u); break;
        case kRetreat:   retreatState(u); break;
        case kCall:      callState(u); break;
        default:   break;                          // 5 does nothing; 0x14 (FUN_0047a530) returns at once
    }
}

// FUN_0046c040: the buildings, bases and gates (kinds 0x183..0x196,
// 0x19b..0x19e) do not turn; their facing shows whose they are
// (FUN_0046c290: ours and allies' 3, the enemies' 6 / 9, the others' 2),
// and with it which clip they play.
void TacticsUnits::face(Unit& u, int dir) const {
    const int kind = u.kind();
    if ((kind >= 0x183 && kind <= 0x196) || (kind >= 0x19b && kind <= 0x19e)) {
        switch (relation(u.team())) {
            case 0: case 4: u.dir = 3; break;
            case 1: u.dir = 6; break;
            case 2: u.dir = 9; break;
            case 3: u.dir = 2; break;
            default: break;
        }
        return;
    }
    u.dir = static_cast<std::uint8_t>(dir);
}

// FUN_00480d00: the team with the most units on the field within range
// mask 5 of the building (ties to the higher team; the neutral side, 7,
// never counts), if it may take it: any team takes a neutral building, an
// owned one only from fewer of its owner's units. -1: nobody.
int TacticsUnits::occupier(const Unit& b) const {
    if (!map_) return -1;
    std::array<int, 16> n{};
    for (const Unit& o : units_) {
        if (!o.used || !o.onField() || o.team() == 7 || o.slot == b.slot) continue;
        if (map_->inRange(b.cx(), b.cy(), o.cx(), o.cy(), 5)) ++n[std::size_t(o.team() & 0xf)];
    }
    int best = -1, most = 0;
    for (int t = 15; t >= 0; --t)
        if (n[std::size_t(t)] > most) { most = n[std::size_t(t)]; best = t; }
    if (best == -1 || best == b.team()) return -1;
    if (b.team() == 7 || n[std::size_t(b.team() & 0xf)] < most) return best;
    return -1;
}

// FUN_00480a60, every frame for the buildings and bases (kinds 0x183..0x196):
// a neutral one goes to its occupier at once; an owned one holds out while
// its counter (600, a player's 900) runs down under occupation and refills
// otherwise; at 0 the takeover starts (message 0xe).
void TacticsUnits::occupy(Unit& u) {
    if (opening_) return;                          // +0x108b48
    if (!u.onField() || u.holdOut == 0 || u.state == kCapture) return;
    const int team = occupier(u);
    const int full = u.human() ? 900 : 600;
    if (u.team() == 7) {
        if (team == -1) return;
        u.holdOut = 0;
    } else if (team == -1 || team == 4) {
        if (u.holdOut < full) ++u.holdOut;
        return;
    } else if (--u.holdOut != 0) {
        return;
    }
    if (tactics::traceUnits()) SDL_Log("[tactics] unit %d (kind %d) taken by team %d", u.slot, u.kind(), team);
    changeTeam(u, team);
}

// FUN_0048ef60: a building or base changes hands through state 0x15; any
// other unit joins the team at once (FUN_0046d970 / FUN_00475140 for ours).
// Anything but a base refuses while busy (FUN_004693b0); a building goes
// into 0x15 already steady and busy.
bool TacticsUnits::changeTeam(Unit& u, int team) {
    if (!base(u) && busyNow(u)) return false;
    if (structure(u) || base(u)) {
        if (!base(u)) setFlags(u, true);
        u.captor = static_cast<std::uint8_t>(team);
        u.next = kCapture;
        return true;
    }
    u.data.set8(0xa4, team);
    if (team == viewer_) {
        if (u.human() && u.kind() < 0x40) addFace(u);   // FUN_0048ef60: FUN_00468eb0 -> FUN_0046d970
        u.aiControlled = false;
        u.data.set8(0x9f, 2);
        u.data.set8(0xa0, 0);
    }
    u.captor = static_cast<std::uint8_t>(team);
    return true;
}

// FUN_0047a590, state 0x15: the takeover clip for the old and new side
// (DAT_00618d40), a pause by kind, then the new team, a full counter and
// the new side's facing.
void TacticsUnits::captureState(Unit& u) {
    const GameVersion v = ctx_.version;
    switch (u.sub) {
        case 0:
            setFlags(u, true);                     // FUN_0046b0f0 / FUN_0046b0b0(.., 1)
            ++u.sub;
            break;
        case 1: {
            const int from = relation(u.team()), to = relation(u.captor);
            const int clip = static_cast<std::int8_t>(logic::sdU8(0x618d40 + std::uint32_t(to + from * 5), v));
            if (clip != -1) playAnim(u, clip);
            u.timer = 0;
            ++u.sub;
            break;
        }
        case 2: {
            const int kind = u.kind();
            const bool a = (kind >= 0x183 && kind <= 0x188) || (kind >= 0x18f && kind <= 0x194);
            const int wait = a ? 0x48 : kind >= 0x189 && kind <= 0x18e ? 0x25 : base(u) ? 0x3e : 0x3c;
            if (u.timer >= wait) ++u.sub;
            break;
        }
        case 3:
            setFlags(u, false);
            u.data.set8(0xa4, u.captor);
            u.holdOut = u.human() ? 900 : 600;
            face(u, u.dir);
            u.next = base(u) ? 0x1c : 2;
            if (!u.human()) u.data.set8(0x9f, 2);
            break;
        default: break;
    }
}

// ------------------------------------------------------- the AI states ---

std::pair<int, std::uint32_t> TacticsUnits::routeOf(Unit& u) const {
    const GameVersion v = ctx_.version;
    const std::uint32_t table = sdU32(kRoutes + std::uint32_t(mapId_) * 4, v);
    if (table == 0) return {0, 0};
    if (u.data.u8(0xa2) == 0) {                    // the original asserts, then takes route 1
        SDL_Log("[tactics] unit %d (kind %d) follows no route", u.slot, u.kind());
        u.data.set8(0xa2, 1);
    }
    const std::uint32_t e = table + std::uint32_t(u.data.u8(0xa2)) * 8;
    return {logic::sdS32(e, v), sdU32(e + 4, v)};
}

void TacticsUnits::setGoal(Unit& u, int cx, int cy) {
    u.goal = {cx, cy};
}

// FUN_0047f090: on to the route's next point; a row's +8 moves the unit
// to another route (from its start), or -1 ends the route following.
bool TacticsUnits::nextPoint(Unit& u) {
    const GameVersion v = ctx_.version;
    auto [count, rows] = routeOf(u);
    if (count <= 0 || rows == 0) {
        u.ai.set8(0, 0);
        return true;
    }
    const int link = sdS8(rows + 8 + std::uint32_t(u.route) * 12, v);
    if (link == 0) {
        if (++u.route >= count) u.route = 0;
    } else if (link == -1) {
        u.ai.set8(0, 0);
        return true;
    } else {
        u.data.set8(0xa2, static_cast<std::uint8_t>(link));
        u.route = 0;
        std::tie(count, rows) = routeOf(u);
        if (count <= 0 || rows == 0) {
            u.ai.set8(0, 0);
            return true;
        }
    }
    const std::uint32_t row = rows + std::uint32_t(u.route) * 12;
    setGoal(u, logic::sdU8(row, v), logic::sdU8(row + 1, v));
    return false;
}

// FUN_0047ed90, state 0x10: walk (4) to the route's point until within
// range 3 of it; there wait (0x1a) as the row says, or go on to the next.
void TacticsUnits::routeState(Unit& u) {
    if (u.sub != 0 || !map_) return;
    const GameVersion v = ctx_.version;
    const auto [count, rows] = routeOf(u);
    if (count <= 0 || rows == 0) {
        u.ai.set8(0, 0);
        u.next = kChoose;
        return;
    }
    if (u.route < 0 || u.route >= count) u.route = 0;
    const std::uint32_t row = rows + std::uint32_t(u.route) * 12;
    const int px = logic::sdU8(row, v), py = logic::sdU8(row + 1, v);
    if (!map_->inRange(u.cx(), u.cy(), px, py, 3)) {
        setGoal(u, px, py);
        u.next = kMove;
    } else if (const std::int32_t wait = logic::sdS32(row + 4, v); wait == 0) {
        u.next = nextPoint(u) ? 0xf : 4;
    } else {
        u.ai.set32(0x38, wait);
        u.next = kRouteWait;
    }
}

// FUN_0047efb0, state 0x1a: stand for the row's wait, then on.
void TacticsUnits::routeWaitState(Unit& u) {
    if (u.sub == 0) {
        playAnim(u, 1);
        ++u.sub;
    } else if (u.sub == 1) {
        u.ai.set32(0x38, u.ai.s32(0x38) - 1);
        if (u.ai.s32(0x38) < 1) {
            u.ai.set32(0x38, 0);
            u.next = nextPoint(u) ? 0xf : 4;
        }
    }
}

// FUN_0047a470, state 0x16 (stage order 4): wait until someone hits the
// unit; striking back drops the computer's units to the normal order.
void TacticsUnits::ambushState(Unit& u) {
    if (const Unit* t = counterTarget(u); t && react(u, *t)) {
        if (!u.human()) u.data.set8(0xa0, 0);
        return;
    }
    if (u.order() != 4) u.next = kChoose;
}

bool TacticsUnits::straightGoal(Unit& u, int dir, int most) {
    if (!map_ || dir < 1 || dir > 9) return false;
    const GameVersion v = ctx_.version;
    const int dx = logic::sdS16(kStepOffsets + std::uint32_t(dir) * 4, v);
    const int dy = logic::sdS16(kStepOffsets + std::uint32_t(dir) * 4 + 2, v);
    if (dx == 0 && dy == 0) return false;
    const int type = u.moveType();
    const tactics::Cell from{u.cx(), u.cy()};
    // FUN_004388e0: every cell on the line open to the move type and free.
    const auto open = [&](int cx, int cy) {
        return (type == 4 || map_->moveRate(cx, cy, type) != 0) && !restingOn(cx, cy, &u);
    };
    const auto clear = [&](int x1, int y1) {
        int ex = x1 - from.first, ey = y1 - from.second;
        const int sx = ex < 0 ? -1 : 1, sy = ey < 0 ? -1 : 1;
        ex = std::abs(ex);
        ey = std::abs(ey);
        int x = from.first, y = from.second, err = 0;
        const int n = std::max(ex, ey);
        for (int i = 0; i < n; ++i) {
            if (ex < ey) {
                y += sy;
                if ((err += ex) >= ey) { x += sx; err -= ey; }
            } else {
                x += sx;
                if ((err += ey) >= ex) { y += sy; err -= ex; }
            }
            if (!open(x, y)) return false;
        }
        return true;
    };
    int x = from.first, y = from.second, n = 0;
    tactics::Cell last{-1, -1};
    while (n < most) {
        x += dx;
        y += dy;
        if (x < 0 || x >= map_->cellsW() || y < 0 || y >= map_->cellsH() || !clear(x, y)) break;
        ++n;
        last = {x, y};
    }
    if (n == 0) return false;
    setGoal(u, last.first, last.second);
    return true;
}

// FUN_004771d0, state 0x17: walk straight on, or turned one way or the
// other, as far as the way is open; stand three seconds where it is not.
void TacticsUnits::wanderState(Unit& u) {
    const GameVersion v = ctx_.version;
    if (u.sub == 0) {
        if (structure(u)) {
            u.next = kGuard;
            return;
        }
        const int r = rand() % 3;
        const std::uint32_t table = kWanderTurns + std::uint32_t(r == 1 ? 12 : r == 2 ? 24 : 0);
        if (!straightGoal(u, sdS8(table + u.dir, v), 0x7fff)) {
            playAnim(u, 1);
            (void)rand();                          // +10 = rand % 0x78, then cleared
            u.timer = 0;
            u.sub = 1;
            return;
        }
        plan(u);
        u.sub = 2;
    } else if (u.sub == 1) {
        if (u.timer >= 0xb5) u.sub = 0;
        return;
    }
    if (u.sub != 2) return;
    if (u.aiControlled && aiReact(u, false)) return;
    if (walk(u) != Walk::On) u.sub = 0;       // FUN_004771d0: whatever the result
}

// FUN_0047e680, state 0x1d: run from the unit in +0x4ee, as far as the AI
// record's +0x24 says (10 cells, the sight or twice it, the reach or twice
// it, or without end), then choose again.
void TacticsUnits::fleeState(Unit& u) {
    const GameVersion v = ctx_.version;
    const auto done = [&] {
        u.target = u.skill = -1;
        u.next = kChoose;
    };
    switch (u.sub) {
        case 0: {
            if (structure(u)) {
                u.next = kGuard;
                return;
            }
            if (u.target < 0 || u.target >= kSlots) return done();
            const Unit& t = units_[std::size_t(u.target)];
            if (!t.used || down(t)) return done();
            // FUN_00470620 + 180 degrees (FUN_004706d0) -> FUN_00470720, then one of three
            // directions near it.
            const int away = facing(u.cx() - t.cx(), u.cy() - t.cy(), v);
            const int dir = sdS8(kFleeTurns + std::uint32_t(away * 3 + rand() % 3), v);
            int most = 0x7fff;
            switch (u.ai.u8(0x24)) {
                case 0: most = 10; break;
                case 1: most = u.data.u8(0x2c); break;
                case 2: most = u.data.u8(0x2c) * 2; break;
                case 3: most = u.data.u8(0x37); break;
                case 4: most = u.data.u8(0x37) * 2; break;
                default: break;
            }
            if (!straightGoal(u, dir, most)) {
                playAnim(u, 1);
                (void)rand();                      // +10 = rand % 0x78, then cleared
                u.timer = 0;
                u.sub = 1;
                return;
            }
            plan(u);
            u.sub = 2;
            [[fallthrough]];
        }
        case 2:
            // On the way only the record's +0x12 2 (run from what the scan
            // found, healers' finds included; the walk goes on this frame)
            // and 4 (the skill state, with what it had) act.
            if (u.aiControlled) {
                if (const Unit* t = scan(u)) {
                    const int act = u.ai.u8(0x12);
                    if (act == 2) {
                        u.target = static_cast<std::int16_t>(t->slot);
                        u.next = kFlee;
                    } else if (act == 4) {
                        u.next = kSkill;
                        return;
                    }
                }
            }
            if (walk(u) != Walk::On) u.sub = 3;
            break;
        case 1:
            if (u.timer > 0x3c) u.sub = 3;
            break;
        case 3:
            done();
            break;
        default: break;
    }
}

// FUN_0047ce70: strike back at the unit that hit this one while it is in
// sight (+0x1e 0); the original's +0x1e 2 (flee) branch is never reached,
// FUN_0047cc90 letting only +0x1e 0 through.
bool TacticsUnits::react(Unit& u, const Unit& t) {
    u.counter = -1;
    if (u.ai.u8(0x1e) == 0 && reaches(u, t, sight(u)) && aimAt(u, t)) {
        u.next = kSkill;
        return true;
    }
    return false;
}

// FUN_0047b210: whether the skill state drops its target for `t`: yes
// with none, no when `t` is the target already, else on a roll (rand % 100)
// under the AI record's +0x13.
bool TacticsUnits::switchTo(Unit& u, const Unit& t) {
    if (u.target == -1) return true;
    if (u.target == t.slot) return false;
    return rand() % 100 < static_cast<std::int8_t>(u.ai.u8(0x13));
}

// FUN_0047b790 / FUN_0047b800: the enemy a teammate called out, while it
// can still be fought. (FUN_0047b920 then aims at it, state 6, and clears
// the call, FUN_00492e60, as aiReact does.)
const TacticsUnits::Unit* TacticsUnits::helpTarget(Unit& u) {
    if (u.help < 0 || u.help >= kSlots) return nullptr;
    const Unit& t = units_[std::size_t(u.help)];
    if (t.used && (!structure(t) || u.ai.u8(0x1c) != 0) && attackable(u, t) && t.onField() && !down(t)) return &t;
    u.help = -1;
    return nullptr;
}

// FUN_0047b2c0 -> FUN_0047b340: AI record +0x17 picks which teammates on
// the field hear of the enemy (1 within 15 cells, 2 beyond 30, 3 within
// the caller's sight, 4 beyond it, more: all), +0x18 0 only its group.
void TacticsUnits::callHelp(const Unit& u, const Unit& t) {
    const int mode = u.ai.u8(0x17);
    if (mode == 0 || !map_) return;
    const bool anyGroup = u.ai.u8(0x18) != 0;
    for (Unit& o : units_) {
        if (!o.used || !o.onField() || o.slot == u.slot || relationOf(u, o) != 0) continue;
        if (!anyGroup && o.group() != u.group()) continue;
        bool hears = true;
        switch (mode) {
            case 1: hears = map_->inRange(u.cx(), u.cy(), o.cx(), o.cy(), 0xf); break;
            case 2: hears = !map_->inRange(u.cx(), u.cy(), o.cx(), o.cy(), 0x1e); break;
            case 3: hears = map_->inRange(u.cx(), u.cy(), o.cx(), o.cy(), sight(u)); break;
            case 4: hears = !map_->inRange(u.cx(), u.cy(), o.cx(), o.cy(), sight(u)); break;
            default: break;
        }
        if (hears && !down(o)) o.help = static_cast<std::int16_t>(t.slot);   // FUN_00492e20
    }
}

// FUN_00480740: how many of the unit's group (same team and +0x98) it may
// still call in: +0x97 less those of +0x96 3 on the field, while any wait
// (FUN_00480640 counts the others of the group off the field that may come,
// FUN_0046a120).
int TacticsUnits::summonRoom(const Unit& u) const {
    const int limit = u.data.u8(0x97);
    if (limit == 0) return 0;
    int waiting = 0, active = 0;
    for (const Unit& o : units_) {
        if (!o.used || o.slot == u.slot || o.team() != u.team() || o.group() != u.group()) continue;
        const int at = o.data.u8(0xf);
        const int kind = o.kind();
        // FUN_0046a120: off the field, not a building, base or gate.
        if ((at == 0 || at == 2) && !structure(o) && !base(o) && (kind < 0x19b || kind > 0x19e)) ++waiting;
        if (o.onField() && o.data.u8(0x96) == 3) ++active;   // FUN_00480540, over the field list
    }
    if (waiting == 0 || active >= limit) return 0;
    return limit - active;
}

// FUN_00480480: the computer's units with a call interval (AI +0x20)
// count up to it (+0x34), then call when there is room.
bool TacticsUnits::summonDue(Unit& u) {
    if (u.human() || structure(u) || u.ai.s32(0x20) == 0) return false;
    u.ai.set32(0x34, u.ai.s32(0x34) + 1);
    if (u.ai.s32(0x34) < u.ai.s32(0x20)) return false;
    u.ai.set32(0x34, 0);
    return summonRoom(u) > 0;
}

// FUN_004807e0: the waiting teammates come in near the caller, within its
// sight, up to the room left.
int TacticsUnits::summon(const Unit& u) {
    const int most = std::min<int>(summonRoom(u), u.data.u8(0x97));
    int n = 0;
    appearGiven_.clear();                          // FUN_0043d4e0
    for (Unit& o : units_) {
        if (n >= most) break;
        if (!o.used || o.slot == u.slot || o.team() != u.team() || o.group() != u.group()) continue;
        if (appearUnit(o.slot, u.cx(), u.cy(), sight(u), false)) ++n;
    }
    if (tactics::traceUnits()) SDL_Log("[tactics] unit %d calls in %d of group %d", u.slot, n, u.group());
    return n;
}

// FUN_0047f1d0, state 0x18: three seconds' call (clip 0xc), the calling
// clip (0x11), the group comes in, a recovery, then stand.
void TacticsUnits::summonState(Unit& u) {
    switch (u.sub) {
        case 0:
            u.busy = true;                         // FUN_0046b0f0(.., 1)
            u.cast = u.castFull = 0xb4;
            playAnim(u, 0xc);
            ++u.sub;
            break;
        case 1:
            if (--u.cast < 1) {
                u.cast = 0;
                u.steady = true;                   // FUN_0046b0b0(.., 1): the call itself
                playAnim(u, 0x11);
                u.recover = u.recoverFull;
                ++u.sub;
            }
            break;
        case 2:
            if (u.animEnd) {
                playAnim(u, 0xf);
                u.recover = u.recoverFull;
                u.steady = false;
                summon(u);
                ++u.sub;
            }
            break;
        case 3:
            if (--u.recover < 1) {
                u.recover = 0;
                u.busy = false;
                ++u.sub;
            }
            break;
        case 4:
            u.next = kStand;
            break;
        default: break;
    }
}

// The reactions FUN_00476dd0, FUN_004776a0 and FUN_004771d0 share: the
// enemy a teammate called out, the one that hit the unit, a call for
// reinforcements (`summon`), then what the scan finds, by the AI record's
// +0x12 (0 aim at it, 2 run from it, 4 the skill state again).
bool TacticsUnits::aiReact(Unit& u, bool summon) {
    if (const Unit* h = helpTarget(u)) {
        u.help = -1;
        if (aimAt(u, *h)) {
            u.next = kSkill;
            return true;
        }
    }
    if (const Unit* t = counterTarget(u); t && react(u, *t)) return true;
    if (summon && summonDue(u)) {
        u.next = kSummon;
        return true;
    }
    // The scan's find goes through the same switch whether it is an enemy
    // or, for a healer (FUN_0047ac80), a hurt friend: 0 aims at it, 2 runs
    // from it (+0x4ee), 4 goes back to the skill state with the target and
    // skill it already had, 3 asserts, anything else (1 in the stage data)
    // does nothing.
    const Unit* t = scan(u);
    if (!t) return false;
    switch (u.ai.u8(0x12)) {
        case 0:
            if (!aimAt(u, *t)) return false;
            u.next = kSkill;
            return true;
        case 2:
            // No return here: FUN_00476dd0 / FUN_004771d0 still take this
            // frame's walk step, and a walk that ends then overrides the
            // flight with 0xf (FUN_00476dd0).
            u.target = static_cast<std::int16_t>(t->slot);
            u.next = kFlee;
            return false;
        case 4:
            u.next = kSkill;
            return true;
        default:
            return false;
    }
}

// FUN_0046ba10: what a unit leaving the field lets go of: its route and
// targets, the conditions, the effects, the cast and recovery.
void TacticsUnits::leaveField(Unit& u) {
    u.path.clear();
    u.step = 0;
    u.goal = {-1, -1};
    u.area = 0;
    u.areaCell = {-1, -1};
    u.selected = false;
    u.holdOut = u.human() ? 900 : 600;
    u.counter = u.help = -1;                       // FUN_00492e60 (+0x27c..+0x27e to -1)
    u.target = u.skill = -1;
    u.status = {};                                 // FUN_004902a0 on each of the three slots
    u.landing.clear();
    u.delayed.clear();
    u.changes.clear();                             // FUN_00493570
    for (std::size_t k = 0; k < u.fx.size(); ++k) u.fxOn[k] = false;
    u.cast = u.castFull = u.recover = u.recoverFull = 0;
    u.aiControlled = false;
    u.orders.clear();                              // FUN_0048de60
}

// FUN_00479e40, state 0x1b: the unit leaves the field and waits (0xb, +0xf 0).
void TacticsUnits::withdrawState(Unit& u) {
    if (u.sub != 0) return;
    setFlags(u, false);
    setState(u, kOffField);
    leaveField(u);
    u.data.set8(0xf, 0);
}

// FUN_00479f40, state 0xb, off the field: a fallen member of the party
// (FUN_00468eb0) recovers its HP (at least 1) and waits to go out again
// (+0xf 2, message 0x96); a fallen enemy of +0x96 3 with lives left
// (+0x97, 99 for ever) comes back whole to wait (+0xf 2); the rest stay
// gone (+0xf 4).
void TacticsUnits::offFieldState(Unit& u) {
    if (u.sub == 0) {
        setFlags(u, false);
        u.timer = 0;
        ++u.sub;
    }
    if (u.sub != 1) return;
    const int kind = u.kind();
    const bool member = u.human() && kind >= 1 && kind <= 0x2d;
    switch (u.data.u8(0xf)) {
        case 2:
            if (member) regenerate(u);
            break;
        case 3:
            if (member) {
                if (u.hp() == 0) u.setHp(1);
                regenerate(u);
                if (u.hp() >= u.maxHp()) {
                    u.setHp(u.maxHp());
                    u.setMp(u.data.s16(0x1a));
                    u.data.set8(0xf, 2);
                    voice(u, 0x96);
                    if (tactics::traceUnits()) SDL_Log("[tactics] unit %d recovered and waits", u.slot);
                }
            } else if (!structure(u) && u.data.u8(0x96) == 3 && u.data.u8(0x97) != 0) {
                if (u.data.u8(0x97) < 99) u.data.set8(0x97, u.data.u8(0x97) - 1);
                u.setHp(u.maxHp());
                u.setMp(u.data.s16(0x1a));
                u.data.set32(0x24, u.data.s32(0x20));
                u.data.set8(0xf, 2);
            } else {
                u.data.set8(0xf, 4);
            }
            break;
        default: break;
    }
}

bool TacticsUnits::withdraw(int slot) {
    if (slot < 0 || slot >= kSlots) return false;
    Unit& u = units_[std::size_t(slot)];
    if (!u.used || !u.onField()) return false;
    u.next = kWithdraw;
    return true;
}

void TacticsUnits::changeSide(int slot, int team) {
    if (slot < 0 || slot >= kSlots || !units_[std::size_t(slot)].used) return;
    changeTeam(units_[std::size_t(slot)], team);
}

// FUN_00466640: the effect's clip through FUN_0046be50 (FUN_00465140 on
// the slot's player, kind 7). FUN_00466500 clears the ten slots (+0x10c680,
// 0x520 each) at the start, scriptFx_'s defaults here.
void TacticsUnits::playScriptEffect(int slot, int x, int y, int id, int mode, int unit) {
    if (slot < 0 || slot >= int(scriptFx_.size())) return;
    const GameVersion v = ctx_.version;
    ScriptEffect& f = scriptFx_[std::size_t(slot)];
    f.on = false;
    f.x = x;
    f.y = y;
    f.mode = mode;
    f.unit = unit;
    if (id < 1000 || !effect_.loaded()) return;
    const int file = id / 1000 - 1, no = id % 1000;
    if (file < 0 || file > 3 || no > 199) return;
    const std::uint32_t e = 0x60f890 + std::uint32_t(file) * 800 + std::uint32_t(no) * 4;
    if (f.node == AnmPlayer::kNone) f.node = anm_.newRoot(effect_);
    anm_.play(f.node, logic::sdU8(e + 1, v), logic::sdU8(e + 2, v), 0);
    f.on = true;
}

// FUN_00493960; FUN_004938d0 takes the next of the five in turn (on, 300
// frames), the oldest giving way.
void TacticsUnits::flare(int x, int y, int side) {
    Flare& f = flares_[std::size_t(nextFlare_)];
    nextFlare_ = nextFlare_ + 1 > 4 ? 0 : nextFlare_ + 1;
    f.on = true;
    f.life = 300;
    f.side = static_cast<std::uint8_t>(side);
    f.x = x;
    f.y = y;
    if (f.node == AnmPlayer::kNone) return;
    if (side == 0) {
        anm_.play(f.node, 0, 0x2d, 0);
        ctx_.audio->sePlay(10);
    } else {
        anm_.play(f.node, 3, 0x87, 0);
        ctx_.audio->sePlay(11);
    }
}

void TacticsUnits::stopScriptEffect(int slot) {
    if (slot >= 0 && slot < int(scriptFx_.size())) scriptFx_[std::size_t(slot)].on = false;
}

// FUN_004761c0, state 0x1c (gates and bases): steady and busy for good,
// the stand clip once, then nothing.
void TacticsUnits::buildingState(Unit& u) {
    if (u.sub != 0) return;
    setFlags(u, true);                             // FUN_0046b0f0 / FUN_0046b0b0
    playAnim(u, 1);
    ++u.sub;
}

// FUN_00475d60: the appear clip, a second's pause, then stand.
void TacticsUnits::appearState(Unit& u) {
    switch (u.sub) {
        case 0:
            face(u, 2);
            playAnim(u, 0x12);
            ++u.sub;
            break;
        case 1:
            if (u.animEnd) {
                if (u.state == kAppear) voice(u, 0x65);
                if (u.state == kAppear2) voice(u, 0x7d);
                ++u.timer;
                ++u.sub;
            }
            break;
        case 2:
            if (u.timer > 0x3b) {
                u.timer = 0;
                ++u.sub;
            }
            break;
        case 3:
            if (u.timer > 0) {
                u.next = kStand;
                setFlags(u, false);                // FUN_0046b0b0 / FUN_0046b0f0(.., 0)
            }
            break;
        default: break;
    }
}

// FUN_00476250: the standing clip. After 60 frames (the player's units) or
// 30..210 (the computer's, re-rolled each frame) the AI takes over (state
// 0xf), or the unit holds (3) on the player's "stand" setting (+0x9f 3).
// Team 7 never moves.
void TacticsUnits::standState(Unit& u) {
    switch (u.sub) {
        case 0:
            playAnim(u, 1);
            ++u.sub;
            break;
        case 1:                                    // not while the opening runs (+0x108b48)
            if (opening_) break;
            u.timer = 0;
            ++u.sub;
            break;
        case 2: {
            const int wait = u.human() ? 0x3c : rand() % 10 * 0x14 + 0x1e;
            if (u.timer <= wait) break;
            if (u.team() != 7) u.next = u.human() && u.stance() == 3 ? 3 : 0xf;
            ++u.sub;
            break;
        }
        default: break;
    }
}

// FUN_004763d0: stand still until the setting changes.
void TacticsUnits::holdState(Unit& u) {
    if (u.sub == 0) {
        playAnim(u, 0x18);
        ++u.sub;
    }
    if (u.stance() != 3) u.next = kChoose;
}

// FUN_00476dd0: walk the route; once there the player's units stand, the
// AI's choose again (0xf). On the way the AI reacts to what it meets.
void TacticsUnits::moveState(Unit& u) {
    if (u.sub == 0) {
        plan(u);
        ++u.sub;
        return;
    }
    if (u.aiControlled && aiReact(u, true)) return;
    if (walk(u) != Walk::On) u.next = u.aiControlled ? 0xf : 2;   // FUN_00476dd0: 1..4 alike
}

// The light-wall gates (kinds 0x19b..0x19e, FUN_00469b90) let only their
// own side's units through (as in the game; on map 1 they are the party's).
// The walk itself is stopped by them in FUN_0047dec0 (see walk()).
bool TacticsUnits::restingOn(int cx, int cy, const Unit* except) const {
    for (const Unit& o : units_) {
        if (!o.used || !o.onField() || &o == except) continue;
        if (except && o.team() == except->team() && o.kind() >= 0x19b && o.kind() <= 0x19e) continue;
        const bool moving = o.state == kMove || o.state == kWander || o.state == kFlee;
        if (moving ? o.goal == tactics::Cell{cx, cy} : (o.cx() == cx && o.cy() == cy)) return true;
    }
    return false;
}

// FUN_0047d210 (the outer level, +0x2f4, units not counted): a goal in a
// straight open line is the one waypoint (FUN_00438ad0); a goal far off
// (more than 99 squared cells) or closed goes over the map's node graph
// (FUN_0044ea80; on one node it falls through); else a closed goal is
// approached as far as its line is open (FUN_00438d00) and an open one by
// the local search (FUN_00437e40). Each leg is then the inner level (+0x3e0,
// FUN_0047d730 with units counted): the same line test and search between
// the waypoints, walked cell by cell along Bresenham lines (FUN_00470a50).
// The original plans each inner leg when the unit reaches it; here all are
// planned at once. A taken goal still moves to the nearest free cell.
// (FUN_0047d830 steps the outer route to its next waypoint, +0x312, and
// at its end checks the goal, FUN_0047d190 again if missed.)
// (FUN_0047d570 is the request for one level: 4 when already on the goal
// cell, 2 while the unit's search waits in the queue, +0x2a634, FUN_00437d10;
// else it queues it, FUN_00437d90, and runs FUN_00437e40: -2 wait, -1 none,
// 1 found, the start cell kept at +0x1c. Here the search runs at once.)
// (FUN_00437c90 empties a route record, 0xec bytes: the points from +0x22,
// +0x1e the next, +0x20 the count, as path / step here.)
// A fresh plan is FUN_00476680's step 0: the walk and pause counts (AI
// +0x2c, +0x30) and the search wait (+0x4e0) start over. A retry (its steps
// 4..6, after a later search failed) keeps them.
void TacticsUnits::plan(Unit& u, bool retry) {
    // Debug: CH_PERF names each plan that took long.
    static const bool perf = SDL_getenv("CH_PERF") != nullptr;
    const std::uint64_t t0 = perf ? SDL_GetPerformanceCounter() : 0;
    if (!retry) {
        u.ai.set32(0x2c, 0);
        u.ai.set32(0x30, 0);
        u.walkPause = false;
        u.routeWait = 0;
    }
    u.routeRetry = retry;
    planRoute(u);
    if (perf) {
        const double ms = double(SDL_GetPerformanceCounter() - t0) * 1000.0 / double(SDL_GetPerformanceFrequency());
        if (ms > 2.0)
            SDL_Log("[perf] unit %d plans %d,%d -> %d,%d: %.1f ms, %zu cells", u.slot, u.cx(), u.cy(), u.goal.first,
                    u.goal.second, ms, u.path.size());
    }
}

void TacticsUnits::planRoute(Unit& u) {
    u.path.clear();
    u.step = 0;
    u.routeCut = false;
    if (!map_) return;
    const tactics::Cell from{u.cx(), u.cy()};
    const int type = u.moveType();
    const auto blocked = [&](int cx, int cy) { return restingOn(cx, cy, &u); };
    if (blocked(u.goal.first, u.goal.second)) {
        const tactics::Cell free = tactics::nearestFree(*map_, type, u.goal, from, 4, blocked);
        if (free.first >= 0) u.goal = free;
    }
    const tactics::Cell to = u.goal;
    if (to.first < 0 || to == from) return;
    u.routeCut = true;                             // until a route reaches `to`
    // The other units' cells (othersOn), gathered once for the searches.
    // The port's change: the walkers of the unit's own side and its allies
    // are left out, so a route goes through them and not round them (the
    // original closes every unit's cell here, though the walk itself has
    // always let a unit through its own side's, FUN_0047dec0 in walk(); a
    // group in a narrow way then found no route and stood waiting). Other
    // sides' units, and every building, base and gate, close their cells as before.
    const int mw = map_->cellsW(), mh = map_->cellsH();
    std::vector<std::uint8_t> taken(std::size_t(mw) * std::size_t(mh), 0);
    const auto walksAside = [&](const Unit& o) {
        return o.kind() < 0x183 && (o.team() == u.team() || relationOf(u, o) == 4);
    };
    for (const Unit& o : units_)
        if (o.used && o.onField() && &o != &u && !walksAside(o) && o.cx() >= 0 && o.cy() >= 0 && o.cx() < mw && o.cy() < mh)
            taken[std::size_t(o.cy()) * std::size_t(mw) + std::size_t(o.cx())] = 1;
    const tactics::Occupied occupied = [&](int cx, int cy) {
        return cx >= 0 && cy >= 0 && cx < mw && cy < mh && taken[std::size_t(cy) * std::size_t(mw) + std::size_t(cx)] != 0;
    };
    const auto open = [&](tactics::Cell c) { return map_->moveRate(c.first, c.second, type == 4 ? 0 : type) != 0; };
    // One level of FUN_0047d210: waypoints from `a` to `b`, false if none.
    const auto level = [&](tactics::Cell a, tactics::Cell b, const tactics::Occupied* occ, bool outer,
                           std::vector<tactics::Cell>& out) {
        out.clear();
        if (tactics::clearLine(*map_, type, a, b, occ)) {
            out.push_back(b);
            return true;
        }
        const int far = (b.first - a.first) * (b.first - a.first) + (b.second - a.second) * (b.second - a.second);
        if (outer && graph_.loaded() && (!open(b) || far > 99)) {
            const int r = tactics::coarseRoute(graph_, *map_, type, a, b, out);
            if (r == 1) return true;
            if (r != 2) return false;
            out.clear();
        }
        if (!open(b)) {
            const tactics::Cell reach = tactics::lineReach(*map_, type, a, b, occ);
            if (reach == a) return false;
            out.push_back(reach);
            return true;
        }
        return tactics::localRoute(*map_, type, a, b, occ, out) == 1;
    };
    std::vector<tactics::Cell> outer, inner;
    u.legEnd.clear();
    if (!level(from, to, nullptr, true, outer)) return;
    u.path.push_back(from);                        // path[0]: where the walk starts
    tactics::Cell at = from;
    for (std::size_t k = 0; k < outer.size(); ++k) {
        const tactics::Cell& wp = outer[k];
        if (wp == at) continue;
        if (!level(at, wp, &occupied, false, inner)) break;
        for (const tactics::Cell& p : inner) {
            for (const tactics::Cell& c : tactics::lineCells(at, p)) u.path.push_back(c);
            at = p;
        }
        u.legEnd.resize(u.path.size(), 0);
        if (k + 1 < outer.size()) u.legEnd.back() = 1;
    }
    u.step = 1;
    if (u.path.size() == 1) u.path.clear();
    u.routeCut = u.path.empty() || u.path.back() != to;
}

// (FUN_00476680 itself treats a unit acting on its own on "fixed point"
// or "standby", +0x9f 2 / 3, as arrived unless its AI record follows a
// route, +0: such a unit does not walk.)
// FUN_00476680 -> FUN_0047dec0 with the speed of FUN_00470250: towards the
// next cell's centre at rate % x speed (+0x2e) % pixels a frame, the
// vertical half that; the walking clip (3) faces the leg's direction.
// A route cut short (a later leg found no way) is searched again after a
// wait (FUN_00476680's steps 4..6: 0xb4 frames for the computer's units,
// 0x1e for the players'), as often as it takes; only a first search that
// finds nothing at all ends the walk (3).
TacticsUnits::Walk TacticsUnits::walk(Unit& u) {
    if (!map_) return Walk::Arrived;
    if (u.routeWait > 0) {                         // FUN_00476680: +0x4e0 counts down first
        if (--u.routeWait > 0) return Walk::On;
        plan(u, true);
    }
    if (u.step >= u.path.size()) {
        if (!u.routeCut) return Walk::Arrived;
        if (u.path.empty() && !u.routeRetry) return Walk::NoRoute;
        u.path.clear();
        u.step = 0;
        u.routeRetry = true;
        u.routeWait = u.human() ? 0x1e : 0xb4;
        if (u.clip == 3) playAnim(u, 1);
        return Walk::On;
    }
    if (u.aiControlled && u.ai.u8(0) != 1 && (u.stance() == 2 || u.stance() == 3)) return Walk::Arrived;
    // FUN_00476680's step 10: a unit acting on its own stands between its
    // walks, FUN_0047e2b0 counting AI +0x30 up to the record's +8.
    if (u.walkPause) {
        u.ai.set32(0x30, u.ai.s32(0x30) + 1);
        if (u.ai.s32(0x30) < u.ai.s32(8)) return Walk::On;
        u.ai.set32(0x30, 0);
        u.walkPause = false;
    }
    const GameVersion v = ctx_.version;
    const auto [tcx, tcy] = u.path[u.step];        // FUN_00438860 hands out the route's next point
    // FUN_0047da30: the route ends before a closed cell (the searches may
    // route through one when there is no other way, at a cost of 3600).
    if (map_->moveRate(tcx, tcy, u.moveType() == 4 ? 0 : u.moveType()) == 0) {
        u.path.clear();
        u.step = 0;
        u.routeCut = false;
        return Walk::Blocked;
    }
    const int tx = tcx * TacticsMap::kCellW + TacticsMap::kCellW / 2;
    const int ty = tcy * TacticsMap::kCellH + TacticsMap::kCellH / 2;
    int dx = tx - u.px(), dy = ty - u.py();
    if (dx == 0 && dy == 0) {
        // FUN_0047dbd0: at the end of an inner route, with the outer one not
        // done, a goal less than 3 cells off counts as reached.
        if (u.step < u.legEnd.size() && u.legEnd[u.step] != 0) {
            const int gx = u.goal.first - tcx, gy = u.goal.second - tcy;
            if (gx * gx + gy * gy < 9) {
                u.path.clear();
                u.legEnd.clear();
                u.step = 0;
                u.routeCut = false;
                return Walk::Arrived;
            }
        }
        if (++u.step < u.path.size()) return walk(u);
        // FUN_00470840 steps aside when someone else ended on the cell: the
        // nearest free cell (FUN_004709d0 -> FUN_0043d660, range 0x1f), checked
        // again each frame by FUN_004707b0 (FUN_0043c0c0 on that cell).
        // (The table's first free cell there would be its own, as the table
        // does not count units; the port takes the nearest free cell.)
        if (restingOn(tcx, tcy, &u)) {
            const auto blocked = [&](int cx, int cy) { return restingOn(cx, cy, &u); };
            u.goal = tactics::nearestFree(*map_, u.moveType(), {tcx, tcy}, {tcx, tcy}, 4, blocked);
            plan(u);
            if (u.path.empty()) u.routeCut = false;   // nowhere to step aside: stay
            return u.path.empty() ? Walk::Arrived : Walk::On;
        }
        if (u.routeCut) return walk(u);            // the cut: wait, then search again
        return Walk::Arrived;
    }
    const int type = u.moveType();
    const float s = float(speedRate(*map_, u.cx(), u.cy(), type)) * 0.01f * float(u.data.u16(0x2e)) * 0.01f * speedScale(u);
    // FUN_00470250: the angle a float from the CRT's double atan2, its cosine
    // and sine from the double cos / sin (FUN_0056d9d4 / FUN_0056d924), each
    // kept as a float (see degrees()).
    const float a = static_cast<float>(std::atan2(double(dy), double(dx)));
    // The original clamps each component to the distance left.
    const float vx = std::clamp(static_cast<float>(std::cos(double(a))) * s, -float(std::abs(dx)), float(std::abs(dx)));
    const float vy = std::clamp(static_cast<float>(std::sin(double(a))) * s, -float(std::abs(dy)), float(std::abs(dy)));
    std::int32_t stepX = static_cast<std::int32_t>(vx * 65536.0f), stepY = static_cast<std::int32_t>(vy * 65536.0f) / 2;
    // FUN_0047dec0: the cell ahead (+0x4dc) holds another unit (FUN_0043c0c0's
    // count, more than the unit itself): the first there in the field list
    // (FUN_0047e320) stops the walk when it is of another side and not an
    // ally (4, which FUN_00476680 returns as 2); of ours or an ally no
    // slower than this one, it halves the step. This is also what keeps
    // other sides out of the light-wall gates.
    {
        int count = 0;
        const Unit* first = nullptr;
        for (const Unit& o : units_) {
            if (!o.used || !o.onField() || o.cx() != tcx || o.cy() != tcy) continue;
            ++count;
            if (!first) first = &o;
        }
        if (count > (u.cx() == tcx && u.cy() == tcy ? 1 : 0)) {
            if (!first || (first->team() != u.team() && relationOf(u, *first) != 4)) {
                u.path.clear();
                u.step = 0;
                u.routeCut = false;
                return Walk::Blocked;
            }
            if (u.data.u16(0x2e) <= first->data.u16(0x2e)) {
                stepX /= 2;
                stepY /= 2;
            }
        }
    }
    // FUN_0047dec0: a unit acting on its own on "defensive" (+0x9f 1) steps
    // on only while its post (+0x2e8) lies within its sight (FUN_0047d010)
    // or it is already heading there (FUN_0047d0e0); else it heads for the
    // post (FUN_0047d190, 3), which FUN_00476680 searches for after 0x3c
    // frames (0x1e for the players' units).
    if (u.aiControlled && u.stance() == 1 && u.post.first >= 0 && u.goal != u.post &&
        !map_->inRange(u.cx(), u.cy(), u.post.first, u.post.second, sight(u))) {
        u.goal = u.post;
        u.path.clear();
        u.step = 0;
        u.routeCut = true;
        u.routeWait = u.human() ? 0x1e : 0x3c;
        if (u.clip == 3) playAnim(u, 1);
        return Walk::On;
    }
    u.x += stepX;
    u.y += stepY;
    // The facing: the leg from the previous cell, by angle in 11-degree
    // steps, from the straight-leaning table near the axes.
    const auto [fcx, fcy] = u.path[u.step - 1];
    const std::uint8_t dir = facing(tcx - fcx, tcy - fcy, v);
    if (u.clip != 3 || u.dir != dir) {
        face(u, dir);
        const int keepTimer = u.timer;
        playAnim(u, 3);
        u.timer = static_cast<std::int16_t>(keepTimer);
    }
    // FUN_0047e240: it walks for the AI record's +4 frames (+0x2c), then
    // (FUN_0047dec0's 2) stands for +8 when that is set, the stand clip.
    if (u.aiControlled) {
        u.ai.set32(0x2c, u.ai.s32(0x2c) + 1);
        if (u.ai.s32(0x2c) >= u.ai.s32(4)) {
            u.ai.set32(0x2c, 0);
            u.ai.set32(0x30, 0);
            if (u.ai.s32(8) != 0) {
                playAnim(u, 1);
                u.walkPause = true;
            }
        }
    }
    return Walk::On;
}

namespace {
// FUN_0043ab10 (from the map's setup, FUN_0043a060): the placement table,
// 65 x 65 offsets {dx, dy, distance}. FUN_0043b110 floods the steps from
// the centre (64 down by 1 a step), so the distance is the steps' count,
// at most 64. Each offset's key is its distance * 0x100 plus its place
// among the equal ones counted from the end; within 6 cells those whose
// dx + dy is odd take 0x8000 off (in u16: they go last, which spaces the
// units out); then the offsets are sorted by the key.
struct AppearOffset { std::int8_t dx, dy; std::uint16_t dist; };
const std::vector<AppearOffset>& appearTable() {
    static const std::vector<AppearOffset> table = [] {
        constexpr int n = 0x41, half = n / 2;
        struct E { AppearOffset o; std::uint16_t key; };
        std::vector<E> e(std::size_t(n * n));
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const int dx = x - half, dy = y - half;
                const int d = std::min(std::abs(dx) + std::abs(dy), n - 1);
                e[std::size_t(y * n + x)] = {{std::int8_t(dx), std::int8_t(dy), std::uint16_t(d)}, std::uint16_t(d)};
            }
        for (int d = 0; d < n * n; ++d) {
            int count = 0;
            for (int i = n * n - 1; i >= 0; --i)
                if (e[std::size_t(i)].o.dist == d) e[std::size_t(i)].key = std::uint16_t(e[std::size_t(i)].key * 0x100 + count++);
        }
        for (E& x : e) {
            const bool oddY = (x.o.dy & 1) != 0, oddX = (x.o.dx & 1) != 0;
            if (oddX != oddY && std::abs(x.o.dx) < 7 && std::abs(x.o.dy) < 7) x.key = std::uint16_t(x.key - 0x8000);
        }
        // An exchange sort on unique keys: the same order as std::sort.
        std::sort(e.begin(), e.end(), [](const E& a, const E& b) { return a.key < b.key; });
        std::vector<AppearOffset> out;
        out.reserve(e.size());
        for (const E& x : e) out.push_back(x.o);
        return out;
    }();
    return table;
}
} // namespace

// FUN_0043d660: the first offset within `range` (at most 0x20) of the
// table whose cell lies on the map, is open to the move type (any for 4,
// FUN_0043be60) and not given out since the last FUN_0043d4e0 (+0x263c8,
// up to 40: FUN_0043d530 looks, FUN_0043d5b0 adds). The table is walked
// in groups of five, each group's order turned by a counter (+0x263c4)
// that moves on with every call. The original does not count the units
// standing there (a newcomer may land on one and step aside later); the
// port skips those cells, keeping the table's order.
tactics::Cell TacticsUnits::appearCell(int cx, int cy, int range, int moveType) {
    if (!map_) return {-1, -1};
    range = std::clamp(range, 0, 0x20);
    std::array<int, 5> order{0, 1, 2, 3, 4};
    appearTurn_ = (appearTurn_ + 1) % 5;
    for (int i = 0; i < appearTurn_ * 3; ++i) std::swap(order[0], order[std::size_t(i % 5)]);
    const std::vector<AppearOffset>& t = appearTable();
    for (std::size_t i = 0; i < t.size(); ++i) {
        const AppearOffset& o = t[i / 5 * 5 + std::size_t(order[i % 5])];
        if (o.dist > range) continue;
        const int x = cx + o.dx, y = cy + o.dy;
        if (x < 0 || y < 0 || x >= map_->cellsW() || y >= map_->cellsH()) continue;
        if (moveType != 4 && map_->moveRate(x, y, moveType) == 0) continue;
        const tactics::Cell c{x, y};
        if (std::find(appearGiven_.begin(), appearGiven_.end(), c) != appearGiven_.end()) continue;
        if (restingOn(x, y, nullptr)) continue;
        if (appearGiven_.size() < 40) appearGiven_.push_back(c);
        return c;
    }
    return {-1, -1};
}

// FUN_0049a310 -> FUN_0043d660, then FUN_0048e790 for each.
void TacticsUnits::deployParty(int count) {
    GameWork& w = ctx_.work;
    if (!map_) return;
    const int sx = w.s16(stage::kStartX), sy = w.s16(stage::kStartY), range = std::min<int>(w.u8(stage::kStartRange), 0x20);
    const int most = w.u8(stage::kMost);
    if (most > 0) count = std::min(count, most);
    count = std::min(count, party_);
    appearGiven_.clear();                          // FUN_0043d4e0
    for (int i = 0; i < count; ++i) {
        Unit& u = units_[std::size_t(i)];
        const tactics::Cell c = appearCell(sx, sy, range, u.moveType());
        if (c.first < 0) {
            SDL_Log("[tactics] no free cell for party unit %d", i);
            continue;
        }
        if (busyNow(u)) continue;                  // FUN_0048e790 refuses a busy unit
        placeAt(u, c);
        if (tactics::traceUnits()) SDL_Log("[tactics] party unit %d appears at %d,%d", i, c.first, c.second);
    }
}

// FUN_0047fcf0 (the script's, which first empties the cells given out,
// FUN_0043d4e0) / FUN_004807e0's loop: FUN_0043d660, then FUN_00480040.
bool TacticsUnits::appearUnit(int slot, int cx, int cy, int range, bool fresh) {
    if (!map_ || slot < 0 || slot >= kSlots) return false;
    Unit& u = units_[std::size_t(slot)];
    // FUN_00480040: only a unit still off the field (FUN_0046a120 / FUN_004695d0).
    const int at = u.data.u8(0xf);
    if (!u.used || (at != 0 && at != 2) || structure(u) || base(u)) return false;
    if (fresh) appearGiven_.clear();
    const tactics::Cell c = appearCell(cx, cy, range, u.moveType());
    if (c.first < 0) return false;
    setPosition(u, c.first, c.second);
    u.data.set8(0xf, 1);
    u.next = kNoState;
    setState(u, kAppear);
    if (tactics::traceUnits()) SDL_Log("[tactics] unit %d appears at %d,%d", slot, c.first, c.second);
    return true;
}

// FUN_00430980(group, 0): none of the group's enemies stands on the field.
bool TacticsUnits::groupClear(int group) const {
    for (const Unit& u : units_)
        if (u.used && u.onField() && u.group() == group) {
            const int r = relationTo(u);
            if (r >= 1 && r <= 2) return false;
        }
    return true;
}

// FUN_0047f9d0 -> FUN_0047fa40 for teams 4..15: the group's fallen units
// wait to come in again (+0xf 2) with their HP and MP restored.
void TacticsUnits::reviveGroup(int group) {
    for (Unit& u : units_) {
        if (!u.used || u.team() < 4 || u.group() != group) continue;
        const int at = u.data.u8(0xf);
        if (at < 2 || at > 4) continue;
        u.data.set8(0xf, 2);
        u.setHp(u.maxHp());
        u.setMp(u.data.s16(0x1a));
    }
}

// FUN_0047fc60 -> FUN_0047fe70 for teams 4..15 (FUN_0047fbf0: the viewer's
// team): each of the group's units waiting off the field (FUN_0046a120)
// goes to the free cell nearest the point within `range` (FUN_0043d660;
// its list of cells already given out, +0x263c8: FUN_0043d4e0 empties it,
// FUN_0043d530 looks in it, FUN_0043d5b0 adds, up to 40; here each unit
// takes them from the placement table, appearCell) as a placement (FUN_00480040 -> message 4, FUN_0048e790).
int TacticsUnits::appearGroup(int group, int cx, int cy, int range) {
    if (!map_) return 0;
    int n = 0;
    appearGiven_.clear();                          // FUN_0047fc60's FUN_0043d4e0
    for (int team = 4; team < 0x10; ++team) {
        for (Unit& u : units_) {
            if (!u.used || u.team() != team || u.group() != group) continue;
            const int at = u.data.u8(0xf);
            if ((at != 0 && at != 2) || u.onField() || structure(u) || base(u) || busyNow(u)) continue;
            const tactics::Cell c = appearCell(cx, cy, range, u.moveType());
            if (c.first < 0) continue;
            placeAt(u, c);
            ++n;
        }
    }
    return n;
}

void TacticsUnits::setTeamGroupField(int team, int group, int field, int value) {
    const auto v8 = static_cast<std::uint8_t>(value);
    for (Unit& u : units_) {
        if (!u.used || (team < 0 ? u.team() < 4 : u.team() != team) || u.group() != group) continue;
        switch (field) {
            case 0: u.data.set8(0x9f, v8); break;
            case 1: u.data.set8(0xa0, v8); break;
            case 2: u.data.set8(0xa2, v8); break;
            case 3: u.ai.set8(0, v8); break;
            case 4: u.ai.set8(1, v8); break;
            case 5: case 6: case 7: u.ai.set32(std::uint32_t(4 * (field - 4)), value); break;
            case 0x17: u.ai.set32(0x20, value); break;
            case 0x18: u.ai.set8(0x24, v8); break;
            default:
                if (field >= 8 && field <= 0x16) u.ai.set8(std::uint32_t(0x10 + field - 8), v8);
                break;
        }
    }
}

const TacticsUnits::Unit* TacticsUnits::at(int cx, int cy) const {
    for (const Unit& u : units_)
        if (u.used && u.onField() && u.cx() == cx && u.cy() == cy) return &u;
    return nullptr;
}

// FUN_0046f440 walks the units on the field and takes the first whose box
// (FUN_0046fc20) holds the pointer; others' units only on a cell in sight
// (FUN_00440d40). The port takes the front-most of those hit (the one
// drawn on top), so the pick matches what the player sees.
const TacticsUnits::Unit* TacticsUnits::pointedUnit(int x, int y) const {
    const Unit* best = nullptr;
    for (const Unit& u : units_) {
        if (!u.used || !u.onField() || !pointedAt(u, x, y)) continue;
        if (u.team() != viewer_ && map_ && !map_->visible(u.cx(), u.cy())) continue;
        if (!best || u.py() > best->py()) best = &u;
    }
    return best;
}

int TacticsUnits::count() const {
    return int(std::count_if(units_.begin(), units_.end(), [](const Unit& u) { return u.used; }));
}

// FUN_00468f40: a unit that can be seen and picked; not while appearing
// (1, 0x19) or in states 0xc..0xe.
// FUN_00468f40: the other sides' units also only where we see them.
// FUN_00468f40: not while appearing, collapsing, falling, retreating
// (1, 0xc..0xe, 0x19); during the sortie not the fallen (+0xf 3..5). Our
// own units can be picked off the field too (from the roster, to call them
// in); the others only where we see them.
bool TacticsUnits::selectable(const Unit& u) const {
    if (!u.used) return false;
    switch (u.state) {
        case kAppear: case kCollapse: case kFall: case kRetreat: case kAppear2: return false;
        default: break;
    }
    const int f = u.data.u8(0xf);
    if (cards_ && f >= 3 && f <= 5) return false;
    if (u.team() == viewer_) return true;
    return u.onField() && seen(u);
}

// FUN_0046cbd0's test: units of other relations than ours (0) and our
// allies' (4) stand hidden outside last frame's sight.
bool TacticsUnits::seen(const Unit& u) const {
    const int rel = relation(u.team());
    return rel < 1 || rel > 3 || (map_ != nullptr && map_->visible(u.cx(), u.cy()));
}

// FUN_0046b760 -> FUN_0043d270: ours and our allies' units on the field
// light their sight (FUN_004ddbb0: unit data +0x2c, 0..32).
void TacticsUnits::stampSight(TacticsMap& map) const {
    for (const Unit& u : units_) {
        if (!u.used || !u.onField()) continue;
        const int rel = relation(u.team());
        if (u.team() != viewer_ && rel != 4) continue;
        map.see(u.cx(), u.cy(), sight(u));
    }
}

// FUN_0048d720's checks: the viewer's own units, not the bases and gates
// (kinds 0x195, 0x196, 0x19b..0x19e), able to take an order.
bool TacticsUnits::orderable(const Unit& u) const {
    const int kind = u.kind();
    if (kind == 0x195 || kind == 0x196 || (kind >= 0x19b && kind <= 0x19e)) return false;
    return selectable(u) && u.team() == viewer_ && u.onField();
}

void TacticsUnits::clearSelection() {
    for (Unit& u : units_) u.selected = false;
}

// FUN_0046f170: with an own unit in the selection the others drop out.
// FUN_0046f170: with one of ours picked the others drop out, and with one
// of ours on the field picked, ours off the field too.
void TacticsUnits::trimSelection() {
    const bool own = std::any_of(units_.begin(), units_.end(),
                                 [&](const Unit& u) { return u.used && u.selected && u.team() == viewer_; });
    const bool ownOut = std::any_of(units_.begin(), units_.end(), [&](const Unit& u) {
        return u.used && u.selected && u.team() == viewer_ && u.onField();
    });
    for (Unit& u : units_) {
        const bool card = cards_ && u.team() == viewer_ && !u.onField() && u.data.u8(0xf) < 3;
        if (!u.selected) continue;
        if ((!selectable(u) && !card) || (own && u.team() != viewer_) || (ownOut && !u.onField())) u.selected = false;
    }
}

// FUN_0046efa0(0): the front-most unit on the cell. (FUN_0046e520 lists the
// units on the field drawn on the screen, FUN_0046b130, lowest first, for
// the original's picking; here the lowest wins the same way.)
void TacticsUnits::selectAt(int cx, int cy, bool toggle) {
    if (!toggle) clearSelection();
    Unit* pick = nullptr;
    for (Unit& u : units_)
        if (selectable(u) && u.onField() && u.cx() == cx && u.cy() == cy && (!pick || u.y > pick->y)) pick = &u;
    if (pick) pick->selected = toggle ? !pick->selected : true;
    trimSelection();
}

// FUN_0046efa0(1) with FUN_0046fd70: every unit in the box.
void TacticsUnits::selectBox(int x0, int y0, int x1, int y1, bool toggle) {
    if (!toggle) clearSelection();
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    for (Unit& u : units_) {
        if (!selectable(u) || !u.onField() || u.px() < x0 || u.px() > x1 || u.py() < y0 || u.py() > y1) continue;
        u.selected = toggle ? !u.selected : true;
    }
    trimSelection();
    // FUN_0046ea10 after a drag: each picked unit of ours says a line,
    // 0x97 (or 0x96 for one off the field, +0xf 3).
    for (const Unit& u : units_) {
        if (!u.used || !u.selected || u.team() != viewer_) continue;
        const int f = u.data.u8(0xf);
        if (f <= 2) voice(u, 0x97);
        else if (f == 3) voice(u, 0x96);
    }
}

// ---------------------------------------------------------------------------
// The number keys' groups (unitctrlkeyselect.cpp).
// ---------------------------------------------------------------------------

// FUN_00493e60 (FUN_00493fe0 for each): all ten empty. FUN_00493dc0 makes
// them at construction (+0x116098, ten 0x14-slot lists).
void TacticsUnits::clearGroups() {
    for (auto& g : groups_) g.clear();
}

// FUN_00494030, while the selection holds units of ours on the field
// (+0x108b54 == 1): FUN_004941a0 takes the picked units out of every
// group, then FUN_004940b0 makes group g the picked units of ours
// (FUN_0046a4c0), marking each one's +0x98.
void TacticsUnits::storeGroup(int g) {
    if (g < 0 || g >= 10) return;
    bool own = false, field = false;
    for (const Unit& u : units_) {
        if (!u.used || !u.selected || u.team() != viewer_) continue;
        own = true;
        field = field || u.onField();
    }
    if (!own || !field) return;
    for (auto& list : groups_)
        std::erase_if(list, [&](int slot) { return units_[std::size_t(slot)].selected; });
    groups_[std::size_t(g)].clear();
    for (Unit& u : units_) {
        if (!u.used || !u.selected || !orderable(u)) continue;
        groups_[std::size_t(g)].push_back(u.slot);
        u.data.set8(0x98, static_cast<std::uint8_t>(g));
    }
}

int TacticsUnits::nextStance() const {
    for (const Unit& u : units_)
        if (u.used && u.selected && u.team() == viewer_ && u.onField()) return (u.stance() + 1) % 4;
    return -1;
}

bool TacticsUnits::recallGroup(int g, int& centre) {
    centre = -1;
    if (g < 0 || g >= 10 || groups_[std::size_t(g)].empty()) return false;
    const auto& list = groups_[std::size_t(g)];
    for (int slot : list) {
        const Unit& u = units_[std::size_t(slot)];
        if (!u.onField()) continue;
        if (u.selected) centre = slot;             // FUN_0043b400 on its position
        break;
    }
    clearSelection();
    for (int slot : list) units_[std::size_t(slot)].selected = true;
    trimSelection();
    return true;
}

// FUN_004948f0: the picked units drop, then every unit of ours in view
// (FUN_0046b1a0: x -0x20..0x420, y 0xe..0x288 on the screen) is picked.
bool TacticsUnits::selectOnScreen() {
    clearSelection();
    if (!map_) return true;
    for (Unit& u : units_) {
        if (!u.used || !u.onField() || !orderable(u)) continue;
        const int x = u.px() - map_->viewX(), y = u.py() - map_->scrollY() + TacticsMap::kViewY;
        if (y >= 0xe && y < 0x289 && x >= -0x20 && x < 0x421) u.selected = true;
    }
    trimSelection();
    return true;
}

// FUN_00494a50: on a double click on one of ours on the field, every unit
// of ours in view of the same class (the job row's +2: 2..5, the rest
// one class) is picked instead.
bool TacticsUnits::selectLike(const Unit& like) {
    const GameVersion v = ctx_.version;
    if (!like.used || !like.onField() || !orderable(like)) return false;
    const auto cls = [&](const Unit& u) { return int(logic::sdU8(kJobRows + 2 + std::uint32_t(u.data.u16(0xc)) * 0x40, v)); };
    const auto group = [](int c) { return c >= 2 && c <= 5 ? c : -1; };
    const int want = group(cls(like));
    clearSelection();
    for (Unit& u : units_) {
        if (!u.used || !u.onField() || !orderable(u) || !map_) continue;
        const int x = u.px() - map_->viewX(), y = u.py() - map_->scrollY() + TacticsMap::kViewY;
        if (y < 0xe || y >= 0x289 || x < -0x20 || x >= 0x421) continue;
        if (group(cls(u)) == want) u.selected = true;
    }
    trimSelection();
    return true;
}

bool TacticsUnits::inGroup(const Unit& u) const {
    const int g = u.group();
    if (g >= 10) return false;
    const auto& list = groups_[std::size_t(g)];
    return std::find(list.begin(), list.end(), u.slot) != list.end();
}

// ---------------------------------------------------------------------------
// The pointer (unitctrlcursor.cpp).
// ---------------------------------------------------------------------------
namespace {
// FUN_0046a250: the skills that may be used on our own side (0x6b, 0x6c,
// 0x75, 0x76, 0x1ed..0x1f2).
bool friendlySkill(int id) {
    return (id >= 0x6b && id <= 0x6c) || (id >= 0x75 && id <= 0x76) || (id >= 0x1ed && id < 499);
}
} // namespace

// FUN_00490fc0, with exactly one unit picked, ours, on the field
// (FUN_004695d0) and standing: the skill it holds ready (data +0x9d, the
// +0x9e'th of its list at +0x84; 0 or none its job's skill) decides the
// pointer. The job's skill (or one whose +5 is 3) goes to FUN_004912c0:
// on a cell in sight over a unit, 2 when the skill may help (FUN_0046a250)
// one of ours or an ally (FUN_0046a5e0), 1 on an enemy (FUN_0046a550),
// else 0. A skill aimed elsewhere needs the cell in sight (unless it
// targets the ground) and by its target (+0x18): 1 FUN_004913f0 any unit
// 3 / none 4; 2 FUN_00491440 an enemy 3 / else 4; 3 FUN_004914d0 ours or
// an ally 5 / else 6; 4 FUN_00491570 the ground 7; 5 none (FUN_004915a0);
// 6 FUN_00491600 one of its own kind 9 / else 10. +0x30995 marks too
// little MP for it (+8).
void TacticsUnits::updateCursor(bool onMap, int cx, int cy, int hover) {
    const GameVersion v = ctx_.version;
    cursorKind_ = 0;
    cursorShort_ = false;
    if (!onMap || !map_) return;
    const Unit* u = nullptr;
    int picked = 0;
    for (const Unit& w : units_)
        if (w.used && w.selected && ++picked == 1) u = &w;
    if (picked != 1 || u->team() != viewer_ || down(*u)) return;
    const int f = u->data.u8(0xf);
    if (f == 0 || (f > 1 && f < 6)) return;
    const Unit* h = hover >= 0 && hover < kSlots && units_[std::size_t(hover)].used ? &units_[std::size_t(hover)] : nullptr;
    const auto own = [&](const Unit& t) { return t.team() == viewer_; };
    const auto ally = [&](const Unit& t) { return !own(t) && relation(t.team()) == 4; };
    const auto enemy = [&](const Unit& t) { const int r = relation(t.team()); return r == 1 || r == 2; };
    const int job = logic::sdS16(kJobSkill + std::uint32_t(u->data.u16(0xc)) * 0x40, v);
    int skill = job;
    if (u->data.u8(0x9d) != 0) {
        const int armed = u->data.u16(0x84 + std::uint32_t(u->data.u8(0x9e)) * 2);
        if (armed & 0x8000) return;
        if (armed != 0) skill = armed;
    }
    std::uint32_t rec = skillRow(skill);
    if (skill != job) {
        if (logic::sdU8(rec + kSkillAim, v) != 4 && !map_->visible(cx, cy)) return;
        if (logic::sdU8(rec + kSkillType, v) == 3) skill = job;
    }
    if (skill == job) {                            // FUN_004912c0
        if (!map_->visible(cx, cy) || !h) return;
        if (friendlySkill(job) && (own(*h) || ally(*h))) cursorKind_ = 2;
        else cursorKind_ = enemy(*h) ? 1 : 0;
        return;
    }
    switch (logic::sdU8(rec + kSkillAim, v)) {
        case 1: cursorKind_ = h ? 3 : 4; break;
        case 2: cursorKind_ = h && enemy(*h) ? 3 : 4; break;
        case 3: cursorKind_ = h && (own(*h) || ally(*h)) ? 5 : 6; break;
        case 4: cursorKind_ = 7; break;
        case 6: cursorKind_ = h && h->kind() == u->kind() ? 9 : 10; break;
        default: break;
    }
    cursorShort_ = u->mp() < logic::sdS16(rec + kSkillMpCost, v);
}

// FUN_00491680 (not while an event runs, FUN_00454c40): common.bin frame
// 0x226 + kind at the pointer, priority 1, with frame 0x231 over it when
// MP falls short.
void TacticsUnits::drawCursor(int x, int y) const {
    if (cursorShort_)
        ctx_.draw.sprite(SpriteCmd{.bank = kHudBank, .frame = 0x231, .x = x, .y = y, .flags = kSprBank, .priority = 1});
    ctx_.draw.sprite(SpriteCmd{.bank = kHudBank, .frame = static_cast<std::uint16_t>(0x226 + cursorKind_), .x = x, .y = y,
                               .flags = kSprBank, .priority = 1});
}

// ---------------------------------------------------------------------------
// The command bar (unitctrlwindow.cpp). Buttons are 0x24 square:
// DAT_0061adb8 {x, y} x 20 (8 bytes each): 0 the skill-mode switch
// (0x19d, 0x260), 1 the call-in mark (0x19d, 0x2b0), 2 retreat (0x19d,
// 0x2d9), 3 an unused one (0x19d, 0x289), 4..11 the skills (DAT_0061add8,
// y 0x260), 12..15 the stances (DAT_0061ae18, y 0x289), 16..19 a second
// row nothing uses (DAT_0061ae38).
// ---------------------------------------------------------------------------
namespace {
constexpr std::uint32_t kBarButtons = 0x61adb8;
struct BarPos { int x, y; };
BarPos barButton(int i, GameVersion v) {
    const std::uint32_t e = kBarButtons + std::uint32_t(i) * 8;
    return {logic::sdS32(e, v), logic::sdS32(e + 4, v)};
}
// FUN_00470070
bool inBox(int bx, int by, int w, int h, int x, int y) { return x >= bx && x < bx + w && y >= by && y < by + h; }
bool onButton(int i, int x, int y, GameVersion v) {
    const BarPos b = barButton(i, v);
    return inBox(b.x, b.y, 0x24, 0x24, x, y);
}
} // namespace

// FUN_0046a840: the player's (FUN_0046a430), on the field (+0xf 1), a
// character (FUN_00468e10: kind 1..0x2d, so no tower, base or gate) with
// more than a minute (+0x24 > 0xe10) left.
bool TacticsUnits::canRetreat(const Unit& u) const {
    const int kind = u.kind();
    return u.used && u.team() == viewer_ && u.data.u8(0xf) == 1 && kind != 0 && kind <= 0x2d &&
           (u.data.s32(0x24) > 0xe10 || !timeLimited());
}

// FUN_0046a120: waiting off the field (+0xf 0 or 2), not a building,
// base or gate.
bool TacticsUnits::callable(const Unit& u) const {
    const int f = u.data.u8(0xf);
    const int kind = u.kind();
    return u.used && (f == 0 || f == 2) && !structure(u) && !base(u) && !(kind >= 0x19b && kind <= 0x19e);
}

// FUN_0049bd90 queues command 0xd, which FUN_0048eda0 carries out: the
// stance 0..3 to +0x9f (1 makes the unit's cell its post), 4..7 the order
// to +0xa0.
void TacticsUnits::setStance(Unit& u, int stance) {
    if (u.team() != viewer_) return;
    if (stance < 4) {
        u.data.set8(0x9f, static_cast<std::uint8_t>(stance));
        if (stance == 1) u.post = {u.cx(), u.cy()};
    } else {
        u.data.set8(0xa0, static_cast<std::uint8_t>(stance - 4));
    }
}

// FUN_0049b180 (with FUN_0049b6a0 for the buttons): the call-in mark
// (FUN_0049ba60: only units of ours off the field are picked and one of
// them can come); one picked unit of ours standing holds its skill ready
// (+0x9d) while Shift is held, several drop it; F1..F8 pick that skill
// (FUN_0049be60, not readied). Below y 600 a press with one unit (or a
// release with several) works the buttons: a skill picks it (and a press
// readies it), the switch (FUN_0049bc30) flips +0x9d, a stance goes to
// every picked unit of ours standing whose stance differs (FUN_0049bc90).
// Then the retreat button (DAT_0061adc8): shown pressed while held
// (+0x2f157), released it retreats each picked unit that may
// (FUN_0049bb20, command 5: FUN_0048e990).
bool TacticsUnits::commandBar(const BarInput& in) {
    const GameVersion v = ctx_.version;
    retreatDown_ = false;
    int own = 0, field = 0;
    Unit* first = nullptr;
    for (Unit& u : units_) {
        if (!u.used || !u.selected) continue;
        if (!first) first = &u;
        if (u.team() == viewer_) {
            ++own;
            if (u.onField()) ++field;
        }
    }
    const int kind = !first ? 0 : own == 0 ? 3 : field > 0 ? 1 : 2;
    callReady_ = false;
    if (kind == 2)
        for (const Unit& u : units_)
            if (u.used && u.selected && callable(u)) callReady_ = true;
    if (!first) return false;
    Unit* lead = nullptr;
    int alive = 0;
    if (first->team() == viewer_)
        for (Unit& u : units_)
            if (u.used && u.selected && !down(u) && ++alive == 1) lead = &u;
    if (!lead) lead = first;
    if (lead->team() == viewer_) {
        if (alive == 1) {
            if (lead->onField()) {
                if (in.shift) hold(*lead, 1, -1);
                else if (in.shiftUp) hold(*lead, 0, -1);
            }
        } else {
            for (Unit& u : units_)
                if (u.used && u.selected && u.team() == viewer_ && u.onField()) hold(u, 0, -1);
        }
    }
    const int fkey = alive == 1 && in.fkey >= 0 ? in.fkey + 1 : 0;
    if (first->team() != viewer_) return false;
    const auto arm = [&](int c, bool ready) {      // FUN_0049be60
        hold(*lead, ready ? 1 : -1, c);
    };
    if (in.stance >= 0 && in.stance < 4) {
        for (Unit& u : units_)
            if (u.used && u.selected && u.team() == viewer_ && !down(u) && u.stance() != in.stance)
                queueOrder(u, {0xd, in.stance, 0, -1});
        ctx_.audio->sePlay(5);
        return true;
    }
    // FUN_0049b6a0: 0 nothing, 1 handled (the map may still see the
    // click), 2 taken.
    const auto buttons = [&](bool press) -> int {
        if (fkey != 0) {
            arm(fkey - 1, false);
            return 1;
        }
        if (!press) {
            if (in.y >= barButton(3, v).y) {
                if (in.y >= barButton(1, v).y) return 0;
                if (selectedCount() == 1 && onButton(3, in.x, in.y, v)) return 1;
                for (int i = 0; i < 4; ++i) {
                    if (onButton(12 + i, in.x, in.y, v)) {
                        for (Unit& u : units_)
                            if (u.used && u.selected && u.team() == viewer_ && !down(u) && u.stance() != i)
                                queueOrder(u, {0xd, i, 0, -1});   // FUN_0049bd90: command 0xd
                        ctx_.audio->sePlay(0);   // FUN_0049ad50(0)
                        return 1;
                    }
                    if (onButton(16 + i, in.x, in.y, v)) {   // FUN_0049bc90(1, ..) does nothing
                        ctx_.audio->sePlay(0);
                        return 1;
                    }
                }
                return 0;
            }
            if (alive != 1) return 0;
            if (!in.shift && onButton(0, in.x, in.y, v)) {
                hold(*lead, heldReady(*lead) ^ 1, -1);
                return 2;
            }
        }
        for (int c = 0; c < 8; ++c) {
            if (onButton(4 + c, in.x, in.y, v)) {
                arm(c, press);
                return 2;
            }
        }
        return 0;
    };
    bool tryButtons = true;
    bool press = false;
    if (fkey == 0) {
        if (in.y < 600 || in.dragging) return false;
        if (alive == 1 && in.pressed) {
            if (in.shift) return false;
            press = true;
        } else if (!in.released) {
            tryButtons = false;
        }
    }
    if (tryButtons) {
        const int r = buttons(press);
        if (r == 1) return false;
        if (r == 2) return true;
    }
    const BarPos rb = barButton(2, v);
    if (in.y >= rb.y && onButton(2, in.x, in.y, v)) {
        if (in.held) retreatDown_ = true;
        if (in.released) {
            if (kind == 1)
                for (Unit& u : units_)
                    if (u.used && u.selected && canRetreat(u)) queueOrder(u, {5, 0, 0, -1});   // command 5
            ctx_.audio->sePlay(3);
        }
        return true;
    }
    return false;
}

// FUN_0049c400 (not during the sortie or a drag): FUN_0049c560 the
// panel's fields (DAT_0061aea8: 0x18 boxes {x, y, w, h}, the 16th not
// used) while a unit is shown and picked, else FUN_0049c670 the bar
// (0x18c, 600, 0x18e x 0xa8): its buttons (the switch 0x14 while the
// skill is readied), the picked unit's skills by id (+0x7c + button * 2).
// The text comes from FUN_0049c9b0's tables (kHelpPanel and the others).
void TacticsUnits::updateHelp(const BarInput& in, int shown) {
    const GameVersion v = ctx_.version;
    helpKind_ = helpId_ = -1;
    if (in.sortie || in.dragging) return;
    const Unit* first = nullptr;
    for (const Unit& u : units_)
        if (u.used && u.selected) { first = &u; break; }
    if (first && in.x < 0x18c && in.y > 599 && shown >= 0) {      // FUN_0049c560
        for (int i = 0; i < 0x18; ++i) {
            if (i == 0xf) continue;
            const std::uint32_t e = 0x61aea8 + std::uint32_t(i) * 8;
            if (inBox(logic::sdS16(e, v), logic::sdS16(e + 2, v), logic::sdS16(e + 4, v), logic::sdS16(e + 6, v), in.x, in.y)) {
                helpKind_ = 0;
                helpId_ = i;
                return;
            }
        }
    }
    if (!inBox(0x18c, 600, 0x18e, 0xa8, in.x, in.y)) return;      // FUN_0049c670
    for (int c = 0; c < 0x14; ++c) {
        if (!onButton(c, in.x, in.y, v)) continue;
        if (c < 4 || c > 0xb) {
            helpKind_ = 1;
            helpId_ = c;
            if (c == 0) helpId_ = first && first->team() == viewer_ && first->data.u8(0x9d) != 0 ? 0x14 : 0;
            return;
        }
        if (first) {
            const int id = first->data.u16(0x7c + std::uint32_t(c) * 2);
            if (!(id & 0x8000) && id != 0 && id < 100) {
                helpKind_ = 2;
                helpId_ = id;
            }
        }
        return;
    }
}

// FUN_0049beb0 (not during the sortie), at priority 199: for the first
// picked unit standing (else the first) of a playable side (FUN_00468d80):
// the switch lit (frame DAT_0061ae58) while its skill is readied, its
// stance's button (0xc3 + stance), and the eight skill buttons (0xeb + id,
// 0xeb empty, 0x14f + id for a locked one), the picked one full and the
// rest dimmed, red-shifted when MP is short; under each its MP cost (or
// frame 0x242 for a +5 == 3 skill). Then the call-in mark (DAT_0061ae5a)
// and the pressed retreat button (DAT_0061ae5c).
void TacticsUnits::drawBar(bool sortie) const {
    const GameVersion v = ctx_.version;
    if (sortie) return;
    constexpr int kPrio = 199;
    const auto sprite = [&](int frame, int x, int y, std::uint8_t r = 0x80, std::uint8_t g = 0x80, std::uint8_t b = 0x80) {
        ctx_.draw.sprite(SpriteCmd{.bank = kHudBank, .frame = static_cast<std::uint16_t>(frame), .x = x, .y = y,
                                   .flags = kSprBank, .color = {r, g, b, 0xff}, .priority = kPrio});
    };
    const Unit* lead = nullptr;
    const Unit* first = nullptr;
    for (const Unit& u : units_) {
        if (!u.used || !u.selected) continue;
        if (!first) first = &u;
        if (u.human() && !down(u)) { lead = &u; break; }
    }
    if (!lead) lead = first;
    if (lead && lead->human()) {
        if (lead->data.u8(0x9d) != 0) {
            const BarPos b = barButton(0, v);
            sprite(logic::sdS16(0x61ae58, v), b.x, b.y);
        }
        const int stance = lead->stance();
        if (stance < 4) {
            const BarPos b = barButton(12 + stance, v);
            sprite(0xc3 + stance, b.x, b.y);
        }
        for (int c = 0; c < 8; ++c) {
            const BarPos b = barButton(4 + c, v);
            const int id = lead->data.u16(0x84 + std::uint32_t(c) * 2);
            int frame = 0xeb;
            bool shortMp = false;
            if ((id & 0x7fff) != 0 && !(id & 0x8000)) {
                frame = 0xeb + id;
                const std::uint32_t rec = skillRow(id);
                shortMp = lead->mp() < logic::sdS16(rec + kSkillMpCost, v);
                if (logic::sdU8(rec + kSkillType, v) == 3) {
                    sprite(0x242, b.x + 3, b.y + 0x16);
                } else {
                    char cost[8];
                    std::snprintf(cost, sizeof cost, "%3d", int(logic::sdS16(rec + kSkillMpCost, v)));
                    ctx_.draw.text({.x = b.x + 0x10, .y = b.y + 0x18, .priority = kPrio, .argb = 0xffffffffu,
                                    .text = cost, .fontPx = 10});
                }
            } else if (id & 0x8000) {
                frame = (id & 0x7fff) + 0x14f;
            }
            std::uint8_t r = 0x80, g = 0x80, bl = 0x80;
            if (lead->data.u8(0x9e) != c) r = g = bl = 0x40;
            if (shortMp) { g /= 2; bl /= 2; }
            sprite(frame, b.x, b.y, r, g, bl);
        }
    }
    if (callReady_) {
        const BarPos b = barButton(1, v);
        sprite(logic::sdS16(0x61ae5a, v), b.x, b.y);
    }
    if (retreatDown_) {
        const BarPos b = barButton(2, v);
        sprite(logic::sdS16(0x61ae5c, v), b.x, b.y);
    }
}

// FUN_0049c850: the help lines at (0x1d1, 0x2b9 / 0x2ce / 0x2dc / 0x2ea),
// priority 0xc4; the first not for a network watcher (FUN_004577f0), the
// rest not in network play (FUN_00456b20, DAT_007f4491).
void TacticsUnits::drawHelp() const {
    if (helpKind_ < 0 || helpId_ < 0) return;
    const StringTable* table = helpKind_ == 0 ? &kHelpPanel : helpKind_ == 1 ? &kHelpBar : helpKind_ == 2 ? &kHelpSkill : nullptr;
    if (!table || std::size_t(helpId_) * 4 >= table->count) return;
    const auto& lines = strings(ctx_.version, *table);
    static constexpr int kY[4] = {0x2b9, 0x2ce, 0x2dc, 0x2ea};
    const bool network = ctx_.work.u8(stage::kNetwork) != 0;
    for (int i = 0; i < 4; ++i) {
        const std::string& t = lines[std::size_t(helpId_) * 4 + std::size_t(i)];
        if (t.empty() || (i > 0 && network)) continue;
        ctx_.draw.text({.x = 0x1d1, .y = kY[i], .priority = 0xc4, .argb = 0xffffffffu, .text = t,
                        .fontPx = static_cast<std::uint16_t>(i == 0 ? 14 : 12)});
    }
}

// FUN_0049cc60: no panel during the sortie (+0x115cb8 0), else 1 (2
// with the call-in mark) and FUN_0049ce90: the unit under the pointer
// (+0x30992) when its cell is in sight; else, with units picked, the
// first of ours among them, or the first picked when in sight. A new one
// restarts the panel's model (a copy of the unit's animation, FUN_0046c040
// facing 2, clip 1); it runs a frame (FUN_0040a100) each time.
int TacticsUnits::updatePanel(int hover, bool sortie) {
    if (sortie) {
        panelMode_ = 0;
        return -1;
    }
    panelMode_ = callReady_ ? 2 : 1;
    const int before = panelShown_;
    panelShown_ = -1;
    if (!map_) return -1;
    int pick = -1;
    if (hover >= 0 && hover < kSlots && units_[std::size_t(hover)].used) {
        const Unit& h = units_[std::size_t(hover)];
        if (map_->visible(h.cx(), h.cy())) pick = hover;
    }
    if (pick < 0) {
        const Unit* first = nullptr;
        for (const Unit& u : units_) {
            if (!u.used || !u.selected) continue;
            if (!first) first = &u;
            if (u.team() == viewer_) { pick = u.slot; break; }
        }
        if (pick < 0 && first && map_->visible(first->cx(), first->cy())) pick = first->slot;
    }
    if (pick < 0) return -1;
    const Unit& u = units_[std::size_t(pick)];
    if (pick != before) {
        if (panelNode_ != AnmPlayer::kNone) anm_.freeRoot(panelNode_);
        panelNode_ = AnmPlayer::kNone;
        if (u.set >= 0) {
            const GameVersion v = ctx_.version;
            panelNode_ = anm_.newRoot(sets_[std::size_t(u.set)]->anm);
            const std::uint32_t id = std::uint32_t(sets_[std::size_t(u.set)]->file + 8);
            const std::uint32_t rows = sdU32(kClipRows + id * 4, v), clips = sdU32(kClips + id * 4, v);
            // FUN_0046c040(.., 2): kinds 0x183..0x196 and 0x19b..0x19e keep
            // their own facing (FUN_0046c290).
            const int kind = u.kind();
            const int dir = (kind >= 0x183 && kind <= 0x196) || (kind >= 0x19b && kind <= 0x19e) ? u.dir : 2;
            const std::uint32_t e = rows + (std::uint32_t(logic::sdU8(kDirColumn + std::uint32_t(dir), v)) + 1u * 8) * 2;
            const std::uint16_t clip = sdU16(clips + std::uint32_t(logic::sdU8(e, v)) * 2, v);
            anm_.play(panelNode_, clip >> 8, clip & 0xff, logic::sdU8(e + 1, v) & 3);
        }
    }
    panelShown_ = pick;
    if (panelNode_ != AnmPlayer::kNone) anm_.update(panelNode_);
    return pick;
}

// FUN_0049ad90: a party member of ours (characters 1..12) may say a line
// for the event: DAT_0061a394 [event] below 100, else the byte at
// 0x61a358 + event, gives the line (-1 none). Most events speak half the
// time (the CRT's rand): 3, 0x70, 0x71 and 0x96 always, 0x6a one in five,
// 0x6c..0x6e three in five, 0x7d two in five, 0x94 / 0x95 three in ten,
// 0x97 45 in 100. Lines 6 / 0x2f, 9 / 8 and 0x26 / 0x27 swap at times;
// the line picks one of the character's nine voices (FUN_00459080 loads
// them into SE group character + 6), played in slot character + 9.
void TacticsUnits::voice(const Unit& u, int event) {
    const GameVersion v = ctx_.version;
    if (!u.used || !orderable(u)) return;
    const int ch = u.kind();
    if (ch == 0 || ch > 0xc || event < 0) return;
    int line = static_cast<std::int8_t>(logic::sdU8(event < 100 ? 0x61a394 + std::uint32_t(event) : 0x61a358 + std::uint32_t(event), v));
    if (line == -1) return;
    int r = std::rand() % 100;
    switch (event) {
        case 3: case 0x70: case 0x71: case 0x96: break;
        case 0x6a: if (r < 0x50) return; break;
        case 0x6c: case 0x6d: case 0x6e: if (r < 0x28) return; break;
        case 0x7d: if (r < 0x3c) return; break;
        case 0x94: case 0x95: if (r < 0x46) return; break;
        case 0x97: if (r < 0x37) return; break;
        default: if (r < 0x32) return; break;
    }
    r = std::rand() % 100;
    switch (line) {
        case 6: if (r < 0x28) line = 0x2f; break;
        case 9: if (r < 0x32) line = 8; break;
        case 0x26: if (r < 0x32) line = 0x27; break;
        case 0x27: if (r < 0x32) line = 0x26; break;
        case 0x2f: if (r < 0x3c) line = 6; break;
        default: break;
    }
    int index = -1;
    switch (line) {
        case 2: index = 4; break;
        case 4: index = 5; break;
        case 6: index = 0; break;
        case 7: index = 1; break;
        case 8: index = 2; break;
        case 0x1f: case 0x20: index = 6; break;
        case 0x22: index = 8; break;
        case 0x24: index = 7; break;
        case 0x2f: index = 3; break;
        default: return;
    }
    ctx_.audio->sePlayIn(std::uint32_t(ch + 6), std::uint32_t(index), ch + 9);   // FUN_0040d390
}

// FUN_0049ab60 also rebuilds a text sprite of the state's name
// (PTR_PTR_0061a2a4 [character] [state], developer labels such as
// "待命：通常") at the face's place (DAT_00617ef0), but nothing draws it;
// the port keeps only the line.
void TacticsUnits::talk(bool sortie) {
    if (sortie) return;
    for (Unit& u : units_) {
        if (!u.used || u.face < 0 || u.team() != viewer_ || u.talkState == u.state) continue;
        u.talkState = u.state;
        voice(u, u.state);
    }
}

SDL_Rect TacticsUnits::barBox(int button) const {
    const BarPos b = barButton(button, ctx_.version);
    return {b.x, b.y, 0x24, 0x24};
}

// A face's x on the roster (DAT_00617ef0). With the port's wider view the
// left column (faces 0..9) keeps to the view's left edge and the right one
// to its right edge.
int TacticsUnits::faceX(int face) const {
    const int margin = map_ ? map_->margin() : 0;
    return logic::sdS16(0x617ef0 + std::uint32_t(face) * 4, ctx_.version) + (face < 10 ? -margin : margin);
}

bool TacticsUnits::faceBox(const Unit& u, SDL_Rect& box) const {
    if (!u.used || u.face < 0 || u.team() != viewer_) return false;
    const GameVersion v = ctx_.version;
    box = {faceX(u.face), logic::sdS16(0x617ef0 + std::uint32_t(u.face) * 4 + 2, v),
           0x32, 0x39};
    return true;
}

int TacticsUnits::ownSelected() const {
    return int(std::count_if(units_.begin(), units_.end(),
                             [&](const Unit& u) { return u.used && u.selected && u.team() == viewer_; }));
}

int TacticsUnits::selectedCount() const {
    return int(std::count_if(units_.begin(), units_.end(), [](const Unit& u) { return u.used && u.selected; }));
}

// FUN_0048f100 (from FUN_0048d720 for each unit it gives an order):
// effect 0x412 on the unit's model, 0x30 px up (the original's
// model-attached effect call, which is otherwise not ported).
void TacticsUnits::orderMark(Unit& u) {
    playEffect(u, 5, 0x412);
    u.fxDy = -0x30;
}

// FUN_0048d720's first case -> FUN_004812c0: with ours waiting off the
// field picked (+0x2f156) and the right button on one of ours standing on
// the field (FUN_00469660; not a gate or a base), FUN_00480fd0 lists the
// picked who may come (FUN_0046a7a0) and, unless a call is already queued
// (FUN_0048e020 0xb / 0xc), FUN_00481360 sends it command 11 with the list
// (FUN_004813f0) and the order mark (FUN_0048f100). FUN_004815c0 then
// ends the call mode.
bool TacticsUnits::orderCall(int slot) {
    if (!callReady_ || slot < 0 || slot >= kSlots) return false;
    Unit& c = units_[std::size_t(slot)];
    const int kind = c.kind();
    if (!c.used || base(c) || (kind >= 0x19b && kind <= 0x19e) || down(c) || !c.onField() || c.team() != viewer_) return false;
    switch (c.state) {
        case kAppear: case kCollapse: case kFall: case kAppear2: return false;   // FUN_00469660
        default: break;
    }
    std::vector<std::int16_t> list;                  // FUN_00480f50 empties it first (+0x108f48)
    for (const Unit& o : units_)
        if (o.used && o.selected && o.team() == viewer_ && callable(o)) list.push_back(static_cast<std::int16_t>(o.slot));
    if (list.empty()) return true;
    for (const Unit::Order& o : c.orders)
        if (o.type == 0xb || o.type == 0xc) return true;
    c.callOrder = list;
    orderMark(c);
    queueOrder(c, {0xb, 0, 0, -1});
    if (tactics::traceUnits()) SDL_Log("[tactics] unit %d calls %zu waiting unit(s) in", c.slot, list.size());
    return true;
}

// FUN_00481610: the call drag's step for the frame, by the button's state
// (+0x19b8: 1 pressed, 2 held, 3 let go, 0 up). Without ours waiting picked
// an armed or running drag ends (FUN_004815c0). The original's +0x2a344,
// set at the start and the end, is read nowhere.
bool TacticsUnits::callDrag(const BarInput& in) {
    const int button = in.released ? 3 : in.pressed ? 1 : in.held ? 2 : 0;
    const bool had = callState_ >= 2;
    if (in.pressed) {
        callPressX_ = in.x;
        callPressY_ = in.y;
    }
    if (!callReady_) {
        if (callState_ == 1 || callState_ == 2) endCallDrag();
        return had;
    }
    if (callState_ < 2) {                          // FUN_004816c0
        if (button == 0) {
            if (callState_ != 0) endCallDrag();
        } else if (button == 1) {
            // FUN_0046f8b0(.., 0): the roster column the press is in.
            const GameVersion v = ctx_.version;
            const auto r = [&](std::uint32_t va) { return logic::sdS32(va, v); };
            int column = 0;
            if (r(0x617ed0) <= in.x && in.x <= r(0x617ed8) && r(0x617ed4) <= in.y && in.y <= r(0x617edc)) column |= 1;
            if (r(0x617ee0) <= in.x && in.x <= r(0x617ee8) && r(0x617ee4) <= in.y && in.y <= r(0x617eec)) column |= 2;
            if (in.faces && column != 0 && callFaceAt(in.x, in.y, column)) callState_ = 1;
        } else if (button == 2 && callState_ != 0 && (std::abs(in.x - callPressX_) > 4 || std::abs(in.y - callPressY_) > 4)) {
            // FUN_00412810: past the system's drag distance. FUN_00481a60
            // lets go of the picked who may not come (FUN_0046a7a0), then
            // FUN_0046f170; FUN_00481ef0(0) starts the ring.
            for (Unit& u : units_)
                if (u.used && u.selected && !callable(u)) u.selected = false;
            trimSelection();
            ring_ = {};
            callState_ = 2;
        }
    } else if (callState_ == 2) {                  // FUN_00481810
        if (button == 2) tickCallRing(in.x, in.y);
        else if (button == 3) {
            // FUN_00481bf0: the first of ours on the field under the
            // pointer calls the picked in (FUN_004812c0), and the drag ends.
            for (const Unit& c : units_) {
                if (!c.used || c.team() != viewer_ || c.data.u8(0xf) != 1 || !pointedAt(c, in.x, in.y)) continue;
                orderCall(c.slot);
                endCallDrag();
                break;
            }
        } else if (button != 1) {
            endCallDrag();
        }
        // (A press while the drag runs, button 1, cannot happen: the
        // original asserts there.)
    }
    return had;
}

// FUN_00481580: the drag ends (+0x2f15b 0); FUN_004815c0 also drops the
// map's box (+0x2a340 0), which the port's map never started for a face.
void TacticsUnits::endCallDrag() noexcept {
    callState_ = 0;
}

// FUN_004818e0: a face of the press's column (0..9 left, 10..19 right)
// under the pointer (FUN_0046fac0(0): 0x32 x 0x37 from its place) whose
// unit is picked and may be called (FUN_0046a7a0).
bool TacticsUnits::callFaceAt(int x, int y, int column) const {
    const GameVersion v = ctx_.version;
    for (const Unit& u : units_) {
        if (!u.used || u.face < 0 || u.team() != viewer_) continue;
        if (!(column & (u.face < 10 ? 1 : 2))) continue;
        const int fx = faceX(u.face);
        const int fy = logic::sdS16(0x617ef0 + std::uint32_t(u.face) * 4 + 2, v);
        if (x >= fx && x <= fx + 0x32 && y >= fy && y <= fy + 0x37 && callable(u) && u.selected) return true;
    }
    return false;
}

// FUN_0046fc20: the pointer (inside the view, y 0x1e..599) on the unit's
// box: DAT_00615e5c [size] {dx, dy, half width, half height}, the size
// from DAT_00615eac [kind * 2] (FUN_00466fc0, +0x502).
bool TacticsUnits::pointedAt(const Unit& u, int x, int y) const {
    if (!map_ || y <= 0x1d || y >= 600) return false;
    const GameVersion v = ctx_.version;
    const std::uint32_t box = 0x615e5c + std::uint32_t(logic::sdU8(0x615eac + std::uint32_t(u.kind()) * 2, v)) * 4;
    const int sx = u.px() - map_->viewX() + sdS8(box, v);
    const int sy = u.py() - map_->scrollY() + 0x1e + sdS8(box + 1, v);
    return std::abs(x - sx) <= sdS8(box + 2, v) && std::abs(y - sy) <= sdS8(box + 3, v);
}

// FUN_00481fc0: the ring follows the pointer. Pattern 0 turns 2 degrees
// a frame and swells 0..0x2d and back by its step; pattern 1 turns a
// degree every fourth frame (FUN_00481ef0 only ever starts pattern 0).
void TacticsUnits::tickCallRing(int x, int y) {
    ring_.x = x;
    ring_.y = y;
    if (ring_.mode == 1) {
        if (++ring_.swell > 3) {
            ring_.swell = 0;
            ring_.angle = (ring_.angle + 1) % 0x168;
        }
        return;
    }
    ring_.angle = (ring_.angle + 2) % 0x168;
    ring_.swell += ring_.step;
    if (ring_.swell > 0x2c) {
        ring_.step = -ring_.step;
        ring_.swell = 0x2d;
    }
    if (ring_.swell < 0) {
        ring_.step = -ring_.step;
        ring_.swell = 0;
    }
}

// FUN_00482150 (from FUN_0046b670), while the drag runs and the button is
// not being let go (FUN_00481b90): the picked faces spread evenly round
// the pointer, drawn by FUN_0046df90(.., 1) (grey 0x90, alpha 0xb8, at
// 199). Pattern 0 (FUN_00482210): a circle of radius n * 3.3 + 64 scaled
// by cos(swell); pattern 1 (FUN_00482370): x = cos(t) * 120, y =
// sin(9t) * 120; pattern 2 (FUN_004824a0) draws nothing.
void TacticsUnits::drawCallRing() const {
    if (callState_ < 2) return;
    std::vector<const Unit*> picked;
    for (const Unit& u : units_)
        if (u.used && u.selected) picked.push_back(&u);
    const int n = int(picked.size());
    if (n == 0 || ring_.mode == 2) return;
    constexpr float kPi = 3.141592f, kRad = 0.017453289f;
    const double swell = std::cos(double(float(ring_.swell) / 180.0f * kPi));
    for (int i = 0; i < n; ++i) {
        int x = 0, y = 0;
        if (ring_.mode == 0) {
            const double t = double((360.0f / float(n) * float(i) + float(ring_.angle)) / 180.0f * kPi);
            const double r = double(float(n) * 3.3f + 64.0f);
            x = int(std::cos(t) * r * swell + ring_.x);
            y = int(std::sin(t) * r * swell + ring_.y);
        } else {
            const int k = (int(float(i) * (360.0f / float(n))) + ring_.angle) % 0x168;
            const double t = double(float(k) * kRad);
            x = int(std::cos(t) * 120.0) + ring_.x;
            y = int(std::sin(9.0 * t) * 120.0) + ring_.y;
        }
        if (picked[std::size_t(i)]->team() == viewer_) drawFace(*picked[std::size_t(i)], x, y, 199, 0x90, 0x90, 0x90, 0xb8);
    }
}

// FUN_0048d720 case 0x6e: each picked unit of ours is sent command 1 to
// the cell (with the mark, FUN_0048f100).
void TacticsUnits::orderMinimap(int cx, int cy) {
    for (Unit& u : units_) {
        if (!u.selected || !orderable(u) || (u.cx() == cx && u.cy() == cy)) continue;
        orderMark(u);
        queueOrder(u, {1, cx, cy, -1});
    }
    // FUN_004728e0: the cell's centre; the clip from its start.
    groundX_ = cx * TacticsMap::kCellW + 0x10;
    groundY_ = cy * TacticsMap::kCellH + 8;
    if (groundMark_ != AnmPlayer::kNone) anm_.play(groundMark_, 1, 0x58, 0);
}

// ---------------------------------------------------------------------------
// The orders (unitctrlcommand.cpp). The original pushes each command on
// the task's input queue; FUN_0048ce60 -> FUN_0048d5d0 moves the frame's
// commands to the queue FUN_0048cd90 reads (recording them for a replay,
// FUN_00496690, which never runs), and FUN_0048cd90 hands each to its unit
// (asserting it is in use, FUN_00468d10) through FUN_0048dcd0. The port
// gives them to the unit at once. In network play the commands are
// exchanged between the players first (the Network category).
// ---------------------------------------------------------------------------

// FUN_0048dcd0: unless the new command is one that waits its turn
// (FUN_0048e1b0: 0xc; FUN_0048e140: 9, 10, 0xc, 0xd, 0xe, 0x11), the
// queued ones that do not wait are dropped first (FUN_0048df50); then it
// goes on the end (FUN_0048dd60).
void TacticsUnits::queueOrder(Unit& u, const Unit::Order& o) {
    if (netSink_ && !applyingNet_) {
        // Network play: the order goes out; the call's list follows it, one
        // command 0xc per unit called (the original's 0xc carries the rest
        // of a call's list too).
        netSink_({.type = o.type, .unit = std::int16_t(u.slot), .target = std::int16_t(o.target),
                  .cx = std::int16_t(o.cx), .cy = std::int16_t(o.cy), .extra = std::int16_t(o.skill)});
        if (o.type == 0xb)
            for (const std::int16_t s : u.callOrder) netSink_({.type = 0xc, .unit = std::int16_t(u.slot), .extra = s});
        return;
    }
    const auto waits = [](int t) { return t == 9 || t == 10 || t == 0xc || t == 0xd || t == 0xe || t == 0x11; };
    if (o.type != 0xc && !waits(o.type))
        std::erase_if(u.orders, [&](const Unit::Order& q) { return q.type == 0xc || !waits(q.type); });
    u.orders.push_back(o);
}

void TacticsUnits::hold(Unit& u, int ready, int index) {
    const int r = ready < 0 ? heldReady(u) : ready;
    const int i = index < 0 ? (u.netIndex >= 0 ? u.netIndex : u.data.u8(0x9e)) : index;
    if (!netSink_) {
        u.data.set8(0x9d, std::uint8_t(r));
        u.data.set8(0x9e, std::uint8_t(i));
        return;
    }
    if (r == heldReady(u) && i == (u.netIndex >= 0 ? u.netIndex : u.data.u8(0x9e))) return;
    u.netReady = std::int8_t(r);
    u.netIndex = std::int8_t(i);
    netSink_({.type = 0x40, .unit = std::int16_t(u.slot), .cx = std::int16_t(r), .cy = std::int16_t(i)});
}

void TacticsUnits::applyNet(int slot, const net::Command& c) {
    const auto refuse = [&](const char* why) {
        refusedMask_ = std::uint8_t(refusedMask_ | (1u << (slot & 7)));
        SDL_Log("[net] refused command %d for unit %d from player %d: %s", c.type, c.unit, slot, why);
    };
    // A call's list ends with the first command that is not one of its 0xc.
    if (!(c.type == 0xc && c.unit == netCall_.slot && slot == netCall_.sender)) endNetTurn();
    if (c.unit < 0 || c.unit >= kSlots) return refuse("no such unit");
    Unit& u = units_[std::size_t(c.unit)];
    if (!u.used || u.team() != slot) return refuse("not the sender's unit");
    const auto validSlot = [&](int s) { return s >= 0 && s < kSlots && units_[std::size_t(s)].used; };
    switch (c.type) {
        case 1:
            if (!map_ || c.cx < 0 || c.cy < 0 || c.cx >= map_->cellsW() || c.cy >= map_->cellsH()) return refuse("cell off the map");
            break;
        case 3:
            if (!validSlot(c.target)) return refuse("no such target");
            if (c.extra != -1 && (c.extra <= 0 || c.extra >= 0x400)) return refuse("no such skill");
            break;
        case 5: break;
        case 0xd:
            if (c.cx < 0 || c.cx > 3) return refuse("no such stance");
            break;
        case 0xb:
            netCall_ = {c.unit, slot, {}};
            return;
        case 0xc:
            if (netCall_.slot != c.unit || !validSlot(c.extra) || units_[std::size_t(c.extra)].team() != slot)
                return refuse("call list out of place");
            netCall_.list.push_back(c.extra);
            return;
        case 0x40:
            if (c.cx < 0 || c.cx > 1 || c.cy < 0 || c.cy > 7) return refuse("no such skill slot");
            u.data.set8(0x9d, std::uint8_t(c.cx));
            u.data.set8(0x9e, std::uint8_t(c.cy));
            if (u.netReady == c.cx && u.netIndex == c.cy) u.netReady = u.netIndex = -1;
            return;
        default:
            return refuse("unknown order");
    }
    applyingNet_ = true;
    queueOrder(u, {c.type, c.cx, c.cy, c.target, c.type == 3 ? c.extra : -1});
    applyingNet_ = false;
}

void TacticsUnits::endNetTurn() {
    if (netCall_.slot < 0) return;
    Unit& caller = units_[std::size_t(netCall_.slot)];
    caller.callOrder = netCall_.list;
    applyingNet_ = true;
    queueOrder(caller, {0xb, 0, 0, -1});
    applyingNet_ = false;
    netCall_ = {};
}

std::uint32_t TacticsUnits::stateHash() const {
    std::uint32_t h = 0x811c9dc5u;
    const auto mix = [&](std::uint32_t v) { h = (h ^ v) * 0x01000193u; };
    for (const Unit& u : units_) {
        if (!u.used) continue;
        mix(std::uint32_t(u.slot));
        mix(std::uint32_t(u.x));
        mix(std::uint32_t(u.y));
        mix(u.state | u.sub << 8 | u.data.u8(0xf) << 16 | std::uint32_t(u.team()) << 24);
        mix(std::uint32_t(u.data.u16(0x16)) | std::uint32_t(u.data.u16(0x1c)) << 16);
    }
    return h;
}

// FUN_0048e020: a command of the type is waiting.
bool TacticsUnits::hasOrder(const Unit& u, int type) {
    return std::any_of(u.orders.begin(), u.orders.end(), [&](const Unit::Order& q) { return q.type == type; });
}

// FUN_0048e2a0, once a frame from FUN_0046b760 while the battle runs: the
// first command is tried and, once carried out, dropped (FUN_0048dee0); a
// busy unit (FUN_004693b0) keeps it for the next frame.
//   1 FUN_0048e5d0: walk to the cell (a unit holding its post, +0x9f 1,
//     takes the cell as its post);
//   3 FUN_0048e6d0: attack the target (message 0x6a);
//   5 FUN_0048e990: retreat (state 0xe);
//   0xd FUN_0048eda0: the stance (`cx`, setStance).
// The others reach the units straight from their callers (none of them
// waits on a busy unit but the call): 4 FUN_0048e790 (placement),
//   0xb FUN_0048ea90: the call (callParty, the list in callOrder);
// 9 FUN_0048e9f0 (state 0xd), 10 FUN_0048ea40,
// 0xe FUN_0048ef60, 0x11 FUN_0048f0b0 (state 0x1b); 0xc (FUN_0048e1b0)
// only carries the rest of a call's list.
void TacticsUnits::runOrders(Unit& u) {
    if (u.orders.empty()) return;
    const Unit::Order o = u.orders.front();
    bool done = true;
    switch (o.type) {
        case 1:
            if (busyNow(u)) { done = false; break; }
            u.aiControlled = false;
            u.target = u.skill = -1;
            u.goal = {o.cx, o.cy};
            setState(u, kMove);                        // planned on its first step
            if (u.stance() == 1) u.post = {o.cx, o.cy};
            break;
        case 3: {
            if (busyNow(u)) { done = false; break; }
            if (o.target < 0 || o.target >= kSlots) break;
            const Unit& t = units_[std::size_t(o.target)];
            if (o.skill > 0) {                     // FUN_0048e6d0: a player's order, the skill as given
                if (!t.used || down(t)) break;
                u.target = static_cast<std::int16_t>(t.slot);
                u.skill = static_cast<std::int16_t>(o.skill);
                u.aim = {o.cx, o.cy};                  // +0x4f0 / +0x4f1
            } else if (!t.used || !attackable(u, t) || !aimAt(u, t)) {
                break;
            }
            u.aiControlled = false;
            u.path.clear();
            setState(u, kSkill);
            voice(u, 0x6a);
            break;
        }
        case 5:
            done = retreat(u.slot);
            break;
        case 0xd:
            setStance(u, o.cx);
            break;
        case 0xb:                                  // FUN_0048ea90: the call waits on a busy unit
            done = callParty(u.slot, u.callOrder);
            break;
        default: break;
    }
    if (done) u.orders.pop_front();
}

// ------------------------------------------------------------- combat ---

// FUN_00467e00: the battle's own generator, seeded 0x87654321. Its state
// and the count of rolls (+0x2e710) are the check words each network frame
// carries (FUN_0048ceb0), so every side can tell when it has drifted.
int TacticsUnits::rand() {
    ++rolls_;
    const std::uint32_t r = seed_;
    seed_ = seed_ * 0x48d159e1u + 1u;
    return int(r >> 16 & 0x7fff);
}

// FUN_0046af50: 0 on the same team, else the battle's relation table.
int TacticsUnits::relationOf(const Unit& a, const Unit& b) const { return relationCode(a.team(), b.team()); }

bool TacticsUnits::hostile(const Unit& a, const Unit& b) const {
    const int r = relationOf(a, b);
    return r > 0 && r <= 2;
}

// FUN_0046ae20: not while appearing or falling, not the bases.
bool TacticsUnits::attackable(const Unit& a, const Unit& b) const {
    switch (b.state) {
        case kAppear: case kCollapse: case kFall: case kAppear2: return false;
        default: return !base(b) && hostile(a, b);
    }
}

bool TacticsUnits::down(const Unit& u) {
    return u.state == kCollapse || u.state == kFall || !u.onField();
}

// FUN_004692e0 / FUN_004693b0: the flag, or a state no hit or order can
// break into (appearing 1 / 0x19, 0xc, falling 0xd).
static bool lockedState(int state) {
    return state == TacticsUnits::kAppear || state == TacticsUnits::kCollapse || state == TacticsUnits::kFall ||
           state == TacticsUnits::kAppear2;
}
bool TacticsUnits::steadyNow(const Unit& u) { return u.steady || lockedState(u.state); }
bool TacticsUnits::busyNow(const Unit& u) { return u.busy || lockedState(u.state); }

// FUN_00469ad0: the structures (kinds 0x183..0x194), left alone unless the
// AI record's +0x1c says otherwise.
bool TacticsUnits::structure(const Unit& u) {
    const int kind = u.kind();
    return kind >= 0x183 && kind <= 0x194;
}

// FUN_00469d30 (kind 0x195) / FUN_00469df0 (kind 0x196): the two bases.
bool TacticsUnits::base(const Unit& u) {
    const int kind = u.kind();
    return kind == 0x195 || kind == 0x196;
}

int TacticsUnits::jobSkill(const Unit& u) const {
    return logic::sdS16(kJobSkill + std::uint32_t(u.data.u16(0xc)) * 0x40, ctx_.version);
}

// FUN_004ddbb0: the sight range: the blessings 0x2e (x1.2) and 0x2f (all
// 32), blindness (6) x0.8; 0..32.
int TacticsUnits::sight(const Unit& u) {
    int r = u.data.s8(0x2c);
    if (u.status[2].type == 0x2e) r = static_cast<int>(double(r) * 1.2);
    else if (u.status[2].type == 0x2f) r = 0x20;
    if (u.status[0].type == 6) r = static_cast<int>(double(r) * 0.8);
    return r > 0x1f ? 0x20 : std::max(r, 0);
}

// FUN_004ddc60: the job skills (101 on) reach +0x37, or +0x44 for those
// aimed at kind 6; the learnt skills (1..100) by their own rules: the area
// radius FUN_004ddd50 for kind 6, the reach FUN_004ddf40 otherwise.
int TacticsUnits::skillRange(const Unit& u, int skill) const {
    const bool six = logic::sdU8(skillRow(skill) + kSkillAim, ctx_.version) == 6;
    if (skill > 0 && skill <= 100) return six ? skillArea(u, skill) : learntReach(u, skill);
    return std::clamp<int>(u.data.u8(six ? 0x44 : 0x37), 0, 0x20);
}

// FUN_004de8f0: the learnt skills' 7-byte formula row (0x6d4e58 + 7 id);
// the job skills share one row.
std::uint32_t TacticsUnits::formulas(int skill) {
    return skill > 0 && skill < 0x65 ? 0x6d4e58 + std::uint32_t(skill) * 7 : 0x738af0;
}

// FUN_004ddf40: the reach, from +0x37 and record +0x12 / +0x13 by stat.
int TacticsUnits::learntReach(const Unit& u, int skill) const {
    const GameVersion v = ctx_.version;
    const std::uint32_t rec = skillRow(skill);
    const int add = logic::sdU8(rec + 0x12, v), pct = logic::sdU8(rec + 0x13, v);
    int r = u.data.u8(0x37);
    switch (logic::sdU8(formulas(skill), v)) {
        case 2: r += add + u.data.u8(4) * pct / 100; break;
        case 3: r = add + u.data.u8(5) * pct / 100; break;
        case 4: r = add + u.data.u8(6) * pct / 100; break;
        case 5: r += add + u.data.u8(6) * pct / 100; break;
        default: break;
    }
    return std::clamp(r, 0, 0x20);
}

// FUN_004ddd50: the area's radius, from +0x44 and record +0x16 / +0x17.
int TacticsUnits::skillArea(const Unit& u, int skill) const {
    const GameVersion v = ctx_.version;
    const std::uint32_t rec = skillRow(skill);
    const int add = logic::sdU8(rec + 0x16, v), pct = logic::sdU8(rec + 0x17, v);
    int r = u.data.u8(0x44);
    switch (logic::sdU8(formulas(skill) + 2, v)) {
        case 2: r += add + u.data.u8(4) * pct / 100; break;
        case 3: r = add + u.data.u8(4) * pct / 100; break;
        case 4: r += add + u.data.u8(6) * pct / 100; break;
        case 5: r = add + u.data.u8(6) * pct / 100; break;
        case 6: r = add + u.data.u8(10) * pct / 100; break;
        default: break;
    }
    return std::clamp(r, 0, 0x20);
}

// FUN_0048a380 / FUN_0048a730's shared shapes: record +0x20/+0x22 (attack)
// or +0x24/+0x26 (spread) by the caster's stats (unit data +4..+9).
int TacticsUnits::skillValue(const Unit& u, int kind, int base, int add, int pct) {
    const auto d = [&](int o) { return int(u.data.u8(std::size_t(o))); };
    switch (kind) {
        case 2: return base + add + d(4) * pct / 100;
        case 3: return add + d(4) * pct / 100;
        case 4: return base + add + d(5) * pct / 100;
        case 5: return base + add + d(6) * pct / 100;
        case 6: return add + d(6) * pct / 100;
        case 7: return add + d(7) * pct / 100;
        case 8: return add + d(8) * pct / 100;
        case 9: return d(8) * (d(8) + d(9) + add) / 100 * pct / 100;
        case 10: return (d(8) + d(5) + add) * d(8) / 100 * pct / 100;
        case 11: return (d(8) + d(7) + add) * d(8) / 100 * pct / 100;
        case 12: return (d(8) + d(4) + add) * d(8) / 100 * pct / 100;
        default: return base;
    }
}

bool TacticsUnits::reaches(const Unit& u, const Unit& t, int range) const {
    return map_ && map_->inRange(u.cx(), u.cy(), t.cx(), t.cy(), range);
}

bool TacticsUnits::friendSees(const Unit& u, int cx, int cy) const {
    if (!map_) return false;
    for (const Unit& o : units_)
        if (o.used && o.onField() && friendly(u, o) && map_->inRange(o.cx(), o.cy(), cx, cy, sight(o))) return true;
    return false;
}

// FUN_0047c210 by kind: 1..3 (a unit) in the unit's sight, or for a
// player's unit in a friend's (FUN_0047e460); 4 (a cell) for a computer's
// unit holding its ground (+0x9f 2) within the skill's reach; 6 always for
// a player's unit, else in sight; 0 and 5 assert and count as lost.
bool TacticsUnits::inSight(const Unit& u, int kind) const {
    const bool haveTarget = u.target >= 0 && u.target < kSlots && units_[std::size_t(u.target)].used;
    const Unit* t = haveTarget ? &units_[std::size_t(u.target)] : nullptr;
    switch (kind) {
        case 1: case 2: case 3:
            if (!t) return false;
            if (reaches(u, *t, sight(u))) return true;
            return u.human() && friendSees(u, t->cx(), t->cy());
        case 4:                                    // to the aimed cell, +0x4f0 / +0x4f1
            if (!u.human() && u.stance() == 2)
                return map_ && map_->inRange(u.cx(), u.cy(), u.aim.first, u.aim.second, skillRange(u, u.skill));
            return true;
        case 6:
            if (u.human()) return true;
            return t && reaches(u, *t, sight(u));
        default:
            return false;
    }
}

bool TacticsUnits::enemiesGone() const {
    for (const Unit& o : units_) {
        if (!o.used || structure(o) || base(o)) continue;
        const int kind = o.kind();
        if (kind >= 0x19b && kind <= 0x19e) continue;
        const int rel = relation(o.team());
        if (rel < 1 || rel > 2) continue;
        if (o.onField() && !down(o)) return false;
        if (!o.onField() && o.data.u8(0x96) != 3 && o.data.u8(0xf) == 0) return false;
    }
    return true;
}

bool TacticsUnits::baseOurs(int n) const {
    const int slot = 0xf9 - (n - 1);
    if (n <= 0 || slot < 0 || slot >= kSlots || !units_[std::size_t(slot)].used) return false;
    return units_[std::size_t(slot)].team() == viewer_;
}

// FUN_0046d6a0: over a building or base, how far its capture has gone
// (+0x4fe of 600, or 900 for ours) as a green bar 60 px wide in a dark frame,
// 8 px higher for the buildings (kinds 0x183..0x188, 0x18f..0x194).
void TacticsUnits::drawHold(const Unit& u, int x, int y) const {
    if (!structure(u) && !base(u)) return;
    const int kind = u.kind();
    const bool tall = (kind >= 0x183 && kind <= 0x188) || (kind >= 0x18f && kind <= 0x194);
    const int off = tall ? -8 : 0x1e;
    const int w = u.holdOut * 100 / (u.human() ? 900 : 600) * 0x3c / 100;
    ysub::fill(ctx_, x - 0x1e, y - 0x50 + 1 + off, w, 7, 0x37a, 0xff80ff00u);
    ysub::fill(ctx_, x - 0x20, y - 0x50 + off, 0x40, 9, 0x37a, 0xff202020u);
}

// FUN_0047c690: an enemy AI unit (not a player's, not ours) whose AI record
// allows it (+0x19) rolls against its pattern's chances (+0x1a,
// DAT_00738808, 8 bytes each) for one of its learnt skills (+0x84, 8 words,
// 0x8000 off); FUN_0047c8a0 takes it unless it is no heal and the target
// is a hurt friend. 0: none, the job skill then.
int TacticsUnits::learntChoice(Unit& u, const Unit& t) {
    const GameVersion v = ctx_.version;
    if (u.human() || u.team() == viewer_) return 0;
    const int pattern = static_cast<std::int8_t>(u.ai.u8(0x1a));
    if (u.ai.u8(0x19) == 0 || pattern < 1 || pattern > 0x31 || u.data.u16(0x84) == 0) return 0;
    const std::uint32_t chances = 0x738808 + std::uint32_t(pattern) * 8;
    const int roll = rand() % 100;
    int sum = sdS8(chances, v);
    for (int i = 0; i < 8; ++i) {
        const int w = u.data.u16(0x84 + std::size_t(i) * 2);
        if (w & 0x8000) continue;
        const int skill = w & 0x7fff;
        if (skill == 0) return 0;
        const std::uint32_t rec = skillRow(skill);
        if (roll < sum && u.mp() >= logic::sdS16(rec + kSkillMpCost, v)) {
            const bool heal = logic::sdS16(rec + kSkillEffect, v) == 5;
            const int pct = t.hp() * 100 / std::max<int>(t.maxHp(), 1);
            if (!friendly(u, t) || pct > 0x31 || heal) return skill;
            return 0;
        }
        if (i != 7) sum += sdS8(chances + std::uint32_t(i) + 1, v);
    }
    return 0;
}

// FUN_0047c580: a learnt skill (FUN_0047c690), else the job's own skill,
// at the unit.
bool TacticsUnits::aimAt(Unit& u, const Unit& t) {
    const int learnt = learntChoice(u, t);
    const int skill = learnt != 0 ? learnt : jobSkill(u);
    if (skill <= 0) return false;
    if (tactics::traceUnits() && u.target != t.slot)
        SDL_Log("[tactics] unit %d aims skill %d at unit %d (kind %d team %d) at %d,%d", u.slot, skill, t.slot,
                t.kind(), t.team(), t.cx(), t.cy());
    // FUN_0047c8a0: skills for one's own side or the caster (+0x19 3..4)
    // and those aimed at kind 6 start at the caster.
    const std::uint32_t rec = skillRow(skill);
    const int rule = logic::sdU8(rec + kSkillAimRule, ctx_.version), kind = logic::sdU8(rec + kSkillAim, ctx_.version);
    const bool self = learnt != 0 && ((rule > 2 && rule < 5) || kind == 6);
    u.target = static_cast<std::int16_t>(self ? u.slot : t.slot);
    u.skill = static_cast<std::int16_t>(skill);
    // +0x4f0 / +0x4f1, the cell a ground skill (kind 4) goes to: where the
    // target stands now. For the caster's own cell FUN_0047c8a0 divides
    // its y by 32 rather than 16, as here.
    u.aim = self ? tactics::Cell{u.cx(), u.py() / 32} : tactics::Cell{t.cx(), t.cy()};
    return true;
}

// FUN_0047cc90: the unit that last hit this one, while it can still be
// fought and is in sight.
const TacticsUnits::Unit* TacticsUnits::counterTarget(Unit& u) {
    if (u.counter < 0 || u.counter >= kSlots) return nullptr;
    const Unit& t = units_[std::size_t(u.counter)];
    if (u.ai.u8(0x1e) == 0 && t.used && (!structure(t) || u.ai.u8(0x1c) != 0) && attackable(u, t) && t.onField() &&
        reaches(u, t, sight(u)))
        return &t;
    u.counter = -1;
    return nullptr;
}

// FUN_0047a920: every +0xc frames FUN_0047a9d0 picks the best enemy by
// +0x11, comparing each with the best so far through FUN_0047aed0 (1 / 2
// most / least attack, 3 / 5 most HP, 4 / 6 least, 7 / 8 by +0x24, 9
// nearest, 10 farthest, else the first), within sight when +0x10 is 1;
// +0x10 above 1 never looks. The healers look first (FUN_0047ac80); a
// found enemy is called out to the unit's teammates (FUN_0047b2c0).
const TacticsUnits::Unit* TacticsUnits::scan(Unit& u) {
    if (++u.aiScan < u.ai.s32(0xc)) return nullptr;
    u.aiScan = 0;
    if (const Unit* h = healTarget(u)) return h;
    const int mode = u.ai.u8(0x10), rule = u.ai.u8(0x11);
    if (mode > 1) return nullptr;
    const auto better = [&](const Unit* best, const Unit& c) {
        if (!best) return true;
        const auto dist = [&](const Unit& o) {
            return std::abs(std::int64_t(o.x) - u.x) + std::abs(std::int64_t(o.y) - u.y);
        };
        switch (rule) {
            case 1:  return best->data.u16(0x3a) < c.data.u16(0x3a);
            case 2:  return best->data.u16(0x3a) > c.data.u16(0x3a);
            case 3: case 5: return best->hp() < c.hp();
            case 4: case 6: return best->hp() > c.hp();
            case 7:  return best->data.s32(0x24) < c.data.s32(0x24);
            case 8:  return best->data.s32(0x24) > c.data.s32(0x24);
            case 9:  return dist(c) < dist(*best);
            case 10: return dist(c) > dist(*best);
            default: return false;
        }
    };
    const Unit* best = nullptr;
    for (const Unit& o : units_) {
        if (!o.used || !o.onField() || &o == &u || !attackable(u, o) || down(o)) continue;
        if (structure(o) && u.ai.u8(0x1c) == 0) continue;
        if (mode == 1 && !reaches(u, o, sight(u))) continue;
        if (better(best, o)) best = &o;
    }
    if (best) callHelp(u, *best);
    return best;
}

// FUN_0047ac80: a unit whose job skill heals (record +2 5, a job skill,
// MP enough) picks the most hurt of its side and allies, below 91% of its
// HP and within the skill's reach.
const TacticsUnits::Unit* TacticsUnits::healTarget(const Unit& u) const {
    const GameVersion v = ctx_.version;
    const int skill = jobSkill(u);
    if (skill <= 100) return nullptr;
    const std::uint32_t rec = skillRow(skill);
    if (logic::sdS16(rec + kSkillEffect, v) != 5 || u.mp() < logic::sdS16(rec + kSkillMpCost, v)) return nullptr;
    const int range = skillRange(u, skill);
    const Unit* best = nullptr;
    int lowest = 100;
    for (const Unit& o : units_) {
        if (!o.used || !o.onField() || !friendly(u, o)) continue;
        const int pct = o.hp() * 100 / std::max<int>(o.maxHp(), 1);
        if (pct < 0x5b && reaches(u, o, range) && pct < lowest) {
            lowest = pct;
            best = &o;
        }
    }
    return best;
}

// FUN_0046abc0 / FUN_0046ad60: the same side, or an ally.
bool TacticsUnits::friendly(const Unit& a, const Unit& b) const {
    return a.team() == b.team() || relationOf(a, b) == 4;
}

// FUN_0048aae0's first test: a heal (record +2 5) at a friendly unit.
bool TacticsUnits::healing(const Unit& u, const Unit& t) const {
    return u.skill > 0 && friendly(u, t) &&
           logic::sdS16(skillRow(u.skill) + kSkillEffect, ctx_.version) == 5;
}

// FUN_00476470: the AI picks the unit's next state from the player's
// setting (+0x9f), the stage's order (+0xa0) and the AI record's mode
// (+0: 0 guard, 1 follow the route, 2 wander). Flyers (move type 4) guard.
void TacticsUnits::chooseState(Unit& u) {
    u.aiControlled = true;
    const int moveType = u.moveType(), stance = u.stance(), order = u.order(), mode = u.ai.u8(0);
    if (!u.human() && order == 0 && mode == 1) {
        u.next = moveType != 4 ? 0x10 : 0x13;
        return;
    }
    switch (stance) {
        case 0: case 1:
            if (moveType == 4) { u.next = kGuard; return; }
            break;
        case 2: u.next = kGuard; return;
        case 3: u.next = kStand; return;
        default: break;
    }
    switch (order) {
        case 0:
            if (mode == 0) u.next = kGuard;
            else if (mode == 1) u.next = moveType != 4 ? 0x10 : 0x13;
            else if (mode == 2) u.next = moveType != 4 ? 0x17 : 0x13;
            break;
        case 1: case 2: case 3: u.next = kGuard; break;
        case 4: u.next = kAmbush; break;
        default: break;
    }
}

// FUN_004776a0: stand on guard and react (aiReact) to what comes, healers
// included: a hurt friend the scan finds goes through the same +0x12 switch.
void TacticsUnits::guardState(Unit& u) {
    if (u.sub == 0) {
        playAnim(u, 1);
        ++u.sub;
    } else if (u.sub == 1 && u.timer > 0xb3) {
        playAnim(u, 0x18);
        ++u.sub;
    }
    if (u.sub == 0) return;
    if (u.human() && u.stance() == 3) {
        u.next = kChoose;
        return;
    }
    aiReact(u, true);
}

// FUN_00477980 for a skill aimed at a unit: walk until the target is in
// reach (sub 0xa..0xc), the cast (0x14, 0x15), the strike (0x16), the
// recovery (0x17), then again while the target stands (0x18).
// The hit lands when the strike clip ends; in the original the skill's
// effect carries it (FUN_0046beb0 -> FUN_004919f0).
void TacticsUnits::skillState(Unit& u) {
    const GameVersion v = ctx_.version;
    const auto giveUp = [&] {
        if (tactics::traceUnits()) SDL_Log("[tactics] unit %d gives up skill %d at step %d", u.slot, u.skill, u.sub);
        u.target = u.skill = -1;
        u.aim = {-1, -1};
        u.next = kChoose;
    };
    if (u.skill < 0 || u.target < 0 || u.target >= kSlots) return giveUp();
    if (u.kind() > 0xa9 && u.kind() < 200) return giveUp();   // these kinds never use skills
    Unit& t = units_[std::size_t(u.target)];
    const std::uint32_t anims = kSkillAnims + std::uint32_t(u.skill) * 0x10;
    const bool jobs = u.skill == jobSkill(u);
    const int kind = logic::sdU8(skillRow(u.skill) + kSkillAim, v);
    // FUN_0047ba20: whether the target still serves: by the skill's kind
    // (+0x18), a heal (+2 5) at a friend always; else kind 2 wants one it
    // may strike, and kinds 1..3 one on the field and up; 4 and 6 always.
    const auto stillValid = [&] {
        if (!t.used) return false;
        const bool heal = logic::sdS16(skillRow(u.skill) + kSkillEffect, v) == 5 && friendly(u, t);
        switch (kind) {
            case 2: if (heal) return true; if (!attackable(u, t)) return false; [[fallthrough]];
            case 1: case 3: return heal || !down(t);
            case 4: case 6: return true;
            default: return false;          // 0 and 5 assert
        }
    };
    // FUN_0047bc90 / FUN_0047bed0 case 4: a ground skill aims at its cell
    // (+0x4f0 / +0x4f1), the rest at the target's.
    const bool atCell = kind == 4 && u.aim.first >= 0;
    const tactics::Cell aimCell = atCell ? u.aim : tactics::Cell{t.cx(), t.cy()};
    const auto inReach = [&](int range) { return map_ && map_->inRange(u.cx(), u.cy(), aimCell.first, aimCell.second, range); };
    if (u.sub > 0 && u.sub < 0x14 && !stillValid()) {
        u.path.clear();
        u.next = kStand;
        return;
    }
    if (u.sub > 9 && u.sub < 0x14) {
        if (inReach(skillRange(u, u.skill))) {     // FUN_0047bed0: 1
            u.path.clear();
            // FUN_00477980: a unit acting on its own does not strike a
            // structure of another side unless its AI record's +0x1c says
            // so; it stands guard (0x13) instead. Else the cast starts on
            // the next frame.
            if (structure(t) && !friendly(u, t) && u.aiControlled && u.ai.u8(0x1c) == 0) u.next = kGuard;
            else u.sub = 0x14;
            return;
        }
        if (u.aiControlled) {
            // FUN_0047bed0's 2: a unit acting on its own (+0x2a7, FUN_004698a0)
            // that may not chase (FUN_00469950: its order +0xa0 is 2 or 3, or
            // it never moves, move type 4) chooses again (0xf). The original
            // goes on with this frame (a counter below may still win).
            const int order = u.order();
            if ((order >= 2 && order <= 3) || u.moveType() == 4) {
                u.path.clear();
                u.next = kChoose;
            }
        }
    }
    if (u.sub > 0 && u.sub < 0x14) {
        // FUN_0047cc90 -> FUN_0047b210: the unit that hit this one takes
        // over only on a roll under the AI record's +0x13 (always when the
        // unit has no target, never when it is the target already; a
        // failed roll forgets it); then FUN_0047ce70.
        if (const Unit* c = counterTarget(u)) {
            if (switchTo(u, *c)) {
                if (react(u, *c)) {
                    u.path.clear();
                    return;
                }
            } else {
                u.counter = -1;
            }
        }
        // A player's order (FUN_004697f0), or an AI whose record's +0x14 is
        // 1, gives up a target lost from sight (FUN_0047c210) for what its
        // record's +0x15 says: 0 stand, 1 the AI's choice, 2 lie in wait.
        // (+0x16 makes the cell the unit's post, +0x2e8, which nothing in
        // the port reads.)
        if ((!u.aiControlled || u.ai.u8(0x14) == 1) && !inSight(u, kind)) {
            u.path.clear();
            switch (u.ai.u8(0x15)) {
                case 0: u.next = kStand; break;
                case 1: u.next = kChoose; break;
                case 2: u.next = kAmbush; break;
                default: break;
            }
            return;
        }
    }
    // FUN_00477980 (LAB_00477dbd): a unit acting on its own whose AI record
    // has +0x1b 1 keeps scanning (FUN_0047a920, from step 0xa on, its count
    // running even through the cast) and, before the cast, may take what
    // the scan finds instead (FUN_0047b210's roll), by the record's +0x12.
    if (u.sub > 9 && u.aiControlled && u.ai.u8(0x1b) == 1) {
        if (const Unit* f = scan(u); f && u.sub < 0x14 && f->slot != t.slot && switchTo(u, *f)) {
            switch (u.ai.u8(0x12)) {
                case 0:
                    if ((u.ai.u8(0x1c) != 0 || !structure(*f)) && aimAt(u, *f)) {
                        u.path.clear();
                        u.next = kSkill;
                        return;
                    }
                    break;
                case 2:
                    u.target = static_cast<std::int16_t>(f->slot);
                    u.path.clear();
                    u.next = kFlee;
                    break;
                case 4:
                    u.path.clear();
                    u.next = kSkill;
                    return;
                default: break;
            }
        }
    }
    switch (u.sub) {
        case 0: {                                  // FUN_0047bc90: head for the target
            if (!atCell && down(t)) return giveUp();
            // FUN_0046c930: the MP for the skill (record +8) is there; else
            // the job's own skill takes over when that one is paid for (the
            // target's cell taken again, +0x4f0, step 0 next frame), else
            // the unit chooses again.
            const auto paid = [&](int s) {
                return s >= 0 && u.mp() >= logic::sdS16(skillRow(s) + kSkillMpCost, v);
            };
            if (!paid(u.skill)) {
                if (!paid(jobSkill(u))) return giveUp();
                u.skill = static_cast<std::int16_t>(jobSkill(u));
                u.aim = {t.cx(), t.cy()};
                break;
            }
            u.goal = aimCell;
            // A unit acting on its own on "fixed point" (+0x9f 2) does not
            // go after it: it waits (0x12) until the target comes in reach
            // (the test above runs in 0xa..0x13).
            u.sub = u.aiControlled && u.stance() == 2 ? 0x12 : 0xa;
            break;
        }
        case 0xa:                                  // FUN_00477980: the walk starts over
            u.chased = {-1, -1};
            ++u.sub;
            break;
        case 0xb:
            ++u.sub;
            break;
        case 0x12:                                 // the wait: the setting changed
            if (u.stance() < 2) u.sub = 0;
            else if (u.stance() == 3) return giveUp();
            break;
        case 0xc: {                                // FUN_00476680(.., 1): the chase
            // The original searches once per chase and looks again only when
            // the walk ends; here the route follows the target cell by cell
            // (not while a failed search waits), which reads more smoothly.
            if (u.chased != aimCell && u.routeWait == 0) {
                u.chased = aimCell;
                u.goal = aimCell;
                plan(u);
                // Port: already as near as it gets and still out of reach;
                // the original would plan again every few frames for ever.
                if (u.path.empty() && !u.routeCut && !inReach(skillRange(u, u.skill))) {
                    u.chased = {-1, -1};
                    return giveUp();
                }
            }
            switch (walk(u)) {
                case Walk::On: break;
                case Walk::Arrived:                // 1: step 0 again (in reach, or chase on)
                    u.chased = {-1, -1};
                    u.sub = 0;
                    break;
                default:                           // 2..4: give up
                    u.chased = {-1, -1};
                    return giveUp();
            }
            break;
        }
        case 0x14: {
            u.busy = true;                         // FUN_0046b0f0(.., 1): no orders until the recovery ends
            if (aimCell != tactics::Cell{u.cx(), u.cy()}) face(u, facing(aimCell.first - u.cx(), aimCell.second - u.cy(), v));
            const std::uint32_t rec = skillRow(u.skill);
            u.castFull = jobs ? u.data.s16(0x32) : logic::sdS16(rec + 0xa, v);
            u.recoverFull = jobs ? u.data.s16(0x34) : logic::sdS16(rec + 0xe, v);
            // FUN_0048cae0 -> FUN_0048cb30: skills 10 and 0x10 take the job
            // skill's times, the recovery doubled (trebled from +5 75 on),
            // plus their own (+0xa / +0xe).
            if (u.skill == 10 || u.skill == 0x10) {
                const std::uint32_t job = skillRow(jobSkill(u));
                u.castFull = static_cast<std::int16_t>(logic::sdS16(job + 0xa, v) + logic::sdS16(rec + 0xa, v));
                u.recoverFull = static_cast<std::int16_t>(logic::sdS16(job + 0xe, v) * (u.data.u8(5) < 0x4b ? 2 : 3) +
                                                          logic::sdS16(rec + 0xe, v));
            }
            castScale(u, u.skill);
            ++u.sub;
            if (u.castFull > 0) {
                playAnim(u, sdS8(anims, v));
                u.cast = u.castFull;
                playEffect(u, 1, logic::sdS16(anims + 4, v));   // the cast effect
                break;
            }
            [[fallthrough]];
        }
        case 0x15:
            if (u.sub == 0x15 && --u.cast >= 1) break;
            // LAB_00478339: the strike (FUN_0048ccb0 swaps the clip for skills 10 and 0x10);
            // FUN_0046c990 pays the skill's MP (record +8).
            u.cast = 0;
            u.steady = true;                       // FUN_0046b0b0(.., 1): the strike is not interrupted
            u.data.set16(0x1c, std::clamp<int>(u.mp() - logic::sdS16(skillRow(u.skill) + kSkillMpCost, v), 0,
                                               u.data.s16(0x1a)));
            // FUN_00477980: a learnt skill (FUN_0046c9f0 false) is no longer
            // held once used; the unit goes back to its job's skill.
            if (u.skill <= 100) u.data.set8(0x9d, 0);
            u.sub = 0x16;
            playAnim(u, sdS8(anims + 1, v));
            playEffect(u, 1, -1);
            playEffect(u, 0, logic::sdS16(anims + 6, v));     // the strike effect
            u.recover = u.recoverFull;
            u.struck = u.hitSignal = false;
            break;
        case 0x16:
            // The hit goes out on the strike's flag-4 frame (of the clip or
            // its effect, FUN_00482a30), else when the clip ends.
            // (FUN_00482a30 -> FUN_00482ac0 walks the node's queued packets,
            // +0x34, and frees each one FUN_00482b50 took.)
            if (!u.struck && (u.hitSignal || u.animEnd)) {
                u.struck = true;
                hit(u, t);
            }
            if (!u.animEnd) break;
            ++u.sub;
            u.steady = false;                      // FUN_0046b0b0(.., 0)
            u.recover = u.recoverFull;
            playAnim(u, sdS8(anims + 2, v));
            voice(u, 0x6a);
            break;
        case 0x17:
            if (--u.recover < 1) {
                playAnim(u, 2);
                u.recover = 0;
                u.busy = false;                    // FUN_0046b0f0(.., 0)
                ++u.sub;
            }
            break;
        case 0x18: {
            // The AI record's +0x1d 1 runs from the target after each use.
            if (u.ai.u8(0x1d) == 1 && u.target >= 0) {
                u.skill = -1;
                u.next = kFlee;
                break;
            }
            // A player's unit (not on "stand", +0x9f 3) goes again while the
            // target is in sight: always on order +0xa0 2, else with its job's
            // skill (FUN_0046c9f0); not at a friend while the AI drives it.
            // Everything else goes back to the AI's choice.
            const bool again = (!friendly(u, t) || !u.aiControlled) && u.human() && u.stance() != 3 &&
                               (u.order() == 2 || u.skill > 100) && inSight(u, kind);
            if (again) u.sub = 0;
            else giveUp();
            break;
        }
        default: break;
    }
}

// FUN_0048ac90 for the job skill: both sides' fighting values
// (FUN_004deaa0: attack +0x3a, its spread +0x3c, accuracy +0x38, evasion
// +0x4c, guard +0x4d, toughness +0x52; FUN_0046b3e0 the terrain's
// percentages), then FUN_004824d0:
//   0 miss, 1..3 hit (3 past three times the toughness, or critical),
//   4 no damage.
// FUN_004deaa0 + FUN_0046b3e0: a unit's fighting values (attack +0x3a, its
// spread +0x3c, accuracy +0x38) and its defences with its terrain's rates.
TacticsUnits::Fighter TacticsUnits::fighter(const Unit& u) const {
    Fighter f{u.data.u16(0x3a), u.data.u16(0x3c), u.data.u16(0x38), u.data.u8(0x4c), u.data.u8(0x4d),
              u.data.u8(0x50), u.data.u8(0x4f), u.data.u8(0x4e), u.data.u16(0x52)};
    statusStats(u, f);
    return f;
}

int TacticsUnits::rollAmount(const Fighter& a) {
    return (a.spread & 0xffff) != 0 ? a.atk + rand() % (a.spread & 0xffff) : a.atk;
}

// FUN_004824d0: 0 miss, 1..3 hit (3 past three times the toughness, or a
// critical), 4 no damage.
std::pair<int, int> TacticsUnits::weaponRoll(const Fighter& a, const Unit& to) {
    int evadeRate = 0, guardRate = 0;
    if (map_) map_->combatRates(to.cx(), to.cy(), to.moveType(), evadeRate, guardRate);
    const Fighter d = fighter(to);
    const int evade = d.evade, guard = d.guard, tough = d.tough;
    int chance = a.acc - evade * evadeRate / 100;
    const int roll = rand() % 100;
    chance -= roll;
    bool miss = false, crit = false;
    if (evade == 0) crit = chance >= 0x96;
    else if (roll < 5) crit = true;
    else if (roll >= 0x5f || chance < 0) miss = true;
    else crit = chance >= 0x96;
    if (miss) return {0, 0};
    const int scale = crit ? std::max(chance, 0x96) : 100;
    const int rolled = rollAmount(a);
    int damage = rolled * scale / 100 - guard * guardRate / 100;
    if (tactics::traceUnits())
        SDL_Log("[tactics]   weapon on %d: roll %d chance %d (evade %d x %d%%) scale %d, amount %d - guard %d x %d%%, tough %d",
                to.slot, roll, chance, evade, evadeRate, scale, rolled, guard, guardRate, tough);
    if (scale != 100) return {3, damage < 0 ? 1 : damage};   // a critical of 0 stays 0
    if (damage < 1) return {4, 0};
    return {damage > tough * 3 ? 3 : damage > tough ? 2 : 1, damage};
}

// FUN_004826f0: the spell's roll less the magic resistance (+0x50) and the
// magic guard (+0x4f) by the terrain; at least 1.
std::pair<int, int> TacticsUnits::spellRoll(const Fighter& a, const Unit& to) {
    const Fighter d = fighter(to);
    const int resist = d.resist, mguard = d.mguard, tough = d.tough;
    const int rate = map_ ? map_->magicGuardRate(to.cx(), to.cy(), to.moveType()) : 0;
    const int rolled = rollAmount(a);
    const int damage = std::max(rolled * (100 - resist) / 100 - mguard * rate / 100, 1);
    if (tactics::traceUnits())
        SDL_Log("[tactics]   spell on %d: amount %d x (100 - resist %d) - magic guard %d x %d%%, tough %d", to.slot, rolled,
                resist, mguard, rate, tough);
    return {damage > tough * 3 ? 3 : damage > tough ? 2 : 1, damage};
}

// FUN_004919f0 / FUN_00493090 (HP) / FUN_00493180 (MP): the result lands;
// `amount` < 0 hurts, > 0 heals. A miss shows MISS.
void TacticsUnits::land(Unit& from, Unit& to, int result, int amount, int skill, tactics::Cell at, int pools,
                        bool tail) {
    if (result == 0) {                             // FUN_004950c0
        popup(to, 0x13);
        // FUN_004919f0 case 0 runs on into case 1: a miss still marks the
        // attacker as the one to strike back at.
        if (!down(from) && !down(to)) {
            to.data.set16(0xa6, from.slot);
            to.counter = static_cast<std::int16_t>(from.slot);
            voice(to, 0x94);                       // FUN_0049ad90: dodged, then case 1's
            voice(to, 0x95);
        }
        return;
    }
    // FUN_004919f0: nothing lands once either side is down or off the
    // field (FUN_004690a0), else the attacker is the one credited (+0xa6).
    if (down(from) || down(to)) return;
    to.data.set16(0xa6, from.slot);
    // Job 0x1c6 takes nothing from the learnt skills (below 100), job 0x1c7
    // nothing from the job skills (from 100): a hurt 0 (FUN_00493090(.., 0,
    // 0)) unless the hit did nothing, and no reaction.
    const int job = to.data.u16(0xc);
    if ((job == 0x1c6 && skill < 100) || (job == 0x1c7 && skill > 99)) {
        if (result != 4) {
            queueChange(to, 0, 0);
            showHp(to, 0, 0);
        }
        return;
    }
    const auto change = [&](int style) {           // FUN_00491ef0: HP first, then MP
        if (pools & kPoolHp) {
            queueChange(to, 0, amount);
            showHp(to, amount, style);
        }
        if (pools & kPoolMp) {
            queueChange(to, 1, amount);
            showMp(to, amount);
        }
    };
    // The target strikes back at the attacker (FUN_004919f0 case 1, and
    // FUN_00492de0 for 2..4: the attacker's id to +0x4fa); a heal (always
    // 1) marks the healer too, whom FUN_0047cc90 then passes over.
    to.counter = static_cast<std::int16_t>(from.slot);
    if (amount > 0) {
        change(0);
        if (tail) learntTail(from, to, skill, result);
        return;
    }
    if (tail) learntTail(from, to, skill, result);
    react(from, to, result, skill, at);
    // FUN_00491ef0 changes and shows the number unless the hit did nothing
    // (result 4), when FUN_004919f0 case 4 shows "no damage" (FUN_00495050)
    // instead.
    if (result == 4) popup(to, 0x12);
    else change(damageStyle(from, skill));
    // FUN_004919f0's lines: 1 hurt 0x95, 2 staggered 0x6c, 3 knocked 0x6d,
    // 4 unharmed 0x6e.
    if (amount < 1 && result >= 1 && result <= 3) voice(to, result == 1 ? 0x95 : result == 2 ? 0x6c : 0x6d);
    if (result == 4) voice(to, 0x6e);
}

void TacticsUnits::hit(Unit& from, Unit& to) {
    if (from.skill > 0 && from.skill <= 100) return areaHit(from, to);
    const GameVersion v = ctx_.version;
    // FUN_0048aae0 -> FUN_0048ac90 / FUN_0048b080 / FUN_0048b1b0: the
    // unit's own fighting values (FUN_004deaa0 + FUN_0046b3e0), no bonus
    // from the skill's record. (Only FUN_00483cf0 adds record +0x20..+0x26
    // by level, for skill 11, and nothing calls it.)
    const Fighter a = fighter(from);
    const std::uint32_t rec = skillRow(from.skill);
    // (FUN_0048ac90, FUN_0048b080 and FUN_0048b1b0 each call FUN_00482d30,
    // which is empty.)
    if (healing(from, to)) {                       // FUN_0048b1b0 -> FUN_00482810
        // FUN_0048b1b0 raises only a negative amount to 1: a roll of 0 heals 0.
        const int heal = rollAmount(a);
        if (tactics::traceUnits()) SDL_Log("[tactics] unit %d heals %d by %d", from.slot, to.slot, heal);
        // FUN_0048b1b0 -> FUN_0046beb0(.., 0x7f2): the heal's own effect,
        // not the skill's hit.
        strike(from, to, 1, heal, from.skill, {to.cx(), to.cy()}, 0x7f2);
        return;
    }
    if (friendly(from, to)) return;                // FUN_0048aae0: nothing else for one's own side
    switch (logic::sdU8(rec + kSkillAim, v)) {          // FUN_0048aae0 by the aim
        case 1: case 2: case 3: break;
        case 6: splash(from, a); return;
        default: return;                           // 0, 4, 5: the original asserts and does nothing
    }
    const bool spell = logic::sdU8(rec + kSkillType, v) == 2;   // FUN_0048b080, else FUN_0048ac90
    const auto [result, damage] = spell ? spellRoll(a, to) : weaponRoll(a, to);
    if (tactics::traceUnits())
        SDL_Log("[tactics] unit %d %s %d with skill %d: result %d, damage %d (HP %d; atk %d+%d acc %d)", from.slot,
                spell ? "casts at" : "hits", to.slot, from.skill, result, damage, to.hp(), a.atk, a.spread, a.acc);
    const int amount = result == 4 ? 0 : -damage;
    // FUN_00483cf0 ff.: a steady target (FUN_004692e0) takes any hit as a plain one.
    strike(from, to, result != 0 && steadyNow(to) ? 1 : result, amount, from.skill, {to.cx(), to.cy()});
    inflict(from.data.u8(0x45), from.skill, from, to);   // FUN_0048ac90 / FUN_0048b080's tail
}

// FUN_0048adc0, a job skill aimed by +0x18 6: every unit the attacker may
// strike (FUN_0046ae20) within its reach (+0x37, at most 0x20, FUN_0043c1d0)
// takes its own weapon roll (FUN_004824d0; a steady one a plain hit) and
// the added effect (FUN_0048b2e0) at once; each hit's effect waits
// 10..59 frames (rand() % 50 + 10) before it starts.
void TacticsUnits::splash(Unit& from, const Fighter& a) {
    if (!map_) return;
    const int reach = std::min<int>(from.data.u8(0x37), 0x20);
    for (Unit& t : units_) {
        if (!t.used || !t.onField() || !attackable(from, t)) continue;
        if (!map_->inRange(from.cx(), from.cy(), t.cx(), t.cy(), reach)) continue;
        auto [result, damage] = weaponRoll(a, t);
        if (result != 0 && steadyNow(t)) result = 1;
        const int delay = rand() % 0x32 + 10;
        strike(from, t, result, result == 4 ? 0 : -damage, from.skill, {t.cx(), t.cy()}, -1, kPoolHp, delay);
        inflict(from.data.u8(0x45), from.skill, from, t);
    }
}

// FUN_00488af0 (FUN_0048a260 / FUN_0048a380 / FUN_0048a730): a learnt
// skill's accuracy, attack and spread by its formula row and record.
TacticsUnits::Fighter TacticsUnits::learntFighter(const Unit& u, int skill) const {
    const GameVersion v = ctx_.version;
    const std::uint32_t rec = skillRow(skill), row = formulas(skill);
    Fighter f = fighter(u);
    switch (logic::sdU8(row + 1, v)) {
        case 2: f.acc += logic::sdU8(rec + 0x14, v) + u.data.u8(4) * logic::sdU8(rec + 0x15, v) / 100; break;
        case 3: f.acc += logic::sdU8(rec + 0x14, v) + u.data.u8(6) * logic::sdU8(rec + 0x15, v) / 100; break;
        case 0xff: f.acc = 0xff; break;
        default: break;
    }
    f.atk = skillValue(u, logic::sdU8(row + 4, v), f.atk, logic::sdS16(rec + 0x20, v), logic::sdS16(rec + 0x22, v)) & 0xffff;
    const int spreadKind = logic::sdU8(row + 5, v);
    // Kinds 0 and 1 copy the attack (FUN_0048a730 reads param_4[0] there).
    f.spread = (spreadKind <= 1 ? f.atk : skillValue(u, spreadKind, f.spread, logic::sdS16(rec + 0x24, v),
                                                     logic::sdS16(rec + 0x26, v))) & 0xffff;
    return f;
}

// FUN_00482b50 -> FUN_00488bc0 for the learnt skills (1..100): every unit
// the skill may touch (record +0x19, FUN_0048c950) within its area around
// the target (or, mode 5, a chain from one to the next nearest) takes the
// formula row's effect: none, the weapon or spell roll, or a heal, and the
// added effects (FUN_0048b2e0). Each hit's effect waits by the pattern
// (anim +0xe, steps from DAT_00618ee8), the target itself (aim 1..3, 6)
// none; its added effects come when it lands (FUN_00491ef0).
void TacticsUnits::areaHit(Unit& from, Unit& primary) {
    const GameVersion v = ctx_.version;
    const int skill = from.skill;
    const std::uint32_t rec = skillRow(skill), row = formulas(skill);
    const std::uint32_t anim = kSkillAnims + std::uint32_t(skill) * 0x10;
    const int radius = skillArea(from, skill), mode = sdS8(anim + 0xe, v);
    // A ground skill (+0x18 4) at its cell, else around the target.
    const bool atCell = logic::sdU8(rec + kSkillAim, v) == 4 && from.aim.first >= 0;
    const int cx = atCell ? from.aim.first : primary.cx(), cy = atCell ? from.aim.second : primary.cy();
    const auto touches = [&](const Unit& t) {                     // FUN_0048c950
        if (!t.used || !t.onField() || down(t)) return false;
        switch (logic::sdU8(rec + kSkillAimRule, v)) {
            case 2: if (relationOf(from, t) != 1) return false; break;
            case 3: if (!friendly(from, t)) return false; break;
            case 4: if (t.slot != from.slot) return false; break;
            case 5: if (t.slot == from.slot) return false; break;
            default: break;
        }
        if (skill == 0x2b) {                                      // not at job classes 4 and 5
            const int cls = logic::sdU8(0x6b2d8a + std::uint32_t(t.data.u16(0xc)) * 0x40, v);
            if (cls == 4 || cls == 5) return false;
        }
        return true;
    };
    std::vector<Unit*> hits;
    if (mode >= 0 && mode < 5) {
        for (Unit& t : units_)
            if (touches(t) && map_ && map_->inRange(cx, cy, t.cx(), t.cy(), radius)) hits.push_back(&t);
    } else if (mode == 5) {                                       // FUN_00489e70: the chain
        std::vector<Unit*> pool;                                  // FUN_00489df0: the candidates' list (+0xde460)
        for (Unit& t : units_) if (touches(t)) pool.push_back(&t);
        int px = cx, py = cy;
        while (!pool.empty() && map_) {
            auto best = pool.end();
            int nearest = 0x7fffffff;
            for (auto it = pool.begin(); it != pool.end(); ++it) {
                if (!map_->inRange(px, py, (*it)->cx(), (*it)->cy(), radius)) continue;
                const int dist = std::abs(px - (*it)->cx()) + std::abs(py - (*it)->cy());
                if (dist < nearest) { nearest = dist; best = it; }
            }
            if (best == pool.end()) break;
            hits.push_back(*best);
            px = (*best)->cx();
            py = (*best)->cy();
            pool.erase(best);
        }
    }
    // FUN_00489a60: patterns 1 and 2 take the targets nearest the centre
    // first (cells apart, an exchange sort as the original's).
    if (mode == 1 || mode == 2)
        for (std::size_t i = 0; i + 1 < hits.size(); ++i)
            for (std::size_t j = i + 1; j < hits.size(); ++j) {
                const int di = std::abs(cx - hits[i]->cx()) + std::abs(cy - hits[i]->cy());
                const int dj = std::abs(cx - hits[j]->cx()) + std::abs(cy - hits[j]->cy());
                if (dj < di) std::swap(hits[i], hits[j]);
            }
    if (tactics::traceUnits())
        SDL_Log("[tactics] unit %d skill %d at %d,%d: area %d, mode %d, %zu hit", from.slot, skill, cx, cy, radius, mode,
                hits.size());
    const Fighter a = learntFighter(from, skill);
    const int effect = logic::sdU8(row + 6, v);
    const bool noAnim = logic::sdS16(anim + 0xc, v) == 0;
    const int kind = logic::sdU8((noAnim ? skillRow(jobSkill(from)) : rec) + kSkillType, v);
    // FUN_00491ef0 by the row's +6: 1 / 4 HP, 2 / 5 MP, 3 / 6 both, 0
    // nothing; a skill without its own hit (anim +0xc 0) goes by the job's
    // skill, always HP.
    const int pools = noAnim ? kPoolHp : effect == 0 ? 0 : (effect - 1) % 3 == 0 ? kPoolHp : (effect - 1) % 3 == 1 ? kPoolMp : kPoolHp | kPoolMp;
    const int aimKind = logic::sdU8(rec + kSkillAim, v);
    const auto step = [&](int k) { return int(logic::sdS16(0x618ee8 + std::uint32_t(k) * 2, v)); };
    const int count = int(hits.size());
    for (int i = 0; i < count; ++i) {
        Unit* t = hits[std::size_t(i)];
        int result = 1, amount = 0;
        if (noAnim) {
            if (kind == 1 || kind == 4) std::tie(result, amount) = weaponRoll(a, *t);
            else if (kind == 2) std::tie(result, amount) = spellRoll(a, *t);
        } else if (kind == 1 || kind == 4) {
            if (effect >= 1 && effect <= 3) std::tie(result, amount) = weaponRoll(a, *t);
        } else if (kind == 2) {
            if (effect >= 1 && effect <= 3) std::tie(result, amount) = spellRoll(a, *t);
            else if (effect >= 4 && effect <= 6) amount = rollAmount(a);   // FUN_00482810
        }
        if (result == 4) amount = 0;
        if (result != 0 && steadyNow(*t)) result = 1;   // FUN_00488bc0: FUN_004692e0
        if (effect >= 1 && effect <= 3) amount = -amount;
        else if (effect == 0) amount = 0;
        if (tactics::traceUnits())
            SDL_Log("[tactics] unit %d skill %d (area %d, mode %d) on %d: result %d, %+d", from.slot, skill, radius, mode,
                    t->slot, result, amount);
        // The wait: 1 nearest first, 2 farthest first, 3 by distance out,
        // 4 by distance in, 5 along the chain, 0 at random (10..59).
        const int dist = std::abs(cx - t->cx()) + std::abs(cy - t->cy());
        int delay = 0;
        switch (mode) {
            case 1: delay = i * step(0) + 10; break;
            case 2: delay = (count - i) * step(1) + 10; break;
            case 3: delay = dist * step(2) + 10; break;
            case 4: delay = std::max(0x8a - dist * step(3), 10); break;
            case 5: delay = i * step(5) + 10; break;
            default: delay = rand() % 0x32 + 10; break;
        }
        if ((aimKind == 1 || aimKind == 2 || aimKind == 3 || aimKind == 6) && t == &primary) delay = 0;
        if (noAnim) {
            // FUN_00482d60 with the job skill's row; the unit's own effect
            // (+0x45) goes on at once unless it missed.
            const int id = result == 0 ? -1 : hitEffect(jobSkill(from), result);
            strike(from, *t, result, amount, skill, {cx, cy}, id, pools, delay, true);
            if (result != 0) inflict(from.data.u8(0x45), skill, from, *t);
        } else {
            // The skill's hit effect (anim +0xc). The original sets it off by
            // rand() % 20 - 10 each way; the port's slot has no offset, but
            // the draws are kept.
            (void)rand();
            (void)rand();
            strike(from, *t, result, amount, skill, {cx, cy}, logic::sdS16(anim + 0xc, v), pools, delay, true);
        }
    }
}

// FUN_00491ef0's tail: the skill's added effect unless the hit did nothing,
// then the cure (0x1d) and the dispel of blessings (0xc, 0x2f) on any result.
void TacticsUnits::learntTail(Unit& from, Unit& to, int skill, int result) {
    if (skill <= 0 || skill >= 100) return;
    const std::uint32_t rec = skillRow(skill);
    if (result != 4) inflict(logic::sdU8(rec + kSkillAdded, ctx_.version), skill, from, to);
    if (skill == 0x1d) cure(from, to);
    // FUN_004924e0: the blessing (slot 2) goes when rand() % 100 < 100.
    else if ((skill == 0xc || skill == 0x2f) && to.status[2].type != 0) {
        (void)rand();                              // always under 100
        clearStatus(to, 2);
    }
}

void TacticsUnits::queueChange(Unit& u, int kind, int delta) {
    u.changes.push_back({static_cast<std::uint8_t>(kind), static_cast<std::int16_t>(delta)});
}

// FUN_00493450: walks the list from its head, the newest first, through
// FUN_00472550 (kind 2 is never queued), and frees the nodes.
void TacticsUnits::applyChanges(Unit& u) {
    std::vector<Unit::Change> list;
    list.swap(u.changes);
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (it->kind == 0) addHp(u, it->delta);
        else addMp(u, it->delta);
    }
}

// FUN_00472550(.., 0, delta): +0x54 lets a big enough blow leave 1 HP.
// --------------------------------------------------- status conditions ---
// The conditions' table (DAT_006f4088, 0x10 bytes per type 0..0x3b): +2
// the slot + 1, +3 the strength (a weaker one does not replace a stronger),
// +4 which resistance holds it off (1 the status resistance +0x4e, else the
// magic resistance +0x50), +5 2: rolled against it, +7..+0xc the types it
// cancels instead of taking hold.

namespace {
constexpr std::uint32_t kStatusTypes = 0x6f4088;
constexpr std::uint32_t kStatusEffects = 0x615dd0;  // s16 effect id per type
constexpr std::uint32_t kJobResist   = 0x6b2daa;   // u8 [kind * 0x40 + column]

} // namespace

int TacticsUnits::statusSlot(const Unit& u, int type) {
    for (int i = 0; i < 3; ++i)
        if (u.status[std::size_t(i)].type == type) return i + 1;
    return 0;
}

// FUN_004902e0: empties the slot and stops its effect (slot + 2).
void TacticsUnits::clearStatus(Unit& u, int slot) {
    u.status.at(std::size_t(slot)) = Status{};
    playEffect(u, slot + 2, -1);
}

// FUN_0048b720: how long it lasts, by the skill's formula row +3: the
// caster's +0x48, or the record's +0x1c plus a percentage (+0x1e) of one
// of its stats (+4, 6, 7, 8, 5).
int TacticsUnits::statusTime(int skill, const Unit& from) const {
    const GameVersion v = ctx_.version;
    const std::uint32_t rec = skillRow(skill);
    const int base = logic::sdS16(rec + 0x1c, v) & 0xffff, pct = logic::sdS16(rec + 0x1e, v) & 0xffff;
    static constexpr std::array<std::uint32_t, 5> kStats{4, 6, 7, 8, 5};
    const int kind = logic::sdU8(formulas(skill) + 3, v);
    if (kind == 1) return from.data.s32(0x48);
    if (kind >= 2 && kind <= 6) return base + from.data.u8(kStats[std::size_t(kind - 2)]) * pct / 100;
    return 0;
}

// The HP / MP a tick takes: the learnt skills' record +0x28 plus +0x2a % of
// the caster's stat, the job skills' the caster's +0x42.
int TacticsUnits::statusPower(int skill, const Unit& from, int stat) const {
    const GameVersion v = ctx_.version;
    if (skill > 100) return from.data.s16(0x42);
    const std::uint32_t rec = skillRow(skill);
    return logic::sdS16(rec + 0x28, v) + from.data.u8(std::uint32_t(stat)) * logic::sdS16(rec + 0x2a, v) / 100;
}

// FUN_0048b2e0: an attack's added effect (the attacker's +0x45, or a learnt
// skill's record +0x1a) on the unit it struck, by the handlers:
// FUN_0048b900 poison (FUN_0048b980 type 4, FUN_0048bd20 type 9 past the
// attacker's +6 75), FUN_0048ba70 the drain (FUN_0048baf0 type 5,
// FUN_0048be10 type 10 past the target's +7 75), FUN_0048bbe0 6,
// FUN_0048bc80 7 (no power), FUN_0048c0e0 0xd, FUN_0048bf00 / FUN_0048c170
// 0xb / 0xe against weapon fighters, FUN_0048bff0 / FUN_0048c210 0xc / 0xf
// against casters, each rolled by FUN_0048c850 with its time
// (FUN_0048b720); FUN_0048c2b0, FUN_0048c340, FUN_0048c3d0, FUN_0048c460,
// FUN_0048c4f0, FUN_0048c580, FUN_0048c610, FUN_0048c6a0, FUN_0048c730 and
// FUN_0048c7c0 the blessings 0x29, 0x2c..0x2f, 0x31..0x35, set outright
// (FUN_00490180). FUN_0048b6c0 swaps in the learnt skill's record.
void TacticsUnits::inflict(int effect, int skill, Unit& from, Unit& to) {
    if (effect == 0 || skill <= 0) return;
    const GameVersion v = ctx_.version;
    Status s{};
    s.skill = static_cast<std::int16_t>(skill);
    // FUN_0048bf00 / FUN_0048bff0 ...: only against fighters of the job
    // skill's kind (+5: 1 and 4 weapons, 2 spells).
    const auto targetKind = [&] {
        return logic::sdU8(skillRow(jobSkill(to)) + kSkillType, v);
    };
    switch (effect) {
        case 2: case 4: case 9: {                  // poison: the stronger past +6 75
            const int type = effect == 2 ? (from.data.u8(6) < 0x4b ? 4 : 9) : effect;
            s.time = statusTime(skill, from);
            s.power = static_cast<std::int16_t>(statusPower(skill, from, 6));
            return tryStatus(skill, type, from, to, s);
        }
        case 3: case 5: case 10: {                 // the MP drain: the stronger past the target's +7 75
            const int type = effect == 3 ? (to.data.u8(7) < 0x4b ? 5 : 10) : effect;
            s.time = statusTime(skill, from);
            s.power = static_cast<std::int16_t>(statusPower(skill, from, 7));
            return tryStatus(skill, type, from, to, s);
        }
        case 6: case 7: case 0xd:
            s.time = statusTime(skill, from);
            return tryStatus(skill, effect, from, to, s);
        case 0xb: case 0xe: {                      // against weapon fighters
            const int k = targetKind();
            if (k != 1 && k != 4) return;
            s.time = statusTime(skill, from);
            return tryStatus(skill, effect, from, to, s);
        }
        case 0xc: case 0xf:                        // against casters
            if (targetKind() != 2) return;
            s.time = statusTime(skill, from);
            return tryStatus(skill, effect, from, to, s);
        case 0x29: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
        case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
            // FUN_0048c2b0 ...: the blessings take hold without a roll.
            s.time = statusTime(skill, from);
            return applyStatus(to, 2, effect, s);
        default: break;
    }
}

// FUN_004828a0 with FUN_004dfba0: the chance (100 less the resistance,
// plus the job's column) scales the time and the power, which turns to a
// loss; under 184 frames it fails.
bool TacticsUnits::statusRoll(int type, const Unit& to, Status& s) {
    const GameVersion v = ctx_.version;
    const std::uint32_t t = kStatusTypes + std::uint32_t(type) * 0x10;
    const Fighter d = fighter(to);
    const int column = logic::sdU8(t + 0xe, v);
    const int job = column < 0xc ? logic::sdU8(kJobResist + std::uint32_t(column) + std::uint32_t(to.data.u16(0xc)) * 0x40, v) : 0;
    int chance = (100 - (logic::sdU8(t + 4, v) == 1 ? d.sresist : d.resist)) + job;
    chance = std::min(chance, 100);
    const int roll = rand() % 100;
    if ((logic::sdU8(t + 5, v) != 2 || chance < roll) && chance != 0) {
        if (chance < 0) chance = 100 - chance;
        s.time = s.time * chance / 100;
        s.power = static_cast<std::int16_t>(-(s.power * chance / 100));
        if (s.time > 0xb7) return true;
    }
    s.time = 0;
    s.power = 0;
    return false;
}

// FUN_0048c850: the roll, and for the job skills the attacker's added
// effect rate (+0x41); a miss shows the resisted message.
void TacticsUnits::tryStatus(int skill, int type, Unit& from, Unit& to, Status s) {
    if (!statusRoll(type, to, s) || (skill > 100 && rand() % 100 >= from.data.u8(0x41))) {
        showMessage(to, 0);
        return;
    }
    applyStatus(to, 0, type, s);
}

// FUN_00490180: a condition it cancels goes instead (FUN_0048ff70); else it
// takes the slot unless a stronger one holds it (FUN_0048fe50): the slot
// is cleared (FUN_004902e0) and the type's setter from DAT_00618fb8 fills
// it (types without one never take hold). The setters all copy the skill,
// time and power, then show the condition's message (FUN_00495130) and,
// all but type 0x15, start its effect (DAT_00615dd0 by type) on effect
// slot 2 for the ailments or 4 for the blessings:
//   FUN_0048f210 4, FUN_0048f2a0 5, FUN_0048f330 6, FUN_0048f3c0 7,
//   FUN_0048f450 9, FUN_0048f4e0 10, FUN_0048f570 11, FUN_0048f600 12,
//   FUN_0048f690 13, FUN_0048f720 14, FUN_0048f7b0 15, FUN_0048f840 0x15;
//   FUN_0048f8b0 0x29, FUN_0048f940 0x2c, FUN_0048f9d0 0x2d,
//   FUN_0048fa60 0x2e, FUN_0048faf0 0x2f, FUN_0048fb80 0x31,
//   FUN_0048fc10 0x32, FUN_0048fca0 0x33, FUN_0048fd30 0x34, FUN_0048fdc0 0x35.
void TacticsUnits::applyStatus(Unit& u, int slot, int type, const Status& s) {
    const GameVersion v = ctx_.version;
    if (type < 1 || type > 0x3b || slot < 0 || slot > 2) return;
    const std::uint32_t t = kStatusTypes + std::uint32_t(type) * 0x10;
    int cancelled = 0;
    for (std::uint32_t i = 0; i < 6; ++i) {
        const int c = logic::sdU8(t + 7 + i, v);
        if (const int at = c != 0 ? statusSlot(u, c) : 0; at != 0) {
            clearStatus(u, at - 1);
            ++cancelled;
        }
    }
    if (cancelled != 0) return;
    Status& cur = u.status[std::size_t(slot)];
    if (cur.type != 0 && logic::sdU8(t + 3, v) < logic::sdU8(kStatusTypes + std::uint32_t(cur.type) * 0x10 + 3, v)) return;
    if (logic::sdS32(0x618fb8 + std::uint32_t(type) * 4, v) == 0) return;
    clearStatus(u, slot);
    cur = s;
    cur.type = static_cast<std::uint8_t>(type);
    cur.tick = 0;
    if (type != 0x15) playEffect(u, type >= 0x29 ? 4 : 2, logic::sdS16(kStatusEffects + std::uint32_t(type) * 2, v));
    if (tactics::traceUnits())
        SDL_Log("[tactics] unit %d takes status %d (skill %d) for %d frames, power %d", u.slot, type, s.skill, s.time, s.power);
    showMessage(u, type >= 0x29 ? 0xe : 10);
}

// FUN_00490350 -> FUN_00490a50 for each slot, every frame: fallen units
// lose them; the time runs out (0 lasts); then the type's handler from
// DAT_006190d0 counts the 180-frame tick: poison FUN_004903c0 (4) and
// FUN_00490540 (9) change the HP and the drain FUN_00490430 (5) and
// FUN_004905b0 (10) the MP by the power; the others only count
// (FUN_004904a0 6, FUN_004904f0 7, FUN_00490620 11, FUN_00490670 12,
// FUN_004906e0 14, FUN_00490730 15, FUN_00490780 0x15, FUN_004907d0 0x2d,
// FUN_00490820 0x2e, FUN_00490870 0x2f, FUN_004908c0 0x31,
// FUN_00490910 0x32, FUN_00490960 0x33, FUN_004909b0 0x34,
// FUN_00490a00 0x35) and haste's FUN_004906c0 (13) does nothing.
void TacticsUnits::tickStatus(Unit& u) {
    for (int i = 0; i < 3; ++i) {
        Status& s = u.status[std::size_t(i)];
        if (down(u)) {
            if (s.type != 0) clearStatus(u, i);
            continue;
        }
        if (s.type == 0) continue;
        if (s.time != 0 && --s.time == 0) {
            clearStatus(u, i);
            continue;
        }
        if (++s.tick <= 0xb3) continue;
        s.tick = 0;
        if (s.type == 4 || s.type == 9) {          // FUN_00493320
            queueChange(u, 0, s.power);
            showPoison(u, s.power);
        } else if (s.type == 5 || s.type == 10) {  // FUN_00493180
            queueChange(u, 1, s.power);
            showMp(u, s.power);
        }
    }
}

// FUN_00492320: skill 0x1d lifts the ailment by chance, by the caster's +7
// (DAT_00619200 rows) and the ailment's strength.
void TacticsUnits::cure(const Unit& from, Unit& to) {
    const GameVersion v = ctx_.version;
    const int type = to.status[0].type;
    if (type == 0) return;
    const int s7 = from.data.u8(7);
    const std::uint32_t row = 0x619200 + std::uint32_t(s7 < 0x1f ? 0 : s7 < 0x33 ? 6 : s7 < 0x4c ? 12 : s7 < 0x5b ? 18 : 24);
    const int strength = logic::sdU8(kStatusTypes + std::uint32_t(type) * 0x10 + 3, v);
    if (rand() % 100 < logic::sdU8(row + std::uint32_t(strength), v)) {
        clearStatus(to, 0);
        showMessage(to, 1);
    }
}

// FUN_004dea30 -> FUN_004dfa70 for each slot holding time: the conditions'
// changes (DAT_00738af8 [type]) to the combat values, from the causing
// skill's record (base, then a stat of the unit's own times the next
// field, / 100) and clamped: 11 FUN_004deaf0 (its accuracy takes the
// multiplier at +0x2a, not +0x22), 12 FUN_004deca0, 14 FUN_004dedf0,
// 15 FUN_004def80, 0x29 FUN_004df0b0, 0x2c FUN_004df240, 0x2d
// FUN_004df3d0, 0x31 FUN_004df560, 0x32 FUN_004df6f0, 0x33 FUN_004df880,
// 0x35 FUN_004df9b0.
void TacticsUnits::statusStats(const Unit& u, Fighter& f) const {
    const GameVersion v = ctx_.version;
    struct Change { int Fighter::*field; std::uint32_t base; int hi; std::uint32_t mul = 0; };
    const auto apply = [&](const Status& s, int stat, std::initializer_list<Change> changes) {
        const std::uint32_t rec = skillRow(s.skill);
        for (const Change& c : changes) {
            const std::uint32_t mul = c.mul != 0 ? c.mul : c.base + 2;
            const int add = logic::sdS16(rec + c.base, v) + u.data.u8(std::uint32_t(stat)) * logic::sdS16(rec + mul, v) / 100;
            f.*c.field = std::clamp(f.*c.field + add, 0, c.hi);
        }
    };
    for (const Status& s : u.status) {
        if (s.type == 0 || s.time == 0) continue;
        switch (s.type) {
            case 11: apply(s, 7, {{&Fighter::acc, 0x20, 999, 0x2a}, {&Fighter::atk, 0x2c, 999}, {&Fighter::spread, 0x30, 999}}); break;
            case 12: apply(s, 8, {{&Fighter::atk, 0x28, 999}, {&Fighter::spread, 0x2c, 999}}); break;
            case 14: apply(s, 8, {{&Fighter::guard, 0x28, 0xff}, {&Fighter::sresist, 0x2c, 100}, {&Fighter::evade, 0x30, 0xff}}); break;
            case 15: apply(s, 8, {{&Fighter::mguard, 0x28, 0xff}, {&Fighter::resist, 0x2c, 100}}); break;
            case 0x29: apply(s, 4, {{&Fighter::acc, 0x20, 999}, {&Fighter::atk, 0x24, 999}, {&Fighter::spread, 0x28, 999}}); break;
            case 0x2c: case 0x2d: case 0x31:
                apply(s, 8, {{&Fighter::acc, 0x20, 999}, {&Fighter::atk, 0x24, 999}, {&Fighter::spread, 0x28, 999}}); break;
            case 0x32: apply(s, 8, {{&Fighter::guard, 0x20, 0xff}, {&Fighter::sresist, 0x24, 100}, {&Fighter::evade, 0x28, 0xff}}); break;
            case 0x33: apply(s, 8, {{&Fighter::mguard, 0x20, 0xff}, {&Fighter::resist, 0x24, 100}}); break;
            case 0x35: apply(s, 8, {{&Fighter::evade, 0x24, 0xff}}); break;
            default: break;
        }
    }
}

// FUN_00490b30: the walking speed: slow (7) halves it, haste (13) and the
// blessings 0x34 / 0x35 scale it by their formulas.
float TacticsUnits::speedScale(const Unit& u) const {
    const GameVersion v = ctx_.version;
    const auto formula = [&](std::uint32_t at, int stat) {
        return float(logic::sdS16(at, v) + u.data.u8(std::uint32_t(stat)) * logic::sdS16(at + 2, v) / 100) / 100.0f;
    };
    float k = 1.0f;
    if (u.status[0].type == 7) k = 0.5f;
    else if (u.status[0].type == 13) k = formula(0x6c3e38, 7);
    if (u.status[2].type == 0x34) k *= formula(0x6c4348, 7);
    else if (u.status[2].type == 0x35) k *= formula(0x6c3bf8, 8);
    return k;
}

// FUN_00490cd0: the cast and recovery times: x1.5 under slow (7, the job
// skills), under 9 for weapon and 10 for spell skills (the learnt ones);
// haste (13) or blessing 0x34 by the causing skill's +0x24 / +0x26.
void TacticsUnits::castScale(Unit& u, int skill) const {
    const GameVersion v = ctx_.version;
    const auto scale = [&](int pct) {
        u.castFull = static_cast<std::int16_t>(u.castFull * pct / 100);
        u.recoverFull = static_cast<std::int16_t>(u.recoverFull * pct / 100);
    };
    const int a = u.status[0].type;
    if (skill <= 100) {
        const int kind = logic::sdU8(skillRow(skill) + kSkillType, v);
        if (a == 9) { if (kind == 1) scale(0x96); return; }
        if (a == 10) { if (kind == 2) scale(0x96); return; }
    } else if (a == 7) {
        scale(0x96);
        return;
    }
    const auto byRecord = [&](const Status& s) {
        const std::uint32_t rec = skillRow(s.skill);
        scale(logic::sdS16(rec + 0x24, v) + u.data.u8(7) * logic::sdS16(rec + 0x26, v) / 100);
    };
    if (a == 13) byRecord(u.status[0]);
    else if (u.status[2].type == 0x34) byRecord(u.status[2]);
}

void TacticsUnits::addMp(Unit& u, int delta) {
    u.setMp(std::clamp<int>(u.mp() + delta, 0, u.data.s16(0x1a)));
}

// ------------------------------------------------------------ effects ---

// FUN_0046beb0 -> FUN_00465140: effect `id` (1000 * file + no, DAT_0060f890
// {?, set, clip}) on the unit's slot; -1 or 0 stops it.
void TacticsUnits::playEffect(Unit& u, int slot, int id) {
    const GameVersion v = ctx_.version;
    AnmPlayer::NodeId& n = u.fx.at(std::size_t(slot));
    if (slot == 0 && !u.landing.empty()) flushLanding(u);
    u.fxOn[std::size_t(slot)] = false;
    if (id < 1000 || !effect_.loaded()) return;
    const int file = id / 1000 - 1, no = id % 1000;
    if (file < 0 || file > 3 || no > 199) return;
    const std::uint32_t e = 0x60f890 + std::uint32_t(file) * 800 + std::uint32_t(no) * 4;
    if (n == AnmPlayer::kNone) n = anm_.newRoot(effect_);
    anm_.play(n, logic::sdU8(e + 1, v), logic::sdU8(e + 2, v), 0);
    (void)anm_.takeSignals(n);
    u.fxOn[std::size_t(slot)] = true;
}

// The effect slots each frame: a flag-4 frame of the strike effect sends
// the unit's hit, a flag-8 frame (or the end) of the hit effect lands what
// it carries.
void TacticsUnits::updateEffects(Unit& u) {
    if (!u.delayed.empty()) {
        std::vector<Unit::Delayed> ready;
        for (auto it = u.delayed.begin(); it != u.delayed.end();) {
            if (--it->wait > 0) { ++it; continue; }
            ready.push_back(*it);
            it = u.delayed.erase(it);
        }
        for (const Unit::Delayed& d : ready) {
            const Unit::Landing& l = d.hit;
            if (l.from < 0 || l.from >= kSlots) continue;
            strike(units_[std::size_t(l.from)], u, l.result, l.amount, l.skill, l.at, d.effect, l.pools, 0, l.tail);
        }
    }
    for (std::size_t k = 0; k < u.fx.size(); ++k) {
        if (!u.fxOn[k]) continue;
        const bool ended = anm_.update(u.fx[k]);
        const AnmPlayer::Signals sig = anm_.takeSignals(u.fx[k]);
        if (k == 0 && sig.hit) u.hitSignal = true;
        if (k == 0 && (sig.land || ended) && !u.landing.empty()) flushLanding(u);
        if (ended) u.fxOn[k] = false;
    }
}

// FUN_004918d0 -> FUN_00491960 on each effect's list (+0x48, +0xa0 + i *
// 0x58): every hit whose frame came goes to FUN_004919f0 and its node is
// freed (FUN_0040be20).
void TacticsUnits::flushLanding(Unit& u) {
    std::vector<Unit::Landing> pending;
    pending.swap(u.landing);
    if (tactics::traceUnits()) SDL_Log("[tactics] unit %d: %zu hit(s) land", u.slot, pending.size());
    for (const Unit::Landing& l : pending)
        if (l.from >= 0 && l.from < kSlots) land(units_[std::size_t(l.from)], u, l.result, l.amount, l.skill, l.at, l.pools, l.tail);
}

int TacticsUnits::hitEffect(int rowSkill, int result) {
    const std::uint32_t row = 0x613b78 + std::uint32_t(std::max<int>(rowSkill, 0)) * 6;
    int id = logic::sdS16(result == 4 ? row : result == 3 ? row + 4 : row + 2, ctx_.version);
    switch (id) {
        case 0x7d1: case 0x7d7: case 0x7dc: case 0x7e2:
        case 0x7e7: case 0x7ed: case 0x804: case 0x809:
            id += rand() % 5;
            break;
        default: break;
    }
    return id;
}

void TacticsUnits::strike(Unit& from, Unit& to, int result, int amount, int skill, tactics::Cell at, int effect,
                          int pools, int delay, bool tail) {
    if (result == 0) {                             // FUN_004950c0
        land(from, to, 0, 0, skill, at);
        return;
    }
    const int id = effect >= 0 ? effect : hitEffect(from.skill, result);
    // The effect node keeps still and unseen while the packet's first word
    // counts down (FUN_0040a800: to 0, then -2, then it starts at -3).
    if (delay > 0) {
        to.delayed.push_back({{from.slot, result, amount, skill, at, pools, tail}, id, delay + 1});
        return;
    }
    playEffect(to, 0, id);
    if (!to.fxOn[0]) {
        land(from, to, result, amount, skill, at, pools, tail);
        return;
    }
    to.landing.push_back({from.slot, result, amount, skill, at, pools, tail});
    if (tactics::traceUnits()) SDL_Log("[tactics] unit %d: hit effect %d on %d", from.slot, id, to.slot);
}

void TacticsUnits::addHp(Unit& u, int delta) {
    int hp = std::min(u.hp() + delta, 0x7fff);
    const int guts = u.data.u16(0x54);
    if (guts != 0 && hp < 1 && delta < 0 && guts <= u.hp() && guts <= -delta) hp = 1;
    u.setHp(std::clamp<int>(hp, 0, u.maxHp()));
}

// FUN_00469220: a unit off the field but still in the battle (+0xf 2 waiting
// to go out, 3 fallen, 4 gone).
bool TacticsUnits::waiting(const Unit& u) {
    const int at = u.data.u8(0xf);
    return at >= 2 && at <= 4;
}

// FUN_00472370: 1 HP every +0x18 frames, 1 MP every +0x1e, both through
// FUN_00472550. The original subtracts unit +0x508 / +0x50a from the
// intervals, but nothing in ch.exe ever writes them (always 0). Off the
// field (FUN_00469220) HP comes back twice as fast and MP half as fast;
// a negative interval counts as 1.
void TacticsUnits::regenerate(Unit& u) {
    const int hp = u.hp(), mp = u.mp();
    const bool off = waiting(u);
    if (hp == 0 || hp >= u.maxHp()) {
        u.regenHp = 0;
    } else {
        ++u.regenHp;
        int every = u.data.s16(0x18);
        if (off) every = every * 0x32 / 100;
        if (every < 0) every = 1;
        if (every <= u.regenHp) {
            u.regenHp = 0;
            addHp(u, 1);
        }
    }
    if (mp == 0 || mp < u.data.s16(0x1a)) {
        ++u.regenMp;
        int every = u.data.s16(0x1e);
        if (off) every = every * 200 / 100;
        if (every < 0) every = 1;
        if (every <= u.regenMp) {
            u.regenMp = 0;
            addMp(u, 1);
        }
    } else {
        u.regenMp = 0;
    }
}

// FUN_004799c0: the unit falls. Whoever struck last (+0xa6) is credited
// the kill (+0xaa) and the unit's worth (+0xa8 into +0xac); after the
// fall clip and 70 frames the unit leaves the field (+0xf 3).
void TacticsUnits::fallState(Unit& u) {
    const GameVersion v = ctx_.version;
    switch (u.sub) {
        case 0: {
            u.selected = false;
            u.path.clear();
            u.data.set8(0xae, std::min(u.data.u8(0xae) + 1, 0x7f));
            const int by = u.data.s16(0xa6);
            if (by >= 0 && by < kSlots && units_[std::size_t(by)].used) {
                Unit& k = units_[std::size_t(by)];
                // Each 16-bit sum is kept only if it grew (one that would
                // wrap leaves the old value).
                const auto grow = [](int old, int add) {
                    const auto sum = static_cast<std::int16_t>(old + add);
                    return old < sum ? int(sum) : old;
                };
                k.data.set16(0xaa, static_cast<std::int16_t>(grow(k.data.s16(0xaa), 1)));
                k.data.set16(0xac, static_cast<std::int16_t>(grow(k.data.s16(0xac), u.data.s16(0xa8))));
                if (k.team() < 16) {               // +0x2ef14 [the killer's team]
                    int& worth = teamWorth_[std::size_t(k.team())];
                    worth = grow(worth, u.data.s16(0xa8));
                }
            }
            // FUN_00430b20: a fighting member (FUN_00468e10) adds to its team's falls.
            if (u.kind() >= 1 && u.kind() <= 0x2d && u.team() < 16) ++teamFalls_[std::size_t(u.team())];
            setFlags(u, true);                     // FUN_0046b0b0 / FUN_0046b0f0(.., 1)
            if (tactics::traceUnits()) SDL_Log("[tactics] unit %d falls (by %d)", u.slot, by);
            playAnim(u, sdS8(kFallAnims + std::uint32_t(u.data.u16(0xc)) * 10, v));
            voice(u, 0x71);
            ++u.sub;
            break;
        }
        case 1:
            if (u.animEnd) {
                ++u.sub;
                u.timer = 0;
            }
            break;
        case 2:
            if (u.timer > 0x46) {
                setFlags(u, false);
                u.data.set8(0xf, 3);
                // FUN_004799c0: a party member (FUN_00468eb0) with no more
                // than 0xe0f of its time left (+0x24) is out of action (4,
                // time 0). Otherwise the disc builds cut one of ours to 50%
                // of its time, 75% with learnt skill 0x54 (TW 0x479d9b, JP
                // 0x43f92c; the later build has dropped the store), at least
                // 0. The multiplication wraps as the original's does.
                const int kind = u.kind();
                if (u.human() && timeLimited()) {
                    if (kind >= 1 && kind <= 0x2d && u.data.s32(0x24) <= 0xe0f) {
                        u.data.set32(0x24, 0);
                        u.data.set8(0xf, 4);
                    } else {
                        const std::uint32_t pct = hasLearnt(u, 0x54) ? 75 : 50;
                        const auto scaled = static_cast<std::int32_t>(static_cast<std::uint32_t>(u.data.s32(0x24)) * pct);
                        u.data.set32(0x24, std::max(scaled / 100, 0));
                    }
                }
                setState(u, kOffField);
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// The popups (unitctrlpopup.cpp). Each kind runs a script from
// PTR_DAT_00619668 [kind], 8 bytes a step: s8 dx, dy, u8 flags (0x80 the
// last), alpha, s8 glyph (DAT_00619748; -1 for the numbers), s16 frames.
// The numbers: 0 hurt, 1 hurt by magic, 2 healed, 3 MP gained, 4 poison,
// 5 MP lost (+9 over a unit not the viewer's; 6..8 and 0xf..0x11 are not
// used), in digit style DAT_00619f08 [kind]. The words: 0x12 no damage,
// 0x13 miss, 0x14 + n the messages (FUN_00495130 adds 0x14 and
// FUN_004951b0 0x14 more: 0x28 resisted, 0x29 cured, 0x32 an ailment,
// 0x36 a blessing).
// ---------------------------------------------------------------------------
namespace {
constexpr std::uint32_t kPopupScripts = 0x619668;   // u32 per kind
constexpr std::uint32_t kPopupStyles  = 0x619f08;   // u32 per number kind
}

// FUN_00494d20 (and FUN_00494d70 for each).
void TacticsUnits::clearPopups() {
    popups_.fill(Popup{});
}

// FUN_00494db0: the first free record (none when all 0x200 are in use),
// then FUN_00495630: the kind's script from its first step, and a random
// offset (the CRT's rand, FUN_00467e70): the numbers dx -12..11, dy
// -0x2c..-0x33; the words and messages dx -8..7, dy -0x2c..-0x25 (the
// kinds 0x14..0x27, -4..3).
void TacticsUnits::popup(const Unit& u, int kind, int value) {
    const GameVersion v = ctx_.version;
    if (kind < 0 || kind > 0x3b || (kind >= 6 && kind <= 8) || (kind >= 0xf && kind <= 0x11)) return;
    auto it = std::find_if(popups_.begin(), popups_.end(), [](const Popup& p) { return !p.on; });
    if (it == popups_.end()) return;
    Popup& p = *it;
    p = Popup{};
    p.on = true;
    p.kind = static_cast<std::uint8_t>(kind);
    p.unit = u.slot;
    p.value = std::abs(value);
    p.script = std::uint32_t(logic::sdS32(kPopupScripts + std::uint32_t(kind) * 4, v));
    p.timer = logic::sdS16(p.script + 6, v);
    if (kind <= 0xe) {
        p.dx = std::rand() % 0x18 - 0xc;
        p.dy = -0x2c - std::rand() % 8;
    } else if (kind >= 0x14 && kind <= 0x27) {
        p.dx = std::rand() % 16 - 8;
        p.dy = std::rand() % 8 - 4;
    } else {
        p.dx = std::rand() % 16 - 8;
        p.dy = std::rand() % 8 - 0x2c;
    }
}

void TacticsUnits::showNumber(const Unit& u, int value, int kind) {
    popup(u, kind, value);
}

// The kinds FUN_00493090 / FUN_00493180 / FUN_00493320 pick, +9 when the
// unit is not the viewer's (FUN_0046a430).
void TacticsUnits::showHp(const Unit& u, int amount, int style) {
    showNumber(u, amount, (amount < 1 ? style : 2) + (u.team() == viewer_ ? 0 : 9));
}

void TacticsUnits::showMp(const Unit& u, int amount) {
    showNumber(u, amount, (amount < 1 ? 5 : 3) + (u.team() == viewer_ ? 0 : 9));
}

void TacticsUnits::showPoison(const Unit& u, int amount) {
    showNumber(u, amount, 4 + (u.team() == viewer_ ? 0 : 9));
}

void TacticsUnits::showMessage(const Unit& u, int n) {
    popup(u, n + 0x28);
}

// FUN_00491ef0's style for a hit's number: by the learnt skill's record
// (+5: 2 magic) when it has one, else by the job's own skill.
int TacticsUnits::damageStyle(const Unit& from, int skill) const {
    const GameVersion v = ctx_.version;
    int id = skill;
    if (skill <= 0 || skill > 100 || logic::sdS16(kSkillAnims + std::uint32_t(skill) * 0x10 + 0xc, v) == 0)
        id = logic::sdS16(kJobSkill + std::uint32_t(from.data.u16(0xc)) * 0x40, v);
    if (id < 0) return 0;
    return logic::sdU8(skillRow(id) + kSkillType, v) == 2 ? 1 : 0;
}

// FUN_00495220 -> FUN_00495320 -> FUN_004954f0 / FUN_00495540 /
// FUN_00495590 / FUN_004955e0 by kind, all
// FUN_004958b0: count the step down, at its end the next step (or, after
// the last, free the record); from the fifth step on the numbers rise and
// fade (hurt: 2 px and 4 a frame; healed and MP gained: 1 px and 3).
void TacticsUnits::tickPopups() {
    const GameVersion v = ctx_.version;
    for (Popup& p : popups_) {
        if (!p.on) continue;
        if (--p.timer < 1) {
            if (logic::sdU8(p.script + std::uint32_t(p.step) * 8 + 2, v) & 0x80) {
                p = Popup{};
                continue;
            }
            ++p.step;
            p.timer = logic::sdS16(p.script + std::uint32_t(p.step) * 8 + 6, v);
        }
        switch (p.kind) {
            case 0: case 1: case 4: case 5: case 9: case 10: case 0xd: case 0xe:
                if (p.step > 3 && p.fade + 8 < 0xff) { p.dy -= 2; p.fade += 4; }
                break;
            case 2: case 3: case 0xb: case 0xc:
                if (p.step > 3 && p.fade + 3 < 0xff) { p.dy -= 1; p.fade += 3; }
                break;
            default: break;
        }
    }
}

// FUN_004952a0 -> FUN_00495ab0: over the unit (while it is on the field and
// in view, FUN_0046b130) at priority 0x348: the numbers in their kind's
// colour, faded; the words as their glyph.
void TacticsUnits::drawPopups() const {
    const GameVersion v = ctx_.version;
    if (!map_) return;
    for (const Popup& p : popups_) {
        if (!p.on || p.unit < 0 || p.unit >= kSlots) continue;
        const Unit& u = units_[std::size_t(p.unit)];
        if (!u.used || !u.onField()) continue;
        const std::uint32_t e = p.script + std::uint32_t(p.step) * 8;
        const int x = u.px() + std::int8_t(logic::sdU8(e, v)) + p.dx - map_->viewX();
        const int y = u.py() + TacticsMap::kViewY + std::int8_t(logic::sdU8(e + 1, v)) + p.dy - map_->scrollY();
        if (y < -0x22 || y >= 0x299 || x < -0x40 || x >= 0x441) continue;   // FUN_0046b130
        const int alpha = logic::sdU8(e + 3, v);
        std::uint32_t argb = 0;
        switch (p.kind) {
            case 0: case 9:    argb = 0xff808080u; break;
            case 1: case 10:   argb = 0xff803030u; break;
            case 2: case 0xb:  argb = 0xff808030u; break;
            case 3: case 0xc:  argb = 0xff303080u; break;
            case 4: case 0xd:  argb = 0xff308030u; break;
            case 5: case 0xe:  argb = 0xff803080u; break;
            default:
                tactics::drawGlyph(ctx_, std::int8_t(logic::sdU8(e + 4, v)), x, y, 0x348, std::uint8_t(alpha));
                continue;
        }
        tactics::drawDigits(ctx_, x, y, 0x348, p.value, std::uint8_t(alpha - p.fade),
                            int(logic::sdS32(kPopupStyles + std::uint32_t(p.kind) * 4, v)), argb);
    }
}

// FUN_0046c350(u, 0): the learnt skill held ready (+0x9d, the +0x9e'th of
// +0x84) unless it is one that is never aimed (+5 3), else the job's; -1
// while the slot held is sealed (0x8000).
int TacticsUnits::orderSkill(const Unit& u) const {
    if (u.data.u8(0x9d) != 0) {
        const int w = u.data.u16(0x84 + std::uint32_t(u.data.u8(0x9e)) * 2);
        if (w != 0) {
            if (w & 0x8000) return -1;
            const int skill = w & 0x7fff;
            if (logic::sdU8(skillRow(skill) + kSkillType, ctx_.version) != 3) return skill;
        }
    }
    return jobSkill(u);
}

namespace {
// FUN_0046c480: what a skill of target kind `kind` (record +0x18) does with
// what the pointer is over (`at`, +0x3098d: 1 ours, 2 an enemy, 3 an ally,
// 4 the other enemies, 5 the neutral, 100 the ground): 0 nothing, `at`
// itself (use it on that unit), 100 use it at the cell, 0x6e walk there.
int skillAction(int kind, int at, bool self) {
    switch (kind) {
        case 1: return at == 0 ? 0 : at <= 4 ? at : 0x6e;            // any unit
        case 2: return at == 0 ? 0 : at == 2 || at == 4 ? at : 0x6e; // enemies
        case 3: return at == 0 ? 0 : at == 1 || at == 3 ? at : 0x6e; // our side
        case 4: return at == 0 ? 0 : 100;                          // the ground
        case 5: return at == 0 ? 0 : 0x6e;
        case 6: return at == 0 ? 0 : at == 1 && self ? at : 0x6e;  // the caster itself
        default: return at == 100 ? 0x6e : 0;
    }
}
} // namespace

// FUN_0048d720 for each picked unit of ours (FUN_00469b90 / FUN_00469c60 /
// FUN_004690a0 / FUN_00469480 / FUN_0046a430 / FUN_00469660: orderable):
// its skill (orderSkill) and the pointer's kind (FUN_0046a950: the
// relation 0 / 1 / 2 / 3 / 4 as 1 / 2 / 4 / 5 / 3) choose (skillAction).
// On an enemy (2 / 4) the target must be one to fight (FUN_0046a6a0) and
// in our sight (FUN_0047e460), else the unit walks there. A use needs its
// MP (FUN_0046c930) and goes out as command 3 with the skill (+6); a walk
// as command 1 (case 0x6e), not to the cell the unit stands on; a skill at
// a cell (case 100) as command 3 with the caster for its target.
void TacticsUnits::orderAt(int cx, int cy, int slot) {
    const GameVersion v = ctx_.version;
    const Unit* t = slot >= 0 && slot < kSlots && units_[std::size_t(slot)].used ? &units_[std::size_t(slot)] : nullptr;
    int at = 100;
    if (t) {
        static constexpr int kAt[5] = {1, 2, 4, 5, 3};
        const int r = relation(t->team());
        at = r >= 0 && r < 5 ? kAt[r] : 100;
    }
    for (Unit& u : units_) {
        if (!u.selected || !orderable(u)) continue;
        const int skill = orderSkill(u);
        if (skill < 0) continue;
        const std::uint32_t rec = skillRow(skill);
        int act = skillAction(logic::sdU8(rec + kSkillAim, v), at, t && t->slot == u.slot);
        if (t && (act == 2 || act == 4)) {
            if (!attackable(u, *t)) continue;
            if (!seen(*t)) act = 0x6e;
        }
        if (tactics::traceUnits())
            SDL_Log("[tactics] order: unit %d skill %d (kind %d) at %d -> action %d%s", u.slot, skill,
                    logic::sdU8(rec + kSkillAim, v), at, act, t && !seen(*t) ? " (target unseen)" : "");
        if (act == 100) {                          // the skill at the cell, the caster its own target
            if (u.mp() < logic::sdS16(rec + kSkillMpCost, v)) continue;   // FUN_0046c930
            orderMark(u);
            queueOrder(u, {3, cx, cy, u.slot, skill});
            continue;
        }
        if (act == 0x6e) {
            if (u.cx() == cx && u.cy() == cy) continue;
            orderMark(u);
            queueOrder(u, {1, cx, cy, -1});
            continue;
        }
        if (act == 0 || !t) continue;
        if (u.mp() < logic::sdS16(rec + kSkillMpCost, v)) continue;   // FUN_0046c930
        orderMark(u);
        queueOrder(u, {3, t->cx(), t->cy(), t->slot, skill});
    }
}

bool TacticsUnits::anyOwnOnField() const {
    for (const Unit& u : units_) {
        const int kind = u.kind();
        if (u.used && u.onField() && !down(u) && u.team() == viewer_ && !base(u) && !(kind >= 0x19b && kind <= 0x19e)) return true;
    }
    return false;
}

int TacticsUnits::standing(bool own) const {
    int n = 0;
    for (const Unit& u : units_) {
        if (!u.used || !u.onField() || down(u) || u.team() == 7) continue;
        const int kind = u.kind();
        if (structure(u) || base(u) || (kind >= 0x19b && kind <= 0x19e)) continue;   // the buildings and gates
        const int r = relation(u.team());
        if (own ? r == 0 : (r == 1 || r == 2)) ++n;
    }
    return n;
}

// FUN_0046b670 -> FUN_0046cbd0 per unit on the field.
void TacticsUnits::draw(int hover) const {
    static const AnmPlayer::SpriteTemplate kSprite{.color = {0x80, 0x80, 0x80, 0xFF}};
    if (!map_) return;
    const int picked = selectedCount();
    for (const Unit& u : units_) {
        if (!u.used || !u.onField() || u.node == AnmPlayer::kNone || !seen(u)) continue;
        const int x = u.px() - map_->viewX();
        const int y = u.py() - map_->scrollY() + TacticsMap::kViewY;
        if (y < -0x22 || y >= 0x299 || x < -0x40 || x >= 0x441) continue;   // FUN_0046b130
        const int prio = 0x6e8 - y;
        anm_.draw(u.node, ctx_.draw, *ctx_.sprites, kSprite, x, y, std::uint16_t(prio), 0x74c, 900);
        // FUN_0046cbd0 -> FUN_0046d210 while FUN_00469500 (on the field, not
        // appearing, falling or leaving: 1, 0xc..0xe, 0x19): the shadow,
        // common.bin frame DAT_0061878c [size] (+0x503, DAT_00615ead [kind *
        // 2]; size 3 none), centred on the feet, grey at 0xc0, behind every
        // unit at 0x756.
        if (u.state != kAppear && u.state != kAppear2 && (u.state < kCollapse || u.state > kRetreat)) {
            const int size = logic::sdU8(0x615ead + std::uint32_t(u.kind() & 0x1ff) * 2, ctx_.version);
            if (size < 3) {
                const int frame = logic::sdS16(0x61878c + std::uint32_t(size) * 2, ctx_.version);
                ctx_.draw.sprite(SpriteCmd{.bank = kHudBank, .frame = static_cast<std::uint16_t>(frame), .x = x, .y = y,
                                           .flags = std::uint16_t(kSprBank | kSprCentre), .blend = kBlendAlpha,
                                           .color = {0x80, 0x80, 0x80, 0xc0}, .priority = 0x756});
            }
        }
        for (std::size_t k = 0; k < u.fx.size(); ++k)
            if (u.fxOn[k]) anm_.draw(u.fx[k], ctx_.draw, *ctx_.sprites, kSprite, x, y + (k == 5 ? u.fxDy : 0), std::uint16_t(prio - 1));
        drawDot(u);
        // The gauge: always for the own side and allies, for others while
        // hovered or selected.
        const int rel = relation(u.team());
        if (rel == 0 || rel == 4 || u.slot == hover || u.selected) drawGauge(u, x, y, prio);
        drawHold(u, x, y);
        // FUN_0046cbd0's tail: the one picked unit on the field shows the
        // reach of the skill it would use (FUN_0046ca60: the armed one, else
        // the job's, which the original reads from slot 0's job) on the map
        // (FUN_004ddc60 -> FUN_0043c530).
        if (picked == 1 && u.selected && u.data.u8(0xf) == 1) {
            int skill = jobSkill(units_[0].used ? units_[0] : u);
            if (u.data.u8(0x9d) != 0) {
                const int armed = u.data.u16(0x84 + std::uint32_t(u.data.u8(0x9e)) * 2);
                if (armed != 0 && !(armed & 0x8000)) skill = armed;
            }
            map_->drawRange(u.cx(), u.cy(), skillRange(u, skill));
        }
        // FUN_00472860: the ring over a selected own unit.
        if (u.selected && u.team() == viewer_ && marker_ != AnmPlayer::kNone)
            anm_.draw(marker_, ctx_.draw, *ctx_.sprites, kSprite, x, y, 0x352);
    }
    // FUN_00466910: the script's effects.
    for (std::size_t i = 0; i < scriptFx_.size(); ++i) {
        const ScriptEffect& f = scriptFx_[i];
        if (!f.on) continue;
        int x = f.x, y = f.y;
        if (f.mode == 0) { x -= map_->viewX(); y += TacticsMap::kViewY - map_->scrollY(); }
        else if (f.mode == 2 && f.unit >= 0 && f.unit < kSlots) {
            const Unit& t = units_[std::size_t(f.unit)];
            x = t.px() - map_->viewX();
            y = t.py() - map_->scrollY() + TacticsMap::kViewY;
        }
        anm_.draw(f.node, ctx_.draw, *ctx_.sprites, kSprite, x, y, std::uint16_t(i < 5 ? 0x370 : 100));
    }
    // FUN_0046b670 -> FUN_00472a70(.., 800): the ground mark.
    if (groundX_ >= 0 && groundMark_ != AnmPlayer::kNone)
        anm_.draw(groundMark_, ctx_.draw, *ctx_.sprites, kSprite, groundX_ - map_->viewX(),
                  groundY_ - map_->scrollY() + TacticsMap::kViewY, 800);
    drawCallRing();                                  // FUN_0046b670 -> FUN_00482150
    // FUN_0046b670 -> FUN_00493b80: the flares at their place on the screen.
    const auto layer = ctx_.draw.setLayer(1);         // on the minimap: the HUD's
    for (const Flare& f : flares_)
        if (f.on) anm_.draw(f.node, ctx_.draw, *ctx_.sprites, kSprite, f.x, f.y, 0xc4);
    ctx_.draw.setLayer(layer);
    drawPopups();
}

// FUN_00440fe0, from FUN_0046cbd0 for every unit drawn: a 3 x 3 dot on the
// minimap, ours cyan when selected and green otherwise, the enemies'
// magenta, the rest yellow.
void TacticsUnits::drawDot(const Unit& u) const {
    if (!map_ || !map_->hasMinimap()) return;
    static constexpr std::uint32_t kColours[] = {0xff00ffffu, 0xff00ff00u, 0xffc8c83cu, 0xffff00ffu};
    const int rel = relation(u.team());
    const int c = rel == 0 ? (u.selected ? 0 : 1) : rel == 1 ? 3 : 2;
    const int mapW = map_->cellsW() * TacticsMap::kCellW, mapH = map_->cellsH() * TacticsMap::kCellH;
    const int x = TacticsMap::kMiniX + u.px() * map_->miniW() / mapW, y = TacticsMap::kMiniY + u.py() * map_->miniH() / mapH;
    const auto layer = ctx_.draw.setLayer(1);         // the HUD's (the wide view)
    ysub::fill(ctx_, x - 1, y - 1, 3, 3, 0xc6, kColours[c]);
    ctx_.draw.setLayer(layer);
}

// FUN_0046d340: the gauge under a unit, on a dark frame.
void TacticsUnits::drawGauge(const Unit& u, int x, int y, int prio) const {
    const int maxHp = std::max<int>(u.maxHp(), 1);
    const int hpW = u.hp() * 100 / maxHp * 0x28 / 100;
    if (u.team() != viewer_) {                     // FUN_0046a430: not the player's
        static constexpr std::uint32_t kColours[] = {0xffff0000u, 0xffff4040u, 0xff8040ffu, 0xffffff80u, 0xff80ff40u};
        const int rel = relation(u.team());
        const std::uint32_t colour = rel >= 0 && rel < 5 ? kColours[rel] : kColours[0];
        ysub::fill(ctx_, x - 0x14, y + 1, hpW, 5, prio, colour);
        ysub::fill(ctx_, x - 0x16, y, 0x2c, 7, prio, 0xff202020u);
        return;
    }
    // The player's: HP (red) and MP (green), 2 px each, then 1 px of cyan:
    // the recovery left (+0x10 of +0x12), else the cast done (+0xc of +0xe).
    const int maxMp = std::max<int>(u.data.s16(0x1a), 1);
    ysub::fill(ctx_, x - 0x14, y + 1, hpW, 2, prio, 0xffff0000u);
    ysub::fill(ctx_, x - 0x14, y + 4, u.mp() * 100 / maxMp * 0x28 / 100, 2, prio, 0xff00ff00u);
    if (u.recover >= 1) {
        const int pct = u.recoverFull == 0 ? 0 : u.recover * 100 / u.recoverFull;
        ysub::fill(ctx_, x - 0x14, y + 7, pct * 0x28 / 100, 1, prio, 0xff00ffffu);
    } else if (u.cast > 0) {
        const int pct = u.castFull == 0 ? 0 : u.cast * 100 / u.castFull;
        ysub::fill(ctx_, x - 0x14, y + 7, 0x28 - pct * 0x28 / 100, 1, prio, 0xff00ffffu);
    }
    ysub::fill(ctx_, x - 0x16, y, 0x2c, 9, prio, 0xff202020u);
}

// ---------------------------------------------------------------- HUD ---
namespace {
constexpr std::uint32_t kFacePlaces = 0x617ef0;     // {s16 x, y} x 20: the roster
constexpr int kFaceW = 0x31, kFaceH = 0x2f;

using ysub::outline;   // FUN_00410680
} // namespace

void TacticsUnits::addFace(Unit& u) {
    if (u.face >= 0 || faces_ >= 0x14) return;
    u.face = static_cast<std::int8_t>(faces_++);
}

void TacticsUnits::drawFace(const Unit& u, int x, int y, int prio, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                            std::uint8_t a) const {
    SpriteCmd c{.bank = kHudBank, .frame = static_cast<std::uint16_t>(u.kind() + 0x1e), .x = x, .y = y,
                .flags = kSprBank, .blend = kBlendAlpha, .color = {r, g, b, a},
                .priority = static_cast<std::uint16_t>(prio)};
    ctx_.draw.sprite(c);
}

// FUN_00472bf0 (FUN_00472b30 style 0, FUN_00472b90 style 1): common.bin
// frame 0x14, each digit's rectangle from DAT_00618810 [style * 10 +
// digit] {x, y, w, h}, 7 px apart; the colour halved, its alpha as given.
void TacticsUnits::drawNumber(int x, int y, int prio, int value, int digits, std::uint32_t argb, int style) const {
    const GameVersion v = ctx_.version;
    value = std::clamp(value, 0, 999999);
    const bool zeros = digits < 0;
    digits = std::min(std::abs(digits), 6);
    const std::uint8_t r = std::uint8_t((argb >> 16 & 0xff) / 2), g = std::uint8_t((argb >> 8 & 0xff) / 2),
                       b = std::uint8_t((argb & 0xff) / 2), a = std::uint8_t(argb >> 24);
    bool seen = false;
    for (int i = 0; i < digits; ++i) {
        int p = 1;
        for (int k = 1; k < digits - i; ++k) p *= 10;
        const int d = value / p % 10;
        seen = seen || d != 0;
        if (!zeros && !seen && i != digits - 1) continue;
        const std::uint32_t e = 0x618810 + std::uint32_t(style) * 0xa0 + std::uint32_t(d) * 0x10;
        const auto f = [&](std::uint32_t o) { return std::int16_t(logic::sdS32(e + o, v)); };
        SpriteCmd c{.bank = kHudBank, .frame = 0x14, .x = x + i * 7, .y = y, .w = f(8), .h = f(0xc),
                    .src = {f(0), f(4), std::int16_t(f(0) + f(8)), std::int16_t(f(4) + f(0xc))},
                    .flags = std::uint16_t(kSprBank | kSprSrcRect), .blend = a == 0xff ? kBlendTest : kBlendAlpha,
                    .color = {r, g, b, a}, .priority = static_cast<std::uint16_t>(prio)};
        ctx_.draw.sprite(c);
    }
}

int TacticsUnits::rosterAt(int x, int y) const {
    const GameVersion v = ctx_.version;
    for (const Unit& u : units_) {
        if (!u.used || u.face < 0 || u.team() != viewer_) continue;
        const int fx = faceX(u.face);
        const int fy = logic::sdS16(kFacePlaces + std::uint32_t(u.face) * 4 + 2, v);
        if (x >= fx && x < fx + kFaceW && y >= fy && y < fy + kFaceH + 9) return u.slot;
    }
    return -1;
}

// down() counts every unit off the field, which all the cards are: only
// the collapse and fall states (0xc, 0xd) and the fallen (+0xf 3..) keep out.
bool TacticsUnits::cardPickable(const Unit& u) const {
    return u.used && u.team() == viewer_ && !u.onField() && u.state != kCollapse && u.state != kFall && u.data.u8(0xf) < 3;
}

void TacticsUnits::selectCard(int slot, bool toggle) {
    if (slot < 0 || slot >= kSlots) return;
    Unit& u = units_[std::size_t(slot)];
    if (!cardPickable(u)) return;
    if (!toggle) clearSelection();
    u.selected = toggle ? !u.selected : true;
    trimSelection();
}

// FUN_0046f8b0(.., 1) -> FUN_0046eda0(.., 1, columns): a drag box over the
// roster columns picks every face of ours it touches (FUN_0046fac0: the
// face's 0x32 x 0x38 box; FUN_00468f40 / FUN_0046a430: one we may pick, or
// in the sortie a card), Shift toggling them. The number picked; none
// leaves the box to the map (FUN_0046efa0(1)).
int TacticsUnits::selectFaces(int x0, int y0, int x1, int y1, bool toggle, bool cards) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const GameVersion v = ctx_.version;
    std::vector<Unit*> hit;
    for (Unit& u : units_) {
        if (!u.used || u.face < 0 || u.team() != viewer_) continue;
        const int fx = faceX(u.face);
        const int fy = logic::sdS16(kFacePlaces + std::uint32_t(u.face) * 4 + 2, v);
        if (fx + 0x32 < x0 || fx > x1 || fy + 0x38 < y0 || fy > y1) continue;
        if (cards ? cardPickable(u) : selectable(u)) hit.push_back(&u);
    }
    if (hit.empty()) return 0;
    if (!toggle) clearSelection();
    for (Unit* u : hit) u->selected = toggle ? !u->selected : true;
    trimSelection();
    // FUN_0046ea10 after a drag: each picked unit of ours says a line.
    for (const Unit* u : hit) {
        if (!u->selected) continue;
        const int f = u->data.u8(0xf);
        if (f <= 2) voice(*u, 0x97);
        else if (f == 3) voice(*u, 0x96);
    }
    return int(hit.size());
}

void TacticsUnits::deploySelected(int sx, int sy, int range) {
    if (!map_) return;
    appearGiven_.clear();                          // FUN_0049a310's FUN_0043d4e0
    for (Unit& u : units_) {
        if (!u.used || !u.selected || u.onField()) continue;
        const tactics::Cell c = appearCell(sx, sy, range, u.moveType());
        u.selected = false;
        if (c.first < 0 || busyNow(u)) continue;
        placeAt(u, c);
        if (tactics::traceUnits()) SDL_Log("[tactics] unit %d sorties to %d,%d", u.slot, c.first, c.second);
    }
}

void TacticsUnits::selectSlot(int slot, bool toggle) {
    if (slot < 0 || slot >= kSlots) return;
    if (!toggle) clearSelection();
    Unit& u = units_[std::size_t(slot)];
    if (selectable(u)) u.selected = toggle ? !u.selected : true;
    trimSelection();
}

namespace {
// The condition names for the panel (the original draws the type's text
// sprite, task +0x1009a6 [type]).
} // namespace

// FUN_00472f40 with its last argument 1: the minutes (at most 99) and the
// seconds of `value` seconds, from group `first` (0 the hundreds of
// minutes, 1 the minutes, 2 the seconds), common.bin frame 0x14 cut by
// DAT_00618950 [style] (digit d at +d * 0x10, the colon at +0xa0, the size
// at +8 / +0xc), 7 px apart; the colour halved, blended below alpha 0xff.
void TacticsUnits::drawTimeDigits(int x, int y, int prio, int value, int first, int style, std::uint32_t argb) const {
    const GameVersion v = ctx_.version;
    const std::uint32_t t = 0x618950 + std::uint32_t(style) * 0xe0;
    const auto f = [&](std::uint32_t o) { return std::int16_t(logic::sdS16(t + o, v)); };
    const std::int16_t w = f(8), h = f(0xc);
    const std::uint8_t r = std::uint8_t((argb >> 16 & 0xff) / 2), g = std::uint8_t((argb >> 8 & 0xff) / 2),
                       b = std::uint8_t((argb & 0xff) / 2), a = std::uint8_t(argb >> 24);
    const auto glyph = [&](std::uint32_t at) {
        const std::int16_t sx = f(at), sy = f(at + 4);
        ctx_.draw.sprite(SpriteCmd{.bank = kHudBank, .frame = 0x14, .x = x, .y = y, .w = w, .h = h,
                                   .src = {sx, sy, std::int16_t(sx + w), std::int16_t(sy + h)},
                                   .flags = std::uint16_t(kSprBank | kSprSrcRect), .blend = a == 0xff ? kBlendTest : kBlendAlpha,
                                   .color = {r, g, b, a}, .priority = static_cast<std::uint16_t>(prio)});
        x += 7;
    };
    value = std::max(value, 0);
    const int groups[3] = {value / 6000 % 100, value / 0x3c < 100 ? value / 0x3c % 100 : 99, value % 0x3c};
    for (int k = std::clamp(first, 0, 2); k < 3; ++k) {
        glyph(std::uint32_t(groups[k] / 10) * 0x10);
        glyph(std::uint32_t(groups[k] % 10) * 0x10);
        if (k < 2) glyph(0xa0);
    }
}

void TacticsUnits::drawHud(int shown, bool sortie) const {
    const GameVersion v = ctx_.version;
    // FUN_0046da80 -> FUN_0046db20(.., 0) and FUN_0046e0f0: each of our
    // units' face, dimmed while it waits, red once fallen, outlined while
    // selected; under it HP (red), MP (green) and the cast or recovery
    // (cyan) bars.
    for (const Unit& u : units_) {
        if (!u.used || u.face < 0 || u.team() != viewer_) continue;
        const int x = faceX(u.face);
        const int y = logic::sdS16(kFacePlaces + std::uint32_t(u.face) * 4 + 2, v);
        std::uint8_t r = 0x80, g = 0x80, b = 0x80;
        switch (u.data.u8(0xf)) {
            case 0: case 2:
                r = g = b = u.selected ? 0x60 : 0x40;
                if (u.selected) {
                    outline(ctx_, x, y, x + 0x31, y + 0x2f, 0x1f2, 0xffffff00u);
                    outline(ctx_, x + 1, y + 1, x + 0x30, y + 0x2e, 0x1f2, 0xffffff00u);
                }
                break;
            case 1:
                if (u.selected) {
                    r = g = b = 0x90;
                    outline(ctx_, x, y, x + 0x31, y + 0x2f, 0x1f2, 0xff00ff00u);
                    outline(ctx_, x + 1, y + 1, x + 0x30, y + 0x2e, 0x1f2, 0xff00ff00u);
                }
                break;
            case 3: g = b = 0; break;
            case 4: r = 0x40; g = b = 0; break;
            default: break;
        }
        drawFace(u, x, y, 499, r, g, b);
        const int gy = y + 0x30;
        const auto bar = [&](int value, int full, int dy, int h, std::uint32_t argb) {
            if (full <= 0) return;
            const int w = std::clamp(value * 100 / full * 0x30 / 100, 0, 0x30);
            if (w > 0) ysub::fill(ctx_, x + 1, gy + dy, w, h, 0x1f0, argb);
        };
        bar(u.hp(), u.maxHp(), 1, 2, 0xffff0000u);
        bar(u.mp(), u.data.s16(0x1a), 4, 2, 0xff00ff00u);
        if (u.recover > 0) bar(u.recover, u.recoverFull, 7, 1, 0xff00ffffu);
        else if (u.cast > 0 && u.castFull > 0) bar(u.castFull - u.cast, u.castFull, 7, 1, 0xff00ffffu);
        // FUN_004944a0: a unit in its number group shows the number
        // (FUN_00472b90, the larger digits).
        if (inGroup(u)) drawNumber(x + 0x28, gy - 0x10, 0x1f1, u.group(), 1, 0xffffffffu, 1);
        // FUN_0046db20 -> FUN_0046e3d0 (not during the sortie): a counted
        // time left (+0x24) in minutes and seconds beside the face, right of
        // it in the left column, left of it in the right one (FUN_0046e4c0).
        if (!sortie && u.data.s32(0x24) < 0x7fffffff)
            drawTimeDigits(u.face < 10 ? x + 0x36 : x - 0x28, y + 0x20, 500, u.data.s32(0x24) / 0x3c, 1, 0, 0xffffffffu);
        ysub::fill(ctx_, x, gy, 0x32, 9, 0x1f1, 0xff202020u);
    }

    // FUN_0049cce0 -> FUN_0049cdc0: the panel (frame 3 with a unit, else 4), then
    // FUN_0049d160 for it.
    const Unit* u = shown >= 0 && shown < kSlots && units_[std::size_t(shown)].used ? &units_[std::size_t(shown)] : nullptr;
    ysub::drawSub(ctx_, {.bank = kHudBank, .frame = static_cast<std::uint16_t>(u ? 3 : 4)}, 200, 0, 600);
    if (!u) return;
    constexpr int kPrio = 0xc3, kNum = kPrio - 1;
    constexpr std::uint32_t kWhite = 0xffffffffu;
    const Fighter f = fighter(*u);
    if (u->team() == viewer_) drawFace(*u, 0xd, 0x272, kPrio, 0x80, 0x80, 0x80);
    // FUN_0046a390: all but kinds 0x1b9..0x1bf, 0x1c1, 0x1c2 show the
    // panel's model (FUN_0040a330 at (0x24, 0x2e0)).
    if (const int kind = u->kind();
        panelNode_ != AnmPlayer::kNone && u->slot == panelShown_ && !((kind >= 0x1b9 && kind <= 0x1bf) || kind == 0x1c1 || kind == 0x1c2)) {
        static const AnmPlayer::SpriteTemplate kSprite{.color = {0x80, 0x80, 0x80, 0xFF}};
        anm_.draw(panelNode_, ctx_.draw, *ctx_.sprites, kSprite, 0x24, 0x2e0, kPrio);
    }
    drawGauge(*u, 0x24, 0x2e0, kPrio);
    drawNumber(99, 0x28b, kNum, u->hp(), 3, kWhite);
    drawNumber(0x82, 0x28b, kNum, u->maxHp(), 3, kWhite);
    drawNumber(99, 0x29e, kNum, u->mp(), 3, kWhite);
    drawNumber(0x82, 0x29e, kNum, u->data.s16(0x1a), 3, kWhite);
    // The text sprites (FUN_00408f10, white, 12 px). FUN_00467eb0 renders
    // the four tables once with the 12 px font (+0xdb80c): FUN_004671f0
    // the attack kinds, FUN_00467380 the names, FUN_00467520 the jobs,
    // FUN_004676c0 the condition names (tactics_texts.gen.hpp); each frees
    // its sprites first and on a failure (FUN_00467320, FUN_004674c0,
    // FUN_00467660, FUN_004677f0, also from FUN_00465e60). The port's
    // text commands render them as drawn.
    // the name by kind, centred in the 0x5b pixels from 0x4f.
    const auto label = [&](const std::string& text, int x, int y, bool centred = false) {
        ctx_.draw.text({.x = centred ? x + 0x5b / 2 : x, .y = centred ? y + 6 : y, .priority = kNum, .argb = kWhite,
                        .text = text, .fontPx = 12, .centred = centred});
    };
    // The tables of the running build.
    const auto& unitNames = spriteTexts(ctx_.version, kUnitNames);
    const auto& jobNames = spriteTexts(ctx_.version, kJobNames);
    const auto& conditionNames = spriteTexts(ctx_.version, kConditionNames);
    const auto& attackKinds = spriteTexts(ctx_.version, kAttackKinds);
    label(unitNames[u->kind() & 0x1ff], 0x4f, 0x26f, true);
    // The conditions: the ailment (slot 0) and the blessing (slot 2) by
    // name, with the minutes and seconds left in red and green.
    const auto condition = [&](const Status& s, int y, std::uint32_t argb) {
        label(conditionNames[std::size_t(s.type) & 0xff], 0x54, y);
        drawNumber(0x87, y + 2, kNum, s.time % 0x34bc0 / 0xe10, -2, argb);
        drawNumber(0x9a, y + 2, kNum, s.time % 0xe10 / 0x3c, -2, argb);
    };
    condition(u->status[0], 0x2ca, 0xfff66f6fu);
    condition(u->status[2], 0x2dd, 0xff18ff00u);
    label(jobNames[u->data.u16(0xc) & 0x1ff], 0xc4, 0x26f);
    // FUN_0046a4c0 (one of our fighters): its time left, by the clock icon.
    if (const int k = u->kind(); u->team() == viewer_ && k >= 1 && k <= 0x2d && u->data.s32(0x24) != 0x7fffffff)
        drawTimeDigits(0x157, 0x271, kNum, u->data.s32(0x24) / 0x3c, 1, 0, kWhite);
    if (u->kind() < 0x40) drawNumber(0x11a, 0x271, kNum, u->data.u8(0xe), 2, kWhite);   // the level
    else ctx_.draw.text({.x = 0x11d, .y = 0x26d, .priority = kNum - 1, .argb = kWhite, .text = "-", .fontPx = 12});   // FUN_0040ec50 DAT_005a05a4
    // The attack kind by the job skill's record +5: 2 (magic) always hits
    // (必中 for the accuracy) and shows 魔法, 1 and 4 show 物理.
    const int job = jobSkill(*u);
    const int kind = logic::sdU8(skillRow(job) + kSkillType, v);
    if (kind == 2) label(attackKinds[2], 0xd1, 0x28a);
    else drawNumber(0xd3, 0x28b, kNum, f.acc, 3, kWhite);
    if (kind == 1 || kind == 4) label(attackKinds[0], 0x10e, 0x289);
    else if (kind == 2) label(attackKinds[1], 0x10e, 0x289);
    label(conditionNames[u->data.u8(0x45)], 0xcf, 0x2b0);   // the added effect
    drawNumber(0xd3, 0x29e, kNum, f.atk, 3, kWhite);
    drawNumber(0x106, 0x29e, kNum, f.atk + f.spread, 3, kWhite);
    drawNumber(0xd3, 0x2cc, kNum, u->data.u8(0x37), 3, kWhite);     // reach
    drawNumber(0xd3, 0x2df, kNum, u->data.u8(0x44), 3, kWhite);     // area
    drawNumber(0x10c, 0x2df, kNum, u->data.u16(0x34), 4, kWhite);   // recovery
    drawNumber(0x155, 0x28b, kNum, f.evade, 3, kWhite);
    drawNumber(0x155, 0x29d, kNum, f.guard, 3, kWhite);
    drawNumber(0x150, 0x2b1, kNum, f.sresist, 3, kWhite);
    drawNumber(0x155, 0x2cc, kNum, f.mguard, 3, kWhite);
    drawNumber(0x150, 0x2df, kNum, f.resist, 3, kWhite);
    // FUN_0049da20 (0, 1): the grade letters (common.bin frame 0xb + grade)
    // for the derived +0x18 and +0x1e, the grade the first of eight
    // DAT_006e47e8 [kind * 8] bounds the value is within; then
    // FUN_0049d9e0, which draws nothing.
    for (int k = 0; k < 2; ++k) {
        const int value = u->data.s16(k == 0 ? 0x18 : 0x1e);
        int grade = -1;
        for (int i = 0; i < 8 && grade < 0; ++i)
            if (value <= logic::sdS16(0x6e47e8 + std::uint32_t(k) * 0x10 + std::uint32_t(i) * 2, v)) grade = i;
        if (grade < 0) continue;
        ctx_.draw.sprite(SpriteCmd{.bank = kHudBank, .frame = static_cast<std::uint16_t>(grade + 0xb), .x = 0x9a,
                                   .y = k == 0 ? 0x288 : 0x29b, .flags = kSprBank, .priority = kNum});
    }
}

// ------------------------------------------------- the hit reactions ---

// The recovery clip of the unit's job (DAT_00611538 [kind + 100] +2), the
// clip states 8, 9 recover with.
int TacticsUnits::recoveryClip(const Unit& u) const {
    return sdS8(kSkillAnims + (std::uint32_t(u.data.u16(0xc)) + 100) * 0x10 + 2, ctx_.version);
}

// FUN_0043c0c0's count: another unit stands on the cell (FUN_0047e320
// finds it in the on-field list, for the walk FUN_0047dec0). The original
// keeps the count in the cell overlay (+1 of each u16), FUN_0043c470 adding
// and taking one as units are placed, move (FUN_00468690, FUN_0046b760)
// and leave (FUN_0046ba10), each time after FUN_0046b2c0 keeps the last
// cell (+0x4e9) and takes the new one (+0x4e7) from the position; the port
// counts the units instead.
bool TacticsUnits::othersOn(int cx, int cy, const Unit& u) const {
    for (const Unit& o : units_)
        if (o.used && o.onField() && &o != &u && o.cx() == cx && o.cy() == cy) return true;
    return false;
}

// FUN_0046c120: turn toward a map point (pixels), by the angle in 23-degree
// steps (DAT_00618768); the buildings and gates show their side instead.
void TacticsUnits::faceToward(Unit& u, int px, int py) const {
    const float deg = degrees(py - u.py(), px - u.px());
    const int angle = static_cast<int>(deg) % 360;
    face(u, logic::sdU8(0x618768 + std::uint32_t(angle / 0x17), ctx_.version));
}

// FUN_004753f0: keep the state the reaction breaks into, once.
void TacticsUnits::saveState(Unit& u) {
    if (u.saved) return;
    u.saved = true;
    u.savedState = u.state;
    u.savedSub = u.sub;
}

// FUN_00475460: back to the kept state: a skill (6) only while it has one
// and had not got past its strike (+0x293 < 0x17), a flight (0x1d) only
// with a target; else the AI chooses (0xf).
void TacticsUnits::resumeState(Unit& u) {
    if (!u.saved) {                                // the original asserts here
        u.next = kChoose;
        return;
    }
    u.saved = false;
    int s = u.savedState;
    if (s == 6 && (u.skill < 0 || u.savedSub >= 0x17)) s = 0xf;
    if (s == 0x1d && u.target < 0) s = 0xf;
    if (s == 0 || s > 0x1d) s = 0xf;               // asserts in the original
    u.next = static_cast<std::uint8_t>(s);
}

// FUN_004919f0 cases 2 and 3, past the damage (FUN_00491ef0): not for the
// gates (FUN_00469b90) or bases, nor a steady target (FUN_004692e0).
// Result 2 staggers (state 8), 3 knocks back (9): away from the attacker,
// from the skill's cell (record +0x18 4), or not at all (a learnt skill of
// +0x18 0 or 5, or a job whose +0x21 is 100). On maps 7 and 0x4c kind 6 of
// team 4 is never knocked back. The interrupted state is kept unless the
// unit was already reacting (FUN_00469730).
void TacticsUnits::react(Unit& from, Unit& to, int result, int skill, tactics::Cell at) {
    const GameVersion v = ctx_.version;
    if (result != 2 && result != 3) return;
    if (down(from) || down(to)) return;
    const int kind = to.kind();
    if ((kind >= 0x19b && kind <= 0x19e) || base(to)) return;
    if (result == 3 && (mapId_ == 7 || mapId_ == 0x4c) && kind == 6 && to.team() == 4) return;
    if (steadyNow(to)) return;
    if (!reacting(to)) saveState(to);
    to.path.clear();
    if (result == 3) {
        if (logic::sdU8(0x6b2da9 + std::uint32_t(to.data.u16(0xc)) * 0x40, v) == 100) {
            to.knockMode = -1;
        } else if (skill > 0 && skill <= 100) {     // FUN_0046c9f0: a learnt skill
            switch (logic::sdU8(skillRow(skill) + kSkillAim, v)) {
                case 1: case 2: case 3: case 6: to.knockMode = 1; break;
                case 4: to.knockMode = 0; to.knockFrom = at; break;
                default: to.knockMode = -1; break;
            }
        } else {
            to.knockMode = 1;
        }
    }
    to.next = static_cast<std::uint8_t>(result == 2 ? 8 : 9);
    to.hitBy = static_cast<std::int16_t>(from.slot);
}

// FUN_00478850, state 8: the stagger. Facing the attacker it plays hit
// clip 8 or 9 (by the battle's generator), steady until frame 0x2d, then
// its job's recovery clip for a second, stand, and back to what it was
// doing (FUN_00475460).
void TacticsUnits::staggerState(Unit& u) {
    const Unit* by = u.hitBy >= 0 && u.hitBy < kSlots && units_[std::size_t(u.hitBy)].used &&
                             units_[std::size_t(u.hitBy)].onField()
                         ? &units_[std::size_t(u.hitBy)] : nullptr;
    switch (u.sub) {
        case 0:
            setFlags(u, true);
            if (by) faceToward(u, by->px(), by->py());
            playAnim(u, (rand() & 1) == 0 ? 9 : 8);
            u.castFull = u.cast = 0;
            u.timer = 0;
            u.recoverFull = 0x3c;
            u.recover = u.recoverFull;
            ++u.sub;
            break;
        case 1:
            --u.recover;
            if (u.timer > 0x2d) {
                u.steady = false;
                playAnim(u, recoveryClip(u));
                ++u.sub;
            }
            break;
        case 2:
            if (--u.recover < 1) {
                playAnim(u, 2);
                u.recover = 0;
                ++u.sub;
            }
            break;
        case 3:
            resumeState(u);
            u.busy = false;
            break;
        default: break;
    }
}

// FUN_00478bc0, state 9: the knock-back. As the stagger, with clip 8 and
// a second and a half to recover; for its first 0x20 frames the unit slides
// away (from the attacker or the skill's cell) by DAT_00618cf8's speeds
// (12, 8, 3, 2, -1 pixels a frame across, half that down), only onto cells
// it may stand on and no one else holds.
void TacticsUnits::knockState(Unit& u) {
    const GameVersion v = ctx_.version;
    const Unit* by = u.hitBy >= 0 && u.hitBy < kSlots && units_[std::size_t(u.hitBy)].used &&
                             units_[std::size_t(u.hitBy)].onField()
                         ? &units_[std::size_t(u.hitBy)] : nullptr;
    switch (u.sub) {
        case 0:
            setFlags(u, true);
            if (by) faceToward(u, by->px(), by->py());
            playAnim(u, 8);
            u.castFull = u.cast = 0;
            u.timer = 0;
            u.recoverFull = 0x5a;
            u.recover = u.recoverFull;
            ++u.sub;
            break;
        case 1: {
            if (u.timer < 0x20 && u.knockMode != -1 && map_ && (u.knockMode == 0 || by)) {
                const int dx = u.knockMode == 0 ? u.px() - u.knockFrom.first * TacticsMap::kCellW : (u.x - by->x) >> 16;
                const int dy = u.knockMode == 0 ? u.py() - u.knockFrom.second * TacticsMap::kCellH : (u.y - by->y) >> 16;
                const float len = std::sqrt(float(dx * dx + dy * dy));
                if (len > 0.0f) {
                    const int speed = logic::sdS16(0x618cf8 + std::uint32_t(u.timer) * 2, v);
                    const int sx = static_cast<int>(float(dx) / len * float(speed << 16));
                    const int sy = static_cast<int>(float(dy) / len * float(speed / 2 << 16));
                    const int ncx = ((u.x + sx) >> 16) / TacticsMap::kCellW, ncy = ((u.y + sy) >> 16) / TacticsMap::kCellH;
                    if (ncx > 0 && ncy > 0 && map_->moveRate(ncx, ncy, u.moveType()) != 0 && !othersOn(ncx, ncy, u)) {
                        u.x += sx;
                        u.y += sy;
                    }
                }
            }
            --u.recover;
            if (u.timer > 0x3c) {
                u.steady = false;
                playAnim(u, recoveryClip(u));
                ++u.sub;
            }
            break;
        }
        case 2:
            if (--u.recover < 1) {
                playAnim(u, 2);
                u.recover = 0;
                ++u.sub;
            }
            break;
        case 3:
            resumeState(u);
            u.busy = false;
            break;
        default: break;
    }
}

// FUN_00479270, state 0xa: steady, the clip DAT_00613b78 [job skill] +2
// names, then the AI chooses. Nothing in ch.exe enters 0xa, and its test
// for the attacker reads a null unit, so it never turns to face anyone.
void TacticsUnits::guardHitState(Unit& u) {
    switch (u.sub) {
        case 0:
            u.steady = true;
            playAnim(u, logic::sdS16(0x613b78 + std::uint32_t(std::max(jobSkill(u), 0)) * 6 + 2, ctx_.version));
            ++u.sub;
            break;
        case 1:
            if (u.animEnd) ++u.sub;
            break;
        case 2:
            u.aiControlled = false;
            u.next = kChoose;
            break;
        default: break;
    }
}

// FUN_00479820, state 0xc: the collapse. Steady and busy, the clip
// DAT_006149d0 [kind] +1, the stamina (+0x24) gone; after the clip and 70
// frames the unit leaves the field for good (+0xf 4).
void TacticsUnits::collapseState(Unit& u) {
    switch (u.sub) {
        case 0:
            setFlags(u, true);
            ++u.sub;
            playAnim(u, sdS8(kFallAnims + std::uint32_t(u.data.u16(0xc)) * 10 + 1, ctx_.version));
            u.data.set32(0x24, 0);
            voice(u, 0x70);
            break;
        case 1:
            if (u.animEnd) {
                ++u.sub;
                u.timer = 0;
            }
            break;
        case 2:
            if (u.timer > 0x46) {
                setFlags(u, false);
                setState(u, kOffField);
                leaveField(u);
                u.data.set8(0xf, 4);
            }
            break;
        default: break;
    }
}

// Infinite unit time is for single play: all players must count alike.
bool TacticsUnits::timeLimited() const { return network_ || !options::port().infiniteUnitTime; }

// FUN_0046cb10: one of the unit's eight learnt skills (+0x84) is `skill`.
bool TacticsUnits::hasLearnt(const Unit& u, int skill) {
    for (std::uint32_t i = 0; i < 8; ++i) {
        const int s = u.data.u16(0x84 + i * 2);
        if ((s & 0x8000) == 0 && s == skill) return true;
    }
    return false;
}

// FUN_0046bc20: a party member (FUN_0046a0a0: FUN_00468eb0 after the
// slot-in-use assert) whose time left is counted (+0x20 not 0x7fffffff) loses
// a frame of it (+0x24), and collapses (0xc) as it reaches 0. Only the disc
// builds count (TW 0x46bc7b, JP 0x435f5c); the later build the decompile comes
// from has dropped the decrement.
void TacticsUnits::checkCollapse(Unit& u) {
    const int kind = u.kind();
    if (!u.human() || kind < 1 || kind > 0x2d) return;
    if (u.data.s32(0x20) == 0x7fffffff || u.data.s32(0x24) < 0) return;
    if (!timeLimited() && u.data.s32(0x24) > 0) return;
    if (u.data.s32(0x24) > 0) u.data.set32(0x24, u.data.s32(0x24) - 1);
    if (u.data.s32(0x24) != 0 || u.state == kCollapse || u.state == kFall) return;
    setState(u, kCollapse);
}

// FUN_00479410, state 0xe: the retreat. A player's unit with no other
// fighting member (FUN_00468e10) of its team left on the field gives up at
// once. Else three seconds' cast (2 frames with learnt skill 0x3e) with
// effect 0x7fc, then the job's clip DAT_006149d0 [kind] +2; 70 frames on,
// with stamina past 0xe10 the unit leaves to wait (+0xf 2), else the AI
// takes over.
void TacticsUnits::retreatState(Unit& u) {
    if (u.human()) {
        int others = 0;
        for (const Unit& o : units_) {
            const int k = o.kind();
            if (o.used && o.team() == u.team() && k >= 1 && k <= 0x2d && !down(o) && o.slot != u.slot) ++others;
        }
        if (others == 0) {
            playEffect(u, 1, -1);
            u.next = kStand;
            setFlags(u, false);
            u.cast = u.recover = 0;
            return;
        }
    }
    switch (u.sub) {
        case 0: {
            u.busy = true;
            u.castFull = 0xb4;
            ++u.sub;
            playAnim(u, 0xc);
            u.cast = u.castFull;
            if (hasLearnt(u, 0x3e)) u.cast = 2;
            playEffect(u, 1, 0x7fc);
            voice(u, 0x72);
            break;
        }
        case 1:
            if (--u.cast < 1) {
                u.steady = true;
                u.cast = 0;
                playAnim(u, sdS8(kFallAnims + std::uint32_t(u.data.u16(0xc)) * 10 + 2, ctx_.version));
                playEffect(u, 1, -1);
                u.recover = u.recoverFull;
                ++u.sub;
            }
            break;
        case 2:
            if (u.animEnd) {
                u.timer = 0;
                ++u.sub;
            }
            break;
        case 3:
            if (u.timer > 0x46) {
                setFlags(u, false);
                if (u.data.s32(0x24) > 0xe10 || !timeLimited()) {
                    setState(u, kOffField);
                    leaveField(u);
                    u.data.set8(0xf, 2);
                    // The disc builds' cost of a retreat (TW 0x4797bd, JP 0x43f55a):
                    // a minute of the time left, at least 0.
                    if (timeLimited())
                        u.data.set32(0x24, std::max(u.data.s32(0x24) - 0xe10, 0));
                    return;
                }
                u.next = kChoose;
                u.cast = u.recover = 0;
            }
            break;
        default: break;
    }
}

// FUN_00475f60, state 0x11: the call. A cast as long as DAT_00618040 [the
// number called] (180 frames for one, 60 more each) with effect 0x80e,
// the calling clip with 0x812, then FUN_004810d0 brings the list in and
// the unit recovers.
void TacticsUnits::callState(Unit& u) {
    const GameVersion v = ctx_.version;
    switch (u.sub) {
        case 0: {
            u.busy = true;
            const int n = std::min<int>(int(u.callList.size()), 0x14);
            u.castFull = logic::sdS16(0x618040 + std::uint32_t(n) * 2, v);
            ++u.sub;
            playAnim(u, 0xc);
            u.cast = u.castFull;
            playEffect(u, 1, 0x80e);
            break;
        }
        case 1:
            if (--u.cast < 1) {
                u.steady = true;
                u.cast = 0;
                ++u.sub;
                playAnim(u, 0x11);
                playEffect(u, 1, -1);
                playEffect(u, 1, 0x812);
                u.recover = u.recoverFull;
                voice(u, 0x75);
            }
            break;
        case 2:
            if (u.animEnd) {
                ++u.sub;
                playAnim(u, 0xf);
                u.recover = u.recoverFull;
                u.steady = false;
                callIn(u);
            }
            break;
        case 3:
            if (--u.recover < 1) {
                u.recover = 0;
                ++u.sub;
                u.busy = false;
            }
            break;
        case 4:
            u.next = kStand;
            break;
        default: break;
    }
}

// FUN_004810d0: each listed party member waiting off the field comes out on
// the free cell nearest the caller within its sight (+0x2c), as a
// placement (message 4 -> FUN_0048e790).
void TacticsUnits::callIn(Unit& u) {
    if (!map_) return;
    appearGiven_.clear();                          // FUN_0043d4e0
    for (const std::int16_t slot : u.callList) {
        if (slot < 0 || slot >= kSlots) continue;
        Unit& o = units_[std::size_t(slot)];
        if (!o.human() || !callable(o) || busyNow(o)) continue;   // FUN_00468d80, FUN_0046a120
        // FUN_0043d660 with the caller's sight and move type.
        const tactics::Cell c = appearCell(u.cx(), u.cy(), u.data.u8(0x2c), u.moveType());
        if (c.first >= 0) placeAt(o, c);
    }
}

// FUN_0048e790's placement.
void TacticsUnits::placeAt(Unit& u, tactics::Cell c) {
    setFlags(u, true);                             // until the appear clip is over
    setPosition(u, c.first, c.second);
    setState(u, kAppear);
    u.data.set8(0xf, 1);
}

// FUN_0048e990: message 5.
bool TacticsUnits::retreat(int slot) {
    if (slot < 0 || slot >= kSlots || !units_[std::size_t(slot)].used) return false;
    Unit& u = units_[std::size_t(slot)];
    if (busyNow(u)) return false;
    setState(u, kRetreat);
    return true;
}

// FUN_0048ea40: message 10 (no busy test).
bool TacticsUnits::collapse(int slot) {
    if (slot < 0 || slot >= kSlots || !units_[std::size_t(slot)].used) return false;
    setState(units_[std::size_t(slot)], 0xc);
    return true;
}

// FUN_0048ea90: message 11 with the list. The original packs eight ids a
// command and sends the rest of a long list as commands 0xc, which
// FUN_0048e4d0 (finding each with FUN_0048e0a0) appends through
// FUN_0048ebf0; here the whole list comes at once.
bool TacticsUnits::callParty(int slot, const std::vector<std::int16_t>& list) {
    if (slot < 0 || slot >= kSlots || !units_[std::size_t(slot)].used) return false;
    Unit& u = units_[std::size_t(slot)];
    if (busyNow(u)) return false;
    u.callList.assign(list.begin(), list.begin() + std::min<std::size_t>(list.size(), 0x14));
    setState(u, kCall);
    return true;
}

std::string TacticsUnits::name(const Unit& u) const {
    return spriteTexts(ctx_.version, kUnitNames)[u.kind() & 0x1ff];
}

} // namespace ch

