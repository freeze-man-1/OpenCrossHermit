// ============================================================================
// relay_core.hpp — the relay's room (tools/netserver/relay.mjs, class Room
// and handle()), for one room, run inside the room host's own game when
// there is no relay server (the Apinator transport: a hosted channel only
// forwards, so the host's client keeps the turn clock).
//
// Peers are numbered by the transport (0 is the host's own player). Every
// message in and out is the relay protocol of net_protocol.hpp; the only
// lobby message handled here is JoinRoom for this room's code (the transport
// answers ListRooms itself). Driven by tick(nowMs), once a frame: the turn
// clock closes turns on an absolute schedule, as the relay's timers do.
// Keep in step with relay.mjs.
// ============================================================================
#pragma once
#include "net_protocol.hpp"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ch::net {

class RelayRoom {
public:
    using Send = std::function<void(int peer, std::vector<std::uint8_t> message)>;
    // A peer the room asks the transport to drop (a tampered client).
    using Drop = std::function<void(int peer)>;

    RelayRoom(std::string code, int turnFrames, Send send, Drop drop)
        : code_(std::move(code)), turnFrames_(turnFrames), send_(std::move(send)), drop_(std::move(drop)) {}

    // The peer's messages, in order. Peer 0's CreateRoom makes the room.
    void handle(int peer, const std::vector<std::uint8_t>& m, std::uint32_t nowMs);
    void disconnect(int peer);                   // its link is gone
    void tick(std::uint32_t nowMs);

    // What the lobby's advertisement shows.
    [[nodiscard]] bool open() const noexcept { return made_; }
    [[nodiscard]] const std::string& code() const noexcept { return code_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] int players() const;
    [[nodiscard]] int max() const noexcept { return max_; }
    [[nodiscard]] bool inGame() const noexcept { return inGame_; }
    [[nodiscard]] bool isPublic() const noexcept { return public_; }
    [[nodiscard]] std::uint32_t build() const noexcept { return build_; }

private:
    struct Player {
        bool hello = false;
        std::string name;
        std::uint32_t build = 0;
        int slot = -1;
        bool ready = false;
        std::vector<std::uint8_t> data;
        double rtt = 100, jitter = 20;
        std::vector<double> recent;
        int inputs = 0, chats = 0, late = 0;
    };

    void error(int peer, int code, const std::string& text);
    void remove(int peer);
    void broadcastState();
    void broadcast(const std::vector<std::uint8_t>& m);
    [[nodiscard]] std::vector<std::uint8_t> stateFor(int peer) const;
    void start(std::uint32_t nowMs);
    void run(std::uint32_t nowMs);
    void stop();
    void closeTurn();
    [[nodiscard]] int computeDelay() const;
    void addInput(int peer, std::uint32_t turn, std::vector<std::array<std::uint8_t, kCommandBytes>> cmds);
    void addCheck(int peer, std::uint32_t turn, const std::string& key, std::uint8_t refused);
    [[nodiscard]] std::vector<int> members() const;   // the peers in slots, by slot

    std::string code_, name_;
    int turnFrames_ = 2;
    Send send_;
    Drop drop_;
    bool made_ = false, public_ = true;
    std::uint32_t build_ = 0;
    int max_ = 2, host_ = 0;
    std::vector<std::uint8_t> options_;
    std::map<int, Player> peers_;
    std::vector<int> slots_;                     // slot -> peer, -1 empty

    // The game.
    bool inGame_ = false, running_ = false;
    double turnMs_ = 33.3, t0_ = 0;
    std::uint32_t turn_ = 0;
    int delay_ = 2;
    std::uint32_t startedAt_ = 0, lastPing_ = 0, rateAt_ = 0;
    std::set<int> loaded_, over_;
    // turn -> slot -> commands
    std::map<std::uint32_t, std::map<int, std::vector<std::array<std::uint8_t, kCommandBytes>>>> pending_;
    struct CheckOf { std::string key; std::uint8_t refused = 0; };
    std::map<std::uint32_t, std::map<int, CheckOf>> checks_;
    std::uint32_t now_ = 0;
};

} // namespace ch::net
