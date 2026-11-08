// ============================================================================
// net_apinator.cpp — net::Transport over Apinator (apinator.io), a hosted
// Pusher-style WebSocket service: no relay server to run.
//
// Apinator only forwards: a client event on a private or presence channel
// goes to the channel's other subscribers. So the relay's work moves into the
// room host's game (RelayRoom, relay_core.cpp), and this transport stands in
// for the relay towards the Session above it:
//   - Lobby: Hello, Ping and ListRooms are answered here. Hosts advertise
//     their room on private-ch-lobby every 3 s ("client-ad"; a lister's
//     "client-who" asks them at once); ads not heard for 7 s are dropped.
//   - CreateRoom: a six-character code, the presence channel
//     presence-ch-room-<code> (our user_info says we host), and a RelayRoom
//     with our own Session as its peer 0.
//   - JoinRoom: that channel; its member list names the host (none: "no
//     such room"). Our Hello and JoinRoom then go to the host's RelayRoom.
//   - Every relay message between a guest and the host is carried in
//     "client-m" events, bundled every kFlushMs per sender:
//       {"f": sender id, "p": [{"t": recipient id or "h", "m": base64}]}
//     where "m" is the messages each prefixed with a u16 length. Members
//     are identified by their socket id (the presence user_id, which the
//     channel signature binds to the connection).
//   - The live counts (Stats): everyone connected is a member of the
//     presence channel presence-ch-online, so its member list is the number
//     of players online (no secret needed, unlike Apinator's REST stats);
//     the open rooms are the lobby's ads, while we listen to them.
//   - A guest leaving the channel is a player leaving the room; the host
//     leaving ends the room for everyone (the connection reports it).
// Signatures: "<key>:" + hex HMAC-SHA256(secret, "<socket_id>:<channel>
// [:<channel_data>]"), made here when the build carries the secret, else by
// an auth endpoint (tools/netserver/apinator-auth.mjs) POSTed
// {socket_id, channel_name, channel_data} and answering {auth, channel_data}.
// ============================================================================
#include "net_crypto.hpp"
#include "net_json.hpp"
#include "net_protocol.hpp"
#include "net_transport.hpp"
#include "relay_core.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <random>
#include <set>

