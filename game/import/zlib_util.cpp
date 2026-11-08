// ============================================================================
// zlib_util.cpp
// ============================================================================
#include "zlib_util.hpp"

#include <zlib.h>

#include <stdexcept>

namespace ch::import {

namespace {
std::vector<std::uint8_t> run(std::span<const std::uint8_t> in, int windowBits, std::size_t hint, bool exact) {
    z_stream z{};
    if (inflateInit2(&z, windowBits) != Z_OK) throw std::runtime_error("inflate: init failed");
    std::vector<std::uint8_t> out(hint ? hint : in.size() * 4 + 64);
    z.next_in = const_cast<Bytef*>(in.data());
    z.avail_in = static_cast<uInt>(in.size());
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        if (z.total_out == out.size()) {
            if (exact) break;
            out.resize(out.size() * 2);
        }
        z.next_out = out.data() + z.total_out;
        z.avail_out = static_cast<uInt>(out.size() - z.total_out);
        rc = inflate(&z, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&z);
            throw std::runtime_error("inflate: corrupt data");
        }
        if (rc == Z_OK && z.avail_in == 0 && z.avail_out != 0) break;   // input ended early
    }
    out.resize(z.total_out);
    inflateEnd(&z);
    if (rc != Z_STREAM_END && !(exact && out.size() == hint)) throw std::runtime_error("inflate: truncated data");
    return out;
}
} // namespace

std::vector<std::uint8_t> inflateZlib(std::span<const std::uint8_t> in, std::size_t sizeHint) {
    return run(in, 15, sizeHint, false);
}

std::vector<std::uint8_t> inflateRaw(std::span<const std::uint8_t> in, std::size_t size) {
    if (size == 0) return {};
    return run(in, -15, size, true);
}

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc) {
    std::size_t at = 0;
    while (at < data.size()) {   // zlib's length is a uInt
        const auto n = static_cast<uInt>(std::min<std::size_t>(data.size() - at, 1u << 30));
        crc = static_cast<std::uint32_t>(::crc32(crc, data.data() + at, n));
        at += n;
    }
    return crc;
}

} // namespace ch::import
