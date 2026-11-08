// ============================================================================
// demo_task.cpp — DemoTask: lifecycle, bank files, AnmData glue; the director
// and the layers are generated
// ============================================================================
#include "demo_task.hpp"

#include <cstdint>

namespace ch {

namespace {
// The scene's tables: TW VA, JP VA, size.
constexpr std::uint32_t kTablesTw = 0x752300, kTablesJp = 0x66c2e0, kTablesSize = 0x4df0;
} // namespace

// FUN_004e3980 (FUN_00504d70, the Hata base). The JP tables lie at
// JP VA = CH VA - 0xE6020 (six bytes differ).
DemoTask::DemoTask(GameContext& ctx)
    : HataTask(ctx, kTablesTw, kTablesJp, kTablesSize),
      anm_([&ctx](std::uint32_t se) { ctx.audio->sePlay(se); }) {}

// FUN_004e39d0: music and voice (FUN_0041d280), SE (FUN_0040d610), bank 10,
// the anim objects (FUN_00464d30), then the Hata base (FUN_00504e10).
DemoTask::~DemoTask() {
    ctx_.audio->bgmStop();
    ctx_.audio->seStopAll();
    if (anmRoot_ != AnmPlayer::kNone) anm_.freeRoot(anmRoot_);
    ctx_.sprites->unloadBank(kBank);
    ctx_.sprites->unloadBank(kAnmBank);
}

// FUN_004e77b0: bank 10 <- data\op\demodata.bin; OpData2.bin -> the AnmData
// file (+0x260) whose sprites go to bank 0xb.
void DemoTask::loadData() {
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\op\\demodata.bin"));
    anmFile_.load(ctx_.readData("data\\OP\\OpData2.bin"), kAnmBank, *ctx_.sprites);
    if (anmRoot_ == AnmPlayer::kNone) anmRoot_ = anm_.newRoot(anmFile_);
}

void DemoTask::anmPlay(int, int set, int no, int flags) {
    anm_.play(anmRoot_, set, no, static_cast<std::uint8_t>(flags));
}

void DemoTask::anmUpdate(int) { anm_.update(anmRoot_); }

void DemoTask::anmDraw(int, int sprite, int x, int y, int prio) {
    const AnmPlayer::SpriteTemplate tmpl{
        .color  = {U8(sprite + 0x66), U8(sprite + 0x65), U8(sprite + 0x64), U8(sprite + 0x67)},
        .flags  = U16(sprite + 0x58),
        .scaleX = S16(sprite + 0x70),
        .scaleY = S16(sprite + 0x72),
    };
    anm_.draw(anmRoot_, ctx_.draw, *ctx_.sprites, tmpl, x, y, static_cast<std::uint16_t>(prio));
}

// Ghidra's cast types, as used verbatim by the generated code.
namespace ghidra_types {
using byte = std::uint8_t;
using ushort = std::uint16_t;
using uint = std::uint32_t;
} // namespace ghidra_types
using namespace ghidra_types;

#include "demo_director.gen.inc"
#include "demo_layers.gen.inc"

} // namespace ch
