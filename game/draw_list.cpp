// ============================================================================
// draw_list.cpp
// ============================================================================
#include "draw_list.hpp"

#include <algorithm>
#include <numeric>
#include <type_traits>

namespace ch {

namespace {
std::uint16_t priorityOf(const auto& cmd) { return cmd.priority; }

void setArgb(SDL_Renderer* r, std::uint32_t argb) {
    SDL_SetRenderDrawColor(r, std::uint8_t(argb >> 16), std::uint8_t(argb >> 8),
                           std::uint8_t(argb), std::uint8_t(argb >> 24));
}
} // namespace

// FUN_0040e330: priority high -> low; equal priority, newest first.
std::vector<std::size_t> DrawList::renderOrder() const {
    std::vector<std::size_t> order(cmds_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto pa = std::visit([](const auto& c) { return priorityOf(c); }, cmds_[a]);
        const auto pb = std::visit([](const auto& c) { return priorityOf(c); }, cmds_[b]);
        return pa != pb ? pa > pb : a > b;
    });
    return order;
}

std::size_t DrawList::countSprites() const noexcept {
    std::size_t n = 0;
    for (const auto& c : cmds_) n += std::holds_alternative<SpriteCmd>(c);
    return n;
}

void DrawList::dump(std::FILE* f, int tag) const {
    for (std::size_t i : renderOrder()) {
        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, SpriteCmd>) {
                std::fprintf(f, "%d S %u %u %d %d %d %d %u %u %u %u %u %u %d %d %d %u %d %d %d %d\n", tag,
                             c.bank, c.frame, c.x, c.y, c.w, c.h, c.flags, c.blend, c.color.a, c.color.r,
                             c.color.g, c.color.b, c.scaleX, c.scaleY, c.rotation, c.priority,
                             c.src.x0, c.src.y0, c.src.x1, c.src.y1);
            } else if constexpr (std::is_same_v<T, FillCmd>) {
                std::fprintf(f, "%d F %d %d %d %d %u %08x\n", tag, c.x, c.y, c.w, c.h, c.priority, c.argb);
            } else if constexpr (std::is_same_v<T, GeometryCmd>) {
                std::fprintf(f, "%d G %zu %u %08x\n", tag, c.points.size(), c.priority, c.argb);
            } else {
                std::fprintf(f, "%d T %d %d %u %08x %s\n", tag, c.x, c.y, c.priority, c.argb, c.text.c_str());
            }
        }, cmds_[i]);
    }
}

void DrawList::render(SDL_Renderer* r, ISpriteBanks& banks, int layer) const {
    const auto order = renderOrder();

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (std::size_t i : order) {
        if (layer >= 0 && layers_[i] != layer) continue;
        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, SpriteCmd>) {
                banks.drawSprite(r, c);
            } else if constexpr (std::is_same_v<T, FillCmd>) {
                setArgb(r, c.argb);
                // Mode 0 (alpha test) fills are opaque: their colours carry alpha 0.
                if (c.blend == kBlendAdd) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
                else if (c.blend == kBlendTest) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                const SDL_Rect rc{c.x, c.y, c.w, c.h};
                SDL_RenderFillRect(r, &rc);
                if (c.blend != kBlendAlpha) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            } else if constexpr (std::is_same_v<T, GeometryCmd>) {
                const SDL_Color col{std::uint8_t(c.argb >> 16), std::uint8_t(c.argb >> 8), std::uint8_t(c.argb),
                                    std::uint8_t(c.argb >> 24)};
                std::vector<SDL_Vertex> v(c.points.size());
                for (std::size_t k = 0; k < v.size(); ++k) v[k] = {c.points[k], col, {0, 0}};
                SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), nullptr, 0);
            } else {
                banks.drawText(r, c);
            }
        }, cmds_[i]);
    }
}

} // namespace ch
