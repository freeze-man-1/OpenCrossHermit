// ============================================================================
// tactics_hud.hpp — the battle HUD's shared drawing helpers
//
// The glyph table DAT_00619748 (0x12 bytes per glyph: s16 bank, frame,
// x0, y0, x1, y1 inclusive, w, h, u8 flags) holds the HUD's digits
// (styles 0..5, ten glyphs each) and the popup words (60 onwards).
//   FUN_00495fe0  a number, centred, ten pixels a digit
//   FUN_00495ab0  one glyph (its sprite half)
// ============================================================================
#pragma once

#include "../game_context.hpp"

#include <cstdint>

namespace ch::tactics {

// One glyph at (x, y): its bank and frame, the source rectangle, the size,
// the table's flags; the colour at 0x80 grey, `alpha` 0xff drawn solid.
void drawGlyph(GameContext& ctx, int glyph, int x, int y, int prio, std::uint8_t alpha,
               std::uint32_t argb = 0xff808080u);

// FUN_00495fe0(x, y, prio, value, alpha, style, argb): `value` (0..999999,
// no leading zeros, at least one digit) in digit style 0..5, centred on x.
void drawDigits(GameContext& ctx, int x, int y, int prio, int value, std::uint8_t alpha, int style,
                std::uint32_t argb);

} // namespace ch::tactics
