// ============================================================================
// op_task.hpp — OpTask, the opening cinematic (E:\CrossHermit\GAME\SRC\HATA)
//
// A frame-counted timeline: FUN_005063a0 advances one frame per task tick,
// fires keyframe events (layer fade in/out, positions from .data tables) and
// runs per-layer animation/draw functions, some at 1/2, 1/3 or 1/6 rate.
//
// The ~6,500 lines of layer functions are translated mechanically by
// tools/ghidra2cpp.py into op_layers.gen.inc. They address the task's work
// area by its original byte offsets; this class gives them well-defined,
// endian-safe accessors instead of raw pointer casts.
//
// Work area layout (offsets as in the original object):
//   +0x30          cut index (0..0x16, 100 = finished)
//   +0x33..+0x38   per-group picture set
//   +0x39..+0x50   group A layer state  (0 off, 1 fade in, 2 shown, 3 fade out)
//   +0x51..+0x68   group A ramp step     +0x69..+0x80 group A alpha
//   +0x82/+0x9c/+0xb6/+0xd0  group A x / y / w / h   (int16[13] each)
//   +0xea..+0xf1   group B state, +0xf2 step(u16[8]), +0x102 alpha, +0x10a/+0x11a params
//   +0x12a..+0x140 group C (full-screen fades), +0x142.. group D (AnmData effect)
//   +0x1e0         embedded sprite object, +0x260 10 anim objects, +0x5a8 AnmData player
// ============================================================================
#pragma once
#include "../anm/anm_player.hpp"
#include "../hata/hata_task.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace ch {

class OpTask final : public HataTask {
public:
    explicit OpTask(GameContext& ctx);
    ~OpTask() override;                          // FUN_00506290 (FUN_00506360 deletes)

    Co<> start(std::uint32_t param) override;   // FUN_005063a0

private:
    static constexpr std::uint16_t kAnmBank = 0xb;  // data\OP\OpData2.bin block 6 (FUN_00505e20)

    // ---- AnmData player at +0x5a8, drawing through the sprite object at +0x1e0
    void anmSpriteBind() {}   // FUN_004077c0: binds +0x1e0 to the renderer; nothing to do
    void anmPlay(int obj, int file, int no, int param);
    void anmUpdate(int obj);
    void anmDraw(int obj, int sprite, int x, int y, int prio);

    // ---- director ------------------------------------------------------------
    void resetWork();                       // FUN_00525060
    void fireEvents(int frame);             // keyframes of the current cut
    void runLayers(int frame);              // per-cut layer functions
    void fadeA(int i, std::uint8_t state);  // group A: state, step 0, alpha 0 / 0xff
    void stA(int i, std::uint8_t state);    // group A: state, step 0
    void resetC();                          // group C layer 0 restart (fade in)

    // generated (op_layers.gen.inc)
    void op_005247b0(); void op_005246a0(); void op_00524580(); void op_005224e0();
    void op_0051e2b0(); void op_00521e50(); void op_00521780(); void op_0051ca00();
    void op_0051c6c0(); void op_0051d1d0(); void op_00524f20(); void op_0051a760();
    void op_0051b9c0(); void op_00524cb0(); void op_0051b380(); void op_0051a0b0();
    void op_00519eb0(); void op_00519d90(); void op_00519d40(); void op_00519de0();
    void op_00519a20(); void op_00515040(); void op_00512840(); void op_00519910();
    void op_00519960(); void op_0051afe0(); void op_00514170(); void op_00513480();
    void op_005122d0(); void op_0050d5a0(); void op_0050b900(); void op_0051b8b0();
    void op_0050afd0(); void op_0050c6f0();

    int  div2_ = 0, div3_ = 0, div6_ = 0;   // local_14 / local_18 / local_24
    AnmFile             anmFile_;
    AnmPlayer           anm_;
    AnmPlayer::NodeId   anmRoot_ = AnmPlayer::kNone;
};

} // namespace ch
