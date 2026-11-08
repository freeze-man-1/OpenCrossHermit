// ============================================================================
// tactics_hud.cpp
// ============================================================================
#include "tactics_hud.hpp"
#include "../state/game_logic.hpp"

#include <algorithm>
#include <string>

namespace ch::tactics {

namespace {
constexpr std::uint32_t kGlyphs = 0x619748;
}

void drawGlyph(GameContext& ctx, int glyph, int x, int y, int prio, std::uint8_t alpha, std::uint32_t argb) {
    const GameVersion v = ctx.version;
    const std::uint32_t g = kGlyphs + std::uint32_t(glyph) * 0x12;
    const auto r = [&](std::uint32_t o) { return std::int16_t(logic::sdS16(g + o, v)); };
    // +0xde617 alpha; 0xff draws without blending (+0xde60c = 0).
    SpriteCmd c{.bank = static_cast<std::uint16_t>(r(0)), .frame = static_cast<std::uint16_t>(r(2)),
                .x = x, .y = y, .w = r(0xc), .h = r(0xe),
                .src = {r(4), r(6), std::int16_t(r(8) + 1), std::int16_t(r(0xa) + 1)},
                .flags = std::uint16_t(kSprBank | logic::sdU8(g + 0x10, v)),
                .blend = alpha == 0xff ? kBlendTest : kBlendAlpha,
                .color = {std::uint8_t(argb >> 16), std::uint8_t(argb >> 8), std::uint8_t(argb), alpha},
                .priority = static_cast<std::uint16_t>(prio)};
    ctx.draw.sprite(c);
}

void drawDigits(GameContext& ctx, int x, int y, int prio, int value, std::uint8_t alpha, int style,
                std::uint32_t argb) {
    if (style < 0 || style > 5) return;
    value = std::clamp(value, 0, 999999);
    const std::string s = std::to_string(value);
    const int n = int(s.size());
    for (int i = 0; i < n; ++i)
        drawGlyph(ctx, (s[std::size_t(i)] - '0') + style * 10, x - n * 10 / 2 + i * 10, y, prio, alpha, argb);
}

} // namespace ch::tactics
