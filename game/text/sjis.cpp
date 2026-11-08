// ============================================================================
// sjis.cpp
// ============================================================================
#include "sjis.hpp"
#include "../../engine/engine_core.hpp"
#include "cp932_table.gen.hpp"
#include "cp950_table.gen.hpp"

#include <cstdint>

namespace ch {

namespace {
void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | cp >> 6));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | cp >> 12));
        out.push_back(static_cast<char>(0x80 | (cp >> 6 & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

int leadIndex(std::uint8_t b) {
    if (b >= 0x81 && b <= 0x9F) return b - 0x81;
    if (b >= 0xE0 && b <= 0xFC) return b - 0xE0 + (0xA0 - 0x81);
    return -1;
}
} // namespace

namespace {
const std::unordered_map<std::string, std::string>* gTextTable = nullptr;
int gTextGeneration = 0;
} // namespace

void setTextTable(const std::unordered_map<std::string, std::string>* table) {
    gTextTable = table;
    ++gTextGeneration;
}
int textGeneration() { return gTextGeneration; }

std::string textKey(std::string_view utf8) {
    std::uint64_t h = 0xcbf29ce484222325;
    for (const char c : utf8) h = (h ^ std::uint8_t(c)) * 0x100000001b3;
    std::string key = "@0000000000000000";
    for (std::size_t i = 16; i > 0; --i, h >>= 4) key[i] = "0123456789abcdef"[h & 15];
    return key;
}

std::string tr(std::string_view japanese) {
    std::string s(japanese);
    if (gTextTable)
        if (const auto it = gTextTable->find(textKey(s)); it != gTextTable->end()) return it->second;
    return s;
}

namespace {
std::string decodeSjis(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 3 / 2);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto b = static_cast<std::uint8_t>(s[i]);
        if (b < 0x80) {
            appendUtf8(out, b);
        } else if (b >= 0xA1 && b <= 0xDF) {
            appendUtf8(out, 0xFF61u + (b - 0xA1u));
        } else if (const int lead = leadIndex(b); lead >= 0 && i + 1 < s.size()) {
            const auto t = static_cast<std::uint8_t>(s[++i]);
            const std::uint16_t cp = (t >= 0x40 && t <= 0xFC)
                ? cp932::kDouble[std::size_t(lead) * cp932::kTrailCount + (t - 0x40u)] : 0;
            appendUtf8(out, cp ? cp : 0xFFFDu);
        } else {
            appendUtf8(out, 0xFFFDu);
        }
    }
    return out;
}
} // namespace

std::string sjisToUtf8(std::string_view s) {
    std::string out = decodeSjis(s);
    if (gTextTable)
        if (const auto it = gTextTable->find(textKey(out)); it != gTextTable->end()) return it->second;
    return out;
}

std::string translatePassage(const std::vector<std::string>& raw) {
    if (!gTextTable) return {};
    std::string joined;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (i != 0) joined += '\n';
        joined += decodeSjis(raw[i]);
    }
    const auto it = gTextTable->find(textKey(joined));
    return it != gTextTable->end() ? it->second : std::string{};
}

std::string big5ToUtf8(std::string_view s) {
    std::string out;
    out.reserve(s.size() * 3 / 2);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto b = static_cast<std::uint8_t>(s[i]);
        if (b < 0x80) {
            appendUtf8(out, b);
        } else if (b >= 0x81 && b <= 0xFE && i + 1 < s.size()) {
            const auto t = static_cast<std::uint8_t>(s[++i]);
            const std::uint16_t cp = (t >= 0x40 && t <= 0xFE)
                ? cp950::kDouble[std::size_t(b - 0x81) * cp950::kTrailCount + (t - 0x40u)] : 0;
            // 0x8140 is not Big5: the translation kept the Japanese original's
            // Shift-JIS full-width spaces, which padded the menus' strings.
            if (b == 0x81 && t == 0x40) appendUtf8(out, 0x3000u);
            else appendUtf8(out, cp ? cp : 0xFFFDu);
        } else {
            appendUtf8(out, 0xFFFDu);
        }
    }
    return out;
}

std::string gameTextToUtf8(std::string_view raw) {
    return legacy::vfs::variant() == "tw" ? big5ToUtf8(raw) : sjisToUtf8(raw);
}

} // namespace ch
