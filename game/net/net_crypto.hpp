// ============================================================================
// net_crypto.hpp — SHA-256, HMAC-SHA256 (hex) and base64, for the Apinator
// transport: its channel signatures ("app_key:" + hex HMAC of
// "socket_id:channel[:channel_data]") and the binary messages it carries as
// text.
// ============================================================================
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ch::net::crypto {

class Sha256 {
public:
    Sha256() { reset(); }
    void reset() {
        h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        len_ = 0;
        used_ = 0;
    }
    void update(const void* data, std::size_t n) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        len_ += n;
        while (n--) {
            block_[used_++] = *p++;
            if (used_ == 64) { compress(); used_ = 0; }
        }
    }
    void update(std::string_view s) { update(s.data(), s.size()); }
    std::array<std::uint8_t, 32> finish() {
        const std::uint64_t bits = len_ * 8;
        const std::uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (used_ != 56) update(&zero, 1);
        for (int i = 7; i >= 0; --i) { const auto b = std::uint8_t(bits >> (8 * i)); update(&b, 1); }
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 4; ++j) out[std::size_t(i * 4 + j)] = std::uint8_t(h_[std::size_t(i)] >> (24 - 8 * j));
        return out;
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return x >> n | x << (32 - n); }
    void compress() {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = std::uint32_t(block_[std::size_t(i * 4)]) << 24 | std::uint32_t(block_[std::size_t(i * 4 + 1)]) << 16 |
                   std::uint32_t(block_[std::size_t(i * 4 + 2)]) << 8 | block_[std::size_t(i * 4 + 3)];
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto [a, b, c, d, e, f, g, h] = h_;
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        const std::uint32_t add[8] = {a, b, c, d, e, f, g, h};
        for (int i = 0; i < 8; ++i) h_[std::size_t(i)] += add[i];
    }
    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> block_{};
    std::uint64_t len_ = 0;
    std::size_t used_ = 0;
};

[[nodiscard]] inline std::string hex(const std::uint8_t* p, std::size_t n) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

[[nodiscard]] inline std::string hmacSha256Hex(std::string_view key, std::string_view message) {
    std::array<std::uint8_t, 64> k{};
    if (key.size() > 64) {
        Sha256 h;
        h.update(key);
        const auto d = h.finish();
        std::copy(d.begin(), d.end(), k.begin());
    } else {
        std::copy(key.begin(), key.end(), k.begin());
    }
    std::array<std::uint8_t, 64> ipad{}, opad{};
    for (std::size_t i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha256 inner;
    inner.update(ipad.data(), 64);
    inner.update(message);
    const auto id = inner.finish();
    Sha256 outer;
    outer.update(opad.data(), 64);
    outer.update(id.data(), id.size());
    const auto d = outer.finish();
    return hex(d.data(), d.size());
}

inline constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] inline std::string base64(const std::uint8_t* p, std::size_t n) {
    std::string s;
    s.reserve((n + 2) / 3 * 4);
    for (std::size_t i = 0; i < n; i += 3) {
        const std::uint32_t v = std::uint32_t(p[i]) << 16 | (i + 1 < n ? std::uint32_t(p[i + 1]) << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        s += kBase64[v >> 18 & 63];
        s += kBase64[v >> 12 & 63];
        s += i + 1 < n ? kBase64[v >> 6 & 63] : '=';
        s += i + 2 < n ? kBase64[v & 63] : '=';
    }
    return s;
}

// False on a character outside the alphabet.
[[nodiscard]] inline bool unbase64(std::string_view s, std::vector<std::uint8_t>& out) {
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : s) {
        if (c == '=') break;
        const char* at = std::char_traits<char>::find(kBase64, 64, c);
        if (!at) return false;
        acc = acc << 6 | std::uint32_t(at - kBase64);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(std::uint8_t(acc >> bits)); }
    }
    return true;
}

} // namespace ch::net::crypto
