// ============================================================================
// kojin_blend.cpp
// ============================================================================
#include "kojin_blend.hpp"
#include "kojin_task.hpp"
#include "../option/options.hpp"
#include "../state/exe_image.hpp"
#include "../state/game_logic.hpp"
#include "../text/sjis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ch {

namespace {
using U = std::uint32_t;
constexpr std::uint32_t kKojinState = 0x7a4e60;   // s16: 2 fades the screen out
constexpr std::uint32_t kShown = 0x7d6a34;        // s16: index into the unit list
constexpr std::uint32_t kPoints = 0x7a511c;       // s32: shared XP (school points)
constexpr std::uint32_t kStore = 0x7aacaa;        // u16 per item: bit 0 owned, 1-7 owner, 8-11 state
constexpr std::uint32_t kSkills = 0x6c2dc8, kItems = 0x6d5118, kJobs = 0x6b2d8a;
constexpr std::uint32_t kJobRules = 0x738cd0;     // 0x32 per job (game_logic openJobs)
constexpr std::uint32_t kJobOpen = 0xf8, kJobLevel = 0xd8;   // unit progress + job (1-based) / + job - 1
constexpr int kSe = 5;
constexpr int kItemCount = 192;                   // 193 on are test and dummy entries

// Colours.
constexpr std::uint32_t kInk = 0xffeef0f6u, kDim = 0xff9aa3bbu, kGold = 0xffe6cf8eu, kUp = 0xff6fb0ffu,
                        kDown = 0xffff6f6fu, kLine = 0xff6c7898u, kGreen = 0xff7fd17fu;
// Priorities: higher is farther back.
enum : int {
    kPBack = 0x7e, kPPanel = 0x7c, kPTint = 0x7b, kPFrame = 0x7a, kPWell = 0x76, kPWellLine = 0x75,
    kPItem = 0x70, kPText = 0x6c, kPOverBack = 0x79, kPOver = 0x68, kPOverWell = 0x66, kPOverLine = 0x65, kPOverItem = 0x62,
    kPOverText = 0x60, kPTip = 0x50, kPTipLine = 0x4f, kPTipItem = 0x4e, kPTipText = 0x4c,
};

// The status window (KojinData.bin frame 0): its title plates, the Skill
// window's frosted stone and the gold rail around it.
constexpr int kWindow = 0;
struct PlateArt { int x0, y0, x1, y1, cap, mid; };
constexpr PlateArt kSilver{10, 13, 353, 33, 34, 70}, kGoldPlate{372, 12, 536, 39, 26, 505};
constexpr int kStoneX = 552, kStoneY = 40, kStoneW = 34, kStoneH = 340;

// Rect-table rows (KojinTask's): icons and the like.
enum : int { kRSkillIcons = 0xf, kRItemIcons = 0x1d, kRFace = 10, kRJobIcons = 0x41, kRItemStatIcons = 0x4b, kRBoost = 0x50 };

constexpr std::array<int, 15> kClassBase{0xb, 0xd, 0xf, 0x11, 0x13, 1, 3, 5, 7, 9, 0x15, 0x17, 0x19, 0x1b, 0x1d};
constexpr std::array<int, 8> kWait{0x1e, 0x3c, 0x78, 0xb4, 0xf0, 300, 600, 999};
constexpr std::array<int, 8> kMove{999, 0x96, 0x7d, 100, 0x50, 0x3c, 0x28, 0x14};
constexpr std::array<const char*, 8> kGrades{"S", "A", "B", "C", "D", "E", "F", "G"};
struct Field { std::uint8_t off; bool word; };
constexpr std::array<Field, 22> kEffect{{{0xc, true}, {0xe, true}, {0x10, true}, {0x12, true}, {0x14, true},
                                         {0x16, true}, {0x18, false}, {0x1a, true}, {0x1c, false}, {0x1d, false},
                                         {0x1e, false}, {0x1f, false}, {0x20, false}, {0x21, false}, {0x22, true},
                                         {0x24, true}, {0x26, true}, {0x28, false}, {0x2c, true}, {0x30, true},
                                         {0x32, true}, {0x34, false}}};
constexpr std::array<std::uint32_t, 7> kTargetJp{0x50bdcc, 0x50bdc4, 0x50bdbc, 0x50bdb4, 0x50bdac, 0x50bda4, 0x50bd98};
constexpr std::array<std::uint32_t, 7> kTargetTw{0x5a0b20, 0x5a0b18, 0x5a0b0c, 0x5a0b04, 0x5a0afc, 0x5a0af4, 0x5a0ae8};
constexpr std::array<std::uint32_t, 6> kRangeJp{0x50bd80, 0x50bdc4, 0x50bdbc, 0x50bdb4, 0x50bd74, 0x50bd68};
constexpr std::array<std::uint32_t, 6> kRangeTw{0x5a0ad0, 0x5a0b18, 0x5a0b0c, 0x5a0b04, 0x5a0ac4, 0x5a0ab8};
// Per attribute, class (1..5) and +0x72 pair: the values it feeds (KojinTask's kStatIcons, FUN_004abb60).
constexpr std::array<std::array<std::int8_t, 0x35>, 7> kStatIcons{{
    {1, 9, 11, 5, 6, 1, 9, 11, 5, 6, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 9, 11, 6, 15, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 1, 1},
    {8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 5, 6, 8, 14, 1, 5, 6, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 2, 2, 2},
    {13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 5, 6, 15, 13, 18, 5, 6, 15, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 3, 3, 3},
    {19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 6, 15, 19, 2, 12, 6, 15, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 4, 4, 4},
    {10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 6, 15, 0, 10, 2, 6, 15, 0, 5, 5, 5},
    {1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 6, 6, 6},
    {2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 7, 7, 7},
}};

// The port's own words, in Japanese for tr() (languages/<id>/text/port.tsv).
const char* const kStatNames[7] = {"STR", "AGI", "SEN", "VIR", "INT", "CON", "SPI"};
const char* const kAttrNames[7] = {"筋力", "敏捷", "感覚", "精神", "知力", "体力", "霊力"};
const char* const kBoostNames[19] = {"HP", "MP", "絶対", "補正", "命中", "威力", "効果", "回避", "装甲", "結界",
                                     "HP回復", "MP回復", "視界", "移動", "射程", "範囲", "硬直", "肉体", "意志"};
const char* const kCats[12] = {"重", "迅", "前", "後", "聖", "闇", "虚", "地", "風", "水", "火", "深"};
const char* const kItemTypes[10] = {"", "大剣", "剣", "盾", "弓", "弩", "杖", "鎧", "革鎧", "装飾品"};
const char* const kRarities[8] = {"粗", "並", "上", "稀", "神秘", "聖", "神", "特"};

std::string fmt(long long n) {
    const bool neg = n < 0;
    std::string d = std::to_string(neg ? -n : n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return neg ? "-" + out : out;
}

int grade(const std::array<int, 8>& t, int value, bool descending) {
    int i = 0;
    while (i < 7 && (descending ? value < t[std::size_t(i)] : t[std::size_t(i)] < value)) ++i;
    return i;
}

// "4月2週" in the language shown.
std::string date(int month, int week) {
    std::string s = tr("{m}月{w}週");
    s.replace(s.find("{m}"), 3, std::to_string(month));
    s.replace(s.find("{w}"), 3, std::to_string(week));
    return s;
}

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}
} // namespace

KojinBlend::KojinBlend(KojinTask& task, GameContext& ctx) : t_(task), ctx_(ctx) {}

void KojinBlend::open() {
    for (ysub::TextList* l : {&skillText_, &itemText_, &jobText_, &capText_}) l->setFont(12, 1);
    skillText_.load(ctx_, "data\\YsText\\SkillExp.ybc");
    itemText_.load(ctx_, "data\\YsText\\ItemExp.ybc");
    jobText_.load(ctx_, "data\\YsText\\JobExp.ybc");
    capText_.load(ctx_, "data\\YsText\\KSCapabilityUP.ybc");
    skillCache_.clear(); itemCache_.clear(); jobCache_.clear(); capCache_.clear();
    // The glossary opens each character's profile with the full name and age: "名＝姓（１５歳♀）" (JP),
    // "名•姓（女，１５歲）" (TW). Keyed by the given name, which is the unit's short name.
    profileNames_.clear();
    const std::vector<std::uint8_t> words = ctx_.readData("data\\YsText\\Words.ybc");
    for (std::size_t at = 0; at < words.size();) {
        std::size_t end = at;
        while (end < words.size() && words[end]) ++end;
        if (end - at > 4 && end - at < 0x400) {
            const std::string s = gameTextToUtf8({reinterpret_cast<const char*>(words.data() + at), end - at});
            const std::size_t open = s.find("（"), close = s.find("）", open);
            if (open != std::string::npos && open <= 48 && close != std::string::npos &&
                (s.find("歳", open) < close || s.find("歲", open) < close)) {
                const std::string full = s.substr(0, open);
                std::size_t cut = std::string::npos;
                for (const char* sep : {"＝", "•", "‧", "·", "・", "，"}) cut = std::min(cut, full.find(sep));
                if (cut != std::string::npos && cut > 0) profileNames_.emplace(full.substr(0, cut), full);
            }
        }
        at = end + 1;
    }
    focus_ = card_ = shown_ = Kind::None;
    browse_ = 0; classes_ = false; classPick_ = classShown_ = 0; adds_ = {};
    havePreview_ = false; previewKey_ = ~0ull; learnAsk_ = 0; stripScroll_ = -1;
}

// ================================================================= drawing ===
void KojinBlend::fill(int x, int y, int w, int h, std::uint32_t argb, int prio) {
    if (w > 0 && h > 0) ysub::fill(ctx_, x, y, w, h, prio, argb);
}

void KojinBlend::sub(int frame, int sx, int sy, int sw, int sh, int x, int y, int dw, int dh, int prio,
                     std::uint8_t alpha, std::uint8_t rgb, std::uint16_t flags) {
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    SpriteCmd c{.bank = KojinTask::kBank, .frame = static_cast<std::uint16_t>(frame), .x = x, .y = y,
                .w = static_cast<std::int16_t>(dw), .h = static_cast<std::int16_t>(dh),
                .src = {static_cast<std::int16_t>(sx), static_cast<std::int16_t>(sy),
                        static_cast<std::int16_t>(sx + sw), static_cast<std::int16_t>(sy + sh)},
                .flags = static_cast<std::uint16_t>(kSprBank | kSprSrcRect | flags), .blend = kBlendAlpha,
                .color = {rgb, rgb, rgb, alpha}, .priority = static_cast<std::uint16_t>(prio)};
    ctx_.draw.sprite(c);
}

// Small capitals (the headings' style): runs of lower-case letters as capitals at this share of the size.
namespace {
constexpr int kSmallCaps = 80;
// One character each, so they can be spaced out as the wide display capitals are.
struct Run { std::string s; bool small; };
std::vector<Run> capsRuns(const std::string& s) {
    std::vector<Run> out;
    for (std::size_t i = 0; i < s.size();) {
        std::size_t n = 1;
        while (i + n < s.size() && (static_cast<unsigned char>(s[i + n]) & 0xc0) == 0x80) ++n;
        const char c = s[i];
        const bool small = c >= 'a' && c <= 'z';
        out.push_back({small ? std::string(1, char(c - 'a' + 'A')) : s.substr(i, n), small});
        i += n;
    }
    return out;
}
// Letter spacing in tenths of a pixel.
int tracking(int px) { return px; }
} // namespace

int KojinBlend::textW(const std::string& s, int px, bool bold) const {
    if (style_ == kCaps) {
        int w10 = 0;
        const std::vector<Run> runs = capsRuns(s);
        for (const Run& r : runs) w10 += ctx_.sprites->textWidth(r.s, r.small ? px * kSmallCaps / 100 : px, bold, 1) * 10 + tracking(px);
        if (w10) return (w10 - tracking(px)) / 10;
    }
    const int w = ctx_.sprites->textWidth(s, px, bold, style_ == kSans ? 0 : 1);
    return w ? w : int(s.size()) * px / 2;
}

int KojinBlend::text(int x, int y, const std::string& s, int px, std::uint32_t argb, int prio, bool bold, int align,
                     bool shadow) {
    if (s.empty()) return 0;
    const int w = textW(s, px, bold);
    const int left = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
    const int top = y - px * 7 / 10;                  // y is the line's middle
    const auto one = [&](int at, int t, const std::string& str, int size, int face) {
        if (shadow)
            ctx_.draw.text({.x = at + 1, .y = t + 1, .priority = static_cast<std::uint16_t>(prio + 1), .argb = argb & 0xff000000u,
                            .text = str, .fontPx = static_cast<std::uint16_t>(size), .bold = bold, .face = std::uint8_t(face)});
        ctx_.draw.text({.x = at, .y = t, .priority = static_cast<std::uint16_t>(prio), .argb = argb, .text = str,
                        .fontPx = static_cast<std::uint16_t>(size), .bold = bold, .face = std::uint8_t(face)});
    };
    if (style_ != kCaps) { one(left, top, s, px, style_ == kSans ? 0 : 1); return w; }
    int at10 = left * 10;
    for (const Run& r : capsRuns(s)) {
        const int size = r.small ? px * kSmallCaps / 100 : px;
        if (r.s != " ") one(at10 / 10, top + (px - size) * 116 / 100, r.s, size, 1);   // on one baseline (the font's ascent is 1.16 em)
        at10 += ctx_.sprites->textWidth(r.s, size, bold, 1) * 10 + tracking(px);
    }
    return w;
}

int KojinBlend::caps(int x, int y, const std::string& s, int px, std::uint32_t argb, int prio, bool bold, int align, bool shadow) {
    style_ = kCaps;
    const int w = text(x, y, s, px, argb, prio, bold, align, shadow);
    style_ = kSerif;
    return w;
}

int KojinBlend::sans(int x, int y, const std::string& s, int px, std::uint32_t argb, int prio, bool bold, int align, bool shadow) {
    style_ = kSans;
    const int w = text(x, y, s, px, argb, prio, bold, align, shadow);
    style_ = kSerif;
    return w;
}

