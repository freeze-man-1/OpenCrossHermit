// ============================================================================
// tactics_checks.cpp — the per-map end checks (see tactics_checks.hpp)
// ============================================================================
#include "tactics_checks.hpp"

#include <SDL.h>

namespace ch {

namespace {
// DAT_0060ca7c: the rank of a result chunk (FUN_00454af0).
constexpr std::array<int, 17> kRank{6, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 5};

int rankOf(int chunk) { return chunk >= 0 && chunk < int(kRank.size()) ? kRank[std::size_t(chunk)] : 5; }

bool human(const TacticsUnits::Unit& u) { return u.used && u.human(); }            // FUN_00468d80
bool gate(const TacticsUnits::Unit& u) { const int k = u.data.u16(2); return k >= 0x19b && k <= 0x19e; }   // FUN_00469b90
bool buildingA(const TacticsUnits::Unit& u) {                                     // FUN_00469eb0
    const int k = u.data.u16(2);
    return (k >= 0x183 && k <= 0x188) || (k >= 0x18f && k <= 0x194);
}
bool buildingB(const TacticsUnits::Unit& u) { const int k = u.data.u16(2); return k >= 0x189 && k <= 0x18e; }   // FUN_00469f90
}

bool StageChecks::run(int mapId) {
    if (!started_) {
        started_ = true;
        for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) startOwn_[std::size_t(slot)] = own(units_.unit(slot));
        startBuildings_ = buildings();
        if (SDL_getenv("CH_GOALS")) {
            for (int r = 1; r < 0x70; ++r) {
                const Unit& u = rec(r);
                if (!u.used) continue;
                SDL_Log("[goals] map %d rec 0x%x slot %d kind %d team %d at %d hostile %d '%s'", mapId, r, u.slot,
                        u.data.u16(2), u.team(), at(u), hostile(u), units_.name(u).c_str());
            }
        }
    }
    const int before = rank_;
    const bool known = dispatch(mapId);
    if (rank_ != before && SDL_getenv("CH_SCRIPT")) {
        int named_ = 0, offField = 0;
        for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
            const Unit& u = units_.unit(slot);
            if (!own(u) || !named(u)) continue;
            ++named_;
            offField += off(u);
            SDL_Log("[checks]   slot %d id %d at %d", slot, u.data.u16(2), at(u));
        }
        SDL_Log("[checks] map %d: rank %d at frame %d, time left %d, named %d off %d", mapId, rank_,
                clock_.elapsed, clock_.left, named_, offField);
    }
    return known;
}

bool StageChecks::dispatch(int mapId) {
    switch (mapId) {
        case 0: case 0x54: case 0x55: case 0x56: case 0x62:
        case 0x180: case 0x181: case 0x182: case 0x186: case 0x18e: case 399:
            return true;                             // no check: the script ends these
        case 1: case 0x4e: map1(); return true;
        case 2: case 0x4f: map2(); return true;
        case 3: map3(); return true;
        case 4: map4(); return true;
        case 5: map5(); return true;
        case 6: map6(); return true;
        case 7: case 0x4c: map7(); return true;
        case 8: map8(); return true;
        case 9: case 0x4d: map9(); return true;
        case 10: map10(); return true;
        case 0xb: map11(); return true;
        case 0xd: map13(); return true;
        case 0xe: case 0xf: map14(); return true;
        case 0x10: map16(); return true;
        case 0x11: map17(); return true;
        case 0x12: map18(); return true;
        case 0x14: map20(); return true;
        case 0x15: map21(); return true;
        case 0x16: map22(); return true;
        case 0x18: map24(); return true;
        case 0x1d: map29(); return true;
        case 0x1f: map31(); return true;
        case 0x20: map32(); return true;
        case 0x21: map33(); return true;
        case 0x23: map35(); return true;
        case 0x25: case 0x26: case 0x27: map37(); return true;
        case 0x28: case 0x44: map40(); return true;
        case 0x29: map41(); return true;
        case 0x2a: map42(); return true;
        case 0x2b: map43(); return true;
        case 0x2c: map44(); return true;
        case 0x2d: map45(); return true;
        case 0x50: map80(); return true;
        case 0x52: case 0x53: map82(); return true;
        case 0x96: map150(); return true;
        case 0x97: map151(); return true;
        case 0x98: map152(); return true;
        case 0x99: map153(); return true;
        case 0x9a: map154(); return true;
        case 0x47: case 0x48: versus(); return true;
        case 199: coop199(); return true;
        default:
            if (mapId >= 0x30 && mapId <= 0x43) { map48(); return true; }
            if (mapId >= 100 && mapId <= 0x81) { versus(); return true; }   // (FUN_00432100 cases 100..0x81)
            return false;                            // the other network maps (FUN_00437280, FUN_004373a0)
    }
}

