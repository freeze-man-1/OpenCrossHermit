// ============================================================================
// tactics_script.hpp — the battle's stage script (src\yoshida script work,
// DAT_007a49f8, and the event part of the tactics task, +0x43..+0x5c)
//
// data\Tactics\Script\tNNNN.bin (name per map, PTR_s_t0000_bin_0060bc78):
// a block file of two blocks, 101 chunks each; chunk n runs from entry n to
// entry n + 1 and is a complete YBC script (FUN_00455060).
//   block 0  the stage's events, by number: 1 opening (FUN_00499da0), the
//            rest called by the timers and the per-map checks (FUN_00432100)
//   block 1  area events: a unit steps into script area n (FUN_00430400)
//
// A chunk runs twice. FUN_004551c0 first runs it to the end in one go with
// the drawing off (+0x92e1) and mode 0 (opcode 0xa7 reads it): the chunk
// tests its conditions, may already act (place reinforcements, set timers)
// and says with opcode 0xa6 whether its event should play. FUN_004549d0
// then loads it as the running event, mode 1, ticked once a frame with the
// ADV message system drawing (FUN_00454cf0).
//
// Battle opcodes (case = opcode - 5 in FUN_004ce8f0):
//   0x60..0x63 FUN_004e1fa0  global value (type 0..3) = a
//   0x64       FUN_004e21f0  a = per-map value (type 0)
//   0x70       FUN_0047fcf0  unit appears near a cell
//   0x71/0x72  FUN_00497210 / FUN_00497350  area on/off, renumber
//   0x73       FUN_0043bd00  a cell's area number
//   0x75/0x76  FUN_004978c0 / FUN_00497980  dim / undim the battle (waits)
//   0x78       FUN_00498010  scroll to a cell (waits)
//   0x7c/0x7d  FUN_00455420 / FUN_004558e0  one-shot / repeating timer
//   0x7e       FUN_0043bb90  a = the cell's area number
//   0x82       FUN_0047fc60  a group of waiting units appears
//   0x85       FUN_0047f4b0  a stance / AI field of a group
//   0x88       FUN_00499560  sub-commands (pause, camera, area teams ...)
//   0x8e       FUN_00497df0  scroll to a unit (waits)
//   0x9f..0xa1 FUN_004e2760  a = battle value (type 1..3)
//   0xa3..0xa5 FUN_004e29e0  battle value (type 1..3) = a
//   0xa6 / 0xa7               the check result / the run mode
//   0x92       FUN_004997d0  queries (0 the team, 7 the unit that stepped in)
//   0x88 0x1c..0x21 appear timers (+0x2234, FUN_00455d40) and the group pool
//   0x74 / 0x7f FUN_00497160 / FUN_004970d0  points nothing reads (+0x10..0x16, +0xc / +0xe)
//   0x77, 0x79..0x7b  FUN_00497a40, FUN_00498280, FUN_00498920, FUN_004989b0: empty
//   0x84       FUN_00498d30 -> FUN_0047f450  a stance / AI field of the viewer's group
//   0x89..0x8c FUN_00499370 / FUN_004993d0 / FUN_00499430 / FUN_004994e0  script effects
//   0x8f       FUN_00499110 -> FUN_00466410  a unit's position
//   0x88 1..4 effects, 0x18 the unit that stepped in withdraws (FUN_00498a40),
//        0x19 a unit changes sides (FUN_00498f30), 0x1a the stage banner
//        (FUN_004990a0 -> FUN_00456340), 0x1b map 17's raid (FUN_00499520 ->
//        FUN_00434c40), 0x22 the result message's number (FUN_004976e0)
// The script work (DAT_007a49f8, 0x24c4 bytes) is these members: FUN_00496af0 /
// FUN_00496b30 make it, FUN_00496bc0 resets the areas, FUN_00496d00 frees it.
// ============================================================================
#pragma once
#include <functional>
#include "../adv/ybc_vm.hpp"
#include "../game_context.hpp"
#include "tactics_map.hpp"
#include "tactics_units.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <vector>

namespace ch {

class StageScript final : private IYbcHost {
public:
    StageScript(GameContext& ctx, TacticsMap& map, TacticsUnits& units);
    ~StageScript() override;
    StageScript(const StageScript&) = delete;
    StageScript& operator=(const StageScript&) = delete;

