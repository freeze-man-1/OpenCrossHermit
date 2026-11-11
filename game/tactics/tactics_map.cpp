// ============================================================================
// tactics_map.cpp
// ============================================================================
#include "tactics_map.hpp"
#include "../state/game_logic.hpp"
#include "../ysub/ysub.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <tuple>

namespace ch {

namespace {
constexpr std::uint32_t kMapFiles = 0x6b13f0;   // char*[4] per map id
constexpr int kHeader = 0x10, kCellBytes = 6;

std::string mapFile(int id, int kind, GameVersion v) {
    const std::uint32_t p = static_cast<std::uint32_t>(logic::sdS32(kMapFiles + std::uint32_t(id * 4 + kind) * 4, v));
    std::string name;
    for (std::uint32_t i = 0; i < 32; ++i) {
        const char c = static_cast<char>(logic::sdU8(p + i, v));
        if (c == 0) break;
        name.push_back(c);
    }
    return "data\\tactics\\map\\" + name;
}
} // namespace

// FUN_0043a060 builds the map at its defaults (the members' initialisers
// here; its locks +0x26595.. free, the box's +0x265dc 1, so the box always
// shows while dragged). FUN_0043a270 destroys it through FUN_0043a320.
TacticsMap::~TacticsMap() { unload(); }

// FUN_0043a320: the map's banks and buffers go (FUN_0043cf10 frees the
// three fog grids).
void TacticsMap::unload() {
    if (ctx_ != nullptr) ctx_->sprites->unloadBank(kBank);
    if (ctx_ != nullptr && miniW_ > 0) ctx_->sprites->unloadBank(kMiniBank);
    miniW_ = miniH_ = 0;
    ctx_ = nullptr;
    bin_.clear();
    vpt_.clear();
    now_.clear();
    last_.clear();
    ever_.clear();
}

bool TacticsMap::load(GameContext& ctx, int mapId) {
    unload();
    try {
        bin_ = ctx.readData(mapFile(mapId, 0, ctx.version));
        std::vector<std::uint8_t> pages = ctx.readData(mapFile(mapId, 1, ctx.version));
        if (bin_.size() < kHeader || pages.size() < 0x404) throw std::runtime_error("short map file");
        ctx_ = &ctx;
        if (bin_.size() < std::size_t(kHeader + cellsW() * cellsH() * kCellBytes)) throw std::runtime_error("short map cells");
        ctx.sprites->loadBank(kBank, std::vector<std::uint8_t>(pages.begin() + 0x404, pages.end()));
        if (mapCom_.empty()) mapCom_ = ctx.readData("data\\Tactics\\mapcom.bin");   // FUN_0043a510
    } catch (const std::exception& ex) {
        SDL_Log("[tactics] map %d: %s", mapId, ex.what());
        ctx_ = nullptr;
        bin_.clear();
        return false;
    }
    // FUN_0044e440: mapNN.vpt (kind 3), the route graph; some maps have none.
    try {
        vpt_ = ctx.readData(mapFile(mapId, 3, ctx.version));
    } catch (const std::exception&) {
        vpt_.clear();
    }
    // FUN_004404a0: mapNN.bmp, an 8 bpp bitmap, as a one-picture bank.
    try {
        std::vector<std::uint8_t> bmp = ctx.readData(mapFile(mapId, 2, ctx.version));
        std::vector<std::uint8_t> bank(12);
        const auto put32 = [&](std::size_t o, std::uint32_t v) { for (int k = 0; k < 4; ++k) bank[o + std::size_t(k)] = std::uint8_t(v >> (8 * k)); };
        put32(0, std::uint32_t(12 + bmp.size()));
        put32(4, 1);
        put32(8, 12);
        bank.insert(bank.end(), bmp.begin(), bmp.end());
        ctx.sprites->loadBank(kMiniBank, std::move(bank));
        std::tie(miniW_, miniH_) = ctx.sprites->frameSize(kMiniBank, 0);
    } catch (const std::exception& ex) {
        SDL_Log("[tactics] map %d minimap: %s", mapId, ex.what());
        miniW_ = miniH_ = 0;
    }
    // FUN_0043ce10: the three fog grids, a byte per cell, made and zeroed
    // by FUN_0043cfe0 / FUN_0043d0c0.
    now_.assign(std::size_t(cellsW() * cellsH()), 0);
    last_ = now_;
    ever_ = now_;
    scrollX_ = scrollY_ = 0;
    return true;
}

void TacticsMap::scrollTo(int x, int y) {
    if (!loaded()) return;
    x = std::min(std::max(x, 0), cellsW() * kCellW - kViewW);
    y = std::min(std::max(y, 0), cellsH() * kCellH - kViewH);
    scrollX_ = x;
    scrollY_ = y;
}

int TacticsMap::viewX() const noexcept {
    if (margin_ == 0 || !loaded()) return scrollX_;
    const int lo = margin_, hi = cellsW() * kCellW - kViewW - margin_;
    return hi < lo ? (lo + hi) / 2 : std::clamp(scrollX_, lo, hi);   // a map narrower than the view sits in its middle
}

std::uint8_t TacticsMap::cell(int cx, int cy, int field) const {
    if (!loaded() || cx < 0 || cy < 0 || cx >= cellsW() || cy >= cellsH()) return 0;
    return bin_[std::size_t(kHeader + (cy * cellsW() + cx) * kCellBytes + field)];
}

void TacticsMap::setCell(int cx, int cy, int field, std::uint8_t v) {
    if (!loaded() || cx < 0 || cy < 0 || cx >= cellsW() || cy >= cellsH()) return;
    bin_[std::size_t(kHeader + (cy * cellsW() + cx) * kCellBytes + field)] = v;
}

// mapcom.bin block `b` (FUN_004214f0 on the file); 0 if missing.
std::uint32_t TacticsMap::comBlock(int b) const {
    const auto rd = [&](std::size_t o) {
        return o + 4 <= mapCom_.size() ? std::uint32_t(mapCom_[o]) | std::uint32_t(mapCom_[o + 1]) << 8 |
                                             std::uint32_t(mapCom_[o + 2]) << 16 | std::uint32_t(mapCom_[o + 3]) << 24
                                       : 0u;
    };
    return std::uint32_t(b) < rd(4) ? rd(8 + std::size_t(b) * 4) : 0u;
}

// FUN_0043be60: mapcom block 1 holds 0x4c bytes per terrain, s32 at
// +0xc + 0x10 type per movement type (4 counts as 0); 0 = impassable.
int TacticsMap::moveRate(int cx, int cy, int type) const {
    if (!loaded() || cx < 0 || cy < 0 || cx >= cellsW() || cy >= cellsH()) return 0;
    if (type == 4) type = 0;
    if (type < 0 || type > 3 || comBlock(1) == 0) return 0;
    return comS32(comBlock(1) + std::uint32_t(cell(cx, cy, 0)) * 0x4c + 0xc + std::uint32_t(type) * 0x10);
}

std::int32_t TacticsMap::comS32(std::uint32_t at) const {
    if (std::size_t(at) + 4 > mapCom_.size()) return 0;
    return static_cast<std::int32_t>(std::uint32_t(mapCom_[at]) | std::uint32_t(mapCom_[at + 1]) << 8 |
                                     std::uint32_t(mapCom_[at + 2]) << 16 | std::uint32_t(mapCom_[at + 3]) << 24);
}

// Block 0 is itself a block file: mask r ((2r + 1)^2 bytes) at its own
// offset r. 0 if missing or cut short.
std::uint32_t TacticsMap::mask(int sight) const {
    const std::uint32_t masks = comBlock(0);
    if (sight < 0 || sight > 0x20 || masks == 0 || std::size_t(masks) + 8 > mapCom_.size() ||
        std::uint32_t(sight) >= std::uint32_t(comS32(masks + 4)))
        return 0;
    const std::uint32_t at = masks + std::uint32_t(comS32(masks + 8 + std::uint32_t(sight) * 4));
    const std::size_t size = std::size_t(2 * sight + 1);
    return std::size_t(at) + size * size <= mapCom_.size() ? at : 0;
}

bool TacticsMap::inRange(int x0, int y0, int x1, int y1, int range) const {
    if (!loaded()) return false;
    const std::uint32_t m = mask(range);
    if (m == 0) return false;
    const int size = 2 * range + 1, half = range;
    const int dx = std::abs(x0 - x1), dy = std::abs(y0 - y1);
    if (dx > half || dy > half) return false;
    return mapCom_[m + std::uint32_t((dy + half) * size + dx + half)] != 0;
}

void TacticsMap::fogFrame() {
    std::swap(now_, last_);
    std::fill(now_.begin(), now_.end(), std::uint8_t{0});
}

void TacticsMap::see(int cx, int cy, int sight) {
    if (!loaded()) return;
    const std::uint32_t m = mask(sight);
    if (m == 0) return;
    const int size = 2 * sight + 1;
    for (int y = 0; y < size; ++y) {
        const int my = cy - sight + y;
        if (my < 0 || my >= cellsH()) continue;
        for (int x = 0; x < size; ++x) {
            const int mx = cx - sight + x;
            if (mx < 0 || mx >= cellsW()) continue;
            const std::uint8_t v = mapCom_[m + std::uint32_t(y * size + x)];
            if (v == 0) continue;
            const std::size_t i = std::size_t(my * cellsW() + mx);
            now_[i] = std::max(now_[i], v);
            ever_[i] = std::max(ever_[i], now_[i]);
        }
    }
}

// The grids' getters: FUN_0043d1a0 "now" (+0xa4), FUN_0043d1d0 "last"
// (+0xa8), FUN_0043d200 "ever" (+0xa0).
bool TacticsMap::explored(int cx, int cy) const {
    if (!loaded() || cx < 0 || cy < 0 || cx >= cellsW() || cy >= cellsH()) return false;
    return ever_[std::size_t(cy * cellsW() + cx)] != 0;
}

void TacticsMap::drawMinimap(int prio) const {
    if (!loaded() || miniW_ <= 0 || miniH_ <= 0) return;
    // FUN_00440a10: the cells never seen in black (a fill per run of a row,
    // queued before the picture so that it lies on top at the same priority);
    // FUN_00440af0 gives the cell under each minimap pixel (0x8000 when
    // never seen). FUN_00440c10 copies the bitmap's rows into the surface,
    // which the port's bank does at load.
    const int w = cellsW(), h = cellsH();
    for (int cy = 0; cy < h; ++cy) {
        const int y0 = cy * miniH_ / h, y1 = (cy + 1) * miniH_ / h;
        if (y1 <= y0) continue;
        int cx = 0;
        while (cx < w) {
            if (explored(cx, cy)) { ++cx; continue; }
            const int start = cx;
            while (cx < w && !explored(cx, cy)) ++cx;
            const int x0 = start * miniW_ / w, x1 = cx * miniW_ / w;
            if (x1 > x0) ysub::fill(*ctx_, kMiniX + x0, kMiniY + y0, x1 - x0, y1 - y0, prio, 0xff000000u);
        }
    }
    ysub::drawSub(*ctx_, {.bank = static_cast<std::uint8_t>(kMiniBank), .frame = 0}, prio, kMiniX, kMiniY);
    // FUN_00441170: the view's frame.
    const int mapW = w * kCellW, mapH = h * kCellH;
    const int vx = std::max(viewX() - margin_, 0), vw = std::min(kViewW + 2 * margin_, mapW);
    const int fx = kMiniX + vx * miniW_ / mapW, fy = kMiniY + scrollY_ * miniH_ / mapH;
    const int fw = vw * miniW_ / mapW, fh = kViewH * miniH_ / mapH;
    ysub::fill(*ctx_, fx, fy, fw, 1, 0xc5, 0xffffffffu);
    ysub::fill(*ctx_, fx, fy + fh - 1, fw, 1, 0xc5, 0xffffffffu);
    ysub::fill(*ctx_, fx, fy, 1, fh, 0xc5, 0xffffffffu);
    ysub::fill(*ctx_, fx + fw - 1, fy, 1, fh, 0xc5, 0xffffffffu);
}

// FUN_0043c530: range mask `range` round (cx, cy), each of its cells on
// the map filled with DAT_005ff9cc (0x200080ff) at 0xeff, below the units.
void TacticsMap::drawRange(int cx, int cy, int range) const {
    if (!loaded() || range < 0 || range > 0x20) return;
    const std::uint32_t m = mask(range);
    if (m == 0) return;
    const int size = 2 * range + 1;
    const int x0 = cx - (size - 1) / 2, y0 = cy - (size - 1) / 2;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const int mx = x0 + x, my = y0 + y;
            if (mx < 0 || my < 0 || mx >= cellsW() || my >= cellsH() || mapCom_[m + std::uint32_t(y * size + x)] == 0) continue;
            ysub::fill(*ctx_, mx * kCellW - viewX(), my * kCellH - scrollY_ + kViewY, kCellW, kCellH, 0xeff, 0x200080ffu);
        }
}

