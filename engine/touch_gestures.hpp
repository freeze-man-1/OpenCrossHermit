// ============================================================================
// touch_gestures.hpp — touch screen gestures, and the mouse they stand in for
//
// SDL-free, so tests/touch_test.cpp drives it directly. The engine feeds it
// SDL finger events (on the web, SDL turns the browser's touch events into
// those), already mapped to the logical screen: SDL's renderer normalizes a
// finger to the letterboxed logical viewport, so x * logicalW is the game's x.
//
// One finger stands in for the mouse, so the original's mouse-only UI plays on
// a phone and turns into the same game commands (network play is lockstep):
//   tap             left click where it lifted
//   long-press      right click where it is held (fires while still held)
//   drag            left button held from where it started, released where it
//                   lifts (the selection box)
// A second finger ends the one-finger gesture (a drag is released where it
// is) and starts two-finger pan / pinch, which drive no mouse: they are only
// reported, for a UI that wants them. Nothing more happens until every finger
// is up.
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>

namespace legacy::touch {

struct Point { float x = 0.f, y = 0.f; };

// What happened since the last tick, plus what is going on now.
struct Gestures {
    int   fingers = 0;                 // down now
    bool  used = false;                // any touch since start (a UI may then show its touch controls)
    bool  tap = false;           Point tapAt;
    bool  longPress = false;     Point longPressAt;
    // One-finger drag: began / ended are this tick's edges, active while it lasts.
    bool  dragBegan = false, dragActive = false, dragEnded = false;
    Point dragStart, dragPos;
    // Two fingers: pan is the midpoint's movement this tick, pinch the ratio of
    // the fingers' spread this tick to the last (1 = no change).
    bool  twoFinger = false;
    Point center, pan;
    float pinch = 1.f;
};

// A pointer step for the mouse state: move there, then press or release a
// button (0 = move only). endTick: let one logic tick see this before the
// next step, so the game sees the pointer arrive before the press, and the
// press where the drag started.
struct MouseStep {
    enum Button : std::uint8_t { None, Left, Right };
    Point   at;
    Button  button = None;
    bool    down = false;
    bool    endTick = false;
};

struct Config {
    float         slop = 14.f;          // logical px a finger may wander and still tap / long-press
    std::uint32_t longPressMs = 450;
};

class Recognizer {
public:
    explicit Recognizer(Config cfg = {}) : cfg_(cfg) {}

    // Off: the mouse is left alone, only Gestures report (for a UI that reads
    // gestures itself). A one-finger gesture under way is dropped.
    void setEmulateMouse(bool on) {
        if (on == emulate_) return;
        if (!on && state_ == State::Dragging) push({.at = last_, .button = MouseStep::Left, .down = false});
        emulate_ = on;
        if (state_ == State::Pending || state_ == State::Dragging) state_ = State::Ignore;
    }
    [[nodiscard]] bool emulateMouse() const noexcept { return emulate_; }

    void down(std::int64_t id, Point p, std::uint32_t ms) {
        g_.used = true;
        if (count_ < 2) { ids_[count_] = id; pos_[count_] = p; }
        ++count_;
        g_.fingers = count_;
        if (count_ == 1) {
            if (state_ != State::Idle) return;
            state_ = State::Pending;
            start_ = last_ = p;
            downMs_ = ms;
            if (emulate_) push({.at = p});   // hover where the finger is
        } else if (count_ == 2) {
            if (state_ == State::Dragging) endDrag(last_);
            state_ = State::Two;
            g_.twoFinger = true;
            lastSpread_ = spread();
            lastCenter_ = g_.center = mid();
        }
    }

    void move(std::int64_t id, Point p, std::uint32_t ms) {
        const int k = slot(id);
        if (k >= 0) pos_[k] = p;
        if (state_ == State::Two) {
            if (count_ < 2 || k < 0) return;
            const Point c = mid();
            g_.pan.x += c.x - lastCenter_.x;
            g_.pan.y += c.y - lastCenter_.y;
            g_.center = lastCenter_ = c;
            const float s = spread();
            if (lastSpread_ > 1.f && s > 1.f) g_.pinch *= s / lastSpread_;
            lastSpread_ = s;
            return;
        }
        if (k != 0 || count_ != 1) return;
        update(ms);
        if (state_ == State::Pending && dist(p, start_) > cfg_.slop) {
            state_ = State::Dragging;
            g_.dragBegan = g_.dragActive = true;
            g_.dragStart = start_;
            if (emulate_) {
                push({.at = start_, .endTick = true});
                push({.at = start_, .button = MouseStep::Left, .down = true, .endTick = true});
            }
        }
        last_ = p;
        if (state_ == State::Dragging) {
            g_.dragPos = p;
            if (emulate_) moveTo(p);
        } else if (state_ == State::Pending && emulate_) {
            moveTo(p);
        }
    }

