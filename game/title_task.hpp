// ============================================================================
// title_task.hpp — TitleTask (E:\CrossHermit\GAME\SRC\YOSHIDA\...)
// ============================================================================
#pragma once
#include "game_context.hpp"

#include <array>
#include <cstdint>

namespace ch {

namespace title_layout {
struct Rect16  { std::int16_t x, y, w, h; };
struct Point16 { std::int16_t x, y; };

inline constexpr std::uint16_t kBank = 0x5A;   // data\Tactics\Title.bin

// Main menu layout (screen rects, read off both builds). Both builds
// hit-test 6 rects; they differ in what the right column holds.
struct MenuLayout {
    std::array<Rect16, 6>  hit;        // screen rects
    std::array<Rect16, 6>  hiliteSrc;  // {srcX, srcY, w, h} in Title.bin frame 2
    std::array<Point16, 6> hiliteDst;  // {x, y}; dest y is offset by srcY
};

// Cross Hermit.exe (JP): Multi / Tutorial / Option.        VA 0x004f87b8 / 87e8 / 8818
inline constexpr MenuLayout kMenuJp{
    {{{215, 492, 207, 32}, {215, 524, 207, 33}, {215, 557, 207, 30},     // New Game, Load Game, Exit
      {422, 492, 103, 32}, {422, 524, 103, 33}, {422, 557, 103, 30}}},   // Multi, Tutorial, Option
    {{{0, 0, 207, 32}, {0, 32, 207, 33}, {0, 65, 207, 30},
      {207, 0, 103, 32}, {207, 32, 103, 33}, {207, 65, 103, 30}}},
    {{{215, 492}, {215, 492}, {215, 492}, {422, 492}, {422, 492}, {422, 492}}}};

// ch.exe (TW): no tutorial; Multi / Option. The table has 5 rows, so entry 5
// is really the first hilite-src row: a hidden hotspot at the top-left
// corner with no case in the result switch.  VA 0x0060ceb0 / ced8 / cf00
inline constexpr MenuLayout kMenuTw{
    {{{215, 492, 207, 32}, {215, 524, 207, 33}, {215, 557, 207, 30},     // New Game, Load Game, Exit
      {422, 492, 103, 32}, {422, 524, 103, 33},                          // Multi, Option
      {0, 0, 207, 32}}},                                                 // (overlap, see above)
    {{{0, 0, 207, 32}, {0, 32, 207, 33}, {0, 65, 207, 30},
      {207, 0, 103, 32}, {207, 32, 103, 33}, {215, 492, 215, 492}}},
    {{{215, 492}, {215, 492}, {215, 492}, {422, 492}, {422, 492}, {0, 0}}}};

// The port's option: the TW menu with the JP Tutorial item, in the empty cell
// under Option (the JP menu's place for Option). Its art is drawn by drawTutorial.
inline constexpr MenuLayout kMenuTwTutorial{
    {{kMenuTw.hit[0], kMenuTw.hit[1], kMenuTw.hit[2], kMenuTw.hit[3], kMenuTw.hit[4], {422, 557, 103, 30}}},
    kMenuTw.hiliteSrc, kMenuTw.hiliteDst};

// ch.exe's trial title (TitleTrialTask, data\Tactics\TitleTrial.bin): six
// items, the same rects as the JP menu.          VA 0x0060cf48 / cf78 / cfa8
// Its quit panel's tables (0x0060cf38 / cfc0 / cfd0) are kQuit* below.
inline constexpr MenuLayout kMenuTrial = kMenuJp;

inline constexpr std::array<Rect16, 2>  kQuitHit{{                 // VA 0x0060cea0, panel-relative
    {27, 52, 103, 28}, {140, 52, 102, 28}}};                         // Yes, No
inline constexpr std::array<Rect16, 2>  kQuitHiliteSrc{{           // VA 0x0060cf18
    {0, 91, 135, 30}, {135, 91, 135, 30}}};
inline constexpr std::array<Point16, 2> kQuitHiliteDst{{           // VA 0x0060cf28, panel-relative
    {0, 52}, {135, 52}}};

inline constexpr Point16 kQuitPanelPos{0x179, 0x144};      // FUN_0045f3d0
inline constexpr Point16 kVersionPos{0x374, 0x2E4};        // FUN_0045ff30
inline constexpr std::uint32_t kVersionArgb = 0xFF6060A0u;
} // namespace title_layout

// The title, or with `trial` the trial build's (TitleTrialTask,
// src\YOSHIDA\TitleTrialTask.cpp, scene 2): FUN_00460040 news (FUN_00460130)
// and links it, start(0); FUN_004601c0 (FUN_004602c0 deletes) is its
// destructor, FUN_00460300 -> FUN_00460340 / FUN_00460410 its start. Its
// functions are the title's with its own bank file, no version text, a
// 4-frame hold after the intro, the music cut instead of faded, three
// picture pages (items 3..5) and its own targets:
//   FUN_00460520 state machine   FUN_004606f0 menu      FUN_00460810 intro
//   FUN_004609a0 fade from black FUN_00460a50 to black  FUN_00460b10 quit box
//   FUN_00460c50 quit hit        FUN_00460f50 menu hit  FUN_00461040 background
//   FUN_004611a0 menu highlight  FUN_00461320 quit panel FUN_00461420 its highlight
//   FUN_004615a0 cursor          FUN_00460d50 / 0dd0 / 0e90 a page: wait, in, out
//   FUN_004610f0 a page's picture
// (TitleTrialTask2 / 3, FUN_00461690.. / FUN_00462e70.., are never created
// and their bank files are not in the game.)
class TitleTask final : public Task {
public:
    explicit TitleTask(GameContext& ctx, bool trial = false) : ctx_(ctx), trial_(trial) {}
    ~TitleTask() override;                       // FUN_0045ea50 (FUN_0045eb50 deletes)