std::vector<std::string> KojinBlend::wrap(const std::string& s, int px, int width) const {
    // Each line of the text wrapped on its own.
    if (const std::size_t nl = s.find('\n'); nl != std::string::npos) {
        std::vector<std::string> out = wrap(s.substr(0, nl), px, width);
        for (std::string& l : wrap(s.substr(nl + 1), px, width)) out.push_back(std::move(l));
        return out;
    }
    std::vector<std::string> out;
    std::string line;
    std::size_t at = 0;
    const bool cjk = !ctx_.sprites->textWidth("a", px, false) || s.find(' ') == std::string::npos;
    while (at < s.size()) {
        // A word (to the next space), or one character in text without spaces.
        std::size_t end = at;
        if (cjk) { end = at + 1; while (end < s.size() && (static_cast<unsigned char>(s[end]) & 0xc0) == 0x80) ++end; }
        else { end = s.find(' ', at); if (end == std::string::npos) end = s.size(); }
        const std::string word = s.substr(at, end - at);
        const std::string next = line.empty() || cjk ? line + word : line + " " + word;
        if (!line.empty() && textW(next, px) > width) { out.push_back(line); line = word; }
        else line = next;
        at = cjk ? end : end + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

int KojinBlend::paragraph(int x, int y, int w, const std::string& s, int px, std::uint32_t argb, int prio, int maxLines) {
    const int lh = px + px / 3 + 1;
    int n = 0;
    for (const std::string& line : wrap(s, px, w)) {
        if (n == maxLines) break;
        text(x, y + lh / 2 + n * lh, line, px, argb, prio, false, 0, false);
        ++n;
    }
    return n * lh;
}

// A vertical gradient, in 2px bands.
void KojinBlend::vgrad(const Box& b, std::uint32_t top, std::uint32_t bottom, int prio) {
    const auto mix = [](std::uint32_t a, std::uint32_t c, int i, int n) {
        std::uint32_t out = 0;
        for (int s = 0; s < 32; s += 8) {
            const int x = int(a >> s & 0xff), y = int(c >> s & 0xff);
            out |= std::uint32_t(x + (y - x) * i / std::max(1, n)) << s;
        }
        return out;
    };
    for (int y = 0; y < b.h; y += 2) fill(b.x, b.y + y, b.w, std::min(2, b.h - y), mix(top, bottom, y, b.h - 1), prio);
}

void KojinBlend::outline(const Box& b, std::uint32_t argb, int prio) {
    fill(b.x, b.y, b.w, 1, argb, prio);
    fill(b.x, b.y + b.h - 1, b.w, 1, argb, prio);
    fill(b.x, b.y + 1, 1, b.h - 2, argb, prio);
    fill(b.x + b.w - 1, b.y + 1, 1, b.h - 2, argb, prio);
}

// A slot: black edge, a silver line, black again, a dark gradient; lit when pointed at, gold when chosen.
void KojinBlend::well(const Box& b, int prio, bool lit, bool gold) {
    outline(b, 0xff000000u, prio);
    outline({b.x + 1, b.y + 1, b.w - 2, b.h - 2}, gold ? kGold : lit ? 0xffc9d1e4u : kLine, prio);
    outline({b.x + 2, b.y + 2, b.w - 4, b.h - 4}, 0xff000000u, prio);
    const Box in{b.x + 3, b.y + 3, b.w - 6, b.h - 6};
    if (gold) vgrad(in, 0xff211d10u, 0xff2a2414u, prio + 1);
    else if (lit) vgrad(in, 0xff0c1330u, 0xff121a3cu, prio + 1);
    else vgrad(in, 0xff020309u, 0xff0a0e1cu, prio + 1);
}

// The Skill window's frosted stone, mirrored tile by tile so it repeats without seams.
void KojinBlend::stone(const Box& b, int prio) {
    // Tiled across, mirrored every other tile; stretched down rather than repeated, since the
    // tile's top and bottom rows meet in a band of a different shade (and its last rows are darker).
    for (int tx = 0, i = 0; tx < b.w; tx += kStoneW, ++i) {
        const int w = std::min(kStoneW, b.w - tx);
        // A flipped tile shows its far edge first: take the cut from that side.
        const int sx = (i & 1) ? kStoneX + kStoneW - w : kStoneX;
        sub(kWindow, sx, kStoneY, w, kStoneH - 9, b.x + tx, b.y, w, b.h, prio, 0xff, 0x80, i & 1 ? kSprFlipU : 0);
    }
}

// The window's gold rail: its corners, and plain stretches of its edges between.
void KojinBlend::frameBorder(const Box& b, int t, int prio) {
    const auto [fw, fh] = ctx_.sprites->frameSize(KojinTask::kBank, kWindow);
    constexpr int C = 10, E = 40;
    const auto edge = [&](int sx, int sy, bool horiz, int x, int y, int len) {
        for (int at = 0; at < len; at += E * t / C) {
            const int n = std::min(E * t / C, len - at), sn = n * C / t;
            if (horiz) sub(kWindow, sx, sy, std::max(1, sn), C, x + at, y, n, t, prio);
            else sub(kWindow, sx, sy, C, std::max(1, sn), x, y + at, t, n, prio);
        }
    };
    edge(40, 0, true, b.x + t, b.y, b.w - 2 * t);
    edge(40, fh - C, true, b.x + t, b.y + b.h - t, b.w - 2 * t);
    edge(0, 160, false, b.x, b.y + t, b.h - 2 * t);
    edge(fw - C, 160, false, b.x + b.w - t, b.y + t, b.h - 2 * t);
    sub(kWindow, 0, 0, C, C, b.x, b.y, t, t, prio);
    sub(kWindow, fw - C, 0, C, C, b.x + b.w - t, b.y, t, t, prio);
    sub(kWindow, 0, fh - C, C, C, b.x, b.y + b.h - t, t, t, prio);
    sub(kWindow, fw - C, fh - C, C, C, b.x + b.w - t, b.y + b.h - t, t, t, prio);
}

void KojinBlend::panel(const Box& b, int prio) {
    const Box in{b.x + 7, b.y + 7, b.w - 14, b.h - 14};
    stone(in, prio);
    vgrad(in, 0xd80d1330u, 0xe8070b1cu, prio - 1);
    frameBorder(b, 7, prio - 2);
}

// A title plate: both end caps drawn `capW` wide, and a plain column of the plate stretched between them.
void KojinBlend::plate(const Box& b, bool gold, int prio, bool lit, int capW, std::uint8_t alpha) {
    const PlateArt& a = gold ? kGoldPlate : kSilver;
    const int sh = a.y1 - a.y0, cap = std::min(b.w / 2, capW);
    const std::uint8_t rgb = lit ? 0xa0 : 0x80;
    sub(kWindow, a.x0, a.y0, a.cap, sh, b.x, b.y, cap, b.h, prio, alpha, rgb);
    sub(kWindow, a.mid, a.y0, 1, sh, b.x + cap, b.y, b.w - 2 * cap, b.h, prio, alpha, rgb);
    sub(kWindow, a.x1 - a.cap, a.y0, a.cap, sh, b.x + b.w - cap, b.y, cap, b.h, prio, alpha, rgb);
}

// A gold plate button.
bool KojinBlend::button(const Box& b, const std::string& label, int prio, bool on, bool enabled, int px) {
    const bool hot = enabled && over(b);
    plate(b, true, prio + 1, on || hot, 12, enabled ? 0xff : 0x73);
    caps(b.x + b.w / 2, b.y + b.h / 2, label, px, enabled ? (on ? 0xfffff6c0u : kInk) : 0x73ffffffu, prio - 1, true, 1);
    return enabled && clicked(b);
}

int KojinBlend::buttonW(const std::string& label, int px) const {
    auto& self = const_cast<KojinBlend&>(*this);
    self.style_ = kCaps;
    const int w = textW(label, px, true) + 32;
    self.style_ = kSerif;
    return w;
}

// A small dark button with a silver line (the header's tools).
bool KojinBlend::tool(const Box& b, const std::string& label, int prio) {
    const bool hot = over(b);
    outline(b, 0xff000000u, prio);
    outline({b.x + 1, b.y + 1, b.w - 2, b.h - 2}, hot ? 0xff9aa8c8u : kLine, prio);
    fill(b.x + 2, b.y + 2, b.w - 4, b.h - 4, 0xff05070fu, prio + 1);
    sans(b.x + b.w / 2, b.y + b.h / 2, label, 12, hot ? kInk : kDim, prio - 1, false, 1);
    return clicked(b);
}

// An arrow button: navy with a dark edge; gold edge under the pointer, faint when it can't be used.
bool KojinBlend::arrow(const Box& b, const std::string& glyph, int px, int prio, bool enabled) {
    const bool hot = enabled && over(b);
    const std::uint32_t a = enabled ? 0xff000000u : 0x40000000u;
    roundBox(b, b.w == 25 ? 8 : b.w < 18 ? 2 : 3, (b.w < 18 ? 0x222c52u : 0x1a2140u) | a, (hot ? kGold : 0xff3a4566u & 0xffffffu) | a, prio);
    sans(b.x + b.w / 2, b.y + b.h / 2, glyph, px, ((hot ? kGold : kInk) & 0xffffffu) | a, prio - 1, false, 1);
    return enabled && clicked(b);
}

// A box with round corners (key caps, pills): a fill and a one-pixel edge.
void KojinBlend::roundBox(const Box& b, int r, std::uint32_t fillArgb, std::uint32_t lineArgb, int prio) {
    r = std::min({r, b.w / 2, b.h / 2});
    for (int y = 0; y < b.h; ++y) {
        const int dy = y < r ? r - y : y >= b.h - r ? y - (b.h - r - 1) : 0;
        const int in = dy ? r - int(std::sqrt(float(r * r - dy * dy)) + 0.5f) : 0;
        const int x0 = b.x + in, x1 = b.x + b.w - in;
        if (y == 0 || y == b.h - 1) { fill(x0, b.y + y, x1 - x0, 1, lineArgb, prio); continue; }
        const int prevIn = [&] { const int py = y - 1, pdy = py < r ? r - py : py >= b.h - r ? py - (b.h - r - 1) : 0;
                                 return pdy ? r - int(std::sqrt(float(r * r - pdy * pdy)) + 0.5f) : 0; }();
        const int nextIn = [&] { const int ny = y + 1, ndy = ny < r ? r - ny : ny >= b.h - r ? ny - (b.h - r - 1) : 0;
                                 return ndy ? r - int(std::sqrt(float(r * r - ndy * ndy)) + 0.5f) : 0; }();
        const int edge = std::max(1, std::max(prevIn, nextIn) - in);    // the curve's steps, so the edge stays closed
        fill(x0, b.y + y, std::min(edge, x1 - x0), 1, lineArgb, prio);
        fill(x1 - std::min(edge, x1 - x0), b.y + y, std::min(edge, x1 - x0), 1, lineArgb, prio);
        fill(x0 + edge, b.y + y, x1 - x0 - 2 * edge, 1, fillArgb, prio + 1);
    }
}

// A small padlock (locked classes).
void KojinBlend::lock(int x, int y, int prio) {
    constexpr std::uint32_t c = 0xffc9a64au;
    fill(x + 2, y, 5, 1, c, prio); fill(x + 1, y + 1, 1, 4, c, prio); fill(x + 7, y + 1, 1, 4, c, prio);
    fill(x, y + 5, 9, 7, c, prio);
    fill(x + 4, y + 7, 1, 3, 0xff3a2c10u, prio - 1);
}

void KojinBlend::itemIcon(int item, int x, int y, int size, int prio, std::uint8_t alpha) {
    if (item <= 0) return;
    const ysub::Rect r = t_.rect(kRItemIcons);
    const int i = item - 1;
    sub(r.frame, r.sx + (i % 0xc) * r.w, r.sy + (i / 0xc) * r.h, r.w, r.h, x, y, size, size * r.h / std::max<int>(1, r.w), prio, alpha);
}

void KojinBlend::skillIcon(int skill, int x, int y, int size, int prio, std::uint8_t alpha) {
    if (skill <= 0) return;
    const GameVersion v = ctx_.version;
    const std::uint32_t S = kSkills + U(skill) * 0x48;
    const ysub::Rect r = t_.rect(kRSkillIcons);
    sub(r.frame, r.sx + (logic::sdU8(S + 4, v) - 1) * r.w, r.sy + (logic::sdU8(S + 7, v) - 1) * r.h, r.w, r.h, x, y, size,
        size * r.h / std::max<int>(1, r.w), prio, alpha);
}

void KojinBlend::jobIcon(int job, int x, int y, int size, int prio, int look) {
    if (job <= 0) return;
    const ysub::Rect r = t_.rect(kRJobIcons);
    const int j = job - 1, cell = j / 2;
    const int sx = r.sx + (cell % 5) * r.w * 2 + (j % 2 ? r.w : 0), sy = r.sy + (cell / 5) * r.h;
    const int h = size * r.h / std::max<int>(1, r.w);
    if (look == 1) {                                 // a black figure with a thin light edge
        for (const auto [dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
            sub(r.frame, sx, sy, r.w, r.h, x + dx, y + dy, size, h, prio + 1, 0xa0, 0xc0);
        sub(r.frame, sx, sy, r.w, r.h, x, y, size, h, prio, 0xff, 0);
        return;
    }
    sub(r.frame, sx, sy, r.w, r.h, x, y, size, h, prio);
}

void KojinBlend::face(int id, int x, int y, int w, int h, int prio, std::uint8_t alpha) {
    const ysub::Rect r = t_.rect(kRFace);
    const int f = id - 1;
    sub(r.frame, r.sx + r.w * (f / 0xe), r.sy + r.h * (f % 0xe), r.w, r.h, x, y, w, h, prio, alpha);
}

void KojinBlend::statIcon(int kind, int x, int y, int prio) {
    const ysub::Rect r = t_.rect(kRItemStatIcons);
    sub(r.frame, r.sx, r.sy + (kind - 8) * 0x10, r.w, r.h, x, y, r.w, r.h, prio);
}

// =================================================================== input ===
bool KojinBlend::over(const Box& b) const { return b.in(mx_, my_); }
bool KojinBlend::pressed(const Box& b) const { return press_ && b.in(mx_, my_); }
// A click lands on release, where it was pressed, unless the press became a drag.
bool KojinBlend::clicked(const Box& b) const { return release_ && !dragged_ && b.in(mx_, my_) && b.in(downX_, downY_); }

// =================================================================== model ===
int KojinBlend::unit() const { return t_.shownUnit(); }
std::uint32_t KojinBlend::rec() const { return GameWork::unitRecord(unit()); }

KojinBlend::Snap KojinBlend::snap() const {
    const stats::Derived& d = t_.derived_;
    Snap s;
    auto& v = s.v;
    v[0] = d.s16(0x14); v[1] = d.s16(0x1a); v[2] = d.s32(0x20) / 0xe10;
    v[3] = d.s16(0x38); v[4] = d.s16(0x3a) + (d.u16(0x3c) >> 1); v[5] = d.u8(0x37); v[6] = d.u8(0x44);
    v[7] = d.u16(0x34); v[8] = d.u8(0x4c); v[9] = d.u8(0x4d); v[10] = d.u8(0x4f); v[11] = d.u8(0x4e);
    v[12] = d.u8(0x50); v[13] = d.u8(0x2c); v[14] = grade(kMove, d.u16(0x2e), true);
    v[15] = grade(kWait, d.s16(0x18), false); v[16] = grade(kWait, d.s16(0x1e), false);
    for (std::size_t c = 0; c < 7; ++c) v[17 + c] = d.u8(4 + c);
    v[24] = t_.total_;
    return s;
}

void KojinBlend::recompute() { t_.computeUnit(unit(), -1, false, 0); }

// The summary if `change` were made, without keeping it.
KojinBlend::Snap KojinBlend::trial(const std::function<void()>& change) {
    const GameWork save = ctx_.work;
    change();
    recompute();
    const Snap s = snap();
    ctx_.work = save;
    t_.computeUnit(unit(), -1, true, 2);
    recompute();
    return s;
}

std::string KojinBlend::unitName(int id) const {
    const auto& names = exe::strings(ctx_.version, ctx_.version == GameVersion::Tw ? 0x6162ac : 0x501d1c, 0x200);
    return id >= 0 && std::size_t(id) < names.size() ? trimmed(names[std::size_t(id)]) : std::string();
}

std::string KojinBlend::className(int job) const {
    const auto& names = exe::strings(ctx_.version, ctx_.version == GameVersion::Tw ? 0x616aac : 0x50251c, 0x200);
    return job > 0 && std::size_t(job) < names.size() ? trimmed(names[std::size_t(job)]) : std::string();
}

std::string KojinBlend::itemName(int item) const {
    const auto& names = exe::strings(ctx_.version, ctx_.version == GameVersion::Tw ? 0x6e3150 : 0x5fbaa8, 0x169);
    return item > 0 && std::size_t(item) < names.size() ? trimmed(names[std::size_t(item)]) : std::string();
}

// A YsText entry as one string, its lines joined.
std::string KojinBlend::passage(ysub::TextList& list, std::map<int, std::string>& cache, int entry) {
    if (const auto it = cache.find(entry); it != cache.end()) return it->second;
    list.select(entry, 0);
    std::string out;
    for (const std::string& l : list.lines()) {
        std::string line = trimmed(l);
        // (and the full-width spaces the Japanese pads its lines with)
        for (bool cut = true; cut;) {
            cut = false;
            while (!line.empty() && line.front() == ' ') { line.erase(line.begin()); cut = true; }
            if (line.starts_with("\u3000")) { line.erase(0, 3); cut = true; }
            if (line.ends_with("\u3000")) { line.resize(line.size() - 3); cut = true; }
        }
        if (line.empty()) continue;
        // The game's lines are cut to its box: joined again with a space between words (none
        // between CJK characters), and a "★Warrior line★" heading kept on a line of its own.
        const auto ascii = [](unsigned char ch) { return ch < 0x80; };
        if (!out.empty())
            out += line.starts_with("★") || out.ends_with("★") ? "\n"
                   : ascii(static_cast<unsigned char>(out.back())) && ascii(static_cast<unsigned char>(line.front())) ? " " : "";
        out += line;
    }
    return cache[entry] = out;
}

KojinBlend::SkillInfo KojinBlend::skillInfo(int id) {
    const GameVersion v = ctx_.version;
    SkillInfo s;
    skillText_.select(id, 0);
    const auto& lines = skillText_.lines();
    std::string head = lines.empty() ? std::string() : trimmed(lines.front());
    // "Name (Active)" / "名前（使用）": the kind in brackets at the end of the first line.
    for (const auto& [open, close] : {std::pair<std::string, std::string>{"(", ")"}, {"（", "）"}}) {
        const std::size_t at = head.rfind(open);
        if (at == std::string::npos || head.size() < close.size() || head.compare(head.size() - close.size(), close.size(), close) != 0) continue;
        const std::string kind = head.substr(at + open.size(), head.size() - close.size() - at - open.size());
        s.passive = kind.find("assive") != std::string::npos || kind.find("パッシブ") != std::string::npos || kind.find("常時") != std::string::npos;
        head = trimmed(head.substr(0, at));
        break;
    }
    s.name = head;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string line = trimmed(lines[i]);
        if (line.empty()) continue;
        if (!s.text.empty() && line.find(' ') != std::string::npos) s.text += ' ';
        s.text += line;
    }
    const std::uint32_t S = kSkills + U(id) * 0x48;
    s.cat = logic::sdU8(S + 4, v); s.tier = logic::sdU8(S + 7, v); s.mp = logic::sdS16(S + 8, v);
    s.price = logic::sdS32(S + 0x40, v); s.prep = std::uint16_t(logic::sdS16(S + 0xa, v));
    s.recov = std::uint16_t(logic::sdS16(S + 0xe, v)); s.aim = logic::sdU8(S + 0x18, v); s.range = logic::sdU8(S + 0x19, v);
    return s;
}

int KojinBlend::itemType(int item) const { return logic::sdS16(kItems + 2 + U(item) * 0x38, ctx_.version) & 0xffff; }
int KojinBlend::ownerOf(int item) const { return ctx_.work.u16(kStore + U(item) * 2) >> 1 & 0x7f; }
int KojinBlend::itemState(int item) const { return ctx_.work.u16(kStore + U(item) * 2) >> 8 & 0xf; }
// Can the shown student put it on: free in storage and fit for the class, or worn by someone else?
bool KojinBlend::wearable(int item) const {
    const int w = ctx_.work.u16(kStore + U(item) * 2), st = w >> 8 & 0xf;
    return (w & 1) && (st == 1 || (st == 3 && ownerOf(item) != unit()));
}
int KojinBlend::gearIn(int k) const { return ctx_.work.u16(rec() + 0x62 + U(k) * 2); }
int KojinBlend::skillIn(int k) const { return ctx_.work.u16(rec() + 0x52 + U(k) * 2); }
int KojinBlend::skillState(int id) const { return ctx_.work.u8(rec() + 0xb8 + U(id - 1) * 0xc); }
// The slot an item goes in: the chosen one if it fits, else the first empty one that takes it.
int KojinBlend::gearTarget(int item) const {
    if (focus_ == Kind::Gear) return t_.canEquip(rec(), focusK_, item) ? focusK_ : -1;
    for (int k = 0; k < t_.itemSlots_; ++k) if (!gearIn(k) && t_.canEquip(rec(), k, item)) return k;
    return -1;
}
int KojinBlend::skillTarget() const {
    if (focus_ == Kind::Skill) return focusK_;
    for (int k = 0; k < t_.skillSlots_; ++k) if (!skillIn(k)) return k;
    return -1;
}
bool KojinBlend::isOpen(int job) const { return ctx_.work.s8(GameWork::unitProgress(unit()) + kJobOpen + U(job)) != 0; }
int KojinBlend::jobLevel(int job) const {
    return std::max<int>(0, ctx_.work.s8(GameWork::unitProgress(unit()) + kJobLevel + U(job) - 1));
}

void KojinBlend::equipItem(int k, int item) {
    GameWork& w = ctx_.work;
    const std::uint32_t slot = rec() + 0x62 + U(k) * 2;
    if (const int old = w.u16(slot)) w.set16(kStore + U(old) * 2, (w.u16(kStore + U(old) * 2) & 0xf0ff) | 0x100);
    w.set16(slot, item);
    if (item) {
        const int st = (w.u16(kStore + U(item) * 2) & 0xf0ff) | 0x300;
        w.set16(kStore + U(item) * 2, (st & 0xff01) | (unit() & 0x7f) << 1);
    }
    recompute();
}

void KojinBlend::equipSkill(int k, int id) {
    GameWork& w = ctx_.work;
    const std::uint32_t slot = rec() + 0x52 + U(k) * 2;
    if (const int old = w.u16(slot)) w.set8(rec() + 0xb8 + U(old - 1) * 0xc, 3);
    w.set16(slot, id);
    if (id) w.set8(rec() + 0xb8 + U(id - 1) * 0xc, 6);
    recompute();
}

// Takes `item` off whichever student wears it, back to storage.
void KojinBlend::takeFrom(int item) {
    GameWork& w = ctx_.work;
    const std::uint32_t r = GameWork::unitRecord(ownerOf(item));
    for (std::uint32_t k = 0; k < 8; ++k) if (w.u16(r + 0x62 + k * 2) == item) w.set16(r + 0x62 + k * 2, 0);
    w.set16(kStore + U(item) * 2, (w.u16(kStore + U(item) * 2) & 0xf0ff) | 0x100);
    recompute();
}

void KojinBlend::putOn(int k, int item) {
    if (itemState(item) == 3 && ownerOf(item) != unit()) takeFrom(item);
    equipItem(k, item);
}

// The classic 技能 tab's learning (KojinTask::tabSkills): paid from shared XP, counted in the score.
bool KojinBlend::learn(int id) {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const int price = logic::sdS32(kSkills + U(id) * 0x48 + 0x40, v), u = unit();
    if (w.s32(kPoints) < price || logic::unitSummary(w, v, u).toCap == 0) return false;
    w.set8(rec() + 0xb8 + U(id - 1) * 0xc, 3);
    const std::uint32_t score = GameWork::unitProgress(u) + 0x11c;
    w.set32(score, std::int64_t(w.s32(score)) + price);
    w.set8(rec() + 0x50, logic::unitSummary(w, v, u).rank);
    w.set32(kPoints, w.s32(kPoints) - price);
    t_.computeUnit(u, -1, true, 2);
    recompute();
    return true;
}

// As the classic 職業変更 tab's 変更: the job changes and gear and skills come off.
void KojinBlend::changeClass(int job) {
    GameWork& w = ctx_.work;
    const int u = unit();
    w.set16(rec() + 6, job);
    t_.job_ = static_cast<std::int16_t>(job);
    t_.computeUnit(u, job, true, 1);
    t_.computeUnit(u, -1, true, 2);
    recompute();
}

void KojinBlend::showStudent(int i) {
    if (t_.unitCount_ <= 0) return;
    i = (i % t_.unitCount_ + t_.unitCount_) % t_.unitCount_;
    ctx_.work.set16(kShown, i);
    focus_ = card_ = shown_ = Kind::None;
    classes_ = false; classPick_ = classShown_ = 0; adds_ = {}; learnAsk_ = 0;
    havePreview_ = false; previewKey_ = ~0ull;
    t_.computeUnit(unit(), -1, true, 2);
    recompute();
}

// ============================================================== stats up ===
int KojinBlend::statLevel(int c) const { return ctx_.work.u8(rec() + 0xc + U(c) * 8); }
int KojinBlend::statCost(int c, int add) const {
    if (!add) return 0;
    return logic::statProgress(ctx_.work, statLevel(c) + add, 0).levelStart - ctx_.work.s32(rec() + 0x10 + U(c) * 8);
}
int KojinBlend::totalCost(const std::array<int, 7>& adds) const {
    int sum = 0;
    for (int c = 0; c < 7; ++c) sum += statCost(c, adds[std::size_t(c)]);
    return sum;
}
// The classic Stats Up rule: under the level cap, and the plan's cost within shared XP and capacity.
bool KojinBlend::canRaise(int c) const {
    const int cap = logic::levelCap(ctx_.work);
    if (statLevel(c) + adds_[std::size_t(c)] >= cap) return false;
    auto next = adds_;
    ++next[std::size_t(c)];
    const int need = totalCost(next);
    return need <= ctx_.work.s32(kPoints) && need <= logic::unitSummary(ctx_.work, ctx_.version, unit()).toCap;
}
bool KojinBlend::stepStat(int c, bool up) {
    if (up ? !canRaise(c) : !adds_[std::size_t(c)]) return false;
    adds_[std::size_t(c)] += up ? 1 : -1;
    previewKey_ = ~0ull;
    return true;
}
// Sets each planned level and its XP, then the student's level (Engage comes from it).
void KojinBlend::raiseStats() {
    GameWork& w = ctx_.work;
    std::array<int, 7> costs{};
    for (int c = 0; c < 7; ++c) costs[std::size_t(c)] = statCost(c, adds_[std::size_t(c)]);
    for (std::uint32_t c = 0; c < 7; ++c) {
        const int exp = w.s32(rec() + 0x10 + c * 8);
        w.set8(rec() + 0xc + c * 8, statLevel(int(c)) + adds_[c]);
        w.set32(rec() + 0x10 + c * 8, exp + costs[c]);
    }
    w.set8(rec() + 0x50, logic::unitSummary(w, ctx_.version, unit()).rank);
}
// The classic 決定 (KojinTask::tabStats).
void KojinBlend::applyStats() {
    GameWork& w = ctx_.work;
    const int u = unit(), spent = totalCost(adds_);
    const int rankBefore = w.u8(rec() + 0x50);
    raiseStats();
    w.set32(kPoints, w.s32(kPoints) - spent);
    const std::uint32_t score = GameWork::unitProgress(u) + 0x11c;
    w.set32(score, std::int64_t(w.s32(score)) + spent);
    adds_ = {};
    t_.voice(u, 7);
    if (rankBefore < w.u8(rec() + 0x50)) ctx_.audio->sePlay(0xb);
    t_.computeUnit(u, -1, true, 2);
    recompute();
    previewKey_ = ~0ull;
}

// The summary if `kind` `id` were put on (and bought first, for a skill); null when it can't be.
KojinBlend::Snap* KojinBlend::previewFor(Kind kind, int id) {
    if (kind == Kind::Item) {
        const int k = gearTarget(id);
        if (!wearable(id) || k < 0) return nullptr;
        preview_ = trial([&] { putOn(k, id); });
        return &preview_;
    }
    const int st = skillState(id), k = skillTarget();
    if (st == 3 && k >= 0) { preview_ = trial([&] { equipSkill(k, id); }); return &preview_; }
    if ((st == 1 || st == 2 || st == 4) && learnAsk_ != -1) {
        const int price = logic::sdS32(kSkills + U(id) * 0x48 + 0x40, ctx_.version);
        if (ctx_.work.s32(kPoints) < price || logic::unitSummary(ctx_.work, ctx_.version, unit()).toCap == 0) return nullptr;
        preview_ = trial([&] { if (learn(id) && skillState(id) == 3 && k >= 0) equipSkill(k, id); });
        return &preview_;
    }
    return nullptr;
}

// =================================================================== frame ===
void KojinBlend::frame() {
    const auto& m = ctx_.mouse();
    const auto& in = ctx_.engine.input();
    mx_ = m.x; my_ = m.y;
    press_ = (m.pressed & legacy::kMouseLeft) != 0;
    release_ = (m.released & legacy::kMouseLeft) != 0;
    held_ = (m.held & legacy::kMouseLeft) != 0;
    wheel_ = m.wheel;
    if (press_) { downX_ = mx_; downY_ = my_; dragged_ = false; }
    if (held_ && downX_ >= 0 && std::abs(mx_ - downX_) + std::abs(my_ - downY_) > 8) dragged_ = true;
    wantKind_ = wantId_ = 0;
    tipAttr_ = -1;

    // Keys: left / right step the student; Escape closes what is open, then the screen.
    if (in.pressed(legacy::Action::Left)) { ctx_.audio->sePlay(kSe); showStudent(ctx_.work.s16(kShown) - 1); stripScroll_ = -1; }
    if (in.pressed(legacy::Action::Right)) { ctx_.audio->sePlay(kSe); showStudent(ctx_.work.s16(kShown) + 1); stripScroll_ = -1; }
    if (in.pressed(legacy::Action::Menu) || in.pressed(legacy::Action::Cancel)) {
        ctx_.audio->sePlay(kSe);
        if (card_ != Kind::None) card_ = Kind::None;
        else if (focus_ != Kind::None || browse_ || classes_) { focus_ = Kind::None; browse_ = 0; classes_ = false; classPick_ = classShown_ = 0; }
        else if (std::any_of(adds_.begin(), adds_.end(), [](int a) { return a != 0; })) adds_ = {};
        else ctx_.work.set16(kKojinState, 2);
        previewKey_ = ~0ull;
    }

    drawBackground();
    drawHeader();
    drawMain();      // before the top: the main panel's choices feed the preview
    drawSide();
    drawTop();
    drawStats();
    drawKeys();

    // Next frame's preview: what the pointer is on, else the class picked, else the planned raise.
    const bool anyAdd = std::any_of(adds_.begin(), adds_.end(), [](int a) { return a != 0; });
    int kind = wantKind_, id = wantId_;
    if (!kind && classes_ && classPick_) { kind = 3; id = classPick_; }
    if (!kind && card_ != Kind::None) { kind = 2; id = card_ == Kind::Item ? cardId_ : 0x10000 | cardId_; }
    std::uint64_t key = std::uint64_t(kind) << 40 | std::uint64_t(std::uint32_t(id)) << 8 | std::uint64_t(focusK_ & 0xff);
    for (int a : adds_) key = key * 131 + std::uint64_t(a);
    key ^= std::uint64_t(unit()) << 56;
    if (key != previewKey_) {
        previewKey_ = key;
        havePreview_ = false;
        if (kind == 1 && focus_ == Kind::Gear) { preview_ = trial([&] { putOn(focusK_, id); }); havePreview_ = true; }
        else if (kind == 1 && focus_ == Kind::Skill) { preview_ = trial([&] { equipSkill(focusK_, id); }); havePreview_ = true; }
        else if (kind == 2) havePreview_ = previewFor(id & 0x10000 ? Kind::SkillCard : Kind::Item, id & 0xffff) != nullptr;
        else if (kind == 3) {
            t_.computeUnit(unit(), id, false, 2);
            preview_ = snap();
            recompute();
            havePreview_ = true;
        } else if (anyAdd) { preview_ = trial([&] { raiseStats(); }); havePreview_ = true; }
    }
    if (!held_) holdC_ = -1;
    if (release_) downX_ = downY_ = -1;
}

// ============================================================ background ===
void KojinBlend::drawBackground() {
    stone({0, 0, 1024, 768}, kPBack);
    vgrad({0, 0, 1024, 768}, 0xe605070fu, 0xf00a0f20u, kPBack - 1);
}

void KojinBlend::drawHeader() {
    const Box head{0, 0, 1024, 48};
    stone(head, kPPanel);
    vgrad(head, 0xf210162cu, 0xf20a0e1du, kPPanel - 1);
    fill(0, 48, 1024, 1, 0xff000000u, kPFrame);
    fill(0, 49, 1024, 1, 0xff8d99b8u, kPFrame);
    fill(0, 50, 1024, 1, 0xff2c3450u, kPFrame);
    for (int i = 0; i < 8; ++i) fill(0, 51 + i, 1024, 1, std::uint32_t(0xb0 - i * 0x16) << 24, kPFrame);

    // Where we are: Students › Status › Equipment.
    int x = 18;
    const std::array<std::string, 3> crumbs{tr("生徒"), tr("ステータス"), tr("装備画面")};
    for (std::size_t i = 0; i < crumbs.size(); ++i) {
        if (i) x += 6 + text(x + 6, 24, "›", 19, kDim, kPText) + 6;
        x += caps(x, 24, crumbs[i], 19, kGold, kPText);
    }

    // Right to left: Back, shared XP, the count, then the student strip fills what's left.
    const std::string back = tr("戻る");
    style_ = kSans;
    const int bw = textW(back, 12) + 18;
    style_ = kSerif;
    if (tool({1006 - bw, 12, bw, 25}, back, kPItem)) { ctx_.audio->sePlay(kSe); ctx_.work.set16(kKojinState, 2); }
    const std::string label = tr("共有XP"), points = fmt(ctx_.work.s32(kPoints));
    const int xpRight = 1006 - bw - 14, xpW = std::max(textW(label, 13), textW(points, 15, true));
    text(xpRight - xpW / 2, 13, label, 13, kDim, kPText, false, 1, false);
    text(xpRight - xpW / 2, 31, points, 15, kInk, kPText, true, 1, false);
    const int n = t_.unitCount_, cur = ctx_.work.s16(kShown);
    const int whoX = xpRight - xpW - 14 - 38;
    text(whoX + 19, 24, std::to_string(cur + 1) + " / " + std::to_string(n), 12, kDim, kPText, false, 1, false);

    const Box left{x + 20, 5, 20, 38}, right{whoX - 24, 5, 20, 38};
    const Box view{left.x + 24, 2, right.x - 4 - (left.x + 24), 44};
    constexpr int kPitch = 43;
    const int full = n * kPitch - 3 + 24, maxScroll = std::max(0, full - view.w);
    if (stripScroll_ < 0) stripScroll_ = std::clamp(cur * kPitch + 20 - view.w / 2, 0, maxScroll);
    stripScroll_ = std::min(stripScroll_, maxScroll);
    if (arrow(left, "◀", 10, kPItem, stripScroll_ > 0)) stripScroll_ = std::max(0, stripScroll_ - view.w * 4 / 5);
    if (arrow(right, "▶", 10, kPItem, stripScroll_ < maxScroll)) stripScroll_ = std::min(maxScroll, stripScroll_ + view.w * 4 / 5);
    if (over(view) && wheel_) stripScroll_ = std::clamp(stripScroll_ - wheel_ * kPitch, 0, maxScroll);
    static int lastX = 0;
    if (held_ && dragged_ && view.in(downX_, downY_)) { if (lastX) stripScroll_ = std::clamp(stripScroll_ - (mx_ - lastX), 0, maxScroll); lastX = mx_; }
    else lastX = 0;
    const ysub::Rect fr = t_.rect(kRFace);
    for (int i = 0; i < n; ++i) {
        const int fx = view.x + 12 + i * kPitch - stripScroll_;
        const int l = std::max(fx, view.x), r = std::min(fx + 40, view.x + view.w);
        if (r <= l) continue;
        const Box f{fx, 5, 40, 38};
        const bool on = i == cur, hot = over(f) && view.in(mx_, my_);
        // Faces fade out over the strip's last 14px each side.
        const int edge = std::min({l + (r - l) / 2 - view.x, view.x + view.w - (l + (r - l) / 2), 14});
        const int fade = std::clamp(edge * 255 / 14, 0, 255);
        const std::uint8_t a = std::uint8_t((on ? 255 : hot ? 230 : 140) * fade / 255);
        const std::uint32_t line = ((on ? kGold : 0xff3a4566u) & 0xffffffu) | std::uint32_t(fade) << 24;
        if (fx >= view.x && fx + 40 <= view.x + view.w) outline(f, line, kPWell);
        else { fill(l, 5, r - l, 1, line, kPWell); fill(l, 42, r - l, 1, line, kPWell); }
        // The face, cut to the strip.
        const int id = t_.units_[std::size_t(i)] - 1;
        const int cl = std::clamp(l - (fx + 1), 0, 38), cr = std::clamp(fx + 39 - r, 0, 38);
        if (38 - cl - cr > 0)
            sub(fr.frame, fr.sx + fr.w * (id / 0xe) + cl * fr.w / 38, fr.sy + fr.h * (id % 0xe), (38 - cl - cr) * fr.w / 38, fr.h,
                fx + 1 + cl, 6, 38 - cl - cr, 36, kPItem, a);
        if (clicked(f) && view.in(mx_, my_) && i != cur) { ctx_.audio->sePlay(kSe); showStudent(i); }
    }
}

void KojinBlend::drawKeys() {
    fill(0, 731, 1024, 1, 0xff3a4566u, kPFrame);
    fill(0, 732, 1024, 36, 0xff0a0e1du, kPPanel - 1);
    const auto keyW = [&](const std::string& k) { style_ = kSans; const int w = std::max(18, textW(k, 11, true) + 10); style_ = kSerif; return w; };
    const auto key = [&](int x, const std::string& k, const std::string& label) {
        const int w = keyW(k);
        roundBox({x, 742, w, 18}, 9, 0xff0a0e1du, 0xff9aa8c8u, kPWell);
        sans(x + w / 2, 751, k, 11, kInk, kPText, true, 1);
        return w + 6 + text(x + w + 6, 750, label, 14, kDim, kPText, false, 0, false);
    };
    const auto width = [&](const std::string& k, const std::string& label) { return keyW(k) + 6 + textW(label, 14); };
    key(18, "Esc", tr("戻る"));
    const std::string k2 = "← →", l2 = tr("生徒を切り替え"), k3 = tr("クリック"), l3 = tr("選ぶ");
    int x = 1006 - width(k3, l3);
    key(x, k3, l3);
    key(x - 22 - width(k2, l2), k2, l2);
}

// =================================================================== top ===
// The full name: as the language pack has it ("{name}<id>" in port.tsv), else from the glossary's
// profile, else the short name.
std::string KojinBlend::fullName(int id) const {
    if (const std::string key = "{name}" + std::to_string(id), s = tr(key); s != key) return s;
    const std::string shortName = unitName(id);
    const auto it = profileNames_.find(shortName);
    return it != profileNames_.end() ? it->second : shortName;
}

void KojinBlend::drawTop() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const int id = unit();
    const Snap s = snap();
    const Snap* p = havePreview_ ? &preview_ : nullptr;
    const logic::UnitSummary sum = logic::unitSummary(w, v, id);
    const int job = w.s16(rec() + 6);

    // Portrait, in the gold rail.
    const Box port{22, 62, 162, 192};
    frameBorder(port, 6, kPFrame);
    sub(id + 0xb, 19, 10, 242, 290, port.x + 6, port.y + 6, 150, 180, kPItem);
    fill(port.x + 6, port.y + 6, 150, 180, 0xff0d1326u, kPWell);

    // The name as text (in small capitals where the script has them), else the portrait's own name plate.
    if (const std::string name = fullName(id); !name.empty()) {
        int px = 25;
        style_ = kCaps;
        while (px > 16 && textW(name, px, true) > 330) --px;
        style_ = kSerif;
        caps(188, 78, name, px, kInk, kPText);
    } else {
        // The plate's band is black under white letters: added on, only the letters show.
        ctx_.draw.sprite({.bank = KojinTask::kBank, .frame = static_cast<std::uint16_t>(id + 0xb), .x = 188, .y = 64, .w = 316, .h = 24,
                          .src = {26, 362, 248, 379}, .flags = static_cast<std::uint16_t>(kSprBank | kSprSrcRect), .blend = kBlendAdd,
                          .color = {0x80, 0x80, 0x80, 0xff}, .priority = kPItem});
    }

    // HP, MP, Engage with gauges; a preview shows where they'd go.
    const auto bar = [&](int top, const std::string& label, int lpx, int i, int pct, std::uint32_t c0, std::uint32_t c1, bool minutes) {
        const int y = top + 14;
        style_ = kCaps;
        while (lpx > 9 && textW(label, lpx, true) > 56) --lpx;       // fits before the gauge
        style_ = kSerif;
        caps(188, y, label, lpx, kInk, kPText);
        // The value (and where a preview takes it) right-aligned; the gauge takes the room left.
        const int now = s.v[std::size_t(i)];
        const bool moved = p && p->v[std::size_t(i)] != now;
        const std::string nowText = std::to_string(now), unitText = minutes ? tr("分") : std::string();
        const std::string toText = moved ? "› " + std::to_string(p->v[std::size_t(i)]) : std::string();
        const int valueW = textW(nowText, 19, true) + (minutes ? 4 + textW(unitText, 12, true) : 0) + (moved ? 5 + textW(toText, 19, true) : 0);
        const int gw = 518 - std::max(76, valueW) - 8 - 250;
        outline({249, top + 9, gw + 2, 11}, kLine, kPWellLine);
        outline({250, top + 10, gw, 9}, 0xff000000u, kPWellLine);
        fill(251, top + 11, gw - 2, 7, 0xff0a0d18u, kPWellLine + 1);
        vgrad({251, top + 11, (gw - 2) * std::clamp(pct, 0, 100) / 100, 7}, c0, c1, kPItem);
        int x = 518;
        if (moved) {
            const bool better = p->v[std::size_t(i)] > now;       // (more Engage time is longer on the field)
            x -= text(x, y, toText, 19, better ? kUp : kDown, kPText, true, 2) + 5;
        }
        if (minutes) x -= text(x, y + 2, unitText, 12, kDim, kPText, true, 2, false) + 4;
        text(x, y, nowText, 19, kInk, kPText, true, 2);
    };
    bar(96, "HP", 17, 0, 100, 0xff9be36au, 0xff3f8f2au, false);
    bar(122, "MP", 17, 1, 100, 0xffd985ffu, 0xff7a39b8u, false);
    bar(148, tr("エンゲージ"), 13, 2, s.v[2] * 100 / 99, 0xff8ff0ffu, 0xff2a95b0u, true);

    // Class, level and the XP to the next.
    const Box row{188, 180, 330, 46};
    outline(row, 0xff000000u, kPWell);
    outline({row.x + 1, row.y + 1, row.w - 2, row.h - 2}, kLine, kPWell);
    fill(row.x + 2, row.y + 2, row.w - 4, row.h - 4, 0xff020309u, kPWell + 1);
    jobIcon(job, 197, 185, 25, kPItem);
    text(230, 203, className(job), 14, kInk, kPText, true);
    const int spend = totalCost(adds_);
    const int rankAfter = spend ? logic::rankForTotal(sum.total + spend, v) : sum.rank;
    const auto nextFor = [&](int rank, int total) { return std::max(0, logic::sdS32(0x6252fc + U(rank + 1) * 4, v) - total); };
    int x = 509;
    if (spend) x -= text(x, 204, "› " + (sum.toCap - spend <= 0 ? tr("満") : fmt(nextFor(rankAfter, sum.total + spend))), 12, kUp, kPText, false, 2) + 4;
    x -= text(x, 204, sum.toCap == 0 ? tr("容量いっぱい") : tr("次") + " " + fmt(nextFor(sum.rank, sum.total)), 12, kDim, kPText, false, 2) + 8;
    if (rankAfter != sum.rank) x -= text(x, 203, "› " + std::to_string(rankAfter), 20, kUp, kPText, true, 2) + 4;
    x -= text(x, 203, std::to_string(sum.rank), 20, kInk, kPText, true, 2) + 3;
    text(x, 204, "Lv.", 14, kDim, kPText, false, 2);

    // The class's figure on a lit floor, and the student arrows under it.
    jobIcon(classes_ && classPick_ ? classPick_ : job, 534, 60, 100, kPItem);
    for (int i = 0; i < 45; ++i) {
        const int a = 0xc0 * (45 - i) / 45;
        fill(584 - i - 1, 213, 1, 2, std::uint32_t(a) << 24 | 0x5a70b8u, kPItem + 1);
        fill(584 + i, 213, 1, 2, std::uint32_t(a) << 24 | 0x5a70b8u, kPItem + 1);
    }
    if (arrow({539, 221, 25, 21}, "◀", 12, kPItem, true)) { ctx_.audio->sePlay(kSe); showStudent(w.s16(kShown) - 1); stripScroll_ = -1; }
    if (arrow({604, 221, 25, 21}, "▶", 12, kPItem, true)) { ctx_.audio->sePlay(kSe); showStudent(w.s16(kShown) + 1); stripScroll_ = -1; }
}

void KojinBlend::drawStats() {
    const Snap s = snap();
    const Snap* p = havePreview_ ? &preview_ : nullptr;
    const std::array<int, 3> colX{644, 769, 893};
    const std::array<std::string, 3> caps{tr("能力"), tr("戦闘"), tr("耐性・速さ")};
    for (std::size_t c = 0; c < 3; ++c) {
        const Box col{colX[c], 58, 119, 181};
        fill(col.x + 1, col.y + 1, col.w - 2, col.h - 2, 0xc0020309u, kPWell);
        outline(col, kLine, kPWellLine);
        plate({col.x, col.y, col.w, 19}, false, kPItem, false, 14);
        this->caps(col.x + col.w / 2, col.y + 10, caps[c], 12, kInk, kPText, true, 1);
    }
    const auto rowTop = [](int row) { return 80 + row * 39 / 2; };
    // One value: now, and where the preview would take it.
    const auto value = [&](int col, int row, const std::string& label, int i, int kind, int attr = -1) {
        const int x = colX[std::size_t(col)], y = rowTop(row) + 10;
        const Box name{x + 4, y - 9, textW(label, 12) + 4, 18};
        const bool tip = attr >= 0 && over(name);
        if (tip) { tipAttr_ = attr; tipY_ = y; }
        text(x + 6, y + 1, label, 12, tip ? kGold : kDim, kPText, false, 0, false);   // on the value's baseline
        const auto show = [&](int vv) {
            if (kind == 1) return std::string(kGrades[std::size_t(std::clamp(vv, 0, 7))]);
            if (kind == 2 && vv == 999) return tr("必中");
            return fmt(vv);
        };
        int right = x + 113 - (attr >= 0 ? 35 : 0);
        const int now = s.v[std::size_t(i)];
        if (p && p->v[std::size_t(i)] != now) {
            const bool better = kind == 1 ? p->v[std::size_t(i)] < now : p->v[std::size_t(i)] > now;
            right -= text(right, y, "› " + show(p->v[std::size_t(i)]), 14, better ? kUp : kDown, kPText, true, 2, false) + 2;
        }
        text(right, y, show(now), 14, kInk, kPText, true, 2, false);
    };
    for (int c = 0; c < 7; ++c) {
        value(0, c, tr(kStatNames[c]), 17 + c, 0, c);
        // ▲ / ▼: a press steps once, holding keeps stepping, faster the longer it's held.
        const int top = rowTop(c) + 2;
        const Box up{colX[0] + 82, top, 15, 15}, down{colX[0] + 98, top, 15, 15};
        const bool canUp = canRaise(c), canDown = adds_[std::size_t(c)] != 0;
        arrow(up, "▲", 8, kPItem, canUp);
        arrow(down, "▼", 8, kPItem, canDown);
        for (const auto& [b, isUp, en] : {std::tuple{up, true, canUp}, std::tuple{down, false, canDown}}) {
            if (!en || !pressed(b)) continue;
            ctx_.audio->sePlay(kSe);
            stepStat(c, isUp);
            holdC_ = c; holdUp_ = isUp; holdTimer_ = 23; holdN_ = 0;
        }
    }
    if (holdC_ >= 0 && held_ && --holdTimer_ <= 0) {
        if (stepStat(holdC_, holdUp_)) holdTimer_ = std::max(2, 7 - holdN_++ / 3);
        else holdC_ = -1;
    }
    value(0, 7, tr("合計"), 24, 0);
    value(1, 0, tr("命中"), 3, 2); value(1, 1, tr("威力"), 4, 0); value(1, 2, tr("射程"), 5, 0); value(1, 3, tr("範囲"), 6, 0);
    value(1, 4, tr("硬直"), 7, 0); value(1, 5, tr("回避"), 8, 0); value(1, 6, tr("装甲"), 9, 0); value(1, 7, tr("結界"), 10, 0);
    value(2, 0, tr("肉体"), 11, 0); value(2, 1, tr("意志"), 12, 0); value(2, 2, tr("視界"), 13, 0); value(2, 3, tr("移動"), 14, 1);
    value(2, 4, tr("HP回復"), 15, 1); value(2, 5, tr("MP回復"), 16, 1);

    // The raise waiting for Apply, in the third column's free rows.
    if (const int spend = totalCost(adds_)) {
        text(899, rowTop(6) + 8, tr("上昇コスト"), 12, kDim, kPText, false, 0, false);
        text(1006, rowTop(6) + 8, fmt(spend) + " XP", 12, kGold, kPText, true, 2, false);
        if (button({897, rowTop(7) - 1, 76, 19}, tr("決定"), kPItem, false, true, 11)) { ctx_.audio->sePlay(kSe); applyStats(); }
        if (button({975, rowTop(7) - 1, 33, 19}, "×", kPItem, false, true, 11)) { ctx_.audio->sePlay(kSe); adds_ = {}; previewKey_ = ~0ull; }
    }
    if (tipAttr_ >= 0) drawStatTip(tipAttr_, tipY_);
}

// What an attribute raises: the Boost icons for the student's class line, then the game's own text.
void KojinBlend::drawStatTip(int c, int y) {
    const GameVersion v = ctx_.version;
    const int job = ctx_.work.s16(rec() + 6), cls = logic::sdU8(kJobs + U(job) * 0x40, v), pair = ctx_.work.u8(rec() + 0x72);
    const std::string body = passage(capText_, capCache_, c);
    const Box b{328, std::min(y - 10, 768 - 260), 310, 0};
    const std::vector<std::string> lines = wrap(body, 12, b.w - 20);
    const int h = 58 + int(std::min<std::size_t>(lines.size(), 12)) * 17;
    fill(b.x, b.y, b.w, h, 0xf8080c1cu, kPTip);
    outline({b.x, b.y, b.w, h}, kGold, kPTipLine);
    caps(b.x + 10, b.y + 14, tr(kAttrNames[c]), 15, kGold, kPTipText);
    int x = b.x + 10;
    const ysub::Rect r = t_.rect(kRBoost);
    for (int k = 0; k < 5; ++k) {
        const int idx = (cls - 1) * 10 + (pair - 1) * 5 + k;
        if (idx < 0 || idx >= 0x35) continue;
        const int icon = kStatIcons[std::size_t(c)][std::size_t(idx)];
        if (icon == 0) continue;
        sub(r.frame, r.sx + (icon - 1) * 0x16, r.sy, r.w, r.h, x, b.y + 30, 18, 18 * r.h / std::max<int>(1, r.w), kPTipItem);
        x += 20 + text(x + 20, b.y + 39, tr(kBoostNames[icon - 1]), 12, kInk, kPTipText);
        x += 10;
    }
    int ly = b.y + 58;
    for (std::size_t i = 0; i < lines.size() && i < 12; ++i, ly += 17) {
        const bool heading = lines[i].starts_with("★");          // "★All classes★"
        text(b.x + 10, ly, lines[i], 12, heading ? kGold : 0xffdfe3eeu, kPTipText, heading, 0, false);
    }
}

// ================================================================== main ===
void KojinBlend::drawMain() {
    const Box p{22, 256, 732, 474};
    panel(p, kPPanel);
    const int job = ctx_.work.s16(rec() + 6);
    const Box pl{33, 267, 710, 52};
    plate(pl, false, kPOverBack - 1, false, 26);           // in front of the lists' backing
    jobIcon(job, 61, 268, 30, kPItem);
    const int tw = caps(101, 293, className(job), 21, kInk, kPText);
    text(111 + tw, 294, tr("クラスレベル") + " " + std::to_string(jobLevel(job)), 14, 0xffe5e9f4u, kPText);
    const std::array<std::string, 3> labels{tr("装備一覧"), tr("技能一覧"), tr("クラス変更")};
    std::array<int, 3> bx{};
    for (int i = 2, x = 715; i >= 0; --i) { x -= buttonW(labels[std::size_t(i)]); bx[std::size_t(i)] = x; x -= 6; }
    const auto toggle = [&](int i, bool on) { return button({bx[std::size_t(i)], 278, buttonW(labels[std::size_t(i)]), 31}, labels[std::size_t(i)], kPItem, on); };
    if (toggle(0, browse_ == 1)) {
        ctx_.audio->sePlay(kSe); browse_ = browse_ == 1 ? 0 : 1; classes_ = false; focus_ = Kind::None; card_ = Kind::None; listScroll_ = 0;
    }
    if (toggle(1, browse_ == 2)) {
        ctx_.audio->sePlay(kSe); browse_ = browse_ == 2 ? 0 : 2; classes_ = false; focus_ = Kind::None; card_ = Kind::None; listScroll_ = 0;
    }
    if (toggle(2, classes_)) {
        ctx_.audio->sePlay(kSe); classes_ = !classes_; browse_ = 0; focus_ = Kind::None; card_ = Kind::None; classPick_ = classShown_ = 0;
    }
    if (classes_ || browse_) fill(29, 309, 718, 414, 0xf40c1226u, kPOverBack);
    if (classes_) drawClasses();
    else if (browse_) drawBrowser();
    else drawSlots();
}

void KojinBlend::drawSlots() {
    shown_ = Kind::None;
    const auto column = [&](int x, bool gear) {
        const int n = gear ? t_.itemSlots_ : t_.skillSlots_;
        int used = 0;
        for (int k = 0; k < n; ++k) used += (gear ? gearIn(k) : skillIn(k)) != 0;
        caps(x + 2, 340, gear ? tr("装備") : tr("技能"), 14, kInk, kPText);
        text(x + 337, 340, std::to_string(used) + " / " + std::to_string(n), 13, kDim, kPText, false, 2, false);
        fill(x + 2, 352, 335, 1, kLine, kPWellLine);
        if (n == 0) text(x + 10, 378, gear ? tr("このクラスは装備を持てない") : tr("このクラスは技能枠がない"), 14, kDim, kPText);
        for (int k = 0; k < n; ++k) {
            const Box b{x, 358 + k * 45, 337, 42};
            const bool on = focus_ == (gear ? Kind::Gear : Kind::Skill) && focusK_ == k;
            well(b, kPWell, over(b), on);
            caps(b.x + 9, b.y + 21, gear ? std::to_string(k + 1) : "F" + std::to_string(k + 1), 11, kGold, kPText);
            const int id = gear ? gearIn(k) : skillIn(k);
            if (id) {
                if (gear) {
                    itemIcon(id, b.x + 33, b.y + 4, 34, kPItem);
                    text(b.x + 77, b.y + 21, itemName(id), 17, kInk, kPText);
                } else {
                    skillIcon(id, b.x + 32, b.y + 3, 36, kPItem);
                    const SkillInfo si = skillInfo(id);
                    text(b.x + 77, b.y + 21, si.name, 17, kInk, kPText);
                    text(b.x + b.w - 9, b.y + 21, std::to_string(si.mp) + " MP", 13, kDim, kPText, false, 2, false);
                }
                if (over(b)) { shown_ = gear ? Kind::Item : Kind::SkillCard; shownId_ = id; }
            } else {
                text(b.x + 77, b.y + 21, tr("空き"), 15, 0xff5e688au, kPText, false, 0, false);
            }
            if (clicked(b)) {
                ctx_.audio->sePlay(kSe);
                const Kind kd = gear ? Kind::Gear : Kind::Skill;
                if (focus_ == kd && focusK_ == k) focus_ = Kind::None;
                else { focus_ = kd; focusK_ = k; }
                card_ = Kind::None;
            }
        }
    };
    column(43, true);
    column(396, false);
}

// A list cell: black edge, a dim line; brighter pointed at, gold when it's the student's, pale when chosen.
void KojinBlend::cell(const Box& b, int prio, bool hot, bool mine, bool sel) {
    outline(b, 0xff000000u, prio);
    outline({b.x + 1, b.y + 1, b.w - 2, b.h - 2}, sel ? 0xfffff3c4u : mine ? kGold : 0xff4d5878u, prio);
    fill(b.x + 2, b.y + 2, b.w - 4, b.h - 4, mine ? 0xff2a2616u : hot ? 0xff18224au : 0xff0b1024u, prio + 1);
}

// A row of legend entries, centred under a list.
void KojinBlend::legend(const std::vector<std::pair<int, std::string>>& entries, int y) {
    // kind: 0 none, 1 gold box, 2 dark box, 3 a red ×
    int w = 0;
    for (const auto& [kind, label] : entries) w += (kind == 0 ? 0 : kind == 3 ? textW("×", 12, true) + 4 : 15) + textW(label, 12) + 16;
    int x = 388 - (w - 16) / 2;
    for (const auto& [kind, label] : entries) {
        if (kind == 1 || kind == 2) {
            outline({x, y - 6, 11, 11}, kind == 1 ? kGold : 0xff2c3658u, kPOverWell);
            fill(x + 1, y - 5, 9, 9, kind == 1 ? 0xff2a2616u : 0xff000000u, kPOverWell + 1);
            x += 15;
        } else if (kind == 3) x += text(x, y, "×", 12, kDown, kPOverText, true, 0, false) + 4;
        x += text(x, y, label, 12, kDim, kPOverText, false, 0, false) + 16;
    }
}

void KojinBlend::drawBrowser() {
    const int me = unit(), myJob = ctx_.work.s16(rec() + 6);
    if (browse_ == 1) {
        // Type filters, with a mark for a type nothing of which fits the class.
        std::array<int, 10> have{}, all{}, fits{}, stored{};
        for (int it = 1; it <= kItemCount; ++it) {
            const int t = itemType(it), w = ctx_.work.u16(kStore + U(it) * 2), st = w >> 8 & 0xf;
            if (t < 0 || t > 9) continue;
            ++all[std::size_t(t)]; ++all[0];
            if (!(w & 1)) continue;
            ++have[std::size_t(t)]; ++have[0]; ++stored[std::size_t(t)];
            if (st == 1 || st == 3) ++fits[std::size_t(t)];
        }
        int x = 43, y = 319;
        for (int t = 0; t < 10; ++t) {
            const bool no = t && stored[std::size_t(t)] && !fits[std::size_t(t)];
            const std::string label = t ? tr(kItemTypes[t]) : tr("すべて");
            const std::string count = std::to_string(have[std::size_t(t)]) + "/" + std::to_string(all[std::size_t(t)]);
            const int w = 9 + (no ? textW("×", 12) + 3 : 0) + textW(label, 12) + 3 + textW(count, 10) + 9;
            if (x + w > 733) { x = 43; y += 27; }
            const Box chip{x, y, w, 23};
            const bool on = itemFilter_ == t;
            roundBox(chip, 10, on ? 0xff2a2414u : 0xff05070fu, on ? kGold : kLine, kPOverWell);
            int tx = chip.x + 9;
            if (no) tx += text(tx, chip.y + 11, "×", 12, kDown, kPOverText, false, 0, false) + 3;
            tx += text(tx, chip.y + 11, label, 12, on || over(chip) ? kInk : no ? 0xff6c7593u : kDim, kPOverText, false, 0, false) + 3;
            text(tx, chip.y + 12, count, 10, kDim, kPOverText, false, 0, false);
            if (clicked(chip)) { ctx_.audio->sePlay(kSe); itemFilter_ = t; listScroll_ = 0; }
            x += w + 4;
        }
        std::vector<int> cells;
        for (int t = 1; t <= 9; ++t)
            for (int it = 1; it <= kItemCount; ++it)
                if (itemType(it) == t && (!itemFilter_ || itemFilter_ == t)) cells.push_back(it);
        const Box grid{43, y + 31, 690, 682 - (y + 31)};
        constexpr int kCols = 14, kPitchX = 48, kPitchY = 50;
        const int rows = (int(cells.size()) + kCols - 1) / kCols, maxScroll = std::max(0, rows * kPitchY - 4 - grid.h);
        if (over(grid) && wheel_) listScroll_ -= wheel_ * kPitchY;
        static int lastY = 0;
        if (held_ && dragged_ && grid.in(downX_, downY_)) { if (lastY) listScroll_ -= my_ - lastY; lastY = my_; } else lastY = 0;
        listScroll_ = std::clamp(listScroll_, 0, maxScroll);
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const int it = cells[i];
            const Box c{grid.x + int(i % kCols) * kPitchX, grid.y + int(i / kCols) * kPitchY - listScroll_, 44, 46};
            if (c.y < grid.y || c.y + c.h > grid.y + grid.h) continue;
            const int w = ctx_.work.u16(kStore + U(it) * 2), st = w >> 8 & 0xf, owner = st >= 3 ? ownerOf(it) : 0;
            const bool hot = over(c);
            cell(c, kPOverWell, hot, owner == me, card_ == Kind::Item && cardId_ == it);
            // Not in storage: dark; the class can't wear it: faded with a red ×.
            const ysub::Rect r = t_.rect(kRItemIcons);
            const int ii = it - 1;
            sub(r.frame, r.sx + (ii % 0xc) * r.w, r.sy + (ii / 0xc) * r.h, r.w, r.h, c.x + 4, c.y + 5, 36, 36, kPOverItem,
                (w & 1) && (st == 2 || st == 4) ? 0x66 : 0xff, (w & 1) ? 0x80 : 0x26);
            if ((w & 1) && (st == 2 || st == 4)) text(c.x + c.w - 7, c.y + 9, "×", 12, kDown, kPOverText, true, 1, false);
            if (owner && owner != me) {
                outline({c.x + c.w - 19, c.y + c.h - 18, 20, 19}, 0xff3a4566u, kPOverText + 2);
                face(owner, c.x + c.w - 18, c.y + c.h - 17, 18, 17, kPOverText + 1, 0xff);
            }
            if (hot) { wantKind_ = 2; wantId_ = it; }
            if (clicked(c)) { ctx_.audio->sePlay(kSe); card_ = Kind::Item; cardId_ = it; learnAsk_ = 0; }
        }
        legend({{1, tr("着用中") + " " + unitName(me)}, {0, tr("顔: その生徒が着用")}, {3, className(myJob) + tr("は装備できない")},
                {2, tr("暗い: 未所持")}}, 696);
        return;
    }
    // Skill List: the classic grid, 12 categories by 7 tiers.
    for (int col = 0; col < 12; ++col) caps(69 + col * 54 + 25, 338, tr(kCats[col]), 11, kGold, kPOverText, true, 1, false);
    for (int row = 0; row < 7; ++row) {
        caps(54, 348 + row * 43 + 20, std::to_string(row + 1), 12, kDim, kPOverText, true, 1, false);
        for (int col = 0; col < 12; ++col) {
            const Box c{69 + col * 54, 348 + row * 43, 50, 40};
            const int id = t_.gridSkill(col, row), st = id > 0 ? skillState(id) : 0;
            const bool known = id > 0 && st != 0 && st <= 6;
            cell(c, kPOverWell, known && over(c), st == 6, known && card_ == Kind::SkillCard && cardId_ == id);
            if (!known) { caps(c.x + 25, c.y + 20, "?", 17, 0xff333c5cu, kPOverText, true, 1, false); continue; }
            const bool dim = st == 1 || st == 2 || st == 4 || st == 5;
            skillIcon(id, c.x + 7, c.y + 2, 36, kPOverItem, dim ? 0x66 : 0xff);
            if (st == 4 || st == 5) text(c.x + c.w - 7, c.y + 9, "×", 12, kDown, kPOverText, true, 1, false);
            if (st == 1 || st == 2 || st == 4) {                // can be learned: its price along the bottom
                text(c.x + c.w / 2, c.y + c.h - 7, fmt(logic::sdS32(kSkills + U(id) * 0x48 + 0x40, ctx_.version)), 9, kGold, kPOverText,
                     true, 1, true);
            }
            if (over(c)) { wantKind_ = 2; wantId_ = 0x10000 | id; }
            if (clicked(c)) { ctx_.audio->sePlay(kSe); card_ = Kind::SkillCard; cardId_ = id; learnAsk_ = 0; }
        }
    }
    legend({{1, tr("枠に装備中")}, {0, tr("明るい: 修得済み")}, {0, tr("薄く値段つき: 修得できる")}, {3, className(myJob) + tr("は使えない")},
            {0, tr("?: まだ開いていない")}}, 696);
}

void KojinBlend::drawClasses() {
    const GameVersion v = ctx_.version;
    const int job = ctx_.work.s16(rec() + 6), pair = ctx_.work.u8(rec() + 0x72);
    for (int i = 0; i < 15; ++i) {
        const int j = kClassBase[std::size_t(i)] - 1 + pair;
        const Box c{43 + (i % 5) * 279 / 2, 321 + (i / 5) * 113, 132, 105};
        const bool open = isOpen(j), hot = over(c);
        outline(c, !open && (hot || j == classShown_) ? 0xff8a6b6bu : 0xff000000u, kPOverWell);
        outline({c.x + 1, c.y + 1, c.w - 2, c.h - 2}, open && (j == job || j == classPick_) ? kGold : kLine, kPOverWell);
        if (!open && (hot || j == classShown_)) fill(c.x + 2, c.y + 2, c.w - 4, c.h - 4, 0xff120c18u, kPOverWell + 1);
        else vgrad({c.x + 2, c.y + 2, c.w - 4, c.h - 4}, open && hot ? 0xff18224au : 0xff0b1024u, 0xff05070fu, kPOverWell + 1);
        jobIcon(j, c.x + 41, c.y + 7, 50, kPOverItem, open ? 0 : 1);
        text(c.x + c.w / 2, c.y + 89, className(j), 13, open ? kInk : 0xff6c7593u, kPOverText, open, 1);
        if (open) text(c.x + 6, c.y + 12, std::to_string(j < int(t_.jobCount_.size()) ? t_.jobCount_[std::size_t(j)] : 0), 11, kDim, kPOverText, false, 0, false);
        else lock(c.x + 6, c.y + 5, kPOverText);
        text(c.x + c.w - 6, c.y + 12, "Lv " + std::to_string(jobLevel(j)), 11, 0xffc9b27au, kPOverText, false, 2, false);
        if (hot) { classShown_ = j; if (open) { wantKind_ = 3; wantId_ = j; } }
        if (clicked(c)) {
            ctx_.audio->sePlay(kSe);
            classShown_ = j;
            if (open) classPick_ = j;
        }
    }
    const int pick = classPick_;
    std::string note = tr("クラスを選ぶと能力を上に表示");
    if (pick && pick != job)
        note = className(pick) + ": " + tr("技能") + " " + std::to_string(logic::sdU8(kJobs + 1 + U(pick) * 0x40, v)) + " / " +
               tr("装備") + " " + std::to_string(logic::sdU8(kJobs + 2 + U(pick) * 0x40, v));
    text(43, 679, note, 14, kDim, kPOverText, false, 0, false);
    const std::string cancel = tr("キャンセル"), change = tr("変更");
    if (button({415, 664, buttonW(cancel), 31}, cancel, kPOverItem)) { ctx_.audio->sePlay(kSe); classes_ = false; classPick_ = classShown_ = 0; }
    if (button({733 - buttonW(change), 664, buttonW(change), 31}, change, kPOverItem, false, pick && pick != job)) {
        ctx_.audio->sePlay(0xb);
        const int line = logic::unitVoiceLine(unit(), 5, 5);
        if (line >= 0) ctx_.audio->voicePlay(logic::unitVoicePath(line, v));
        changeClass(pick);
        classes_ = false; classPick_ = classShown_ = 0; shown_ = Kind::None;
        previewKey_ = ~0ull;
    }
}

// ================================================================== side ===
void KojinBlend::drawSide() {
    const Box p{756, 256, 262, 474};
    panel(p, kPPanel);
    const Box pl{767, 267, 240, 44};
    plate(pl, false, kPFrame, false, 26);
    const Box body{775, 321, 224, 392};
    std::string title = tr("詳細");
    if (classes_) {
        const int j = classShown_ ? classShown_ : classPick_ ? classPick_ : ctx_.work.s16(rec() + 6);
        title = isOpen(j) ? tr("クラス") : tr("未解放のクラス");
        caps(pl.x + 28, pl.y + 22, title, 16, kInk, kPText, true);
        if (isOpen(j)) sideClass(body, j); else sideLocked(body, j);
        return;
    }
    if (card_ == Kind::Item) {
        caps(pl.x + 28, pl.y + 22, tr(kItemTypes[std::clamp(itemType(cardId_), 0, 9)]), 16, kInk, kPText, true);
        if (focus_ != Kind::None && button({pl.x + pl.w - 70, pl.y + 8, 56, 22}, tr("戻る"), kPItem)) { ctx_.audio->sePlay(kSe); card_ = Kind::None; }
        sideItem(body, cardId_, true);
        return;
    }
    if (card_ == Kind::SkillCard) {
        caps(pl.x + 28, pl.y + 22, tr("技能詳細"), 16, kInk, kPText, true);
        if (focus_ != Kind::None && button({pl.x + pl.w - 70, pl.y + 8, 56, 22}, tr("戻る"), kPItem)) { ctx_.audio->sePlay(kSe); card_ = Kind::None; }
        sideSkill(body, cardId_, true);
        return;
    }
    if (focus_ != Kind::None) { sidePicker(body); return; }
    if (shown_ == Kind::Item) { caps(pl.x + 28, pl.y + 22, tr(kItemTypes[std::clamp(itemType(shownId_), 0, 9)]), 16, kInk, kPText, true); sideItem(body, shownId_, false); return; }
    if (shown_ == Kind::SkillCard) { caps(pl.x + 28, pl.y + 22, tr("技能詳細"), 16, kInk, kPText, true); sideSkill(body, shownId_, false); return; }
    caps(pl.x + 28, pl.y + 22, title, 16, kInk, kPText, true);
    paragraph(body.x, body.y, body.w, tr("装備や技能を指すと説明を表示。枠をクリックで変更。"), 13, kDim, kPText);
}

// A card's head: the icon in a box, the name (two lines at most) and up to two small lines under it.
int KojinBlend::hero(const Box& b, const std::function<void(int, int)>& icon, const std::string& name, const std::string& sub1,
                     const std::string& sub2) {
    outline({b.x, b.y, 60, 60}, 0xff9aa8c8u, kPWell);
    fill(b.x + 1, b.y + 1, 58, 58, 0xff0b1024u, kPWell + 1);
    icon(b.x + 3, b.y + 3);
    std::vector<std::string> lines = wrap(name, 18, b.w - 70);
    if (lines.size() > 2) lines.resize(2);
    const int n = int(lines.size()) + !sub1.empty() + !sub2.empty();
    int y = b.y + 30 - (int(lines.size()) * 21 + (n - int(lines.size())) * 20) / 2;
    for (const std::string& l : lines) { text(b.x + 70, y + 11, l, 18, kInk, kPText, true, 0, false); y += 21; }
    for (const std::string* s : {&sub1, &sub2})
        if (!s->empty()) { text(b.x + 70, y + 10, *s, 13, kDim, kPText, false, 0, false); y += 20; }
    return b.y + 70;
}

// One "key ... value" row of a card.
int KojinBlend::kv(int x, int y, int w, const std::string& k, const std::string& value, std::uint32_t argb) {
    text(x, y + 10, k, 13, kDim, kPText, false, 0, false);
    text(x + w, y + 10, value, 13, argb, kPText, false, 2, false);
    return y + 21;
}

// A card's text under a line, as far as `bottom`.
int KojinBlend::desc(int x, int y, int w, const std::string& body, int bottom) {
    fill(x, y, w, 1, 0xff3a4566u, kPWellLine);
    y += 8;
    for (const std::string& l : wrap(body, 14, w)) {
        if (y + 19 > bottom) break;
        text(x, y + 10, l, 14, 0xffdfe3eeu, kPText, false, 0, false);
        y += 19;
    }
    return y;
}

// The strip of actions under a card: a line, a darker band, notes, then the button (left-aligned label).
bool KojinBlend::actStrip(int y, const std::vector<std::pair<std::string, std::uint32_t>>& notes, const std::string& label, bool on,
                          bool enabled) {
    int h = 9;
    for (const auto& [n, c] : notes) h += 19 * int(std::min<std::size_t>(2, wrap(n, 13, 224).size()));
    if (!label.empty()) h += 31;
    h += 10;
    fill(763, y, 248, 1, 0xff3a4566u, kPWellLine);
    fill(763, y + 1, 248, h - 1, 0xff0e1429u, kPWell + 2);
    int ny = y + 9;
    for (const auto& [n, c] : notes) ny += paragraph(775, ny, 224, n, 13, c, kPText, 2);
    if (label.empty()) return false;
    const Box bt{775, ny, 224, 31};
    const bool hot = enabled && over(bt);
    plate(bt, true, kPItem + 1, on || hot, 12, enabled ? 0xff : 0x73);
    caps(bt.x + 16, bt.y + 15, label, 14, enabled ? (on ? 0xfffff6c0u : kInk) : 0x73ffffffu, kPItem - 1);
    return enabled && clicked(bt);
}
int KojinBlend::actStripH(const std::vector<std::pair<std::string, std::uint32_t>>& notes, bool button) const {
    int h = 19;
    for (const auto& [n, c] : notes) h += 19 * int(std::min<std::size_t>(2, wrap(n, 13, 224).size()));
    return h + (button ? 31 : 0);
}

// An item: name, kind, effects, which classes can wear it, its text; and what can be done with it.
void KojinBlend::sideItem(const Box& b, int item, bool actions) {
    const GameVersion v = ctx_.version;
    const int type = itemType(item), rarity = (logic::sdS16(kItems + 8 + U(item) * 0x38, v) & 0xffff) - 1;
    int y = hero(b, [&](int x, int yy) { itemIcon(item, x + 1, yy + 1, 54, kPItem); }, itemName(item),
                 tr(kItemTypes[std::clamp(type, 0, 9)]) + (rarity >= 0 && rarity < 8 ? " · " + tr(kRarities[rarity]) : ""));
    // Effects (kind 8..): an icon and the value each, as pills.
    const std::uint32_t I = kItems + U(item) * 0x38;
    int x = b.x;
    for (std::uint32_t j = 0; j < 3; ++j) {
        const int kind = logic::sdU8(I + 0x35 + j, v);
        if (kind < 8 || kind - 8 >= int(kEffect.size())) continue;
        const Field f = kEffect[std::size_t(kind - 8)];
        const int value = f.word ? logic::sdS16(I + f.off, v) : logic::sdU8(I + f.off, v);
        if (value < 0) continue;
        const int w = 31 + textW(std::to_string(value), 14, true);
        roundBox({x, y + 1, w, 25}, 10, 0xff0b1024u, 0xff3a4566u, kPWell);
        statIcon(kind, x + 4, y + 6, kPItem);
        text(x + 24, y + 13, std::to_string(value), 14, kInk, kPText, true, 0, false);
        x += w + 5;
    }
    if (x != b.x) y += 36;
    const int st = itemState(item), owner = st >= 3 ? ownerOf(item) : 0;
    if (owner) y = kv(b.x, y, b.w, tr("着用"), unitName(owner), kInk);
    const int pair = ctx_.work.u8(rec() + 0x72);
    for (int i = 0; i < 5; ++i) {
        const bool ok = logic::sdU8(0x6e45f8 + U(type) + U(pair + i * 2) * 0xf, v) != 0;
        y = kv(b.x, y, b.w, className(kClassBase[std::size_t(5 + i)] - 1 + pair), ok ? tr("可") : tr("不可"), ok ? kUp : kDown);
    }
    y += 8;

    // What can be done with it.
    const int me = unit(), w = ctx_.work.u16(kStore + U(item) * 2);
    std::vector<std::pair<std::string, std::uint32_t>> notes;
    std::string label;
    int k = -1;
    if (!(w & 1)) notes.push_back({tr("まだ所持していない"), kDim});
    else if (owner == me) label = tr("外す");
    else if (st == 2 || st == 4) notes.push_back({className(ctx_.work.s16(rec() + 6)) + tr("は装備できない"), kDim});
    else if ((k = gearTarget(item)) >= 0) {
        label = focus_ == Kind::Gear ? tr("装備する") + " → " + std::to_string(k + 1) : tr("装備する");
        if (owner) notes.push_back({tr("着用") + ": " + unitName(owner), kDim});
    } else notes.push_back({t_.itemSlots_ ? tr("装備枠がいっぱい: 左の枠を選んで入れ替える") : className(ctx_.work.s16(rec() + 6)) + tr("は装備を持てない"), kDim});
    const int bottom = actions ? b.y + b.h + 2 - actStripH(notes, !label.empty()) : b.y + b.h;
    y = desc(b.x, y, b.w, passage(itemText_, itemCache_, item), bottom);
    if (!actions) return;
    if (!actStrip(y + 8, notes, label)) return;
    ctx_.audio->sePlay(kSe);
    if (owner == me) { for (int s = 0; s < t_.itemSlots_; ++s) if (gearIn(s) == item) equipItem(s, 0); }
    else { putOn(k, item); if (focus_ != Kind::None) { focus_ = Kind::None; card_ = Kind::None; } }
    previewKey_ = ~0ull;
}

void KojinBlend::sideSkill(const Box& b, int id, bool actions) {
    const GameVersion v = ctx_.version;
    const SkillInfo s = skillInfo(id);
    int y = hero(b, [&](int x, int yy) { skillIcon(id, x, yy, 54, kPItem); }, s.name,
                 (s.cat >= 1 && s.cat <= 12 ? tr(kCats[s.cat - 1]) : std::string("?")) + " " + std::to_string(s.tier) + " · " +
                     (s.passive ? tr("パッシブ") : tr("アクティブ")));
    const bool tw = v == GameVersion::Tw;
    const auto label = [&](std::uint32_t va) { return trimmed(exe::text(v, va)); };
    y = kv(b.x, y, b.w, "MP", std::to_string(s.mp), kInk);
    y = kv(b.x, y, b.w, tr("準備"), std::to_string(s.prep), kInk);
    y = kv(b.x, y, b.w, tr("硬直"), std::to_string(s.recov), kInk);
    y = kv(b.x, y, b.w, tr("狙い"), s.aim < 7 ? label((tw ? kTargetTw : kTargetJp)[std::size_t(s.aim)]) : std::string(), kInk);
    y = kv(b.x, y, b.w, tr("対象"), s.range < 6 ? label((tw ? kRangeTw : kRangeJp)[std::size_t(s.range)]) : std::string(), kInk);
    y = kv(b.x, y, b.w, tr("費用"), fmt(s.price) + " XP", kInk);
    y += 8;

    const int st = skillState(id), k = skillTarget();
    const std::string job = className(ctx_.work.s16(rec() + 6));
    std::vector<std::pair<std::string, std::uint32_t>> notes;
    std::string lbl;
    bool enabled = true;
    if (st == 1 || st == 2 || st == 4) {
        const bool full = logic::unitSummary(ctx_.work, v, unit()).toCap == 0, poor = ctx_.work.s32(kPoints) < s.price;
        notes.push_back({tr("共有XP") + " " + fmt(ctx_.work.s32(kPoints)) + " › " + fmt(ctx_.work.s32(kPoints) - s.price), kDim});
        if (st == 4) notes.push_back({job + tr("はこの技能を使えない: 修得しても枠には入らない"), 0xffffb08au});
        if (full) notes.push_back({tr("容量がいっぱい"), 0xffffb08au});
        else if (poor) notes.push_back({tr("共有XPが足りない"), 0xffffb08au});
        // Learning asks once more before it spends the XP.
        lbl = learnAsk_ == id ? tr("修得する？") + " " + fmt(s.price) + " XP"
                              : tr("修得") + " " + fmt(s.price) + " XP" + (st != 4 && k >= 0 ? " → F" + std::to_string(k + 1) : "");
        enabled = !full && !poor;
    } else if (st == 3) {
        if (k >= 0) lbl = focus_ == Kind::Skill ? tr("装備する") + " → F" + std::to_string(k + 1) : tr("装備する");
        else notes.push_back({t_.skillSlots_ ? tr("技能枠がいっぱい: 左の枠を選んで入れ替える") : job + tr("は技能枠がない"), kDim});
    } else if (st == 5) notes.push_back({tr("修得済みだが") + job + tr("は使えない"), kDim});
    else if (st == 6) lbl = tr("外す");
    else notes.push_back({tr("まだ修得できない"), kDim});
    const int bottom = actions ? b.y + b.h + 2 - actStripH(notes, !lbl.empty()) : b.y + b.h;
    y = desc(b.x, y, b.w, s.text, bottom);
    if (!actions) return;
    if (!actStrip(y + 8, notes, lbl, learnAsk_ == id, enabled)) return;
    if (st == 1 || st == 2 || st == 4) {
        if (learnAsk_ != id) { ctx_.audio->sePlay(kSe); learnAsk_ = id; return; }
        if (!learn(id)) return;
        ctx_.audio->sePlay(0xb);
        learnAsk_ = 0;
        if (skillState(id) == 3 && k >= 0) { equipSkill(k, id); if (focus_ != Kind::None) { focus_ = Kind::None; card_ = Kind::None; } }
    } else if (st == 3) {
        ctx_.audio->sePlay(kSe);
        equipSkill(k, id);
        if (focus_ != Kind::None) { focus_ = Kind::None; card_ = Kind::None; }
    } else if (st == 6) {
        ctx_.audio->sePlay(kSe);
        for (int s2 = 0; s2 < t_.skillSlots_; ++s2) if (skillIn(s2) == id) equipSkill(s2, 0);
    }
    previewKey_ = ~0ull;
}

// The slot chosen on the left: what can go in it.
void KojinBlend::sidePicker(const Box& b) {
    const GameVersion v = ctx_.version;
    const Box pl{767, 267, 226, 38};
    int y = b.y;
    const auto row = [&](int icon, bool isItem, const std::string& name, const std::string& small, int pickId, bool enabled, std::uint8_t alpha = 0xff) {
        const Box r{b.x, y, b.w, 36};
        if (r.y + r.h > b.y + b.h) { y += 38; return false; }
        well(r, kPWell, enabled && over(r));
        if (icon) { if (isItem) itemIcon(icon, r.x + 3, r.y + 3, 30, kPItem, alpha); else skillIcon(icon, r.x + 3, r.y + 3, 30, kPItem, alpha); }
        text(r.x + 38, r.y + (small.empty() ? 18 : 12), name, 13, enabled ? kInk : kDim, kPText);
        if (!small.empty()) text(r.x + 38, r.y + 27, small, 11, kDim, kPText);
        if (enabled && over(r) && pickId) { wantKind_ = 1; wantId_ = pickId; }
        y += 38;
        return clicked(r);
    };
    if (focus_ == Kind::Gear) {
        const int cur = gearIn(focusK_);
        caps(pl.x + 28, pl.y + 22, tr("装備") + " " + std::to_string(focusK_ + 1), 16, kInk, kPText, true);
        std::vector<int> free, worn;
        for (int it = 1; it <= kItemCount; ++it) {
            const int w = ctx_.work.u16(kStore + U(it) * 2), st = w >> 8 & 0xf;
            if (!(w & 1) || !t_.canEquip(rec(), focusK_, it)) continue;
            if (st == 1) free.push_back(it);
            else if (st == 3 && ownerOf(it) != unit()) worn.push_back(it);
        }
        if (cur && row(0, true, tr("外す") + ": " + itemName(cur), "", 0, true)) { ctx_.audio->sePlay(kSe); equipItem(focusK_, 0); previewKey_ = ~0ull; }
        for (int it : free)
            if (row(it, true, itemName(it), tr(kItemTypes[std::clamp(itemType(it), 0, 9)]), it, true)) {
                ctx_.audio->sePlay(kSe); putOn(focusK_, it); focus_ = Kind::None; previewKey_ = ~0ull; return;
            }
        if (!worn.empty()) { text(b.x, y + 8, tr("他の生徒が着用中"), 11, kGold, kPText, true); y += 18; }
        for (int it : worn)
            if (row(it, true, itemName(it), tr("着用") + ": " + unitName(ownerOf(it)), it, true, 0xa0)) {
                ctx_.audio->sePlay(kSe); card_ = Kind::Item; cardId_ = it;
            }
        if (free.empty() && worn.empty() && !cur) paragraph(b.x, y, b.w, tr("この枠に入る物は倉庫にない"), 13, kDim, kPText);
        return;
    }
    const int cur = skillIn(focusK_);
    caps(pl.x + 28, pl.y + 22, tr("技能") + " F" + std::to_string(focusK_ + 1), 16, kInk, kPText, true);
    if (cur && row(0, false, tr("外す") + ": " + skillInfo(cur).name, "", 0, true)) { ctx_.audio->sePlay(kSe); equipSkill(focusK_, 0); previewKey_ = ~0ull; }
    std::vector<int> learned, buy;
    for (int row7 = 0; row7 < 7; ++row7)
        for (int col = 0; col < 12; ++col) {
            const int id = t_.gridSkill(col, row7);
            if (id <= 0) continue;
            const int st = skillState(id);
            if (st == 3) learned.push_back(id);
            else if (st == 1 || st == 2 || st == 4) buy.push_back(id);
        }
    for (int id : learned)
        if (row(id, false, skillInfo(id).name, tr(kCats[std::clamp(skillInfo(id).cat, 1, 12) - 1]) + " " + std::to_string(skillInfo(id).tier), id, true)) {
            ctx_.audio->sePlay(kSe); equipSkill(focusK_, id); focus_ = Kind::None; previewKey_ = ~0ull; return;
        }
    if (learned.empty()) { text(b.x, y + 8, tr("使える修得済みの技能がない"), 12, kDim, kPText); y += 20; }
    if (!buy.empty()) { text(b.x, y + 8, tr("共有XPで修得"), 11, kGold, kPText, true); y += 18; }
    for (int id : buy) {
        const SkillInfo s = skillInfo(id);
        const bool can = logic::unitSummary(ctx_.work, v, unit()).toCap > 0 && ctx_.work.s32(kPoints) >= s.price;
        if (row(id, false, s.name + (skillState(id) == 4 ? " ×" : ""), fmt(s.price) + " XP", 0, can, 0x90)) {
            ctx_.audio->sePlay(kSe); card_ = Kind::SkillCard; cardId_ = id; learnAsk_ = 0;
        }
    }
}

// An open class: its level, slots, a radar of what the student would have in it, its text.
void KojinBlend::sideClass(const Box& b, int job) {
    const GameVersion v = ctx_.version;
    const int cur = ctx_.work.s16(rec() + 6);
    hero(b, [&](int x, int y) { jobIcon(job, x + 7, y - 4, 40, kPItem); }, className(job),
         tr("クラスレベル") + " " + std::to_string(jobLevel(job)) + (job == cur ? " · " + tr("現在") : ""),
         tr("技能") + " " + std::to_string(logic::sdU8(kJobs + 1 + U(job) * 0x40, v)) + " · " + tr("装備") + " " +
             std::to_string(logic::sdU8(kJobs + 2 + U(job) * 0x40, v)));
    radar(b.x + b.w / 2, b.y + 152, 54, job, kPItem);
    int y = b.y + 238;
    if (job != cur) {
        fill(b.x + 30, y - 4, 12, 8, 0xc0f07a3au, kPItem);
        int x = b.x + 46 + text(b.x + 46, y, className(job), 11, kDim, kPText) + 12;
        fill(x, y - 4, 12, 8, 0xff8fa0c8u, kPItem);
        fill(x + 1, y - 3, 10, 6, 0xff0b1129u, kPItem - 1);
        text(x + 16, y, className(cur) + " (" + tr("現在") + ")", 11, kDim, kPText);
        y += 14;
    }
    desc(b.x, y + 4, b.w, passage(jobText_, jobCache_, job), b.y + b.h);
}

// The student's values in each of the 15 classes on 8 axes, each scaled to their best class.
void KojinBlend::radar(int cx, int cy, int r, int job, int prio) {
    struct Axis { const char* label; int i; bool grade; };
    constexpr std::array<Axis, 8> kAxes{{{"HP", 0, false}, {"MP", 1, false}, {"威力", 4, false}, {"射程", 5, false},
                                         {"回避", 8, false}, {"装甲", 9, false}, {"結界", 10, false}, {"移動", 14, true}}};
    const auto valueOf = [](const Snap& s, const Axis& a) { return a.grade ? 7 - s.v[std::size_t(a.i)] : s.v[std::size_t(a.i)]; };
    const int pair = ctx_.work.u8(rec() + 0x72), cur = ctx_.work.s16(rec() + 6);
    const auto stats = [&](int j) { t_.computeUnit(unit(), j, false, 2); const Snap s = snap(); recompute(); return s; };
    std::array<int, 8> best{};
    best.fill(1);
    for (int b : kClassBase) {
        const Snap s = stats(b - 1 + pair);
        for (std::size_t i = 0; i < kAxes.size(); ++i) best[i] = std::max(best[i], valueOf(s, kAxes[i]));
    }
    const Snap mine = stats(job), now = stats(cur);
    const auto at = [&](std::size_t i, float f) {
        const float a = -3.14159265f / 2 + float(i) * 2 * 3.14159265f / float(kAxes.size());
        return SDL_FPoint{float(cx) + std::cos(a) * float(r) * f, float(cy) + std::sin(a) * float(r) * f};
    };
    const auto line = [&](SDL_FPoint p, SDL_FPoint q, float wdt, std::uint32_t argb, int pr) {
        const float dx = q.x - p.x, dy = q.y - p.y, len = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
        const float nx = -dy / len * wdt / 2, ny = dx / len * wdt / 2;
        ctx_.draw.geometry({.points = {{p.x + nx, p.y + ny}, {q.x + nx, q.y + ny}, {q.x - nx, q.y - ny},
                                       {p.x + nx, p.y + ny}, {q.x - nx, q.y - ny}, {p.x - nx, p.y - ny}},
                            .priority = static_cast<std::uint16_t>(pr), .argb = argb});
    };
    const auto shape = [&](const Snap& s, std::uint32_t fillArgb, std::uint32_t lineArgb, float wdt, int pr) {
        std::vector<SDL_FPoint> pts;
        for (std::size_t i = 0; i < kAxes.size(); ++i) pts.push_back(at(i, std::max(0.0f, float(valueOf(s, kAxes[i]))) / float(best[i])));
        GeometryCmd g{.priority = static_cast<std::uint16_t>(pr + 1), .argb = fillArgb};
        for (std::size_t i = 0; i < pts.size(); ++i) {
            g.points.push_back({float(cx), float(cy)});
            g.points.push_back(pts[i]);
            g.points.push_back(pts[(i + 1) % pts.size()]);
        }
        ctx_.draw.geometry(std::move(g));
        for (std::size_t i = 0; i < pts.size(); ++i) line(pts[i], pts[(i + 1) % pts.size()], wdt, lineArgb, pr);
        return pts;
    };
    for (float f : {0.25f, 0.5f, 0.75f, 1.0f})
        for (std::size_t i = 0; i < kAxes.size(); ++i) line(at(i, f), at((i + 1) % kAxes.size(), f), 1, 0xff2c3658u, prio + 4);
    for (std::size_t i = 0; i < kAxes.size(); ++i) line({float(cx), float(cy)}, at(i, 1), 1, 0xff2c3658u, prio + 4);
    if (job != cur) shape(now, 0x1e96a5c8u, 0xff8fa0c8u, 1.2f, prio + 2);
    const auto pts = shape(mine, 0x60e87034u, 0xfff07a3au, 2, prio);
    for (const SDL_FPoint& q : pts) fill(int(q.x) - 2, int(q.y) - 2, 5, 5, 0xfff07a3au, prio - 1);
    for (std::size_t i = 0; i < kAxes.size(); ++i) {
        const SDL_FPoint q = at(i, 1.3f);
        const int align = std::abs(q.x - float(cx)) < 4 ? 1 : q.x < float(cx) ? 2 : 0;
        const Axis& a = kAxes[i];
        caps(int(q.x), int(q.y) - 5, tr(a.label), 10, kGold, kPText, true, align);
        text(int(q.x), int(q.y) + 7, a.grade ? kGrades[std::size_t(std::clamp(mine.v[std::size_t(a.i)], 0, 7))] : fmt(mine.v[std::size_t(a.i)]),
             11, kInk, kPText, false, align);
    }
}

// A locked class: what it needs, the student's value, met or not.
void KojinBlend::sideLocked(const Box& b, int job) {
    const GameVersion v = ctx_.version;
    GameWork& w = ctx_.work;
    const std::uint32_t r = kJobRules + U(job) * 0x32;
    int y = hero(b, [&](int x, int yy) { jobIcon(job, x + 7, yy - 4, 40, kPItem); }, className(job), tr("未解放"));
    y += paragraph(b.x, y, b.w, passage(jobText_, jobCache_, job), 14, 0xffdfe3eeu, kPText, 4) + 4;
    caps(b.x, y + 10, tr("解放の条件"), 11, kGold, kPText);
    y += 22;
    const auto req = [&](bool ok, const std::string& what, const std::string& now) {
        text(b.x, y + 11, ok ? "○" : "×", 13, ok ? kGreen : kDown, kPText, true, 0, false);
        const int tw = text(b.x + 19, y + 11, what, 13, kInk, kPText, false, 0, false);
        if (!now.empty()) text(b.x + 26 + tw, y + 12, "(" + now + ")", 12, kDim, kPText, false, 0, false);
        y += 23;
    };
    const int m = logic::sdU8(r + 3, v), wk = logic::sdU8(r + 4, v);
    if (m && wk) {
        y += paragraph(b.x + 18, y, b.w - 18, tr("その日が来れば開く") + ": " + date(m, wk) + " (" + tr("今") + " " + date(w.month(), w.week()) + ")",
                       12, kDim, kPText);
        text(b.x, y + 8, tr("それより早く:"), 12, kDim, kPText);
        y += 18;
    }
    int total = 0;
    for (int s = 0; s < 7; ++s) {
        const int need = logic::sdS16(r + 6 + U(s) * 2, v), have = statLevel(s);
        total += have;
        if (need) req(have >= need, tr(kStatNames[s]) + " " + std::to_string(need), tr("今") + " " + std::to_string(have));
    }
    if (const int need = logic::sdS16(r + 0x14, v)) req(total >= need, tr("能力合計") + " " + std::to_string(need), tr("今") + " " + std::to_string(total));
    for (std::uint32_t k = 0; k < 3; ++k) {
        const int nj = logic::sdU8(r + 0x16 + k * 2, v), lv = logic::sdU8(r + 0x17 + k * 2, v);
        if (nj) req(jobLevel(nj) >= lv, className(nj) + " " + tr("クラスLv") + " " + std::to_string(lv), tr("今") + " " + std::to_string(jobLevel(nj)));
    }
    paragraph(b.x, y + 8, b.w, tr("条件を満たすと、週を進めたときに開く。"), 13, kDim, kPText);
}

void KojinBlend::sideDetails(const Box&) {}

} // namespace ch
