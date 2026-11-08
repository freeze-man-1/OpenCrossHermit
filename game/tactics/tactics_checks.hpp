// ============================================================================
// tactics_checks.hpp — the per-map end checks (FUN_00432100 and the
// FUN_004326d0..FUN_004373a0 it dispatches to, src\yoshida)
//
// Once a frame, while no event plays (FUN_00454c40) and the sortie is over
// (+0x108b48), the battle asks its map's check whether the stage is won or
// lost. A check reads the units, the battle clock, the area tally and the
// stage script's values, and ends the battle by playing one of the script's
// result chunks (FUN_00454ab0 / FUN_00454af0 / FUN_00454bb0): chunk 2..4
// rank 1, 5..7 rank 2, 8..10 rank 3, 11..13 rank 4, 14..16 rank 5
// (DAT_0060ca7c). Some checks also start mid-battle events or bring in
// reinforcements.
//
// Ported: the story maps. Not ported: the versus scoring of the network maps
// (FUN_004313f0 for 0x30..0x44, 0x47..0x81, 199; FUN_00456b20 is false in
// the story) and the network message of FUN_00480040.
// ============================================================================
#pragma once
#include "tactics_map.hpp"
#include "tactics_script.hpp"
#include "tactics_units.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace ch {

// +0x2e6f4..+0x2e6fc (FUN_00467aa0 / FUN_00467b60): frames.
struct BattleClock {
    std::int32_t limit = 0x7fffffff;   // +0x2e6fc, 0x7fffffff: none
    std::int32_t left = 0x7fffffff;    // +0x2e6f4
    std::int32_t elapsed = 0;          // +0x2e6f8
    void start(std::int32_t frames) noexcept { limit = left = frames; elapsed = 0; }
    void tick() noexcept {
        ++elapsed;
        if (limit != 0x7fffffff && left > 0) --left;
    }
};

// The port's own (no original): what a story stage's best result asks
// for, worded from its check below, for the pause menu's list
// (TacticsTask::drawGoals). Each goal is worked out from the same tests
// the check makes, so the list and the result agree.
struct Goal {
    enum class Kind : std::uint8_t {
        Objective,     // the stage's own objective (a script flag; Space shows it)
        AllEnemies,    // have: enemies still on the field
        Defeat,        // slots: have of need fallen
        Side,          // slots: have of need ours (take / win over / keep)
        Protect,       // slots: none of them may fall
        NoneFall,      // no named character of ours falls
        Buildings, Bases, Gates, EnemyGates,   // have of need ours standing (enemy gates: destroyed)
        Within,        // won before `frames`; left: what is left of it
        TimeLimit,     // the stage's clock
        HoldOut,       // the battle lasts `frames`
        Hold,          // what is taken is kept for `frames` (left: of it)
        Occupy,        // have of need points held
        Leak,          // have: enemies that broke through (none for PERFECT)
        Escort,        // slots: have of need at the goal
        Reach,         // a unit of ours at the goal
        ReachAfter,    // the same, once `frames` have gone by
    };
    enum class State : std::uint8_t { Open, Done, Failed };
    Kind               kind = Kind::Objective;
    State              state = State::Open;
    std::vector<int>   slots;
    int                have = -1, need = -1;
    std::int32_t       frames = 0, left = -1;
    int                drop = 5;           // the best rank left once it has failed
    bool               take = false;       // Side, Protect: buildings and the like (taken / destroyed)
    bool               keep = false;       // Side, Buildings, Bases, Gates: ours from the start (kept, not taken)
    bool               collect = false;    // Side: treasure chests (collected)
    bool               foe = false;        // Protect: on the other side (not to be defeated)
    bool               hidden = false;     // only a lower rank's: counts for `best`, not listed
};
struct Goals {
    std::vector<Goal> list;
    int               best = 1;            // the best rank still to be had (1 PERFECT .. 5)
    bool              known = false;       // the stage has a list
};

class StageChecks {
public:
    StageChecks(TacticsUnits& units, StageScript& script, const TacticsMap& map, const BattleClock& clock)
        : units_(units), script_(script), map_(map), clock_(clock) {}

    // FUN_00432100. Returns false for a map with no ported check.
    bool run(int mapId);
    // +0x117c38 -> +0x174: the result rank the ending chunk set (-1: none).
    [[nodiscard]] int rank() const noexcept { return rank_; }
    // The pause menu's list for this map (the port's own).
    [[nodiscard]] Goals goals(int mapId) const;

private:
    using Unit = TacticsUnits::Unit;
    using List = std::initializer_list<int>;   // record numbers (DAT_005ff7xx, -1 ended)

    bool dispatch(int mapId);

