// ============================================================================
// net_session.cpp — Session: the relay's messages for the lobby and battle
// ============================================================================
#include "net_session.hpp"

namespace ch::net {

// The room's DirectPlay messages (netmessage.cpp, message type at +0xd)
// and what does their work here:
//   FUN_00443400 (the host's) / FUN_00443780 (a guest's) take every message
//     in (from FUN_00448cc0 / FUN_00448e50, the room's pumps): handle()
//     below, polled once a frame. DirectPlay's own player joins and leaves
//     (system messages 3 and 5) are the relay's RoomState.
//   2 the player list: FUN_00442b30 sends it, FUN_00442ce0 takes it -> RoomState.
//   3 ready: FUN_00441de0 (a guest) / FUN_00441ea0 (the host's side);
//     9 not ready: FUN_004427d0 / FUN_004428b0; 0xc the host's own mark
//     (FUN_00442db0) -> SetReady and the members' ready flags (FUN_00441b90
//     asks ours). 3 with 1 from the host (FUN_00441f40) begins -> Start.
//   5 the map: FUN_00441fe0 -> SetOptions; 7 the teams: FUN_004429a0 /
//     FUN_00442a50 -> the same options.
//   6 the units: a guest's to the host (FUN_00442580), then the host's
//     merge (FUN_00441c10, each player's up to the map's limit) back to
//     everyone with the teams, start cells and the seed (FUN_00442360 /
//     FUN_00442690) -> SetPlayerData, Started and net_game's writeBattleWork.
//   0xd a player's count and total level (FUN_00443080) -> their player data.
//   0xe a guest back from a battle (FUN_00442eb0), the host sending all
//     again (FUN_00442e50) -> the relay's RoomState after GameOver.
//   0xb chat (FUN_004431a0 / FUN_004432f0) -> Chat / ChatFrom.

std::uint32_t Session::buildId() {
    // FNV-1a of the compile time: two copies of one build agree, anything
    // rebuilt does not (its simulation may differ).
    static constexpr char kStamp[] = __DATE__ " " __TIME__;
    std::uint32_t h = 0x811c9dc5u;
    for (const char c : kStamp) h = (h ^ std::uint8_t(c)) * 0x01000193u;
    return h;
}

void Session::connect(const std::string& url, const std::string& name) {
    disconnect();
    name_ = name;
    error_.clear();
    link_ = net::connect(url);
    state_ = link_->state() == Transport::State::Closed ? State::Failed : State::Connecting;
    if (state_ == State::Failed) error_ = link_->error();
    helloSent_ = false;
    // The hello goes out as soon as the socket is open (both transports
    // hold messages sent while connecting).
    send(Writer(Msg::Hello).u16(kProtocolVersion).u32(buildId()).str(name_).take());
    helloSent_ = true;
}

void Session::disconnect() {
    if (link_) link_->close();
    link_.reset();
    lockstep.stop();
    state_ = State::Offline;
    room_ = {};
    online_ = roomsOpen_ = -1;
}

void Session::createRoom(const std::string& name, int maxPlayers, bool isPublic) {
    send(Writer(Msg::CreateRoom).str(name).u8(std::uint8_t(maxPlayers)).u8(isPublic ? 1 : 0).take());
}
void Session::joinRoom(const std::string& code) { send(Writer(Msg::JoinRoom).str(code).take()); }
void Session::leaveRoom() {
    send(Writer(Msg::LeaveRoom).take());
    room_ = {};
    lockstep.stop();
    if (state_ == State::InRoom || state_ == State::InGame) state_ = State::Lobby;
}
void Session::chat(const std::string& text) { send(Writer(Msg::Chat).str(text).take()); }
void Session::setReady(bool ready) { send(Writer(Msg::SetReady).u8(ready ? 1 : 0).take()); }
void Session::setOptions(const std::vector<std::uint8_t>& o) { send(Writer(Msg::SetOptions).bytes(o).take()); }
void Session::setPlayerData(const std::vector<std::uint8_t>& d) { send(Writer(Msg::SetPlayerData).bytes(d).take()); }

void Session::poll(std::uint32_t nowMs) {
    if (!link_) return;
    std::vector<std::vector<std::uint8_t>> in;
    link_->poll(in);
    for (const auto& m : in) handle(m, nowMs);
    if (link_->state() == Transport::State::Closed && state_ != State::Failed && state_ != State::Offline) {
        error_ = link_->error().empty() ? "disconnected" : link_->error();
        state_ = State::Failed;
        lockstep.stop();
    }
    // Our own round trip, once a second.
    if (link_->state() == Transport::State::Open && nowMs - lastPing_ >= 1000) {
        lastPing_ = nowMs;
        send(Writer(Msg::Ping).u32(nowMs).take());
    }
    lockstep.flush();
}

void Session::handle(const std::vector<std::uint8_t>& m, std::uint32_t nowMs) {
    Reader r(m);
    switch (Msg(r.u8())) {
        case Msg::Welcome:
            (void)r.u32();
            state_ = State::Lobby;
            break;
        case Msg::RoomList: {
            rooms_.clear();
            const int n = r.u8();
            for (int i = 0; i < n && r.ok(); ++i) {
                RoomInfo ri;
                ri.code = r.str();
                ri.name = r.str();
                ri.players = r.u8();
                ri.max = r.u8();
                ri.inGame = r.u8() != 0;
                rooms_.push_back(std::move(ri));
            }
            break;
        }
        case Msg::RoomState: {
            Room rm;
            rm.code = r.str();
            rm.name = r.str();
            rm.host = r.u8();
            rm.you = r.u8();
            rm.options = r.bytes();
            const int n = r.u8();
            for (int i = 0; i < n && r.ok(); ++i) {
                Member mb;
                mb.slot = r.u8();
                mb.name = r.str();
                mb.ready = r.u8() != 0;
                mb.rttMs = r.u16();
                mb.data = r.bytes();
                rm.members.push_back(std::move(mb));
            }
            if (!r.ok()) break;
            room_ = std::move(rm);
            if (state_ != State::InGame) state_ = State::InRoom;
            break;
        }
        case Msg::ChatFrom: {
            ChatLine c;
            c.slot = r.u8();
            c.text = r.str();
            c.seq = chatSeq_++;
            chat_.push_back(std::move(c));
            if (chat_.size() > 64) chat_.erase(chat_.begin());
            break;
        }
        case Msg::Started: {
            seed = r.u32();
            const int turnFrames = r.u8(), delay = r.u8(), checkEvery = r.u8(), you = r.u8();
            slots = r.u8();
            if (!r.ok()) break;
            room_.you = you;
            leftSlots.clear();
            lockstep.start(turnFrames, delay, checkEvery, you, [this](std::vector<std::uint8_t> b) { send(std::move(b)); });
            state_ = State::InGame;
            startedPending = true;
            break;
        }
        case Msg::Turn: {
            Lockstep::Turn t;
            int delay = 0;
            if (readTurn(r, t, delay) && lockstep.running()) lockstep.onTurn(std::move(t), delay);
            break;
        }
        case Msg::Ping:
            send(Writer(Msg::Pong).u32(r.u32()).take());
            break;
        case Msg::Pong: {
            const std::uint32_t sent = r.u32();
            if (r.ok()) rtt_ = int(nowMs - sent);
            break;
        }
        case Msg::Error:
            errorCode_ = r.u8();
            error_ = r.str();
            ++errors_;
            break;
        case Msg::Desync: {
            Alert a;
            a.turn = r.u32();
            a.suspects = r.u8();
            a.text = r.str();
            alerts_.push_back(std::move(a));
            break;
        }
        case Msg::PlayerLeft:
            leftSlots.push_back(r.u8());
            break;
        case Msg::Stats: {
            const int online = r.u16(), rooms = r.u16();
            if (r.ok()) { online_ = online; roomsOpen_ = rooms == 0xffff ? -1 : rooms; }
            break;
        }
        default:
            break;
    }
}

} // namespace ch::net