#if __has_include("apinator_config.gen.hpp")
#include "apinator_config.gen.hpp"   // CMake: from tools/netserver/apinator-*.env
#else
namespace ch::net::apinator_config {
struct Region { const char* region; const char* label; const char* url; const char* key; const char* secret; const char* auth; };
inline constexpr Region kRegions[1] = {{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};
inline constexpr int kRegionCount = 0;
}
#endif

namespace ch::net {

namespace {

constexpr std::uint32_t kFlushMs = 50;        // bundling window per sender
constexpr std::uint32_t kAdMs = 3000, kAdLifeMs = 7000;
constexpr const char* kLobby = "private-ch-lobby";
constexpr const char* kOnline = "presence-ch-online";
constexpr const char* kCodeChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no 0/O, 1/I

std::uint32_t nowMs() {
    using namespace std::chrono;
    return std::uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

class ApinatorTransport final : public Transport {
public:
    ApinatorTransport(std::string url, std::string key, std::string secret, std::string auth)
        : key_(std::move(key)), secret_(std::move(secret)), auth_(std::move(auth)) {
        if (key_.empty()) { fail("this server is not set up in this build"); return; }
        ws_ = openSocket(url + (url.ends_with('/') ? "" : "/") + "app/" + key_, true);
        if (ws_->state() == State::Closed) fail(ws_->error());
    }
    ~ApinatorTransport() override { close(); }

    State state() const override { return state_; }
    std::string error() const override { return error_; }

    void send(std::vector<std::uint8_t> m) override {
        if (state_ == State::Closed || m.empty()) return;
        if (socketId_.empty()) { early_.push_back(std::move(m)); return; }
        fromSession(m);
    }

    void poll(std::vector<std::vector<std::uint8_t>>& out) override {
        if (state_ != State::Closed && ws_) {
            std::vector<std::vector<std::uint8_t>> in;
            ws_->poll(in);
            for (const auto& f : in) frame(std::string(f.begin(), f.end()));
            if (ws_->state() == State::Closed && state_ != State::Closed) fail(ws_->error().empty() ? "connection lost" : ws_->error());
        }
        if (state_ != State::Closed) tick(nowMs());
        for (auto& m : toSession_) out.push_back(std::move(m));
        toSession_.clear();
    }

    void close() override {
        if (state_ == State::Closed) return;
        if (mode_ == Mode::Guest || mode_ == Mode::Host) flush(true);
        if (ws_) ws_->close();
        state_ = State::Closed;
    }

private:
    enum class Mode { Lobby, Creating, Joining, Guest, Host };

    void fail(std::string why) {
        if (error_.empty()) error_ = std::move(why);
        state_ = State::Closed;
    }
    void toSession(std::vector<std::uint8_t> m) { toSession_.push_back(std::move(m)); }
    void sessionError(int code, const std::string& text) { toSession(Writer(Msg::Error).u8(std::uint8_t(code)).str(text).take()); }

    // ------------------------------------------------------ the wire ---
    void raw(const std::string& event, const std::string& channel, const std::string& data) {
        std::string f = "{\"event\":" + json::quote(event);
        if (!channel.empty()) f += ",\"channel\":" + json::quote(channel);
        f += ",\"data\":" + json::quote(data) + "}";
        ws_->send(std::vector<std::uint8_t>(f.begin(), f.end()));
        lastSent_ = nowMs();
    }
    void subscribe(const std::string& channel, const std::string& channelData) {
        if (!secret_.empty()) {
            std::string sign = socketId_ + ":" + channel;
            if (!channelData.empty()) sign += ":" + channelData;
            sendSubscribe(channel, key_ + ":" + crypto::hmacSha256Hex(secret_, sign), channelData);
            return;
        }
        std::string body = "{\"socket_id\":" + json::quote(socketId_) + ",\"channel_name\":" + json::quote(channel);
        if (!channelData.empty()) body += ",\"channel_data\":" + json::quote(channelData);
        authing_.push_back({channel, channelData, httpPost(auth_, body + "}")});
    }
    void sendSubscribe(const std::string& channel, const std::string& auth, const std::string& channelData) {
        std::string d = "{\"channel\":" + json::quote(channel) + ",\"auth\":" + json::quote(auth);
        if (!channelData.empty()) d += ",\"channel_data\":" + json::quote(channelData);
        raw("realtime:subscribe", "", d + "}");
    }
    void unsubscribe(const std::string& channel) {
        raw("realtime:unsubscribe", "", "{\"channel\":" + json::quote(channel) + "}");
    }
    [[nodiscard]] std::string presenceData(bool host) const {
        return "{\"user_id\":" + json::quote(socketId_) + ",\"user_info\":{\"n\":" + json::quote(name_) +
               ",\"h\":" + (host ? "true" : "false") + "}}";
    }

    // One frame from Apinator.
    void frame(const std::string& text) {
        json::Value f, d;
        if (!json::parse(text, f) || f.kind != json::Value::Kind::Object) return;
        const std::string event = f.text("event"), channel = f.text("channel");
        if (const json::Value* data = f.get("data")) {
            if (data->kind == json::Value::Kind::String) (void)json::parse(data->str, d);
            else d = *data;
        }
        if (event == "realtime:connection_established") {
            socketId_ = d.text("socket_id");
            activityMs_ = std::uint32_t(d.number("activity_timeout", 120)) * 1000;
            state_ = State::Open;
            auto early = std::move(early_);
            for (const auto& m : early) fromSession(m);
            subscribe(kOnline, presenceData(false));   // after the Hello: it names us
        } else if (event == "realtime:ping") {
            raw("realtime:pong", "", "{}");
        } else if (event == "realtime:error") {
            const std::string why = d.text("message");
            if (mode_ == Mode::Lobby) sessionError(3, why.empty() ? "server error" : why);
        } else if (event == "realtime:subscription_succeeded") {
            subscribed(channel, d);
        } else if (event == "realtime:subscription_error") {
            const std::string why = "channel refused: " + d.text("error");
            if (channel == room_ && (mode_ == Mode::Creating || mode_ == Mode::Joining)) {
                sessionError(mode_ == Mode::Creating ? 3 : 2, why);
                mode_ = Mode::Lobby;
                room_.clear();
            } else if (channel == kLobby) {
                lobby_ = false;
                sessionError(3, why);
            }
        } else if (event == "realtime:member_added" && channel == kOnline) {
            online_.insert(d.text("user_id"));
            stats();
        } else if (event == "realtime:member_removed" && channel == kOnline) {
            online_.erase(d.text("user_id"));
            stats();
        } else if (event == "realtime:member_added") {
            if (channel == room_) members_[d.text("user_id")] = d.get("user_info") && d.get("user_info")->flag("h");
        } else if (event == "realtime:member_removed") {
            if (channel == room_) memberGone(d.text("user_id"));
        } else if (event == "client-ad" && channel == kLobby) {
            heardAd(d);
        } else if (event == "client-who" && channel == kLobby) {
            if (mode_ == Mode::Host) advertise();
        } else if (event == "client-m" && channel == room_) {
            carried(d);
        }
    }

    void subscribed(const std::string& channel, const json::Value& d) {
        if (channel == kOnline) {
            online_.clear();
            if (const json::Value* p = d.get("presence"); p && p->get("hash"))
                for (const auto& [id, info] : p->get("hash")->fields) online_.insert(id);
            online_.insert(socketId_);
            stats();
            return;
        }
        if (channel == kLobby) {
            lobby_ = true;
            stats();
            raw("client-who", kLobby, "{}");
            if (mode_ == Mode::Host) advertise();
            return;
        }
        if (channel != room_) return;
        members_.clear();
        if (const json::Value* p = d.get("presence"); p && p->get("hash")) {
            for (const auto& [id, info] : p->get("hash")->fields) members_[id] = info.flag("h");
        }
        if (mode_ == Mode::Creating) {
            if (members_.size() > 1) {   // the code is taken: another one
                unsubscribe(room_);
                createRoom();
                return;
            }
            mode_ = Mode::Host;
            relay_ = std::make_unique<RelayRoom>(code_, 2,
                [this](int peer, std::vector<std::uint8_t> m) { fromRelay(peer, std::move(m)); },
                [this](int peer) { kick(peer); });
            relay_->handle(0, hello_, nowMs());
            relay_->handle(0, create_, nowMs());
            ensureLobby();
            advertise();
        } else if (mode_ == Mode::Joining) {
            hostId_.clear();
            for (const auto& [id, host] : members_) if (host && id != socketId_) hostId_ = id;
            if (hostId_.empty()) {
                unsubscribe(room_);
                room_.clear();
                mode_ = Mode::Lobby;
                sessionError(2, "no such room");
                return;
            }
            mode_ = Mode::Guest;
            if (lobby_) { unsubscribe(kLobby); lobby_ = false; listing_ = false; }
            queue("h", hello_);
            queue("h", Writer(Msg::JoinRoom).str(code_).take());
            flush(true);
        }
    }

    void memberGone(const std::string& id) {
        members_.erase(id);
        if (mode_ == Mode::Host) {
            if (const auto it = peerOf_.find(id); it != peerOf_.end()) {
                relay_->disconnect(it->second);
                idOf_.erase(it->second);
                peerOf_.erase(it);
            }
        } else if (mode_ == Mode::Guest && id == hostId_) {
            fail("the host has left the room");
        }
    }

    // A "client-m" bundle.
    void carried(const json::Value& d) {
        const std::string from = d.text("f");
        const json::Value* parts = d.get("p");
        if (from.empty() || !parts || parts->kind != json::Value::Kind::Array) return;
        if (mode_ == Mode::Host && !members_.count(from)) return;   // not in the channel
        if (mode_ == Mode::Guest && from != hostId_) return;        // only the host speaks to guests
        for (const json::Value& part : parts->items) {
            const std::string to = part.text("t");
            std::vector<std::uint8_t> bytes;
            if (!crypto::unbase64(part.text("m"), bytes)) continue;
            if (mode_ == Mode::Host ? to != "h" : to != socketId_) continue;
            for (std::size_t at = 0; at + 2 <= bytes.size();) {
                const std::size_t n = std::size_t(bytes[at]) | std::size_t(bytes[at + 1]) << 8;
                at += 2;
                if (at + n > bytes.size() || n == 0 || n > 4096) break;
                std::vector<std::uint8_t> m(bytes.begin() + std::ptrdiff_t(at), bytes.begin() + std::ptrdiff_t(at + n));
                at += n;
                if (mode_ == Mode::Host) {
                    if (banned_.count(from)) continue;
                    auto [it, fresh] = peerOf_.try_emplace(from, nextPeer_);
                    if (fresh) idOf_[nextPeer_++] = from;
                    relay_->handle(it->second, m, nowMs());
                } else if (Msg(m[0]) != Msg::Welcome) {   // the Session had ours already
                    toSession(std::move(m));
                }
            }
        }
    }

    // ------------------------------------------------- the Session's ---
    void fromSession(const std::vector<std::uint8_t>& m) {
        const auto type = Msg(m[0]);
        if (mode_ == Mode::Host) {
            if (type == Msg::LeaveRoom) { leaveRoom(); return; }
            if (type == Msg::ListRooms) return;
            relay_->handle(0, m, nowMs());
            return;
        }
        if (mode_ == Mode::Guest) {
            if (type == Msg::ListRooms) return;
            queue("h", m);
            if (type == Msg::LeaveRoom) leaveRoom();
            return;
        }
        // The lobby (and a room being made or joined).
        switch (type) {
            case Msg::Hello: {
                hello_ = m;
                Reader r(m);
                (void)r.u8();
                (void)r.u16();
                (void)r.u32();
                name_ = r.str();
                toSession(Writer(Msg::Welcome).u32(1).take());
                break;
            }
            case Msg::Ping: {
                Reader r(m);
                (void)r.u8();
                toSession(Writer(Msg::Pong).u32(r.u32()).take());
                break;
            }
            case Msg::ListRooms:
                listing_ = true;
                if (lobby_) raw("client-who", kLobby, "{}");
                ensureLobby();
                roomList();
                break;
            case Msg::CreateRoom:
                if (mode_ != Mode::Lobby) break;
                create_ = m;
                createRoom();
                break;
            case Msg::JoinRoom: {
                if (mode_ != Mode::Lobby) break;
                Reader r(m);
                (void)r.u8();
                code_.clear();
                for (const char c : r.str()) if (c != ' ') code_ += char(std::toupper(std::uint8_t(c)));
                bool ok = code_.size() == 6;
                for (const char c : code_) ok &= std::char_traits<char>::find(kCodeChars, 32, c) != nullptr;
                if (!ok) { sessionError(2, "no such room"); break; }
                mode_ = Mode::Joining;
                room_ = "presence-ch-room-" + code_;
                subscribe(room_, presenceData(false));
                break;
            }
            default: break;   // nothing else outside a room
        }
    }

    void createRoom() {
        std::random_device rd;
        code_.clear();
        for (int i = 0; i < 6; ++i) code_ += kCodeChars[rd() % 32];
        mode_ = Mode::Creating;
        room_ = "presence-ch-room-" + code_;
        subscribe(room_, presenceData(true));
    }

    void leaveRoom() {
        flush(true);
        if (!room_.empty()) unsubscribe(room_);
        if (mode_ == Mode::Host && lobby_ && !listing_) { unsubscribe(kLobby); lobby_ = false; }
        relay_.reset();
        peerOf_.clear();
        idOf_.clear();
        banned_.clear();
        members_.clear();
        room_.clear();
        hostId_.clear();
        mode_ = Mode::Lobby;
    }

    // The host's RelayRoom speaking to a peer.
    void fromRelay(int peer, std::vector<std::uint8_t> m) {
        if (peer == 0) {
            if (Msg(m[0]) != Msg::Welcome) toSession(std::move(m));
            return;
        }
        if (const auto it = idOf_.find(peer); it != idOf_.end()) queue(it->second, m);
    }
    void kick(int peer) {
        if (peer == 0) return;
        const auto it = idOf_.find(peer);
        if (it == idOf_.end()) return;
        queue(it->second, Writer(Msg::Error).u8(3).str("dropped by the host").take());
        banned_.insert(it->second);
        relay_->disconnect(peer);
        peerOf_.erase(it->second);
        idOf_.erase(it);
    }

    // ------------------------------------------------------ bundling ---
    void queue(const std::string& to, const std::vector<std::uint8_t>& m) {
        auto& b = outbox_[to];
        b.push_back(std::uint8_t(m.size()));
        b.push_back(std::uint8_t(m.size() >> 8));
        b.insert(b.end(), m.begin(), m.end());
    }
    void flush(bool now) {
        if (outbox_.empty() || room_.empty() || (!now && nowMs() - lastFlush_ < kFlushMs)) return;
        lastFlush_ = nowMs();
        std::string d = "{\"f\":" + json::quote(socketId_) + ",\"p\":[";
        bool first = true;
        for (const auto& [to, bytes] : outbox_) {
            if (!first) d += ',';
            first = false;
            d += "{\"t\":" + json::quote(to) + ",\"m\":\"" + crypto::base64(bytes.data(), bytes.size()) + "\"}";
        }
        outbox_.clear();
        raw("client-m", room_, d + "]}");
    }

    // ---------------------------------------------------------- lobby ---
    void ensureLobby() {
        if (lobby_ || lobbyAsked_) return;
        lobbyAsked_ = true;
        subscribe(kLobby, "");
    }
    void advertise() {
        if (!relay_ || !relay_->open() || !relay_->isPublic() || !lobby_) return;
        lastAd_ = nowMs();
        raw("client-ad", kLobby,
            "{\"c\":" + json::quote(relay_->code()) + ",\"n\":" + json::quote(relay_->name()) +
            ",\"p\":" + std::to_string(relay_->players()) + ",\"m\":" + std::to_string(relay_->max()) +
            ",\"g\":" + (relay_->inGame() ? "1" : "0") + ",\"b\":" + std::to_string(relay_->build()) + "}");
    }
    void heardAd(const json::Value& d) {
        Ad a;
        a.code = d.text("c");
        a.name = d.text("n");
        a.players = int(d.number("p"));
        a.max = int(d.number("m"));
        a.inGame = d.flag("g");
        a.heard = nowMs();
        if (a.code.size() != 6) return;
        const bool fresh = !ads_.count(a.code);
        ads_[a.code] = a;
        if (fresh) stats();
        if (listing_ && mode_ == Mode::Lobby) roomList();
    }
    // The counts, to the Session when they change.
    void stats() {
        const int online = int(online_.size()), rooms = lobby_ ? int(ads_.size()) : 0xffff;
        if (online == sentOnline_ && rooms == sentRooms_) return;
        sentOnline_ = online;
        sentRooms_ = rooms;
        toSession(Writer(Msg::Stats).u16(std::uint16_t(std::min(online, 0xfffe))).u16(std::uint16_t(rooms)).take());
    }
    void roomList() {
        Writer w(Msg::RoomList);
        std::vector<const Ad*> list;
        for (const auto& [code, a] : ads_) if (list.size() < 50) list.push_back(&a);
        w.u8(std::uint8_t(list.size()));
        for (const Ad* a : list)
            w.str(a->code).str(a->name).u8(std::uint8_t(a->players)).u8(std::uint8_t(a->max)).u8(a->inGame ? 1 : 0);
        toSession(w.take());
    }

    void tick(std::uint32_t now) {
        // Signatures from the auth endpoint.
        for (auto it = authing_.begin(); it != authing_.end();) {
            if (!it->post->done()) { ++it; continue; }
            json::Value r;
            if (it->post->status() == 200 && json::parse(it->post->body(), r) && !r.text("auth").empty()) {
                const std::string cd = r.text("channel_data").empty() ? it->channelData : r.text("channel_data");
                sendSubscribe(it->channel, r.text("auth"), cd);
            } else {
                const bool lobby = it->channel == kLobby;
                if (lobby) lobbyAsked_ = false;
                if (it->channel == room_ && mode_ != Mode::Host && mode_ != Mode::Guest) { mode_ = Mode::Lobby; room_.clear(); }
                sessionError(lobby ? 3 : 2, "the server's sign-in failed (" + std::to_string(it->post->status()) + ")");
            }
            it = authing_.erase(it);
        }
        if (relay_) relay_->tick(now);
        if (mode_ == Mode::Host && now - lastAd_ >= kAdMs) advertise();
        bool expired = false;
        for (auto it = ads_.begin(); it != ads_.end();) {
            if (now - it->second.heard > kAdLifeMs) { it = ads_.erase(it); expired = true; }
            else ++it;
        }
        if (expired) stats();
        if (expired && listing_ && mode_ == Mode::Lobby) roomList();
        flush(false);
        if (state_ == State::Open && activityMs_ && now - lastSent_ > activityMs_ / 2) raw("realtime:ping", "", "{}");
    }

    struct Ad { std::string code, name; int players = 0, max = 0; bool inGame = false; std::uint32_t heard = 0; };
    struct Authing { std::string channel, channelData; std::unique_ptr<HttpPost> post; };

    std::string key_, secret_, auth_;
    std::unique_ptr<Transport> ws_;
    State state_ = State::Connecting;
    std::string error_, socketId_, name_;
    std::uint32_t activityMs_ = 0, lastSent_ = 0, lastFlush_ = 0, lastAd_ = 0;
    std::vector<std::vector<std::uint8_t>> early_, toSession_;
    std::vector<std::uint8_t> hello_, create_;
    std::deque<Authing> authing_;

    Mode mode_ = Mode::Lobby;
    std::string room_, code_, hostId_;
    std::map<std::string, bool> members_;        // socket id -> hosts
    std::unique_ptr<RelayRoom> relay_;
    std::map<std::string, int> peerOf_;
    std::map<int, std::string> idOf_;
    std::set<std::string> banned_;
    int nextPeer_ = 1;
    std::map<std::string, std::vector<std::uint8_t>> outbox_;

    bool lobby_ = false, lobbyAsked_ = false, listing_ = false;
    std::set<std::string> online_;
    int sentOnline_ = -1, sentRooms_ = -1;
    std::map<std::string, Ad> ads_;
};

} // namespace

std::vector<ServerChoice> builtInServers() {
    std::vector<ServerChoice> v;
    for (int i = 0; i < apinator_config::kRegionCount; ++i)
        v.push_back({apinator_config::kRegions[i].label, std::string("apinator:") + apinator_config::kRegions[i].region});
    return v;
}

// "us" / "eu" (a configured region), or "<ws url>|<key>|<secret or http(s) auth URL>".
std::unique_ptr<Transport> connectApinator(const std::string& spec) {
    if (const std::size_t bar = spec.find('|'); bar != std::string::npos) {
        const std::size_t bar2 = spec.find('|', bar + 1);
        const std::string url = spec.substr(0, bar), key = spec.substr(bar + 1, bar2 == std::string::npos ? std::string::npos : bar2 - bar - 1);
        const std::string third = bar2 == std::string::npos ? std::string() : spec.substr(bar2 + 1);
        const bool isUrl = third.rfind("http://", 0) == 0 || third.rfind("https://", 0) == 0;
        return std::make_unique<ApinatorTransport>(url, key, isUrl ? std::string() : third, isUrl ? third : std::string());
    }
    for (int i = 0; i < apinator_config::kRegionCount; ++i) {
        const auto& r = apinator_config::kRegions[i];
        if (spec == r.region) return std::make_unique<ApinatorTransport>(r.url, r.key, r.secret, r.auth);
    }
    return std::make_unique<ApinatorTransport>("", "", "", "");
}

} // namespace ch::net
