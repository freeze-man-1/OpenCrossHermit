// ============================================================================
// net_transport.cpp — the WebSocket clients behind net::Transport
// ============================================================================
#include "net_transport.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <random>

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#include <emscripten/websocket.h>
#else
#include <cerrno>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#ifdef CH_HAVE_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif
#endif

namespace ch::net {

namespace {

#ifdef __EMSCRIPTEN__
// ------------------------------------------------------------ browser ---
class BrowserSocket final : public Transport {
public:
    BrowserSocket(const std::string& url, bool text) : text_(text) {
        EmscriptenWebSocketCreateAttributes a;
        emscripten_websocket_init_create_attributes(&a);
        a.url = url.c_str();
        ws_ = emscripten_websocket_new(&a);
        if (ws_ <= 0) { state_ = State::Closed; error_ = "WebSocket could not be created"; return; }
        emscripten_websocket_set_onopen_callback(ws_, this, [](int, const EmscriptenWebSocketOpenEvent*, void* self) {
            auto* s = static_cast<BrowserSocket*>(self);
            s->state_ = State::Open;
            for (auto& m : s->early_) s->put(m);
            s->early_.clear();
            return EM_TRUE;
        });
        emscripten_websocket_set_onmessage_callback(ws_, this, [](int, const EmscriptenWebSocketMessageEvent* e, void* self) {
            auto* s = static_cast<BrowserSocket*>(self);
            if (bool(e->isText) == s->text_) {
                // A text message's numBytes counts its terminating zero.
                const std::uint32_t n = e->isText && e->numBytes > 0 ? e->numBytes - 1 : e->numBytes;
                s->in_.emplace_back(e->data, e->data + n);
            }
            return EM_TRUE;
        });
        emscripten_websocket_set_onclose_callback(ws_, this, [](int, const EmscriptenWebSocketCloseEvent* e, void* self) {
            auto* s = static_cast<BrowserSocket*>(self);
            s->state_ = State::Closed;
            if (s->error_.empty() && e->code != 1000) s->error_ = "closed (" + std::to_string(e->code) + ")";
            return EM_TRUE;
        });
        emscripten_websocket_set_onerror_callback(ws_, this, [](int, const EmscriptenWebSocketErrorEvent*, void* self) {
            auto* s = static_cast<BrowserSocket*>(self);
            s->state_ = State::Closed;
            if (s->error_.empty()) s->error_ = "connection failed";
            return EM_TRUE;
        });
    }
    ~BrowserSocket() override {
        if (ws_ > 0) {
            emscripten_websocket_close(ws_, 1000, "bye");
            emscripten_websocket_delete(ws_);
        }
    }
    State state() const override { return state_; }
    void send(std::vector<std::uint8_t> m) override {
        if (state_ == State::Open) put(m);
        else if (state_ == State::Connecting) early_.push_back(std::move(m));
    }
    void poll(std::vector<std::vector<std::uint8_t>>& out) override {
        for (auto& m : in_) out.push_back(std::move(m));
        in_.clear();
    }
    void close() override {
        if (ws_ > 0 && state_ != State::Closed) emscripten_websocket_close(ws_, 1000, "bye");
        state_ = State::Closed;
    }
    std::string error() const override { return error_; }

private:
    void put(const std::vector<std::uint8_t>& m) {
        if (text_) {
            const std::string s(m.begin(), m.end());
            emscripten_websocket_send_utf8_text(ws_, s.c_str());
        } else {
            emscripten_websocket_send_binary(ws_, const_cast<std::uint8_t*>(m.data()), std::uint32_t(m.size()));
        }
    }
    EMSCRIPTEN_WEBSOCKET_T ws_ = 0;
    bool text_ = false;
    State state_ = State::Connecting;
    std::vector<std::vector<std::uint8_t>> in_, early_;
    std::string error_;
};

// The browser's fetch (emscripten_fetch, -sFETCH=1).
class BrowserPost final : public HttpPost {
public:
    BrowserPost(const std::string& url, const std::string& json) : json_(json) {
        emscripten_fetch_attr_t a;
        emscripten_fetch_attr_init(&a);
        std::strcpy(a.requestMethod, "POST");
        a.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        a.requestData = json_.c_str();
        a.requestDataSize = json_.size();
        static const char* headers[] = {"Content-Type", "application/json", nullptr};
        a.requestHeaders = headers;
        a.userData = this;
        a.onsuccess = [](emscripten_fetch_t* f) { static_cast<BrowserPost*>(f->userData)->finish(f); };
        a.onerror = [](emscripten_fetch_t* f) { static_cast<BrowserPost*>(f->userData)->finish(f); };
        fetch_ = emscripten_fetch(&a, url.c_str());
    }
    ~BrowserPost() override { if (fetch_) emscripten_fetch_close(fetch_); }
    bool done() override { return done_; }
    int status() const override { return status_; }
    const std::string& body() const override { return body_; }

private:
    void finish(emscripten_fetch_t* f) {
        status_ = f->status;
        if (f->data) body_.assign(f->data, std::size_t(f->numBytes));
        done_ = true;
        emscripten_fetch_close(f);
        fetch_ = nullptr;
    }
    std::string json_, body_;
    emscripten_fetch_t* fetch_ = nullptr;
    bool done_ = false;
    int status_ = 0;
};
#else
// ------------------------------------------------------------- native ---
// Sockets: Winsock on Windows, BSD sockets elsewhere.
#ifdef _WIN32
using Sock = SOCKET;
const Sock kNoSock = INVALID_SOCKET;
constexpr int kNoSignal = 0;
bool sockStartup() {
    static const bool ok = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    return ok;
}
void sockClose(Sock s) { closesocket(s); }
void sockNonBlocking(Sock s) { u_long one = 1; ioctlsocket(s, FIONBIO, &one); }
int sockError() { return WSAGetLastError(); }
bool sockInterrupted(int e) { return e == WSAEINTR; }
bool sockWouldBlock(int e) { return e == WSAEWOULDBLOCK; }
bool sockNotYet(int e) { return e == WSAENOTCONN || e == WSAEINPROGRESS || e == WSAEWOULDBLOCK; }
int sockPoll(pollfd* p) { return WSAPoll(p, 1, 0); }
#else
using Sock = int;
constexpr Sock kNoSock = -1;
#ifdef MSG_NOSIGNAL
constexpr int kNoSignal = MSG_NOSIGNAL;
#else
constexpr int kNoSignal = 0;
#endif
bool sockStartup() { return true; }
void sockClose(Sock s) { ::close(s); }
void sockNonBlocking(Sock s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK); }
int sockError() { return errno; }
bool sockInterrupted(int e) { return e == EINTR; }
bool sockWouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
bool sockNotYet(int e) { return e == ENOTCONN || e == EINPROGRESS; }
int sockPoll(pollfd* p) { return ::poll(p, 1, 0); }
#endif

struct Url { bool tls = false; std::string host, port, path; };
bool splitUrl(const std::string& url, const char* plain, const char* secure, Url& u) {
    std::string rest;
    if (url.rfind(plain, 0) == 0) rest = url.substr(std::strlen(plain));
    else if (url.rfind(secure, 0) == 0) { rest = url.substr(std::strlen(secure)); u.tls = true; }
    else return false;
    const std::size_t slash = rest.find('/');
    u.path = slash == std::string::npos ? "/" : rest.substr(slash);
    u.host = rest.substr(0, slash);
    u.port = u.tls ? "443" : "80";
    if (const std::size_t colon = u.host.rfind(':'); colon != std::string::npos) {
        u.port = u.host.substr(colon + 1);
        u.host = u.host.substr(0, colon);
    }
    return true;
}

#ifdef CH_HAVE_TLS
SSL_CTX* tlsContext() {
    static SSL_CTX* ctx = [] {
        SSL_CTX* c = SSL_CTX_new(TLS_client_method());
        if (c) {
            SSL_CTX_set_verify(c, SSL_VERIFY_PEER, nullptr);
            SSL_CTX_set_default_verify_paths(c);
            SSL_CTX_set_min_proto_version(c, TLS1_2_VERSION);
        }
        return c;
    }();
    return ctx;
}
#endif

// A minimal RFC 6455 client: the opening handshake, masked frames out,
// unmasked frames in (pings answered, fragments joined). With wss:// the
// bytes go through OpenSSL; every step is non-blocking.
class NativeSocket final : public Transport {
public:
    NativeSocket(const std::string& url, bool text) : text_(text) {
        Url u;
        if (!splitUrl(url, "ws://", "wss://", u)) { fail("not a ws:// or wss:// URL"); return; }
#ifndef CH_HAVE_TLS
        if (u.tls) { fail("this build has no TLS: wss:// needs OpenSSL"); return; }
#endif
        tls_ = u.tls;
        host_ = u.host;
        path_ = u.path;
        if (!sockStartup()) { fail("no network (WSAStartup failed)"); return; }
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host_.c_str(), u.port.c_str(), &hints, &res) != 0 || !res) { fail("unknown host " + host_); return; }
        fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd_ != kNoSock) {
            const int one = 1;
            setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);   // no Nagle delay on small frames
            sockNonBlocking(fd_);
