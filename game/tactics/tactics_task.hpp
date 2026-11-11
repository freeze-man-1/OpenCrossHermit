// ============================================================================
// tactics_task.hpp — TacticsTask, scene 0x10 (src\yoshida\TacticsTask.cpp)
//
// The battle, in real time. Ported so far: the stage's map (TacticsMap)
// with scrolling, the units on it (TacticsUnits) with the party's sortie,
// selecting units and ordering them to walk or attack, the units' AI and
// fighting, the frame (FUN_00453e10) and the stage script (StageScript:
// the opening, timers, reinforcements, area events). The win and loss
// checks are per map (FUN_00432100, StageChecks); a map without one ends
// the battle when one side has no fighters left (or on Enter, a win).
//   FUN_00451080  create (task 0x1194f0 bytes), FUN_00451170 constructor
//   FUN_00451670  Start: banks, FUN_00452cf0 stage setup, phase loop
//                 (+0x34: 0 placement FUN_00451f50, 1 battle, 2 end),
//                 then the next scene by map id and result
//   FUN_00452cf0  stage setup: party at DAT_007f4492 (cells), map
//                 FUN_0043a640 / FUN_0043a920 / FUN_0044e440 by DAT_007f4488
//   FUN_004539b0  the battle's frame: FUN_0046b470 units, then the draws
//   FUN_00499da0  the sortie: the camera on the start, the player picks
//                 units by their cards (FUN_0049a600 the panel) and presses
//                 Sortie, FUN_0049a310 orders them onto the field; then the
//                 opening event and the start banners (FUN_00499910)
//   FUN_0046ea10  the left button: a click selects the unit under the
//                 pointer, a drag the units in the box (Shift toggles)
//   FUN_0046f440  the right button (FUN_0043b7b0): the order cell
//   FUN_00432100  the per-map checks (StageChecks::run)
//   FUN_00430fe0  the counts of the stage's buildings and gates (+0x115cba)
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "tactics_checks.hpp"
#include "tactics_map.hpp"
#include "tactics_script.hpp"
#include "tactics_units.hpp"
#include "tactics_wide.hpp"
#include "../ysub/chat_box.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace ch {

class TacticsTask final : public Task {
public:
    explicit TacticsTask(GameContext& ctx)
        : ctx_(ctx), units_(ctx), script_(ctx, map_, units_), checks_(units_, script_, map_, clock_) {}
    ~TacticsTask() override;
    Co<> start(std::uint32_t) override;

private:
    void scroll();
    void mouse();                                // FUN_0046ea10 / FUN_0046f440
    void loadSounds();                           // FUN_00458ff0 / FUN_00459080
    // The pause menu (Esc or the button at the top left) and the
    // objectives (Space or the button at (0x3d1, 0x26f)).
    bool openMenu();                             // FUN_00458580
    bool openObjectives();                       // FUN_00458be0
    int  menuStep();                             // FUN_00453d00: 1 when the battle is given up
    void drawMenu() const;                       // FUN_004586d0's drawing
    void drawGoals() const;                      // the port's own: the stage's PERFECT goals, over the menu
    bool keys();                                 // FUN_0046ea10's keys (unitctrlkeyselect.cpp)
    [[nodiscard]] bool keyHit(int scancode);     // FUN_004128f0(key, 1)
    [[nodiscard]] int groupKey();                // FUN_00494690: DAT_00619280 pairs
    [[nodiscard]] TacticsUnits::BarInput barInput() const;
    void draw();
    [[nodiscard]] int viewMargin() const;        // the port's wider view
    void drawClock(int prio);                    // FUN_00467c20
    void drawTime(int x, std::int32_t frames, int prio);   // FUN_00472f40
    void drawGlyph(int sx, int sy, int x, int prio);
    void finish(int rank);
    void sortieMouse();                          // FUN_00499da0 case 1
    void startDrag(bool face);                   // a left press: a box may follow
    [[nodiscard]] int rosterColumn(int x0, int y0, int x1, int y1) const;   // FUN_0046f8b0
    void startBanner(int type);                  // FUN_004998b0
    bool tickBanner();                           // FUN_00499910
    void drawBanner() const;                     // FUN_00499b80
    void drawSortie() const;                     // FUN_0049a600
    void drawDigits(int x, int y, int value, int style, std::uint32_t argb, int prio) const;   // FUN_00495fe0
    // StageChecks::run (FUN_00432100): false when the map has no check (the stand-in runs).
    static constexpr std::uint16_t kCommonBank = 0x5a;   // data\Tactics\common.bin (FUN_00451b10)