    void up(std::int64_t id, Point p, std::uint32_t ms) {
        const int k = slot(id);
        if (k < 0) {         // a finger past the second (untracked), or one never seen down
            if (count_ > 2) g_.fingers = --count_;
            return;
        }
        if (count_ == 1) {
            update(ms);
            if (state_ == State::Pending) {
                g_.tap = true;
                g_.tapAt = p;
                if (emulate_) click(p, MouseStep::Left);
            } else if (state_ == State::Dragging) {
                endDrag(p);
            }
        }
        // Keep the remaining finger in slot 0.
        if (k == 0) { ids_[0] = ids_[1]; pos_[0] = pos_[1]; }
        count_ = std::max(count_ - 1, 0);
        g_.fingers = count_;
        if (count_ < 2) g_.twoFinger = false;
        if (count_ == 0) state_ = State::Idle;
        else if (state_ == State::Two) state_ = State::Ignore;   // until every finger is up
    }

    // Lost focus, or the touch was cancelled: forget every finger.
    void cancel() {
        if (state_ == State::Dragging) endDrag(last_);
        count_ = 0;
        g_.fingers = 0;
        g_.twoFinger = false;
        state_ = State::Idle;
    }

    // Call every frame: a long-press fires on time even when the finger is still.
    void update(std::uint32_t ms) {
        if (state_ != State::Pending || ms - downMs_ < cfg_.longPressMs) return;
        state_ = State::Ignore;
        g_.longPress = true;
        g_.longPressAt = last_;
        if (emulate_) click(last_, MouseStep::Right);
    }

    // The next step for the mouse, if any.
    [[nodiscard]] bool hasMouseStep() const noexcept { return !steps_.empty(); }
    MouseStep popMouseStep() { MouseStep s = steps_.front(); steps_.pop_front(); return s; }

    // This tick's gestures; the edges and deltas start over.
    [[nodiscard]] const Gestures& gestures() const noexcept { return g_; }
    void endTick() {
        g_.tap = g_.longPress = g_.dragBegan = g_.dragEnded = false;
        g_.pan = {};
        g_.pinch = 1.f;
    }

private:
    enum class State : std::uint8_t { Idle, Pending, Dragging, Two, Ignore };

    static float dist(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
    int slot(std::int64_t id) const {
        for (int i = 0; i < std::min(count_, 2); ++i)
            if (ids_[i] == id) return i;
        return -1;
    }
    Point mid() const { return {(pos_[0].x + pos_[1].x) / 2.f, (pos_[0].y + pos_[1].y) / 2.f}; }
    float spread() const { return dist(pos_[0], pos_[1]); }

    void push(MouseStep s) { steps_.push_back(s); }
    // Moves merge into a plain move before them.
    void moveTo(Point p) {
        if (!steps_.empty() && steps_.back().button == MouseStep::None && !steps_.back().endTick) steps_.back().at = p;
        else push({.at = p});
    }
    void click(Point p, MouseStep::Button b) {
        push({.at = p, .endTick = true});
        push({.at = p, .button = b, .down = true});
        push({.at = p, .button = b, .down = false});
    }
    void endDrag(Point p) {
        g_.dragActive = false;
        g_.dragEnded = true;
        g_.dragPos = p;
        if (emulate_) push({.at = p, .button = MouseStep::Left, .down = false});
        state_ = State::Ignore;
    }

    Config        cfg_;
    bool          emulate_ = true;
    State         state_ = State::Idle;
    int           count_ = 0;
    std::int64_t  ids_[2] = {};
    Point         pos_[2];
    Point         start_, last_, lastCenter_;
    float         lastSpread_ = 0.f;
    std::uint32_t downMs_ = 0;
    Gestures      g_;
    std::deque<MouseStep> steps_;
};

} // namespace legacy::touch
