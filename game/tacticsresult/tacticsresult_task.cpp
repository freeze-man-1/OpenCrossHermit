// ============================================================================
// tacticsresult_task.cpp
// ============================================================================
#include "tacticsresult_task.hpp"
#include "../state/game_logic.hpp"

#include <algorithm>
#include <ctime>

namespace ch {

using ysub::drawRect;
using ysub::fill;

namespace {
constexpr std::uint32_t kEvent = 0x7a5294, kStage = 0x7a5296, kPoints = 0x7a511c;
constexpr std::uint32_t kItemState = 0x7aaca8;            // u16 by item id
constexpr std::uint32_t kItems = 0x6d5118;                // 0x38-byte item records
constexpr std::uint32_t sortieRow(int s) { return std::uint32_t(s) * 0x70; }
constexpr std::uint32_t event(int e) { return 0x73bed0 + std::uint32_t(e) * 0x100; }
// Rect table DAT_0061c320 (16-byte records).
enum : std::uint32_t {
    kCursor = 0x61c320, kBack = 0x61c330, kCardFont = 0x61c340, kFont = 0x61c350, kBigFont = 0x61c360,
    kRankPlate = 0x61c370, kTurnPlate = 0x61c380, kGoalPlate = 0x61c390, kNextLit = 0x61c3a0,
    kName = 0x61c3b0, kFace = 0x61c3c0, kItemIcon = 0x61c3d0, kCardLabel = 0x61c3e0,
    kCardComma = 0x61c3f0, kComma = 0x61c400, kBigComma = 0x61c410,
};
constexpr std::uint32_t kShareSteps = 0x61c50a;           // s16[10]: damage share thresholds, %
constexpr std::uint32_t kJobRate = 0x61c508;              // s16[type * 11 + step]: exp %, by job type
constexpr std::uint32_t kStatShare = 0x61c58c;            // s16[type * 7 + stat]: exp split, %
} // namespace

TacticsResultTask::~TacticsResultTask() {
    ctx_.sprites->unloadBank(kBank);
    ctx_.audio->bgmStop();                                 // FUN_004db120
}

ysub::Rect TacticsResultTask::rect(std::uint32_t va) const { return ysub::rectAt(va, ctx_.version); }

void TacticsResultTask::number(int x, int y, int offX, int offY, int digits, int prio, std::uint32_t value,
                               int pitch, int gap, int groupGap, int flags, std::uint32_t font,
                               std::uint32_t comma) {
    ysub::drawCommaNumber(ctx_, rect(font), rect(comma), x, y, offX, offY, digits, prio, value, pitch, gap,
                          groupGap, flags);
}

// ------------------------------------------------------------------ start ---
Co<> TacticsResultTask::start(std::uint32_t) {
    init();
    if (!skip_) co_await screen();
    co_await yieldFrame();
    ctx_.scenes.request(SceneId::Preparation);
}

// FUN_004baa90
void TacticsResultTask::init() {
    GameWork& w = ctx_.work;
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\TacticsResult\\TacticsResult.bin"));
    if (w.u8(stage::kNetwork) == 1 && w.u8(stage::kNetSaveUnits) == 0) { skip_ = true; return; }
    // FUN_004bbcc0
    eventRecords();
    itemDrops();
    expAndPoints();
    skip_ = w.month() == 0xf && w.week() == 4;
}

// FUN_004bd210: keep the event's best rank; event 0x25 opens a few of
// seven skills for random members who lack them (more for a better rank).
void TacticsResultTask::eventRecords() {
    GameWork& w = ctx_.work;
    const int ev = w.s16(kEvent);
    const int rank = std::clamp<int>(w.s16(stage::kRank), 1, 5);
    const int s = w.s16(kStage) - 1;
    const std::uint32_t best = 0x7a54ca + std::uint32_t(ev);
    if (rank < w.u8(best)) w.set8(best, rank);
    if (ev != 0x25) return;
    static constexpr std::array<int, 6> kRolls{0, 10, 5, 3, 1, 0};
    static constexpr std::array<int, 7> kSkills{0xd, 0x4f, 0x52, 0x51, 0x50, 0x53, 0x54};
    std::array<std::array<std::int16_t, 0x1e>, 7> lacking{};
    std::array<int, 7> count{};
    const auto skillState = [&](int unit, int skill) {
        return GameWork::unitRecord(unit) + 0xb8 + std::uint32_t(skill - 1) * 0xc;
    };
    for (int c = 0; c < w.s16(0x7a52f8 + sortieRow(s)); ++c) {
        const int unit = w.s16(0x7a52d0 + std::uint32_t(c) * 2 + sortieRow(s));
        for (std::size_t k = 0; k < 7; ++k)
            if (w.u8(skillState(unit, kSkills[k])) == 0 && count[k] < 0x1e)
                lacking[k][std::size_t(count[k]++)] = static_cast<std::int16_t>(unit);
    }
    logic::srand(std::uint32_t(std::time(nullptr)));
    for (int i = 0; i < kRolls[std::size_t(rank)]; ++i) {
        const std::size_t k = std::size_t(logic::rand() % 7);
        if (count[k] == 0) continue;
        const int unit = lacking[k][std::size_t(logic::rand() % count[k])];
        if (w.u8(skillState(unit, kSkills[k])) == 0) w.set8(skillState(unit, kSkills[k]), 2);
    }
}

// FUN_004bca40: the event's fixed rewards, the opened chests, then random
// drops by item category (more for a better rank). A drop the class
// already owns becomes two more rolls in the next category down.
void TacticsResultTask::itemDrops() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const std::uint32_t ev = event(w.s16(kEvent));
    const int rank = std::max(w.s16(stage::kRank) - 1, 0);
    constexpr int kCats = 10;
    const auto category = [&](int item) { return int(logic::sdS16(kItems + 8 + std::uint32_t(item) * 0x38, v)); };
    const auto owned = [&](int item) { return (w.u16At(kItemState, item) & 1) != 0; };
    std::array<std::vector<std::int16_t>, kCats> pool;     // every item by category
    std::array<std::array<std::int16_t, 10>, kCats> got{};
    std::array<int, kCats> gotCount{};
    std::array<int, kCats + 2> rolls{};                    // rolls[c + 1]: random drops of category c
    for (int i = 1; i <= 0x168; ++i) {
        const int c = category(i);
        if (c > 0 && c < kCats) pool[std::size_t(c)].push_back(static_cast<std::int16_t>(i));
    }
    for (int s = 0; s < 7; ++s)
        rolls[std::size_t(8 - s)] = logic::sdS16(ev + 0x4a + std::uint32_t(rank * 7 + s) * 2, v);
    const auto give = [&](int item) {
        const int c = category(item);
        if (c <= 0 || c >= kCats || gotCount[std::size_t(c)] >= 10) return;
        got[std::size_t(c)][std::size_t(gotCount[std::size_t(c)]++)] = static_cast<std::int16_t>(item);
        const std::uint32_t at = kItemState + std::uint32_t(item) * 2;
        w.set16(at, ((w.u16(at) | 1) & 0xf0ff) | 0x100);
    };
    for (std::uint32_t k = 0; k < 8; ++k) {
        const int item = logic::sdS16(ev + 0x90 + k * 2, v);
        if (item > 0 && !owned(item)) give(item);
    }
    for (std::uint32_t k = 0; k < 0x10; ++k) {
        if (w.u8(stage::kPrizes + k) == 0) continue;
        const std::uint32_t chest = ev + 0xa0 + k * 6;
        const int item = logic::sdS16(chest, v);
        if (item < 1) {
            const std::size_t c = std::size_t(logic::sdS16(chest + 2, v) + 1);
            if (c < rolls.size()) rolls[c] += logic::sdS16(chest + 4, v);
        } else if (owned(item)) {
            const std::size_t c = std::size_t(category(item));
            if (c < rolls.size()) rolls[c] += 2;
        } else {
            give(item);
        }
    }
    logic::srand(std::uint32_t(std::time(nullptr)));
    for (int c = 7; c > 0; --c) {
        for (int j = 0; j < rolls[std::size_t(c + 1)]; ++j) {
            const auto& p = pool[std::size_t(c)];
            if (p.empty()) { rolls[std::size_t(c)] += 2; continue; }
            const int item = p[std::size_t(logic::rand() % int(p.size()))];
            if (owned(item)) rolls[std::size_t(c)] += 2;
            else give(item);
        }
    }
    for (int c = 7; c > 0; --c)
        for (int j = 0; j < gotCount[std::size_t(c)]; ++j)
            items_[std::size_t(7 - c)][std::size_t(j)] = got[std::size_t(c)][std::size_t(j)];
}

