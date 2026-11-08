// ============================================================================
// ysub.cpp
// ============================================================================
#include "ysub.hpp"
#include "../state/game_logic.hpp"
#include "../text/sjis.hpp"

#include <algorithm>
#include <cstdlib>

namespace ch::ysub {

Rect rectAt(std::uint32_t va, GameVersion v) {
    return {.bank = logic::sdU8(va, v), .frame = static_cast<std::uint16_t>(logic::sdS16(va + 2, v)),
            .sx = logic::sdS16(va + 4, v), .sy = logic::sdS16(va + 6, v),
            .w = logic::sdS16(va + 8, v), .h = logic::sdS16(va + 10, v),
            .x = logic::sdS16(va + 12, v), .y = logic::sdS16(va + 14, v)};
}

void drawRect(GameContext& ctx, const Rect& r, int prio, int x, int y, int offX, int offY,
              std::uint8_t alpha) {
    const auto sx = static_cast<std::int16_t>(r.sx + offX), sy = static_cast<std::int16_t>(r.sy + offY);
    ctx.draw.sprite({.bank = r.bank, .frame = r.frame, .x = x, .y = y, .w = r.w, .h = r.h,
                     .src = {sx, sy, static_cast<std::int16_t>(sx + r.w), static_cast<std::int16_t>(sy + r.h)},
                     .flags = kSprSrcRect | kSprBank, .blend = kBlendAlpha,
                     .color = {0x80, 0x80, 0x80, alpha},
                     .priority = static_cast<std::uint16_t>(prio)});
}

void drawSub(GameContext& ctx, const Sub& s, int prio, int x, int y) {
    SpriteCmd c{.bank = s.bank, .frame = s.frame, .x = x, .y = y,
                .flags = kSprBank, .blend = s.blend,
                .color = {s.rgb, s.rgb, s.rgb, s.alpha},
                .priority = static_cast<std::uint16_t>(prio)};
    if (s.w != 0) {
        c.w = static_cast<std::int16_t>(s.w); c.h = static_cast<std::int16_t>(s.h);
        c.src = {static_cast<std::int16_t>(s.sx), static_cast<std::int16_t>(s.sy),
                 static_cast<std::int16_t>(s.sx + s.w), static_cast<std::int16_t>(s.sy + s.h)};
        c.flags |= kSprSrcRect;
    }
    if (s.scaleX != 100 || s.scaleY != 100) {
        c.flags |= kSprScale;
        c.scaleX = static_cast<std::int16_t>(s.scaleX); c.scaleY = static_cast<std::int16_t>(s.scaleY);
    }
    ctx.draw.sprite(c);
}

void outline(GameContext& ctx, int x0, int y0, int x1, int y1, int prio, std::uint32_t argb, std::uint8_t blend) {
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    fill(ctx, x0, y0, x1 - x0 + 1, 1, prio, argb, blend);
    fill(ctx, x0, y1, x1 - x0 + 1, 1, prio, argb, blend);
    if (y1 - y0 > 1) {
        fill(ctx, x0, y0 + 1, 1, y1 - y0 - 1, prio, argb, blend);
        fill(ctx, x1, y0 + 1, 1, y1 - y0 - 1, prio, argb, blend);
    }
}

void fill(GameContext& ctx, int x, int y, int w, int h, int prio, std::uint32_t argb, std::uint8_t blend) {
    ctx.draw.fill({.x = x, .y = y, .w = w, .h = h, .priority = static_cast<std::uint16_t>(prio),
                   .argb = argb, .blend = blend});
}

Hit hit(const GameContext& ctx, int x, int y, int w, int h) {
    const auto& m = ctx.mouse();
    const int dx = m.x - x, dy = m.y - y;
    if (dx < 0 || dy < 0 || dx > w || dy > h) return {};
    return {.over = true, .held = m.held, .pressed = m.pressed};
}

Hit hitEntry(const GameContext& ctx, int index, int offX, int offY) {
    constexpr std::uint32_t kTable = 0x6253c8;
    const std::uint32_t row = kTable + std::uint32_t(index) * 8;
    const int x = logic::sdS16(row, ctx.version), y = logic::sdS16(row + 2, ctx.version);
    const int w = logic::sdS16(row + 4, ctx.version), h = logic::sdS16(row + 6, ctx.version);
    const auto& m = ctx.mouse();
    const int dx = (m.x + 1) - (x + w / 2 + offX), dy = (m.y + 1) - (y + h / 2 + offY);
    if (std::abs(dx) >= w / 2 + 1 || std::abs(dy) >= h / 2 + 1) return {};
    return {.over = true, .held = m.held, .pressed = m.pressed};
}

void drawNumber(GameContext& ctx, const Rect& r, int x, int y, int digits, int prio,
                std::uint32_t value, bool suppressZeros) {
    bool skipping = suppressZeros;
    for (int i = 0; i < digits; ++i) {
        std::uint32_t div = 1;
        for (int k = 0; k < digits - 1 - i; ++k) div *= 10;
        const int d = int(value / div % 10);
        if (d != 0 || i + 1 == digits) skipping = false;
        if (skipping) continue;
        drawRect(ctx, r, prio, x + r.x * i, y, r.w * d, 0);      // FUN_004d28c0, alpha 0xff
    }
}

void drawDigits(GameContext& ctx, const Rect& font, int x, int y, int digits, int prio, std::uint32_t value,
                int pitch, int gap, int groupGap, int flags) {
    bool skipping = (flags & kDigitsNoZeros) != 0;
    int width = (digits != 0 ? (digits - 1) * gap : 0) + digits * pitch;
    if ((flags & kDigitsGrouped) && digits / 3 != 0) width += digits / 3 * (groupGap + gap);
    int dx = x - ((flags & kDigitsRight) ? width : 0);
    for (int i = 0; i < digits; ++i) {
        std::uint32_t div = 1;
        for (int k = 0; k < digits - 1 - i; ++k) div *= 10;
        const int d = int(value / div % 10);
        if (d != 0 || i + 1 == digits) skipping = false;
        if (!skipping) {
            Rect r = font;
            r.sx = static_cast<std::int16_t>(font.sx + font.x * d);
            drawRect(ctx, r, prio, dx, y);
        }
        dx += gap + pitch;
        if ((digits - (i + 1)) % 3 == 0) dx += gap + groupGap;
    }
}

void drawCommaNumber(GameContext& ctx, const Rect& font, const Rect& comma, int x, int y, int offX, int offY,
                     int digits, int prio, std::uint32_t value, int pitch, int gap, int groupGap, int flags) {
    bool skipping = (flags & kDigitsNoZeros) != 0;
    int width = (digits != 0 ? (digits - 1) * gap : 0) + digits * pitch;
    if ((flags & kDigitsGrouped) && digits / 3 != 0) width += digits / 3 * (groupGap + gap);
    int dx = x - ((flags & kDigitsRight) ? width : 0);
    for (int i = 0; i < digits; ++i) {
        const bool last = i + 1 == digits;
        std::uint32_t div = 1;
        for (int k = 0; k < digits - 1 - i; ++k) div *= 10;
        const int d = int(value / div % 10);
        if (d != 0 || last) skipping = false;
        if (!skipping) drawRect(ctx, font, prio, dx, y, offX + font.x * d, offY);
        dx += gap + pitch;
        if ((digits - (i + 1)) % 3 == 0 && !last && !skipping) {
            drawRect(ctx, comma, prio, dx, y, 0, offY);
            dx += gap + groupGap;
        }
    }
}

// ------------------------------------------------------------- TextList ---
void TextList::reset(int visible) {
    visible_ = visible;
    pos_ = maxPos_ = 0;
    lines_.clear();
}

void TextList::add(const std::string& raw) { addUtf8(gameTextToUtf8(raw)); }

void TextList::addUtf8(std::string utf8) {
    // The count clamps at 199: later lines land in a slot that is never shown.
    if (int(lines_.size()) < kMaxLines - 1) lines_.push_back(std::move(utf8));
    maxPos_ = std::max(0, int(lines_.size()) - visible_);
}

int TextList::scroll(int cmd) {
    switch (cmd) {
        case 0: if (pos_ != 0) --pos_; break;
        case 1: if (pos_ != maxPos_) ++pos_; break;
        case 2: pos_ = 0; break;
        case 3: pos_ = maxPos_; break;
        default: break;
    }
    return pos_;
}

void TextList::load(GameContext& ctx, std::string_view legacyPath) {
    file_ = YbcScript(ctx.readData(legacyPath));
}

void TextList::select(int entry, int visible) {
    skipFrame_ = true;
    const std::uint32_t index = file_.block(3);
    const std::uint32_t entries = file_.u32(index);
    if (std::uint32_t(entry) > entries) entry = int(entries) - 1;     // entry == count passes, as in the original
    const std::uint16_t first = file_.u16(index + 4 + std::uint32_t(entry) * 4);
    const std::uint16_t n     = file_.u16(index + 6 + std::uint32_t(entry) * 4);
    reset(visible);
    std::vector<std::string> raw;
    for (std::uint16_t i = 0; i < n; ++i) raw.emplace_back(file_.text(first + i));
    // A language pack translates the entry as one, in lines of its own.
    if (const std::string passage = translatePassage(raw); !passage.empty()) {
        for (std::size_t at = 0; at <= passage.size();) {
            const std::size_t end = std::min(passage.find('\n', at), passage.size());
            addUtf8(passage.substr(at, end - at));
            at = end + 1;
        }
    } else {
        for (const std::string& line : raw) add(line);
    }
    if (int(lines_.size()) < visible_) visible_ = int(lines_.size());
}

void TextList::draw(GameContext& ctx, int x, int y, int prio) {
    if (skipFrame_) { skipFrame_ = false; return; }
    for (int i = 0; i < visible_; ++i) {
        const int line = pos_ + i;
        if (line >= int(lines_.size())) break;
        ctx.draw.text({.x = x, .y = y, .priority = static_cast<std::uint16_t>(prio), .argb = 0xFFFFFFFFu,
                       .text = lines_[std::size_t(line)], .fontPx = static_cast<std::uint16_t>(px_)});
        y += lineHeight();
    }
}

// -------------------------------------------------------------- Flashes ---
void Flashes::add(const Rect& r, int prio, int x, int y, int frames) {
    for (Entry& e : slots_) {
        if (e.used) continue;
        e = {.used = true, .age = 0, .frames = static_cast<std::int16_t>(frames), .r = r,
             .x = static_cast<std::int16_t>(x), .y = static_cast<std::int16_t>(y), .prio = static_cast<std::int16_t>(prio)};
        return;
    }
}

void Flashes::draw(GameContext& ctx) {
    for (Entry& e : slots_) {
        if (!e.used) continue;
        drawRect(ctx, e.r, e.prio, e.x, e.y);      // kind 1: FUN_004d2ca0
        if (++e.age == e.frames) e.used = false;
    }
}

} // namespace ch::ysub
