// ============================================================================
// adv_data.cpp
// ============================================================================
#include "adv_data.hpp"
#include "../state/exe_image.hpp"

#include <map>

namespace ch::adv_tables {

namespace {
constexpr std::uint32_t kSpeakerTable = 0x624520, kFrameLists = 0x624834, kScTable = 0x622668, kMcTable = 0x622250;
constexpr std::size_t kSpeakerCount = 12, kFrameTypes = 7, kScCount = 131, kMcCount = 132, kNameCount = 201;
} // namespace

const Layout& layout(GameVersion v) {
    static std::map<GameVersion, Layout> cache;
    auto [it, fresh] = cache.try_emplace(v);
    Layout& l = it->second;
    if (!fresh) return l;
    const exe::Image& d = exe::logicData(v);
    for (std::size_t i = 0; i < kSpeakerCount; ++i) {
        Speaker r{};
        for (std::size_t k = 0; k < r.size(); ++k) r[k] = d.s16(kSpeakerTable + std::uint32_t(i * 0x26 + k * 2));
        l.speakers.push_back(r);
    }
    for (std::size_t t = 0; t < kFrameTypes; ++t) {   // rows of 6 x s16, -1 ends
        const std::uint32_t list = d.u32(kFrameLists + std::uint32_t(t * 4));
        auto& parts = l.frames.emplace_back();
        for (std::uint32_t p = list; d.contains(p, 12) && d.s16(p) != -1 && parts.size() < 64; p += 12)
            parts.push_back({d.s16(p), d.s16(p + 2), d.s16(p + 4), d.s16(p + 6), d.s16(p + 8), d.s16(p + 10)});
    }
    for (std::size_t i = 0; i < kScCount; ++i) {
        const std::uint32_t b = kScTable + std::uint32_t(i * 60);
        ScFace f{std::string(d.cstr(d.u32(b))), d.s16(b + 4), d.s16(b + 6), d.s16(b + 8), d.s16(b + 10), {}};
        for (std::uint32_t e = 0; e < 8; ++e)
            for (std::uint32_t k = 0; k < 3; ++k) f.expr[e][k] = d.s16(b + 0xc + e * 6 + k * 2);
        l.scFaces.push_back(std::move(f));
    }
    for (std::size_t i = 0; i < kMcCount; ++i) {
        const std::uint32_t b = kMcTable + std::uint32_t(i * 8);
        l.mcFaces.push_back({std::string(d.cstr(d.u32(b))), d.s16(b + 4), d.s16(b + 6)});
    }
    return l;
}

const std::vector<std::string>& names(GameVersion v) {
    return exe::strings(v, v == GameVersion::Jp ? 0x530ce8 : 0x621f2c, kNameCount);
}

} // namespace ch::adv_tables

namespace ch::adv_media {

namespace {
constexpr std::size_t kBgCount = 220, kVoiceGroups = 141;
struct Va { std::uint32_t bg, voice; };
Va va(GameVersion v) { return v == GameVersion::Jp ? Va{0x52ff64, 0x512140} : Va{0x621bbc, 0x621988}; }
} // namespace

const std::vector<std::string>& bgNames(GameVersion v) { return exe::strings(v, va(v).bg, kBgCount); }

std::string voice(GameVersion v, int group, int index) {
    if (group < 0 || std::size_t(group) >= kVoiceGroups) return {};
    const exe::Image& img = exe::own(v);
    const std::uint32_t table = va(v).voice;
    // The groups point into the flat list of name pointers just before the
    // group table; it ends where the table starts.
    const std::uint32_t at = img.u32(table + std::uint32_t(group) * 4) + std::uint32_t(std::max(index, 0)) * 4;
    if (at < img.u32(table) || at >= table) return {};
    return exe::text(v, img.u32(at));
}

} // namespace ch::adv_media