// --------------------------------------------------------------- ending ---
void StageChecks::end2(int rankChunk, int chunk) {
    script_.runIf(true, 0, chunk);
    rank_ = rankOf(rankChunk);
}

void StageChecks::endIf(int chunk, int rank) {
    script_.runIf(true, 0, chunk);
    rank_ = rank;
}

// FUN_004368b0 (the versus maps 0x47, 0x48, 100..0x6f): FUN_004313f0
// counts each team's fighting members (FUN_00468e10) standing on the field
// (+0x1e of the team's 0x32-byte tally) and adds them up by side, a side
// being its first team allied to it (FUN_00431a60). With fewer than two
// sides left (FUN_00431910) ours wins unless it is the one gone; when the
// time runs out (+0x2e6f4 0) the best tally wins, a shared best draws
// (FUN_00431990: 0 rank 1, 1 rank 5, 2 rank 3).
void StageChecks::versus() {
    std::array<int, 16> team{};
    for (int i = 0; i < 0xfa; ++i) {
        const Unit& u = units_.unit(i);
        const int kind = u.data.u16(2);
        if (u.used && u.onField() && kind >= 1 && kind <= 0x2d) ++team[std::size_t(u.team() & 0xf)];
    }
    const auto sideOf = [&](int t) {
        for (int c = 0; c < 4; ++c) if (units_.rawRelation(t, c) == 0) return c;
        return t & 3;
    };
    std::array<int, 4> side{};
    for (int t = 0; t < 4; ++t) side[std::size_t(sideOf(t))] += team[std::size_t(t)];
    const int me = sideOf(units_.viewer());
    int alive = 0;
    for (const int s : side) alive += s != 0;
    if (alive < 2) {
        endEvent(side[std::size_t(me)] == 0 ? 5 : 1);
        return;
    }
    if (clock_.left != 0) return;
    int best = 0, steps = 0;
    for (const int s : side) {
        if (best <= s) {
            best = s;
            ++steps;
        }
    }
    endEvent(side[std::size_t(me)] != best ? 5 : steps < 2 ? 1 : 3);
}

// FUN_004373a0 (map 199, the co-operative debug scenario; FUN_00437280 is
// its twin that nothing calls): the players win once team 5 has neither
// fighters (+0x1e of its FUN_004313f0 tally) nor other units (+0x24, those
// not buildings, bases or gates) on the field; they lose when one player
// present (DAT_007f44d4) has no fighter left (FUN_00431890) or the time is up.
void StageChecks::coop199() {
    int enemyFighters = 0, enemyOthers = 0;
    std::array<int, 4> mine{};
    for (int i = 0; i < 0xfa; ++i) {
        const Unit& u = units_.unit(i);
        if (!u.used || !u.onField()) continue;
        const int kind = u.data.u16(2);
        const bool fighter = kind >= 1 && kind <= 0x2d;
        if (u.team() == 5) {
            if (fighter) ++enemyFighters;
            else if (!TacticsUnits::isBase(u) && !TacticsUnits::isStructure(u) && !(kind >= 0x19b && kind <= 0x19e)) ++enemyOthers;
        } else if (u.team() < 4 && fighter) {
            ++mine[std::size_t(u.team())];
        }
    }
    if (enemyFighters == 0 && enemyOthers == 0) {
        endEvent(1);
        return;
    }
    bool gone = false;
    for (int s = 0; s < 4; ++s)
        if (units_.work().u8(0x7f44d4 + std::uint32_t(s)) != 0 && mine[std::size_t(s)] == 0) gone = true;
    if (gone || clock_.left == 0) endEvent(5);
}

void StageChecks::endEvent(int rank) {
    rank_ = rank;
    script_.startEvent(0, 0, true, false, false, true);
}

// FUN_004551c0 then FUN_004549d0(0, chunk, ends, 0, 1, 1).
bool StageChecks::playIf(int chunk, bool ends, int rank) {
    if (!script_.check(0, chunk)) return false;
    script_.startEvent(0, chunk, ends, true, false, true);
    if (rank >= 0) rank_ = rank;
    return true;
}

void StageChecks::win() {
    if (perfect()) end(2);
    else end(5);
}

// Maps 9, 0x52 and 0x99: three groups come in and groups 1 and 2 change
// their orders (FUN_0047fc60 / FUN_0047f4b0).
void StageChecks::reinforce() {
    units_.appearGroup(9, 0x34, 0x55, 5);
    units_.appearGroup(10, 2, 3, 5);
    units_.appearGroup(0xb, 3, 0x5b, 5);
    for (const int g : {1, 2}) {
        units_.setGroupField(g, 0, 0);
        units_.setGroupField(g, 1, 0);
        units_.setGroupField(g, 2, 2);
        units_.setGroupField(g, 3, 1);
    }
}

