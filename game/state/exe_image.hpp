// ============================================================================
// exe_image.hpp — the two executables' data, as the importer stores it
// ============================================================================
// Nothing from the executables is compiled into the port: the importer
// (game/import) writes the player's own copies into the data folder and the
// tables and texts are read from them by original address.
//   static/ch_data.bin  .rdata + .data of the TW build the port was
//                       decompiled from (VA 0x592000..0x80c000), rebuilt by
//                       resources/ch_data.chpatch (tools/make_ch_patch.py)
//   static/jp.exe       the JP 1.2 executable (Cross Hermit Update 1.2)
// Game logic reads the TW build's tables in both versions (logic::sdU8 and
// friends, with the few tables that differ taken from the JP exe); the
// versions' own texts come from their own executable (text(), texts()).
#pragma once
#include "../game_context.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ch::exe {

// Bytes at a base VA; addresses outside it read as 0.
class Image {
public:
    Image() = default;
    Image(std::uint32_t base, std::vector<std::uint8_t> bytes) : base_(base), bytes_(std::move(bytes)) {}
    // A PE file's sections laid out at their addresses (uninitialised data zero).
    static Image fromPe(std::span<const std::uint8_t> file);

    [[nodiscard]] bool contains(std::uint32_t va, std::size_t n = 1) const {
        return va >= base_ && va - base_ <= bytes_.size() && n <= bytes_.size() - (va - base_);
    }
    [[nodiscard]] std::uint8_t u8(std::uint32_t va) const {
        if (traced_) note(va, 1);
        return contains(va) ? bytes_[va - base_] : 0;
    }
    [[nodiscard]] std::uint16_t u16(std::uint32_t va) const { return std::uint16_t(u8(va) | u8(va + 1) << 8); }
    [[nodiscard]] std::uint32_t u32(std::uint32_t va) const { return u16(va) | std::uint32_t(u16(va + 2)) << 16; }
    [[nodiscard]] std::int16_t s16(std::uint32_t va) const { return std::int16_t(u16(va)); }
    [[nodiscard]] std::int32_t s32(std::uint32_t va) const { return std::int32_t(u32(va)); }
    // The bytes [va, va + n); throws if they are not all in the image.
    [[nodiscard]] std::span<const std::uint8_t> bytes(std::uint32_t va, std::size_t n) const;
    // The NUL-terminated string at va, in the build's code page ("" outside the image).
    [[nodiscard]] std::string_view cstr(std::uint32_t va) const;
    [[nodiscard]] std::uint32_t base() const { return base_; }
    [[nodiscard]] std::size_t size() const { return bytes_.size(); }
    [[nodiscard]] std::vector<std::uint8_t>& raw() { return bytes_; }
    [[nodiscard]] bool empty() const { return bytes_.empty(); }
    // CH_TRACE_TWONLY (exe_image.cpp): reads of this image are checked.
    void setTraced(bool on) { traced_ = on; }

private:
    void note(std::uint32_t va, std::size_t n) const;
    std::uint32_t base_ = 0;
    std::vector<std::uint8_t> bytes_;
    bool traced_ = false;
};

inline constexpr std::uint32_t kChDataLo = 0x592000, kChDataHi = 0x80c000;

// The decompiled TW build's data (static/ch_data.bin).
const Image& chData();
// That data as the given version's logic sees it: the JP version has the
// tables that differ between the builds (kJpOverrides in exe_image.cpp) from
// its own exe.
const Image& logicData(GameVersion v);
// The version's own executable: ch_data for TW, jp.exe for JP. Its texts and
// layouts are at that build's addresses.
const Image& own(GameVersion v);

// The string at `va` of the version's own exe, as UTF-8.
[[nodiscard]] std::string text(GameVersion v, std::uint32_t va);
// `count` string pointers at `table` of the version's own exe, as UTF-8
// (null pointers and pointers outside the image give "").
[[nodiscard]] std::vector<std::string> texts(GameVersion v, std::uint32_t table, std::size_t count,
                                             std::size_t stride = 4);
// texts(), loaded once (until reload()).
[[nodiscard]] const std::vector<std::string>& strings(GameVersion v, std::uint32_t table, std::size_t count,
                                                      std::size_t stride = 4);
// The same strings as `const char*` (for tables used as `const char*` arrays).
[[nodiscard]] const std::vector<const char*>& textTable(GameVersion v, std::uint32_t table, std::size_t count,
                                                        std::size_t stride = 4);

// Drops the loaded images (after an import replaced the files).
void reload();

} // namespace ch::exe