bool TacticsMap::visible(int cx, int cy) const {
    if (!loaded() || cx < 0 || cy < 0 || cx >= cellsW() || cy >= cellsH()) return false;
    return last_[std::size_t(cy * cellsW() + cx)] != 0;
}

void TacticsMap::combatRates(int cx, int cy, int type, int& evade, int& defence) const {
    evade = defence = 0;
    if (!loaded() || cx < 0 || cy < 0 || cx >= cellsW() || cy >= cellsH() || comBlock(1) == 0) return;
    if (type == 4) type = 0;
    if (type < 0 || type > 3) return;
    const std::uint32_t at = comBlock(1) + std::uint32_t(cell(cx, cy, 0)) * 0x4c + std::uint32_t(type) * 0x10;
    evade = comS32(at + 0x10);
    defence = comS32(at + 0x14);
}

int TacticsMap::magicGuardRate(int cx, int cy, int type) const {
    if (!loaded() || cx < 0 || cy < 0 || cx >= cellsW() || cy >= cellsH() || comBlock(1) == 0) return 0;
    if (type == 4) type = 0;
    if (type < 0 || type > 3) return 0;
    return comS32(comBlock(1) + std::uint32_t(cell(cx, cy, 0)) * 0x4c + std::uint32_t(type) * 0x10 + 0x18);
}

