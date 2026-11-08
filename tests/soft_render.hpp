// Software reference renderer for headless tests: rasterises the same sprite
// semantics as BankSprites (flags, blend modes, 0x80 = 1.0 colour, specular)
// into a 1024x768 framebuffer and writes PNGs. Fills arrive via sdl_fake.cpp.
#pragma once
#include "../game/bank_format.hpp"
#include "../game/draw_list.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace soft {

inline constexpr int W = 1024, H = 768;
inline std::vector<std::uint32_t> fb(W * H, 0xFF000000u);
inline bool enabled = false;

inline void blend(int x, int y, std::uint32_t argb, bool additive) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    std::uint32_t& d = fb[std::size_t(y) * W + x];
    const unsigned a = argb >> 24;
    auto ch = [&](int s) {
        const unsigned sc = (argb >> s) & 0xFF, dc = (d >> s) & 0xFF;
        const unsigned v = additive ? dc + sc * a / 255 : (sc * a + dc * (255 - a)) / 255;
        return std::min(255u, v) << s;
    };
    d = 0xFF000000u | ch(16) | ch(8) | ch(0);
}

inline void fillRect(int x, int y, int w, int h, std::uint32_t argb) {
    for (int j = std::max(0, y); j < std::min(H, y + h); ++j)
        for (int i = std::max(0, x); i < std::min(W, x + w); ++i) blend(i, j, argb, false);
}

class Banks final : public ch::ISpriteBanks {
public:
    void loadBank(std::uint16_t bank, std::vector<std::uint8_t> bytes) override {
        auto& b = banks_[bank];
        b.bytes = std::move(bytes);
        b.entries = ch::splitBank(b.bytes);
        b.images.assign(b.entries.size(), {});
        b.done.assign(b.entries.size(), false);
    }
    void unloadBank(std::uint16_t bank) override { banks_.erase(bank); }
    void drawText(SDL_Renderer*, const ch::TextCmd&) override {}
    std::pair<int, int> frameSize(std::uint16_t bank, std::uint16_t i) override {
        auto it = banks_.find(bank);
        return it != banks_.end() && i < it->second.entries.size() ? ch::entrySize(it->second.entries[i])
                                                                  : std::pair<int, int>{0, 0};
    }

