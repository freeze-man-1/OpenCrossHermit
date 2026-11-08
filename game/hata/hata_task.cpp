// ============================================================================
// hata_task.cpp — HataTask: table reads and the bank-10 draw helpers
// ============================================================================
#include "hata_task.hpp"

#include "../lang/language_pack.hpp"

#include <SDL.h>

#include <cstdio>

namespace ch {

std::uint8_t HataTask::T8(std::uint32_t va) const {
    const std::span<const std::uint8_t> t = table_;
    if (va < tablesVa_ || va - tablesVa_ >= t.size()) {
        char msg[64];
        std::snprintf(msg, sizeof msg, "HataTask table read at VA 0x%08x", va);
        throw std::out_of_range(msg);
    }
    return t[va - tablesVa_];
}

void HataTask::opSprite(int prio, int frame, int x, int y, std::uint16_t flags, SrcRect src,
                        int w, int h, Rgba8 color, int scaleX, int scaleY, int rot, std::uint8_t blend) {
    // Debug: CH_OP_TRACE=<file> logs every sprite drawn from the sheets that hold text.
    static std::FILE* const trace = [] { const char* v = SDL_getenv("CH_OP_TRACE"); return v ? std::fopen(v, "w") : nullptr; }();
    if (trace && (frame == 0 || frame == 1 || frame == 13 || frame == 22))
        std::fprintf(trace, "%llu %d %d %d %d %d %d %d %d %d %u %u %u %u %d %d %d %d %d\n", static_cast<unsigned long long>(ctx_.engine.tickCount()), frame, src.x0, src.y0, src.x1, src.y1,
                     x, y, w, h, flags, color.r, color.g, color.a, scaleX, scaleY, rot, prio, blend);
    // A language pack's replacement text may not fit the old rectangle.
    if ((flags & kSprSrcRect) && !layoutBank_.empty() && src.x1 > src.x0 && src.y1 > src.y0) {
        if (const auto* parts = lang::spriteParts(layoutBank_, frame, src.x0, src.y0, src.x1, src.y1)) {
            const int oldW = src.x1 - src.x0, oldH = src.y1 - src.y0;
            for (const lang::SpritePart& p : *parts) {
                const int pw = p.x1 - p.x0, ph = p.y1 - p.y0;
                // The part's top left, or its centre when (x, y) is the sprite's centre.
                const int ox = flags & kSprCentre ? 2 * p.dx + pw - oldW : p.dx, oy = flags & kSprCentre ? 2 * p.dy + ph - oldH : p.dy;
                const int div = flags & kSprCentre ? 2 : 1;
                ctx_.draw.sprite({.bank = kBank, .frame = static_cast<std::uint16_t>(p.frame),
                                  .x = x + ox * w / oldW / div, .y = y + oy * h / oldH / div,
                                  .w = static_cast<std::int16_t>(pw * w / oldW), .h = static_cast<std::int16_t>(ph * h / oldH),
                                  .src = {static_cast<std::int16_t>(p.x0), static_cast<std::int16_t>(p.y0),
                                          static_cast<std::int16_t>(p.x1), static_cast<std::int16_t>(p.y1)},
                                  .flags = static_cast<std::uint16_t>(flags | kSprBank), .blend = blend,
                                  .color = color, .scaleX = static_cast<std::int16_t>(scaleX),
                                  .scaleY = static_cast<std::int16_t>(scaleY), .rotation = static_cast<std::int16_t>(rot),
                                  .priority = static_cast<std::uint16_t>(prio)});
            }
            return;
        }
    }
    ctx_.draw.sprite({.bank = kBank, .frame = static_cast<std::uint16_t>(frame), .x = x, .y = y,
                      .w = static_cast<std::int16_t>(w), .h = static_cast<std::int16_t>(h),
                      .src = src, .flags = static_cast<std::uint16_t>(flags | kSprBank), .blend = blend,
                      .color = color, .scaleX = static_cast<std::int16_t>(scaleX),
                      .scaleY = static_cast<std::int16_t>(scaleY), .rotation = static_cast<std::int16_t>(rot),
                      .priority = static_cast<std::uint16_t>(prio)});
}

namespace {
SrcRect rect(int sx, int sy, int sw, int sh) {
    return {static_cast<std::int16_t>(sx), static_cast<std::int16_t>(sy),
            static_cast<std::int16_t>(sx + sw), static_cast<std::int16_t>(sy + sh)};
}
Rgba8 argb(int a, int r, int g, int b) {
    return {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b),
            static_cast<std::uint8_t>(a)};
}
} // namespace

// FUN_00504f10: the whole frame, untinted. (The sprite's size is the frame's.)
void HataTask::opDraw(int prio, int frame, int x, int y) {
    opSprite(prio, frame, x, y, 0, {}, 0, 0, {}, 100, 100, 0, kBlendAlpha);
}

void HataTask::opDrawSrc(int prio, int frame, int x, int y, int sx, int sy, int w, int h) {
    opSprite(prio, frame, x, y, kSprSrcRect, rect(sx, sy, w, h), w, h, {}, 100, 100, 0, kBlendAlpha);
}

// FUN_00505260: the whole frame, tinted.
void HataTask::opDrawTint(int prio, int frame, int x, int y, int a, int r, int g, int b) {
    opSprite(prio, frame, x, y, 0, {}, 0, 0, argb(a, r, g, b), 100, 100, 0, kBlendAlpha);
}

void HataTask::opDrawSrcTint(int prio, int frame, int x, int y, int sx, int sy, int w, int h,
                             int a, int r, int g, int b) {
    opSprite(prio, frame, x, y, kSprSrcRect, rect(sx, sy, w, h), w, h, argb(a, r, g, b), 100, 100, 0, kBlendAlpha);
}

void HataTask::opDrawStretchC(int prio, int frame, int x, int y, int dw, int dh, int sw, int sh) {
    opSprite(prio, frame, x, y, kSprSrcRect | kSprCentre, rect(0, 0, sw, sh), dw, dh, {}, 100, 100, 0, kBlendAlpha);
}

void HataTask::opDrawSrcSizeC(int prio, int frame, int x, int y, int sx, int sy, int dw, int dh, int sw, int sh) {
    opSprite(prio, frame, x, y, kSprSrcRect | kSprCentre, rect(sx, sy, sw, sh), dw, dh, {}, 100, 100, 0, kBlendAlpha);
}

void HataTask::opDrawSrcSizeTintC(int prio, int frame, int x, int y, int sx, int sy, int dw, int dh,
                                  int a, int r, int g, int b, int sw, int sh) {
    opSprite(prio, frame, x, y, kSprSrcRect | kSprCentre, rect(sx, sy, sw, sh), dw, dh, argb(a, r, g, b),
             100, 100, 0, kBlendAlpha);
}

void HataTask::opDrawSrcAdd(int prio, int frame, int x, int y, int sx, int sy, int w, int h) {
    opSprite(prio, frame, x, y, kSprSrcRect, rect(sx, sy, w, h), w, h, {}, 100, 100, 0, kBlendAdd);
}

void HataTask::fillRect(int x, int y, int w, int h, int prio, std::uint32_t c, int mode) {
    ctx_.draw.fill({.x = x, .y = y, .w = w, .h = h, .priority = static_cast<std::uint16_t>(prio),
                    .argb = mode ? c : (c | 0xFF000000u)});
}

} // namespace ch
