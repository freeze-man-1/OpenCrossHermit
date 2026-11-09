// touch::Recognizer (engine/touch_gestures.hpp): the gestures, and the mouse
// ticks they stand in for, run through the engine's per-tick mouse sampling.
#include "../engine/touch_gestures.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

using namespace legacy::touch;

namespace {
// InputState's mouse: a press released before the tick still holds for it.
struct Tick { int x, y; unsigned held, pressed, released; };
struct Mouse {
    Recognizer& r;
    unsigned down = 0, latch = 0, prev = 0;
    int x = 0, y = 0;
    Tick tick() {
        while (r.hasMouseStep()) {   // InputState::applyTouch
            const MouseStep s = r.popMouseStep();
            x = int(s.at.x); y = int(s.at.y);
            if (s.button != MouseStep::None) {
                const unsigned bit = s.button == MouseStep::Left ? 1u : 2u;
                if (s.down) { down |= bit; latch |= bit; } else down &= ~bit;
            }
            if (s.endTick) break;
        }
        const unsigned cur = down | latch;
        Tick t{x, y, cur, (prev ^ cur) & cur, (prev ^ cur) & prev};
        prev = cur;
        latch = 0;
        r.endTick();
        return t;
    }
    std::vector<Tick> ticks(int n) { std::vector<Tick> v; for (int i = 0; i < n; ++i) v.push_back(tick()); return v; }
};
} // namespace

int main() {
    // Tap: the pointer arrives, then a left click there.
    {
        Recognizer r;
        Mouse m{r};
        r.down(7, {100, 200}, 1000);
        r.move(7, {104, 203}, 1050);           // within the slop
        r.up(7, {104, 203}, 1100);
        CHECK(r.gestures().tap && r.gestures().tapAt.x == 104.f);
        const auto t = m.ticks(4);
        CHECK(t[0].x == 104 && t[0].y == 203 && t[0].held == 0);
        CHECK(t[1].pressed == 1 && t[1].x == 104);
        CHECK(t[2].released == 1 && t[2].held == 0);
        CHECK(t[3].held == 0 && t[3].pressed == 0 && t[3].released == 0);
        CHECK(!r.gestures().tap);               // an edge: gone after its tick
    }
    // Long-press: a right click while still held; the lift does nothing more.
    {
        Recognizer r;
        Mouse m{r};
        r.down(1, {300, 300}, 0);
        r.update(200);
        CHECK(!r.gestures().longPress);
        r.update(460);
        CHECK(r.gestures().longPress && r.gestures().longPressAt.x == 300.f);
        auto t = m.ticks(3);
        CHECK(t[1].pressed == 2 && t[1].x == 300);
        CHECK(t[2].released == 2);
        r.move(1, {400, 300}, 600);            // no drag after a long-press
        r.up(1, {400, 300}, 700);
        CHECK(!r.gestures().tap && !r.gestures().dragBegan);
        for (const Tick& k : m.ticks(3)) CHECK(k.held == 0 && k.pressed == 0);
    }
    // Drag: left pressed where it started, held while moving, released where it lifted.
    {
        Recognizer r;
        Mouse m{r};
        r.down(2, {100, 100}, 0);
        r.move(2, {150, 120}, 50);
        CHECK(r.gestures().dragBegan && r.gestures().dragActive);
        r.move(2, {200, 160}, 80);
        r.update(900);                          // no long-press once dragging
        CHECK(!r.gestures().longPress);
        auto t = m.ticks(3);
        CHECK(t[0].x == 100 && t[0].held == 0);
        CHECK(t[1].pressed == 1 && t[1].x == 100 && t[1].y == 100);
        CHECK(t[2].held == 1 && t[2].x == 200 && t[2].y == 160);
        r.up(2, {210, 170}, 1000);
        CHECK(r.gestures().dragEnded && !r.gestures().dragActive);
        t = m.ticks(2);
        CHECK(t[0].released == 1 && t[0].x == 210 && t[0].y == 170);
    }
    // Two fingers: no click, pan and pinch reported, nothing until all are up.
    {
        Recognizer r;
        Mouse m{r};
        r.down(1, {100, 100}, 0);
        r.down(2, {200, 100}, 30);
        CHECK(r.gestures().twoFinger && r.gestures().fingers == 2);
        r.move(2, {300, 100}, 60);             // spread 100 -> 200, midpoint +50
        CHECK(r.gestures().pinch > 1.99f && r.gestures().pinch < 2.01f);
        CHECK(r.gestures().pan.x == 50.f && r.gestures().center.x == 200.f);
        m.tick();
        CHECK(r.gestures().pinch == 1.f && r.gestures().pan.x == 0.f);
        r.up(1, {100, 100}, 100);
        CHECK(!r.gestures().twoFinger && r.gestures().fingers == 1);
        r.update(2000);
        r.up(2, {300, 100}, 2100);
        CHECK(!r.gestures().tap && !r.gestures().longPress && r.gestures().fingers == 0);
        for (const Tick& k : m.ticks(4)) CHECK(k.held == 0 && k.pressed == 0);
        // and the next touch taps again
        r.down(3, {50, 60}, 3000);
        r.up(3, {50, 60}, 3050);
        CHECK(r.gestures().tap);
    }
    // A second finger during a drag releases it where it is.
    {
        Recognizer r;
        Mouse m{r};
        r.down(1, {100, 100}, 0);
        r.move(1, {200, 200}, 50);
        r.down(2, {500, 500}, 60);
        CHECK(r.gestures().dragEnded);
        const auto t = m.ticks(4);
        CHECK(t[1].pressed == 1 && t[2].released == 1 && t[2].x == 200);
    }
    // Three fingers: the untracked one still counts.
    {
        Recognizer r;
        r.down(1, {1, 1}, 0); r.down(2, {2, 2}, 0); r.down(3, {3, 3}, 0);
        CHECK(r.gestures().fingers == 3);
        r.up(3, {3, 3}, 10); r.up(1, {1, 1}, 10); r.up(2, {2, 2}, 10);
        CHECK(r.gestures().fingers == 0);
        r.down(4, {9, 9}, 20); r.up(4, {9, 9}, 30);
        CHECK(r.gestures().tap);
    }
    // Without mouse emulation: gestures only.
    {
        Recognizer r;
        r.setEmulateMouse(false);
        r.down(1, {10, 10}, 0);
        r.up(1, {10, 10}, 10);
        CHECK(r.gestures().tap && !r.hasMouseStep());
    }
    std::puts("touch: ok");
    return 0;
}
