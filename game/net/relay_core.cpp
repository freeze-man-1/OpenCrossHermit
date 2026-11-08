// ============================================================================
// relay_core.cpp — RelayRoom: relay.mjs's Room and handle() for one room
// ============================================================================
#include "relay_core.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <random>

namespace ch::net {

namespace {
constexpr int kProtocol = 1;
constexpr int kCheckEvery = 15;
constexpr std::size_t kMaxCommands = 64;
using Raw = std::array<std::uint8_t, kCommandBytes>;

// Characters, not bytes (as relay.mjs's [...s]).
std::string firstChars(const std::string& s, std::size_t most) {
    std::string out;
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((std::uint8_t(s[i]) & 0xc0) != 0x80 && n++ == most) break;
        out += s[i];
    }
    return out;
}
std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
} // namespace

int RelayRoom::players() const {
    return int(std::count_if(slots_.begin(), slots_.end(), [](int p) { return p >= 0; }));
}

std::vector<int> RelayRoom::members() const {
    std::vector<int> v;
    for (const int p : slots_) if (p >= 0) v.push_back(p);
    return v;
}

void RelayRoom::error(int peer, int code, const std::string& text) {
    send_(peer, Writer(Msg::Error).u8(std::uint8_t(code)).str(text).take());
}

void RelayRoom::broadcast(const std::vector<std::uint8_t>& m) {
    for (const int p : members()) send_(p, m);
}

std::vector<std::uint8_t> RelayRoom::stateFor(int peer) const {
    Writer w(Msg::RoomState);
    w.str(code_).str(name_).u8(std::uint8_t(host_)).u8(std::uint8_t(peers_.at(peer).slot)).bytes(options_);
    const auto list = members();
    w.u8(std::uint8_t(list.size()));
    for (const int q : list) {
        const Player& o = peers_.at(q);
        w.u8(std::uint8_t(o.slot)).str(o.name).u8(o.ready ? 1 : 0)
         .u16(std::uint16_t(std::min(std::lround(o.rtt), 65535L))).bytes(o.data);
    }
    return w.take();
}

void RelayRoom::broadcastState() {
    for (const int p : members()) send_(p, stateFor(p));
}

void RelayRoom::disconnect(int peer) {
    remove(peer);
    peers_.erase(peer);
}

void RelayRoom::remove(int peer) {
    auto it = peers_.find(peer);
    if (it == peers_.end() || it->second.slot < 0) return;
    const int slot = it->second.slot;
    slots_[std::size_t(slot)] = -1;
    it->second.slot = -1;
    if (players() == 0) { stop(); return; }
    if (host_ == slot) host_ = int(std::find_if(slots_.begin(), slots_.end(), [](int p) { return p >= 0; }) - slots_.begin());
    if (inGame_) {
        // Into the next turn, for every client at the same frame: command
        // 0x43 "this slot has gone" (stamped with the slot that left).
        Raw gone{};
        gone[0] = 0x43;
        gone[2] = 0xff;
        gone[3] = 0xff;
        pending_[turn_][slot].push_back(gone);
        broadcast(Writer(Msg::PlayerLeft).u8(std::uint8_t(slot)).take());
        if (players() < 2) stop();
    }
    broadcastState();
}

int RelayRoom::computeDelay() const {
    double worst = 0;
    for (const int p : members()) worst = std::max(worst, peers_.at(p).rtt + 2 * peers_.at(p).jitter);
    const int d = int(std::ceil((worst + 8) / (turnMs_ > 0 ? turnMs_ : 33.3))) + 1;
    return std::min(std::max(d, 2), 20);
}

void RelayRoom::start(std::uint32_t nowMs) {
    inGame_ = true;
    running_ = false;
    over_.clear();
    turnMs_ = turnFrames_ * 1000.0 / 60.0;
    turn_ = 0;
    pending_.clear();
    checks_.clear();
    delay_ = computeDelay();
    std::random_device rd;
    const std::uint32_t seed = rd() & 0x7fffffffu;
    for (const int p : members()) {
        Player& o = peers_.at(p);
        o.late = 0;
        send_(p, Writer(Msg::Started).u32(seed).u8(std::uint8_t(turnFrames_)).u8(std::uint8_t(delay_))
                     .u8(kCheckEvery).u8(std::uint8_t(o.slot)).u8(std::uint8_t(max_)).take());
    }
    // The clock waits until every player's battle is loaded (Loaded), or a
    // minute at most.
    loaded_.clear();
    startedAt_ = nowMs;
}

void RelayRoom::run(std::uint32_t nowMs) {
    if (!inGame_ || running_) return;
    running_ = true;
    t0_ = nowMs;
}

void RelayRoom::stop() {
    inGame_ = false;
    running_ = false;
    for (const int p : members()) peers_.at(p).ready = false;
}

