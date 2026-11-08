// ============================================================================
// ed_task.hpp — EdTask, the ending (scene 0x12, src\hata\edtask.cpp)
//
// A frame-counted timeline like the opening's (HataTask): FUN_004fb430 counts
// frames and steps through five parts (+0x31): 0 the scrolling credits (their
// lines switched on at set frames, +0x150..+0x17f, moved and drawn by
// FUN_004fe1a0 / FUN_004ff070), 1 and 2 two picture fades, 3 ten pictures each
// with its voice line, 4 the wait for the last frame (0x5d00); then 100 ends
// it. There is no skip. The layer functions are translated by
// tools/ghidra2cpp.py into ed_layers.gen.inc; FUN_004fc570 (the voices) is
// ported by hand.
//
// Work area (as in the original object):
//   +0x31          part (0..4, 100 = finished)
//   +0x39..+0x42   group A layer state (0 off, 1 fade in, 2 shown, 3 fade out),
//   +0x51.. step, +0x69.. alpha (as OpTask's)
//   +0xea / +0xf2 / +0x102   group B state / step / alpha
//   +0x150..+0x17f the credit lines on, +0x180.. their y (int16)
// ============================================================================
#pragma once
#include "../hata/hata_task.hpp"

namespace ch {

class EdTask final : public HataTask {
public:
    explicit EdTask(GameContext& ctx);
    ~EdTask() override;                          // FUN_004fb320 (FUN_004fb3f0 deletes)

    Co<> start(std::uint32_t param) override;   // FUN_004fb430

private:
    void voices();                               // FUN_004fc570

    // generated (ed_layers.gen.inc)
    void ed_00504830(); void ed_004ff070(); void ed_004fe1a0(); void ed_004fde30();
    void ed_004fdca0(); void ed_004fdd70(); void ed_004fdb70(); void ed_004fda40();
    void ed_004fdae0(); void ed_004fca10(); void ed_004fc240();
};

} // namespace ch
