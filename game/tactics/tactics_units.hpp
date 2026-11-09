// ============================================================================
// tactics_units.hpp — the battle's units (src\yoshida unit work, task +0x80aec)
//
// 250 slots of unit work (0x520 bytes each in the original) over 0xb0-byte
// unit data blocks (task +0xd0c2c): the party from DAT_007f4518 in the low
// slots, the map's enemy table from slot 0xf9 downwards.
//   FUN_00452fc0  party data copied in, FUN_00453540 enemies
//   FUN_004da450  enemy record (0x54 bytes, PTR_DAT_006a7f6c[map], count
//                 DAT_006a85ac[map]) -> unit data:
//                 FUN_004da5f0 0x30-byte summary of the template (id < 200:
//                 0x6f5088 records, else 0x72ed88 + 0x30 (id - 200)),
//                 FUN_004dfc20 derived values, FUN_004e0a00 clamps
//   FUN_004da220  comenemy.cpp's static object: FUN_004da260 -> FUN_004da3b0
//                 builds its two arrays of ten 0x80-byte D3D sprites
//                 (FUN_004075e0), FUN_004da2a0 hands FUN_004da2e0 ->
//                 FUN_004da320 to atexit to destroy them. The port's draw
//                 list needs no sprite objects, so there is nothing to keep.
//   FUN_004680b0  create the unit work: facing, AI state, graphics
//   FUN_00467020  graphics: DAT_00610538 + 8 kind {path, file, palette kind,
//                 palette}; one DxAnim set per (file, palette), FUN_00466e90
//   FUN_00465040  play clip `anim` facing `dir` (1..9) through the per-file
//                 tables PTR_DAT_0060e850 / PTR_DAT_0060f070 [file + 8]
//   FUN_00468690  cell -> position (centre, 16.16 fixed point)
//   FUN_0049a310  placement: each party unit to the next free cell near the
//                 start (FUN_0043d660)
//   FUN_0046cbd0  draw: sprite at priority 0x6e8 - y, HP gauge FUN_0046d340
//   FUN_00475700  each unit's state machine (+0x290); ported: 1 appear,
//                 2 stand, 3 hold, 4 walk to an ordered cell, 6 use a skill
//                 on a unit, 0xb off the field, 0xd fall, 0xf the AI's
//                 choice, 0x10 follow the route, 0x13 guard (look for
//                 targets), 0x15 change sides, 0x16 lie in wait, 0x17 wander,
//                 0x18 call in reinforcements, 0x1a wait at a route point,
//                 0x1c buildings, 0x1d flee
//   FUN_0046ea10  selection by click / box, FUN_0048d720 the right-click order
//   FUN_004824d0  the hit roll and damage, FUN_00472550 HP / MP changes,
//                 FUN_00472370 regeneration, FUN_00472770 falling at 0 HP
//
// The battle runs in real time: every unit acts on its own, the player
// orders their units around.
//   FUN_0046b0b0 / FUN_0046b0f0  unit +6 "steady" / +7 "busy" (see Unit)
// ============================================================================
#pragma once
#include "../anm/anm_player.hpp"
#include "../game_context.hpp"
#include "../net/net_protocol.hpp"
#include "../state/unit_stats.hpp"
#include "tactics_map.hpp"
#include "tactics_path.hpp"

#include <array>
#include <functional>
#include <cstdint>
#include <memory>
#include <string>
#include <deque>
#include <vector>

namespace ch {

namespace tactics {
using Compact = stats::Bytes<0x30>;
// FUN_004da5f0
[[nodiscard]] Compact compactRecord(int id, GameVersion v);
// FUN_004dfc20 (+ FUN_004e0a00)
void deriveCompact(const Compact& c, stats::Derived& out, GameVersion v);
// FUN_004da450: enemy record at `rec` (static data VA).
void enemyData(std::uint32_t rec, stats::Derived& out, GameVersion v);
// Debug: CH_UNITS set logs what the units do (read once).
[[nodiscard]] bool traceUnits();
} // namespace tactics

class TacticsUnits {
public:
    static constexpr int kSlots = 0xfa;

    // FUN_00475700's states (Unit::state +0x290, Unit::next +0x292). State 5
    // does nothing; 0x14 (FUN_0047a530) returns at once.
    enum UnitState : std::uint8_t {
        kNoState = 0,                             // as `next`: stay in the state
        kAppear = 1, kStand = 2, kHold = 3, kMove = 4, kSkill = 6,
        kStagger = 8, kKnock = 9, kGuardHit = 0xa, kOffField = 0xb, kCollapse = 0xc, kFall = 0xd,
        kRetreat = 0xe, kChoose = 0xf, kRoute = 0x10, kCall = 0x11, kGuard = 0x13, kCapture = 0x15,
        kAmbush = 0x16, kWander = 0x17, kSummon = 0x18, kAppear2 = 0x19, kRouteWait = 0x1a,
        kWithdraw = 0x1b, kBuilding = 0x1c, kFlee = 0x1d,
    };

    // A status condition (unit work +0x14 + slot * 0x10): slot 0 the
    // ailments, 1 the 0x15..0x18 group, 2 the blessings (table 0x6f4088 +2).
    struct Status {
        std::uint8_t  type = 0;                   // +0
        std::int16_t  skill = 0;                  // +4: the skill that caused it
        std::int32_t  time = 0;                   // +8: frames left, 0 for good
        std::int16_t  tick = 0;                   // +0xc: frames to the next 3-second tick
        std::int16_t  power = 0;                  // +0xe: HP / MP per tick (negative hurts)
    };

