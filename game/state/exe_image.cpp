// ============================================================================
// exe_image.cpp
// ============================================================================
#include "exe_image.hpp"
#include "../../engine/engine_core.hpp"
#include "../import/ch_patch.hpp"
#include "../text/sjis.hpp"

#include <SDL.h>

#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>

namespace ch::exe {

namespace {
// Tables whose values differ between the builds: ch.exe VA, JP VA, size
// (everything else the logic reads is the same in both, or JP-only text).
struct Override { std::uint32_t va, jpVa, size; };
constexpr std::array<Override, 1> kJpOverrides{{
    {0x6252fc, 0x53cc18, 0xcc},   // rank thresholds + cap
}};
// The values ch.exe changed from the TW disc's exe (same VAs), which the
// player's disc data goes with: the cap on a unit's experience and equipment
// value, and the ending's text rows.
struct DiscRange { std::uint32_t va, size; };
constexpr std::array<DiscRange, 2> kTwDiscValues{{{0x6253c4, 4}, {0x7577b0, 0xa0}}};

struct Images {
    std::optional<Image> ch, jpLogic, jpExe;
    std::map<std::tuple<int, std::uint32_t, std::size_t, std::size_t>,
             std::pair<std::vector<std::string>, std::vector<const char*>>> tables;
};
Images& images() { static Images i; return i; }

// CH_TRACE_TWONLY=1: report the JP version's reads of ch_data bytes that
// only the TW disc's exe supplies (left zero when only the JP disc was
// imported), once per 16-byte block.
struct TwOnlyTrace {
    std::vector<bool> marked, reported;
};
TwOnlyTrace* trace() {
    static std::unique_ptr<TwOnlyTrace> t = []() -> std::unique_ptr<TwOnlyTrace> {
        const char* e = SDL_getenv("CH_TRACE_TWONLY");
        if (!e || !*e || *e == '0') return nullptr;
        auto r = std::make_unique<TwOnlyTrace>();
        const auto patch = chpatch::parse(legacy::vfs::readResource("ch_data.chpatch"));
        r->marked.assign(patch.targetSize, false);
        r->reported.assign(patch.targetSize / 16 + 1, false);
        std::size_t at = 0;
        for (const auto& op : patch.ops) {
            if (op.kind == chpatch::Op::Copy && op.source == 1)
                for (std::size_t i = 0; i < op.length; ++i) r->marked[at + i] = true;
            at += op.length;
        }
        for (const auto& o : kJpOverrides)   // the JP version has these from its own exe
            for (std::uint32_t i = 0; i < o.size; ++i) r->marked[o.va - kChDataLo + i] = false;
        return r;
    }();
    return t.get();
}

} // namespace

Image Image::fromPe(std::span<const std::uint8_t> f) {
    auto rd32 = [&](std::size_t o) {
        if (o + 4 > f.size()) throw std::runtime_error("exe: truncated PE");
        return std::uint32_t(f[o] | f[o + 1] << 8 | f[o + 2] << 16 | std::uint32_t(f[o + 3]) << 24);
    };
    auto rd16 = [&](std::size_t o) { return std::uint16_t(rd32(o) & 0xffff); };
    if (f.size() < 0x40 || f[0] != 'M' || f[1] != 'Z') throw std::runtime_error("exe: not an executable");
    const std::uint32_t pe = rd32(0x3c);
    if (rd32(pe) != 0x4550) throw std::runtime_error("exe: not a PE file");
    const unsigned nsec = rd16(pe + 6), opt = rd16(pe + 20);
    const std::uint32_t imageBase = rd32(pe + 24 + 28);
    struct Sec { std::uint32_t vsize, rva, rawSize, rawPtr; };
    std::vector<Sec> secs;
    std::uint32_t lo = 0xffffffff, hi = 0;
    for (unsigned i = 0; i < nsec; ++i) {
        const std::size_t s = pe + 24 + opt + i * 40 + 8;
        Sec x{rd32(s), rd32(s + 4), rd32(s + 8), rd32(s + 12)};
        secs.push_back(x);
        lo = std::min(lo, x.rva);
        hi = std::max(hi, x.rva + std::max(x.vsize, x.rawSize));
    }
    if (secs.empty() || hi - lo > 0x4000000) throw std::runtime_error("exe: bad section table");
    std::vector<std::uint8_t> img(hi - lo, 0);
    for (const auto& x : secs) {
        const std::uint32_t n = std::min<std::uint32_t>(x.rawSize, std::max(x.vsize, x.rawSize));
        if (x.rawPtr >= f.size()) continue;
        const std::size_t avail = std::min<std::size_t>(n, f.size() - x.rawPtr);
        std::copy_n(f.begin() + x.rawPtr, avail, img.begin() + (x.rva - lo));
    }
    return Image(imageBase + lo, std::move(img));
}

std::span<const std::uint8_t> Image::bytes(std::uint32_t va, std::size_t n) const {
    if (!contains(va, n)) {
        char m[96];
        std::snprintf(m, sizeof m, "exe data: 0x%x+0x%zx is outside the image", va, n);
        throw std::out_of_range(m);
    }
    if (traced_) note(va, n);
    return std::span<const std::uint8_t>(bytes_).subspan(va - base_, n);
}

void Image::note(std::uint32_t va, std::size_t n) const {
    TwOnlyTrace* t = trace();
    if (!t) return;
    for (std::size_t i = 0; i < n; ++i) {
        if (va + i < base_) continue;
        const std::size_t o = va - base_ + i;
        if (o < t->marked.size() && t->marked[o] && !t->reported[o / 16]) {
            t->reported[o / 16] = true;
            SDL_Log("[twonly] JP read of TW-only data at 0x%zx", base_ + o);
        }
    }
}

std::string_view Image::cstr(std::uint32_t va) const {
    if (!contains(va)) return {};
    const std::size_t o = va - base_;
    std::size_t e = o;
    while (e < bytes_.size() && bytes_[e] != 0) ++e;
    if (traced_) note(va, e - o + 1);
    return std::string_view(reinterpret_cast<const char*>(bytes_.data()) + o, e - o);
}

const Image& chData() {
    auto& i = images();
    if (!i.ch) {
        auto bytes = legacy::vfs::readAsset("static\\ch_data.bin");
        if (bytes.size() != kChDataHi - kChDataLo)
            throw std::runtime_error("static/ch_data.bin: unexpected size (import the game files again)");
        i.ch.emplace(kChDataLo, std::move(bytes));
        if (legacy::vfs::assetExists("static\\tw.exe")) {
            const Image tw = Image::fromPe(legacy::vfs::readAsset("static\\tw.exe"));
            for (const auto& r : kTwDiscValues) {
                const auto src = tw.bytes(r.va, r.size);
                std::copy(src.begin(), src.end(), i.ch->raw().begin() + (r.va - kChDataLo));
            }
        }
    }
    return *i.ch;
}

const Image& own(GameVersion v) {
    if (v == GameVersion::Tw) return chData();
    auto& i = images();
    if (!i.jpExe) i.jpExe = Image::fromPe(legacy::vfs::readAsset("static\\jp.exe"));
    return *i.jpExe;
}

const Image& logicData(GameVersion v) {
    if (v == GameVersion::Tw) return chData();
    auto& i = images();
    if (!i.jpLogic) {
        Image img = chData();
        const Image& jp = own(GameVersion::Jp);
        for (const auto& o : kJpOverrides) {
            const auto src = jp.bytes(o.jpVa, o.size);
            std::copy(src.begin(), src.end(), img.raw().begin() + (o.va - kChDataLo));
        }
        img.setTraced(trace() != nullptr);
        i.jpLogic.emplace(std::move(img));
    }
    return *i.jpLogic;
}

std::string text(GameVersion v, std::uint32_t va) {
    const std::string_view s = own(v).cstr(va);
    return v == GameVersion::Tw ? big5ToUtf8(s) : sjisToUtf8(s);
}

std::vector<std::string> texts(GameVersion v, std::uint32_t table, std::size_t count, std::size_t stride) {
    const Image& img = own(v);
    std::vector<std::string> out;
    out.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        const std::uint32_t p = img.u32(table + std::uint32_t(k * stride));
        out.push_back(p ? text(v, p) : std::string());
    }
    return out;
}

namespace {
auto& tableSlot(GameVersion v, std::uint32_t table, std::size_t count, std::size_t stride) {
    auto& slot = images().tables[{int(v), table, count, stride}];
    if (slot.first.size() != count) {
        slot.first = texts(v, table, count, stride);
        slot.second.clear();
        for (const auto& s : slot.first) slot.second.push_back(s.c_str());
    }
    return slot;
}
} // namespace

const std::vector<std::string>& strings(GameVersion v, std::uint32_t table, std::size_t count, std::size_t stride) {
    return tableSlot(v, table, count, stride).first;
}

const std::vector<const char*>& textTable(GameVersion v, std::uint32_t table, std::size_t count, std::size_t stride) {
    return tableSlot(v, table, count, stride).second;
}

void reload() { images() = Images{}; }

} // namespace ch::exe