// ---------------------------------------------------------------- units ---
bool StageChecks::hostile(const Unit& u) const {
    if (!u.used) return false;
    const int r = units_.relationTo(u);
    return r >= 1 && r <= 2;
}

bool StageChecks::ally(const Unit& u) const {
    return u.used && !own(u) && units_.relationTo(u) == 4;
}

int StageChecks::ownCount(List l) const {
    int n = 0;
    for (const int r : l) n += own(rec(r));
    return n;
}

int StageChecks::goneCount(List l) const {
    int n = 0;
    for (const int r : l) n += gone(rec(r));
    return n;
}

int StageChecks::hostileCount(List l) const {
    int n = 0;
    for (const int r : l) n += hostile(rec(r));
    return n;
}

int StageChecks::humanCount(List l) const {
    int n = 0;
    for (const int r : l) n += human(rec(r));
    return n;
}

// Every named member of ours (FUN_0046a4c0) is off the field; true at once
// when the side has none.
bool StageChecks::partyGone() const {
    int named_ = 0, offField = 0;
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        const Unit& u = units_.unit(slot);
        if (!own(u) || !named(u)) continue;
        ++named_;
        offField += off(u);
    }
    return named_ == offField;
}

// +0x2ef34[team] counts the named members that fell (FUN_00430b20); here,
// the named members that are gone now.
bool StageChecks::perfect() const {
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        const Unit& u = units_.unit(slot);
        if (own(u) && named(u) && gone(u)) return false;
    }
    return true;
}

bool StageChecks::noFalls() const {
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        const Unit& u = units_.unit(slot);
        if (human(u) && named(u) && gone(u)) return false;
    }
    return true;
}

// No enemy is left on the field or waiting to come in (+0xf 0), apart from
// the buildings, gates and bases and the units that never come (+0x96 3).
bool StageChecks::allClear() const {
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        const Unit& u = units_.unit(slot);
        if (!hostile(u) || TacticsUnits::isStructure(u) || gate(u) || TacticsUnits::isBase(u)) continue;
        if (on(u)) return false;
        if (off(u) && u.data.u8(0x96) != 3 && at(u) == 0) return false;
    }
    return true;
}

int StageChecks::enemiesLeft() const {
    int n = 0;
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        const Unit& u = units_.unit(slot);
        n += on(u) && hostile(u) && !TacticsUnits::isStructure(u);
    }
    return n;
}

// FUN_00431890: a side in the battle (DAT_007f44d4, only ours in the story)
// has no named member on the field.
bool StageChecks::teamOut() const {
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        const Unit& u = units_.unit(slot);
        if (own(u) && named(u) && on(u)) return false;
    }
    return true;
}

std::array<int, 25> StageChecks::buildings() const {
    std::array<int, 25> c{};
    auto add = [&](int base, const Unit& u) {
        const bool ours = own(u) || ally(u);
        ++c[std::size_t(base + (ours ? 0 : 1) + (gone(u) ? 2 : 0))];
        ++c[std::size_t(base + 4)];
    };
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        const Unit& u = units_.unit(slot);
        if (!u.used) continue;
        if (TacticsUnits::isStructure(u)) {
            if (buildingA(u)) add(0, u);
            if (buildingB(u)) add(5, u);
        }
        if (TacticsUnits::isBase(u)) add(15, u);
        if (gate(u)) add(20, u);
    }
    for (std::size_t k = 0; k < 5; ++k) c[10 + k] = c[k] + c[5 + k];
    return c;
}

// ----------------------------------------------------------------- maps ---
void StageChecks::map1() {
    StageScript& s = script_;
    static constexpr int kLeaders[3] = {1, 2, 3};        // DAT_005ff754
    if (s.value(3, 9) == 0) {
        for (const int r : kLeaders) {
            const Unit& u = rec(r);
            if (on(u) && u.data.s16(0x16) < 200) {
                s.setValue(1, 0, u.slot);
                s.runIf(false, 0, 0x1d);
                s.setValue(3, 9, 1);
                return;
            }
        }
    }
    const Unit& a = units_.unit(0xf5);                   // +0xcf28c / +0xced6c
    const Unit& b = units_.unit(0xf4);
    const bool ownA = own(a), ownB = own(b);
    s.setValue(2, 0, ownA && ownB);
    int down = 0;
    for (const int r : kLeaders) down += TacticsUnits::isDown(rec(r)) && off(rec(r));   // FUN_004690a0
    if (down != 0 && s.value(3, 0) != 0 && s.check(0, 0x18)) {
        event(0x18);
        s.setValue(3, 0, 0);
        s.setValue(3, 1, 0);
        return;
    }
    if (s.value(3, 2) == 0 && own(units_.unit(0xf6)) && s.check(0, 0x19)) {   // +0xcf7ac
        event(0x19);
        return;
    }
    if (s.value(3, 3) == 0 && (ownA || ownB)) {
        s.setValue(1, 0, ownA ? a.slot : b.slot);
        if (s.check(0, 0x1a)) {
            event(0x1a);
            return;
        }
    }
    if (partyGone()) {
        playIf(0xe, true, 5);
        return;
    }
    if (s.value(3, 7) == 0 || enemiesLeft() != 0) return;
    if (s.value(2, 1) != 0) {
        endIf(0xb, 4);
        return;
    }
    const auto c = buildings();
    const bool houses = c[10] == c[14], gates = c[20] == c[24];
    if (perfect() && houses && gates) endIf(2, 1);
    else if (houses && gates) endIf(8, 2);
    else endIf(0xb, 3);
}

