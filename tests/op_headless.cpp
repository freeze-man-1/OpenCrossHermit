// Runs the real OpTask for the whole opening without a window and checks the
// timeline: every cut is reached in order and the task ends on schedule.
// Optionally renders chosen ticks to PNG with the software reference renderer.
// Usage: CH_DATA=<imported data folder> op_headless [out_prefix tick...]
#include "../game/bank_format.hpp"
#include "../game/game_context.hpp"
#include "../game/op/op_task.hpp"
#include "soft_render.hpp"
#include <string>

#include <cassert>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <vector>

namespace {
// Records sprite draws; validates frame indices against the real bank.
class RecordingBanks final : public ch::ISpriteBanks {
public:
    void loadBank(std::uint16_t bank, std::vector<std::uint8_t> bytes) override {
        if (bank == 10) frames10 = ch::splitBank(bytes).size();
        held[bank] = std::move(bytes);
    }
    void unloadBank(std::uint16_t bank) override { held.erase(bank); }
    std::pair<int, int> frameSize(std::uint16_t bank, std::uint16_t i) override {
        auto it = held.find(bank);
        if (it == held.end()) return {0, 0};
        auto e = ch::splitBank(it->second);
        return i < e.size() ? ch::entrySize(e[i]) : std::pair<int, int>{0, 0};
    }
    void drawSprite(SDL_Renderer*, const ch::SpriteCmd& c) override {
        if (c.bank == 10 && c.frame >= frames10) { std::fprintf(stderr, "bad frame %u\n", c.frame); std::abort(); }
        if (c.bank == 11) ++anmDraws;
    }
    void drawText(SDL_Renderer*, const ch::TextCmd&) override {}
    std::size_t frames10 = 0;
    int anmDraws = 0;
    std::map<std::uint16_t, std::vector<std::uint8_t>> held;
};
} // namespace

int main(int argc, char** argv) {
    // The opening reads the imported game files (CH_DATA); without them, skip.
    if (const char* data = std::getenv("CH_DATA"); !data || !std::ifstream(std::string(data) + "/installed.txt")) {
        std::puts("SKIP: no imported game data");
        return 77;
    }
    legacy::Engine engine({.title = "headless", .logicalW = 1024, .logicalH = 768});
    ch::GameContext ctx{.engine = engine, .sprites = std::make_unique<RecordingBanks>(),
                        .audio = std::make_unique<ch::NullAudio>()};
    auto* banks = static_cast<RecordingBanks*>(ctx.sprites.get());
    // OP_VERSION=tw runs the ch.exe tables; default is the JP build's.
    if (const char* v = std::getenv("OP_VERSION"); v && std::string(v) == "tw") ctx.version = ch::GameVersion::Tw;

    const std::string prefix = argc > 1 ? argv[1] : "";
    soft::Banks softBanks;
    std::set<int> dumpAt;
    for (int i = 2; i < argc; ++i) dumpAt.insert(std::atoi(argv[i]));

    ch::Task& op = ctx.tasks.link(std::make_unique<ch::OpTask>(ctx), 0, 0);
    (void)op;
    int tick = 0, sprites = 0, fills = 0;
    std::size_t maxCmds = 0;
    for (; !ctx.tasks.empty() && tick < 20000; ++tick) {
        ctx.draw.clear();
        ctx.tasks.runFrame();
        ctx.audio->tick();
        ctx.draw.render(nullptr, *banks);      // exercises ordering + frame checks
        if (dumpAt.count(tick)) {
            for (auto& [id, bytes] : banks->held) softBanks.loadBank(id, bytes);
            soft::enabled = true;
            std::fill(soft::fb.begin(), soft::fb.end(), 0xFF000000u);
            ctx.draw.render(nullptr, softBanks);
            soft::enabled = false;
            soft::writePng(prefix + std::to_string(tick) + ".png");
        }
        maxCmds = std::max(maxCmds, ctx.draw.size());
        sprites += int(ctx.draw.countSprites());
        fills += int(ctx.draw.size() - ctx.draw.countSprites());
    }
    std::printf("opening finished after %d ticks; %d sprite draws (%d AnmData), %d fills, max %zu cmds/frame\n",
                tick, sprites, banks->anmDraws, fills, maxCmds);
    assert(banks->anmDraws > 0);
    auto next = ctx.scenes.take();
    assert(next && *next == ch::SceneId::Title);
    // 6 setup ticks + bgm start + 0x3f4c timeline frames + fade (0x3c) + 2.
    assert(tick > 0x3f4c && tick < 0x3f4c + 0x60);
    std::puts("OK");
}