    void drawSprite(SDL_Renderer*, const ch::SpriteCmd& c) override {
        if (!enabled) return;
        const ch::Image* img = image(c.bank, c.frame);
        if (!img) return;
        int sx0 = 0, sy0 = 0, sw = img->w, sh = img->h;
        if (c.flags & ch::kSprSrcRect) { sx0 = c.src.x0; sy0 = c.src.y0; sw = c.src.x1 - c.src.x0; sh = c.src.y1 - c.src.y0; }
        const float bw = float(c.w ? c.w : img->w), bh = float(c.h ? c.h : img->h);
        const float dw = (c.flags & ch::kSprScale) ? bw * c.scaleX / 100.f : bw;
        const float dh = (c.flags & ch::kSprScale) ? bh * c.scaleY / 100.f : bh;
        float px = float(c.x), py = float(c.y);
        if (c.flags & ch::kSprCentre) { px -= dw / 2; py -= dh / 2; }
        const int ang = (c.flags & ch::kSprRotate) ? c.rotation % 360 : 0;
        const float cx = px + ((c.flags & ch::kSprPivotCentre) ? dw / 2 : c.pivotX);
        const float cy = py + ((c.flags & ch::kSprPivotCentre) ? dh / 2 : c.pivotY);
        const float rad = float(ang) * 3.14159265f / 180.f, cs = std::cos(rad), sn = std::sin(rad);

        std::uint8_t a = c.color.a;
        const bool add = c.blend == ch::kBlendAdd;
        if (!add && !(c.blend == ch::kBlendAlpha && a != 0xFF)) a = a > 8 ? 0xFF : 0;
        if (!a || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
        auto dif = [](unsigned v) { return std::min(255u, v * 2); };
        auto spec = [](unsigned v) { return v <= 0x80 ? 0u : v == 0xFF ? 255u : (v - 0x80) * 2; };

        // Bounding box of the (possibly rotated) quad.
        const float r = ang ? std::hypot(dw, dh) : 0;
        const int x0 = int(std::floor(ang ? cx - r : px)), x1 = int(std::ceil(ang ? cx + r : px + dw));
        const int y0 = int(std::floor(ang ? cy - r : py)), y1 = int(std::ceil(ang ? cy + r : py + dh));
        for (int y = std::max(0, y0); y < std::min(H, y1); ++y) {
            for (int x = std::max(0, x0); x < std::min(W, x1); ++x) {
                float fx = x + 0.5f, fy = y + 0.5f;
                if (ang) {   // inverse-rotate the pixel centre into quad space
                    const float ux = fx - cx, uy = fy - cy;
                    fx = cx + ux * cs + uy * sn;
                    fy = cy - ux * sn + uy * cs;
                }
                float u = (fx - px) / dw, v = (fy - py) / dh;
                if (u < 0 || v < 0 || u >= 1 || v >= 1) continue;
                if (c.flags & ch::kSprFlipU) u = 1 - u;
                if (c.flags & ch::kSprFlipV) v = 1 - v;
                const int tx = sx0 + int(u * sw), ty = sy0 + int(v * sh);
                if (tx < 0 || ty < 0 || tx >= img->w || ty >= img->h) continue;
                const std::uint32_t t = img->argb[std::size_t(ty) * img->w + tx];
                if (!(t >> 24)) continue;
                unsigned rr = std::min(255u, ((t >> 16) & 0xFF) * dif(c.color.r) / 255 + spec(c.color.r));
                unsigned gg = std::min(255u, ((t >> 8) & 0xFF) * dif(c.color.g) / 255 + spec(c.color.g));
                unsigned bb = std::min(255u, (t & 0xFF) * dif(c.color.b) / 255 + spec(c.color.b));
                blend(x, y, std::uint32_t(a) << 24 | rr << 16 | gg << 8 | bb, add);
            }
        }
    }

private:
    struct Bank {
        std::vector<std::uint8_t> bytes;
        std::vector<std::span<const std::uint8_t>> entries;
        std::vector<ch::Image> images;
        std::vector<bool> done;
    };
    const ch::Image* image(std::uint16_t bank, std::uint16_t i) {
        auto it = banks_.find(bank);
        if (it == banks_.end() || i >= it->second.entries.size()) return nullptr;
        Bank& b = it->second;
        if (!b.done[i]) { b.images[i] = ch::decodeBankEntry(b.entries[i]); b.done[i] = true; }
        return b.images[i].empty() ? nullptr : &b.images[i];
    }
    std::map<std::uint16_t, Bank> banks_;
};

// Minimal PNG writer (stored deflate blocks, no compression).
inline void writePng(const std::string& path) {
    auto crc = [](const std::vector<std::uint8_t>& d, std::size_t o, std::size_t n) {
        std::uint32_t c = 0xFFFFFFFFu;
        for (std::size_t i = o; i < o + n; ++i) {
            c ^= d[i];
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
        return ~c;
    };
    std::vector<std::uint8_t> raw;
    raw.reserve(std::size_t(H) * (W * 3 + 1));
    for (int y = 0; y < H; ++y) {
        raw.push_back(0);
        for (int x = 0; x < W; ++x) {
            const std::uint32_t p = fb[std::size_t(y) * W + x];
            raw.push_back(std::uint8_t(p >> 16)); raw.push_back(std::uint8_t(p >> 8)); raw.push_back(std::uint8_t(p));
        }
    }
    std::vector<std::uint8_t> z{0x78, 0x01};
    std::uint32_t s1 = 1, s2 = 0;
    for (std::size_t o = 0; o < raw.size(); o += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - o);
        z.push_back(o + n == raw.size());
        z.push_back(std::uint8_t(n)); z.push_back(std::uint8_t(n >> 8));
        z.push_back(std::uint8_t(~n)); z.push_back(std::uint8_t(~n >> 8));
        for (std::size_t i = o; i < o + n; ++i) { s1 = (s1 + raw[i]) % 65521; s2 = (s2 + s1) % 65521; z.push_back(raw[i]); }
    }
    const std::uint32_t ad = s2 << 16 | s1;
    for (int k = 3; k >= 0; --k) z.push_back(std::uint8_t(ad >> (8 * k)));

    std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    auto chunk = [&](const char* type, const std::vector<std::uint8_t>& data) {
        const std::uint32_t n = std::uint32_t(data.size());
        for (int k = 3; k >= 0; --k) out.push_back(std::uint8_t(n >> (8 * k)));
        const std::size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        const std::uint32_t c = crc(out, start, out.size() - start);
        for (int k = 3; k >= 0; --k) out.push_back(std::uint8_t(c >> (8 * k)));
    };
    std::vector<std::uint8_t> ihdr{0, 0, std::uint8_t(W >> 8), std::uint8_t(W), 0, 0, std::uint8_t(H >> 8),
                                   std::uint8_t(H), 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(out.data(), 1, out.size(), f); std::fclose(f); }
}

} // namespace soft
