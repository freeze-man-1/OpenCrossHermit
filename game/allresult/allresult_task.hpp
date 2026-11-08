// ============================================================================
// allresult_task.hpp — AllResultTask, scene 0xc (src\yoshimaru\AllResult)
//
// The end of the week: apply each group's lecture or adventure results,
// count them up on screen, play the MVP script, write the week into every
// unit's history and adjust affinities, then CH003 (to WeekStart).
//   FUN_004c1aa0  Start
//   FUN_004bd870  init: SE group, banks, results (FUN_004c00c0 /
//                 FUN_004c0400 lecture / FUN_004c0c10 adventure)
//   FUN_004bdac0  the screen: fade in, count-up, MVP (mvp.ybc), buttons
//   FUN_004be0b0  draw: FUN_004be950 leaders, FUN_004be2e0 members
//   FUN_004c1350  history, job levels, FUN_004bf9e0 affinities
// ============================================================================
#pragma once
#include "../anm/anm_player.hpp"
#include "../game_context.hpp"
#include "../ysub/ysub.hpp"

#include <array>
#include <cstdint>

namespace ch {

class AllResultTask final : public Task {
public:
    explicit AllResultTask(GameContext& ctx) : ctx_(ctx) {}
    ~AllResultTask() override;
    Co<> start(std::uint32_t) override;          // FUN_004c1aa0

private:
    static constexpr std::uint16_t kBank = 0xf;      // data\AllResult\AllResult.bin
    static constexpr std::uint16_t kAnmBank = 0x11;  // data\AllResult\AnmAllResult.bin

    // Per unit (task +0x38 + 0x44 id): values before the week and the
    // count-up's last shown gains.
    struct UnitResult {
        std::int16_t rankBefore = 0;                 // +0x38
        std::array<std::int16_t, 7> levelBefore{};   // +0x3a
        std::array<std::int16_t, 8> newSkill{};      // +0x4a: by stat row
        std::int16_t rankShown = 0;                  // +0x5a
        std::array<std::int16_t, 7> gainShown{};     // +0x5c
        std::array<std::int16_t, 7> skillShown{};    // +0x6c
    };

    void init();                                 // FUN_004bd870
    void computeResults();                       // FUN_004c00c0
    void lectureResult(int unit, int group);     // FUN_004c0400
    void adventureResult(int unit);              // FUN_004c0c10
    void unlockSkills(int unit, int lecture, bool adventure);   // shared tail of both
    Co<> screen();                               // FUN_004bdac0
    void draw();                                 // FUN_004be0b0
    void drawLeader(int group, int leader);      // FUN_004be950
    void drawMember(int slot, int group, int unit);   // FUN_004be2e0
    void drawAdventurePlate(int x, int y, int type, int sub, bool title, int prio);   // FUN_004beb90
    void drawLecturePlate(int x, int y, int level, int sub, int prio);                // FUN_004bee00
    void drawFace(int x, int y, int kind, int index, int prio);                       // FUN_004bf6c0
    void drawName(int x, int y, int kind, int index, int prio);                       // FUN_004bf870
    void drawCommaNumber(int x, int y, int digits, int prio, std::uint32_t value, int pitch, int gap,
                         int groupGap, int font, int comma);                           // FUN_004bef20
    void writeHistory();                         // FUN_004c1350
    void adjustAffinities();                     // FUN_004bf9e0
    [[nodiscard]] ysub::Rect rect(int i) const;  // DAT_0061c600 + 16 i
    [[nodiscard]] int skillAt(int cat, int sub) const {   // FUN_004c0060
        return skillGrid_[std::size_t(cat + sub * 0xc) % skillGrid_.size()]; }
    void fx(int node, bool play, int clip, int x, int y);

    GameContext&  ctx_;
    std::int16_t  mode_ = 0;                     // +0x30: 0 week end, 1 battle quit, 2 last week
    std::int32_t  pointsGained_ = 0;             // +0x34
    std::array<UnitResult, 0x2d> units_{};
    std::array<std::int16_t, 0x54> skillGrid_{}; // +0x136e: skill id by (category - 1) + (sub - 1) * 12
    bool          skillLearned_ = false;         // +0x142c
    bool          noAdventure_ = true;           // +0x142e
    AnmFile       anmFile_;                      // +0x1430
    AnmPlayer     anm_;
    std::array<AnmPlayer::NodeId, 0xa0> nodes_{};    // +0x1484
    ysub::Flashes flashes_;
};

} // namespace ch