void RelayRoom::closeTurn() {
    const std::uint32_t t = turn_++;
    auto per = std::move(pending_[t]);
    pending_.erase(t);
    // Every 30 turns the delay follows the room's round trips: up at once,
    // down one turn at a time.
    if (t % 30 == 29) {
        const int want = computeDelay();
        if (want > delay_) delay_ = want;
        else if (want < delay_) --delay_;
    }
    Writer w(Msg::Turn);
    w.u32(t).u8(std::uint8_t(delay_));
    int groups = 0;
    for (const auto& [slot, c] : per) groups += !c.empty();
    w.u8(std::uint8_t(groups));
    for (const auto& [slot, cmds] : per) {
        if (cmds.empty()) continue;
        w.u8(std::uint8_t(slot)).u8(std::uint8_t(cmds.size()));
        for (const Raw& c : cmds) for (const std::uint8_t b : c) w.u8(b);
    }
    broadcast(w.take());
    // Reports a stalled player never completed.
    for (auto it = checks_.begin(); it != checks_.end();) {
        if (it->first + 300 < t) it = checks_.erase(it);
        else ++it;
    }
}

void RelayRoom::tick(std::uint32_t nowMs) {
    now_ = nowMs;
    // Pings for the round trips, and the rate limits' windows.
    if (nowMs - lastPing_ >= 1000) {
        lastPing_ = nowMs;
        for (auto& [peer, o] : peers_) {
            send_(peer, Writer(Msg::Ping).u32(nowMs).take());
            o.inputs = o.chats = 0;
        }
    }
    if (!inGame_) return;
    if (!running_) {
        if (nowMs - startedAt_ >= 60000) run(nowMs);
        return;
    }
    // Turns close on an absolute schedule (at most a few per tick: a host
    // stalled for long does not flood the room).
    for (int n = 0; n < 8 && inGame_ && double(nowMs) - t0_ >= (turn_ + 1) * turnMs_; ++n) closeTurn();
    if (inGame_ && double(nowMs) - t0_ >= (turn_ + 1) * turnMs_)
        t0_ = double(nowMs) - turn_ * turnMs_;   // too far behind: skip ahead
}

void RelayRoom::addInput(int peer, std::uint32_t turn, std::vector<Raw> cmds) {
    Player& p = peers_.at(peer);
    // Late input goes to the next turn still open; input far ahead is refused.
    if (turn < turn_) { turn = turn_; ++p.late; }
    if (turn > turn_ + 60) return;
    auto& have = pending_[turn][p.slot];
    if (have.size() + cmds.size() > kMaxCommands) { error(peer, 3, "too many commands"); return; }
    have.insert(have.end(), cmds.begin(), cmds.end());
}

void RelayRoom::addCheck(int peer, std::uint32_t turn, const std::string& key, std::uint8_t refused) {
    auto& m = checks_[turn];
    m[peers_.at(peer).slot] = {key, refused};
    if (int(m.size()) < players()) return;
    const auto all = std::move(m);
    checks_.erase(turn);
    // Everyone refused the same sender's commands: an illegal order.
    std::uint8_t refusedBy = 0xff;
    for (const auto& [slot, v] : all) refusedBy &= v.refused;
    for (int s = 0; s < kMaxPlayers; ++s)
        if (refusedBy & (1 << s))
            broadcast(Writer(Msg::Desync).u32(turn).u8(std::uint8_t(1 << s)).str("illegal commands").take());
    std::map<std::string, int> groups;
    for (const auto& [slot, v] : all) groups[v.key] |= 1 << slot;
    if (groups.size() == 1) return;
    int best = 0, bestMask = 0;
    for (const auto& [k, mask] : groups) {
        const int n = std::popcount(unsigned(mask));
        if (n > best) { best = n; bestMask = mask; }
    }
    const bool tie = std::count_if(groups.begin(), groups.end(),
                                   [&](const auto& g) { return std::popcount(unsigned(g.second)) == best; }) > 1;
    const int suspects = tie ? 0 : (((1 << max_) - 1) & ~bestMask);
    broadcast(Writer(Msg::Desync).u32(turn).u8(std::uint8_t(suspects)).str(tie ? "the players disagree" : "a player drifted").take());
    if (tie) {
        stop();
        return;
    }
    for (int s = 0; s < max_; ++s)
        if (suspects & (1 << s) && slots_[std::size_t(s)] >= 0 && slots_[std::size_t(s)] != 0) drop_(slots_[std::size_t(s)]);
}

