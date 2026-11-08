// ============================================================================
// coming_task.cpp
// ============================================================================
#include "coming_task.hpp"
#include "ysub/ysub.hpp"

namespace ch {

using legacy::kMouseLeft;

// FUN_0042fe50: FUN_0041d280 (the music and the voice), FUN_0040d610 (the
// sounds), FUN_0041f4e0(0x5a).
ComingTask::~ComingTask() {
    ctx_.audio->bgmStop();
    ctx_.audio->voiceStop();
    ctx_.audio->seStopAll();
    ctx_.sprites->unloadBank(kBank);
}

Co<> ComingTask::start(std::uint32_t) {
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\Tactics\\Coming.bin"));   // FUN_0042fff0
    // FUN_004300c0: in by 0x180 a frame, a click, out again.
    frame_ = (ctx_.comingResult != 0) + 1;
    alpha_ = 0;
    for (int phase = 1; phase < 4;) {
        switch (phase) {
            case 1:
                alpha_ += 0x180;
                if (alpha_ > 0xfeff) { alpha_ = 0xff00; ++phase; }
                break;
            case 2:
                if (ctx_.mouse().pressed & kMouseLeft) { ctx_.audio->sePlay(5); ++phase; }   // FUN_004db2b0(5)
                break;
            default:
                alpha_ -= 0x180;
                if (alpha_ < 0x181) { alpha_ = 0; ++phase; }
                break;
        }
        draw();
        co_await yieldFrame();
    }
    ctx_.audio->bgmFadeOut(0x5a);                // FUN_004db230(0x5a)
    while (!ctx_.audio->bgmFadeFinished()) co_await yieldFrame();   // FUN_004db270
    ctx_.audio->seStopAll();                     // FUN_004db300
    ctx_.scenes.request(SceneId::Title);         // FUN_00439e30(1)
}

void ComingTask::draw() const {
    // FUN_00430330: the picture at the fade's alpha.
    ysub::drawSub(ctx_, {.bank = kBank, .frame = static_cast<std::uint16_t>(frame_),
                         .alpha = static_cast<std::uint8_t>(alpha_ >> 8)}, 100, 0, 0);
    // FUN_00430260: frame 0 at the pointer.
    const auto& m = ctx_.mouse();
    ysub::drawSub(ctx_, {.bank = kBank, .frame = 0}, 1, m.x, m.y);
}

} // namespace ch