    struct Unit {
        bool              used = false;
        int               slot = 0;               // +2
        stats::Derived    data{};                 // +600 -> unit data block
        std::int32_t      x = 0, y = 0;           // +0x2ec / +0x2f0, pixels in 16.16
        std::uint8_t      dir = 2;                // +0x28a, numpad direction
        std::uint8_t      state = 0;              // +0x290
        std::uint8_t      next = 0;               // +0x292: state to enter next frame
        std::uint8_t      sub = 0;                // +0x293: step within the state
        bool              animEnd = false;        // +4: the clip has ended
        // +6 (FUN_0046b0b0): hits on it land as plain hits, results 2..4
        // become 1 (no stagger, no "no damage"); set while striking, falling,
        // being placed or taken over.
        bool              steady = false;
        // +7 (FUN_0046b0f0): it takes no orders (FUN_004693b0); set from the
        // cast to the end of the recovery and in the same states as +6.
        bool              busy = false;
        std::int16_t      timer = 0;              // +10: frames in the step
        int               clip = -1;              // +0x54: clip playing
        bool              selected = false;       // +0x28c
        std::vector<tactics::Cell> path;          // the route being walked
        // Where an inner route (+0x3e0) ends while the outer one (+0x2f4)
        // goes on: FUN_0047dbd0 lets the walk end there near the goal.
        std::vector<std::uint8_t> legEnd;
        std::size_t       step = 0;               //   and the next cell on it
        tactics::Cell     goal{-1, -1};           // +0x4d0: where the move order points
        tactics::Cell     post{-1, -1};           // +0x2e8 / 9: the cell it holds (stance 1)
        stats::Bytes<0x40> ai{};                  // +0x2ac: the AI record (enemy record +0x14), FUN_0047a8e0
        std::int32_t      aiScan = 0;             //   +0x28: frames since the last target scan
        bool              aiControlled = false;   // +0x2a7: acting on its own (state 0xf)
        std::int16_t      counter = -1;           // +0x4fa: the last unit that hit it
        std::int16_t      hitBy = -1;             // +0x4f8: whose hit staggered it (8, 9)
        // +0x4f6: how a knock-back (9) pushes it: -1 not at all, 1 away
        // from hitBy, 0 away from knockFrom (+0x4f4 / +0x4f5, a skill's cell).
        std::int8_t       knockMode = -1;
        tactics::Cell     knockFrom{-1, -1};
        // FUN_004753f0: the state a hit reaction interrupted (+0x29b..,
        // flag +0x2a6), taken up again by FUN_00475460.
        bool              saved = false;
        std::uint8_t      savedState = 0, savedSub = 0;
        // +0x260: the party members a call (0x11) brings in.
        std::vector<std::int16_t> callList;
        std::array<Status, 3> status{};           // +0x14
        std::int16_t      help = -1;              // +0x4fc: the enemy a teammate called it to
        int               talkState = -1;         // +0x25c's record +0x80 (FUN_0049ab60)
        std::int8_t       face = -1;              // +0x501: its place in the roster (FUN_0046d970)
        std::int8_t       route = 0;              // +0x2a9: the next point on its route
        bool              walkPause = false;      // FUN_00476680's step 10: standing between walks
        std::int16_t      routeWait = 0;          // +0x4e0: frames before the next search (FUN_00476680)
        bool              routeCut = false;       // the plan stops short of the goal
        bool              routeRetry = false;     // past the first search (FUN_00476680's steps 4..6)
        std::int16_t      target = -1;            // +0x4ee: the skill's target
        std::int16_t      skill = -1;             // +0x4f2: the skill in use
        std::int16_t      cast = 0, castFull = 0;           // +0xc / +0xe
        std::int16_t      recover = 0, recoverFull = 0;     // +0x10 / +0x12
        std::int16_t      regenHp = 0, regenMp = 0;         // +0x504 / +0x506
        tactics::Cell     chased{-1, -1};         // the target's cell when the chase was planned
        tactics::Cell     aim{-1, -1};            // +0x4f0 / +0x4f1: the cell a ground skill (+0x18 4) is used at
        std::int8_t       area = 0;               // +0x4e6: the script area it last entered
        tactics::Cell     areaCell{-1, -1};       // +0x4e9 / +0x4ea: the cell that was checked
        std::int16_t      holdOut = 600;          // +0x4fe: a building's resistance to capture
        std::uint8_t      captor = 0;             // +0x500: the team taking it over
        int               set = -1;               // DxAnim set index
        AnmPlayer::NodeId node = AnmPlayer::kNone;   // +0x48
        // The effect slots (+0xa0 + slot * 0x58, Efct.bin): 0 the strike or
        // the hit taken, 1 the cast, 2..4 the status conditions in status
        // slots 0..2 (the setters use 2 for the ailments, 4 for the blessings);
        // 5 stands for the effects the original hangs on the unit's own
        // model (+0x48) at an offset (fxDy); only the order mark uses it.
        std::array<AnmPlayer::NodeId, 6> fx{AnmPlayer::kNone, AnmPlayer::kNone, AnmPlayer::kNone,
                                            AnmPlayer::kNone, AnmPlayer::kNone, AnmPlayer::kNone};
        std::array<bool, 6> fxOn{};
        int               fxDy = 0;               // slot 5's offset
        // Network play: the held skill last asked for (+0x9d / +0x9e), not yet back from the relay.
        std::int8_t       netReady = -1, netIndex = -1;
        // +0x514 / +0x518: the orders waiting (16-byte commands: type,
        // unit, target, cell, skill); the first is tried each frame.
        struct Order {
            std::uint8_t type = 0;                // 1 move, 3 attack, ...
            int          cx = 0, cy = 0;          // +3 / +4
            int          target = -1;             // +2
            int          skill = -1;              // +6: the skill a player's order 3 uses (FUN_0048e6d0)
        };
        std::deque<Order> orders;
        std::vector<std::int16_t> callOrder;      // the list a queued command 11 carries
        bool              hitSignal = false;      // a flag-4 frame of its clip or strike effect
        bool              struck = false;         // the strike's hit is out
        // Hits waiting for the hit effect's flag-8 frame (FUN_004919f0).
        // `tail`: a learnt skill's added effects come with it (FUN_00491ef0).
        struct Landing { int from, result, amount, skill; tactics::Cell at; int pools; bool tail = false; };
        std::vector<Landing> landing;
        // Splash and area hits whose effect waits (the packet's first word,
        // which FUN_0040a800 counts down before the node starts).
        struct Delayed { Landing hit; int effect, wait; };
        std::vector<Delayed> delayed;
        // HP (0) / MP (1) changes waiting for the unit's own frame (+0x510:
        // nodes from the 0x200-node pool at +0x2b6f0, FUN_00492e90 /
        // FUN_00492f00 / FUN_00492f40, pushed at the head by FUN_00493020).
        struct Change { std::uint8_t kind; std::int16_t delta; };
        std::vector<Change> changes;
        [[nodiscard]] int px() const noexcept { return x >> 16; }
        [[nodiscard]] int py() const noexcept { return y >> 16; }
        [[nodiscard]] int cx() const noexcept { return px() / TacticsMap::kCellW; }
        [[nodiscard]] int cy() const noexcept { return py() / TacticsMap::kCellH; }
        // Fields of the data block, each at its own width.
        [[nodiscard]] auto kind() const { return data.u16(2); }        // the unit's record id
        [[nodiscard]] auto maxHp() const { return data.s16(0x14); }
        [[nodiscard]] auto hp() const { return data.s16(0x16); }
        void setHp(int v) { data.set16(0x16, v); }
        [[nodiscard]] auto mp() const { return data.s16(0x1c); }
        void setMp(int v) { data.set16(0x1c, v); }
        [[nodiscard]] auto moveType() const { return data.u8(0x30); }   // the map's move-rate column (4: as 0)
        [[nodiscard]] auto group() const { return data.u8(0x98); }      // the script's unit group
        [[nodiscard]] auto stance() const { return data.u8(0x9f); }     // 1 keeps to its post
        [[nodiscard]] auto order() const { return data.u8(0xa0); }
        [[nodiscard]] int team() const noexcept { return data.u8(0xa4); }
        [[nodiscard]] bool onField() const noexcept { return data.u8(0xf) == 1; }   // FUN_00469480
        [[nodiscard]] bool human() const noexcept { return team() < 4; }            // FUN_00468d80
    };