void RelayRoom::handle(int peer, const std::vector<std::uint8_t>& m, std::uint32_t nowMs) {
    now_ = nowMs;
    Reader r(m);
    const auto type = Msg(r.u8());
    Player& p = peers_[peer];
    if (!p.hello && type != Msg::Hello && type != Msg::Ping && type != Msg::Pong) { drop_(peer); return; }
    const bool in = p.slot >= 0;
    switch (type) {
        case Msg::Hello: {
            const int version = r.u16();
            p.build = r.u32();
            p.name = firstChars(trim(r.str()), 24);
            if (p.name.empty()) p.name = "Player " + std::to_string(peer + 1);
            if (!r.ok() || version != kProtocol) {
                error(peer, 1, "protocol " + std::to_string(version) + ", the host speaks " + std::to_string(kProtocol));
                drop_(peer);
                return;
            }
            p.hello = true;
            send_(peer, Writer(Msg::Welcome).u32(std::uint32_t(peer + 1)).take());
            send_(peer, Writer(Msg::Ping).u32(nowMs).take());
            break;
        }
        case Msg::CreateRoom: {
            if (peer != 0 || made_) break;
            const std::string name = r.str();
            const int most = r.u8();
            public_ = r.u8() != 0;
            name_ = firstChars(name, 40);
            if (name_.empty()) name_ = p.name + "'s room";
            max_ = std::min(std::max(most ? most : 2, 2), kMaxPlayers);
            build_ = p.build;
            slots_.assign(std::size_t(max_), -1);
            made_ = true;
            slots_[0] = peer;
            p.slot = 0;
            host_ = 0;
            broadcastState();
            break;
        }
        case Msg::JoinRoom: {
            const std::string code = r.str();
            std::string want;
            for (const char c : trim(code)) want += char(std::toupper(std::uint8_t(c)));
            if (!made_ || want != code_) { error(peer, 2, "no such room"); break; }
            if (in) break;
            if (inGame_) { error(peer, 2, "the game has started"); break; }
            if (p.build != build_) { error(peer, 2, "that room runs a different build of the game"); break; }
            const auto free = std::find(slots_.begin(), slots_.end(), -1);
            if (free == slots_.end()) { error(peer, 2, "the room is full"); break; }
            p.slot = int(free - slots_.begin());
            p.ready = false;
            p.data.clear();
            *free = peer;
            broadcastState();
            break;
        }
        case Msg::LeaveRoom: remove(peer); break;
        case Msg::Chat: {
            if (!in || ++p.chats > 20) break;
            broadcast(Writer(Msg::ChatFrom).u8(std::uint8_t(p.slot)).str(firstChars(r.str(), 130)).take());
            break;
        }
        case Msg::SetReady:
            if (in && !inGame_) { p.ready = r.u8() != 0; broadcastState(); }
            break;
        case Msg::SetOptions:
            if (in && !inGame_ && host_ == p.slot) { options_ = r.bytes(); broadcastState(); }
            break;
        case Msg::SetPlayerData:
            if (in && !inGame_) { p.data = r.bytes(); broadcastState(); }
            break;
        case Msg::Start: {
            if (!in || inGame_ || host_ != p.slot) break;
            // A host alone may start too, as in the original (FUN_0041af20 counts only the ready).
            bool all = true;
            for (const int q : members()) all &= q == peer || peers_.at(q).ready;
            if (!all) { error(peer, 4, "not everyone is ready"); break; }
            start(nowMs);
            break;
        }
        case Msg::Input: {
            if (!in || !inGame_) break;
            if (++p.inputs > 120) { error(peer, 3, "too many inputs"); drop_(peer); return; }
            const std::uint32_t turn = r.u32();
            const std::size_t n = r.u8();
            if (n > kMaxCommands) { drop_(peer); return; }
            std::vector<Raw> cmds(n);
            for (auto& c : cmds) for (auto& b : c) b = r.u8();
            if (!r.ok()) { drop_(peer); return; }
            addInput(peer, turn, std::move(cmds));
            break;
        }
        case Msg::Check: {
            if (!in || !inGame_) break;
            const std::uint32_t turn = r.u32(), seed = r.u32(), rolls = r.u32(), hash = r.u32();
            const std::uint8_t refused = r.u8();
            if (r.ok()) addCheck(peer, turn, std::to_string(seed) + ":" + std::to_string(rolls) + ":" + std::to_string(hash), refused);
            break;
        }
        case Msg::Loaded:
            if (in && inGame_ && !running_) {
                loaded_.insert(p.slot);
                bool all = true;
                for (const int q : members()) all &= loaded_.count(peers_.at(q).slot) != 0;
                if (all) run(nowMs);
            }
            break;
        case Msg::GameOver: {
            if (!in || !inGame_) break;
            over_.insert(p.slot);
            bool all = true;
            for (const int q : members()) all &= over_.count(peers_.at(q).slot) != 0;
            if (all) { over_.clear(); stop(); broadcastState(); }
            break;
        }
        case Msg::Ping: send_(peer, Writer(Msg::Pong).u32(r.u32()).take()); break;
        case Msg::Pong: {
            // Our own ping back: the median of the last 9 round trips, and the
            // median distance from it as the jitter.
            const std::uint32_t sample = nowMs - r.u32();
            if (r.ok() && sample < 5000) {
                p.recent.push_back(sample);
                if (p.recent.size() > 9) p.recent.erase(p.recent.begin());
                auto sorted = p.recent;
                std::sort(sorted.begin(), sorted.end());
                p.rtt = sorted[sorted.size() / 2];
                std::vector<double> dev;
                for (const double x : p.recent) dev.push_back(std::abs(x - p.rtt));
                std::sort(dev.begin(), dev.end());
                p.jitter = dev[dev.size() / 2];
            }
            break;
        }
        default: break;
    }
}

} // namespace ch::net