// FUN_0043b530 (with FUN_0043b710: the pointer's map pixel, or -1 off the
// view's rows 0x1e..0x257).
bool TacticsMap::cellAt(int sx, int sy, int& cx, int& cy) const {
    if (!loaded()) return false;
    const int vy = sy - kViewY;
    if (vy < 0 || vy > 0x23f) return false;
    const int wx = sx + viewX();
    if (wx < 0) return false;                        // left of a map narrower than the view
    cx = wx / kCellW;
    cy = vy / kCellH + scrollY_ / kCellH + (vy % kCellH + scrollY_ % kCellH) / kCellH;
    return cx < cellsW() && cy < cellsH();
}

void TacticsMap::draw(int prio) const {
    if (!loaded()) return;
    const int pageW = s16(8), pageH = s16(10), pagesX = s16(0xc);
    const int perPageX = pageW / kCellW, perPageY = pageH / kCellH;
    // The view's left edge on the map, floored to a cell (it is left of the map where that is narrower).
    const int left = viewX() - margin_, c0 = left >= 0 ? left / kCellW : -((-left + kCellW - 1) / kCellW);
    const int ox = c0 * kCellW - left - margin_, oy = kViewY - scrollY_ % kCellH;
    const int r0 = scrollY_ / kCellH, cols = 0x21 + (2 * margin_ + kCellW - 1) / kCellW;
    for (int r = 0; r < 0x25; ++r) {
        const int cy = r0 + r;
        if (cy >= cellsH()) break;
        // Consecutive cells on the same page with the same light go out as one sprite.
        int c = 0;
        while (c < cols) {
            const int cx = c0 + c;
            if (cx >= cellsW()) break;
            if (cx < 0) { ++c; continue; }
            const auto lightAt = [&](int x) {
                const std::size_t i = std::size_t(cy * cellsW() + x);
                return now_[i] != 0 ? now_[i] : static_cast<std::uint8_t>(ever_[i] / 3);
            };
            const std::uint8_t light = lightAt(cx);
            const int page = cx / perPageX + pagesX * (cy / perPageY);
            int n = 1;
            while (c + n < cols && cx + n < cellsW() && (cx + n) / perPageX == cx / perPageX && lightAt(cx + n) == light) ++n;
            if (light != 0) {
                // D3D modulate by light / 255; the draw list's colour is 0x80-based.
                ysub::drawSub(*ctx_,
                              {.bank = static_cast<std::uint8_t>(kBank), .frame = static_cast<std::uint16_t>(page),
                               .sx = cx % perPageX * kCellW, .sy = cy % perPageY * kCellH, .w = n * kCellW, .h = kCellH,
                               .rgb = static_cast<std::uint8_t>((light * 0x80 + 0x7f) / 0xff)},
                              prio, ox + c * kCellW, oy + r * kCellH);
            }
            c += n;
        }
    }
}

} // namespace ch
