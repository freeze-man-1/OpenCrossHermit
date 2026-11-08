// ============================================================================
// net_session.hpp — one player's connection to the relay: the lobby state
// (the rooms, the room joined, its players and chat) and, once the host
// starts, the battle's Lockstep. The game polls it once a frame.
//
// It replaces the original's DirectPlay session (dxplay.cpp) and the session
// half of NetTask (nettask.cpp): creating a session became creating a room,
// "Now Search..." the room list, typing the host's address the room's
// invite code.
// ============================================================================
#pragma once
#include "lockstep.hpp"
#include "net_transport.hpp"

#include <array>
#include <memory>
#include <string>

namespace ch::net {

class Session {
public:
    enum class State { Offline, Connecting, Lobby, InRoom, InGame, Failed };
    struct RoomInfo { std::string code, name; int players = 0, max = 0; bool inGame = false; };
    struct Member { int slot = -1; std::string name; bool ready = false; int rttMs = 0; std::vector<std::uint8_t> data; };
    struct Room {
        std::string code, name;
        int host = -1, you = -1;
        std::vector<std::uint8_t> options;
        std::vector<Member> members;
    };
    struct ChatLine { int slot = -1; std::string text; std::uint64_t seq = 0; };   // seq: its place among all lines
    struct Alert { std::uint32_t turn = 0; std::uint8_t suspects = 0; std::string text; };

    // The build every player of a room must share (lockstep needs the same
    // simulation code; the relay refuses a room to other builds).
    [[nodiscard]] static std::uint32_t buildId();

    void connect(const std::string& url, const std::string& name);
    void disconnect();
    void poll(std::uint32_t nowMs);              // once a frame

    void listRooms() { send(Writer(Msg::ListRooms).take()); }
    void createRoom(const std::string& name, int maxPlayers, bool isPublic);
    void joinRoom(const std::string& code);
    void leaveRoom();
    void chat(const std::string& text);
    void setReady(bool ready);
    void setOptions(const std::vector<std::uint8_t>& options);
    void setPlayerData(const std::vector<std::uint8_t>& data);
    void startGame() { send(Writer(Msg::Start).take()); }
    void loaded() { send(Writer(Msg::Loaded).take()); }   // the battle can run
    // The battle is over here: the relay stops its clock once everyone says so.
    void gameOver() {
        send(Writer(Msg::GameOver).take());
        lockstep.stop();
        if (state_ == State::InGame) state_ = State::InRoom;
    }

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] const std::string& lastError() const noexcept { return error_; }
    // The relay's last refusal (1 protocol, 2 joining, 3 limits, 4 starting) and what it was about.
    [[nodiscard]] int lastErrorCode() const noexcept { return errorCode_; }
    [[nodiscard]] std::uint32_t errorCount() const noexcept { return errors_; }
    [[nodiscard]] const std::vector<RoomInfo>& rooms() const noexcept { return rooms_; }
    [[nodiscard]] const Room& room() const noexcept { return room_; }
    [[nodiscard]] bool isHost() const noexcept { return room_.you >= 0 && room_.you == room_.host; }
    [[nodiscard]] std::vector<ChatLine>& chatLog() noexcept { return chat_; }
    [[nodiscard]] std::uint64_t chatCount() const noexcept { return chatSeq_; }   // lines ever received
    [[nodiscard]] std::vector<Alert>& alerts() noexcept { return alerts_; }
    [[nodiscard]] int rttMs() const noexcept { return rtt_; }
    // The server's live counts (Stats): players connected and rooms open, -1 until told.
    [[nodiscard]] int playersOnline() const noexcept { return online_; }
    [[nodiscard]] int roomsOpen() const noexcept { return roomsOpen_; }
    // Set when the host started the game; the game clears it when it has
    // gone to the battle.
    bool startedPending = false;
    std::uint32_t seed = 0;
    int slots = 0;                               // the room's player slots
    std::vector<int> leftSlots;                  // players gone during the battle
    // Ours across the network screens (NetTask, scenes 0xe / 0xf): the name
    // (DAT_00809694), the roster we pick from and which we bring.
    std::string playerName;
    std::string server;                          // the server picked (NetTask; not the original's)
    std::vector<std::array<std::uint8_t, 0xb0>> roster;
    std::vector<std::uint8_t> picked;
    bool rosterFromSave = false;                 // +0x1a99
    Lockstep lockstep;

private:
    void send(std::vector<std::uint8_t> m) { if (link_) link_->send(std::move(m)); }
    void handle(const std::vector<std::uint8_t>& m, std::uint32_t nowMs);

    std::unique_ptr<Transport> link_;
    State state_ = State::Offline;
    std::string name_, error_;
    int errorCode_ = 0;
    std::uint32_t errors_ = 0;
    std::vector<RoomInfo> rooms_;
    Room room_;
    std::vector<ChatLine> chat_;
    std::uint64_t chatSeq_ = 0;
    std::vector<Alert> alerts_;
    std::uint32_t lastPing_ = 0;
    int rtt_ = 0;
    int online_ = -1, roomsOpen_ = -1;
    bool helloSent_ = false;
};

} // namespace ch::net