    Co<> start(std::uint32_t param) override;   // FUN_0045eb90

private:
    // Results of the main menu (FUN_0045efb0) / state machine (FUN_0045edf0).
    static constexpr int kTimeout = 6;
    static constexpr int kNone    = -1;

    void      loadResources();                   // FUN_0045ebf0
    Co<>      run();                             // FUN_0045ecc0
    Co<int>   stateMachine();                    // FUN_0045edf0
    Co<>      introFlash();                      // FUN_0045f0d0
    Co<int>   mainMenu();                        // FUN_0045efb0
    Co<int>   quitConfirm();                     // FUN_0045f3d0
    Co<>      fadeOutToBlack();                  // FUN_0045f310
    Co<>      fadeInFromBlack();                 // FUN_004609a0 (trial)
    Co<>      page(int frame);                   // FUN_00460dd0 / 0d50 / 0e90 (trial)

    [[nodiscard]] int hitMenu() const;           // FUN_0045f810
    [[nodiscard]] int hitQuit(int px, int py) const;   // FUN_0045f510

    void drawBackground();                       // FUN_0045f900 (+ FUN_0045ff30)
    void drawCursor();                           // FUN_0045fe70
    [[nodiscard]] bool twTutorial() const;       // the TW menu has the port's Tutorial item
    void drawTutorial(bool lit);                 // its button: built from the TW art, which has none
    void startTutorial();                        // JP FUN_0042e6a0
    void drawMenuHighlight(int item);            // FUN_0045fa70
    void drawQuitPanel(int px, int py);          // FUN_0045fbf0
    void drawQuitHighlight(int px, int py, int item);   // FUN_0045fcf0
    void drawFade(std::uint32_t argb);           // FUN_00410310(0,0,0x403,0x300,0x50,argb,1)

    [[nodiscard]] const title_layout::MenuLayout& menu() const {
        if (trial_) return title_layout::kMenuTrial;
        if (ctx_.version == GameVersion::Jp) return title_layout::kMenuJp;
        return twTutorial() ? title_layout::kMenuTwTutorial : title_layout::kMenuTw;
    }

    GameContext& ctx_;
    bool         trial_;
};

} // namespace ch