    // FUN_004653d0, the UnitCtrl constructor (the unit array and its lists
    // cleared, the route searches FUN_004378d0 / FUN_0044e280 made, two
    // 12 px GDI fonts for the labels), is this class's member initialisers;
    // FUN_00465bc0, its destructor, ~TacticsUnits (the fonts are the draw
    // list's own text). It also makes the AI command lists' node pools
    // (FUN_004646e0 -> FUN_004648d0, two of 300; FUN_004649c0 ->
    // FUN_00464b70, one of 300; freed by FUN_00464800), which the port's
    // per-unit vectors replace, and destroys the route searches
    // (FUN_00437940 -> FUN_004379b0 / FUN_00439410, FUN_0044e330 ->
    // FUN_0044e3b0). FUN_00451170 hands it the script's flare link
    // (+0x117c38, FUN_00465f90). Its member objects' constructors and
    // destructors are FUN_00465ac0 / FUN_00465b00 (a sound object,
    // FUN_004075e0 / FUN_00407710) and FUN_00465b40 / FUN_00465b80 (a
    // list, FUN_00427530 / FUN_004275b0); FUN_00437a30 and FUN_0044e810
    // give the two searches the map (+0xb0, +0xa8), and FUN_00466ab0
    // clears the 0x200 graphics-set slots (+0x2a6f0, sets_ here). The
    // pools' parts: FUN_00464780 / FUN_004647c0 / FUN_00464a40 /
    // FUN_00464a80 their lists' constructors and destructors, FUN_00464880
    // (FUN_00464980 on both lists) and FUN_00464ac0 (FUN_00464b30 ->
    // FUN_00464c20) the node lists' teardown.
    // FUN_00466000 -> FUN_0040b960(1): the animations' sound commands play
    // from group 1, the battle's sounds (FUN_0040b990 -> FUN_0040d390).
    explicit TacticsUnits(GameContext& ctx)
        : ctx_(ctx), anm_([&ctx](std::uint32_t se) { ctx.audio->sePlayIn(1, se, -1); }) {}
    ~TacticsUnits();
    TacticsUnits(const TacticsUnits&) = delete;
    TacticsUnits& operator=(const TacticsUnits&) = delete;

    // The party (DAT_007f4518, still off the field) and the map's enemies.
    // `fixedParty`: FUN_00452cf0 set the party, which keeps its places.
    void build(const TacticsMap& map, int mapId, bool fixedParty = false);
    // FUN_0049a310: appear orders for the first `count` party units, near
    // the start cell.
    void deployParty(int count);
    void update();                                 // FUN_0046b470, the unit part
    // `hover`: the slot under the pointer (+0x30992), -1 none.
    void draw(int hover) const;

    // The on-field unit standing on the cell, or null.
    [[nodiscard]] const Unit* at(int cx, int cy) const;
    // FUN_0046f440: the unit under the pointer (screen x, y) by its box
    // (FUN_0046fc20), for the order and the pointer (+0x30992).
    [[nodiscard]] const Unit* pointedUnit(int x, int y) const;
    [[nodiscard]] int count() const;
    [[nodiscard]] int partyCount() const noexcept { return party_; }
    // FUN_0046a950: how team `team` stands to the viewing side (0 own,
    // 1 enemy, 2 / 3 others, 4 ally), from the DAT_00618568 table.
    [[nodiscard]] int relation(int team) const;

    // Selection (FUN_0046ea10, FUN_0046efa0): any unit that can be seen;
    // own units push the others out.
    void clearSelection();
    // The unit on the cell; `toggle` flips it instead (Shift).
    void selectAt(int cx, int cy, bool toggle);
    // Every unit whose position lies in the map-pixel box.
    void selectBox(int x0, int y0, int x1, int y1, bool toggle);
    [[nodiscard]] int selectedCount() const;
    // The number keys' groups (unitctrlkeyselect.cpp: ten lists at task
    // +0x116098; a unit's group in its data +0x98).
    void clearGroups();                                // FUN_00493e60
    void storeGroup(int g);                            // FUN_00494030 (Ctrl + number)
    // FUN_004942b0 (number): the group becomes the selection; `centre` the
    // slot to centre the view on (its first member on the field when that
    // was already picked), else -1. False for an empty group.
    bool recallGroup(int g, int& centre);
    bool selectOnScreen();                             // FUN_004948f0 (Ctrl + A)
    bool selectLike(const Unit& u);                    // FUN_00494a50 (double click)
    [[nodiscard]] bool inGroup(const Unit& u) const;   // FUN_004944a0
    // The pointer (unitctrlcursor.cpp): which of common.bin's pointer
    // frames (0x226 + kind) shows over the map, and the MP-short mark.
    void updateCursor(bool onMap, int cx, int cy, int hover);   // FUN_00490fc0
    void drawCursor(int x, int y) const;                         // FUN_00491680
    // The command bar under the map (unitctrlwindow.cpp): the picked
    // unit's eight skills (F1..F8 too), the four stances, the skill-mode
    // switch, the retreat button, and the help lines for what the pointer
    // is over.
    struct BarInput {
        int  x = 0, y = 0;                       // the pointer (+0x196c / 70)
        bool pressed = false, held = false, released = false;   // left: +0x1990 & 8, +0x198c & 1, +0x199c & 1
        bool dragging = false;                   // +0x19b8: a box drag
        bool shift = false, shiftUp = false;     // DIK 0x2a / 0x36 held, released
        int  fkey = -1;                          // F1..F8 pressed (DAT_0061aea0)
        bool sortie = false;                     // +0x108b48
    };
    bool commandBar(const BarInput& in);         // FUN_0049b180; true when it took the click (+0x2f150)
    void updateHelp(const BarInput& in, int shown);   // FUN_0049c400
    void drawBar(bool sortie) const;             // FUN_0049beb0
    void drawHelp() const;                       // FUN_0049c850
    // FUN_0049cc60 -> FUN_0049ce90: the unit the panel shows (+0x109d4e),
    // -1 none; its model plays clip 1 facing down in the panel.
    int updatePanel(int hover, bool sortie);
    // FUN_0049ab60 (from FUN_0046db20 for each roster face of ours, not
    // during the sortie): a unit whose state changed says that state's line.
    void talk(bool sortie);
    // FUN_0048d720: the right button at cell (cx, cy) over unit `slot` (-1
    // none): each picked unit of ours uses its skill there, or walks.
    void orderAt(int cx, int cy, int slot);
    [[nodiscard]] int orderSkill(const Unit& u) const;    // FUN_0046c350(.., 0)
    [[nodiscard]] const Unit& unit(int slot) const { return units_.at(std::size_t(slot)); }
    // Its name by kind, as the status panel shows it (FUN_00467380's table).
    [[nodiscard]] std::string name(const Unit& u) const;
    [[nodiscard]] GameVersion version() const noexcept { return ctx_.version; }
    // Fighting units (not the buildings and gates) of the viewer's side /
    // of its enemies still on the field.
    [[nodiscard]] int standing(bool own) const;
    // One of ours on the field that could call the waiting in (not a base or gate).
    [[nodiscard]] bool anyOwnOnField() const;