void StageChecks::map2() {
    if (script_.value(3, 0) == 0) {
        if (clock_.left == 0) end2(0xb, 0xe);
        else if (partyGone()) end2(0xe, 0xe);
    } else if (ownCount({1, 2, 3}) == 3) {
        end2(perfect() ? 2 : 5, 5);
    } else {
        end2(8, 5);
    }
}

// Three leaders (records 2..4) can be talked over (value 3/k) and can turn
// back (chunk 0x17); value 2/0 counts the talks (chunks 0x14..0x16).
void StageChecks::map3() {
    StageScript& s = script_;
    static constexpr int kLeaders[3] = {2, 3, 4};        // DAT_005ff760
    const int turned = ownCount({2, 3, 4});
    for (int k = 0; k < 3; ++k) {
        const Unit& u = rec(kLeaders[k]);
        if (s.value(3, std::uint32_t(k)) == 0 || !hostile(u)) continue;
        s.setValue(3, std::uint32_t(k), 0);
        s.setValue(1, 0, u.slot);
        s.setValue(2, 0, s.value(2, 0) - 1);
        s.runIf(false, 0, 0x17);
    }
    for (int k = 0; k < 3; ++k) {
        const Unit& u = rec(kLeaders[k]);
        if (own(u) && s.value(3, std::uint32_t(k)) == 0 && map3Talk()) {
            s.setValue(3, std::uint32_t(k), 1);
            s.setValue(1, 0, u.slot);
            return;
        }
    }
    if (turned > 2 && allClear()) {
        if (s.value(2, 0) == 3) {
            s.setValue(1, 0, 0xff);
        } else {
            int k = 2;
            if (s.value(3, 0) == 0) k = 0;
            else if (s.value(3, 1) == 0) k = 1;
            s.setValue(1, 0, rec(kLeaders[k]).slot);
        }
        end2(perfect() ? 2 : 5, 5);
    }
    if (clock_.left == 0) {
        if (turned < 3 && !allClear()) end(0xb);
        else end2(8, 5);
    } else if (partyGone()) {
        end(0xe);
    }
}

bool StageChecks::map3Talk() {
    const std::int32_t n = script_.value(2, 0);
    const int chunk = n == 0 ? 0x14 : n == 1 ? 0x15 : n == 2 && !allClear() ? 0x16 : 0;
    if (chunk == 0 || !script_.check(0, chunk)) return false;
    event(chunk);
    script_.setValue(2, 0, n + 1);
    return true;
}

void StageChecks::map4() {
    if (!allClear()) {
        if (clock_.left == 0) end(0xb);
        else if (partyGone()) end(0xe);
    } else if (clock_.elapsed < 0x6271) {
        win();
    } else {
        end(8);
    }
}

void StageChecks::map5() {
    StageScript& s = script_;
    if (own(rec(3)) && s.value(3, 0) == 0) {
        s.runIf(false, 0, 0x14);
        s.setValue(3, 0, 1);
        return;
    }
    if (dead(rec(0xb)) && ownCount({2, 3}) >= 2) {
        win();
    } else if (clock_.left == 0) {
        if (s.value(3, 0) == 0) end2(0xb, 0xb);
        else end2(8, 8);
    } else if (partyGone()) {
        if (s.value(3, 0) == 0) end2(0xe, 0xe);
        else end2(0xe, 8);
    }
}

void StageChecks::map6() {
    if (!dead(rec(5))) {
        if (partyGone()) end(0xb);
    } else if (!allClear() || ownCount({1, 2, 3, 4}) != 4) {
        end(8);
    } else {
        win();
    }
}

