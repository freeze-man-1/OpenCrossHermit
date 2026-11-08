// ============================================================================
// coming_task.hpp — the "coming" screen (scene 0x11, src\yoshida\comingtask.cpp)
//
// The picture the trial maps end on: data\Tactics\Coming.bin as sprite bank
// 0x5a, frame 1 or 2 by how the battle ended (DAT_005ff720), fading in,
// held until a click, fading out; then the music fades and the title comes.
//   FUN_0042ff40  the task body      FUN_0042fff0  load Coming.bin
//   FUN_004300c0  the fade loop      FUN_00430220  draw: FUN_00430330 the
//                                    picture, FUN_00430260 the cursor
// ============================================================================
#pragma once
#include "game_context.hpp"
#include "task_system.hpp"

#include <cstdint>

namespace ch {

class ComingTask final : public Task {
public:
    explicit ComingTask(GameContext& ctx) : ctx_(ctx) {}
    ~ComingTask() override;                      // FUN_0042fe50 (FUN_0042ff00 deletes)
    Co<> start(std::uint32_t param) override;   // FUN_0042ff40

private:
    static constexpr std::uint16_t kBank = 0x5a;
    void draw() const;                           // FUN_00430220

    GameContext&  ctx_;
    int           frame_ = 1;                    // +0x38
    std::int32_t  alpha_ = 0;                    // +0x34, 8.8
};

} // namespace ch
