// ============================================================================
// tactics_map.hpp — the battle map (src\yoshida map class, task +0x263b4..)
//
// data\tactics\map\ files per map id (DAT_006b13f0: 4 names per id):
//   mapNN.bin  header s16[8]: pixel w, h; cells w, h (32 x 16 px each);
//              page w, h (256); pages across, down. Then 6 bytes per cell:
//              +0 terrain, +2 (1..3), +5 light (written at run time).
//   mapNN.map  u16 pages across, down, a 0x400-byte palette, then a sprite
//              bank of the 256 x 256 pages (bank 0x5c)
//   mapNN.bmp  the minimap (FUN_004404a0), mapNN.vpt  map editor data
//   FUN_0043a640  load .bin, cell overlay (u16 per cell)
//   FUN_0043a920  load .map into bank 0x5c
//   FUN_0043b460  set the scroll, clamped to the map less the 1024 x 568 view
//   FUN_0043b530  cell under the pointer
//   FUN_0043fd80  draw the visible 33 x 37 cells below the 30 px top bar,
//                 each lit by the fog (FUN_0043eba0 is the unfogged draw
//                 with the cells' own light, not used by the battle)
// The fog (+0xa0 / +0xa4 / +0xa8, one byte per cell): every frame the units
// of ours and our allies stamp their sight mask (FUN_0043d270) into "now"
// and "ever"; "last" is the finished previous frame (FUN_0043d130 swaps).
// A cell lit now is drawn at its sight value, one seen before at a third of
// it, one never seen not at all; enemies stand hidden outside last frame's
// sight (FUN_0043c160).
// data\tactics\mapcom.bin block 0: 33 range masks, (2r + 1)^2 bytes for
// radius r; block 1: per terrain and movement type the move rate and the
// evade and defence multipliers.
// ============================================================================
#pragma once
#include "../game_context.hpp"

#include <cstdint>
#include <vector>

namespace ch {

class TacticsMap {
public:
    static constexpr std::uint16_t kBank = 0x5c;
    static constexpr std::uint16_t kMiniBank = 0x5d;     // mapNN.bmp, the minimap (FUN_004404a0)
    static constexpr int kMiniX = 0x31c, kMiniY = 0x269; // its place on screen
    static constexpr int kCellW = 0x20, kCellH = 0x10;
    static constexpr int kViewY = 0x1e, kViewW = 0x400, kViewH = 0x238;

    ~TacticsMap();
    // Returns false (and logs) if the map's files are missing.
    bool load(GameContext& ctx, int mapId);
    void unload();
    [[nodiscard]] bool loaded() const noexcept { return ctx_ != nullptr; }
    // mapNN.vpt, the route graph's file (empty if the map has none).
    [[nodiscard]] const std::vector<std::uint8_t>& vpt() const noexcept { return vpt_; }

    void scrollTo(int x, int y);                   // FUN_0043b460
    void centreOn(int x, int y) { scrollTo(x - 0x200, y - 0x11d); }   // FUN_0043b400
    [[nodiscard]] int scrollX() const noexcept { return scrollX_; }
    [[nodiscard]] int scrollY() const noexcept { return scrollY_; }
    [[nodiscard]] int cellsW() const noexcept { return s16(4); }
    [[nodiscard]] int cellsH() const noexcept { return s16(6); }
    // FUN_0043b530: the cell under the screen point, or false outside the view.
    [[nodiscard]] bool cellAt(int sx, int sy, int& cx, int& cy) const;
    [[nodiscard]] std::uint8_t cell(int cx, int cy, int field) const;
    void setCell(int cx, int cy, int field, std::uint8_t v);   // FUN_0043bd00 (field 2)
    // FUN_0043be60: the terrain's move rate for movement type 0..4, 0 = blocked.
    [[nodiscard]] int moveRate(int cx, int cy, int type) const;
    // FUN_0043c1d0: cell (x1, y1) lies inside range mask `range` (0..32)
    // centred on (x0, y0).
    [[nodiscard]] bool inRange(int x0, int y0, int x1, int y1, int range) const;
    // FUN_0043b9e0: the terrain's evade and defence multipliers (percent)
    // for the movement type.
    void combatRates(int cx, int cy, int type, int& evade, int& defence) const;
    [[nodiscard]] int magicGuardRate(int cx, int cy, int type) const;   // FUN_0043b9e0's +0x18
    void draw(int prio) const;                     // FUN_0043fd80
    void drawRange(int cx, int cy, int range) const;   // FUN_0043c530
    // FUN_0043c920: the pointer on the screen's edge: 1 top (y < 1), 2
    // bottom (y > 0x2fe), 4 left (x < 1), 8 right (x > 0x3fe).
    [[nodiscard]] static int edges(int x, int y) noexcept {
        return (y < 1 ? 1 : y > 0x2fe ? 2 : 0) | (x < 1 ? 4 : x > 0x3fe ? 8 : 0);
    }

    // The fog.
    void fogFrame();                               // FUN_0043d230 -> FUN_0043d130
    void see(int cx, int cy, int sight);           // FUN_0043d270, sight 0..32
    [[nodiscard]] bool visible(int cx, int cy) const;   // FUN_0043c160
    [[nodiscard]] bool explored(int cx, int cy) const;  // +0xa0
    [[nodiscard]] bool hasMinimap() const noexcept { return miniW_ > 0; }
    // FUN_00440f00 (+ FUN_00440a10, FUN_00441170): the minimap, black where
    // nothing has been seen yet, and the view's frame.
    void drawMinimap(int prio) const;
    // Minimap pixels <-> map pixels (+0x26484 / +0x2647c).
    [[nodiscard]] int miniW() const noexcept { return miniW_; }
    [[nodiscard]] int miniH() const noexcept { return miniH_; }

private:
    [[nodiscard]] int s16(std::size_t off) const noexcept {
        return static_cast<std::int16_t>(bin_[off] | bin_[off + 1] << 8);
    }

    [[nodiscard]] std::uint32_t comBlock(int b) const;
    [[nodiscard]] std::int32_t comS32(std::uint32_t at) const;

    GameContext*               ctx_ = nullptr;
    std::vector<std::uint8_t>  mapCom_;            // data\Tactics\mapcom.bin (FUN_0043a510)
    std::vector<std::uint8_t>  bin_;               // +0x2659c
    std::vector<std::uint8_t>  vpt_;               // mapNN.vpt (FUN_0044e440), the route graph
    [[nodiscard]] std::uint32_t mask(int sight) const;   // this + 8 + sight * 4
    std::vector<std::uint8_t>  now_, last_, ever_;   // +0xa4 / +0xa8 / +0xa0
    int                        miniW_ = 0, miniH_ = 0;
    int                        scrollX_ = 0, scrollY_ = 0;   // +0x265a6 / +0x265a8
};

} // namespace ch
