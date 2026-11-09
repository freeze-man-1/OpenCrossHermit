// ============================================================================
// net_protocol.hpp — the relay protocol (client side; tools/netserver/relay.mjs
// is the server and must agree with this file).
//
// The original (yoshida\nettask.cpp, netmessage.cpp, DirectPlay 4) ran a
// strict per-frame lockstep between up to four peers: each frame every side
// broadcast message 4 (FUN_0048ceb0: its player number, the frame, the
// generator's state and roll count as check words, and the frame's 16-byte
// commands) and FUN_00456bc0 waited for all the others before stepping.
//
// Here the peers talk to a relay instead, which keeps the clock:
//   - The battle advances in turns of `turnFrames` frames (default 2, 33 ms).
//   - A client sends the commands it issues for turn T + inputDelay (INPUT).
//   - The relay closes each turn on its own clock and broadcasts TURN with
//     every player's commands for it; a player whose input is late does not
//     stall the others: their commands go into the next open turn.
//   - Clients run a turn's frames only once its TURN is in (lockstep), and
//     run extra frames to catch up when several are buffered.
//   - The relay adapts inputDelay to the room's measured round trip.
//   - Every `checkEvery` turns each client reports CHECK (the generator's
//     state, its roll count, a hash of the units, and which players sent
//     commands it had to refuse); the relay compares them and reports a
//     DESYNC, naming the odd one out when three or more players agree.
//   - Ping / Pong go both ways: each side measures its own round trip.
//
// Every message is one binary WebSocket frame: u8 type, then the fields,
// little-endian; strings are u8 length + UTF-8 bytes.
// ============================================================================
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ch::net {

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr int kMaxPlayers = 4;               // DirectPlay's session limit in the original
inline constexpr int kMaxCommandsPerTurn = 64;      // the relay drops a player sending more

enum class Msg : std::uint8_t {
    // client -> relay
    Hello = 1,        // u16 version, u32 build, str name
    ListRooms = 2,    // -
    CreateRoom = 3,   // str name, u8 maxPlayers, u8 isPublic
    JoinRoom = 4,     // str code
    LeaveRoom = 5,    // -
    Chat = 6,         // str text
    SetReady = 7,     // u8 ready
    SetOptions = 8,   // host: u16 len, bytes (the game's room settings: map, ...)
    Start = 9,        // host: -
    Input = 10,       // u32 turn, u8 n, n x Command
    Check = 11,       // u32 turn, u32 seed, u32 rolls, u32 hash, u8 refusedMask
    Ping = 12,        // u32 senderMs (either way; answered with Pong)
    SetPlayerData = 13,   // u16 len, bytes (the player's own picks: units, ...)
    Loaded = 14,      // - : the battle is ready to run; turns start once all have sent it
    GameOver = 15,    // - : our battle has ended; the room goes back to choosing
    // relay -> client
    Welcome = 101,    // u32 playerId
    RoomList = 102,   // u8 n, n x {str code, str name, u8 players, u8 max, u8 inGame}
    RoomState = 103,  // str code, str name, u8 host, u8 you, u16 len, options, u8 n,
                      //   n x {u8 slot, str name, u8 ready, u16 rttMs, u16 len, data}
    ChatFrom = 104,   // u8 slot, str text
    Started = 105,    // u32 seed, u8 turnFrames, u8 inputDelay, u8 checkEvery, u8 you, u8 n
    Turn = 106,       // u32 turn, u8 inputDelay, u8 n, n x {u8 slot, u8 count, count x Command}
    Pong = 107,       // u32 senderMs, echoed (either way)
    Error = 108,      // u8 code, str text
    Desync = 109,     // u32 turn, u8 suspectMask, str text
    PlayerLeft = 110, // u8 slot
    Stats = 111,      // u16 players online, u16 open rooms (0xffff: not known)
};

// One order, as the original's 16-byte command records (FUN_0048ceb0 copies
// them as they are): the type (TacticsUnits::Unit::Order), the unit's slot,
// the target slot, the cell and one extra word (a call list's next slot).
struct Command {
    std::uint8_t  type = 0;
    std::uint8_t  flags = 0;
    std::int16_t  unit = -1;
    std::int16_t  target = -1;
    std::int16_t  cx = 0, cy = 0;
    std::int16_t  extra = -1;
    std::uint32_t reserved = 0;
};
inline constexpr std::size_t kCommandBytes = 16;
// Command types the network layer sends itself (the others are unit orders).
inline constexpr std::uint8_t kCmdEventDone  = 0x42;   // a player's dialog is over (extra: its serial)
inline constexpr std::uint8_t kCmdPlayerLeft = 0x43;   // from the relay: this slot has gone

// UTF-8 helpers for the chat and names: count characters, not bytes, and
// never cut one in two.
[[nodiscard]] inline std::size_t utf8Length(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s) n += (std::uint8_t(c) & 0xc0) != 0x80;
    return n;
}
// Appends the characters of `typed` (control characters dropped) while the
// line holds fewer than `most`.
inline void appendUtf8(std::string& line, std::string_view typed, std::size_t most) {
    std::size_t n = utf8Length(line);
    for (std::size_t i = 0; i < typed.size();) {
        std::size_t len = 1;
        const auto b = std::uint8_t(typed[i]);
        if (b >= 0xf0) len = 4; else if (b >= 0xe0) len = 3; else if (b >= 0xc0) len = 2;
        if (i + len > typed.size()) break;
        if (b >= 0x20 && b != 0x7f && n < most) {
            line.append(typed.substr(i, len));
            ++n;
        }
        i += len;
    }
}
inline void popUtf8(std::string& line) {
    if (line.empty()) return;
    std::size_t at = line.size() - 1;
    while (at > 0 && (std::uint8_t(line[at]) & 0xc0) == 0x80) --at;
    line.erase(at);
}
// The longest prefix of at most `bytes` bytes that ends on a character.
[[nodiscard]] inline std::string_view utf8Prefix(std::string_view s, std::size_t bytes) {
    if (s.size() <= bytes) return s;
    std::size_t at = bytes;
    while (at > 0 && (std::uint8_t(s[at]) & 0xc0) == 0x80) --at;
    return s.substr(0, at);
}

