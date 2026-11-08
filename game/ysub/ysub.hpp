// ============================================================================
// ysub.hpp — helpers shared by the Yoshimaru scenes (WorkRoom, Han/Kojin,
// Preparation, results, save/load): src\yoshimaru\ysub.cpp and neighbours.
//
//   FUN_004d2620          fill                     -> fill()
//   FUN_004d2700 / 2790   draw a rect-table entry  -> drawRect()
//   FUN_004d5ec0 / 5b70   mouse over a rect        -> hit()
//   FUN_004d5c40 / 5ca0   hit-table entry          -> hitEntry()
//   FUN_004d1cf0          sprite digits            -> drawNumber()
//   FUN_004d5fb0 ..       YsText line list         -> TextList
//   FUN_004d6930 / 69e0   timed sprite overlays    -> Flashes
// ============================================================================
#pragma once
#include "../adv/ybc_vm.hpp"
#include "../game_context.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ch::ysub {

// 16-byte rect-table record: bank (u8), frame, src x/y/w/h, default x/y.
struct Rect {
    std::uint8_t  bank = 0;
    std::uint16_t frame = 0;
    std::int16_t  sx = 0, sy = 0, w = 0, h = 0;
    std::int16_t  x = 0, y = 0;
};

// A rect-table record read from the exe's static data (ch.exe VA).
[[nodiscard]] Rect rectAt(std::uint32_t va, GameVersion v);

// FUN_004d2700 (offX = offY = 0) / FUN_004d2790: sub-rect sprite, source
// shifted by (offX, offY), via FUN_004d29e0 mode 1. Mode 3 (FUN_004d3050)
// is the same draw with a vertex alpha (0x80 = the hover fade).
void drawRect(GameContext& ctx, const Rect& r, int prio, int x, int y, int offX = 0, int offY = 0,
              std::uint8_t alpha = 0xff);

// FUN_004d2f10 (mode 3 with colour) / FUN_004d2dd0 (mode 2, scaled) /
// FUN_004d2ba0 (mode 0, whole frame): the general forms; FUN_004d2830 (a
// table rect's source at an explicit size) and FUN_004d30c0 (a whole frame,
// coloured, straight to the sprite object) are draws of this kind too.
struct Sub {
    std::uint8_t  bank = 0;
    std::uint16_t frame = 0;
    int sx = 0, sy = 0, w = 0, h = 0;           // w == 0: the whole frame
    std::uint8_t  alpha = 0xff, rgb = 0x80;    // vertex colour (0x80 = unmodulated)
    std::uint8_t  blend = kBlendAlpha;
    int scaleX = 100, scaleY = 100;
};
void drawSub(GameContext& ctx, const Sub& s, int prio, int x, int y);

// FUN_00410680: a 1-pixel frame from (x0, y0) to (x1, y1), both included
// (a closed line strip); FUN_004d2690 takes the corner and the size.
void outline(GameContext& ctx, int x0, int y0, int x1, int y1, int prio, std::uint32_t argb,
             std::uint8_t blend = kBlendAlpha);
inline void box(GameContext& ctx, int x, int y, int w, int h, int prio, std::uint32_t argb,
                std::uint8_t blend = kBlendAlpha) { outline(ctx, x, y, x + w, y + h, prio, argb, blend); }

// FUN_004d2620 -> FUN_00410310.
void fill(GameContext& ctx, int x, int y, int w, int h, int prio, std::uint32_t argb,
          std::uint8_t blend = kBlendAlpha);

// FUN_004d5ec0: buttons only report while the pointer is over the rect.
// FUN_004d5b70 includes both edges.
struct Hit {
    bool          over = false;   // +0xc
    std::uint32_t held = 0;       // +0  (DAT_007a49fc + 0x198c)
    std::uint32_t pressed = 0;    // +4  (+0x1990)
};
[[nodiscard]] Hit hit(const GameContext& ctx, int x, int y, int w, int h);

// FUN_004d5ca0 -> FUN_004d5d10 -> FUN_004d5a60: a 2x2 box at the pointer
// against row `index` of the shared hit table (DAT_006253c8, {x, y, w, h}),
// shifted by (offX, offY). Centre distances must be strictly inside.
// (FUN_004d5c40 empties the result's list of rows and FUN_004d5bf0 appends
// the one asked for; one row is all any caller asks, so the list is gone.)
[[nodiscard]] Hit hitEntry(const GameContext& ctx, int index, int offX = 0, int offY = 0);

