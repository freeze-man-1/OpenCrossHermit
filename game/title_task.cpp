// ============================================================================
// title_task.cpp
// ============================================================================
#include "title_task.hpp"
#include "option/options.hpp"
#include "state/game_logic.hpp"
#include "preparation/preparation_task.hpp"

#include <cstdio>

namespace ch {

using namespace title_layout;
using legacy::kMouseLeft;

namespace {
// Fade values are 8.8 fixed point; the alpha byte is the integer part.
constexpr std::uint32_t whiteWithAlpha(std::int32_t fx) { return (std::uint32_t(fx >> 8) & 0xFFu) << 24 | 0x00FFFFFFu; }
constexpr std::uint32_t blackWithAlpha(std::int32_t fx) { return (std::uint32_t(fx >> 8) & 0xFFu) << 24; }

// Strict bounds, as in the original compare chain.
constexpr bool inside(int mx, int my, int x, int y, int w, int h) {
    return x < mx && mx < x + w && y < my && my < y + h;
}

constexpr std::uint16_t kTitleBgmTrack  = 0x20;     // TitleSE.mp3, played once
constexpr std::uint32_t kGameVersion    = 0x10002;  // FUN_004e2c30: major << 16 | minor
} // namespace

// FUN_0045ea50
TitleTask::~TitleTask() {
    ctx_.audio->seStopAll();             // FUN_0040d610 (+ FUN_0040d120 channels 1..19)
    ctx_.sprites->unloadBank(kBank);     // FUN_0041f4e0(0x5a)
    ++(trial_ ? ctx_.globals.trialTitleVisits : ctx_.globals.titleVisits);   // DAT_007a49d4 / DAT_007a49cc
}

// FUN_0045eb90
Co<> TitleTask::start(std::uint32_t) {
    if (!trial_) SDL_Log("TitleTask::Start():ST");
    loadResources();
    co_await run();
    if (!trial_) SDL_Log("TitleTask::Start():ED");
}

// FUN_0045ebf0: whole-file read, hand the buffer to sprite bank 0x5a, free it.
void TitleTask::loadResources() {
    ctx_.sprites->loadBank(kBank, ctx_.readData(trial_ ? "data\\Tactics\\TitleTrial.bin" : "data\\Tactics\\Title.bin"));
}

// FUN_0045ecc0
Co<> TitleTask::run() {
    const int result = co_await stateMachine();
    if (trial_) {
        // FUN_00460410. The original resets nothing here; the work reset
        // (FUN_004e1b60) that the title's New Game and the JP Tutorial do is
        // done too, so a trial run starts on a clean calendar and roster.
        switch (result) {
            case 0:
                logic::newGame(ctx_.work, ctx_.version);
                ctx_.scenes.request(SceneId::WeekStart);
                break;
            case 1:                                   // the tutorial map
                logic::newGame(ctx_.work, ctx_.version);
                ctx_.work.set16(0x7f4488, 0x50);
                PreparationTask::loadStage(ctx_.work, ctx_.version, 0x50, true);   // FUN_004da860
                ctx_.scenes.request(SceneId::Tactics);
                break;
            case 2: ctx_.quitRequested = true; co_await yieldFrame(); break;
            default: ctx_.scenes.request(SceneId::Opening); break;   // 6: idle
        }
        co_return;
    }
    switch (result) {
        case 0:  /* New Game: FUN_004e1b60 work reset (calendar, flags, roster) */
                 logic::newGame(ctx_.work, ctx_.version);
                 ctx_.scenes.request(SceneId::WeekStart); break;
        case 1:  /* Load Game: FUN_004e1b60 work reset, then SLProg in mode 0 */
                 logic::newGame(ctx_.work, ctx_.version);
                 ctx_.work.set16(0x7e11a8, 0);
                 ctx_.scenes.request(SceneId::SaveLoad); break;
        case 2:  /* Exit (confirmed) */ ctx_.quitRequested = true; co_await yieldFrame(); break;
        case 3:  /* Multi */    ctx_.scenes.request(SceneId::NetHost); break;
        case 4:
            if (ctx_.version == GameVersion::Jp) {
                startTutorial();
            } else {
                ctx_.scenes.request(SceneId::Option);
            }
            break;
        case kTimeout: ctx_.scenes.request(SceneId::Opening); break;
        default:
            if (result == 5 && ctx_.version == GameVersion::Jp) {   // Option
                ctx_.scenes.request(SceneId::Option);
                break;
            }
            if (result == 5 && twTutorial()) {
                startTutorial();
                break;
            }
            // TW item 5 (hidden top-left hotspot) has no case in the original
            // and leaves no scene running.
            // Deviation: return to the title instead of hanging on black.
            SDL_Log("TitleTask: item %d has no target scene, reopening title", result);
            ctx_.scenes.request(SceneId::Title);
            break;
    }
}

// JP FUN_0042e6a0: work reset (FUN_004796d0), the map id (DAT_007087a8) set to
// 0x50 and its stage row loaded (FUN_00475290 -> FUN_004752e0, the battle cleared).
void TitleTask::startTutorial() {
    logic::newGame(ctx_.work, ctx_.version);
    ctx_.work.set16(0x7f4488, 0x50);
    PreparationTask::loadStage(ctx_.work, ctx_.version, 0x50, true);
    ctx_.scenes.request(SceneId::Tactics);
}

bool TitleTask::twTutorial() const {
    return !trial_ && ctx_.version == GameVersion::Tw && options::port().twTutorial;
}

// FUN_0045edf0
Co<int> TitleTask::stateMachine() {
    enum State { Intro = 0, Menu = 1, Quit = 10, Exit = 0x14 };
    State state  = Intro;
    int   result = 0;                               // local_c
    for (;;) {
        switch (state) {
            case Intro:
                ctx_.audio->bgmPlay(kTitleBgmTrack, 0);
                co_await introFlash();
                state = Menu;
                break;
            case Menu:
                result = co_await mainMenu();
                state  = (result == 2) ? Quit : Exit;
                break;
            case Quit:
                state = (co_await quitConfirm() == 0) ? Exit : Menu;   // 0 = yes
                break;
            case Exit:
                co_await fadeOutToBlack();
                if (trial_) {
                    // FUN_00460520 case 0x14: items 3..5 show their picture
                    // (frames 3..5) until a click, then back to the menu.
                    if (result >= 3 && result <= 5) {
                        co_await page(result);
                        co_await fadeInFromBlack();
                        state = Menu;
                        break;
                    }
                    ctx_.audio->bgmStop();            // FUN_004db120
                    co_return result;
                }
                ctx_.audio->bgmFadeOut(0x5A);
                while (!ctx_.audio->bgmFadeFinished()) co_await yieldFrame();
                ctx_.audio->seStopAll();
                co_await yieldFrame();
                co_return result;
        }
    }
}

// FUN_0045f0d0: fade to white (204 frames), reveal the title under the white
// (170 frames, left click skips), then hold 10 frames.
Co<> TitleTask::introFlash() {
    for (std::int32_t a = 0; a < 0xFF00; a += 0x140) {
        drawFade(whiteWithAlpha(a));
        co_await yieldFrame();
    }
    for (std::int32_t a = 0xFF00;;) {
        drawBackground();
        drawCursor();
        drawFade(whiteWithAlpha(a));
        co_await yieldFrame();
        a -= 0x180;
        if (a < 0xFF) break;
        if (ctx_.mouse().pressed & kMouseLeft) break;
    }
    for (int i = 0, n = trial_ ? 4 : 10; i < n; ++i) {
        drawBackground();
        drawCursor();
        co_await yieldFrame();
    }
}

// FUN_0045efb0. Highlight shows while the left button is held; the item fires
// on release. 0xe4c frames (~61 s) without a choice returns kTimeout. The
// counter is never reset by input; that matches the original.
Co<int> TitleTask::mainMenu() {
    for (std::int32_t idle = 0xE4C;;) {
        if (--idle == 0) co_return kTimeout;

        const int sel = hitMenu();
        if (sel != kNone && (ctx_.mouse().released & kMouseLeft)) {
            ctx_.audio->sePlay(0);
            for (int i = 0; i < 8; ++i) {
                drawBackground();
                drawCursor();
                drawMenuHighlight(sel);
                co_await yieldFrame();
            }
            co_return sel;
        }
        drawBackground();
        drawCursor();
        if (ctx_.mouse().held & kMouseLeft) drawMenuHighlight(sel);
        co_await yieldFrame();
    }
}

// FUN_0045f3d0
Co<int> TitleTask::quitConfirm() {
    const int px = kQuitPanelPos.x, py = kQuitPanelPos.y;
    int sel;
    while ((sel = hitQuit(px, py)) == kNone || !(ctx_.mouse().released & kMouseLeft)) {
        drawBackground();
        drawQuitPanel(px, py);
        if (ctx_.mouse().held & kMouseLeft) drawQuitHighlight(px, py, sel);
        drawCursor();
        co_await yieldFrame();
    }
    ctx_.audio->sePlay(0);
    for (int i = 0; i < 4; ++i) {
        drawBackground();
        drawQuitPanel(px, py);
        drawQuitHighlight(px, py, sel);
        drawCursor();
        co_await yieldFrame();
    }
    co_return sel;
}

// FUN_0045f310: 32-frame fade to black over the title.
Co<> TitleTask::fadeOutToBlack() {
    for (std::int32_t a = 0; a < 0xFF01; a += 0x800) {
        drawBackground();
        drawCursor();
        drawFade(blackWithAlpha(a));
        co_await yieldFrame();
    }
}

// FUN_004609a0: the trial title back from black, 32 frames.
Co<> TitleTask::fadeInFromBlack() {
    for (std::int32_t a = 0xFF00; a > 0; a -= 0x800) {
        drawBackground();
        drawCursor();
        drawFade(blackWithAlpha(a));
        co_await yieldFrame();
    }
}

// FUN_00460dd0 / FUN_00460d50 / FUN_00460e90: a full-screen picture
// (FUN_004610f0: frame `frame`) in from black, held until a click (sound 0),
// out to black.
Co<> TitleTask::page(int frame) {
    const auto picture = [&] {
        ctx_.draw.sprite({.bank = kBank, .frame = std::uint16_t(frame), .x = 0, .y = 0, .flags = 0x80, .priority = 100});
        drawCursor();
    };
    for (std::int32_t a = 0xFF00; a > 0; a -= 0x800) {
        picture();
        drawFade(blackWithAlpha(a));
        co_await yieldFrame();
    }
    while (!(ctx_.mouse().pressed & kMouseLeft)) {
        picture();
        co_await yieldFrame();
    }
    ctx_.audio->sePlay(0);
    for (std::int32_t a = 0; a < 0xFF01; a += 0x800) {
        picture();
        drawFade(blackWithAlpha(a));
        co_await yieldFrame();
    }
}

// ------------------------------------------------------------ hit tests ---
int TitleTask::hitMenu() const {
    const auto& m = ctx_.mouse();
    for (int i = 0; i < int(menu().hit.size()); ++i) {
        const auto& r = menu().hit[std::size_t(i)];
        if (inside(m.x, m.y, r.x, r.y, r.w, r.h)) return i;
    }
    return kNone;
}

int TitleTask::hitQuit(int px, int py) const {
    const auto& m = ctx_.mouse();
    for (int i = 0; i < int(kQuitHit.size()); ++i) {
        const auto& r = kQuitHit[i];
        if (inside(m.x, m.y, px + r.x, py + r.y, r.w, r.h)) return i;
    }
    return kNone;
}

// ---------------------------------------------------------------- draws ---
void TitleTask::drawBackground() {
    ctx_.draw.sprite({.bank = kBank, .frame = 1, .x = 0, .y = 0, .flags = 0x80, .priority = 100});
    if (trial_) return;                  // FUN_00461040: no version line

    // FUN_0045ff30
    char buf[64];
    std::snprintf(buf, sizeof buf, "VERSION %2u.%2u", unsigned(kGameVersion >> 16), unsigned(kGameVersion & 0xFFFF));
    ctx_.draw.text({.x = kVersionPos.x, .y = kVersionPos.y, .priority = 0x51,
                    .argb = kVersionArgb, .text = buf});
    if (twTutorial()) drawTutorial(false);
}

// The ends of the Option button around a blank stretch of the Exit button's
// plate, with the label on top. Frame 1 has the title's buttons and frame 2
// the same ones lit, laid out from the menu's corner (215, 492).
void TitleTask::drawTutorial(bool lit) {
    const auto piece = [&](int sx, int sy, std::int16_t w, std::int16_t h, int dx, int dy) {
        const int ox = lit ? 215 : 0, oy = lit ? 492 : 0;
        const std::int16_t x = std::int16_t(sx - ox), y = std::int16_t(sy - oy);
        ctx_.draw.sprite({.bank = kBank, .frame = std::uint16_t(lit ? 2 : 1), .x = dx, .y = dy, .w = w, .h = h,
                          .src = SrcRect{x, y, std::int16_t(x + w), std::int16_t(y + h)},
                          .flags = 0xC0, .priority = std::uint16_t(lit ? 98 : 99)});
    };
    constexpr int kY = 557;                              // the row under Option (524..557)
    piece(422, 524, 25, 31, 422, kY);
    piece(235, 557, 51, 29, 447, kY + 1);
    piece(498, 524, 27, 31, 498, kY);
    ctx_.draw.text({.x = 422 + 103 / 2, .y = kY + 14, .priority = 97, .argb = lit ? 0xFFFFFFFFu : 0xFFD8D8C4u,
                    .text = "Tutorial", .fontPx = 16, .centred = true});
}

void TitleTask::drawCursor() {
    const auto& m = ctx_.mouse();
    ctx_.draw.sprite({.bank = kBank, .frame = 0, .x = m.x, .y = m.y, .flags = 0x80, .priority = 1});
}

void TitleTask::drawMenuHighlight(int item) {
    if (item == kNone) return;
    if (item == 5 && twTutorial()) { drawTutorial(true); return; }
    const auto& s = menu().hiliteSrc[std::size_t(item)];
    const auto& d = menu().hiliteDst[std::size_t(item)];
    ctx_.draw.sprite({.bank = kBank, .frame = 2,
                      .x = d.x, .y = d.y + s.y,                     // dest y is offset by src y
                      .w = s.w, .h = s.h,
                      .src = SrcRect{s.x, s.y, std::int16_t(s.x + s.w), std::int16_t(s.y + s.h)},
                      .flags = 0xC0, .priority = 99});
}

void TitleTask::drawQuitPanel(int px, int py) {
    ctx_.draw.sprite({.bank = kBank, .frame = 6, .x = px, .y = py,
                      .w = 0x10E, .h = 0x5B, .src = SrcRect{0, 0, 0x10E, 0x5B},
                      .flags = 0xC0, .priority = 0x5B});
}

void TitleTask::drawQuitHighlight(int px, int py, int item) {
    if (item == kNone) return;
    const auto& s = kQuitHiliteSrc[item];
    const auto& d = kQuitHiliteDst[item];
    ctx_.draw.sprite({.bank = kBank, .frame = 6, .x = px + d.x, .y = py + d.y,
                      .w = s.w, .h = s.h,
                      .src = SrcRect{s.x, s.y, std::int16_t(s.x + s.w), std::int16_t(s.y + s.h)},
                      .flags = 0xC0, .priority = 0x5A});
}

void TitleTask::drawFade(std::uint32_t argb) {
    ctx_.draw.fill({.x = 0, .y = 0, .w = 0x403, .h = 0x300, .priority = 0x50, .argb = argb});
}

} // namespace ch
