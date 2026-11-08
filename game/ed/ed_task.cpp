// ============================================================================
// ed_task.cpp — EdTask director (FUN_004fb430) and the voices (FUN_004fc570)
// ============================================================================
#include "ed_task.hpp"

namespace ch {

namespace {
// The scene's tables: TW VA, JP VA, size.
constexpr std::uint32_t kTablesTw = 0x7570f0, kTablesJp = 0x67112c, kTablesSize = 0x8d0;

// FUN_004fb430 part 0: the frame each credit line (+0x150 + k) comes on.
constexpr int kCreditFrames[48] = {
    0x3c, 0x114, 0x234, 0x39c, 0x42c, 0x4bc, 0x5dc, 0x6fc, 0x81c, 0x93c, 0xa5c, 0xb7c,
    0xc9c, 0xdbc, 0xedc, 0xffc, 0x111c, 0x123c, 0x13a6, 0x1434, 0x162c, 0x174c, 0x186c, 0x198c,
    0x1aac, 0x1bcc, 0x1cec, 0x1e0c, 0x1fbc, 0x20dc, 0x21fc, 0x231c, 0x243c, 0x25a4, 0x26c4, 0x27e4,
    0x2938, 0x2a58, 0x2bc0, 0x2ce0, 0x2e48, 0x3160, 0x3280, 0x33e8, 0x3508, 0x3628, 0x3aa2, 0x3bc2};

// FUN_004fc570's voice lines, one per picture of part 3 (+0x39 + i).
constexpr const char* kVoices[10] = {
    "data\\sound\\voice\\v098\\v098_001_01.mp3", "data\\sound\\voice\\v098\\v098_001_02.mp3",
    "data\\sound\\voice\\v098\\v098_001_03.mp3", "data\\sound\\voice\\v098\\v098_002_01.mp3",
    "data\\sound\\voice\\v098\\v098_002_02.mp3", "data\\sound\\voice\\v098\\v098_003_01.mp3",
    "data\\sound\\voice\\v098\\v098_004_01.mp3", "data\\sound\\voice\\v098\\v098_004_02.mp3",
    "data\\sound\\voice\\v098\\v098_005_01.mp3", "data\\sound\\voice\\v098\\v098_005_02.mp3"};
} // namespace

// FUN_004fb2d0 (FUN_00504d70, the Hata base). The JP tables lie at
// JP VA = CH VA - 0xE5FC4 (nine bytes differ).
EdTask::EdTask(GameContext& ctx) : HataTask(ctx, kTablesTw, kTablesJp, kTablesSize) {}

// FUN_004fb320: music and voice stopped (FUN_0041d280), SE (FUN_0040d610),
// bank 10 freed (FUN_0041f4e0(10)).
EdTask::~EdTask() {
    ctx_.audio->bgmStop();
    ctx_.audio->voiceStop();
    ctx_.audio->seStopAll();
    ctx_.sprites->unloadBank(kBank);
}

// FUN_004fc570: each picture fading in (state 1) steps its alpha through
// DAT_00757940; at the end it is shown (2) and its voice line plays on the
// voice stream (FUN_0041e190 on +0x8f00).
void EdTask::voices() {
    for (int i = 0; i < 10; ++i) {
        if (I8(0x39 + i) != 1) continue;
        U8(0x69 + i) = T8(0x00757940 + U8(0x51 + i));
        U8(0x51 + i) = std::uint8_t(U8(0x51 + i) + 1);
        if (U8(0x51 + i) > 0x1e) {
            U8(0x51 + i) = 0;
            U8(0x39 + i) = 2;
            ctx_.audio->voicePlay(kVoices[i]);
        }
    }
}

// FUN_004fb430
Co<> EdTask::start(std::uint32_t) {
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\ed\\eddata.bin"));   // FUN_004fc1d0
    co_await yieldFrame();
    fillRect(0, 0, 0x400, 0x300, 0, 0xFF000000u, 0);   // FUN_00416a10: textures touched
    co_await yieldFrame();
    ed_00504830();
    ctx_.audio->bgmPlay(0x21, 0);                      // FUN_004db060(0x21, 0)
    while (!ctx_.audio->bgmIsPlaying()) co_await yieldFrame();   // FUN_0041e3d0
    int frame = 0, div2 = 0;
    for (;;) {
        ++frame;
        div2 = div2 < 1 ? div2 + 1 : 0;
        const int f = frame;
        switch (U8(0x31)) {
            case 0:
                for (int i = 0; i < 0x14; ++i) {           // the frame round the credits
                    const std::uint32_t r = 0x757850 + std::uint32_t(i) * 0xc;
                    fillRect(TS16(r), TS16(r + 2), TS16(r + 4), TS16(r + 6), TS16(r + 8),
                             std::uint32_t(TU16(r + 10)) << 24, 1);
                }
                for (int k = 0; k < 48; ++k) if (f == kCreditFrames[k]) U8(0x150 + k) = 1;
                ed_004ff070();
                if (div2 == 0) ed_004fe1a0();
                break;
            case 1:
                if (f == 0x432d) { U8(0x39) = 1; U8(0x51) = 0; U8(0x69) = 0; }
                else if (f == 0x43a5) { U8(0x3a) = 1; U8(0x52) = 0; U8(0x6a) = 0; }
                else if (f == 0x45fe) { U8(0x39) = 3; U8(0x51) = 0; U8(0x69) = 0xff; }
                else if (f == 0x463a) { U8(0x3a) = 3; U8(0x52) = 0; U8(0x6a) = 0xff; }
                ed_004fde30();
                if (div2 == 0) { ed_004fdca0(); ed_004fdd70(); }
                break;
            case 2:
                if (f == 0x46b2) { U8(0xea) = 1; U16(0xf2) = 0; U8(0x102) = 0; }
                else if (f == 0x4ab8) { U8(0xea) = 3; U16(0xf2) = 0; U8(0x102) = 0xff; }
                ed_004fdb70();
                if (div2 == 0) { ed_004fda40(); ed_004fdae0(); }
                break;
            case 3: {
                // The ten pictures fade in one by one, then all fade out.
                constexpr int kIn[10] = {0x4b6c, 0x4d10, 0x4eb4, 0x5058, 0x510c, 0x51fc, 0x52ec, 0x5508, 0x57d8, 0x597c};
                for (int i = 0; i < 10; ++i)
                    if (f == kIn[i]) { U8(0x39 + i) = 1; U8(0x51 + i) = 0; U8(0x69 + i) = 0; }
                if (f == 0x5b98)
                    for (int i = 0; i < 10; ++i) { U8(0x39 + i) = 3; U8(0x51 + i) = 0; U8(0x69 + i) = 0xff; }
                ed_004fca10();
                if (div2 == 0) { ed_004fc240(); voices(); }
                break;
            }
            case 4:
                if (f == 0x5d00) U8(0x31) = 100;
                break;
            case 100:
                co_await yieldFrame();
                co_await yieldFrame();
                ctx_.audio->bgmStop();                     // FUN_0041e500 twice: music, voice
                ctx_.audio->voiceStop();
                ctx_.sprites->unloadBank(kBank);           // FUN_0041f4e0(10)
                ctx_.scenes.request(SceneId::HanKojin);    // FUN_00439e30(9)
                co_return;
            default: break;
        }
        co_await yieldFrame();
    }
}

#include "ed_layers.gen.inc"

} // namespace ch