// FUN_004d1cf0: `digits` digits of `value`; digit d is the rect's source
// shifted right by d * w, and digit i is drawn at x + i * r.x (the rect's
// x field is the pitch). With suppressZeros, leading zeros are skipped.
void drawNumber(GameContext& ctx, const Rect& r, int x, int y, int digits, int prio,
                std::uint32_t value, bool suppressZeros = false);

// FUN_004d1ec0: digits from a font rect whose x field is the source pitch.
// Each digit advances pitch + gap; every third from the right adds
// gap + groupGap. Flags: 1 skip leading zeros, 2 group spacing in the
// width, 4 right-align at x.
enum : int { kDigitsNoZeros = 1, kDigitsGrouped = 2, kDigitsRight = 4 };
void drawDigits(GameContext& ctx, const Rect& font, int x, int y, int digits, int prio, std::uint32_t value,
                int pitch, int gap, int groupGap, int flags);

// FUN_004bb520 / FUN_004bef20 (the results screens): drawDigits with a
// comma sprite between groups of three once a digit has shown; both
// sources are shifted by (offX, offY).
void drawCommaNumber(GameContext& ctx, const Rect& font, const Rect& comma, int x, int y, int offX, int offY,
                     int digits, int prio, std::uint32_t value, int pitch, int gap, int groupGap, int flags);

// FUN_004d5fb0: up to 200 text lines with a scroll window, filled either
// line by line (add) or from a YsText .ybc entry (load + select).
// YsText files: block 2 = text table, block 3 = u32 count, then {u16 first, u16 count}.
// The destructor (FUN_004d6010; FUN_004d6640 frees a loaded file) is the members'.
class TextList {
public:
    void setFont(int px, int visible) { px_ = px; reset(visible); }      // FUN_004d6090
    void reset(int visible);                                             // FUN_004d60e0
    void add(const std::string& sjis);                                   // FUN_004d6150
    void addUtf8(std::string utf8);                                      // same, text already decoded
    int  scroll(int cmd);                                                // FUN_004d6230: 0 up, 1 down, 2 top, 3 bottom
    void load(GameContext& ctx, std::string_view legacyPath);            // FUN_004d6420
    void select(int entry, int visible);                                 // FUN_004d64d0
    void draw(GameContext& ctx, int x, int y, int prio);                 // FUN_004d62f0

    [[nodiscard]] int pos() const noexcept { return pos_; }              // +4
    [[nodiscard]] int count() const noexcept { return int(lines_.size()); }   // +10
    [[nodiscard]] int lineHeight() const noexcept { return px_ + 2; }

private:
    static constexpr int kMaxLines = 200;
    int  px_ = 12;
    int  pos_ = 0, maxPos_ = 0, visible_ = 0;        // +4, +6, +8
    bool skipFrame_ = false;                          // +0x6410: one blank frame after select()
    std::vector<std::string> lines_;                  // UTF-8
    YbcScript file_;                                  // +0x6414
};

// FUN_004d6930 / FUN_004d69e0: 100 sprite overlays that each last N frames
// (the press flash on buttons). Only kind 1 (sub-rect) is queued by the
// ported scenes (FUN_004d67f0 / FUN_004d68b0). FUN_004d66a0 constructs (clear),
// FUN_004d66e0 is its empty destructor, FUN_004d6770 frees one slot (used = false).
class Flashes {
public:
    void clear() { for (auto& e : slots_) e.used = false; }             // FUN_004d6710
    void add(const Rect& r, int prio, int x, int y, int frames);         // FUN_004d68b0
    void draw(GameContext& ctx);                                         // FUN_004d69e0
private:
    struct Entry { bool used = false; std::int16_t age = 0, frames = 0; Rect r; std::int16_t x = 0, y = 0, prio = 0; };
    std::array<Entry, 100> slots_{};
};

// Screen fade used by every Yoshimaru scene: alpha << 24 | 0x010101 at prio 0x34.
inline void fadeBlack(GameContext& ctx, int alpha) {
    fill(ctx, 0, 0, 0x400, 0x300, 0x34, std::uint32_t(alpha & 0xFF) << 24 | 0x010101u);
}

} // namespace ch::ysub