    // The battle script's unit opcodes.
    [[nodiscard]] Unit& unitRef(int slot) { return units_.at(std::size_t(slot)); }
    // FUN_0046d970 + FUN_00475140: a unit taken into the player's control
    // gets a roster face and stands on "fixed point" (+0x9f 2, +0xa0 0).
    void takeControl(Unit& u) {
        addFace(u);
        u.aiControlled = false;                     // +0x2a7
        u.data.set8(0x9f, 2);
        u.data.set8(0xa0, 0);
    }
    // FUN_0047fcf0: a waiting unit appears on the free cell nearest the
    // cell, within `range` (FUN_0043d660 + FUN_00480040). False if it cannot.
    bool appearUnit(int slot, int cx, int cy, int range, bool fresh = true);
    // FUN_0047fc60: every waiting unit of `group` (data +0x98) on teams 4..15.
    int appearGroup(int group, int cx, int cy, int range);
    [[nodiscard]] bool groupClear(int group) const;    // FUN_00430980(group, 0)
    void reviveGroup(int group);                       // FUN_0047f9d0
    // FUN_0047f4b0 -> FUN_0047f6b0: one stance / AI field of the group's units.
    void setGroupField(int group, int field, int value) { setTeamGroupField(-1, group, field, value); }
    // FUN_0047f530 -> FUN_0047f6b0 for one team (-1: teams 4..15, FUN_0047f4b0);
    // opcode 0x84 (FUN_0047f450) passes the viewer's.
    void setTeamGroupField(int team, int group, int field, int value);
    [[nodiscard]] int viewer() const noexcept { return viewer_; }
    // FUN_004801e0 -> FUN_00480320: a unit on the field withdraws (message
    // 0x11 -> state 0x1b). False if it is not on the field.
    bool withdraw(int slot);
    // FUN_004809a0: message 0xe, the unit goes over to `team`.
    void changeSide(int slot, int team);
    // The unit messages FUN_0048e2a0 takes from the command queue
    // (FUN_0048cd90; the player's commands and the network's):
    // 5 -> FUN_0048e990, state 0xe: the retreat; 10 -> FUN_0048ea40, state
    // 0xc; 11 -> FUN_0048ea90, state 0x11: call `list` in. False if busy.
    bool retreat(int slot);
    bool collapse(int slot);
    bool callParty(int slot, const std::vector<std::int16_t>& list);
    // The right button on one of ours on the field while ours waiting are
    // picked: it calls them in (FUN_0048d720 -> FUN_004812c0). True when the
    // click was taken for the call.
    bool orderCall(int slot);
    // The right button on the minimap (FUN_004406f0 -> FUN_0046f440, cursor
    // kind 100): each picked unit of ours not already there walks to the
    // cell (FUN_0048d720 case 0x6e) and the cell shows the ground mark
    // (FUN_00472960 -> FUN_004728e0).
    void orderMinimap(int cx, int cy);
    // The call drag (unitctrlappear.cpp, +0x2f15b): with ours waiting
    // picked (+0x2f156), a press on one of their faces arms it (1); moved
    // past the drag distance the picked faces go round the pointer (2), and
    // letting go over one of ours on the field calls them in through it.
    // FUN_0046e9c0 -> FUN_00481610, each frame before the map's mouse. True
    // when the drag had the pointer this frame (the map takes no click).
    bool callDrag(const BarInput& in);
    // A face press the call drag armed: the face's own click waits for the release.
    [[nodiscard]] bool callArmed() const noexcept { return callState_ == 1; }
    [[nodiscard]] bool callDragging() const noexcept { return callState_ >= 2; }
    // FUN_00467850: a hold (1: `on` lets everything run again, 0 stops the
    // units, the scrolling and the timers) or a cut scene (2 with `on`: the
    // units act, the player's input and the timers stop), applied at the
    // next frame's start (FUN_004678c0).
    void requestHold(int kind, bool on) noexcept { holdKind_ = kind; holdOn_ = on; }
    // The player's scrolling and selection (+0x108b36 / 38), the timers (+0x108b39).
    [[nodiscard]] bool inputOpen() const noexcept { return inputOpen_; }
    [[nodiscard]] int timersWanted() noexcept { return std::exchange(timersWanted_, -1); }
    // FUN_00473860's tallies: what each team's kills were worth (+0x2ef14)
    // and how many of its units fell (+0x2ef34, FUN_00430b20).
    [[nodiscard]] int teamWorth(int team) const { return team >= 0 && team < 16 ? teamWorth_[std::size_t(team)] : 0; }
    // FUN_004307b0: no enemy fighter is left, on the field or to come.
    [[nodiscard]] bool enemiesGone() const;
    // FUN_00473940: whether base `n` (enemy slot 0xf9 - (n - 1)) is ours.
    [[nodiscard]] bool baseOurs(int n) const;
    // The script's effects (task +0x10c680, 10 slots, FUN_00466640): effect
    // `id` at a map point (mode 0), a screen point (1) or on a unit (2);
    // slots 0..4 in front of the battle (0x370), 5..9 behind it (100).
    void playScriptEffect(int slot, int x, int y, int id, int mode, int unit);
    void stopScriptEffect(int slot);                   // FUN_00466780
    // FUN_00493960: a flare at (x, y), set 0 clip 0x2d and sound 10 for
    // side 0, else set 3 clip 0x87 and sound 11. Only network play fires
    // one: the click while the chat's flare toggle is on and the other
    // side's message (FUN_00493c40, FUN_00458450: network, not ported).
    void flare(int x, int y, int side);
    // FUN_00467850: the battle's units hold still (events) or act again.
    void setFrozen(bool frozen) noexcept { frozen_ = frozen; }
    // +0x108b48: the battle's opening (FUN_00499c90 sets it, FUN_00499da0
    // clears it once the start banners are done). Standing units keep
    // still (FUN_00476250) and nothing is taken (FUN_00480a60) until then.
    void setOpening(bool on) noexcept { opening_ = on; }
    [[nodiscard]] bool frozen() const noexcept { return frozen_; }
    // Kind tests the stage checks use.
    [[nodiscard]] static bool isStructure(const Unit& u) { return structure(u); }   // FUN_00469ad0
    [[nodiscard]] static bool isBase(const Unit& u) { return base(u); }             // FUN_00469c60
    [[nodiscard]] const GameWork& work() const noexcept { return ctx_.work; }
    [[nodiscard]] static bool isDown(const Unit& u) { return down(u); }             // FUN_004690a0
    [[nodiscard]] int relationTo(const Unit& u) const { return relation(u.team()); }   // FUN_0046a950
    [[nodiscard]] int random() { return rand(); }      // FUN_00467e00, for the map checks
    // The network frame's check words (FUN_0048ceb0: +0x2e70c, +0x2e710).
    [[nodiscard]] std::uint32_t checkSeed() const noexcept { return seed_; }
    [[nodiscard]] std::uint32_t checkRolls() const noexcept { return rolls_; }
    // ---- network play (net/lockstep.hpp) ---------------------------------
    // With a sink set, the player's orders go to it as commands instead of
    // to the units; they come back through applyNet once the relay's turn
    // is in, for every player at the same frame.
    void setNetSink(std::function<void(const net::Command&)> sink) { netSink_ = std::move(sink); }
    [[nodiscard]] bool networked() const noexcept { return bool(netSink_); }
    void setViewer(int team) { viewer_ = team; }
    // Network play (before build): every player's units, the network
    // relation table, each player's start cell (net/net_game.hpp).
    void setNetwork(bool on) { network_ = on; }
    [[nodiscard]] bool network() const noexcept { return network_; }
    // The battle's table as it stands (+0x115bac; 0 the same side).
    [[nodiscard]] int rawRelation(int a, int b) const {
        return (a & 0xf) == (b & 0xf) ? 0 : relations_[std::size_t((a & 0xf) * 0x10 + (b & 0xf))];
    }
    // One player's command (`slot` = the sender, whose team it must be).
    // Commands no honest client sends are refused, the same way by every
    // client, and noted against the sender (takeRefused, for CHECK).
    void applyNet(int slot, const net::Command& c);
    void endNetTurn();
    // Debug (CH_NET_WIPE): every unit of the team on the field to 0 HP.
    void debugWipe(int team) {
        for (Unit& u : units_) if (u.used && u.onField() && u.team() == team) u.setHp(0);
        SDL_Log("[net] debug: team %d wiped", team);
    }                                 // after a turn's commands: a call still collecting its list
    // Debug (CH_HP): one unit's HP.
    void debugHp(int slot, int hp) {
        if (slot >= 0 && slot < kSlots) units_[std::size_t(slot)].setHp(std::uint16_t(hp));
    }
    [[nodiscard]] std::uint8_t takeRefused() noexcept { return std::exchange(refusedMask_, std::uint8_t(0)); }
    // The battle's state in one word: every unit in use, its cell, state,
    // HP, MP and team (with the generator's words, the CHECK report).
    [[nodiscard]] std::uint32_t stateHash() const;
    // The fog: ours and our allies light the map; the others hide outside it.
    void stampSight(TacticsMap& map) const;
    void drawDot(const Unit& u) const;                 // FUN_00440fe0
    // The HUD: the roster columns (FUN_0046da80) and the bottom-left panel
    // for `shown` (FUN_0049cce0 -> FUN_0049d160), -1 none.
    void drawHud(int shown, bool sortie = false) const;
    // The roster face under the screen point, or -1.
    [[nodiscard]] int rosterAt(int x, int y) const;
    // Selects the unit (Shift: toggles it).
    void selectSlot(int slot, bool toggle);
    // The sortie's cards (FUN_00499da0 case 1): a party member waiting off
    // the field is picked by its roster face; the count is the selection
    // (+0x108b50, FUN_0046f170).
    void selectCard(int slot, bool toggle);
    [[nodiscard]] bool cardPickable(const Unit& u) const;
    int selectFaces(int x0, int y0, int x1, int y1, bool toggle, bool cards);
    // FUN_0049a310: each picked unit to the free cell nearest the start
    // within `range`, as a placement (message 4 -> FUN_0048e790).
    void deploySelected(int sx, int sy, int range);
    // While the cards are up, picked waiting units stay picked.
    void setCards(bool on) noexcept { cards_ = on; }
    [[nodiscard]] bool seen(const Unit& u) const;
    [[nodiscard]] bool isSeen(const Unit& u) const { return seen(u); }

private:
    struct AnimSet {
        int           file = 0, palette = 0;
        std::uint16_t bank = 0;
        AnmFile       anm;
    };
    static constexpr std::uint16_t kFirstBank = 0x80;
    static constexpr std::uint16_t kEffectBank = 0xc0;   // data\DxAnim\Efct.bin (FUN_00466000)
    static constexpr int kMaxSets = 0x40;