void StageChecks::map7() {
    // Records 4 / 5 joining call off the appear timers 1..3 / 4..6
    // (FUN_00498ce0).
    if (own(rec(4))) for (const int t : {1, 2, 3}) script_.clearAppear(t);
    if (own(rec(5))) for (const int t : {4, 5, 6}) script_.clearAppear(t);
    if (script_.value(3, 0) != 0) {
        if (ownCount({4, 5}) == 2) win();
        else end(8);
    }
    if (!partyGone()) {
        if (dead(rec(2))) end(0xe);
    } else {
        end(0xb);
    }
}

void StageChecks::map8() {
    if (clock_.elapsed < 0x1a5e0) {
        if (hostileCount({3, 4, 5}) == 3) end(0xf);
        else if (partyGone()) end(0xe);
        return;
    }
    const int a = ownCount({3, 4, 5}), b = ownCount({6}), c = ownCount({1, 2});
    if (a == 3 && b == 1 && c == 2) win();
    else if (a == 3) end(8);
    else end(0xb);
}

void StageChecks::map9() {
    StageScript& s = script_;
    const Unit& a = rec(0x2d);
    const Unit& b = rec(0x2e);
    if (!own(a) || !own(b)) {
        if ((gone(a) || gone(b)) && s.value(3, 0) == 0) {
            end(0xc);
            return;
        }
    } else if (!gone(a) && !gone(b) && s.value(3, 0) == 0) {
        s.setValue(3, 0, 1);
        s.runIf(false, 0, 0x14);
        reinforce();
        return;
    }
    if (!dead(rec(0x2f))) {
        if (partyGone()) end(0xe);
        else if (clock_.left == 0) end(0xb);
    } else if (buildings()[10] == 4) {
        win();
    } else {
        end(8);
    }
}

void StageChecks::map10() {
    if (script_.value(3, 0) != 0) {
        if (ownCount({0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30}) == 6) win();
        else end(8);
    }
    if (!partyGone()) {
        if (dead(rec(1))) end(0xe);
    } else {
        end(0xb);
    }
}

void StageChecks::map11() {
    StageScript& s = script_;
    if (s.value(3, 0) == 0 && dead(units_.unit(0xf3))) {   // +0xce84c
        s.check(0, 0x1b);
        return;
    }
    if (clock_.left == 0) {
        const std::int32_t n = s.value(2, 0);
        if (n == 0) playIf(2, true, 1);
        else if (n >= 1 && n <= 3) playIf(5, true, 2);
        else if (n >= 4 && n <= 9) playIf(8, true, 3);
    }
    if (s.value(2, 0) < 10) {
        if (partyGone()) playIf(0xe, true, 5);
    } else {
        playIf(0xb, true, 4);
    }
}

// Areas 4..9 held by the party (with the right job classes in 4..6) for
// 300 frames.
void StageChecks::map13() {
    StageScript& s = script_;
    bool held = false;
    if (s.value(3, 0) != 0 && s.value(3, 1) != 0 && s.value(3, 2) != 0) {
        auto in = [&](int area, int classes) {
            const StageScript::Tally& t = s.tally(area, 0);
            return t.count != 0 && (classes == 0 || (t.classes & classes) != 0);
        };
        const int n = in(4, 3) + in(5, 0x18) + in(6, 0x18) + in(7, 0) + in(8, 0) + in(9, 0);
        if (n < 6) {
            s.setValue(0, 0, 0);
        } else {
            const std::int32_t t = s.value(0, 0) + 1;
            if (t < 300) s.setValue(0, 0, t);
            else held = true;
        }
    }
    if (!held) {
        if (clock_.left == 0) end(0xb);
        else if (partyGone()) end(0xe);
    } else if (gone(rec(1)) + gone(rec(2)) + gone(rec(3)) < 3) {
        end(8);
    } else {
        win();
    }
}

void StageChecks::map14() {
    if (script_.value(3, 0) == 0) {
        if (partyGone()) end(clock_.elapsed < 36000 ? 0xe : 0xb);
    } else if (clock_.elapsed < 0x8ca1) {
        win();
    } else {
        end(8);
    }
}

void StageChecks::map16() {
    const int a = ownCount({1, 2, 3, 4}), b = ownCount({5, 6, 7, 8, 9, 0xa});
    if (a < 4) {
        if (clock_.left == 0) end(0xb);
        else if (partyGone()) end(0xe);
    } else if (b < 6) {
        end(8);
    } else {
        win();
    }
}

