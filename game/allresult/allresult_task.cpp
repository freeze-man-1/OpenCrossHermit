// ============================================================================
// allresult_task.cpp
// ============================================================================
#include "allresult_task.hpp"
#include "../adv/adv_scene.hpp"
#include "../state/game_logic.hpp"

#include <algorithm>

namespace ch {

using ysub::drawRect;
using ysub::fill;

namespace {
constexpr std::uint32_t kSeGroup = 1;            // FUN_0040c780(1, .., PTR_..._LUP00_wa_0061c5e4)
constexpr std::array<std::string_view, 5> kSePaths{
    "data\\Sound\\plw\\ARSND\\SE-LUP00.wav", "data\\Sound\\plw\\ARSND\\SE-SKUP00.wav",
    "data\\Sound\\plw\\ARSND\\SE-SUP00.wav", "data\\Sound\\plw\\ARSND\\SE-041.wav",
    "data\\Sound\\plw\\ARSND\\SE-042.wav"};
enum : std::uint32_t { kSeLevelUp = 0, kSeSkillUp = 1, kSeStatUp = 2, kSeCount = 3, kSeCountEnd = 4 };

// Count-up state (DAT_007e1180..).
constexpr std::uint32_t kMvp = 0x7e1180, kMvpCount = 0x7e1182, kMvpExp = 0x7e1184, kMaxExp = 0x7e1188,
                        kMaxRank = 0x7e118c, kMaxStat = 0x7e118e, kShownExp = 0x7e1190, kShownRank = 0x7e1194,
                        kShownStat = 0x7e1196, kShowSkill = 0x7e1198, kShownPoints = 0x7e119c;
constexpr std::uint32_t kPoints = 0x7a511c;
constexpr std::uint32_t sortie(int g) { return 0x7a529a + std::uint32_t(g) * 0x70; }
constexpr std::uint32_t group(int g) { return groups::at(g); }   // 0x7aaa12
constexpr std::uint32_t kSkills = 0x6c2dc8, kLectures = 0x74bed0, kSkillNeeds = 0x74f686;
// Skill category -> stat row of its "new skill" mark.
constexpr std::array<int, 12> kCategoryRow{0, 1, 2, 2, 3, 3, 4, 4, 4, 4, 4, -1};
} // namespace

// FUN_004c19a0 (FUN_004c1a60 the deleting form): the music stops
// (FUN_004db120), bank 0xf goes (FUN_0041f4e0) and SE group 1 is released
// (FUN_0040d120), then the animation and its bank.
AllResultTask::~AllResultTask() {
    ctx_.audio->bgmStop();
    ctx_.sprites->unloadBank(kBank);
    ctx_.audio->seLoadGroup(kSeGroup, {});
    if (anmFile_.loaded()) for (AnmPlayer::NodeId n : nodes_) anm_.freeRoot(n);
    ctx_.sprites->unloadBank(kAnmBank);
}

ysub::Rect AllResultTask::rect(int i) const { return ysub::rectAt(0x61c600 + std::uint32_t(i) * 0x10, ctx_.version); }

void AllResultTask::fx(int node, bool play, int clip, int x, int y) {
    if (!anmFile_.loaded() || node < 0 || node >= int(nodes_.size())) return;
    static const AnmPlayer::SpriteTemplate kSprite{.color = {0x80, 0x80, 0x80, 0xFF}};   // +0x4ef4
    const AnmPlayer::NodeId n = nodes_[std::size_t(node)];
    if (play) { anm_.play(n, 0, clip, 0); return; }
    anm_.update(n);
    anm_.draw(n, ctx_.draw, *ctx_.sprites, kSprite, x, y, 0);
}

// ------------------------------------------------------------------ start ---
// FUN_004c1aa0
Co<> AllResultTask::start(std::uint32_t) {
    GameWork& w = ctx_.work;
    mode_ = 0;
    if (w.u8(stage::kNetwork) != 0) mode_ = 1;                          // the battle was abandoned
    else if (w.month() == 0xf && w.week() == 4) mode_ = 2;       // the last week
    init();
    int next = 0;
    const auto countMvp = [&] {
        const std::uint32_t at = std::uint32_t(0x7cf46c + w.s16(kMvp) * 0x124);   // unit progress +0x120
        w.set16(at, std::clamp(w.s16(at) + 1, 0, 5));
    };
    if (mode_ == 0) {
        co_await screen();
        writeHistory();
        co_await yieldFrame(5);
        countMvp();
        next = 0;
    } else if (mode_ == 1) {
        co_await yieldFrame(5);
        next = w.u8(stage::kNetSaveUnits) == 0 ? 4 : 2;
    } else {
        writeHistory();
        co_await yieldFrame(5);
        countMvp();
        logic::advanceWeek(w, ctx_.version);                    // FUN_004d3510
        next = 3;
    }
    switch (next) {
        case 0: ctx_.adv = AdvRequest{"Data\\Adv\\dat\\CH003.ybc", 7}; ctx_.scenes.request(SceneId::AdvScript); break;
        case 2: w.set16(0x7e11a8, 3); ctx_.scenes.request(SceneId::SaveLoad); break;
        case 3: ctx_.adv = AdvRequest{"Data\\Adv\\dat\\CH003.ybc", 0x12}; ctx_.scenes.request(SceneId::AdvScript); break;
        case 4: ctx_.scenes.request(SceneId::NetJoin); break;
        default: ctx_.scenes.request(SceneId::WeekStart); break;
    }
}

// ------------------------------------------------------------------- init ---
// FUN_004bd870
void AllResultTask::init() {
    GameWork& w = ctx_.work;
    ctx_.audio->seLoadGroup(kSeGroup, kSePaths);
    w.set16(kMvp, -1);
    for (std::uint32_t a : {kMvpCount, kMaxRank, kMaxStat, kShownRank, kShownStat, kShowSkill}) w.set16(a, 0);
    for (std::uint32_t a : {kMvpExp, kMaxExp, kShownExp, kShownPoints}) w.set32(a, 0);
    skillLearned_ = false;
    noAdventure_ = true;
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\AllResult\\AllResult.bin"));
    // FUN_004bffc0: skill id of each icon cell.
    skillGrid_.fill(0);
    for (int s = 1; s < 0x55; ++s) {
        const int cat = logic::sdU8(kSkills + 4 + std::uint32_t(s) * 0x48, ctx_.version);
        if (cat == 0) continue;
        const int sub = logic::sdU8(kSkills + 7 + std::uint32_t(s) * 0x48, ctx_.version);
        skillGrid_[std::size_t((cat - 1) + (sub - 1) * 0xc) % skillGrid_.size()] = static_cast<std::int16_t>(s);
    }
    pointsGained_ = 0;
    if (w.u8(stage::kNetwork) == 0) computeResults();
    else if (w.u8(stage::kNetSaveUnits) != 0) {                                // FUN_004c0350: the first stage's units
        const int n = std::clamp<int>(w.s16(0x7a52f8), 0, 0x14);
        for (int i = 0; i < n; ++i) adventureResult(w.s16At(0x7a52d0, i));
    }
    w.set32(kPoints, std::clamp<std::int64_t>(std::int64_t(w.s32(kPoints)) + pointsGained_, 0, 999999999));
    anmFile_.load(ctx_.readData("data\\AllResult\\AnmAllResult.bin"), kAnmBank, *ctx_.sprites);   // FUN_004bd7e0
    for (auto& n : nodes_) n = anm_.newRoot(anmFile_);
    anm_.play(nodes_[0], 0, 1, 0);
}

// FUN_004c00c0: each group's members get their lecture or adventure
// results; the largest gains bound the count-up, the best exp is the MVP.
void AllResultTask::computeResults() {
    GameWork& w = ctx_.work;
    bool adventure = false;
    for (int g = 0; g < 5; ++g) {
        const int kind = w.s16(sortie(g));
        if (kind == 2) { adventure = true; noAdventure_ = false; }
        else if (kind == 4) adventure = false;                    // other kinds keep the previous group's choice
        for (int k = 0; k < 4; ++k) {
            const int slot = w.s16(groups::kMemberSlots + std::uint32_t(g) * 8 + std::uint32_t(k) * 2);
            if (slot < 0) continue;
            const int unit = w.s16At(roster::kMembers, slot);
            if (adventure) adventureResult(unit); else lectureResult(unit, g);
            const UnitResult& u = units_[std::size_t(unit) % units_.size()];
            const std::uint32_t rec = GameWork::unitRecord(unit);
            for (std::uint32_t s = 0; s < 7; ++s)
                w.set16(kMaxStat, std::max<int>(w.s16(kMaxStat), w.u8(rec + 0xc + s * 8) - u.gainShown[s]));
            const std::int32_t gained = w.s32(GameWork::unitProgress(unit) + 0x20);
            if (w.s32(kMvpExp) < gained && unit < 0xd) {
                w.set32(kMvpExp, gained);
                w.set16(kMvp, unit);
                w.set16(kMvpCount, w.s16(GameWork::unitProgress(unit) + 0x120));
            }
            w.set32(kMaxExp, std::max(w.s32(kMaxExp), gained));
            w.set16(kMaxRank, std::max<int>(w.s16(kMaxRank), w.u8(rec + 0x50) - u.rankShown));
        }
    }
}

// FUN_004c0400: the lecture's exp (logic::lectureGain), its pool into school
// points, then the skill rolls and the new levels and rank.
void AllResultTask::lectureResult(int unit, int g) {
    GameWork& w = ctx_.work;
    const std::uint32_t rec = GameWork::unitRecord(unit), prog = GameWork::unitProgress(unit);
    const int lecture = w.s16(sortie(g) + 0xa), affinity = w.s16(sortie(g) + 2);
    const auto gain = logic::lectureGain(w, ctx_.version, unit, lecture, affinity);
    w.set32(prog + 0x20, 0);
    for (std::uint32_t s = 0; s < 8; ++s) {
        if (s == 7) { pointsGained_ += gain[7]; continue; }
        w.set32(rec + 0x10 + s * 8, std::int64_t(w.s32(rec + 0x10 + s * 8)) + gain[s]);
        w.set32(prog + 0x20, std::int64_t(w.s32(prog + 0x20)) + gain[s]);
    }
    UnitResult& u = units_[std::size_t(unit) % units_.size()];
    u.newSkill.fill(0);
    unlockSkills(unit, lecture, false);
    for (std::uint32_t s = 0; s < 7; ++s) {
        u.levelBefore[s] = u.gainShown[s] = w.u8(rec + 0xc + s * 8);
        w.set8(rec + 0xc + s * 8, logic::expToLevel(w.s32(rec + 0x10 + s * 8), ctx_.version));
        u.skillShown[s] = 0;
    }
    u.rankBefore = u.rankShown = w.u8(rec + 0x50);
    w.set8(rec + 0x50, logic::unitSummary(w, ctx_.version, unit).rank);
}

// FUN_004c0c10: the exp earned in battle (unit progress +0..+0x18), new
// levels and rank, then the skill rolls.
void AllResultTask::adventureResult(int unit) {
    GameWork& w = ctx_.work;
    const std::uint32_t rec = GameWork::unitRecord(unit), prog = GameWork::unitProgress(unit);
    UnitResult& u = units_[std::size_t(unit) % units_.size()];
    for (std::uint32_t s = 0; s < 7; ++s) {
        u.levelBefore[s] = u.gainShown[s] = w.u8(rec + 0xc + s * 8);
        w.set32(rec + 0x10 + s * 8, std::int64_t(w.s32(rec + 0x10 + s * 8)) + w.s32(prog + s * 4));
        w.set8(rec + 0xc + s * 8, logic::expToLevel(w.s32(rec + 0x10 + s * 8), ctx_.version));
        u.newSkill[s] = 0;
        u.skillShown[s] = 0;
    }
    u.rankBefore = u.rankShown = w.u8(rec + 0x50);
    w.set8(rec + 0x50, logic::unitSummary(w, ctx_.version, unit).rank);
    unlockSkills(unit, -1, true);
}

// The skill rolls of FUN_004c0400 / FUN_004c0c10: per category the job
// allows, the first not-yet-open skill may open (state 2) if the roll and
// its stat needs pass. Lectures roll the lecture's chance per category and
// skip the job-level check; adventures roll levels + 5 * the highest open.
void AllResultTask::unlockSkills(int unit, int lecture, bool adventure) {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const std::uint32_t rec = GameWork::unitRecord(unit), prog = GameWork::unitProgress(unit);
    const int job = w.s16(rec + 6);
    const auto state = [&](int skill) { return w.u8(rec + 0xb8 + std::uint32_t(skill - 1) * 0xc); };
    std::array<int, 5> jobLevels{};                                // per class, from the unit's job levels
    for (std::uint32_t c = 0; c < 5; ++c)
        for (std::uint32_t o : {0u, 1u, 0xau, 0xbu, 0x14u, 0x15u})
            jobLevels[c] += w.s8(prog + 0xd8 + c * 2 + o);
    UnitResult& u = units_[std::size_t(unit) % units_.size()];
    for (int cat = 0; cat <= 10; ++cat) {
        const int levels = logic::sdU8(0x6e4159 + std::uint32_t(cat) + std::uint32_t(job) * 0xd, v);
        if (levels == 0) continue;
        int highest = 0;
        for (int sub = 0; sub < 7; ++sub) {
            const int st = skillAt(cat, sub) > 0 ? state(skillAt(cat, sub)) : 0;
            if (st == 3 || (st > 4 && st < 7)) highest = sub + 1;
        }
        int skill = -1;
        for (int sub = 0; sub < levels; ++sub) {
            const int id = skillAt(cat, sub);
            const int st = id > 0 ? state(id) : 0;
            if (st == 3 || st == 5 || st == 6) continue;
            if (st != 0) { if (adventure) break; continue; }
            const int chance = adventure ? levels + highest * 5
                                         : logic::sdU8(kLectures + 0x4e + std::uint32_t(cat) + std::uint32_t(lecture) * 0x60, v);
            if (chance != 100 && chance < logic::rand() % 0x65) break;
            skill = id;
            break;
        }
        if (skill <= 0) continue;
        bool ok = true;
        int total = 0;
        for (int s = 0; s < 7; ++s) {
            const int lv = w.u8(rec + 0xc + std::uint32_t(s) * 8);
            total += lv;
            if (lv < logic::sdS16(kSkillNeeds + std::uint32_t(s) * 2 + std::uint32_t(skill) * 0x32, v)) { ok = false; break; }
        }
        if (ok && total < logic::sdS16(0x74f694 + std::uint32_t(skill) * 0x32, v)) ok = false;
        if (ok && adventure)
            for (int c = 0; c < 5; ++c)
                if (jobLevels[std::size_t(c)] < logic::sdS16(0x74f696 + std::uint32_t(c) * 2 + std::uint32_t(skill) * 0x32, v)) { ok = false; break; }
        if (!ok) continue;
        w.set8(rec + 0xb8 + std::uint32_t(skill - 1) * 0xc, 2);
        skillLearned_ = true;
        if (const int row = kCategoryRow[std::size_t(cat)]; row >= 0) u.newSkill[std::size_t(row)] = 1;
    }
}

// ----------------------------------------------------------------- screen ---
// FUN_004bdac0
Co<> AllResultTask::screen() {
    GameWork& w = ctx_.work;
    for (int i = 0; i < 0x10; ++i) {
        fill(ctx_, 0, 0, 0x400, 0x300, 0x51, std::uint32_t(0xff - i * 0xff / 0xf) << 24 | 0x10101u);
        draw();
        co_await yieldFrame();
    }
    ctx_.audio->bgmPlay(noAdventure_ ? 0x1e : 0x1d, 0);          // FUN_004db060
    for (int i = 0; i < 0x1e; ++i) { draw(); co_await yieldFrame(); }

    // Count-up over 0xf0 frames, with a tick for each new stat / rank step.
    ctx_.audio->sePlayIn(kSeGroup, kSeCount, 1);
    int lastStat = 0, lastRank = 0;
    for (int i = 0; i < 0xf0; ++i) {
        w.set32(kShownExp, std::int64_t(w.s32(kMaxExp)) * i / 0xef);
        w.set16(kShownStat, w.s16(kMaxStat) * i / 0xef);
        if (lastStat != w.s16(kShownStat)) ctx_.audio->sePlayIn(kSeGroup, kSeStatUp, -1);
        lastStat = w.s16(kShownStat);
        w.set16(kShownRank, w.s16(kMaxRank) * i / 0xef);
        if (lastRank != w.s16(kShownRank)) ctx_.audio->sePlayIn(kSeGroup, kSeLevelUp, -1);
        lastRank = w.s16(kShownRank);
        w.set32(kShownPoints, w.s32(kPoints) / 0xef * i);
        draw();
        co_await yieldFrame();
    }
    ctx_.audio->sePlayIn(kSeGroup, kSeCountEnd, 1);
    w.set32(kShownExp, w.s32(kMaxExp));
    w.set16(kShownStat, w.s16(kMaxStat));
    w.set16(kShownRank, w.s16(kMaxRank));
    w.set32(kShownPoints, w.s32(kPoints));
    for (int i = 0; i < 0x1e; ++i) { draw(); co_await yieldFrame(); }
    if (skillLearned_) {
        w.set16(kShowSkill, 1);
        ctx_.audio->sePlayIn(kSeGroup, kSeSkillUp, -1);
    }

    for (;;) {
        // The MVP script, drawn over the results (FUN_004ce210 / FUN_004ce8f0).
        bool done = false;
        ctx_.adv = AdvRequest{"Data\\AllResult\\dat\\mvp.ybc", -1};
        ctx_.tasks.link(std::make_unique<AdvTask>(ctx_, &done), 0, 0);
        while (!done) { draw(); co_await yieldFrame(); }
        // 次へ / もう一度
        for (;;) {
            if (ysub::clicked(ctx_, 0x2e1, 0x2db, 0x10f, 0x1e)) {
                ctx_.audio->sePlay(5);
                for (int i = 0; i < 8; ++i) { drawRect(ctx_, rect(3), 0x57, 0x2d6, 0x2d8); draw(); co_await yieldFrame(); }
                for (int i = 0xf; i >= 0; --i) {
                    fill(ctx_, 0, 0, 0x400, 0x300, 0x51, std::uint32_t(0xff - i * 0xff / 0xf) << 24 | 0x10101u);
                    draw();
                    co_await yieldFrame();
                }
                ctx_.audio->bgmFadeOut(0x5a);
                while (!ctx_.audio->bgmFadeFinished()) co_await yieldFrame();
                ctx_.audio->bgmStop();
                co_return;
            }
            if (ysub::clicked(ctx_, 0x1ba, 0x2db, 0x10e, 0x1e)) {
                ctx_.audio->sePlay(5);
                for (int i = 0; i < 8; ++i) { drawRect(ctx_, rect(2), 0x57, 0x1ae, 0x2d8); draw(); co_await yieldFrame(); }
                break;                                             // the MVP script again
            }
            draw();
            co_await yieldFrame();
        }
    }
}

// ------------------------------------------------------------------- draw ---
// FUN_004be0b0
void AllResultTask::draw() {
    GameWork& w = ctx_.work;
    for (int g = 0; g < 5; ++g) {
        const int slot = w.s16At(groups::kLeaderSlots, g);
        if (slot < 0) fill(ctx_, 1, g * 0x8a + 0x27, 0xc6, 0x86, 0x52, 0xa0010101u);
        else drawLeader(g, w.s16At(roster::kGuests, slot));
    }
    for (int g = 0; g < 5; ++g)
        for (int k = 0; k < 4; ++k) {
            const int slot = w.s16(groups::kMemberSlots + std::uint32_t(g) * 8 + std::uint32_t(k) * 2);
            if (slot < 0) fill(ctx_, k * 0xc6 + 0xe7, g * 0x8a + 0x27, 0xc6, 0x86, 0x52, 0xa0010101u);
            else drawMember(k, g, w.s16At(roster::kMembers, slot));
        }
    drawCommaNumber(0x15e, 0x2e2, 9, 0x57, std::uint32_t(w.s32(kShownPoints)), 10, 1, 4, 0x10, 0x19);
    drawRect(ctx_, rect(1), 0x59, 0, 0);
    drawRect(ctx_, rect(0), 0x50, ctx_.mouse().x, ctx_.mouse().y);
}

// FUN_004be950: a group's leader card with what the group did.
void AllResultTask::drawLeader(int g, int leader) {
    GameWork& w = ctx_.work;
    constexpr int x = 1, prio = 0x57;
    const int y = g * 0x8a + 0x27;
    const int kind = w.s16(sortie(g));
    const int grade = 7 - w.s16(sortie(g) + 4);
    int adv = 0;
    if (kind == 2) { adv = 1; drawAdventurePlate(0x2a, y + 0x68, w.s16(sortie(g) + 6), w.s16(sortie(g) + 8), true, prio); }
    else if (kind == 4) drawLecturePlate(0x2a, y + 0x68, w.s16(sortie(g) + 6), w.s16(sortie(g) + 8), prio);
    drawRect(ctx_, rect(0xb), prio, x + 0x7a, y + 0x2d, 0, adv * 0xc);
    drawFace(x + 7, y + 3, 1, leader - 0x65, prio);
    drawName(x + 0xd, y + 0x4e, 1, leader - 0x65, prio);
    drawRect(ctx_, rect(0xa), prio, x + 0x7a, y + 10);
    drawRect(ctx_, rect(0xc), prio, x + 0x7a, y + 0x3e, 0, grade * 0xc);
}

// FUN_004be2e0: a member card; levels, gains, rank and exp count up to the
// current DAT_007e119x values.
void AllResultTask::drawMember(int slot, int g, int unit) {
    GameWork& w = ctx_.work;
    const int x = slot * 0xc6 + 0xe7, y = g * 0x8a + 0x27, prio = 0x57;
    const std::uint32_t rec = GameWork::unitRecord(unit);
    UnitResult& u = units_[std::size_t(unit) % units_.size()];
    const int fxBase = slot * 7 + g * 0x1c;
    ysub::Rect digits = rect(0x11);
    ysub::Rect digitsLit = digits;
    digitsLit.sy = static_cast<std::int16_t>(digitsLit.sy + 0x10);
    constexpr int kFlags = ysub::kDigitsNoZeros | ysub::kDigitsRight;
    for (int s = 0; s < 7; ++s) {
        const std::size_t si = std::size_t(s);
        const int ry = y + 10 + s * 0x12;
        const int gain = std::min<int>(w.u8(rec + 0xc + si * 8) - u.levelBefore[si], w.s16(kShownStat));
        ysub::drawDigits(ctx_, digitsLit, x + 0x9e, ry, 3, prio, std::uint32_t(u.levelBefore[si] + gain), 6, 1, 2, kFlags);
        if (gain != u.gainShown[si]) fx(fxBase + s, true, 2, 0, 0);
        u.gainShown[si] = static_cast<std::int16_t>(gain);
        if (gain > 0) {
            fx(fxBase + s, false, 0, x + 0xb4, y + 0xf + s * 0x12);
            drawRect(ctx_, rect(0xf), prio, x + 0xa3, ry);
            ysub::drawDigits(ctx_, digits, x + 0xb9, ry, 3, prio, std::uint32_t(gain), 6, 1, 2, kFlags);
        }
        if (w.s16(kShowSkill) == 0) {
            u.skillShown[si] = 0;
        } else if (u.newSkill[si] != 0) {
            if (u.skillShown[si] == 0) { u.skillShown[si] = 1; fx(fxBase + s, true, 1, 0, 0); }
            fx(fxBase + s, false, 0, x + 0x66, y + 0x13 + s * 0x12);
            drawRect(ctx_, rect(0xd), prio, x + 0x5c, y + 9 + s * 0x12);
        }
    }
    const int rankGain = std::min<int>(w.u8(rec + 0x50) - u.rankBefore, w.s16(kShownRank));
    const int rankFx = slot + 0x8c + g * 4;
    if (rankGain != u.rankShown) fx(rankFx, true, 3, 0, 0);
    u.rankShown = static_cast<std::int16_t>(rankGain);
    ysub::drawDigits(ctx_, rect(0x12), x + 0x50, y + 0x66, 2, prio, std::uint32_t(u.rankBefore + rankGain), 5, 1, 2, kFlags);
    if (rankGain > 0) fx(rankFx, false, 0, x + 0x2c, y + 0x29);
    const std::int32_t exp = std::min(w.s32(GameWork::unitProgress(unit) + 0x20), w.s32(kShownExp));
    drawCommaNumber(x + 0x40, y + 0x78, 6, prio, std::uint32_t(exp), 5, 1, 3, 0x12, 0x18);
    drawFace(x + 7, y + 3, 0, unit - 1, prio);
    drawName(x + 0xd, y + 0x4e, 0, unit - 1, prio);
    drawRect(ctx_, rect(8), prio, x + 0x65, y + 7);
    const int cls = logic::sdU8(0x6b2d8a + std::uint32_t(w.s16(rec + 6)) * 0x40, ctx_.version);
    drawRect(ctx_, rect(9), prio, x + 0xe, y + 0x65, 0, (cls - 1) * 0xc);
    drawRect(ctx_, rect(0xe), prio, x + 0x43, y + 0x78);
}

// FUN_004beb90: adventure name plate (rects 0x14..0x17, 24 rows a column).
void AllResultTask::drawAdventurePlate(int x, int y, int type, int sub, bool title, int prio) {
    int plate = 0x14;
    switch (type) {
        case 0: plate = 0x15; sub = std::clamp(sub, 0, 10); break;
        case 1: plate = 0x16; sub = std::clamp(sub, 0, 10); break;
        case 2: plate = 0x17; sub = std::clamp(sub, 0, 10); break;
        case 3: plate = 0x14; sub = std::clamp(sub, 0, 0x1f); break;
        default: break;
    }
    ysub::Rect r = rect(plate);
    r.sx = static_cast<std::int16_t>(r.sx + (sub / 0x18) * r.w);
    r.sy = static_cast<std::int16_t>(r.sy + (sub % 0x18) * r.h);
    if (title) r.w = 0x74;
    drawRect(ctx_, r, prio, x, y);
}

// FUN_004bee00: lecture name plate (AllResult frames 4..6 by level).
void AllResultTask::drawLecturePlate(int x, int y, int level, int sub, int prio) {
    sub = std::clamp(sub, 0, 0x50);
    const int frame = level == 1 ? 5 : level == 2 ? 6 : 4;
    drawRect(ctx_, {.bank = kBank, .frame = static_cast<std::uint16_t>(frame),
                    .sx = static_cast<std::int16_t>(sub / 0x17 * 0x74), .sy = static_cast<std::int16_t>(sub % 0x17 * 0x16),
                    .w = 0x74, .h = 0x16}, prio, x, y);
}

// FUN_004bf6c0: face (members rect 4, 14 a column; leaders rect 5, 7).
void AllResultTask::drawFace(int x, int y, int kind, int index, int prio) {
    const ysub::Rect r = rect(kind == 0 ? 4 : 5);
    const int rows = kind == 0 ? 0xe : 7;
    const int i = index & 0xffff;
    drawRect(ctx_, r, prio, x, y, r.w * (i / rows), r.h * (i % rows));
}

// FUN_004bf870: name plate (members rect 6, leaders rect 7).
void AllResultTask::drawName(int x, int y, int kind, int index, int prio) {
    const ysub::Rect r = rect(kind == 0 ? 6 : 7);
    drawRect(ctx_, r, prio, x, y, 0, r.h * index);
}

// FUN_004bef20: right-aligned grouped digits (flags 7) with a comma rect
// between groups; the group gap only follows a drawn comma.
void AllResultTask::drawCommaNumber(int x, int y, int digits, int prio, std::uint32_t value, int pitch, int gap,
                                    int groupGap, int font, int comma) {
    const ysub::Rect f = rect(font), c = rect(comma);
    int width = (digits - 1) * gap + digits * pitch;
    if (digits / 3 != 0) width += digits / 3 * (groupGap + gap);
    int dx = x - width;
    bool skipping = true;
    for (int i = 0; i < digits; ++i) {
        std::uint32_t div = 1;
        for (int k = 0; k < digits - 1 - i; ++k) div *= 10;
        const bool last = i + 1 == digits;
        const int d = int(value / div % 10);
        if (d != 0 || last) skipping = false;
        if (!skipping) drawRect(ctx_, f, prio, dx, y, f.x * d, 0);
        dx += gap + pitch;
        if ((digits - (i + 1)) % 3 == 0 && !last && !skipping) {
            drawRect(ctx_, c, prio, dx, y);
            dx += gap + groupGap;
        }
    }
}

// --------------------------------------------------------------- history ---
// FUN_004c1350: job levels +1 for the whole roster, this week's history
// record (2 adventure, 1 lecture, 3 idle), then FUN_004bf9e0.
void AllResultTask::writeHistory() {
    GameWork& w = ctx_.work;
    const int week = (w.month() - 4) * 5 + w.week() - 1;
    std::vector<int> assigned;
    for (int g = 0; g < 5; ++g)
        for (std::uint32_t k = 0; k < 4; ++k)
            if (const int id = w.s16(group(g) + 0x10 + k * 2); id >= 0) assigned.push_back(id);
    for (int i = 0; i < w.s16(roster::kMemberCount); ++i) {
        const int id = w.s16At(roster::kMembers, i);
        const std::uint32_t at = GameWork::unitProgress(id) + 0xd7 + std::uint32_t(w.s16(GameWork::unitRecord(id) + 6));
        int lv = w.s8(at);
        if (lv != 100) lv = std::clamp(lv + 1, 0, 99);
        w.set8(at, lv);
        const std::uint32_t h = GameWork::unitProgress(id) + 0x24 + std::uint32_t(week) * 3;
        if (std::find(assigned.begin(), assigned.end(), id) == assigned.end()) { w.set8(h, 3); continue; }
    }
    for (int g = 0; g < 5; ++g) {
        const int kind = w.s16(sortie(g));
        if (kind != 2 && kind != 4) continue;
        for (std::uint32_t k = 0; k < 4; ++k) {
            const int id = w.s16(group(g) + 0x10 + k * 2);
            if (id < 0) continue;
            const std::uint32_t h = GameWork::unitProgress(id) + 0x24 + std::uint32_t(week) * 3;
            w.set8(h, kind == 2 ? 2 : 1);
            w.set8(h + 1, w.u8(sortie(g) + 6));
            w.set8(h + 2, w.u8(sortie(g) + 8));
        }
    }
    adjustAffinities();
}

// FUN_004bf9e0: idle units warm to each other (more with the same class or
// rank) and cool towards the assigned; groupmates and their leader warm
// up; each stage's units by its result (3 / 2 / 1 / 0 / -1).
void AllResultTask::adjustAffinities() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    std::vector<int> assigned, idle;
    for (int g = 0; g < 5; ++g)
        for (std::uint32_t k = 0; k < 4; ++k)
            if (const int id = w.s16(group(g) + 0x10 + k * 2); id >= 0) assigned.push_back(id);
    for (int i = 0; i < w.s16(roster::kMemberCount); ++i) {
        const int id = w.s16At(roster::kMembers, i);
        if (std::find(assigned.begin(), assigned.end(), id) == assigned.end()) idle.push_back(id);
    }
    const auto cls = [&](int id) { return logic::sdU8(0x6b2d8a + std::uint32_t(w.s16(GameWork::unitRecord(id) + 6)) * 0x40, v); };
    for (std::size_t a = 0; a < idle.size(); ++a)
        for (std::size_t b = 0; b < idle.size(); ++b) {
            if (a == b) continue;
            if (cls(idle[a]) == cls(idle[b])) logic::adjustAffinity(w, idle[a], idle[b], 1);
            if (w.u8(GameWork::unitRecord(idle[a]) + 0x50) == w.u8(GameWork::unitRecord(idle[b]) + 0x50))
                logic::adjustAffinity(w, idle[a], idle[b], 1);
        }
    for (std::size_t a = 0; a < idle.size(); ++a)
        for (std::size_t b = 0; b < idle.size(); ++b)
            if (a != b) logic::adjustAffinity(w, idle[a], idle[b], 2);
    for (int a : idle)
        for (int b : assigned)
            if (a != b) logic::adjustAffinity(w, a, b, -1);
    for (int g = 0; g < 5; ++g) {
        std::vector<int> m;
        for (std::uint32_t k = 0; k < 4; ++k)
            if (const int id = w.s16(group(g) + 0x10 + k * 2); id >= 0) m.push_back(id);
        for (std::size_t a = 0; a < m.size(); ++a)
            for (std::size_t b = 0; b < m.size(); ++b)
                if (a != b) logic::adjustAffinity(w, m[a], m[b], 1);
        const int leader = w.s16(group(g));
        for (int id : m) {
            logic::adjustAffinity(w, id, leader, 1);
            logic::adjustAffinity(w, leader, id, 1);
        }
    }
    constexpr std::array<int, 5> kByResult{3, 2, 1, 0, -1};
    for (int s = 0; s < w.s16(0x7a5298); ++s) {
        const std::uint32_t r = std::uint32_t(s) * 0x70;
        const int result = w.s16(0x7a5306 + r);
        const int delta = result >= 0 && result < 5 ? kByResult[std::size_t(result)] : -1;
        const int n = w.s16(0x7a52f8 + r), leaders = w.s16(0x7a5304 + r);
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b)
                if (a != b) logic::adjustAffinity(w, w.s16(0x7a52d0 + std::uint32_t(a) * 2 + r), w.s16(0x7a52d0 + std::uint32_t(b) * 2 + r), delta);
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < leaders; ++b) {
                const int m = w.s16(0x7a52d0 + std::uint32_t(a) * 2 + r), l = w.s16(0x7a52fa + std::uint32_t(b) * 2 + r);
                logic::adjustAffinity(w, m, l, delta);
                logic::adjustAffinity(w, l, m, delta);
            }
    }
}

} // namespace ch
