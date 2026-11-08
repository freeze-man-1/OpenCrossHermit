// ============================================================================
// tacticsresult_task.hpp — TacticsResultTask, scene 0xb (src\yoshimaru\TacticsResult)
//
// After each battle: the stage's rank, the exp each unit earned (by its
// share of the damage, job and group affinity) split over its stats, the
// school points, and the items found. Then back to Preparation.
//   FUN_004bd660  Start
//   FUN_004baa90  init: bank 0xe, results unless a network loss
//   FUN_004bd210  best rank per event, event 0x25's skill unlocks
//   FUN_004bca40  item drops
//   FUN_004bbd40  exp and points
//   FUN_004bab60  the screen
//   FUN_004bae30  draw
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "../ysub/ysub.hpp"

#include <array>
#include <cstdint>

namespace ch {

class TacticsResultTask final : public Task {
public:
    explicit TacticsResultTask(GameContext& ctx) : ctx_(ctx) {}
    ~TacticsResultTask() override;               // FUN_004bd590 (FUN_004bd620 deletes)
    Co<> start(std::uint32_t) override;          // FUN_004bd660

private:
    static constexpr std::uint16_t kBank = 0xe;  // data\TacticsResult\TacticsResult.bin

    void init();                                 // FUN_004baa90
    void eventRecords();                         // FUN_004bd210
    void itemDrops();                            // FUN_004bca40
    void expAndPoints();                         // FUN_004bbd40
    Co<> screen();                               // FUN_004bab60
    void draw();                                 // FUN_004bae30
    [[nodiscard]] ysub::Rect rect(std::uint32_t va) const;
    void number(int x, int y, int offX, int offY, int digits, int prio, std::uint32_t value, int pitch,
                int gap, int groupGap, int flags, std::uint32_t font, std::uint32_t comma);

    // A card in the 5 x 4 formation grid.
    struct Card {
        std::int16_t unit = 0;                   // +0x50
        std::int16_t kills = 0;                  // +0x78: battle unit +0xaa
        std::int16_t damage = 0;                 // +0xa0: +0xac
        std::int16_t downs = 0;                  // +0xc8: +0xae (s8)
        std::int32_t exp = 0;                    // +0xf0
    };

    GameContext&  ctx_;
    bool          skip_ = false;                 // +0x32
    std::int16_t  rank_ = 0;                     // +0x34: 0 = S .. 4
    std::int16_t  turns_ = 0;                    // +0x36: turn grade plate, 3 none, 4 no bonus
    std::int16_t  goal_ = 0;                     // +0x38: objective plate
    std::int32_t  points_ = 0;                   // +0x3c: after
    std::int32_t  pointsGained_ = 0;             // +0x40
    std::int32_t  pointsBefore_ = 0;             // +0x44
    std::int16_t  units_ = 0;                    // +0x48
    std::int16_t  kills_ = 0;                    // +0x4a
    std::int16_t  damage_ = 0;                   // +0x4c
    std::int16_t  downs_ = 0;                    // +0x4e
    std::array<std::array<Card, 4>, 5> cards_{};
    std::array<std::array<std::int16_t, 10>, 7> items_{};   // +0x140: item ids by category row
};

} // namespace ch