    void create(int slot, const stats::Derived& d);    // FUN_004680b0
    void setPosition(Unit& u, int cx, int cy);         // FUN_00468690
    int  animSet(int file, int palette);               // FUN_00466e90
    void playAnim(Unit& u, int anim);                  // FUN_0046bcc0 -> FUN_00465040
    void setState(Unit& u, int state);                 // FUN_00475370
    void think(Unit& u);                               // FUN_00475700
    void face(Unit& u, int dir) const;                 // FUN_0046c040
    void buildingState(Unit& u);                       // FUN_004761c0, state 0x1c
    void offFieldState(Unit& u);                       // FUN_00479f40, state 0xb
    void withdrawState(Unit& u);                       // FUN_00479e40, state 0x1b
    void leaveField(Unit& u);                          // FUN_0046ba10
    [[nodiscard]] bool timeLimited() const;            // false with infinite unit time
    [[nodiscard]] static bool hasLearnt(const Unit& u, int skill);   // FUN_0046cb10
    void appearState(Unit& u);                         // FUN_00475d60, state 1
    void standState(Unit& u);                          // FUN_00476250, state 2
    void moveState(Unit& u);                           // FUN_00476dd0, state 4
    void holdState(Unit& u);                           // FUN_004763d0, state 3
    void skillState(Unit& u);                          // FUN_00477980, state 6
    void fallState(Unit& u);                           // FUN_004799c0, state 0xd
    void chooseState(Unit& u);                         // FUN_00476470, state 0xf
    void guardState(Unit& u);                          // FUN_004776a0, state 0x13
    void captureState(Unit& u);                        // FUN_0047a590, state 0x15
    void routeState(Unit& u);                          // FUN_0047ed90, state 0x10
    void routeWaitState(Unit& u);                      // FUN_0047efb0, state 0x1a
    void ambushState(Unit& u);                         // FUN_0047a470, state 0x16
    void wanderState(Unit& u);                         // FUN_004771d0, state 0x17
    void summonState(Unit& u);                         // FUN_0047f1d0, state 0x18
    void fleeState(Unit& u);                           // FUN_0047e680, state 0x1d
    void staggerState(Unit& u);                        // FUN_00478850, state 8
    void knockState(Unit& u);                          // FUN_00478bc0, state 9
    void guardHitState(Unit& u);                       // FUN_00479270, state 0xa
    void collapseState(Unit& u);                       // FUN_00479820, state 0xc
    void retreatState(Unit& u);                        // FUN_00479410, state 0xe
    void callState(Unit& u);                           // FUN_00475f60, state 0x11
    void saveState(Unit& u);                           // FUN_004753f0
    void resumeState(Unit& u);                         // FUN_00475460
    [[nodiscard]] static bool reacting(const Unit& u) { return u.state >= 8 && u.state <= 10; }   // FUN_00469730
    void faceToward(Unit& u, int px, int py) const;    // FUN_0046c120
    // FUN_0048e790's placement: steady and busy, on the cell, appearing.
    void placeAt(Unit& u, tactics::Cell c);
    void callIn(Unit& u);                              // FUN_004810d0
    void checkCollapse(Unit& u);                       // FUN_0046bc20
    // The recovery clip of the unit's job (DAT_00611538 [kind + 100] +2).
    [[nodiscard]] int recoveryClip(const Unit& u) const;
    [[nodiscard]] bool othersOn(int cx, int cy, const Unit& u) const;   // FUN_0043c0c0's count
    // FUN_0047a800: the unit's route (data +0xa2) on this map (FUN_004da7f0:
    // PTR_DAT_006acb88 [map], maps 0..399): {count, rows}
    // with 12-byte rows {u8 cx, cy; s32 wait at +4; s8 next at +8}.
    [[nodiscard]] std::pair<int, std::uint32_t> routeOf(Unit& u) const;
    bool nextPoint(Unit& u);                           // FUN_0047f090: true once the route ends
    void setGoal(Unit& u, int cx, int cy);             // FUN_0047d190
    // FUN_00438ba0 / FUN_00438f70: the farthest cell straight on in `dir`
    // (numpad) the unit can walk to, at most `most` cells; false if none.
    bool straightGoal(Unit& u, int dir, int most);
    // The AI's reactions while it stands, walks or wanders: true if the
    // unit changed state.
    bool aiReact(Unit& u, bool summon);
    bool react(Unit& u, const Unit& t);                // FUN_0047ce70
    bool switchTo(Unit& u, const Unit& t);             // FUN_0047b210
    [[nodiscard]] const Unit* helpTarget(Unit& u);     // FUN_0047b790 / FUN_0047b800
    void callHelp(const Unit& u, const Unit& t);       // FUN_0047b2c0 -> FUN_0047b340
    bool summonDue(Unit& u);                           // FUN_00480480
    [[nodiscard]] int summonRoom(const Unit& u) const; // FUN_00480740
    int summon(const Unit& u);                         // FUN_004807e0
    void occupy(Unit& u);                              // FUN_00480a60
    [[nodiscard]] int occupier(const Unit& u) const;   // FUN_00480d00
    bool changeTeam(Unit& u, int team);                // message 0xe, FUN_0048ef60
    // FUN_0047c580: aim the job's skill at `t`; false if it cannot be used.
    bool aimAt(Unit& u, const Unit& t);
    [[nodiscard]] int learntChoice(Unit& u, const Unit& t);   // FUN_0047c690 / FUN_0047c8a0
    [[nodiscard]] const Unit* scan(Unit& u);           // FUN_0047a920 -> FUN_0047a9d0
    [[nodiscard]] const Unit* healTarget(const Unit& u) const;   // FUN_0047ac80
    [[nodiscard]] bool friendly(const Unit& a, const Unit& b) const;
    [[nodiscard]] bool healing(const Unit& u, const Unit& t) const;
    [[nodiscard]] const Unit* counterTarget(Unit& u);  // FUN_0047cc90
    void hit(Unit& from, Unit& to);                    // the job attack's FUN_00483cf0 / FUN_004919f0
    // FUN_004deaa0: the combat values (FUN_004de930) after the status
    // conditions (FUN_004dea30): attack, spread, accuracy (+0x3a, 0x3c,
    // 0x38), evasion, guard, magic resistance, magic guard, status
    // resistance (+0x4c, 0x4d, 0x50, 0x4f, 0x4e), toughness (+0x52).
    struct Fighter {
        int atk = 0, spread = 0, acc = 0;
        int evade = 0, guard = 0, resist = 0, mguard = 0, sresist = 0, tough = 0;
    };
    [[nodiscard]] Fighter fighter(const Unit& u) const;
    [[nodiscard]] Fighter learntFighter(const Unit& u, int skill) const;   // FUN_00488af0
    [[nodiscard]] int rollAmount(const Fighter& a);
    [[nodiscard]] std::pair<int, int> weaponRoll(const Fighter& a, const Unit& to);   // FUN_004824d0
    [[nodiscard]] std::pair<int, int> spellRoll(const Fighter& a, const Unit& to);    // FUN_004826f0
    // `skill` and `at` (the skill's cell) choose how a strong hit knocks
    // the target back. `pools`: what `amount` changes, kPoolHp and / or
    // kPoolMp (FUN_00491ef0 by the formula row's +6), 0 nothing.
    static constexpr int kPoolHp = 1, kPoolMp = 2;
    void land(Unit& from, Unit& to, int result, int amount, int skill, tactics::Cell at, int pools = kPoolHp,
              bool tail = false);
    // FUN_00491ef0's tail for a learnt skill: its added effect, the cure, the dispel.
    void learntTail(Unit& from, Unit& to, int skill, int result);
    // FUN_00482d60: the hit effect on the target (DAT_00613b78 by skill and
    // result) carries the result to its landing frame; a miss shows at once.
    // `effect`: the hit effect to play instead of the skill's (-1: the skill's, by the result).
    // `delay`: frames before the effect starts (it stays hidden until then).
    void strike(Unit& from, Unit& to, int result, int amount, int skill, tactics::Cell at, int effect = -1,
                int pools = kPoolHp, int delay = 0, bool tail = false);
    // FUN_00482d60: the hit effect of `rowSkill`'s row for a result (some with rand() % 5 added).
    int hitEffect(int rowSkill, int result);
    // FUN_004919f0 cases 2 / 3: the reactions to a strong hit.
    void react(Unit& from, Unit& to, int result, int skill, tactics::Cell at);
    void playEffect(Unit& u, int slot, int id);        // FUN_0046beb0 -> FUN_00465140
    void flushLanding(Unit& u);                        // FUN_004918d0
    void updateEffects(Unit& u);
    void areaHit(Unit& from, Unit& primary);           // FUN_00488bc0
    void addHp(Unit& u, int delta);                    // FUN_00472550(.., 0, ..)
    void addMp(Unit& u, int delta);                    // FUN_00472550(.., 1, ..)
    // FUN_00493090 / FUN_00493250 / FUN_00493320 (HP) and FUN_00493180 (MP):
    // queue the change; FUN_00493450 applies the queue in the unit's frame.
    static void queueChange(Unit& u, int kind, int delta);
    void applyChanges(Unit& u);                        // FUN_00493450
    // The status conditions.
    void inflict(int effect, int skill, Unit& from, Unit& to);   // FUN_0048b2e0
    void tryStatus(int skill, int type, Unit& from, Unit& to, Status s);   // FUN_0048c850
    [[nodiscard]] bool statusRoll(int type, const Unit& to, Status& s);   // FUN_004828a0
    [[nodiscard]] int statusTime(int skill, const Unit& from) const;     // FUN_0048b720
    [[nodiscard]] int statusPower(int skill, const Unit& from, int stat) const;
    void applyStatus(Unit& u, int slot, int type, const Status& s);      // FUN_00490180
    void clearStatus(Unit& u, int slot);               // FUN_004902e0
    [[nodiscard]] static int statusSlot(const Unit& u, int type);        // FUN_004900e0: 1..3, 0 none
    void tickStatus(Unit& u);                          // FUN_00490350
    void cure(const Unit& from, Unit& to);             // FUN_00492320
    void statusStats(const Unit& u, Fighter& f) const; // FUN_004dea30
    [[nodiscard]] float speedScale(const Unit& u) const;   // FUN_00490b30
    void castScale(Unit& u, int skill) const;          // FUN_00490cd0
    void regenerate(Unit& u);                          // FUN_00472370
    [[nodiscard]] static bool waiting(const Unit& u);  // FUN_00469220: off the field, +0xf 2..4
    // The popups over the units (unitctrlpopup.cpp): numbers and words.
    void clearPopups();                                // FUN_00494d20
    void popup(const Unit& u, int kind, int value = 0);   // FUN_00494db0 + FUN_00495630
    void showNumber(const Unit& u, int value, int kind);  // FUN_00494e70
    void showHp(const Unit& u, int amount, int style);    // FUN_00493090's popup
    void showMp(const Unit& u, int amount);               // FUN_00493180's
    void showPoison(const Unit& u, int amount);           // FUN_00493320's
    void showMessage(const Unit& u, int n);               // FUN_00495130 -> FUN_004951b0
    void tickPopups();                                    // FUN_00495220
    void drawPopups() const;                              // FUN_004952a0
    [[nodiscard]] int damageStyle(const Unit& from, int skill) const;   // FUN_00491ef0's choice
    [[nodiscard]] int rand();                          // FUN_00467e00
    [[nodiscard]] int relationOf(const Unit& a, const Unit& b) const;  // FUN_0046af50
    [[nodiscard]] int relationCode(int from, int to) const;
    [[nodiscard]] bool hostile(const Unit& a, const Unit& b) const;    // FUN_0046ac90
    void splash(Unit& from, const Fighter& a);                         // FUN_0048adc0
    void orderMark(Unit& u);                                           // FUN_0048f100
    void queueOrder(Unit& u, const Unit::Order& o);                    // FUN_0048dcd0
    // The held skill (+0x9d ready, +0x9e its place in the list; -1 keeps a
    // field): set at once, or in network play sent as command 0x40.
    void hold(Unit& u, int ready, int index);
    [[nodiscard]] int heldReady(const Unit& u) const { return u.netReady >= 0 ? u.netReady : u.data.u8(0x9d); }
    void runOrders(Unit& u);                                           // FUN_0048e2a0
    [[nodiscard]] static bool hasOrder(const Unit& u, int type);       // FUN_0048e020
    [[nodiscard]] bool attackable(const Unit& a, const Unit& b) const; // FUN_0046ae20
    [[nodiscard]] static bool down(const Unit& u);     // FUN_004690a0
    // FUN_004692e0 / FUN_004693b0: +6 / +7, or appearing (1, 0x19) or in
    // states 0xc, 0xd.
    [[nodiscard]] static bool steadyNow(const Unit& u);
    [[nodiscard]] static bool busyNow(const Unit& u);
    static void setFlags(Unit& u, bool on) noexcept { u.steady = u.busy = on; }
    [[nodiscard]] static bool base(const Unit& u);     // FUN_00469c60
    [[nodiscard]] static bool structure(const Unit& u);   // FUN_00469ad0
    [[nodiscard]] int jobSkill(const Unit& u) const;   // DAT_006b2da0 [kind]
    [[nodiscard]] int skillRange(const Unit& u, int skill) const;   // FUN_004ddc60
    [[nodiscard]] static std::uint32_t formulas(int skill);          // FUN_004de8f0
    [[nodiscard]] int learntReach(const Unit& u, int skill) const;   // FUN_004ddf40
    [[nodiscard]] int skillArea(const Unit& u, int skill) const;     // FUN_004ddd50
    [[nodiscard]] static int skillValue(const Unit& u, int kind, int base, int add, int pct);
    [[nodiscard]] static int sight(const Unit& u);     // FUN_004ddbb0
    [[nodiscard]] bool reaches(const Unit& u, const Unit& t, int range) const;
    // FUN_0047c210: the skill's target (by its record's +0x18 kind) is still
    // in sight; FUN_0047e460 lets a friend's sight count for a player's unit.
    [[nodiscard]] bool inSight(const Unit& u, int kind) const;
    [[nodiscard]] bool friendSees(const Unit& u, int cx, int cy) const;
    void drawHold(const Unit& u, int x, int y) const;   // FUN_0046d6a0
    // FUN_00476680's results: 0 on the way, 1 there, 2 the next cell is
    // closed (FUN_0047da30), 3 no route from the first search.
    enum class Walk { On = 0, Arrived = 1, Blocked = 2, NoRoute = 3 };
    Walk walk(Unit& u);                                // FUN_00476680 / FUN_0047dec0
    void plan(Unit& u, bool retry = false);            // FUN_0047d210
    void planRoute(Unit& u);
    [[nodiscard]] bool selectable(const Unit& u) const;   // FUN_00468f40
    [[nodiscard]] bool orderable(const Unit& u) const;
    void trimSelection();                              // FUN_0046f170
    [[nodiscard]] bool restingOn(int cx, int cy, const Unit* except) const;
    tactics::Cell appearCell(int cx, int cy, int range, int moveType);   // FUN_0043d660
    int                        appearTurn_ = 0;    // +0x263c4
    std::vector<tactics::Cell> appearGiven_;       // +0x263c8: the cells given out
    void drawGauge(const Unit& u, int x, int y, int prio) const;   // FUN_0046d340
    void addFace(Unit& u);                             // FUN_0046d970
    void drawFace(const Unit& u, int x, int y, int prio, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                  std::uint8_t a = 0xff) const;
    void endCallDrag() noexcept;                       // FUN_00481580 / FUN_004815c0
    void drawTimeDigits(int x, int y, int prio, int value, int first, int style, std::uint32_t argb) const;   // FUN_00472f40 mode 1
    [[nodiscard]] bool callFaceAt(int x, int y, int column) const;   // FUN_004818e0
    [[nodiscard]] bool pointedAt(const Unit& u, int x, int y) const; // FUN_0046fc20
    void tickCallRing(int x, int y);                   // FUN_00481fc0
    void drawCallRing() const;                         // FUN_00482150
    // FUN_00472bf0: `digits` places of common.bin's 8x12 digits, leading
    // zeros hidden (negative `digits`: shown).
    void drawNumber(int x, int y, int prio, int value, int digits, std::uint32_t argb, int style = 0) const;