    // FUN_00454fa0 (+ FUN_00455310 / FUN_00455810 / FUN_00496xxx resets).
    bool load(int mapId);
    // FUN_00455160: the timers count from now on: the three kinds' "on"
    // bytes (FUN_004553e0 +0x20e8 the one-shot timers, FUN_004558a0 +0x216c
    // the repeating ones, FUN_00455d00 +0x2230 the appear timers), which
    // the resets clear too. A free timer record has 0xff first
    // (FUN_00455390); a free pool place is -1 (FUN_00456870) and
    // FUN_004568c0 finds the first one (asserting the pool of 0x20 is not
    // full).
    void enableTimers() noexcept { timersOn_ = true; }
    void setTimers(bool on) noexcept { timersOn_ = on; }   // FUN_00455160
    // FUN_00498ce0: an appear timer is called off.
    void clearAppear(int slot) { if (slot >= 0 && slot < int(appears_.size())) appears_[std::size_t(slot)] = Appear{}; }

    // FUN_004551c0: the chunk's check pass; true when its event should play.
    bool check(int block, int index);
    // FUN_004549d0: `endsBattle` (+0x4c) ends the battle after the event,
    // `runScript` (+0x48) plays the chunk, `fade` (+0x50) undims at the end,
    // `pause` (+0x54) holds the units still meanwhile.
    // (FUN_00454980 is FUN_004549d0 with fade off, script and pause on.)
    void startEvent(int block, int index, bool endsBattle, bool runScript, bool fade, bool pause);
    // FUN_00454bb0: check, then play; with `endsBattle` the battle ends
    // even when the chunk has nothing to show.
    void runIf(bool endsBattle, int block, int index);
    [[nodiscard]] bool eventRunning() const noexcept { return event_.active; }   // FUN_00454c40
    // FUN_00454c40's value: 0 no event, 1 its script running, 2 the script
    // done (+0x59), the event finishing or, with the undim, held (state 3)
    // until FUN_00454c80 lets it go.
    [[nodiscard]] int eventState() const noexcept { return !event_.active ? 0 : event_.advDone ? 2 : 1; }
    // FUN_00454c80 (the sortie, after the start banner): a held event goes
    // on to its undim (+0x58).
    void releaseEvent() noexcept { event_.released = true; }
    [[nodiscard]] int resultMessage() const noexcept { return resultMessage_; }
    [[nodiscard]] bool battleOver() const noexcept { return battleOver_; }       // +0x171

    // Once a frame: the event, the timers, the camera and the dimming.
    void update();
    void draw() const;
    // FUN_00430400: a unit's area triggers, after the units moved.
    void checkAreas();

    // DAT_00809348: the battle's own values (types 0 s32 x10, 1 s16 x20,
    // 2 u8 x40, 3 bits x320; FUN_004e2760 / FUN_004e29e0).
    // +0x2e714 (FUN_004305c0): the units of each team standing in each
    // area (the map's raw area numbers 0..31), and the job classes among
    // them (bit class - 1, DAT_006b2d8a). Rebuilt by checkAreas().
    struct Tally { std::int16_t count = 0; std::uint8_t classes = 0; };
    [[nodiscard]] const Tally& tally(int area, int team) const {   // FUN_00430c90 (count)
        static const Tally none{};
        return area >= 0 && area < 32 && team >= 0 && team < 16 ? tally_[std::size_t(area)][std::size_t(team)] : none;
    }

    // Network play: an event's dialog is read at each player's own pace
    // (its ADV task is not in the lockstep), so the event ends for all only
    // once every player present has reported theirs over (command 0x42):
    // `report` sends ours, netEventDone records one arriving.
    void setNetGate(std::uint8_t players, std::function<void(std::uint32_t)> report) {
        netPlayers_ = players;
        netDone_ = std::move(report);
    }
    void netEventDone(int slot, std::uint32_t serial) {
        if (serial == eventSerial_ && slot >= 0 && slot < 8) event_.doneMask = std::uint8_t(event_.doneMask | 1u << slot);
    }
    void dropPlayer(int slot) { if (slot >= 0 && slot < 8) netPlayers_ = std::uint8_t(netPlayers_ & ~(1u << slot)); }
    [[nodiscard]] bool waitingForOthers() const noexcept { return netDone_ && event_.active && event_.localDone && !event_.advDone; }