    GameContext& ctx_;
    TacticsMap   map_;
    TacticsUnits units_;
    StageScript  script_;
    TacticsWide  wide_{ctx_, map_, units_};
    BattleClock  clock_;
    StageChecks  checks_;
    int          rank_ = 0;                      // +0x117c38 -> +0x174
    net::Session* net_ = nullptr;                 // network play: the relay session, else null
    // Network play's chat and flares (FUN_00457890 .. FUN_00458450).
    void netTalk();
    void coopHandBack();                         // FUN_00473dc0
    void drawTalk();                             // FUN_00457c40
    void sendFlare(int px, int py);              // FUN_00493c40 -> FUN_00458380
    void flareAt(int px, int py, int side);      // FUN_00493960 at FUN_00440e60's point
    [[nodiscard]] bool sameSide(int slot) const; // FUN_00458060
    bool netChatOpen_ = false;                   // +0x117f20
    bool netChatTeam_ = false;                   // +0x18c: to our side only
    bool netFlareMode_ = false;                  // +0x18d (FUN_00458100 / FUN_00458130)
    std::string netChatLine_;
    ChatBox netLog_{4, true};                    // FUN_00451170: 4 lines, newest first
    std::uint64_t netSeen_ = 0;                  // the next chat line to show
    // FUN_00499da0's steps (Sortie::phase), run once a frame until the battle.
    enum SortiePhase : int {
        kSortieCamera = 0,          // the camera on the start, the lit area grows
        kSortieCards = 1,           // the cards and the Sortie button
        kSortieChosen = 2,          // a short wait after the button
        kSortieDeploy = 3,          // the chosen units go out (FUN_0049a310)
        kSortieFree = 4,            // the units act for three seconds
        kSortieFreeWait = 5,
        kSortieOpening = 6,         // the opening event
        kSortieOpeningScript = 7,   // until its script is done
        kSortieBanner1 = 8,         // the start banner, if the stage has one
        kSortieBanner1Wait = 9,
        kSortieOpeningEnd = 0xa,    // the event's undim and end
        kSortieBanner2 = 0xb,       // the second banner
        kSortieBanner2Wait = 0xc,
        kSortieDone = 0xd,          // the battle runs
    };
    // FUN_00499c90's record (+0x108b3e): the sortie.
    struct Sortie {
        bool on = false;                         // +0: the player chooses who goes
        int  sx = 0, sy = 0;                     // +2 / +4: the start cell
        int  range = 0, most = 0, fewest = 0;    // +6 / +7 / +8
        int  button = 2;                         // +9: 0 ready, 1 pressed, 2 off
        bool panel = true;                       // +10
        int  phase = 0, shown = 0, wait = 0;     // +0xc / +0xd / +0xe
    } sortie_;
    // FUN_004998b0's banner (+0x30998).
    struct StartBanner {
        bool on = false;
        int  step = 0, type = 0, hold = 0, x = 0, y = 0;
        std::int32_t alpha = 0, rate = 0;
    } startBanner_;
    int          mapId_ = 0;
    // The left button's drag (FUN_0043c9c0, map +0x265d8): map pixels.
    bool         dragging_ = false, dragged_ = false, dragFace_ = false;
    int          dragX_ = 0, dragY_ = 0;
    // The double click (the original's mouse +0x1990 & 0x40): a second
    // press on the same cell within 30 frames.
    int          clickAge_ = 1000, clickX_ = -1, clickY_ = -1;
    int          faceHeld_ = -1;                 // a face press the call drag armed (its click waits for the release)
    std::array<std::uint8_t, 512> keysWere_{};
    // FUN_00453f10's blinds (+0x38 3 opening, 0x13 closing; DAT_007a417c).
    int          blinds_ = 0, blindsT_ = 0;      // 0 none, 1 opening, 2 closing
    void         drawBlinds();
    void         route(int rank);                // FUN_00451670's tail: where the game goes next
    int          menu_ = 0;                      // +0x30: 0 none, 1 the pause menu, 2 the objectives
    bool         quit_ = false;                  // +0x31: given up
    bool         objectivesShade_ = false;       // +0x11804c
    int          menuButton_ = -1;               // FUN_00458800's result this frame
    int          pressX_ = -1, pressY_ = -1;     // the mouse +0x19bc / 0x19c0: where the press began
    bool         stanceKey_ = false;             // the port's stance key this frame
    int          fkey_ = -1;                     // F1..F8 this frame (DAT_0061aea0)
    bool         shiftWas_ = false, shiftUp_ = false;
};

} // namespace ch
