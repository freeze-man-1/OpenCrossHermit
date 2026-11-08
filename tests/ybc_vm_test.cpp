// Runs the New Game script chain (CH001.ybc -> chapter scripts) through the
// YBC VM with a host that completes every presentation opcode at once.
// Checks control flow, script chaining and termination; logs opcode coverage.
// Usage: ybc_vm_test <imported data folder> (the JP release; exit 77 without it)
#include "../game/adv/ybc_vm.hpp"
#include "../game/state/game_logic.hpp"
#include "../game/state/unit_stats.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

namespace {
class NullHost final : public ch::IYbcHost {
public:
    explicit NullHost(std::string root) : root_(std::move(root)) {}
    std::vector<std::uint8_t> loadFile(std::string_view legacy) override {
        std::string p(legacy);
        for (char& c : p) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        while (p.find("//") != std::string::npos) p.erase(p.find("//"), 1);
        std::ifstream f(root_ + "/jp/" + p, std::ios::binary);
        if (!f) f.open(root_ + "/common/" + p, std::ios::binary);
        if (!f) { std::fprintf(stderr, "missing %s\n", p.c_str()); std::abort(); }
        scripts.push_back(p);
        return {std::istreambuf_iterator<char>(f), {}};
    }
    void exec(ch::YbcVm& vm, std::uint16_t op, std::uint32_t rec) override {
        ++hostOps[op];
        if (op == 0x11) ++lines;                          // message text
        (void)vm; (void)rec;
    }
    bool waitDone(std::uint16_t) override { return true; }
    void scriptEnded() override {}

    std::map<std::uint16_t, int> hostOps;
    std::vector<std::string> scripts;
    int lines = 0;
private:
    std::string root_;
};
} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || !std::ifstream(std::string(argv[1]) + "/installed.txt")) { std::puts("SKIP: no imported game data"); return 77; }
    NullHost host(argv[1]);
    ch::GameWork work;
    ch::logic::newGame(work, ch::GameVersion::Jp);        // April, week 0, day 1, starting roster
    ch::logic::advanceWeek(work, ch::GameVersion::Jp);    // WeekStart: week 1
    if (work.month() != 4 || work.week() != 1 || work.s16(0x7a5260) != 3 || !ch::logic::inRoster(work, 9)) {
        std::puts("FAIL: new game state");
        return 1;
    }
    for (int id : {3, 4, 9}) {
        const auto s = ch::logic::unitSummary(work, ch::GameVersion::Jp, id);
        int open = 0;
        for (std::uint32_t j = 0; j < 0x1e; ++j) open += work.u8(ch::GameWork::unitProgress(id) + 0xf9 + j);
        std::printf("unit %d: job %d, rank %u, stat exp %d, %d jobs open\n", id,
                    work.s16(ch::GameWork::unitRecord(id) + 6), s.rank, s.statExp, open);
        // FUN_004b93c0 derived block: a few fields, for eyeballing.
        ch::stats::Derived d;
        ch::stats::Bonus b;
        ch::stats::derive(ch::stats::readRecord(work, id), d, b, ch::GameVersion::Jp);
        std::printf("  derived: +14 %d +18 %d +1a %d +1e %d +20 %d +2e %d +38 %d +3a %d +3c %d +4c %u +4d %u +4e %u +4f %u +50 %u +52 %d\n",
                    d.s16(0x14), d.s16(0x18), d.s16(0x1a), d.s16(0x1e), d.s32(0x20), d.s16(0x2e), d.s16(0x38),
                    d.s16(0x3a), d.s16(0x3c), d.u8(0x4c), d.u8(0x4d), d.u8(0x4e), d.u8(0x4f), d.u8(0x50), d.s16(0x52));
        assert(d.s16(0x14) >= 1 && d.s16(0x14) <= 999);
    }
    ch::YbcVm vm(host, work);
    vm.load("Data\\Adv\\dat\\CH001.ybc", 8);

    int ticks = 0;
    while (vm.tick() && ticks < 1'000'000) ++ticks;
    std::printf("ended after %d ticks, %llu instructions, %d text lines\n", ticks,
                static_cast<unsigned long long>(vm.executed()), host.lines);
    std::printf("scripts:");
    for (auto& s : host.scripts) std::printf(" %s", s.c_str());
    std::printf("\nhost opcodes:");
    for (auto& [op, n] : host.hostOps) std::printf(" %02x:%d", op, n);
    std::printf("\n");
    assert(ticks < 1'000'000);
    assert(host.scripts.size() >= 2);      // chained out of CH001
    assert(host.lines > 0);
    std::puts("OK");
}