    [[nodiscard]] std::int32_t value(int type, std::uint32_t index) const;
    void setValue(int type, std::uint32_t index, std::int32_t v);

private:
    struct Area { std::uint8_t id = 0; bool on = false; std::uint16_t teams = 0xffff; };   // +0x18, 4 bytes
    struct Timer { bool used = false; int block = 0, index = 0; std::int32_t left = 0, period = 0; };
    struct Event {
        bool active = false, endsBattle = false, runScript = false, fade = false, pause = false;
        bool advDone = false, undimming = false;
        bool released = false;                     // +0x58 (FUN_00454c80)
        bool localDone = false;                    // our own ADV task's end
        bool reported = false;
        std::uint8_t doneMask = 0;                 // network play: the players whose dialog is over
    };
    std::uint32_t eventSerial_ = 0;                // events started this battle (the same everywhere)
    std::function<void(std::uint32_t)> netDone_;   // network play: report our dialog's end
    std::uint8_t netPlayers_ = 0;                  // network play: the players present (bits)

    // IYbcHost: the battle opcodes (both passes); the rest is the ADV's.
    std::vector<std::uint8_t> loadFile(std::string_view) override { return {}; }
    void exec(YbcVm& vm, std::uint16_t op, std::uint32_t rec) override;
    bool waitDone(std::uint16_t) override { return true; }
    void scriptEnded() override {}   // the AdvTask stops the voice
    void sub88(YbcVm& vm, std::uint32_t rec);

    [[nodiscard]] std::vector<std::uint8_t> chunk(int block, int index) const;
    [[nodiscard]] int areaAt(int cx, int cy) const;      // FUN_00496e70
    void hold(YbcVm& vm);                                // +0x92e0 until the camera / dim is done
    void release();                                      // FUN_004977a0
    void scrollToCell(int cx, int cy);                   // FUN_00498010
    void scrollToUnit(int slot);                         // FUN_00497df0
    void dim(int frames, bool in);                       // FUN_004978c0 / FUN_00497980
    void tickTimers();                                   // FUN_00455560 / FUN_00455950
    void raid();                                         // FUN_00434c40
    void tickBanner();                                   // FUN_004563d0
    void finishEvent();

    GameContext&  ctx_;
    TacticsMap&   map_;
    TacticsUnits& units_;
    std::vector<std::uint8_t> file_;
    YbcVm         checkVm_;
    bool          checking_ = false;
    bool          mode_ = false;                         // DAT_007a49f8 [0]: opcode 0xa7
    bool          found_ = false;                        //              [1]: opcode 0xa6
    std::array<Area, 100>  areas_{};
    std::array<Timer, 16>  timers_{};                    // +0x20ec
    std::array<Timer, 16>  intervals_{};                 // +0x2170
    // +0x2234 (FUN_00455d40 / FUN_00455ec0): 32 timers that bring a group
    // (or one unit) onto the field when they run out.
    struct Appear { bool used = false, single = false; int who = 0, cx = 0, cy = 0, range = 10; std::int32_t left = 0; };
    std::array<Appear, 32> appears_{};
    std::array<std::int16_t, 32> groupPool_{};          // +0x2450: candidate groups (-1 none)
    void tickAppears();
    [[nodiscard]] int pickGroup();                       // FUN_004569b0
    bool          timersOn_ = false;                     // +0x20e8 / +0x216c
    Event         event_{};
    bool          battleOver_ = false;
    YbcVm*        held_ = nullptr;
    // Camera (+0x24a8..+0x24b4) and the saved centre (+0x24c0).
    bool          scrolling_ = false;
    int           scrollTargetX_ = 0, scrollTargetY_ = 0;
    int           savedX_ = 0, savedY_ = 0;
    int           entered_ = -1;                         // +0x24bc: the unit that stepped in
    // Dimming (+0x2490..+0x249c): 8.8 alpha, up to 0x98 black.
    bool          dimming_ = false, dimIn_ = false;
    std::int32_t  dimValue_ = 0, dimStep_ = 0;
    // The stage banner (+0x2434, FUN_00456340): TactStart frame `idx`
    // (DAT_0060cb04) fading in, held until a click, fading out.
    struct Banner { bool on = false; int phase = 0, idx = 0, x = 0, y = 0, prio = 100, hold = 0; std::int32_t alpha = 0, step = 0; };
    Banner        banner_{};
    bool          bannerLoaded_ = false;
    std::array<std::int16_t, 4> points_{};               // +0x10..+0x16 (opcode 0x74)
    std::array<std::int16_t, 2> marks_{};                // +0xc / +0xe (opcode 0x7f)
    std::uint8_t  resultMessage_ = 0;                    // +3: FUN_00458be0 shows message 7 + it
    std::set<std::uint16_t> unported_;
    std::array<std::array<Tally, 16>, 32> tally_{};
};

} // namespace ch
