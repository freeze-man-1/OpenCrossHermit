// ============================================================================
// net_transport.hpp — a message pipe to the relay (DirectPlay's place).
//
// One WebSocket connection. The browser build uses the browser's own
// WebSocket (emscripten/websocket.h, so wss:// and the page's origin rules
// apply); the native build a small RFC 6455 client over a non-blocking TCP
// socket (ws://, and wss:// through OpenSSL when built with it). Both are
// polled once a frame from the game loop: nothing here blocks or spawns
// threads.
//
// connect() also takes "apinator:<region>" (or "apinator:<ws url>|<key>|
// <secret or auth URL>"): no relay server at all, a hosted Apinator channel
// instead, with the room host's own game keeping the turn clock
// (net_apinator.cpp).
// ============================================================================
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ch::net {

class Transport {
public:
    enum class State { Connecting, Open, Closed };
    virtual ~Transport() = default;
    [[nodiscard]] virtual State state() const = 0;
    // Queues one message; dropped when the connection is not open.
    virtual void send(std::vector<std::uint8_t> message) = 0;
    // Moves the messages that have arrived into `out` (in order).
    virtual void poll(std::vector<std::vector<std::uint8_t>>& out) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual std::string error() const = 0;
};

// "ws://host:port/", "wss://..." or "apinator:...".
[[nodiscard]] std::unique_ptr<Transport> connect(const std::string& url);
// One WebSocket; with `text` its messages are text frames (UTF-8) both ways.
[[nodiscard]] std::unique_ptr<Transport> openSocket(const std::string& url, bool text);
// net_apinator.cpp
[[nodiscard]] std::unique_ptr<Transport> connectApinator(const std::string& spec);
// The Apinator regions this build was configured with (tools/netserver/
// apinator-<region>.env at CMake time): their labels and connect() URLs.
struct ServerChoice { std::string label, url; };
[[nodiscard]] std::vector<ServerChoice> builtInServers();

// A one-shot HTTP(S) POST of a JSON body (the Apinator auth endpoint): call
// poll() once a frame until done().
class HttpPost {
public:
    virtual ~HttpPost() = default;
    [[nodiscard]] virtual bool done() = 0;
    [[nodiscard]] virtual int status() const = 0;       // 0 when the request failed
    [[nodiscard]] virtual const std::string& body() const = 0;
};
[[nodiscard]] std::unique_ptr<HttpPost> httpPost(const std::string& url, const std::string& json);

} // namespace ch::net
