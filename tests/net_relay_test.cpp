// net_relay_test: two clients against a running relay (tools/netserver).
//   net_relay_test ws://127.0.0.1:8787/
//   net_relay_test apinator:us     (no relay: the first client's game hosts)
// They create and join a room, start a game, issue orders at different
// rates and run their lockstep for ~4 s; both must see the same commands in
// the same frames and report the same check words. Then one client reports
// a tampered check, which the relay must flag.
#include "game/net/net_session.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

using namespace ch::net;

namespace {
std::uint32_t nowMs() {
    using namespace std::chrono;
    return std::uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// A stand-in for the battle: its state is a hash of every command applied
// and the frame it was applied in, plus a generator stepped once a frame.
struct Sim {
    std::uint32_t hash = 0x811c9dc5u, seed = 0x87654321u, rolls = 0;
    int applied = 0;
    bool tamper = false;
    void step(Lockstep& ls) {
        if (const Lockstep::Turn* t = ls.beginFrame()) {
            for (const auto& [slot, c] : t->commands) {
                for (const std::uint32_t v : {std::uint32_t(slot), std::uint32_t(c.type), std::uint32_t(c.cx),
                                              std::uint32_t(ls.frame())})
                    hash = (hash ^ v) * 0x01000193u;
                ++applied;
            }
        }
        seed = seed * 0x48d159e1u + 1u;
        ++rolls;
        ls.endFrame([&](std::uint32_t& s, std::uint32_t& r, std::uint32_t& h, std::uint8_t& refused) {
            s = seed;
            r = rolls;
            h = tamper ? hash ^ 1u : hash;
            refused = 0;
        });
    }
};

bool waitFor(Session& a, Session& b, auto&& done, int ms = 3000) {
    for (int t = 0; t < ms / 5; ++t) {
        a.poll(nowMs());
        b.poll(nowMs());
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

int fail(const char* what, const Session& a, const Session& b) {
    std::fprintf(stderr, "net_relay_test: %s (a: %s, b: %s)\n", what, a.lastError().c_str(), b.lastError().c_str());
    return 1;
}
} // namespace

int main(int argc, char** argv) {
    const std::string url = argc > 1 ? argv[1] : "ws://127.0.0.1:8787/";
    Session a, b;
    a.connect(url, "Alice");
    b.connect(url, "Bob");
    if (!waitFor(a, b, [&] { return a.state() == Session::State::Lobby && b.state() == Session::State::Lobby; }))
        return fail("no welcome", a, b);
    a.createRoom("test room", 2, true);
    if (!waitFor(a, b, [&] { return a.state() == Session::State::InRoom; })) return fail("no room", a, b);
    const std::string code = a.room().code;
    b.listRooms();
    b.joinRoom(code);
    if (!waitFor(a, b, [&] { return b.state() == Session::State::InRoom && a.room().members.size() == 2; }))
        return fail("join failed", a, b);
    b.setReady(true);
    if (!waitFor(a, b, [&] { return a.room().members.size() == 2 && a.room().members[1].ready; })) return fail("ready lost", a, b);
    a.startGame();
    if (!waitFor(a, b, [&] { return a.state() == Session::State::InGame && b.state() == Session::State::InGame; }))
        return fail("no start", a, b);
    a.loaded();
    b.loaded();
    std::printf("room %s started: slots a=%d b=%d, delay %d\n", code.c_str(), a.room().you, b.room().you,
                a.lockstep.inputDelay());

    Sim sa, sb;
    int issued = 0, maxBuffered = 0;
    for (int tick = 0; tick < 240; ++tick) {
        const auto t0 = std::chrono::steady_clock::now();
        if (tick % 20 == 5) { a.lockstep.issue({.type = 1, .unit = 0, .cx = std::int16_t(tick)}); ++issued; }
        if (tick % 33 == 7) { b.lockstep.issue({.type = 3, .unit = 1, .cx = std::int16_t(tick)}); ++issued; }
        a.poll(nowMs());
        b.poll(nowMs());
        for (int n = a.lockstep.framesToRun(); n > 0; --n) sa.step(a.lockstep);
        for (int n = b.lockstep.framesToRun(); n > 0; --n) sb.step(b.lockstep);
        maxBuffered = std::max({maxBuffered, a.lockstep.buffered(), b.lockstep.buffered()});
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
    }
    // Let both finish the turns still in flight.
    waitFor(a, b, [&] {
        for (int n = a.lockstep.framesToRun(); n > 0; --n) sa.step(a.lockstep);
        for (int n = b.lockstep.framesToRun(); n > 0; --n) sb.step(b.lockstep);
        return sa.applied == issued && sb.applied == issued;
    }, 2000);
    const std::int64_t common = std::min(a.lockstep.frame(), b.lockstep.frame());
    std::printf("frames a=%lld b=%lld, applied a=%d b=%d of %d issued, most turns buffered %d, rtt %d ms\n",
                (long long)a.lockstep.frame(), (long long)b.lockstep.frame(), sa.applied, sb.applied, issued, maxBuffered,
                a.rttMs());
    if (common < 150) return fail("the battle barely ran", a, b);
    if (sa.applied != issued || sb.applied != issued) return fail("commands went missing", a, b);
    if (sa.hash != sb.hash) return fail("the two sides drifted apart", a, b);
    if (!a.alerts().empty() || !b.alerts().empty()) return fail("a desync was reported for honest clients", a, b);

    // A tampered client: its next check disagrees.
    sb.tamper = true;
    const bool flagged = waitFor(a, b, [&] {
        for (int n = a.lockstep.framesToRun(); n > 0; --n) sa.step(a.lockstep);
        for (int n = b.lockstep.framesToRun(); n > 0; --n) sb.step(b.lockstep);
        return !a.alerts().empty() && !b.alerts().empty();
    }, 3000);
    if (!flagged) return fail("the tampered check was not caught", a, b);
    std::printf("tampered check caught at turn %u: %s\n", a.alerts().front().turn, a.alerts().front().text.c_str());
    std::printf("net_relay_test: ok\n");
    return 0;
}
