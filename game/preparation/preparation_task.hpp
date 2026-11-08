// ============================================================================
// preparation_task.hpp — PreparationTask, scene 10
// (src\yoshimaru\preparation\preparationtask)
//
// No screen: between the class screen (or a finished battle) and Tactics it
// sets up the adventure's next stage, or goes to AllResult when all are done.
//   FUN_004b8ff0  Start
//   FUN_004da860 / FUN_004da8e0  stage battle settings from the map table
//   FUN_004b9210 / 9270 / 92c0 / 9340  the battle's units and their values
// ============================================================================
#pragma once
#include "../game_context.hpp"

namespace ch {

class PreparationTask final : public Task {
public:
    explicit PreparationTask(GameContext& ctx) : ctx_(ctx) {}   // FUN_004b8f20
    ~PreparationTask() override = default;  // FUN_004b8f70 / FUN_004b8fb0: nothing of its own
    Co<> start(std::uint32_t) override;

    // FUN_004da8a0(map, clearBattle): copy map row `map` (0x6ad1d0, 0x2a bytes)
    // into the battle settings at 0x7f4488.
    static void loadStage(GameWork& w, GameVersion v, int map, bool clearBattle);

private:
    GameContext& ctx_;
};

} // namespace ch