// Two guards falling bring a group in at one of four places (DAT_005ff7d4);
// a unit of ours reaching area 1 ends the stage.
void StageChecks::map17() {
    StageScript& s = script_;
    static constexpr std::array<std::array<int, 4>, 4> kSpots{{{34, 18, 3, 5}, {69, 91, 3, 7}, {30, 124, 3, 10}, {18, 42, 3, 13}}};
    auto call = [&](int slot, std::uint32_t flag, int group) {
        if (s.value(3, flag) != 0 || !dead(units_.unit(slot))) return;
        const auto& p = kSpots[std::size_t(units_.random() % 4)];
        units_.appearGroup(group, p[0], p[1], p[2]);
        units_.setGroupField(group, 2, p[3]);
        units_.setGroupField(group, 1, 0);
        units_.setGroupField(group, 3, 1);
        s.setValue(3, flag, 1);
    };
    call(0xcd, 0, 0xe);                                  // +0xc258c
    call(0xcc, 1, 0xf);                                  // +0xc206c
    if (s.tally(1, 0).count > 0) {
        if (clock_.elapsed < 54000) {
            endIf(0xe, 5);
            return;
        }
        const auto c = buildings();
        const bool houses = c[10] == c[14], bases = c[15] == c[19], gates = c[23] == c[24];
        if (houses && bases && gates && perfect()) endIf(2, 1);
        else if (houses && bases && gates) endIf(5, 2);
        else if (!houses && !bases && !gates) endIf(0xb, 4);
        else endIf(8, 3);
    }
    if (partyGone()) endIf(0xe, 5);
}

void StageChecks::map18() {
    StageScript& s = script_;
    if (gone(rec(4)) || gone(rec(5)) || gone(rec(6))) s.setValue(3, 0, 1);
    if (ownCount({1, 2, 3}) == 3) {
        if (allClear()) win();
        else end(8);
        return;
    }
    int fell = 0;
    for (const int r : {1, 2, 3})
        if (gone(rec(r))) { fell = r; break; }
    if (fell == 0) {
        if (partyGone()) end(0xe);
        return;
    }
    s.setValue(1, 0, slotOf(fell));
    end(0xb);
}

void StageChecks::map20() {
    const int n = ownCount({1, 2, 3, 4, 5});
    if (!dead(rec(6))) {
        if (partyGone()) end(clock_.elapsed < 36000 ? 0xe : 0xb);
    } else if (n == 5 && goneCount({1, 2, 3, 4, 5}) == 0) {
        win();
    } else {
        end(8);
    }
}

void StageChecks::map21() {
    StageScript& s = script_;
    const int a = ownCount({1, 2, 3, 4});
    const int b = ownCount({5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11});
    bool held = false;
    if (a < 2 || b < 4) {
        s.setValue(0, 0, 0);
        s.setValue(3, 0, 0);
    } else {
        const std::int32_t t = s.value(0, 0) + 1;
        if (t < 54000) s.setValue(0, 0, t);
        else held = true;
        s.setValue(3, 0, 1);
    }
    if (!held) {
        if (partyGone()) end(clock_.elapsed < 54000 ? 0xe : 0xb);
    } else if (a < 4 || b < 0xd) {
        end(8);
    } else {
        win();
    }
}

void StageChecks::map22() {
    if (!allClear()) {
        if (clock_.left == 0) {
            if (ownCount({2}) == 1) end(perfect() ? 3 : 6);
            else end(0xb);
        } else if (partyGone()) {
            end(0xe);
        }
    } else if (clock_.elapsed < 0x4651) {
        win();
    } else {
        end(8);
    }
}

void StageChecks::map24() {
    StageScript& s = script_;
    // The original reads value type 0 / 0 here (FUN_004e2760(3, 0, 0)), not
    // flag 3/0 as the other checks do.
    if (s.value(0, 0) != 0 && dead(rec(0x38))) {
        s.setValue(1, 0, dead(rec(0x39)) ? 0x38 : 0x39);
        s.setValue(3, 0, 1);
    }
    if (dead(rec(0x38)) && dead(rec(0x39))) {
        if (ownCount({2, 3, 4, 5, 6}) != 5) end(8);
        else win();
        return;
    }
    if (!dead(rec(9)) && !dead(rec(10))) {
        if (partyGone()) end(0xe);
        return;
    }
    end(0xb);
}