#ifdef SO_NOSIGPIPE
            setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);   // macOS: no MSG_NOSIGNAL on older SDKs
#endif
            if (::connect(fd_, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen)) != 0) {
                const int e = sockError();
                if (!sockNotYet(e) && !sockWouldBlock(e)) fail("connect failed");
            }
        } else {
            fail("no socket");
        }
        freeaddrinfo(res);
        if (state_ == State::Closed) return;
        // The handshake goes out as soon as the socket (and TLS) is up.
        std::random_device rd;
        unsigned char key[16];
        for (auto& k : key) k = static_cast<unsigned char>(rd());
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string enc;
        for (int i = 0; i < 16; i += 3) {
            const unsigned v = unsigned(key[i]) << 16 | (i + 1 < 16 ? unsigned(key[i + 1]) << 8 : 0) | (i + 2 < 16 ? key[i + 2] : 0);
            enc += b64[v >> 18 & 63];
            enc += b64[v >> 12 & 63];
            enc += i + 1 < 16 ? b64[v >> 6 & 63] : '=';
            enc += i + 2 < 16 ? b64[v & 63] : '=';
        }
        const bool defaultPort = u.port == (u.tls ? "443" : "80");
        const std::string req = "GET " + path_ + " HTTP/1.1\r\nHost: " + host_ + (defaultPort ? "" : ":" + u.port) +
                                "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + enc +
                                "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        out_.insert(out_.end(), req.begin(), req.end());
    }
    ~NativeSocket() override {
#ifdef CH_HAVE_TLS
        if (ssl_) SSL_free(ssl_);
#endif
        if (fd_ != kNoSock) sockClose(fd_);
    }
    State state() const override { return state_; }
    void send(std::vector<std::uint8_t> m) override {
        if (state_ == State::Closed) return;
        if (state_ == State::Connecting) { early_.push_back(std::move(m)); return; }
        frame(text_ ? 1 : 2, m);
    }
    void poll(std::vector<std::vector<std::uint8_t>>& out) override {
        if (fd_ == kNoSock || state_ == State::Closed) return;
        if (!linkUp()) return;
        flush();
        std::uint8_t buf[8192];
        for (;;) {
            const long n = readSome(buf, sizeof buf);
            if (n > 0) { in_.insert(in_.end(), buf, buf + n); continue; }
            if (n == 0) { fail("the server closed the connection"); break; }
            if (n == -1) break;      // nothing more for now
            fail("connection lost");
            break;
        }
        if (state_ == State::Connecting) {
            const auto end = std::search(in_.begin(), in_.end(), kEnd, kEnd + 4);
            if (end == in_.end()) return;
            const std::string head(in_.begin(), end);
            in_.erase(in_.begin(), end + 4);
            if (head.find(" 101 ") == std::string::npos) {
                const std::size_t eol = head.find('\r');
                fail("the server refused the WebSocket upgrade (" + head.substr(0, eol) + ")");
                return;
            }
            state_ = State::Open;
            for (auto& m : early_) frame(text_ ? 1 : 2, m);
            early_.clear();
        }
        parse(out);
        flush();
    }
    void close() override {
        if (state_ == State::Open) { frame(8, {}); flush(); }
        state_ = State::Closed;
    }
    std::string error() const override { return error_; }

