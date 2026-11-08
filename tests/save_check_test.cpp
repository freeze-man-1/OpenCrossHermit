// ============================================================================
// save_check_test.cpp — logic::saveCheck, the sanity check on uploaded saves.
// ============================================================================
#include "../game/state/game_logic.hpp"
#include "../game/state/game_work.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
using ch::logic::SaveProblem;
using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t kBase = 0x7a50e8;
constexpr std::uint32_t kInRoster = 0x7a5120, kMembers = 0x7a5210, kMemberCount = 0x7a5260;
constexpr std::uint32_t kGuests = 0x7a5262, kGuestCount = 0x7a528a, kYear = 0x7a528c, kMonth = 0x7a528e, kWeek = 0x7a5290;

ch::GameWork gWork;

// Members 3, 4 and guest 0x65 in April of year 1, as after a new game.
Bytes goodSave() {
    gWork.set16(kMemberCount, 2);
    gWork.set16(kMembers, 3);
    gWork.set16(kMembers + 2, 4);
    gWork.set16(kGuestCount, 1);
    gWork.set16(kGuests, 0x65);
    gWork.set8(kInRoster + 3, 1);
    gWork.set8(kInRoster + 4, 1);
    gWork.set8(kInRoster + 0x65, 1);
    gWork.set16(kYear, 1);
    gWork.set16(kMonth, 4);
    gWork.set16(kWeek, 1);
    return ch::logic::saveGame(gWork);
}

void put16(Bytes& b, std::uint32_t va, int v) {
    b[va - kBase] = std::uint8_t(v);
    b[va - kBase + 1] = std::uint8_t(v >> 8);
}

// Puts the checksum right again, as an edited save would.
void reseal(Bytes& b) {
    std::int32_t sum = 0;
    for (std::size_t i = 0; i < b.size(); ++i)
        if (i < 0x2c || i > 0x2f) sum += std::int8_t(b[i]);
    for (int i = 0; i < 4; ++i) b[0x2c + std::size_t(i)] = std::uint8_t(std::uint32_t(sum) >> (i * 8));
}

int failures = 0;
void expect(const char* what, const Bytes& b, SaveProblem want) {
    if (ch::logic::saveCheck(b) == want) return;
    std::printf("FAIL: %s: got %d, want %d\n", what, int(ch::logic::saveCheck(b)), int(want));
    ++failures;
}

template <typename F>
void edited(const char* what, F change, SaveProblem want = SaveProblem::Contents) {
    Bytes b = goodSave();
    change(b);
    reseal(b);
    expect(what, b, want);
}
} // namespace

int main() {
    const Bytes good = goodSave();
    expect("good save", good, SaveProblem::None);
    if (!ch::logic::saveValid(good)) { std::puts("FAIL: saveValid on a good save"); ++failures; }

    expect("empty", {}, SaveProblem::Size);
    expect("short", Bytes(good.begin(), good.end() - 1), SaveProblem::Size);
    Bytes longer = good;
    longer.push_back(0);
    expect("long", longer, SaveProblem::Size);
    expect("zeros", Bytes(good.size()), SaveProblem::Format);

    Bytes flipped = good;
    flipped[0x1000] ^= 1;
    expect("flipped byte", flipped, SaveProblem::Checksum);
    if (ch::logic::saveValid(flipped)) { std::puts("FAIL: saveValid on a flipped byte"); ++failures; }

    edited("version", [](Bytes& b) { b[0] = 6; }, SaveProblem::Format);
    edited("header", [](Bytes& b) { b[6] = 2; }, SaveProblem::Format);
    edited("member count", [](Bytes& b) { put16(b, kMemberCount, 41); });
    edited("negative count", [](Bytes& b) { put16(b, kMemberCount, -1); });
    edited("guest count", [](Bytes& b) { put16(b, kGuestCount, 21); });
    edited("member id", [](Bytes& b) { put16(b, kMembers, 0x65); });
    edited("member id 0", [](Bytes& b) { put16(b, kMembers, 0); });
    edited("guest id", [](Bytes& b) { put16(b, kGuests, 0xf0); });
    edited("duplicate", [](Bytes& b) { put16(b, kMembers + 2, 3); });
    edited("listed, not flagged", [](Bytes& b) { b[kInRoster - kBase + 3] = 0; });
    edited("flagged, not listed", [](Bytes& b) { b[kInRoster - kBase + 9] = 1; });
    edited("flag value", [](Bytes& b) { b[kInRoster - kBase + 3] = 2; });
    edited("year", [](Bytes& b) { put16(b, kYear, 100); });
    edited("month", [](Bytes& b) { put16(b, kMonth, 0); });
    edited("week", [](Bytes& b) { put16(b, kWeek, 10); });
    edited("late month", [](Bytes& b) { put16(b, kMonth, 15); }, SaveProblem::None);
    edited("week 0", [](Bytes& b) { put16(b, kWeek, 0); }, SaveProblem::None);

    if (failures == 0) std::puts("save check: ok");
    return failures == 0 ? 0 : 1;
}