void StageChecks::map29() {
    const List kAll{1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
    if (goneCount(kAll) == 0) {
        if (ownCount(kAll) == 0xf) {
            if (goneCount({0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d}) != 0) {
                end(gone(rec(0x13)) ? 0xb : 8);
                return;
            }
            win();
            return;
        }
        if (!partyGone()) return;
    }
    end(0xe);
}

// Records 0xd..0xf must reach area 1 (the map's own area field).
void StageChecks::map31() {
    const int fell = goneCount({0xd, 0xe, 0xf});
    int there = 0;
    for (const int r : {0xd, 0xe, 0xf}) {
        const Unit& u = rec(r);
        if (on(u) && map_.cell(u.cx(), u.cy(), 2) == 1) ++there;   // FUN_0043bb90
    }
    switch (fell) {
        case 0: if (there == 3) { win(); return; } break;
        case 1: case 2: if (3 - fell == there) { end(8); return; } break;
        case 3: end(0xb); return;
        default: break;
    }
    if (partyGone()) end(0xe);
}

void StageChecks::map32() {
    static constexpr int kRecs[4] = {3, 4, 5, 6};
    const int n = ownCount({3, 4, 5, 6});
    if (n == 1) {
        for (const int r : kRecs)
            if (own(rec(r))) script_.setValue(1, 0, rec(r).slot);
    }
    if (n == 0) {
        end(0xb);
        return;
    }
    if (clock_.left == 0) {
        if (n == 4) win();
        else end(8);
    }
    if (partyGone()) end(0xe);
}

void StageChecks::map33() {
    const std::int32_t t = clock_.elapsed;
    if (!allClear()) {
        if (partyGone()) end(0xb);
    } else {
        end(t < 0x8ca1 ? 2 : t < 0xd2f1 ? 5 : t < 0x1a5e1 ? 8 : 0xb);
    }
}

void StageChecks::map35() {
    const std::int32_t t = clock_.elapsed;
    if (allClear()) end(t < 0x2a31 ? 2 : t < 0x4651 ? 5 : t < 0x8ca1 ? 8 : 0xb);
    if (partyGone()) end(0xe);
}

void StageChecks::map37() {
    if (script_.value(3, 0) == 0) {
        if (partyGone()) end(0xe);
    } else if (!allClear()) {
        end(8);
    } else {
        end(noFalls() ? 2 : 5);
    }
}

void StageChecks::map40() {
    if (script_.value(3, 0) == 0) {
        if (partyGone()) end(0xe);
    } else {
        end(noFalls() ? 2 : 5);
    }
}

void StageChecks::map41() {
    if (!dead(rec(1))) {
        if (clock_.left == 0) end(0xb);
        else if (partyGone()) end(0xe);
    } else if (ownCount({2}) == 1) {
        win();
    } else {
        end(8);
    }
}

void StageChecks::map42() {
    if (!dead(rec(1))) {
        if (teamOut() || clock_.left == 0) endEvent(5);
    } else {
        endEvent(noFalls() ? 1 : 2);
    }
}

void StageChecks::map43() {
    if (!dead(rec(1)) || !dead(rec(2)) || !dead(rec(3)) || !dead(rec(4))) {
        if (teamOut() || clock_.left == 0) endEvent(5);
    } else {
        endEvent(noFalls() ? 1 : 2);
    }
}

void StageChecks::map44() {
    if (!dead(rec(1))) {
        if (clock_.left == 0) end(0xb);
        else if (partyGone()) end(0xe);
    } else if (ownCount({2, 3, 0x60}) == 3) {
        win();
    } else {
        end(8);
    }
}

// Record 1 falling brings record 2 in at 12, 27 (FUN_00480040).
void StageChecks::map45() {
    StageScript& s = script_;
    if (s.value(3, 0) == 0) {
        if (dead(rec(1))) {
            s.setValue(3, 0, 1);
            units_.appearUnit(slotOf(2), 0xc, 0x1b, 10);
        }
    } else if (dead(rec(2))) {
        if (ownCount({3, 4, 5, 6, 7, 8, 0x2c, 0x2d}) != 8) end(8);
        else win();
        return;
    }
    if (partyGone()) end(dead(rec(1)) ? 0xb : 0xe);
}

// Maps 0x30..0x43 (t0048): value 2/0 picks how many of records 1.. must
// fall (DAT_005ff8b8), then a player unit must reach area 1.
void StageChecks::map48() {
    static constexpr std::array<int, 8> kCounts{1, 1, 3, 0, 3, 4, 5, 6};
    const std::int32_t pick = script_.value(2, 0);
    const int need = pick >= 0 && pick < int(kCounts.size()) ? kCounts[std::size_t(pick)] : 0;
    int fell = 0;
    for (int r = 1; r <= need; ++r) fell += gone(rec(r));
    if (fell == need) {
        int there = 0;
        for (int team = 0; team < 4; ++team) there += script_.tally(1, team).count;
        if (there != 0) {
            if (clock_.elapsed > 18000) end(clock_.elapsed < 0xd2f1 ? 8 : 0xb);
            else end(noFalls() ? 2 : 5);
            return;
        }
    }
    if (partyGone()) end(0xe);
}

// Map 0x50: five of records 1..5 falling plays chunk 2; five of 6..10 make
// record 0xb change sides (chunk 3), its coming in plays chunk 4, and the
// leader or it leaving the field ends the stage (chunk 5).
void StageChecks::map80() {
    StageScript& s = script_;
    auto deadOf = [&](int first) {
        int n = 0;
        for (int r = first; r < first + 5; ++r) n += dead(rec(r));
        return n;
    };
    if (s.value(3, 0) == 0 && deadOf(1) > 4) {
        s.setValue(3, 0, 1);
        if (s.check(0, 2)) event(2);
    }
    if (s.value(3, 1) == 0 && deadOf(6) > 4) {
        // FUN_0046d970 / FUN_00475140: it joins the player's side under
        // the player's orders.
        Unit& u = units_.unitRef(slotOf(0xb));
        u.data.set8(0xa4, 0);
        u.data.set32(0x20, 0x7fffffff);
        u.data.set32(0x24, 0x7fffffff);
        units_.takeControl(u);
        s.setValue(3, 1, 1);
        if (s.check(0, 3)) event(3);
    }
    if (s.value(3, 1) == 0) return;
    if (s.value(3, 2) == 0) {
        if (on(rec(0xb))) {
            s.setValue(3, 2, 1);
            if (s.check(0, 4)) event(4);
        }
    } else if (s.value(3, 3) == 0 && (off(units_.unit(0)) || off(rec(0xb)))) {
        s.setValue(3, 3, 1);
        playIf(5, true, 0);
    }
}

void StageChecks::map82() {
    StageScript& s = script_;
    const Unit& a = units_.unit(0xcd);                   // +0xc258c
    const Unit& b = units_.unit(0xcc);                   // +0xc206c
    if (!own(a) || !own(b)) {
        if ((gone(a) || gone(b)) && s.value(3, 0) == 0) playIf(5, true, 3);
    } else if (!gone(a) && !gone(b) && s.value(3, 0) == 0) {
        s.setValue(3, 0, 1);
        if (s.check(0, 10)) event(10);
        reinforce();
        return;
    }
    if (!dead(units_.unit(0xcb))) {                      // +0xc1b4c
        if (!partyGone()) {
            if (clock_.left == 0) playIf(6, true, 4);
        } else {
            playIf(2, true, 5);
        }
    } else if (buildings()[10] == 4) {
        playIf(3, true, 1);
    } else {
        playIf(4, true, 2);
    }
}

void StageChecks::map150() {
    const std::int32_t t = clock_.elapsed;
    if (!allClear()) {
        if (partyGone()) endEvent(4);
    } else {
        endEvent(t < 0x8ca1 ? 1 : t < 0xd2f1 ? 2 : t < 0x1a5e1 ? 3 : 4);
    }
}

void StageChecks::map151() {
    const int n = humanCount({3, 4, 5, 6});
    if (n == 0) {
        endEvent(4);
        return;
    }
    if (clock_.left == 0) endEvent(n == 4 ? (noFalls() ? 1 : 2) : 3);
    if (partyGone()) endEvent(5);
}

void StageChecks::map152() {
    if (!dead(rec(1))) {
        if (clock_.left == 0) endEvent(4);
        else if (partyGone()) endEvent(5);
    } else {
        endEvent(humanCount({2}) == 1 ? (noFalls() ? 1 : 2) : 3);
    }
}

void StageChecks::map153() {
    StageScript& s = script_;
    const Unit& a = rec(0x2d);
    const Unit& b = rec(0x2e);
    if (!human(a) || !human(b)) {
        if ((gone(a) || gone(b)) && s.value(3, 0) == 0) {
            endEvent(4);
            return;
        }
    } else if (!gone(a) && !gone(b) && s.value(3, 0) == 0) {
        s.setValue(3, 0, 1);
        reinforce();
        return;
    }
    if (!dead(rec(0x2f))) {
        if (partyGone()) endEvent(5);
        else if (clock_.left == 0) endEvent(4);
    } else {
        endEvent(humanCount({0x2b, 0x2c, 0x2d, 0x2e}) == 4 ? (noFalls() ? 1 : 2) : 3);
    }
}

void StageChecks::map154() {
    StageScript& s = script_;
    const int a = humanCount({1, 2, 3, 4});
    const int b = humanCount({5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11});
    bool held = false;
    if (a < 2 || b < 4) {
        s.setValue(0, 0, 0);
        s.setValue(3, 0, 0);
    } else {
        const std::int32_t t = s.value(0, 0) + 1;
        if (t < 54000) s.setValue(0, 0, t);
        else held = true;
        s.setValue(3, 0, 1);
    }
    if (!held) {
        if (partyGone()) endEvent(clock_.elapsed < 54000 ? 5 : 4);
    } else {
        endEvent(a < 4 || b < 0xd ? 3 : (noFalls() ? 1 : 2));
    }
}

} // namespace ch