// FUN_004bbd40: the stage's base exp (by rank, with the turn, objective and
// party-size bonuses), scaled per unit by its damage share, job type and
// its group's affinity, then split over the stats within each stat's and
// the rank's caps. Half of what does not fit goes to the school points.
void TacticsResultTask::expAndPoints() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const std::uint32_t ev = event(w.s16(kEvent));
    const int s = w.s16(kStage) - 1;
    const int rank = std::max(w.s16(stage::kRank) - 1, 0);
    const int turns = w.s16(stage::kTimeGrade);
    int goal = w.s16(stage::kEnemiesGone);
    const bool network = w.u8(stage::kNetwork) != 0;
    w.set16(0x7a5306 + sortieRow(s), rank);
    if (turns == 0) turns_ = 3;
    else if (turns > 0 && turns < 4) turns_ = static_cast<std::int16_t>(rank > 2 ? 3 : turns - 1);
    if (logic::sdS16(ev + 0x3a, v) == 0 && logic::sdS16(ev + 0x3c, v) == 0 && logic::sdS16(ev + 0x3e, v) == 0)
        turns_ = 4;
    if (goal == 0) goal_ = 2; else if (goal == 1) goal_ = 1; else if (goal == 2) goal_ = 0;
    if (logic::sdS16(ev + 0x38, v) == 0) { goal = 0; goal_ = 2; }

    const int members = w.s16(0x7a52f8 + sortieRow(s));
    const int party = std::clamp(members - 1, 1, 0x14) / 4;
    rank_ = static_cast<std::int16_t>(rank);
    units_ = static_cast<std::int16_t>(members);
    pointsBefore_ = w.s32(kPoints);
    const auto unitAt = [&](int c) { return stage::kParty + std::uint32_t(c) * 0xb0; };
    // The sum is kept in the 16-bit +0x4c as it goes, then clamped (so past
    // 32767 it wraps, and a negative total becomes 0).
    std::int16_t damage = 0;
    for (int c = 0; c < members; ++c) damage = static_cast<std::int16_t>(damage + w.s16(unitAt(c) + 0xac));
    damage_ = static_cast<std::int16_t>(std::clamp<int>(damage, 0, 999999999));

    const std::int32_t base = logic::sdS32(ev + 0x24 + std::uint32_t(rank) * 4, v);
    int bonus = goal == 2 ? logic::sdS16(ev + 0x38, v) : 0;
    int terms = goal == 2 ? 1 : 0;
    if (turns_ >= 0 && turns_ < 3) { bonus += logic::sdS16(ev + 0x3a + std::uint32_t(turns_) * 2, v); ++terms; }
    ++terms;
    bonus += logic::sdS16(ev + 0x40 + std::uint32_t(party) * 2, v) - terms * 100;
    const std::int32_t stageExp = base + base * bonus / 100;

    std::int32_t spill = 0, total = 0, counted = 0;
    std::int16_t kills = 0, downs = 0;
    for (int c = 0; c < members; ++c) {
        const std::uint32_t bu = unitAt(c);
        const int unit = w.s16(0x7a52d0 + std::uint32_t(c) * 2 + sortieRow(s));
        const int grp = w.s16(0x7a52a8 + std::uint32_t(c) * 2 + sortieRow(s));
        const int share = damage_ == 0 ? 0 : static_cast<std::int16_t>(w.s16(bu + 0xac) * 100 / damage_);
        int step = 0;
        while (step < 10 && logic::sdS16(kShareSteps + std::uint32_t(step) * 2, v) - 1 < share) ++step;
        const std::uint32_t rec = GameWork::unitRecord(unit);
        const int type = logic::sdU8(0x6b2d8a + std::uint32_t(w.s16(rec + 6)) * 0x40, v);
        const int jobRate = logic::sdS16(kJobRate + std::uint32_t(type * 0xb + step) * 2, v);
        const int affinity = network ? 0 : w.s16(0x7a529c + sortieRow(grp));
        const int affRate = affinity / 10 + 100;
        std::int32_t exp = stageExp + (jobRate - 100) * stageExp / 100 + stageExp * (affRate - 100) / 100;
        exp = std::max(exp, base / 2);
        if (network) exp /= 10;

        const logic::UnitSummary sum = logic::unitSummary(w, v, unit);
        const std::uint32_t prog = GameWork::unitProgress(unit);
        for (std::uint32_t k = 0; k < 8; ++k) w.set32(prog + k * 4, 0);
        std::int32_t acc = 0;
        for (int st = 0; st < 7; ++st) {
            const int pct = logic::sdS16(kStatShare + std::uint32_t(type * 7 + st) * 2, v);
            if (pct == 0) continue;
            std::int32_t gain = exp * pct / 100;
            const logic::StatProgress p =
                logic::statProgress(w, w.u8(rec + 0xc + std::uint32_t(st) * 8), w.s32(rec + 0x10 + std::uint32_t(st) * 8));
            if (p.toMax < gain) { spill += (gain - p.toMax) / 2; gain = p.toMax; }
            if (sum.toCap < acc + gain) {
                const std::int32_t over = acc + gain - sum.toCap;
                if (over < gain) { spill += over / 2; gain -= over; }
                else { spill += gain / 2; gain = 0; }
            }
            w.set32(prog + std::uint32_t(st) * 4, gain);
            acc += gain;
        }
        w.set32(prog + 0x20, acc);
        total += acc;
        ++counted;

        for (int g = 0; g < 5; ++g) {
            for (int k = 0; k < 4; ++k) {
                bool here = false;
                if (!network) {
                    const std::uint16_t slot = w.u16(groups::kMemberSlots + std::uint32_t(g) * 8 + std::uint32_t(k) * 2);
                    here = w.s16At(roster::kMembers, slot) == unit;
                } else {
                    here = w.u16(stage::kParty + 2 + std::uint32_t(k + g * 4) * 0xb0) == unit;
                }
                if (!here) continue;
                Card& cd = cards_[std::size_t(g)][std::size_t(k)];
                cd.unit = static_cast<std::int16_t>(unit);
                cd.kills = w.s16(bu + 0xaa);
                cd.damage = w.s16(bu + 0xac);
                cd.downs = static_cast<std::int8_t>(w.u8(bu + 0xae));
                cd.exp = acc;
                downs = static_cast<std::int16_t>(downs + cd.downs);
                kills = static_cast<std::int16_t>(kills + cd.kills);
            }
        }
    }
    downs_ = downs;
    kills_ = kills;
    const int rate = downs_ != 0 && downs_ <= units_ ? 0x5a : 100;
    pointsGained_ = (counted == 0 ? 0 : total * rate / counted) / 100 + spill;
    points_ = std::clamp<std::int64_t>(std::int64_t(pointsGained_) + pointsBefore_, 0, 999999999);
    w.set32(kPoints, points_);
}