    GameContext&                          ctx_;
    const TacticsMap*                     map_ = nullptr;
    tactics::RouteGraph                   graph_;   // +0x2a55c (FUN_0044e850, FUN_0044e440)
    std::array<Unit, kSlots>              units_{};
    std::vector<std::unique_ptr<AnimSet>> sets_;
    AnmPlayer                             anm_;
    AnmFile                               effect_;
    AnmPlayer::NodeId                     marker_ = AnmPlayer::kNone;   // +0x109388: selection ring
    struct ScriptEffect { AnmPlayer::NodeId node = AnmPlayer::kNone; bool on = false; int x = 0, y = 0, mode = 0, unit = 0; };
    std::array<ScriptEffect, 10>          scriptFx_{};
    // The five flares (+0x116254, 0x524 bytes each; +0x116250 the next).
    struct Flare { bool on = false; std::uint8_t side = 0; std::int16_t life = 0; AnmPlayer::NodeId node = AnmPlayer::kNone; int x = 0, y = 0; };
    std::array<Flare, 5>                  flares_{};
    // The ground mark (+0x109dc8, set 1 clip 0x58) at a map pixel
    // (+0x10f9c0 / c2), -1 when off.
    AnmPlayer::NodeId                     groundMark_ = AnmPlayer::kNone;
    int                                   groundX_ = -1, groundY_ = -1;
    int                                   nextFlare_ = 0;
    int                                   viewer_ = 0;  // +0x2ef44
    int                                   party_ = 0;
    int                                   mapId_ = 0;   // DAT_007f4488
    int                                   faces_ = 0;   // +0xdb814
    std::uint32_t                         seed_ = 0x87654321u;   // +0x2e70c (FUN_00451f50)
    std::uint32_t                         rolls_ = 0;            // +0x2e710: how many rolls so far
    std::function<void(const net::Command&)> netSink_;
    bool                                  network_ = false;
    std::array<std::int8_t, 0x100>        relations_{};          // +0x115bac (FUN_00468cd0)
    void loadRelations();                                         // FUN_00468bc0 / FUN_00468c00
    bool                                  applyingNet_ = false;
    std::uint8_t                          refusedMask_ = 0;
    struct NetCall { int slot = -1, sender = -1; std::vector<std::int16_t> list; };
    NetCall                               netCall_;               // a call's list arriving in pieces
    bool                                  frozen_ = false;       // +0x108b3b
    bool                                  opening_ = false;      // +0x108b48
    bool                                  cards_ = false;        // the sortie's cards are up
    int                                   holdKind_ = -1;        // +0x108b3b, -1 none
    bool                                  holdOn_ = false;       // +0x108b3c
    bool                                  inputOpen_ = true;
    int                                   timersWanted_ = -1;
    std::array<int, 16>                   teamWorth_{}, teamFalls_{};
    // FUN_00494db0's records (0x200 of 0x10 bytes at +0x2c6f0).
    struct Popup {
        bool          on = false;     // +0 bit 7
        std::uint8_t  kind = 0;       // +1: 0..5 / 9..0xe numbers, 0x12 no damage, 0x13 miss, 0x28.. words
        int           dx = 0, dy = 0; // +2 / +3 (s8)
        int           unit = 0;       // +4
        int           timer = 0;      // +6: frames left on this step
        int           value = 0;      // +8
        int           step = 0;       // +10: the step in the kind's script
        int           fade = 0;       // +0xb
        std::uint32_t script = 0;     // +0xc: PTR_DAT_00619668[kind]
    };
    std::array<Popup, 0x200>              popups_{};
    bool                                  replay_ = false;
    std::array<std::vector<int>, 10>      groups_{};
    int                                   cursorKind_ = 0;      // +0x30994
    bool                                  cursorShort_ = false; // +0x30995
    bool                                  callReady_ = false;   // +0x2f156 (FUN_0049ba60)
    // The call drag's ring (+0x115cac, FUN_00481ef0): its pattern (+0),
    // the turn in degrees (+2), the swell (+4, 0..0x2d) and its step (+6),
    // and the pointer it circles (+8 / +0xa).
    struct CallRing {
        int mode = 0;
        int angle = 0, swell = 0, step = 1;
        int x = 0, y = 0;
    };
    int                                   callState_ = 0;       // +0x2f15b
    int                                   callPressX_ = 0, callPressY_ = 0;   // +0x19bc / c0
    CallRing                              ring_{};
    int                                   panelMode_ = 0;       // +0x115cb8 (FUN_0049cc10: 0)
    int                                   panelShown_ = -1;     // +0x109d4e
    AnmPlayer::NodeId                     panelNode_ = AnmPlayer::kNone;   // +0x109860's model
    bool                                  retreatDown_ = false; // +0x2f157
    int                                   helpKind_ = -1, helpId_ = -1;   // +0x2f15c / +0x2f15e
    void setStance(Unit& u, int stance);         // FUN_0049bd90 -> FUN_0048eda0
    void voice(const Unit& u, int event);        // FUN_0049ad90
    [[nodiscard]] bool canRetreat(const Unit& u) const;   // FUN_0046a840
    [[nodiscard]] bool callable(const Unit& u) const;     // FUN_0046a120   // +0x117c3c (FUN_00496390)
};

} // namespace ch
