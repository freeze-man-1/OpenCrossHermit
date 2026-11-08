// ============================================================================
// zlib_util.hpp — inflate helpers for the importer (zlib)
// ============================================================================
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ch::import {

// A zlib stream (RFC 1950), or a raw deflate stream (RFC 1951, zip entries).
std::vector<std::uint8_t> inflateZlib(std::span<const std::uint8_t> in, std::size_t sizeHint = 0);
std::vector<std::uint8_t> inflateRaw(std::span<const std::uint8_t> in, std::size_t size);
std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0);

} // namespace ch::import