    // The per-map checks.
    void map1();    // FUN_00432b70 (also 0x4e)
    void map2();    // FUN_00433040 (also 0x4f)
    void map3();    // FUN_00433130
    bool map3Talk();   // FUN_004335b0
    void map4();    // FUN_00433720
    void map5();    // FUN_004337f0
    void map6();    // FUN_00433970
    void map7();    // FUN_00433a50 (also 0x4c)
    void map8();    // FUN_00433bd0
    void map9();    // FUN_00433d00 (also 0x4d)
    void map10();   // FUN_00433f70
    void map11();   // FUN_00434060
    void map13();   // FUN_00434300
    void map14();   // FUN_004345b0 / FUN_00434690 (0xe, 0xf)
    void map16();   // FUN_00434770
    void map17();   // FUN_00434850
    void map18();   // FUN_00434e90
    void map20();   // FUN_00435080
    void map21();   // FUN_00435190
    void map22();   // FUN_004352f0
    void map24();   // FUN_00435410
    void map29();   // FUN_004355a0
    void map31();   // FUN_004356b0
    void map32();   // FUN_00435850
    void map33();   // FUN_004359c0
    void map35();   // FUN_00435aa0
    void map37();   // FUN_00435b70 (0x25..0x27)
    void map40();   // FUN_00435c30 / FUN_00436460 (0x28, 0x44)
    void map41();   // FUN_00435cd0
    void map42();   // FUN_00435dc0
    void map43();   // FUN_00435f10
    void map44();   // FUN_004360b0
    void map45();   // FUN_004361a0
    void map48();   // FUN_004362e0 (0x30..0x43)
    void map80();   // FUN_00436500
    void map82();   // FUN_004327e0 (0x52, 0x53)
    void map150();  // FUN_00436a20
    void map151();  // FUN_00436b40
    void map152();  // FUN_00436c80
    void map153();  // FUN_00436de0
    void map154();  // FUN_004370d0
    void win();     // the common "no named member fell: 2, else 5"
    void versus();  // FUN_004368b0 (0x47, 0x48, 100..0x81): the network maps' scoring
    void coop199(); // FUN_004373a0 (199)

    // Ending the battle.
    void end(int chunk) { end2(chunk, chunk); }                 // FUN_00454ab0
    void end2(int rankChunk, int chunk);                         // FUN_00454af0
    void endIf(int chunk, int rank);                             // FUN_00454bb0(1, 0, chunk); +0x174 = rank
    void endEvent(int rank);                                     // FUN_004549d0(0, 0, 1, 0, 0, 1)
    bool playIf(int chunk, bool ends, int rank);                 // check(0, chunk) then its event
    void event(int chunk) { script_.startEvent(0, chunk, false, true, false, true); }
    void reinforce();                                            // maps 9, 0x52 and 0x99

    // Units (slot = 0xfa - record, FUN_00431cc0 / FUN_00431d40).
    [[nodiscard]] static int slotOf(int record) noexcept { return 0xfa - record; }
    [[nodiscard]] const Unit& rec(int record) const { return units_.unit(slotOf(record)); }
    [[nodiscard]] static int at(const Unit& u) { return u.data.u8(0xf); }
    [[nodiscard]] static bool own(const Unit& u) { return u.used && u.team() == 0; }                   // FUN_0046a430
    [[nodiscard]] static bool dead(const Unit& u) { return u.used && at(u) == 4; }                     // FUN_00469170
    [[nodiscard]] static bool gone(const Unit& u) { return u.used && at(u) >= 2 && at(u) <= 4; }       // FUN_00469220
    [[nodiscard]] static bool on(const Unit& u) { return u.used && at(u) == 1; }                       // FUN_00469480
    [[nodiscard]] static bool off(const Unit& u) { const int a = at(u); return a == 0 || (a >= 2 && a <= 5); }   // FUN_004695d0
    [[nodiscard]] static bool named(const Unit& u) { const int id = u.data.u16(2); return id >= 1 && id <= 0x2d; }   // FUN_00468e10
    [[nodiscard]] bool hostile(const Unit& u) const;                                                  // FUN_0046a550
    [[nodiscard]] bool ally(const Unit& u) const;                                                     // FUN_0046a5e0
    [[nodiscard]] int ownCount(List l) const;            // FUN_00431dd0
    [[nodiscard]] int goneCount(List l) const;           // FUN_00432040
    [[nodiscard]] int hostileCount(List l) const;        // FUN_00431f50
    [[nodiscard]] int humanCount(List l) const;          // FUN_00431e90
    [[nodiscard]] bool partyGone() const;                // FUN_00430df0 (FUN_00430f60 in the story)
    [[nodiscard]] bool perfect() const;                  // FUN_00430ba0: no named member of ours fell
    [[nodiscard]] bool noFalls() const;                  // FUN_00430c30: nor of any player's
    [[nodiscard]] bool allClear() const;                 // FUN_004307b0
    [[nodiscard]] int enemiesLeft() const;               // FUN_00430d40
    [[nodiscard]] bool teamOut() const;                  // FUN_004313f0 + FUN_00431890, the party only
    // FUN_00430fe0 (+0x115cba): [0..4] houses, [5..9] towers, [10..14] both,
    // [15..19] bases, [20..24] gates; each: ours standing, theirs standing,
    // ours gone, theirs gone, all.
    [[nodiscard]] std::array<int, 25> buildings() const;

    TacticsUnits&      units_;
    StageScript&       script_;
    const TacticsMap&  map_;
    const BattleClock& clock_;
    int                rank_ = -1;
    // Who was ours when the battle began (the goals word "win over" / "keep" by it).
    bool               started_ = false;
    std::array<bool, TacticsUnits::kSlots> startOwn_{};
    std::array<int, 25> startBuildings_{};
};

} // namespace ch
