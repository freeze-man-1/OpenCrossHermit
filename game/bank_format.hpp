// ============================================================================
// bank_format.hpp — sprite bank container (.bin) decoding, SDL-free
//
// Container (FUN_0041f0e0 / FUN_004214f0):
//   u32 totalSize, u32 count, u32 offset[count]   entry i spans offset[i]..offset[i+1]
// Entries (FUN_0041f340):
//   "BM"            8bpp Windows BMP, palette colour key      FUN_004048d0 / FUN_00404150
//   "DX" mode 2     A1R5G5B5 texels, top-down                 FUN_00404fb0 / FUN_004046f0
//   "DX" mode 1     P8 (D3DFMT 0x29), not used by the title   FUN_00404690
//   "Dm-No-MakeTex" 16-byte placeholder, no texture
//   "CH" (the port)  u16 0, u16 w, u16 h, 32-bit ARGB rows: frames a language pack painted on
// ============================================================================
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ch {

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint32_t> argb;   // 0xAARRGGBB, row-major, top-down
    [[nodiscard]] bool empty() const noexcept { return w == 0 || h == 0; }
};

// Throws std::runtime_error on a malformed container.
[[nodiscard]] std::vector<std::span<const std::uint8_t>> splitBank(std::span<const std::uint8_t> bank);

// Image size from the entry header only (no decode); {0, 0} if unknown.
[[nodiscard]] std::pair<int, int> entrySize(std::span<const std::uint8_t> entry);

// Decodes one entry. Unsupported or placeholder entries return an empty Image
// and set `why`.
[[nodiscard]] Image decodeBankEntry(std::span<const std::uint8_t> entry, std::string* why = nullptr);

[[nodiscard]] std::vector<std::uint8_t> encodeRgbaEntry(const Image& img);

} // namespace ch