class Writer {
public:
    explicit Writer(Msg type) { u8(std::uint8_t(type)); }
    Writer& u8(std::uint8_t v) { b_.push_back(v); return *this; }
    Writer& u16(std::uint16_t v) { u8(std::uint8_t(v)); return u8(std::uint8_t(v >> 8)); }
    Writer& u32(std::uint32_t v) { u16(std::uint16_t(v)); return u16(std::uint16_t(v >> 16)); }
    Writer& i16(std::int16_t v) { return u16(std::uint16_t(v)); }
    Writer& str(std::string_view s) {
        const std::string_view t = utf8Prefix(s, 255);
        u8(std::uint8_t(t.size()));
        b_.insert(b_.end(), t.begin(), t.end());
        return *this;
    }
    Writer& bytes(const std::vector<std::uint8_t>& v) {
        u16(std::uint16_t(v.size()));
        b_.insert(b_.end(), v.begin(), v.end());
        return *this;
    }
    Writer& command(const Command& c) {
        u8(c.type).u8(c.flags).i16(c.unit).i16(c.target).i16(c.cx).i16(c.cy).i16(c.extra);
        return u32(c.reserved);
    }
    [[nodiscard]] std::vector<std::uint8_t> take() { return std::move(b_); }

private:
    std::vector<std::uint8_t> b_;
};

// Reads fail soft: past the end every read gives 0 and ok() turns false.
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& b) : b_(b) {}
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] bool done() const noexcept { return at_ >= b_.size(); }
    std::uint8_t u8() {
        if (at_ >= b_.size()) { ok_ = false; return 0; }
        return b_[at_++];
    }
    std::uint16_t u16() { const std::uint16_t lo = u8(); return std::uint16_t(lo | u8() << 8); }
    std::uint32_t u32() { const std::uint32_t lo = u16(); return lo | std::uint32_t(u16()) << 16; }
    std::int16_t i16() { return std::int16_t(u16()); }
    std::string str() {
        const std::size_t n = u8();
        if (at_ + n > b_.size()) { ok_ = false; return {}; }
        std::string s(b_.begin() + std::ptrdiff_t(at_), b_.begin() + std::ptrdiff_t(at_ + n));
        at_ += n;
        return s;
    }
    std::vector<std::uint8_t> bytes() {
        const std::size_t n = u16();
        if (at_ + n > b_.size()) { ok_ = false; return {}; }
        std::vector<std::uint8_t> v(b_.begin() + std::ptrdiff_t(at_), b_.begin() + std::ptrdiff_t(at_ + n));
        at_ += n;
        return v;
    }
    Command command() {
        Command c;
        c.type = u8();
        c.flags = u8();
        c.unit = i16();
        c.target = i16();
        c.cx = i16();
        c.cy = i16();
        c.extra = i16();
        c.reserved = u32();
        return c;
    }

private:
    const std::vector<std::uint8_t>& b_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

} // namespace ch::net
