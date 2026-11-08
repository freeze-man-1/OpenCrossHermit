// ============================================================================
// bank_format.cpp
// ============================================================================
#include "bank_format.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace ch {

namespace {
std::uint16_t rd16(std::span<const std::uint8_t> d, std::size_t o) {
    if (o + 2 > d.size()) throw std::out_of_range("bank: read past end");
    return static_cast<std::uint16_t>(d[o] | d[o + 1] << 8);
}
std::uint32_t rd32(std::span<const std::uint8_t> d, std::size_t o) {
    if (o + 4 > d.size()) throw std::out_of_range("bank: read past end");
    return std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 |
           std::uint32_t(d[o + 2]) << 16 | std::uint32_t(d[o + 3]) << 24;
}

Image fail(std::string* why, const char* msg) {
    if (why) *why = msg;
    return {};
}

// FUN_004048d0 + FUN_00404150, colour-key mode (both flags == 1 for banks):
//  * every pure-black palette entry except 0 is replaced by entry 0, then
//  * index 0, and any index whose colour equals entry 0, is transparent.
// Pixels keep full 8-bit precision (the original truncated to 5 bits).
Image decodeBmp8(std::span<const std::uint8_t> e, std::string* why) {
    const std::uint32_t pixOff = rd32(e, 10);
    const auto w   = static_cast<std::int32_t>(rd32(e, 18));
    const auto h   = static_cast<std::int32_t>(rd32(e, 22));
    const auto bpp = rd16(e, 28);
    if (bpp != 8) return fail(why, "BMP: only 8bpp is used by banks");
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return fail(why, "BMP: bad size");

    std::uint32_t colours = rd32(e, 46);
    if (colours == 0) colours = (pixOff - 0x36) >> 2;            // FUN_00420a40
    colours = std::min<std::uint32_t>(colours, 256);

    std::array<std::array<std::uint8_t, 3>, 256> pal{};           // B, G, R
    for (std::uint32_t i = 0; i < colours; ++i)
        for (int c = 0; c < 3; ++c) pal[i][c] = e[0x36 + i * 4 + c];
    for (std::uint32_t i = 1; i < colours; ++i)
        if (pal[i] == std::array<std::uint8_t, 3>{0, 0, 0}) pal[i] = pal[0];

    std::array<std::uint32_t, 256> lut{};
    for (std::uint32_t i = 1; i < 256; ++i) {
        if (pal[i] == pal[0]) continue;                            // transparent
        lut[i] = 0xFF000000u | std::uint32_t(pal[i][2]) << 16 | std::uint32_t(pal[i][1]) << 8 | pal[i][0];
    }

    const std::size_t stride = (std::size_t(w) + 3) & ~std::size_t{3};
    if (pixOff + stride * std::size_t(h) > e.size()) return fail(why, "BMP: truncated pixels");

    Image img{w, h, std::vector<std::uint32_t>(std::size_t(w) * h)};
    for (std::int32_t y = 0; y < h; ++y) {
        const std::uint8_t* row = e.data() + pixOff + std::size_t(h - 1 - y) * stride;   // bottom-up
        for (std::int32_t x = 0; x < w; ++x) img.argb[std::size_t(y) * w + x] = lut[row[x]];
    }
    return img;
}

// FUN_004046f0: verbatim A1R5G5B5 rows of the padded texture (tw x th).
Image decodeDx16(std::span<const std::uint8_t> e, std::string* why) {
    const int w  = rd16(e, 4), h  = rd16(e, 6);
    const int tw = rd16(e, 8), th = rd16(e, 10);
    if (w == 0 || h == 0 || w > tw || h > th) return fail(why, "DX: bad size");
    if (12 + std::size_t(tw) * th * 2 > e.size()) return fail(why, "DX: truncated texels");

    Image img{w, h, std::vector<std::uint32_t>(std::size_t(w) * h)};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint16_t p = rd16(e, 12 + (std::size_t(y) * tw + x) * 2);
            const std::uint32_t r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
            img.argb[std::size_t(y) * w + x] =
                (p & 0x8000 ? 0xFF000000u : 0u) |
                ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
        }
    }
    return img;
}
} // namespace

std::vector<std::uint8_t> encodeRgbaEntry(const Image& img) {
    std::vector<std::uint8_t> e(8 + img.argb.size() * 4);
    e[0] = 'C';
    e[1] = 'H';
    e[4] = std::uint8_t(img.w);
    e[5] = std::uint8_t(img.w >> 8);
    e[6] = std::uint8_t(img.h);
    e[7] = std::uint8_t(img.h >> 8);
    for (std::size_t i = 0; i < img.argb.size(); ++i)
        for (int k = 0; k < 4; ++k) e[8 + i * 4 + std::size_t(k)] = std::uint8_t(img.argb[i] >> (k * 8));
    return e;
}

std::vector<std::span<const std::uint8_t>> splitBank(std::span<const std::uint8_t> bank) {
    const std::uint32_t total = rd32(bank, 0);
    const std::uint32_t count = rd32(bank, 4);
    if (total > bank.size()) throw std::runtime_error("bank: header size exceeds file");
    if (count > 0x10000 || 8 + std::size_t(count) * 4 > total) throw std::runtime_error("bank: bad entry count");

    std::vector<std::span<const std::uint8_t>> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t beg = rd32(bank, 8 + i * 4);
        const std::uint32_t end = (i + 1 < count) ? rd32(bank, 8 + (i + 1) * 4) : total;
        if (beg > end || end > total) throw std::runtime_error("bank: bad entry offset");
        out.push_back(bank.subspan(beg, end - beg));
    }
    return out;
}

std::pair<int, int> entrySize(std::span<const std::uint8_t> e) {
    if (e.size() >= 0x36 && e[0] == 'B' && e[1] == 'M')
        return {static_cast<std::int32_t>(rd32(e, 18)), static_cast<std::int32_t>(rd32(e, 22))};
    if (e.size() >= 12 && e[0] == 'D' && e[1] == 'X') return {rd16(e, 4), rd16(e, 6)};
    if (e.size() >= 8 && e[0] == 'C' && e[1] == 'H') return {rd16(e, 4), rd16(e, 6)};
    return {0, 0};
}

Image decodeBankEntry(std::span<const std::uint8_t> e, std::string* why) {
    static constexpr char kNoTex[] = "Dm-No-MakeTex";
    if (e.size() == 0x10 && std::memcmp(e.data(), kNoTex, sizeof kNoTex - 1) == 0)
        return fail(why, "Dm-No-MakeTex placeholder");
    if (e.size() >= 0x36 && e[0] == 'B' && e[1] == 'M') return decodeBmp8(e, why);
    if (e.size() >= 12 && e[0] == 'D' && e[1] == 'X') {
        switch (rd16(e, 2) & 7) {
            case 2:  return decodeDx16(e, why);
            case 1:  return fail(why, "DX mode 1 (P8) not ported yet");
            default: return fail(why, "DX: unknown mode");
        }
    }
    if (e.size() >= 8 && e[0] == 'C' && e[1] == 'H') {
        const int w = rd16(e, 4), h = rd16(e, 6);
        if (8 + std::size_t(w) * h * 4 > e.size()) return fail(why, "CH: truncated texels");
        Image img{w, h, std::vector<std::uint32_t>(std::size_t(w) * h)};
        for (std::size_t i = 0; i < img.argb.size(); ++i) img.argb[i] = rd32(e, 8 + i * 4);
        return img;
    }
    return fail(why, "unknown entry type");
}

} // namespace ch
