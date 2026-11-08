// ============================================================================
// op_task.cpp — OpTask director (FUN_005063a0) and support
//
// Frame numbers: the original increments the counter before any test. Ghidra
// printed if-chain tests against the new value but jump-table `switch`es
// against the old one, so those case labels are one less than the frame they
// fire on (verified in the machine code: `sub eax,0x1e` for Ghidra's 0x1d).
// All frames below are the real ones.
// ============================================================================
#include "op_task.hpp"


namespace ch {

namespace {
// The scene's tables: TW VA, JP VA, size.
constexpr std::uint32_t kTablesTw = 0x7579c0, kTablesJp = 0x671bdc, kTablesSize = 0x6740;

// Group offsets in the work area (see op_task.hpp).
constexpr int A_STATE = 0x39, A_STEP = 0x51, A_ALPHA = 0x69;
constexpr int C_STATE = 0x12a, C_ALPHA = 0x12c, C_STEP = 0x12e;
} // namespace

OpTask::OpTask(GameContext& ctx)
    // Both builds lay the tables out identically (JP VA = CH VA - 0xE5DE4);
    // they differ in values: caption picture rects in the localized opdata.bin.
    : HataTask(ctx, kTablesTw, kTablesJp, kTablesSize),
      anm_([&ctx](std::uint32_t se) { ctx.audio->sePlay(se); }) { layoutBank_ = "data/op/opdata.bin"; }

// --------------------------------------------------------------- AnmData ---
// The generated code passes work-area offsets: obj = +0x5a8 (the one player
// root this task owns), sprite = +0x1e0 (sprite object fields, see FUN_0051a760).
void OpTask::anmPlay(int, int set, int no, int flags) {
    anm_.play(anmRoot_, set, no, static_cast<std::uint8_t>(flags));
}

void OpTask::anmUpdate(int) { anm_.update(anmRoot_); }

void OpTask::anmDraw(int, int sprite, int x, int y, int prio) {
    const AnmPlayer::SpriteTemplate tmpl{
        .color  = {U8(sprite + 0x66), U8(sprite + 0x65), U8(sprite + 0x64), U8(sprite + 0x67)},
        .flags  = U16(sprite + 0x58),
        .scaleX = S16(sprite + 0x70),
        .scaleY = S16(sprite + 0x72),
    };
    anm_.draw(anmRoot_, ctx_.draw, *ctx_.sprites, tmpl, x, y, static_cast<std::uint16_t>(prio));
}

// ------------------------------------------------------------ lifecycle ---
// FUN_00506290: SE stop, bank 10 freed, the 10 anim objects released.
OpTask::~OpTask() {
    ctx_.audio->seStopAll();
    anm_.freeRoot(anmRoot_);
    ctx_.sprites->unloadBank(kBank);
    ctx_.sprites->unloadBank(kAnmBank);
}

// FUN_00525060
void OpTask::resetWork() {
    U8(0x30) = 0;
    for (int o = 0x33; o <= 0x38; ++o) U8(o) = 0;
    for (int o = 0x39; o <= 0x80; ++o) U8(o) = 0;
    for (int o = 0x82; o <= 0xe8; o += 2) S16(o) = 0;
    for (int o = 0xea; o <= 0xf1; ++o) U8(o) = 0;
    for (int o = 0x102; o <= 0x109; ++o) U8(o) = 0;
    for (int o = 0xf2; o <= 0x100; o += 2) S16(o) = 0;
    for (int o = 0x10a; o <= 0x128; o += 2) S16(o) = 0;
    for (int o = 0x12a; o <= 0x12d; ++o) U8(o) = 0;
    for (int o = 0x12e; o <= 0x140; o += 2) S16(o) = 0;
    U8(0x142) = 0;
    U8(0x143) = 0;
}

void OpTask::fadeA(int i, std::uint8_t state) {
    U8(A_STATE + i) = state;
    U8(A_STEP + i)  = 0;
    U8(A_ALPHA + i) = state == 3 ? 0xFF : 0;
}

void OpTask::stA(int i, std::uint8_t state) {
    U8(A_STATE + i) = state;
    U8(A_STEP + i)  = 0;
}

void OpTask::resetC() {
    U8(C_STATE) = 1;
    S16(C_STEP) = 0;
    U8(C_ALPHA) = 0;
}

// FUN_005063a0
Co<> OpTask::start(std::uint32_t) {
    // FUN_00415480(1, 0x3c): allow up to 60 skipped renders to stay in sync
    // with the music. The engine's fixed-step accumulator covers this.
    co_await yieldFrame();
    ctx_.draw.text({.x = 0x1B8, .y = 0x17C, .priority = 0, .argb = 0xFFFFFFFF, .text = "Now Loading ..."});
    co_await yieldFrame();

    // FUN_0050af40: bank 10 <- data\op\opdata.bin; OpData2.bin -> AnmData file
    // (+0x260) whose sprites go to bank 0xb.
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\op\\opdata.bin"));
    anmFile_.load(ctx_.readData("data\\OP\\OpData2.bin"), kAnmBank, *ctx_.sprites);
    anmRoot_ = anm_.newRoot(anmFile_);
    co_await yieldFrame();
    ctx_.draw.text({.x = 0x1B8, .y = 0x17C, .priority = 0, .argb = 0xFFFFFFFF, .text = "Please Wait ..."});
    co_await yieldFrame();
    // FUN_00416a10: pre-touch every texture, then one black frame.
    fillRect(0, 0, 0x400, 0x300, 0, 0xFF000000u, 0);
    co_await yieldFrame();

    int frame = 0;
    div2_ = div3_ = div6_ = 0;
    resetWork();
    anmPlay(OFF(0x5a8), 0, 2, 0x80);                    // FUN_00409ef0 + FUN_00409ff0

    ctx_.audio->bgmPlay(0x1F, 0);                       // Opening.mp3, once
    while (!ctx_.audio->bgmIsPlaying()) co_await yieldFrame();   // FUN_0041e3d0

    do {
        // Letterbox bars: table rows 0-1 for cuts 0-3, rows 2-3 after.
        const int bar0 = U8(0x30) < 4 ? 0 : 2;
        for (int i = bar0; i < bar0 + 2; ++i) {
            const std::uint32_t row = 0x7587f8 + i * 10;
            fillRect(TS16(row), TS16(row + 2), TS16(row + 4), TS16(row + 6), TS16(row + 8), 0xFF000000u, 0);
        }

        ++frame;
        div2_ = div2_ < 1 ? div2_ + 1 : 0;
        div3_ = div3_ < 2 ? div3_ + 1 : 0;
        div6_ = div6_ < 5 ? div6_ + 1 : 0;

        if (U8(0x30) == 100) break;
        fireEvents(frame);
        runLayers(frame);
        co_await yieldFrame();
    } while (!(ctx_.mouse().pressed & legacy::kMouseLeft));    // left click skips

    ctx_.audio->bgmFadeOut(0x3C);
    while (!ctx_.audio->bgmFadeFinished()) co_await yieldFrame();
    co_await yieldFrame();
    co_await yieldFrame();
    ctx_.audio->bgmStop();                              // FUN_0041e500
    ctx_.sprites->unloadBank(kBank);
    ctx_.sprites->unloadBank(kAnmBank);
    ctx_.scenes.request(SceneId::Title);                // FUN_00439e30(1)
}

// ----------------------------------------------------------------- events ---
void OpTask::fireEvents(int f) {
    switch (U8(0x30)) {
    case 0:
        switch (f) {
            case 0x1e: fadeA(0, 1); break;
            case 0x3c: fadeA(1, 1); break;
            case 0x40: fadeA(2, 1); break;
            case 0xa6: fadeA(1, 3); break;
            case 0xaa: fadeA(2, 3); break;
            case 0xbe: fadeA(0, 3); break;
        }
        break;
    case 1:
        switch (f) {
            case 0x136: U8(0x33) = 1; fadeA(0, 1); break;
            case 0x154: U8(0x35) = 1; U8(0xea) = 1; S16(0xf2) = 0; S16(0x11a) = TS16(0x7579ce); break;
            case 0x19a: fadeA(0, 3); break;
            case 0x1ae: fadeA(1, 1); break;
            case 0x2da: fadeA(1, 3); break;
            case 0x2ee: fadeA(2, 1); break;
            case 0x398: fadeA(2, 3); break;
            case 0x3ca: U8(0x33) = 2; fadeA(0, 1); break;
            case 0x460: fadeA(0, 3); break;
            case 0x474: fadeA(1, 1); break;
            case 0x500: fadeA(1, 3); break;
            case 0x532: U8(0x33) = 3; fadeA(0, 1); break;
            case 0x618: fadeA(0, 3); break;
            case 0x62c: fadeA(1, 1); break;
            case 0x6d6: fadeA(1, 3); break;
            case 0x6ea: fadeA(2, 1); break;
            case 0x7e4: fadeA(2, 3); break;
            case 0x816: U8(0x33) = 4; fadeA(0, 1); break;
            case 0x91a: fadeA(0, 3); break;
            case 0x92e: fadeA(1, 1); break;
            case 0x9b4: fadeA(1, 3); break;
            case 0x9ce: fadeA(2, 1); break;
            case 0xa6e: fadeA(2, 3); break;
            case 0xa82: fadeA(3, 1); break;
            case 0xb2c: fadeA(3, 3); break;
            case 0xb5e: U8(0x33) = 5; fadeA(0, 1); break;
            case 0xc26: fadeA(0, 3); break;
            case 0xc58: U8(0x33) = 6; fadeA(0, 1); break;
            case 0xce4: fadeA(0, 3); break;
            case 0xcf8: fadeA(1, 1); break;
            case 0xe1a: fadeA(1, 3); break;
            case 0xe2e: fadeA(2, 1); break;
            case 0xf0a: fadeA(2, 3); break;
            case 0xf3c: U8(0x33) = 7; fadeA(0, 1); break;
            case 0xfa0: fadeA(0, 3); break;
            case 0xfb4: fadeA(1, 1); break;
            case 0x1090: fadeA(1, 3); break;
            case 0x10a4: fadeA(2, 1); break;
            case 0x10fe: fadeA(2, 3); break;
            case 0x1112: fadeA(3, 1); break;
            case 0x116c: fadeA(3, 3); break;
        }
        break;
    case 2:
        switch (f) {
            case 0x11f0: U8(0x38) = 1; U8(0x142) = 1; S16(0x144) = 0; S16(0x14c) = 0; break;
            case 0x1254: U8(0xeb) = 1; S16(0xf4) = 0; S16(0x11c) = TS16(0x7579de); break;
            case 0x1284: U8(0xec) = 1; S16(0xf6) = 0; S16(0x11e) = TS16(0x757a5e); break;
            case 0x1286: U8(0xed) = 1; S16(0xf8) = 0; S16(0x120) = TS16(0x757aae); break;
            case 0x12d2: U8(0xee) = 1; S16(0xfa) = 0; S16(0x122) = TS16(0x757abe); break;
            case 0x1302: U8(0xef) = 1; S16(0xfc) = 0; S16(0x124) = TS16(0x757ace); break;
            case 0x1320: U8(0x37) = 1; resetC(); break;
            case 0x1330: stA(0, 1); S16(0x82) = TS16(0x757f1c); S16(0x9c) = TS16(0x757f1e); break;
            case 0x1334: stA(1, 1); S16(0x84) = TS16(0x757f2c); S16(0x9e) = TS16(0x757f2e); break;
            case 0x1336:
                stA(2, 1); S16(0x86) = TS16(0x757f3c); S16(0xa0) = TS16(0x757f3e);
                U8(0x54) = 0; S16(0x88) = TS16(0x757f4c); S16(0xa2) = TS16(0x757f4e);
                U8(0x55) = 0; S16(0x8a) = TS16(0x757f5c); S16(0xa4) = TS16(0x757f5e);
                U8(0x56) = 0; S16(0x8c) = TS16(0x757f6c); S16(0xa6) = TS16(0x757f6e);
                break;
            case 0x1338: stA(6, 1); S16(0x8e) = TS16(0x757f7c); S16(0xa8) = TS16(0x757f7e); break;
            case 0x133a:
                stA(7, 1); S16(0x90) = TS16(0x757f8c); S16(0xaa) = TS16(0x757f8e);
                U8(0x59) = 0; S16(0x92) = TS16(0x757f9c); S16(0xac) = TS16(0x757f9e);
                break;
            case 0x133c: stA(9, 1); S16(0x94) = TS16(0x757fac); S16(0xae) = TS16(0x757fae); break;
            case 0x133e:
                stA(10, 1); S16(0x96) = TS16(0x757fbc); S16(0xb0) = TS16(0x757fbe);
                S16(0xca) = 0x153; S16(0xe4) = 0x1a;
                stA(11, 1); S16(0x98) = TS16(0x757adc); S16(0xb2) = TS16(0x757ade);
                S16(0xcc) = 0x200; S16(0xe6) = 0x180;
                break;
            case 0x1348: stA(12, 1); S16(0x9a) = TS16(0x757aec); S16(0xce) = 300; S16(0xe8) = 300; break;
        }
        break;
    case 3:
        if (f == 0x13b6) {
            U8(0x36) = 1; U8(0x51) = 0; U8(0x69) = 0; S16(0xb6) = 0x24e; S16(0xd0) = 0x1bb;
        }
        break;
    case 4:
        switch (f) {
            case 0x14f0: U8(0xea) = 1; S16(0xf2) = 0; S16(0x11a) = TS16(0x757afe); U8(0x102) = 0; break;
            case 0x157d: U8(0x37) = 2; resetC(); break;
            case 0x1589: U8(0x34) = 1; fadeA(0, 1); break;
            case 0x15da:
                U8(C_STATE) = 3; S16(C_STEP) = 0; U8(C_ALPHA) = 0xFF;
                fadeA(0, 3);
                U8(0xeb) = 1;
                U8(0xec) = 1; S16(0xf6) = 0; S16(0x10e) = TS16(0x757bac);
                U8(0xed) = 1; S16(0xf8) = 0; S16(0x110) = TS16(0x757b2c); S16(0x120) = TS16(0x757b2e);
                U8(0xee) = 1; S16(0xfa) = 0; S16(0x112) = TS16(0x757b4c); S16(0x122) = TS16(0x757b4e);
                U8(0xef) = 1; S16(0xfc) = 0; S16(0x114) = TS16(0x757b6c); S16(0x124) = TS16(0x757b6e);
                U8(0xf0) = 1; S16(0xfe) = 0; S16(0x116) = TS16(0x757b8c); S16(0x126) = TS16(0x757b8e);
                U8(0x3a) = 1;
                stA(2, 1); S16(0xa0) = 0x115;
                stA(3, 1); S16(0x88) = TS16(0x7585fc); S16(0xa2) = TS16(0x7585fe);
                break;
            case 0x1606: stA(4, 1); S16(0x8a) = TS16(0x75861c); break;
            case 0x1616: stA(5, 1); S16(0x8c) = TS16(0x75860c); S16(0xa6) = TS16(0x75860e); break;
            case 0x161e: stA(6, 1); S16(0x8e) = TS16(0x75862c); break;
            case 0x1652:
                stA(5, 1); S16(0x8c) = TS16(0x75860c); S16(0xa6) = TS16(0x75860e);
                stA(6, 1); S16(0x8e) = TS16(0x75862c);
                break;
            case 0x1657: stA(3, 1); S16(0x88) = TS16(0x7585fc); S16(0xa2) = TS16(0x7585fe); break;
            case 0x1687: stA(4, 1); S16(0x8a) = TS16(0x75861c); break;
            case 0x168c: stA(3, 1); S16(0x88) = TS16(0x7585fc); S16(0xa2) = TS16(0x7585fe); break;
            case 0x1695:
                stA(7, 1); S16(0x90) = TS16(0x75861c);
                U8(299) = 1; S16(0x130) = 0; U8(0x12d) = 0;
                break;
            case 0x1699: fadeA(8, 1); break;
        }
        break;
    case 5:
        if (f == 0x1718) fadeA(8, 3);
        else if (f == 0x1742) { U8(0x35) = 2; U8(0xea) = 1; S16(0xf2) = 0; U8(0x102) = 0; }
        break;
    case 6:
        switch (f) {
            case 0x1757: U8(0x34) = 2; fadeA(0, 1); break;
            case 0x1759: fadeA(4, 1); break;
            case 0x1771: U8(0xeb) = 1; S16(0xf4) = 0; U8(0x103) = 0; break;
            case 0x178d: U8(0xec) = 1; S16(0xf6) = 0; U8(0x104) = 0; break;
            case 0x17a7: U8(0xed) = 1; S16(0xf8) = 0; U8(0x105) = 0; break;
            case 0x17c1: fadeA(1, 1); break;
            case 0x17dd: fadeA(2, 1); break;
            case 0x17f9: fadeA(3, 1); break;
            case 0x17fb: fadeA(5, 1); break;
            case 0x1813: U8(0xee) = 1; S16(0xfa) = 0; U8(0x106) = 0; break;
            case 0x182f: U8(0xef) = 1; S16(0xfc) = 0; U8(0x107) = 0; break;
            case 0x1839: fadeA(4, 3); break;
            case 0x1849: U8(0xf0) = 1; S16(0xfe) = 0; U8(0x108) = 0; break;
            case 0x184f: fadeA(5, 3); break;
            case 0x1865: U8(0xf1) = 1; S16(0x100) = 0; U8(0x109) = 0; break;
            case 0x1869:
                U8(0x37) = 3; resetC(); S16(0x130) = 0;
                S16(0x134) = TS16(0x758852); S16(0x13a) = TS16(0x75884c); S16(0x13c) = TS16(0x758856);
                break;
        }
        break;
    case 7:
        switch (f) {
            case 0x18a8:
                U8(0x35) = 3; U8(0xea) = 1; S16(0xf2) = 0; U8(0x102) = 0; S16(0x10a) = TS16(0x757c4c);
                break;
            case 0x18e5:
                U8(0x34) = 3; fadeA(0, 1); S16(0x82) = TS16(0x75847c); S16(0x9c) = TS16(0x75847e);
                break;
            case 0x18f6: fadeA(1, 1); S16(0x84) = TS16(0x75851c); S16(0x9e) = TS16(0x75851e); break;
            case 0x1938: fadeA(2, 1); break;
            case 0x196b: fadeA(3, 1); break;
            case 0x19e5: fadeA(2, 3); break;
            case 0x19ec: fadeA(3, 3); break;
            case 0x1a12: U8(0xeb) = 1; S16(0xf4) = 0; U8(0x103) = 0; S16(0x10c) = TS16(0x757c5c); break;
            case 0x1a42: fadeA(4, 1); break;
            case 0x1a9f: fadeA(5, 1); break;
            case 0x1b6f: fadeA(4, 3); break;
            case 0x1b78: fadeA(5, 3); break;
            case 0x1b7e: U8(C_STATE) = 3; S16(C_STEP) = 0; U8(C_ALPHA) = 0xFF; break;
            case 0x1bb8: fadeA(6, 1); break;
        }
        break;
    case 8:
        switch (f) {
            case 0x1c39: fadeA(6, 3); break;
            case 0x1c5c: fadeA(0, 1); S16(0x82) = TS16(0x75852c); S16(0x9c) = TS16(0x75852e); break;
            case 0x1c60:
                fadeA(1, 1); S16(0x84) = TS16(0x7584fc); S16(0x9e) = TS16(0x7584fe);
                fadeA(2, 1);
                break;
            case 0x1cc6: U8(0xea) = 1; S16(0xf2) = 0; S16(0x10a) = TS16(0x757c6c); break;
            case 0x1d09: fadeA(2, 3); break;
            case 0x1d51: U8(0x37) = 4; resetC(); break;
            case 0x1da2: fadeA(3, 1); break;
            case 0x1e57: fadeA(3, 3); break;
            case 0x1e6e: fadeA(4, 1); break;
            case 0x1f73: fadeA(4, 3); break;
            case 0x1fa9: U8(C_STATE) = 3; S16(C_STEP) = 0; U8(C_ALPHA) = 0xFF; break;
        }
        break;
    case 9:
        switch (f) {
            case 0x1fe3: U8(0x37) = 5; resetC(); break;
            case 0x1fe5: U8(0x34) = 5; fadeA(0, 1); S16(0x82) = TS16(0x7583ec); break;
            case 0x1ffe: case 0x2019: case 0x2038: case 0x2057:
            case 0x2070: case 0x2089: case 0x20a2:
                resetC(); break;
            case 0x2000: fadeA(1, 1); S16(0x84) = TS16(0x7583fc); break;
            case 0x201b: fadeA(2, 1); S16(0x86) = TS16(0x75840c); break;
            case 0x203a: fadeA(3, 1); S16(0x88) = TS16(0x75841c); break;
            case 0x2059: fadeA(4, 1); S16(0x8a) = TS16(0x75842c); break;
            case 0x2072: fadeA(5, 1); S16(0x8c) = TS16(0x75843c); break;
            case 0x208b: fadeA(6, 1); S16(0x8e) = TS16(0x75844c); break;
            case 0x20a4: fadeA(7, 1); S16(0x90) = TS16(0x75845c); break;
        }
        break;
    case 10:
        switch (f) {
            case 0x20e6:
                U8(0x35) = 5;
                U8(0xea) = 1; S16(0xf2) = 0; U8(0x102) = 0; S16(0x11a) = TS16(0x757c8e);
                U8(0xeb) = 1; S16(0xf4) = 0; U8(0x103) = 0; S16(0x11c) = TS16(0x757c9e);
                U8(0xec) = 1; S16(0xf6) = 0; U8(0x104) = 0; S16(0x11e) = TS16(0x757cae);
                break;
            case 0x2190: U8(0x34) = 6; fadeA(0, 1); S16(0x9c) = TS16(0x75823e); break;
            case 0x21d0: fadeA(1, 1); break;
            case 0x21fe: fadeA(2, 1); break;
            case 0x2318: fadeA(1, 3); break;
            case 0x232a: U8(0xed) = 2; S16(0xf8) = 0; U8(0x105) = 0xFF; S16(0x120) = TS16(0x757cbe); break;
            case 0x2334: fadeA(2, 3); break;
        }
        break;
    case 11:
        switch (f) {
            case 0x2420:
                U8(0x34) = 7; fadeA(0, 1); S16(0x82) = TS16(0x75850c); S16(0x9c) = TS16(0x75850e);
                break;
            case 0x2426: fadeA(2, 1); break;
            case 0x244c: fadeA(3, 1); break;
            case 0x2462: U8(0xea) = 1; S16(0xf2) = 0; U8(0x102) = 0; break;
            case 0x2468: fadeA(1, 1); S16(0x84) = TS16(0x7584dc); break;
            case 0x24b4: fadeA(2, 3); break;
            case 0x24c2: fadeA(3, 3); break;
            case 0x24cc: U8(0xeb) = 1; S16(0xf4) = 0; U8(0x103) = 0; break;
            case 0x2502: fadeA(4, 1); S16(0x8a) = TS16(0x7584ec); S16(0xa4) = TS16(0x7584ee); break;
            case 0x2522: fadeA(6, 1); break;
            case 0x2526: fadeA(5, 1); S16(0x8c) = TS16(0x75846c); break;
            case 0x2550: fadeA(7, 1); break;
            case 0x255a: fadeA(8, 1); break;
            case 0x25b0: fadeA(6, 3); break;
            case 0x25de: fadeA(7, 3); break;
            case 0x25e8: fadeA(8, 3); break;
            case 0x2604:
                U8(0xeb) = 3; S16(0xf4) = 0; U8(0x103) = 0xFF;
                U8(0xec) = 1; S16(0xf6) = 0; U8(0x104) = 0; S16(0x11e) = TS16(0x757cce);
                break;
        }
        break;
    case 12:
        switch (f) {
            case 0x2676: U8(0x34) = 8; fadeA(0, 1); break;
            case 0x2678: fadeA(1, 1); S16(0x84) = TS16(0x75853c); S16(0x9e) = TS16(0x75853e); break;
            case 0x268e: fadeA(2, 1); S16(0x86) = TS16(0x75854c); S16(0xa0) = TS16(0x75854e); break;
            case 0x2766: fadeA(0, 3); break;
        }
        break;
    case 13:
        switch (f) {
            case 0x27dc: U8(0x34) = 9; fadeA(0, 1); break;
            case 0x28a2: fadeA(0, 3); break;
            case 0x28de: fadeA(1, 1); break;
            case 0x298c: fadeA(1, 3); break;
        }
        break;
    case 14:
        switch (f) {
            case 0x29d4: U8(0x35) = 8; U8(0xea) = 1; S16(0xf2) = 0; U8(0x102) = 0; S16(0x10a) = TS16(0x757cdc); break;
            case 0x2a5b:
                U8(0x34) = 10; fadeA(0, 1); S16(0x82) = TS16(0x7587ec); S16(0x9c) = TS16(0x7587ee);
                break;
            case 0x2a9c: fadeA(1, 1); break;
            case 0x2b74: fadeA(1, 3); break;
            case 0x2be6: fadeA(2, 1); break;
            case 0x2c9a: fadeA(2, 3); break;
        }
        break;
    case 15:
        switch (f) {
            case 0x2cd2: U8(0x35) = 9; U8(0xea) = 1; S16(0xf2) = 0; U8(0x102) = 0; S16(0x10a) = TS16(0x757d1c); break;
            case 0x2ce4: U8(0xeb) = 1; S16(0xf4) = 0; U8(0x103) = 0; S16(0x11c) = TS16(0x757cfe); break;
            case 0x2d0e: U8(0x34) = 0xb; fadeA(0, 1); S16(0x82) = TS16(0x75848c); break;
            case 0x2d46: fadeA(6, 1); break;
            case 0x2d7e: fadeA(1, 1); S16(0x84) = TS16(0x75849c); break;
            case 0x2d80: U8(0xec) = 1; S16(0xf6) = 0; U8(0x104) = 0; S16(0x11e) = TS16(0x757d0e); break;
            case 0x2d92: U8(0xed) = 1; S16(0xf8) = 0; U8(0x105) = 0; S16(0x110) = TS16(0x757d3c); break;
            case 0x2dec: fadeA(2, 1); S16(0x86) = TS16(0x75855c); S16(0xa0) = TS16(0x75855e); break;
            case 0x2dee:
                fadeA(3, 1); S16(0x88) = TS16(0x75856c); S16(0xa2) = TS16(0x75856e);
                fadeA(4, 1); S16(0x8a) = TS16(0x75857c); S16(0xa4) = TS16(0x75857e);
                break;
            case 0x2df2: fadeA(5, 1); S16(0x8c) = TS16(0x75858c); S16(0xa6) = TS16(0x75858e); break;
            case 0x2e52: fadeA(6, 3); break;
            case 0x2e9e: fadeA(7, 1); break;
            case 0x2f38: fadeA(7, 3); break;
            case 0x2f62: fadeA(8, 1); break;
            case 0x2ff8: fadeA(8, 3); break;
        }
        break;
    case 16:
        switch (f) {
            case 0x303c:
                U8(0x35) = 10; U8(0xea) = 1; U8(0x37) = 6;
                U8(C_STATE) = 3; S16(C_STEP) = 0; U8(C_ALPHA) = 0xFF;
                break;
            case 0x3040: U8(0x34) = 0xc; for (int i = 0; i <= 18; ++i) fadeA(i, 1); break;
            case 0x3060: fadeA(19, 1); break;
            case 0x3066: fadeA(20, 1); break;
            case 0x306c: fadeA(21, 1); break;
            case 0x3072: fadeA(22, 1); break;
            case 0x308c: fadeA(14, 3); break;
            case 0x3096: fadeA(5, 3); fadeA(12, 3); break;
            case 0x309a: fadeA(6, 3); break;
            case 0x309e: fadeA(13, 3); fadeA(18, 3); break;
            case 0x30ac: fadeA(8, 3); break;
            case 0x30b2: fadeA(17, 3); break;
            case 0x30b6: fadeA(7, 3); fadeA(10, 3); break;
            case 0x30ba: resetC(); break;
            case 0x30bc: fadeA(9, 3); fadeA(16, 3); fadeA(19, 3); break;
            case 0x30c0: fadeA(0, 3); fadeA(3, 3); fadeA(20, 3); fadeA(22, 3); break;
            case 0x30c2: fadeA(1, 3); fadeA(15, 3); break;
            case 0x30c4: fadeA(21, 3); break;
            case 0x30c6: fadeA(4, 3); break;
            case 0x30ca: fadeA(2, 3); break;
            case 0x30d4: fadeA(11, 3); break;
            case 0x30e2: fadeA(23, 1); break;
            case 0x3140: fadeA(23, 3); break;
        }
        break;
    case 17:
        switch (f) {
            case 0x3184: U8(0x34) = 0xd; fadeA(0, 1); break;
            case 0x3252: fadeA(0, 3); break;
            case 0x328a: fadeA(1, 1); break;
            case 0x3364: fadeA(1, 3); break;
        }
        break;
    case 18:
        switch (f) {
            case 0x33b0: U8(0x34) = 0xe; fadeA(0, 1); break;
            case 0x33b6: fadeA(1, 1); break;
            case 0x33b8: fadeA(2, 1); break;
            case 0x33c0: fadeA(3, 1); break;
            case 0x33c6: fadeA(4, 1); fadeA(5, 1); break;
            case 0x33c8: fadeA(6, 1); break;
            case 0x33ca: fadeA(7, 1); fadeA(8, 1); fadeA(9, 1); break;
            case 0x33ce: fadeA(10, 1); break;
            case 0x33d2: fadeA(11, 1); break;
            case 0x33d8: fadeA(12, 1); break;
            case 0x33dc: fadeA(13, 1); break;
            case 0x3484: fadeA(14, 1); break;
            case 0x3532: fadeA(14, 3); break;
            case 0x3564: fadeA(15, 1); break;
            case 0x3624: fadeA(15, 3); break;
        }
        break;
    case 19:
        switch (f) {
            case 0x366c:
                U8(0x35) = 0xb;
                U8(0xea) = 1; S16(0xf2) = 0; S16(0x11a) = TS16(0x757d6e);
                U8(0xeb) = 2; S16(0xf4) = 0; U8(0x103) = 0xFF; S16(0x11c) = TS16(0x757d7e);
                U8(0x37) = 7; U8(C_STATE) = 3; S16(C_STEP) = 0; U8(C_ALPHA) = 0xFF;
                break;
            case 0x369c: U8(0x34) = 0xf; fadeA(0, 1); break;
            case 0x36ec: U8(0xec) = 1; S16(0xf6) = 0; U8(0x104) = 0; S16(0x11e) = TS16(0x757d8e); break;
            case 0x3784: fadeA(0, 3); break;
            case 0x37be: resetC(); break;
            case 0x380e: fadeA(1, 1); break;
            case 0x391e: fadeA(1, 3); break;
        }
        break;
    case 20:
        switch (f) {
            case 0x396e: U8(0x34) = 0x10; fadeA(0, 1); break;
            case 0x3982:
                U8(0x35) = 0xc; U8(0xea) = 1; S16(0xf2) = 0; S16(0x11a) = TS16(0x757d9e);
                U8(0x37) = 8; U8(C_STATE) = 3; S16(C_STEP) = 0; U8(C_ALPHA) = 0xFF;
                break;
            case 0x39b6: fadeA(1, 1); break;
            case 0x3a4c: fadeA(0, 3); break;
            case 0x3a5c: fadeA(1, 3); break;
            case 0x3a98: fadeA(2, 1); break;
            case 0x3ba0: U8(299) = 1; S16(0x130) = 0; U8(0x12d) = 0; break;
            case 0x3c24: U8(0x3c) = 2; U8(299) = 3; S16(0x130) = 0; U8(0x12d) = 0xFF; break;
            case 0x3ce8: fadeA(3, 3); break;
        }
        break;
    case 21:
        if (f == 0x3d56)      { U8(0x34) = 0x11; fadeA(0, 1); }
        else if (f == 0x3ed4) fadeA(0, 3);
        break;
    case 22:
        if (f == 0x3f4c) U8(0x30) = 100;
        break;
    }
}

// ------------------------------------------------------------ layer runs ---
void OpTask::runLayers(int) {
    const bool d2 = div2_ == 0, d3 = div3_ == 0, d6 = div6_ == 0;
    switch (U8(0x30)) {
    case 0:
        op_005247b0();
        if (d2) { op_005246a0(); op_00524580(); }
        break;
    case 1:
        op_005224e0(); op_0051e2b0();
        if (d2) { op_00521e50(); op_00521780(); op_0051ca00(); op_0051c6c0(); op_0051d1d0(); }
        break;
    case 2:
        op_00524f20(); op_0051e2b0(); op_0051a760(); op_0051b9c0();
        if (d2) { op_0051ca00(); op_0051c6c0(); op_0051d1d0(); op_00524cb0(); }
        if (d3) op_0051b380();
        if (d6) op_0051a0b0();
        break;
    case 3:
        op_00519eb0();
        if (d3) { op_00519d90(); op_00519d40(); op_00519de0(); }
        break;
    case 4:
        op_00519a20(); op_0051b9c0(); op_00515040(); op_00512840();
        if (d2) { op_00519910(); op_00519960(); op_0051b380(); op_0051afe0(); op_00514170(); op_00513480(); op_005122d0(); }
        break;
    case 5:
        op_00515040(); op_0051e2b0();
        if (d2) { op_00513480(); op_0051ca00(); }
        break;
    case 6:
        op_0051e2b0(); op_0050d5a0(); op_00515040(); op_0051b9c0();
        if (d2) { op_0051ca00(); op_0050b900(); op_00514170(); op_00513480(); op_0051b380(); op_0051b8b0(); }
        break;
    case 7:
        op_0051b9c0(); op_0051e2b0(); op_0050d5a0(); op_00515040();
        if (d2) {
            op_0051b380(); op_0051afe0(); op_0051b8b0(); op_0051ca00(); op_0051c6c0(); op_0051d1d0();
            op_0050b900(); op_0050afd0(); op_0050c6f0(); op_00514170(); op_00513480();
        }
        break;
    case 8:
        op_0051e2b0(); op_0050d5a0(); op_00515040(); op_0051b9c0();
        if (d2) {
            op_0051b380(); op_0051afe0(); op_0051c6c0(); op_0051d1d0(); op_0050b900(); op_0050afd0();
            op_0050c6f0(); op_00514170(); op_00513480();
        }
        break;
    case 9:
        op_0051b9c0(); op_0050d5a0();
        if (d2) { op_0051b380(); op_0050b900(); op_0050c6f0(); }
        break;
    case 10: case 11: case 12: case 15:
        op_0051e2b0(); op_0050d5a0(); op_00515040();
        if (d2) {
            op_0051c6c0(); op_0051ca00(); op_0051d1d0(); op_0050afd0(); op_0050b900(); op_0050c6f0();
            op_00513480(); op_00514170();
        }
        break;
    case 13: case 17: case 21:
        op_00515040();
        if (d2) { op_00513480(); op_00514170(); }
        break;
    case 14:
        op_0051e2b0(); op_0050d5a0(); op_00515040();
        if (d2) { op_00513480(); op_00514170(); }
        if (d3) { op_0051c6c0(); op_0051ca00(); op_0051d1d0(); op_0050afd0(); op_0050b900(); op_0050c6f0(); }
        break;
    case 16:
        op_0051e2b0(); op_0050d5a0(); op_00515040(); op_0051b9c0();
        if (d2) { op_0050afd0(); op_0050b900(); op_00513480(); op_00514170(); op_0051afe0(); op_0051b380(); }
        break;
    case 18:
        op_0050d5a0(); op_00515040();
        if (d2) { op_0050b900(); op_00513480(); op_00514170(); }
        break;
    case 19:
        op_0051e2b0(); op_00515040(); op_0051b9c0();
        if (d2) {
            op_0051c6c0(); op_0051ca00(); op_0051d1d0(); op_00513480(); op_00514170(); op_0051afe0();
            op_0051b380();
        }
        break;
    case 20:
        op_0051e2b0(); op_00515040(); op_0051b9c0();
        if (d2) { op_0051d1d0(); op_00513480(); op_00514170(); op_0051afe0(); op_0051b380(); }
        break;
    }
}

// Ghidra's cast types, as used verbatim by the generated code.
namespace ghidra_types {
using byte = std::uint8_t;
using ushort = std::uint16_t;
using uint = std::uint32_t;
} // namespace ghidra_types
using namespace ghidra_types;

#include "op_layers.gen.inc"

} // namespace ch
