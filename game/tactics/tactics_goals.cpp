// ============================================================================
// tactics_goals.cpp — StageChecks::goals: the pause menu's list of what a
// story stage's best result asks for (the port's own; see tactics_checks.hpp)
//
// Each map's list follows its check in tactics_checks.cpp: what it needs
// for rank 1 (PERFECT), and for each goal the best rank left once that goal
// has failed (`drop`; 5 when failing it loses the stage). Lower ranks'
// time steps are hidden goals: they only set `best`. Goals that only ask
// that something lasts (keep, protect, none fall) stay open until they fail:
// a check means done for good.
// ============================================================================
#include "tactics_checks.hpp"

#include <algorithm>

namespace ch {

namespace {
constexpr std::int32_t kMinute = 0xe10;            // frames
}

// The goals() list's entries, each worked out from the check's own tests.
class GoalBuilder {
public:
    using Unit = TacticsUnits::Unit;
    using List = StageChecks::List;

    GoalBuilder(const StageChecks& s, std::vector<Goal>& g) : s_(s), g_(g) {}

    std::vector<int> slotsOf(List l) const {
        std::vector<int> v;
        for (const int r : l) v.push_back(s_.slotOf(r));
        return v;
    }
    Goal& add(Goal::Kind kind, int drop) {
        g_.push_back(Goal{.kind = kind, .drop = drop});
        return g_.back();
    }
    // The stage's own objective: a flag its script sets (value 3/k, or 0/0).
    void objective(int type, std::uint32_t index) {
        Goal& x = add(Goal::Kind::Objective, 5);
        if (s_.script_.value(type, index) != 0) x.state = Goal::State::Done;
    }
    void allEnemies(int drop) {
        Goal& x = add(Goal::Kind::AllEnemies, drop);
        x.have = s_.enemiesLeft();
        if (s_.allClear()) x.state = Goal::State::Done;
    }
    void defeat(List l, int drop = 5) {
        Goal& x = add(Goal::Kind::Defeat, drop);
        x.slots = slotsOf(l);
        x.need = int(l.size());
        x.have = 0;
        for (const int r : l) x.have += s_.dead(s_.rec(r));
        if (x.have == x.need) x.state = Goal::State::Done;
    }
    // Records the check wants on our side: buildings are taken, people won
    // over, and those ours from the start kept. One that fell on the other
    // side can no longer be had.
    void side(List l, int drop) {
        Goal& x = add(Goal::Kind::Side, drop);
        x.slots = slotsOf(l);
        x.need = int(l.size());
        x.have = s_.ownCount(l);
        x.keep = true;
        for (const int r : l) {
            const Unit& u = s_.rec(r);
            const int kind = u.kind();
            x.take = x.take || TacticsUnits::isStructure(u) || TacticsUnits::isBase(u) || (kind >= 0x19b && kind <= 0x19e);
            x.collect = x.collect || kind == 0x195 || kind == 0x196;     // treasure chests
            x.keep = x.keep && s_.startOwn_[std::size_t(s_.slotOf(r))];
            if (s_.gone(u) && !s_.own(u)) x.state = Goal::State::Failed;
        }
        if (x.state != Goal::State::Failed && x.have == x.need && !x.keep) x.state = Goal::State::Done;
    }
    void protect(List l, int drop) {
        Goal& x = add(Goal::Kind::Protect, drop);
        x.slots = slotsOf(l);
        for (const int r : l) {
            const Unit& u = s_.rec(r);
            if (s_.gone(u)) x.state = Goal::State::Failed;
            x.foe = x.foe || s_.hostile(u);
            x.take = x.take || TacticsUnits::isStructure(u) || TacticsUnits::isBase(u);
        }
    }
    void noneFall(bool players = false) {
        Goal& x = add(Goal::Kind::NoneFall, 2);
        if (!(players ? s_.noFalls() : s_.perfect())) x.state = Goal::State::Failed;
    }
    // c: buildings()'s counts; base: its five (ours standing, theirs
    // standing, ours gone, theirs gone, all).
    void standing(Goal::Kind kind, const std::array<int, 25>& c, int base, int drop) {
        const auto at = [&](int k) { return c[std::size_t(base + k)]; };
        if (at(4) == 0) return;
        Goal& x = add(kind, drop);
        x.need = at(4);
        x.have = at(0);
        x.keep = s_.startBuildings_[std::size_t(base)] == s_.startBuildings_[std::size_t(base + 4)];
        if (at(2) + at(3) > 0) x.state = Goal::State::Failed;
        else if (x.have == x.need && !x.keep) x.state = Goal::State::Done;
    }
    void enemyGates(const std::array<int, 25>& c, int drop) {
        if (c[24] == 0) return;
        Goal& x = add(Goal::Kind::EnemyGates, drop);
        x.need = c[24];
        x.have = c[23];
        if (x.have == x.need) x.state = Goal::State::Done;
        else if (c[22] > 0) x.state = Goal::State::Failed;
    }
    void within(std::int32_t frames, int drop, bool hidden = false) {
        Goal& x = add(Goal::Kind::Within, drop);
        x.frames = frames;
        x.left = std::max(frames - s_.clock_.elapsed, 0);
        x.hidden = hidden;
        if (s_.clock_.elapsed >= frames) x.state = Goal::State::Failed;
    }
    void timeLimit(int drop) {
        if (s_.clock_.limit == 0x7fffffff) return;
        Goal& x = add(Goal::Kind::TimeLimit, drop);
        x.frames = s_.clock_.limit;
        x.left = s_.clock_.left;
        if (s_.clock_.left == 0) x.state = Goal::State::Failed;
    }
    void holdOut(std::int32_t frames) {
        Goal& x = add(Goal::Kind::HoldOut, 5);
        x.frames = frames;
        x.left = std::max(frames - s_.clock_.elapsed, 0);
        if (x.left == 0) x.state = Goal::State::Done;
    }
    // Until the stage's clock runs out.
    void untilTimeUp() {
        Goal& x = add(Goal::Kind::HoldOut, 5);
        x.frames = s_.clock_.limit;
        x.left = s_.clock_.left;
        if (s_.clock_.left == 0) x.state = Goal::State::Done;
    }
    void hold(std::int32_t frames, std::int32_t held) {
        Goal& x = add(Goal::Kind::Hold, 5);
        x.frames = frames;
        x.left = std::max(frames - held, 0);
        if (x.left == 0) x.state = Goal::State::Done;
    }
    void reach(bool there, int drop = 5) {
        Goal& x = add(Goal::Kind::Reach, drop);
        if (there) x.state = Goal::State::Done;
    }

private:
    const StageChecks& s_;
    std::vector<Goal>& g_;
};

Goals StageChecks::goals(int mapId) const {
    Goals out;
    auto& g = out.list;
    GoalBuilder b(*this, g);

    switch (mapId) {
        case 1: case 0x4e: {
            const auto c = buildings();
            b.allEnemies(5);
            b.noneFall();
            b.standing(Goal::Kind::Buildings, c, 10, 3);
            b.standing(Goal::Kind::Gates, c, 20, 3);
            break;
        }
        case 2: case 0x4f:
            b.objective(3, 0);
            b.side({1, 2, 3}, 3);
            b.noneFall();
            b.timeLimit(4);
            break;
        case 3:
            b.side({2, 3, 4}, 3);
            b.allEnemies(5);
            b.noneFall();
            b.timeLimit(4);
            break;
        case 4:
            b.allEnemies(5);
            b.within(0x6271, 3);
            b.noneFall();
            b.timeLimit(4);
            break;
        case 5:
            b.defeat({0xb});
            b.side({2, 3}, 3);
            b.noneFall();
            b.timeLimit(4);
            break;
        case 6:
            b.defeat({5});
            b.allEnemies(3);
            b.side({1, 2, 3, 4}, 3);
            b.noneFall();
            break;
        case 7: case 0x4c:
            b.objective(3, 0);
            b.side({4, 5}, 3);
            b.protect({2}, 5);
            b.noneFall();
            break;
        case 8:
            b.holdOut(0x1a5e0);
            b.side({3, 4, 5}, 4);
            b.side({6}, 3);
            b.side({1, 2}, 3);
            b.noneFall();
            break;
        case 9: case 0x4d: {
            b.defeat({0x2f});
            b.side({0x2d, 0x2e}, 4);
            const auto c = buildings();
            b.standing(Goal::Kind::Buildings, c, 10, 3);
            b.noneFall();
            b.timeLimit(4);
            break;
        }
        case 10:
            b.objective(3, 0);
            b.side({0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30}, 3);
            b.protect({1}, 5);
            b.noneFall();
            break;
        case 0xb: {
            // Value 2/0 counts the enemies that broke through (the script's
            // "two", "three" ...); when the time runs out: 0 PERFECT, 1..3
            // EXCELLENT, 4..9 CONGRATULATIONS; ten lose the stage.
            b.untilTimeUp();
            Goal& x = b.add(Goal::Kind::Leak, 2);
            x.have = script_.value(2, 0);
            if (x.have > 0) x.state = Goal::State::Failed;
            out.best = x.have == 0 ? 1 : x.have <= 3 ? 2 : x.have <= 9 ? 3 : 5;
            break;
        }
        case 0xd: {
            {
                Goal& x = b.add(Goal::Kind::Objective, 5);   // flags 3/0..3/2 open the points
                if (script_.value(3, 0) != 0 && script_.value(3, 1) != 0 && script_.value(3, 2) != 0) x.state = Goal::State::Done;
            }
            auto in = [&](int area, int classes) {
                const StageScript::Tally& t = script_.tally(area, 0);
                return t.count != 0 && (classes == 0 || (t.classes & classes) != 0);
            };
            Goal& x = b.add(Goal::Kind::Occupy, 5);
            x.need = 6;
            x.have = in(4, 3) + in(5, 0x18) + in(6, 0x18) + in(7, 0) + in(8, 0) + in(9, 0);
            if (x.have == 6) x.state = Goal::State::Done;
            b.hold(300, script_.value(0, 0));
            b.defeat({1, 2, 3}, 3);
            b.timeLimit(4);
            break;
        }
        case 0xe: case 0xf:
            b.objective(3, 0);
            b.within(0x8ca1, 3);
            b.noneFall();
            break;
        case 0x10:
            b.side({1, 2, 3, 4}, 4);
            b.side({5, 6, 7, 8, 9, 0xa}, 3);
            b.noneFall();
            b.timeLimit(4);
            break;
        case 0x11: {
            const auto c = buildings();
            {
                Goal& x = b.add(Goal::Kind::ReachAfter, 5);
                x.frames = 54000;
                x.left = std::max(54000 - clock_.elapsed, 0);
                if (script_.tally(1, 0).count > 0) x.state = Goal::State::Done;
            }
            b.standing(Goal::Kind::Buildings, c, 10, 3);
            b.standing(Goal::Kind::Bases, c, 15, 3);
            b.enemyGates(c, 3);
            b.noneFall();
            break;
        }
        case 0x12:
            b.side({1, 2, 3}, 5);
            b.allEnemies(3);
            b.protect({1, 2, 3}, 4);
            b.noneFall();
            break;
        case 0x14:
            b.defeat({6});
            b.side({1, 2, 3, 4, 5}, 3);
            b.protect({1, 2, 3, 4, 5}, 3);
            b.noneFall();
            break;
        case 0x15:
            b.hold(54000, script_.value(0, 0));
            b.side({1, 2, 3, 4}, 3);
            b.side({5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11}, 3);
            break;
        case 0x16:
            b.allEnemies(5);
            b.within(0x4651, 3);
            b.noneFall();
            b.timeLimit(4);
            break;
        case 0x18:
            b.defeat({0x38, 0x39});
            b.side({2, 3, 4, 5, 6}, 3);
            b.protect({9, 0xa}, 4);
            b.noneFall();
            break;
        case 0x1d:
            b.side({1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf}, 5);
            b.protect({1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf}, 5);
            b.protect({0x13}, 4);
            b.protect({0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d}, 3);
            b.noneFall();
            break;
        case 0x1f: {
            Goal& x = b.add(Goal::Kind::Escort, 5);
            x.slots = b.slotsOf({0xd, 0xe, 0xf});
            x.need = 3;
            x.have = 0;
            for (const int r : {0xd, 0xe, 0xf}) {
                const Unit& u = rec(r);
                x.have += on(u) && map_.cell(u.cx(), u.cy(), 2) == 1;
            }
            if (x.have == 3) x.state = Goal::State::Done;
            b.protect({0xd, 0xe, 0xf}, 3);
            b.noneFall();
            break;
        }
        case 0x20:
            b.untilTimeUp();
            b.side({3, 4, 5, 6}, 3);
            b.noneFall();
            break;
        case 0x21:
            b.allEnemies(5);
            b.within(0x8ca1, 2);
            b.within(0xd2f1, 3, true);
            b.within(0x1a5e1, 4, true);
            break;
        case 0x23:
            b.allEnemies(5);
            b.within(0x2a31, 2);
            b.within(0x4651, 3, true);
            b.within(0x8ca1, 4, true);
            break;
        case 0x25: case 0x26: case 0x27:
            b.objective(3, 0);
            b.allEnemies(3);
            b.noneFall(true);
            break;
        case 0x28: case 0x44:
            b.objective(3, 0);
            b.noneFall(true);
            break;
        case 0x29:
            b.defeat({1});
            b.side({2}, 3);
            b.noneFall();
            b.timeLimit(4);
            break;
        case 0x2a:
            b.defeat({1});
            b.noneFall(true);
            b.timeLimit(5);
            break;
        case 0x2b:
            b.defeat({1, 2, 3, 4});
            b.noneFall(true);
            b.timeLimit(5);
            break;
        case 0x2c:
            b.defeat({1});
            b.side({2, 3, 0x60}, 3);
            b.noneFall();
            b.timeLimit(4);
            break;
        case 0x2d:
            b.defeat({1});
            if (script_.value(3, 0) != 0) b.defeat({2});
            b.side({3, 4, 5, 6, 7, 8, 0x2c, 0x2d}, 3);
            b.noneFall();
            break;
        case 0x52: case 0x53: {
            Goal& x = b.add(Goal::Kind::Defeat, 5);
            x.slots = {0xcb};
            x.need = 1;
            x.have = dead(units_.unit(0xcb));
            if (x.have == 1) x.state = Goal::State::Done;
            Goal& y = b.add(Goal::Kind::Side, 3);
            y.slots = {0xcd, 0xcc};
            y.need = 2;
            y.have = own(units_.unit(0xcd)) + own(units_.unit(0xcc));
            y.keep = startOwn_[0xcd] && startOwn_[0xcc];
            for (const int s : {0xcd, 0xcc})
                if (gone(units_.unit(s)) && !own(units_.unit(s))) y.state = Goal::State::Failed;
            if (y.state != Goal::State::Failed && y.have == 2) y.state = Goal::State::Done;
            const auto c = buildings();
            b.standing(Goal::Kind::Buildings, c, 10, 2);
            b.timeLimit(4);
            break;
        }
        default:
            if (mapId >= 0x30 && mapId <= 0x43) {
                static constexpr std::array<int, 8> kCounts{1, 1, 3, 0, 3, 4, 5, 6};
                const std::int32_t pick = script_.value(2, 0);
                const int need = pick >= 0 && pick < int(kCounts.size()) ? kCounts[std::size_t(pick)] : 0;
                if (need > 0) {
                    Goal& x = b.add(Goal::Kind::Defeat, 5);
                    x.need = need;
                    x.have = 0;
                    for (int r = 1; r <= need; ++r) {
                        x.slots.push_back(slotOf(r));
                        x.have += gone(rec(r));
                    }
                    if (x.have == need) x.state = Goal::State::Done;
                }
                int there = 0;
                for (int team = 0; team < 4; ++team) there += script_.tally(1, team).count;
                b.reach(there != 0);
                b.within(18000, 3);
                b.within(0xd2f1, 4, true);
                b.noneFall(true);
                break;
            }
            return out;                              // no list: the script ends it, or network play
    }
    out.known = true;
    for (const Goal& x : g)
        if (x.state == Goal::State::Failed) out.best = std::max(out.best, x.drop);
    return out;
}

} // namespace ch