private:
    static constexpr char kEnd[4] = {'\r', '\n', '\r', '\n'};
    void fail(std::string why) {
        if (error_.empty()) error_ = std::move(why);
        state_ = State::Closed;
    }
    // The TCP connect and the TLS handshake, step by step: true once bytes
    // can flow.
    bool linkUp() {
        if (!tcpUp_) {
            pollfd p{fd_, POLLOUT, 0};
            if (sockPoll(&p) <= 0) return false;
            int err = 0;
            socklen_t len = sizeof err;
            getsockopt(fd_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
            if (err != 0) { fail(std::string("connect failed: ") + std::strerror(err)); return false; }
            tcpUp_ = true;
#ifdef CH_HAVE_TLS
            if (tls_) {
                if (!tlsContext() || !(ssl_ = SSL_new(tlsContext()))) { fail("TLS could not start"); return false; }
                SSL_set_fd(ssl_, static_cast<int>(fd_));
                SSL_set_tlsext_host_name(ssl_, host_.c_str());
                SSL_set1_host(ssl_, host_.c_str());
                SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
            }
#endif
        }
#ifdef CH_HAVE_TLS
        if (tls_ && !tlsUp_) {
            const int r = SSL_connect(ssl_);
            if (r == 1) { tlsUp_ = true; return true; }
            const int e = SSL_get_error(ssl_, r);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return false;
            char why[256];
            ERR_error_string_n(ERR_get_error(), why, sizeof why);
            fail(std::string("TLS handshake failed: ") + why);
            return false;
        }
#endif
        return true;
    }
    // > 0 bytes, 0 closed, -1 would block, -2 failed.
    long readSome(std::uint8_t* buf, std::size_t n) {
#ifdef CH_HAVE_TLS
        if (tls_) {
            const int r = SSL_read(ssl_, buf, int(n));
            if (r > 0) return r;
            const int e = SSL_get_error(ssl_, r);
            if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) return -1;
            return e == SSL_ERROR_ZERO_RETURN ? 0 : -2;
        }
#endif
        for (;;) {
            const auto r = ::recv(fd_, reinterpret_cast<char*>(buf), static_cast<int>(n), 0);
            if (r >= 0) return long(r);
            const int e = sockError();
            if (sockInterrupted(e)) continue;
            return sockWouldBlock(e) ? -1 : -2;
        }
    }
    long writeSome(const std::uint8_t* buf, std::size_t n) {
#ifdef CH_HAVE_TLS
        if (tls_) {
            const int r = SSL_write(ssl_, buf, int(n));
            if (r > 0) return r;
            const int e = SSL_get_error(ssl_, r);
            return e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE ? -1 : -2;
        }
#endif
        for (;;) {
            const auto r = ::send(fd_, reinterpret_cast<const char*>(buf), static_cast<int>(n), kNoSignal);
            if (r >= 0) return long(r);
            const int e = sockError();
            if (sockInterrupted(e)) continue;
            return sockWouldBlock(e) || sockNotYet(e) ? -1 : -2;
        }
    }
    void frame(int op, const std::vector<std::uint8_t>& p) {
        out_.push_back(std::uint8_t(0x80 | op));
        const std::size_t n = p.size();
        if (n < 126) {
            out_.push_back(std::uint8_t(0x80 | n));
        } else if (n < 65536) {
            out_.push_back(0x80 | 126);
            out_.push_back(std::uint8_t(n >> 8));
            out_.push_back(std::uint8_t(n));
        } else {
            out_.push_back(0x80 | 127);
            for (int i = 7; i >= 0; --i) out_.push_back(std::uint8_t(std::uint64_t(n) >> (8 * i)));
        }
        const std::uint32_t mask = rng_();
        for (int i = 0; i < 4; ++i) out_.push_back(std::uint8_t(mask >> (8 * i)));
        for (std::size_t i = 0; i < n; ++i) out_.push_back(std::uint8_t(p[i] ^ std::uint8_t(mask >> (8 * (i & 3)))));
    }
    void flush() {
        if (!tcpUp_ || (tls_ && !tlsUp_)) return;
        while (!out_.empty()) {
            const long n = writeSome(out_.data(), out_.size());
            if (n > 0) { out_.erase(out_.begin(), out_.begin() + n); continue; }
            if (n == -1) return;
            fail("send failed");
            return;
        }
    }
    void parse(std::vector<std::vector<std::uint8_t>>& out) {
        for (;;) {
            if (in_.size() < 2) return;
            const bool fin = in_[0] & 0x80;
            const int op = in_[0] & 0x0f;
            std::size_t n = in_[1] & 0x7f, at = 2;
            if (n == 126) {
                if (in_.size() < 4) return;
                n = std::size_t(in_[2]) << 8 | in_[3];
                at = 4;
            } else if (n == 127) {
                if (in_.size() < 10) return;
                n = 0;
                for (int i = 0; i < 8; ++i) n = n << 8 | in_[std::size_t(2 + i)];
                at = 10;
            }
            if (in_.size() < at + n) return;
            std::vector<std::uint8_t> data(in_.begin() + std::ptrdiff_t(at), in_.begin() + std::ptrdiff_t(at + n));
            in_.erase(in_.begin(), in_.begin() + std::ptrdiff_t(at + n));
            if (op == 8) {
                const int code = data.size() >= 2 ? data[0] << 8 | data[1] : 0;
                fail("the server closed the connection" + (code ? " (" + std::to_string(code) + ")" : std::string()));
                return;
            }
            if (op == 9) { frame(10, data); continue; }
            if (op == 10) continue;
            part_.insert(part_.end(), data.begin(), data.end());
            if (fin) {
                out.push_back(std::move(part_));
                part_.clear();
            }
        }
    }

    Sock fd_ = kNoSock;
    bool text_ = false, tls_ = false, tcpUp_ = false, tlsUp_ = false;
