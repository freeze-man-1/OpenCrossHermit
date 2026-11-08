// ============================================================================
// preparation_task.cpp
// ============================================================================
#include "preparation_task.hpp"
#include "../state/game_logic.hpp"
#include "../state/unit_stats.hpp"

#include <climits>

namespace ch {

namespace {
// The week's sortie (FUN_004a6a10): stage s at 0x7a529a + 0x70 s.
constexpr std::uint32_t kStage      = 0x7a5296;   // s16: stages fought
constexpr std::uint32_t kStageCount = 0x7a5298;   // s16
constexpr std::uint32_t stage(int s) { return std::uint32_t(s) * 0x70; }
constexpr std::uint32_t kUnitCount  = 0x7a4ae8;   // s16
constexpr std::uint32_t kUnits      = 0x7a4aec;   // s16[100]
} // namespace

void PreparationTask::loadStage(GameWork& w, GameVersion v, int map, bool clearBattle) {
    const std::uint32_t m = 0x6ad1d0 + std::uint32_t(map) * 0x2a;
    const auto u8 = [&](std::uint32_t o) { return int(logic::sdU8(m + o, v)); };
    w.set16(0x7f4488, logic::sdS16(m, v));
    w.set16(0x7f448a, logic::sdS16(m + 2, v));
    w.set8(0x7f448f, u8(4));
    w.set8(0x7f4490, 0);
    w.set8(0x7f4491, 0);
    for (std::uint32_t k = 0; k < 8; ++k) w.set16(0x7f4492 + k * 2, u8(7 + k));
    w.set8(0x7f44a2, u8(0xf));
    w.set8(0x7f44a3, u8(5));
    w.set8(0x7f44a4, u8(6));
    w.set32(0x7f44a8, static_cast<std::int8_t>(u8(0x10)) == -1 ? INT_MAX : std::int64_t(u8(0x10)) * 0xe10);   // time limits, frames
    w.set32(0x7f44ac, std::int64_t(u8(0x11)) * 0xe10);
    w.set32(0x7f44b0, std::int64_t(u8(0x12)) * 0xe10);
    w.set32(0x7f44b4, std::int64_t(u8(0x13)) * 0xe10);
    for (std::uint32_t k = 0; k < 6; ++k) w.set8(0x7f44b8 + k, u8(0x14 + k));
    for (std::uint32_t k = 0; k < 0x10; ++k) w.set8(0x7f44be + k, u8(0x1a + k));
    w.set8(0x7f448d, 0); w.set8(0x7f448e, 0); w.set8(0x7f448c, 0);
    if (clearBattle) w.fill(0x8093f8, 0x29a, 0);   // FUN_004dab10
}

// FUN_004b8ff0
Co<> PreparationTask::start(std::uint32_t) {
    GameWork& w = ctx_.work;
    const int s = w.s16(kStage);
    if (s == w.s16(kStageCount)) {
        ctx_.scenes.request(SceneId::AllResult);
    } else {
        loadStage(w, ctx_.version, w.s16(0x7a5308 + stage(s)), true);
        for (std::uint32_t i = 0; i < 100; ++i) w.set16(kUnits + i * 2, -1);   // FUN_004b9210
        w.set16(kUnitCount, 0);
        for (int k = 0; k < w.s16(0x7a52f8 + stage(s)); ++k) {                // FUN_004b9270
            const int n = w.s16(kUnitCount);
            w.set16(kUnits + std::uint32_t(n) * 2, w.s16(0x7a52d0 + std::uint32_t(k) * 2 + stage(s)));
            w.set16(kUnitCount, n + 1);
        }
        for (int i = 0; i < w.s16(kUnitCount); ++i) {                          // FUN_004b92c0 / FUN_004b9340
            stats::Derived d;
            stats::Bonus b;
            stats::derive(stats::readRecord(w, w.s16(kUnits + std::uint32_t(i) * 2)), d, b, ctx_.version);
            for (std::uint32_t k = 0; k < 0xb0; ++k) w.set8(0x7f4518 + std::uint32_t(i) * 0xb0 + k, d.b[k]);
        }
        w.set8(0x7f448d, w.s16(kUnitCount));
        w.set16(kStage, s + 1);
        ctx_.scenes.request(SceneId::Tactics);
    }
    co_await yieldFrame();
}

} // namespace ch
