// ============================================================================
// unit_stats.hpp — a unit's derived battle values from its record
// (src\Common: KojinTask, Preparation and battle all use it)
//
//   FUN_004db340  equipmentBonus   skill effects (FUN_004dcf00 + 24 handlers
//                                  at DAT_00738998) and item effects
//                                  (FUN_004dd510, 0x6d5118 records)
//   FUN_004b93c0  derive           0xb0-byte derived block
//
// Records stay byte images in the original layout (0x4a0-byte unit record,
// 0x4c-byte bonus, 0xb0-byte derived block) so callers port one-to-one.
// The x87 expressions the decompiler showed as bare __ftol() calls were
// read from the machine code; they run in double precision and truncate.
//
// Unit record: +0 s16, +6 s16 job, +0xc + 8i {u8 level, s32 exp} x 7,
// +0x50 u8 rank, +0x52 s16[8] skills, +0x62 s16[8] items, +0x72 u8.
// Bonus: +0x00 s16[16] + s32 +0x20 from skills, +0x24.. from items.
// Derived (selected): +0 / +2 s16, +4..+0xa stat levels, +0xc job,
// +0xe rank, +0x14 / +0x16 s16, +0x18 s16, +0x1a / +0x1c s16, +0x1e s16,
// +0x20 / +0x24 s32, +0x2c u8, +0x2e s16, +0x30 u8, +0x32 / +0x34 s16,
// +0x36 / +0x37 u8, +0x38 / +0x3a / +0x3c s16, +0x3e s16, +0x40..0x45 u8,
// +0x48 s32, +0x4c..+0x50 u8, +0x52 / +0x54 s16, +0x84 s16[16] skill list.
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "game_work.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace ch::stats {

// A little-endian byte image with typed accessors.
template <std::size_t N>
struct Bytes {
    std::array<std::uint8_t, N> b{};
    [[nodiscard]] std::uint8_t  u8(std::size_t o) const { return b.at(o); }
    [[nodiscard]] std::int8_t   s8(std::size_t o) const { return static_cast<std::int8_t>(b.at(o)); }
    [[nodiscard]] std::uint16_t u16(std::size_t o) const { return static_cast<std::uint16_t>(b.at(o) | b.at(o + 1) << 8); }
    [[nodiscard]] std::int16_t  s16(std::size_t o) const { return static_cast<std::int16_t>(u16(o)); }
    [[nodiscard]] std::int32_t  s32(std::size_t o) const {
        return static_cast<std::int32_t>(std::uint32_t(b.at(o)) | std::uint32_t(b.at(o + 1)) << 8 |
                                         std::uint32_t(b.at(o + 2)) << 16 | std::uint32_t(b.at(o + 3)) << 24);
    }
    void set8(std::size_t o, int v)  { b.at(o) = static_cast<std::uint8_t>(v); }
    void set16(std::size_t o, int v) { b.at(o) = static_cast<std::uint8_t>(v); b.at(o + 1) = static_cast<std::uint8_t>(v >> 8); }
    void set32(std::size_t o, std::int64_t v) { for (std::size_t k = 0; k < 4; ++k) b.at(o + k) = static_cast<std::uint8_t>(v >> (8 * k)); }
};

using Record  = Bytes<0x4a0>;
using Bonus   = Bytes<0x4c>;
using Derived = Bytes<0xb0>;

[[nodiscard]] Record readRecord(const GameWork& w, int unit);

// FUN_004db340
[[nodiscard]] Bonus equipmentBonus(const Record& r, GameVersion v);

// FUN_004b93c0: fills `out` (some job classes leave fields untouched, as
// in the original, so callers keep their previous block) and `bonus`.
void derive(const Record& r, Derived& out, Bonus& bonus, GameVersion v);

// FUN_004b5af0: item `item`'s storage state (bits 8-11 of 0x7aacaa + 2 id)
// flips between usable (1 / 3) and unusable (2 / 4) for the unit at
// record `rec`, by job (0x6e45f8 table) and rank.
void refreshItemState(GameWork& w, GameVersion v, std::uint32_t rec, int item);
// FUN_004b5c90: the same for skill `skill`'s slot (+0xb0 + 12 (id-1), +8):
// 2 <-> 4 and 3 <-> 5, by job level (0x6e4158 rows), category exclusions
// and class-only skills.
void refreshSkillState(GameWork& w, GameVersion v, std::uint32_t rec, int skill);

} // namespace ch::stats
