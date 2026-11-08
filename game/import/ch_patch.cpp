// ============================================================================
// ch_patch.cpp
// ============================================================================
#include "ch_patch.hpp"
#include "zlib_util.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ch::chpatch {

namespace {
struct Reader {
    std::span<const std::uint8_t> d;
    std::size_t at = 0;
    void need(std::size_t n) const { if (n > d.size() - at) throw std::runtime_error("ch_data.chpatch: truncated"); }
    std::uint8_t u8() { need(1); return d[at++]; }
    std::uint32_t u32() {
        need(4);
        const std::uint32_t v = d[at] | d[at + 1] << 8 | d[at + 2] << 16 | std::uint32_t(d[at + 3]) << 24;
        at += 4;
        return v;
    }
};
} // namespace

Patch parse(std::span<const std::uint8_t> file) {
    if (file.size() < 8 || std::memcmp(file.data(), "CHPATCH1", 8) != 0)
        throw std::runtime_error("ch_data.chpatch: not a patch");
    const auto body = import::inflateZlib(file.subspan(8));
    Reader r{body};
    Patch p;
    p.base = r.u32(); p.targetSize = r.u32(); p.crcFull = r.u32(); p.crcJpOnly = r.u32();
    for (unsigned n = r.u8(), i = 0; i < n; ++i) p.sources.push_back({r.u32(), r.u32()});
    const std::uint32_t nops = r.u32();
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < nops; ++i) {
        Op op{static_cast<Op::Kind>(r.u8())};
        op.length = r.u32();
        if (op.kind == Op::Copy) {
            op.source = r.u8(); op.offset = r.u32();
            if (op.source >= p.sources.size() || std::uint64_t(op.offset) + op.length > p.sources[op.source].size)
                throw std::runtime_error("ch_data.chpatch: bad copy");
        } else if (op.kind == Op::Literal) {
            r.need(op.length);
            op.offset = static_cast<std::uint32_t>(p.literals.size());
            p.literals.insert(p.literals.end(), body.begin() + std::ptrdiff_t(r.at), body.begin() + std::ptrdiff_t(r.at + op.length));
            r.at += op.length;
        } else if (op.kind != Op::Zero) {
            throw std::runtime_error("ch_data.chpatch: bad operation");
        }
        total += op.length;
        p.ops.push_back(op);
    }
    if (total != p.targetSize) throw std::runtime_error("ch_data.chpatch: size mismatch");
    return p;
}

bool isSource(const Patch& p, int index, std::span<const std::uint8_t> file) {
    return index >= 0 && std::size_t(index) < p.sources.size() && file.size() == p.sources[std::size_t(index)].size &&
           import::crc32(file) == p.sources[std::size_t(index)].crc;
}

std::vector<std::uint8_t> apply(const Patch& p, std::span<const std::uint8_t> jp, std::span<const std::uint8_t> tw) {
    if (!isSource(p, 0, jp)) throw std::runtime_error("the JP exe is not the 1.2 update's Cross Hermit.exe");
    const bool haveTw = !tw.empty();
    if (haveTw && !isSource(p, 1, tw)) throw std::runtime_error("the TW exe is not the TW disc's CROSS HERMIT.EXE");
    std::vector<std::uint8_t> out;
    out.reserve(p.targetSize);
    for (const Op& op : p.ops) {
        switch (op.kind) {
        case Op::Zero: out.insert(out.end(), op.length, 0); break;
        case Op::Literal:
            out.insert(out.end(), p.literals.begin() + op.offset, p.literals.begin() + op.offset + op.length);
            break;
        case Op::Copy: {
            const auto src = op.source == 0 ? jp : tw;
            if (src.empty()) out.insert(out.end(), op.length, 0);
            else out.insert(out.end(), src.begin() + op.offset, src.begin() + op.offset + op.length);
            break;
        }
        }
    }
    if (import::crc32(out) != (haveTw ? p.crcFull : p.crcJpOnly)) throw std::runtime_error("ch_data.chpatch: result does not check");
    return out;
}

} // namespace ch::chpatch
