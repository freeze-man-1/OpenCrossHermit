// ============================================================================
// lockstep.hpp — the client's side of the relay's turn clock
//
// The battle runs frame by frame as before; Lockstep only says how many
// frames may run now and hands out each turn's commands at its first frame.
//   - Turn t covers frames [t * turnFrames, (t + 1) * turnFrames). Its first
//     frame may run only once the relay's TURN t is in (FUN_00456bc0's wait,
//     without the stall: the relay closes turns on time).
//   - Normally one frame runs per tick; with more than kCatchUpTurns turns
//     buffered (a hitch, a slow tab) two run, so the client drifts back to
//     the relay's clock instead of staying behind.
//   - Orders the player gives go out as INPUT for the newest turn seen plus
//     the relay's input delay, all of one tick's orders in one message.
//   - After every checkEvery'th turn the game's check words go out (CHECK).
// It owns no socket: `send` is where its messages go.
// In the original: FUN_00457030 sends our frame's commands (message 4,
// FUN_0048d400 empties its packet, FUN_0048ceb0 fills it), FUN_00456bc0 /
// FUN_00457110 wait for everyone's (FUN_0048d490 / FUN_0048d280 put them on
// the command queue with the check words) -> flush / onTurn / beginFrame.
// ============================================================================
#pragma once
#include "net_protocol.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <utility>

namespace ch::net {

class Lockstep {
public:
    struct Turn {
        std::uint32_t turn = 0;
        std::vector<std::pair<int, Command>> commands;   // (sender slot, command), in the relay's order
    };
    using Sender = std::function<void(std::vector<std::uint8_t>)>;
    static constexpr int kCatchUpTurns = 3;

    void start(int turnFrames, int inputDelay, int checkEvery, int slot, Sender send) {
        turnFrames_ = turnFrames > 0 ? turnFrames : 2;
        delay_ = inputDelay;
        checkEvery_ = checkEvery > 0 ? checkEvery : 15;
        slot_ = slot;
        send_ = std::move(send);
        frame_ = 0;
        newest_ = -1;
        sentFor_ = -1;
        turns_.clear();
        pending_.clear();
        running_ = true;
    }
    void stop() { running_ = false; }
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] int slot() const noexcept { return slot_; }
    [[nodiscard]] int inputDelay() const noexcept { return delay_; }
    [[nodiscard]] std::int64_t frame() const noexcept { return frame_; }
    // Turns received but not yet run (0 = waiting on the relay).
    [[nodiscard]] int buffered() const noexcept { return int(turns_.size()); }

    // The relay's TURN message (already decoded).
    void onTurn(Turn t, int inputDelay) {
        newest_ = std::int64_t(t.turn);
        delay_ = inputDelay;
        turns_.push_back(std::move(t));
    }

    // An order of ours: it goes out at the end of the tick (flush).
    void issue(const Command& c) { pending_.push_back(c); }
    void flush() {
        if (!running_ || pending_.empty() || !send_) return;
        // Never aim at a turn before one already aimed at: the orders keep
        // their order even when the delay shrinks.
        const std::int64_t base = newest_ < 0 ? 0 : newest_;
        const std::int64_t target = std::max(base + delay_, sentFor_);
        for (std::size_t at = 0; at < pending_.size(); at += kMaxCommandsPerTurn) {
            const std::size_t n = std::min<std::size_t>(kMaxCommandsPerTurn, pending_.size() - at);
            Writer w(Msg::Input);
            w.u32(std::uint32_t(target)).u8(std::uint8_t(n));
            for (std::size_t i = 0; i < n; ++i) w.command(pending_[at + i]);
            send_(w.take());
        }
        sentFor_ = target;
        pending_.clear();
    }

    // How many frames the game may run this tick.
    [[nodiscard]] int framesToRun() const {
        if (!running_) return 0;
        const std::int64_t ready = turns_.empty() ? 0
            : (std::int64_t(turns_.back().turn) + 1) * turnFrames_ - frame_;
        if (ready <= 0) return 0;
        return turns_.size() > std::size_t(kCatchUpTurns) && ready >= 2 ? 2 : 1;
    }
    // At a turn's first frame: its commands (else null). Call before the frame.
    [[nodiscard]] const Turn* beginFrame() const {
        if (turns_.empty() || frame_ % turnFrames_ != 0) return nullptr;
        return &turns_.front();
    }
    // After the frame. `check` gives (seed, rolls, hash, refusedMask) when a
    // CHECK is due at the end of this turn.
    void endFrame(const std::function<void(std::uint32_t&, std::uint32_t&, std::uint32_t&, std::uint8_t&)>& check) {
        ++frame_;
        if (frame_ % turnFrames_ != 0 || turns_.empty()) return;
        const std::uint32_t done = turns_.front().turn;
        turns_.pop_front();
        if ((done + 1) % std::uint32_t(checkEvery_) == 0 && check && send_) {
            std::uint32_t seed = 0, rolls = 0, hash = 0;
            std::uint8_t refused = 0;
            check(seed, rolls, hash, refused);
            Writer w(Msg::Check);
            w.u32(done).u32(seed).u32(rolls).u32(hash).u8(refused);
            send_(w.take());
        }
    }

private:
    int turnFrames_ = 2, delay_ = 2, checkEvery_ = 15, slot_ = 0;
    std::int64_t frame_ = 0, newest_ = -1, sentFor_ = -1;
    bool running_ = false;
    std::deque<Turn> turns_;
    std::vector<Command> pending_;
    Sender send_;
};

// Decodes a TURN message body (after its type byte); false when malformed.
[[nodiscard]] inline bool readTurn(Reader& r, Lockstep::Turn& t, int& delay) {
    t.turn = r.u32();
    delay = r.u8();
    const int groups = r.u8();
    for (int g = 0; g < groups && r.ok(); ++g) {
        const int slot = r.u8(), n = r.u8();
        for (int i = 0; i < n && r.ok(); ++i) t.commands.emplace_back(slot, r.command());
    }
    return r.ok();
}

} // namespace ch::net
