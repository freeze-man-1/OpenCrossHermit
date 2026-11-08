// Scheduler semantics test: frame skipping, nested coroutines, return values,
// self-kill from depth, owned-task deletion, link order. No SDL required.
#include "../game/task_system.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

static std::vector<std::string> g_log;
static int g_frame = 0;
static void log(const std::string& s) { g_log.push_back(std::to_string(g_frame) + ":" + s); }

static ch::Co<int> pickAfter(int frames, int value) {
    for (int i = 0; i < frames; ++i) co_await ch::yieldFrame();
    co_return value;
}
static ch::Co<> deepKill() {
    co_await ch::yieldFrame();
    co_await ch::killSelf();
    log("UNREACHABLE");
}

struct A : ch::Task {
    inline static int alive = 0;
    A() { ++alive; } ~A() override { --alive; }
    ch::Co<> start(std::uint32_t p) override {
        log("A start " + std::to_string(p));
        co_await ch::yieldFrame(2);          // skip 2 -> resumes 3 frames later
        log("A after skip");
        int v = co_await pickAfter(2, 42);
        log("A got " + std::to_string(v));
    }
};
struct B : ch::Task {
    ch::Co<> start(std::uint32_t) override {
        log("B start");
        co_await deepKill();
        log("UNREACHABLE");
    }
};

int main() {
    ch::TaskManager tm;
    B b;
    tm.link(std::make_unique<A>(), 5, 7);
    tm.link(b, 1);
    for (g_frame = 0; g_frame < 10; ++g_frame) tm.runFrame();
    for (auto& s : g_log) std::printf("%s\n", s.c_str());
    const std::vector<std::string> want = {
        "0:B start", "0:A start 7", "3:A after skip", "5:A got 42"};
    assert(g_log == want);
    assert(A::alive == 0);
    assert(!b.isLinked());
    assert(tm.empty());
    std::printf("OK\n");
}
