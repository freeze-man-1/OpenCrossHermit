// ============================================================================
// demo_task.hpp — DemoTask, the demo loop (scene 0x17, src\hata\demotask.cpp)
//
// The opening's machinery (HataTask) on its own pictures (data\op\demodata.bin,
// bank 10) and the opening's AnmData effects (OpData2.bin, bank 0xb), to the
// opening's music. FUN_004e3ae0 counts frames through 0x12 cuts (+0x32) and,
// after the last, starts over; Space quits the game (a shop demo). Scripts
// reach it by setting the next scene to 0x17.
//
//   FUN_004e3890  new (FUN_004e3980: the Hata base) and link, start(0)
//   FUN_004e39d0  destructor (FUN_004e3aa0 deletes): music, SE, bank 10, the
//                 anim objects, then ~HataTask
//   FUN_004e3ae0  the director (tools/gen/gen_demo_director.py ->
//                 demo_director.gen.inc)
//   FUN_004e77b0  the bank files
//   FUN_004e7830 .. FUN_004faaa0  the layers (tools/ghidra2cpp.py ->
//                 demo_layers.gen.inc; FUN_004faaa0 clears the work)
// (FUN_004e3260 / FUN_004e3290 are empty.) The work area is OpTask's, the cut
// at +0x32 instead of +0x30.
// ============================================================================
#pragma once
#include "../anm/anm_player.hpp"
#include "../hata/hata_task.hpp"

namespace ch {

class DemoTask final : public HataTask {
public:
    explicit DemoTask(GameContext& ctx);
    ~DemoTask() override;                        // FUN_004e39d0

    Co<> start(std::uint32_t) override { co_await director(); }

private:
    static constexpr std::uint16_t kAnmBank = 0xb;   // data\OP\OpData2.bin (FUN_00505e20)

    Co<> director();                             // FUN_004e3ae0
    void loadData();                             // FUN_004e77b0

    // The director's engine calls.
    void renderSync(int, int) {}                 // FUN_00415480: the fixed step keeps time
    void renderFlush() {}                        // FUN_00415420
    void touchTextures(int) { fillRect(0, 0, 0x400, 0x300, 0, 0xFF000000u, 0); }   // FUN_00416a10
    void hataNoop() {}                           // FUN_00505df0: returns 0
    void anmReset(int) {}                        // FUN_00409ef0: the player is made in loadData
    void anmObjFree() {}                         // FUN_00464d30: freed with the task
    void bgmPlay(int track, int flags) { ctx_.audio->bgmPlay(std::uint16_t(track), flags); }
    [[nodiscard]] int bgmIsPlaying() const { return ctx_.audio->bgmIsPlaying() ? 1 : 0; }   // FUN_0041e3d0
    void bgmStop() { ctx_.audio->bgmStop(); }   // FUN_0041e500
    void unloadBank(int bank) { ctx_.sprites->unloadBank(std::uint16_t(bank)); }
    [[nodiscard]] bool skipPressed() const { return ctx_.engine.input().pressed(legacy::Action::Skip); }
    void quitGame() { ctx_.quitRequested = true; }   // DAT_007a4a00 + 0x8e0

    // AnmData player at +0x5a8, drawing through the sprite object at +0x1e0 (as OpTask's).
    void anmSpriteBind() {}
    void anmPlay(int obj, int set, int no, int flags);
    void anmUpdate(int obj);
    void anmDraw(int obj, int sprite, int x, int y, int prio);

    // generated (demo_layers.gen.inc)
    void demo_004faaa0(); void demo_004e7830(); void demo_004e7ad0(); void demo_004e8440();
    void demo_004e8f10(); void demo_004ec540(); void demo_004ecab0(); void demo_004ed6f0();
    void demo_004edec0(); void demo_004ee6b0(); void demo_004f0f70(); void demo_004f0fc0();
    void demo_004f1080(); void demo_004f13a0(); void demo_004f13f0(); void demo_004f1440();
    void demo_004f1510(); void demo_004f1710(); void demo_004f1dc0(); void demo_004f2640();
    void demo_004f2880(); void demo_004f2cb0(); void demo_004f2dc0(); void demo_004f3810();
    void demo_004f3ae0(); void demo_004f4200(); void demo_004f4d00(); void demo_004f71c0();
    void demo_004f7890(); void demo_004f7f20(); void demo_004f9fc0(); void demo_004fa0e0();
    void demo_004fa1f0(); void demo_004fa6f0(); void demo_004fa960();

    AnmFile           anmFile_;
    AnmPlayer         anm_;
    AnmPlayer::NodeId anmRoot_ = AnmPlayer::kNone;
};

} // namespace ch