// ----------------------------------------------------------------- screen ---
// FUN_004bab60
Co<> TacticsResultTask::screen() {
    GameWork& w = ctx_.work;
    ctx_.audio->bgmPlay(w.s16(stage::kRank) - 1 < 3 ? 0x17 : 0x1c, 0);
    for (int i = 0; i < 0x10; ++i) {
        co_await yieldFrame();
        ysub::fadeBlack(ctx_, 0xff - i * 0xff / 0xf);
        draw();
    }
    for (;;) {
        co_await yieldFrame();
        draw();
        if (ysub::clicked(ctx_, 0x175, 0x2c8, 299, 0x1c)) break;
    }
    ctx_.audio->sePlay(5);
    for (int i = 0; i < 8; ++i) {
        co_await yieldFrame();
        drawRect(ctx_, rect(kNextLit), 0x54, 0x169, 0x2c4);
        draw();
    }
    for (int i = 0; i < 0x10; ++i) {
        ysub::fadeBlack(ctx_, i * 0xff / 0xf);
        draw();
        co_await yieldFrame();
    }
    ctx_.audio->bgmFadeOut(0x5a);
    while (!ctx_.audio->bgmFadeFinished()) {
        ysub::fadeBlack(ctx_, 0xff);
        co_await yieldFrame();
    }
    ctx_.audio->bgmStop();
}

