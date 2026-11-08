// ============================================================================
// ch_patch.hpp — resources/ch_data.chpatch (tools/make_ch_patch.py)
// ============================================================================
// Rebuilds static/ch_data.bin, the decompiled TW build's data, from the JP
// 1.2 exe (source 0) and, when the TW disc was imported, the TW disc's exe
// (source 1). Without source 1 its copies stay zero: the JP version does not
// read them.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace ch::chpatch {

struct Op {
    enum Kind : std::uint8_t { Zero = 0, Copy = 1, Literal = 2 } kind;
    std::uint32_t length = 0;
    std::uint8_t source = 0;
    std::uint32_t offset = 0;               // Copy: in the source file; Literal: in Patch::literals
};
struct Source { std::uint32_t size = 0, crc = 0; };
struct Patch {
    std::uint32_t base = 0, targetSize = 0, crcFull = 0, crcJpOnly = 0;
    std::vector<Source> sources;
    std::vector<Op> ops;
    std::vector<std::uint8_t> literals;
};

Patch parse(std::span<const std::uint8_t> file);
// Whether `file` is the patch's source `index` (size and CRC).
bool isSource(const Patch& p, int index, std::span<const std::uint8_t> file);
// The target; `tw` may be empty. Throws if a source or the result does not check.
std::vector<std::uint8_t> apply(const Patch& p, std::span<const std::uint8_t> jp, std::span<const std::uint8_t> tw);

} // namespace ch::chpatch
