// ============================================================================
// hata_task.hpp — the base of Hata's cut-scene tasks (src\hata\hatatask.cpp):
// OpTask (the opening) and EdTask (the ending).
//
// FUN_00504d70 constructs it (the task, a sprite object at +0x1e0 and ten
// anim objects at +0x260, FUN_00464c60 each), FUN_00504e10 destroys it (music
// and SE stopped, FUN_0041d280 / FUN_0040d610). The scenes run on a work area
// addressed by the original byte offsets, through endian-safe accessors, and
// .data tables addressed by the TW exe's VA (the JP build's copy when the JP
// version runs). The draw helpers (FUN_00504f10..FUN_00505cc0) draw from
// bank 10, which each scene fills from its own data file.
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "../state/exe_image.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace ch {

class HataTask : public Task {
public:
    // The scene's tables: `size` bytes at `twVa` of the TW exe or at `jpVa`
    // of the JP exe (exe_image.hpp); T8/TS16 address them by TW VA.
    HataTask(GameContext& ctx, std::uint32_t twVa, std::uint32_t jpVa, std::size_t size)
        : ctx_(ctx), tablesVa_(twVa),
          table_(ctx.version == GameVersion::Jp ? exe::own(GameVersion::Jp).bytes(jpVa, size)
                                                : exe::own(GameVersion::Tw).bytes(twVa, size)) {}

protected:
    static constexpr std::uint16_t kBank = 10;

    // ---- little-endian views of the work area -------------------------------
    template <class T>
    class Le16 {
    public:
        Le16(std::uint8_t* p) : p_(p) {}
        operator T() const { return static_cast<T>(p_[0] | p_[1] << 8); }
        Le16& operator=(int v) {
            p_[0] = static_cast<std::uint8_t>(v);
            p_[1] = static_cast<std::uint8_t>(v >> 8);
            return *this;
        }
        Le16& operator=(const Le16& o) { return *this = static_cast<int>(static_cast<T>(o)); }
    private:
        std::uint8_t* p_;
    };
    class Le32 {
    public:
        Le32(std::uint8_t* p) : p_(p) {}
        operator std::uint32_t() const {
            return std::uint32_t(p_[0]) | std::uint32_t(p_[1]) << 8 | std::uint32_t(p_[2]) << 16 |
                   std::uint32_t(p_[3]) << 24;
        }
        Le32& operator=(std::uint32_t v) {
            for (int i = 0; i < 4; ++i) p_[i] = static_cast<std::uint8_t>(v >> (8 * i));
            return *this;
        }
    private:
        std::uint8_t* p_;
    };

    static constexpr std::size_t kWorkSize = 0x918;   // the object's size (FUN_00428a40(0x918))
    std::uint8_t* at(int off, int n) {
        if (off < 0 || std::size_t(off) + std::size_t(n) > kWorkSize) throw std::out_of_range("HataTask work area");
        return w_.data() + off;
    }
    std::int8_t&    I8(int off)  { return reinterpret_cast<std::int8_t&>(*at(off, 1)); }  // char aliasing
    std::uint8_t&   U8(int off)  { return *at(off, 1); }
    Le16<std::int16_t>  S16(int off) { return {at(off, 2)}; }
    Le16<std::uint16_t> U16(int off) { return {at(off, 2)}; }
    Le32            U32(int off) { return {at(off, 4)}; }
    static constexpr int OFF(int off) { return off; }

    // ---- .data tables, addressed by the TW exe's VA --------------------------
    std::uint8_t  T8(std::uint32_t va) const;
    std::int16_t  TS16(std::uint32_t va) const { return static_cast<std::int16_t>(T8(va) | T8(va + 1) << 8); }
    std::uint16_t TU16(std::uint32_t va) const { return static_cast<std::uint16_t>(TS16(va)); }

    static constexpr std::uint32_t whiteA(std::uint32_t a) { return (a & 0xFF) << 24 | 0x00FFFFFFu; }

    // ---- draw helpers (FUN_00504f10..FUN_00505cc0), bank 10 -----------------
    void opSprite(int prio, int frame, int x, int y, std::uint16_t flags, SrcRect src,
                  int w, int h, Rgba8 color, int scaleX, int scaleY, int rot, std::uint8_t blend);
    void opDraw(int prio, int frame, int x, int y);                                           // FUN_00504f10
    void opDrawSrc(int prio, int frame, int x, int y, int sx, int sy, int w, int h);          // FUN_00505000
    void opDrawTint(int prio, int frame, int x, int y, int a, int r, int g, int b);           // FUN_00505260
    void opDrawSrcTint(int prio, int frame, int x, int y, int sx, int sy, int w, int h,
                       int a, int r, int g, int b);                                           // FUN_00505580
    void opDrawStretchC(int prio, int frame, int x, int y, int dw, int dh, int sw, int sh);   // FUN_00505460
    void opDrawSrcSizeC(int prio, int frame, int x, int y, int sx, int sy, int dw, int dh,
                        int sw, int sh);                                                      // FUN_00505a60
    void opDrawSrcSizeTintC(int prio, int frame, int x, int y, int sx, int sy, int dw, int dh,
                            int a, int r, int g, int b, int sw, int sh);                      // FUN_00505b90
    void opDrawSrcAdd(int prio, int frame, int x, int y, int sx, int sy, int w, int h);       // FUN_00505cc0

    // FUN_00410310(x, y, w, h, prio, argb, mode): mode 0 opaque, 1 alpha blend.
    void fillRect(int x, int y, int w, int h, int prio, std::uint32_t argb, int mode);

    GameContext&                         ctx_;
    std::array<std::uint8_t, kWorkSize>  w_{};
    std::string                          layoutBank_;   // the data file of bank 10, when a language pack may move its sprites

private:
    std::uint32_t                  tablesVa_;
    std::span<const std::uint8_t>  table_;
};

} // namespace ch