// FUN_004bae30
void TacticsResultTask::draw() {
    drawRect(ctx_, rect(kBack), 0x56, 0, 0);
    drawRect(ctx_, rect(kRankPlate), 0x55, 0x10c, 0x57, 0, rank_ * 0x1c);
    drawRect(ctx_, rect(kTurnPlate), 0x55, 0x15a, 0x77, 0, turns_ * 0x1c);
    drawRect(ctx_, rect(kGoalPlate), 0x55, 0x143, 0x94, 0, goal_ * 0x18);
    number(0x1c7, 0xc6, 0, 0, 9, 0x55, std::uint32_t(points_), 0xd, 1, 3, 7, kBigFont, kBigComma);
    number(0x1bc, 0xe6, 0, 0, 9, 0x55, std::uint32_t(pointsGained_), 10, 1, 3, 7, kFont, kComma);
    number(0x1bc, 0x102, 0, 0, 9, 0x55, std::uint32_t(pointsBefore_), 10, 1, 3, 7, kFont, kComma);
    number(400, 0x132, 0, 0, 2, 0x55, std::uint32_t(std::int32_t(units_)), 0xd, 1, 2, 5, kBigFont, kBigComma);
    number(0x173, 0x154, 0, 0x14, 4, 0x55, std::uint32_t(std::int32_t(kills_)), 10, 1, 2, 5, kFont, kComma);
    number(400, 0x154, 0, 0x14, 8, 0x55, std::uint32_t(std::int32_t(damage_)), 10, 1, 2, 1, kFont, kComma);
    number(400, 0x170, 0, 0x28, 4, 0x55, std::uint32_t(std::int32_t(downs_)), 10, 1, 2, 5, kFont, kComma);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 10; ++col) {
            const int item = items_[std::size_t(row)][std::size_t(col)];
            if (item <= 0) continue;
            drawRect(ctx_, rect(kItemIcon), 0x55, col * 0x24 + 0xab, row * 0x24 + 0x1bd, (item - 1) % 0xc * 0x24,
                     (item - 1) / 0xc * 0x24);
        }
    }
    for (int g = 0; g < 5; ++g) {
        for (int k = 0; k < 4; ++k) {
            const Card& cd = cards_[std::size_t(g)][std::size_t(k)];
            if (cd.unit < 1) {
                fill(ctx_, k * 0x66 + 0x230, g * 0x7e + 0x4a, 0x61, 0x76, 0x35, 0xa0010101u);
                continue;
            }
            const int x = k * 0x66 + 0x22f, y = g * 0x7e + 0x4a, id = cd.unit - 1;
            drawRect(ctx_, rect(kName), 0x54, x + 0x11, y + 2, 0, id << 4);
            drawRect(ctx_, rect(kFace), 0x55, x + 0x11, y + 0x12, id / 0xd << 6, id % 0xd * 0x4e);
            drawRect(ctx_, rect(kCardLabel), 0x54, x + 0x20, y + 0x14, 0, 0x10);
            number(x + 0x20, y + 0x14, 0, 0x10, 3, 0x54, std::uint32_t(std::int32_t(cd.kills)), 8, 1, 3, 7, kCardFont, kCardComma);
            number(x + 0x27, y + 0x14, 0, 0x10, 5, 0x54, std::uint32_t(std::int32_t(cd.damage)), 8, 1, 3, 3, kCardFont, kCardComma);
            number(x + 0x5f, y + 0x4a, 0, 0x20, 3, 0x54, std::uint32_t(std::int32_t(cd.downs)), 8, 1, 3, 7, kCardFont, kCardComma);
            number(x + 0x60, y + 100, 0, 0, 9, 0x54, std::uint32_t(cd.exp), 8, 1, 3, 7, kCardFont, kCardComma);
        }
    }
    const auto& m = ctx_.mouse();
    drawRect(ctx_, rect(kCursor), 0x33, m.x, m.y);
}

} // namespace ch
