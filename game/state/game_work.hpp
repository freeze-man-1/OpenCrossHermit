// ============================================================================
// game_work.hpp — the original's game-state globals as one memory image
//
// Calendar, roster, items, per-unit progress and the 65 unit records live in
// ch.exe .bss between 0x7a4e00 and 0x809348 (all zero in the image). New
// Game clears and fills it (FUN_004e1b60); scenes and scripts read and write
// it by address. Keeping it as one little-endian image, addressed by ch.exe
// VA, lets decompiled code port one-to-one and makes the save data a plain
// copy of these bytes. Both builds use the same layout (JP addresses differ
// only by base).
//
// Known layout (ch.exe VAs):
//   0x7a4e62  s16  CH002 played this week (WorkRoom)
//   0x7a5120  u8[0xf0]   in roster, by unit id           (FUN_004d4730 / 4770)
//   0x7a5210  s16[0x28]  members (id < 0x65), count 0x7a5260
//   0x7a5262  s16[0x14]  guests  (id >= 0x65), count 0x7a528a
//   0x7a528c  s16  difficulty (0/1 normal, else hard)
//   0x7a528e  s16  month    0x7a5290 s16 week    0x7a5292 s16 day (background)
//   0x7a54ca  u8[300]    script flags (start at 5)
//   0x7a55f6  s16  area BGM (opcode 0x97)
//   0x7aacaa  u16[0x168] items: bit0 owned, bits 1-7 owner, bits 8-11 state
//   0x7cf34c  0x124-byte unit progress, by unit id:
//             +0xd8 u8[0x20] job levels (100 = mastered), +0xf9 u8[0x20] job open,
//             +0x11c s32 score (opcode 0xab), +0x120 s16 (AllResult)
//   0x7d3d70  0x44 x 67, copied from 0x74e4b0 at New Game
//   0x7d4f3c  u8[0x80]   seen flags (opcode 0x9c)
//   0x7e1180 / 0x7e1182 s16 (scripts), 0x7e11a0 u8 area BGM playing, 0x7e11a8 s16
//   0x7a4ae8  s16 count, 0x7a4aec s16[100]: units in the current battle (Preparation)
//   0x7f4488  0x8c bytes: the current stage's battle settings (FUN_004da8e0)
//   0x7f4518  0xb0 x n: derived values of the battle's units (FUN_004b9340)
//   0x7a50e8  0x306d4 bytes: the save data (logic::saveGame / loadGame)
//   0x7f7c18  0x11730 bytes copied into saves at 0x7aaf7c (cleared by FUN_004e1a10)
//   0x7e17e8  0x4a0-byte unit records x 65, from the 0x6f5088 templates:
//             +6 s16 job, +0xc + 8i {u8 level, u32 exp} x 7 stats, +0x50 u8 rank,
//             +0x52 s16[8], +0x62 s16[8] equipped item ids, +0xb0 0x54 x 12-byte slots
// ============================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace ch {

class GameWork {
public:
    static constexpr std::uint32_t kBase = 0x7a4a00;   // from the sortie member list (0x7a4ae8)
    static constexpr std::uint32_t kEnd  = 0x809800;   // through the battle state at 0x8093f8 and the player name at 0x809694

    GameWork() : mem_(kEnd - kBase, 0) {}

    [[nodiscard]] std::uint8_t  u8(std::uint32_t va) const  { return mem_[at(va, 1)]; }
    [[nodiscard]] std::int8_t   s8(std::uint32_t va) const  { return static_cast<std::int8_t>(u8(va)); }
    [[nodiscard]] std::uint16_t u16(std::uint32_t va) const {
        const std::size_t i = at(va, 2);
        return static_cast<std::uint16_t>(mem_[i] | mem_[i + 1] << 8);
    }
    [[nodiscard]] std::int16_t  s16(std::uint32_t va) const { return static_cast<std::int16_t>(u16(va)); }
    [[nodiscard]] std::uint32_t u32(std::uint32_t va) const {
        const std::size_t i = at(va, 4);
        return std::uint32_t(mem_[i]) | std::uint32_t(mem_[i + 1]) << 8 |
               std::uint32_t(mem_[i + 2]) << 16 | std::uint32_t(mem_[i + 3]) << 24;
    }
    [[nodiscard]] std::int32_t  s32(std::uint32_t va) const { return static_cast<std::int32_t>(u32(va)); }

    void set8(std::uint32_t va, int v)  { mem_[at(va, 1)] = static_cast<std::uint8_t>(v); }
    void set16(std::uint32_t va, int v) {
        const std::size_t i = at(va, 2);
        mem_[i] = static_cast<std::uint8_t>(v);
        mem_[i + 1] = static_cast<std::uint8_t>(v >> 8);
    }
    void set32(std::uint32_t va, std::int64_t v) {
        const std::size_t i = at(va, 4);
        for (int k = 0; k < 4; ++k) mem_[i + std::size_t(k)] = static_cast<std::uint8_t>(v >> (8 * k));
    }
    void fill(std::uint32_t va, std::size_t n, std::uint8_t v) { std::memset(&mem_[at(va, n)], v, n); }
    void copy(std::uint32_t va, std::span<const std::uint8_t> src) {
        std::memcpy(&mem_[at(va, src.size())], src.data(), src.size());
    }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return mem_; }

    // --- named fields ---------------------------------------------------------
    [[nodiscard]] int  month() const { return s16(0x7a528e); }
    [[nodiscard]] int  week()  const { return s16(0x7a5290); }
    [[nodiscard]] int  day()   const { return s16(0x7a5292); }
    void setMonth(int v) { set16(0x7a528e, v); }
    void setWeek(int v)  { set16(0x7a5290, v); }
    void setDay(int v)   { set16(0x7a5292, v); }
    [[nodiscard]] int  areaBgm() const { return s16(0x7a55f6); }
    void setAreaBgm(int v) { set16(0x7a55f6, v); }
    [[nodiscard]] bool areaBgmOn() const { return u8(0x7e11a0) != 0; }
    void setAreaBgmOn(bool v) { set8(0x7e11a0, v); }
    [[nodiscard]] bool workRoomEventSeen() const { return s16(0x7a4e62) != 0; }
    void setWorkRoomEventSeen(bool v) { set16(0x7a4e62, v); }

    static constexpr std::uint32_t unitRecord(int id)   { return 0x7e17e8 + std::uint32_t(id) * 0x4a0; }
    static constexpr std::uint32_t unitProgress(int id) { return 0x7cf34c + std::uint32_t(id) * 0x124; }

private:
    [[nodiscard]] std::size_t at(std::uint32_t va, std::size_t n) const {
        if (va < kBase || va + n > kEnd) throw std::out_of_range("GameWork: address outside the game-state image");
        return va - kBase;
    }
    std::vector<std::uint8_t> mem_;
};

} // namespace ch