#ifdef CH_HAVE_TLS
    SSL* ssl_ = nullptr;
#endif
    State state_ = State::Connecting;
    std::string host_, path_, error_;
    std::vector<std::uint8_t> out_, part_;
    std::deque<std::uint8_t> in_;
    std::vector<std::vector<std::uint8_t>> early_;
    std::mt19937 rng_{std::random_device{}()};
};

// Native builds are for development: the POST blocks (a few hundred ms at
// most, once per channel joined).
class NativePost final : public HttpPost {
public:
    NativePost(const std::string& url, const std::string& json) {
        Url u;
        if (!splitUrl(url, "http://", "https://", u)) return;
        const std::string req = "POST " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nContent-Type: application/json\r\n" +
                                "Content-Length: " + std::to_string(json.size()) + "\r\nConnection: close\r\n\r\n" + json;
        std::string raw;
#ifdef CH_HAVE_TLS
        if (u.tls) {
            BIO* b = BIO_new_ssl_connect(tlsContext());
            if (!b) return;
            SSL* ssl = nullptr;
            BIO_get_ssl(b, &ssl);
            SSL_set_tlsext_host_name(ssl, u.host.c_str());
            SSL_set1_host(ssl, u.host.c_str());
            BIO_set_conn_hostname(b, (u.host + ":" + u.port).c_str());
            if (BIO_do_connect(b) == 1 && BIO_write(b, req.data(), int(req.size())) == int(req.size())) {
                char buf[4096];
                for (int n; (n = BIO_read(b, buf, sizeof buf)) > 0;) raw.append(buf, std::size_t(n));
            }
            BIO_free_all(b);
        } else
#endif
        {
            if (u.tls || !sockStartup()) return;
            addrinfo hints{};
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* res = nullptr;
            if (getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &res) != 0 || !res) return;
            const Sock fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (fd != kNoSock && ::connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen)) == 0 &&
                ::send(fd, req.data(), static_cast<int>(req.size()), kNoSignal) == static_cast<long>(req.size())) {
                char buf[4096];
                for (long n; (n = long(::recv(fd, buf, sizeof buf, 0))) > 0;) raw.append(buf, std::size_t(n));
            }
            if (fd != kNoSock) sockClose(fd);
            freeaddrinfo(res);
        }
        const std::size_t sp = raw.find(' '), end = raw.find("\r\n\r\n");
        if (sp == std::string::npos || end == std::string::npos) return;
        status_ = std::atoi(raw.c_str() + sp + 1);
        body_ = raw.substr(end + 4);
        if (raw.substr(0, end).find("chunked") != std::string::npos) {
            // Transfer-Encoding: chunked (the size lines dropped).
            std::string plain;
            for (std::size_t at = 0; at < body_.size();) {
                const std::size_t eol = body_.find("\r\n", at);
                if (eol == std::string::npos) break;
                const std::size_t n = std::strtoul(body_.c_str() + at, nullptr, 16);
                if (n == 0) break;
                plain += body_.substr(eol + 2, n);
                at = eol + 2 + n + 2;
            }
            body_ = plain;
        }
    }
    bool done() override { return true; }
    int status() const override { return status_; }
    const std::string& body() const override { return body_; }

private:
    int status_ = 0;
    std::string body_;
};
#endif

} // namespace

std::unique_ptr<Transport> openSocket(const std::string& url, bool text) {
#ifdef __EMSCRIPTEN__
    return std::make_unique<BrowserSocket>(url, text);
#else
    return std::make_unique<NativeSocket>(url, text);
#endif
}

std::unique_ptr<Transport> connect(const std::string& url) {
    if (url.rfind("apinator:", 0) == 0) return connectApinator(url.substr(9));
    return openSocket(url, false);
}

std::unique_ptr<HttpPost> httpPost(const std::string& url, const std::string& json) {
#ifdef __EMSCRIPTEN__
    return std::make_unique<BrowserPost>(url, json);
#else
    return std::make_unique<NativePost>(url, json);
#endif
}

} // namespace ch::net
