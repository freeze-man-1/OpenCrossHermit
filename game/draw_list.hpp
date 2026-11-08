// ============================================================================
// draw_list.hpp — retained per-frame draw queue (replaces the D3D8 sprite list)
//
// Tasks queue draws while they run, like FUN_004077c0 / FUN_00407870 /
// FUN_004079c0 did. The queue is flushed once per rendered frame like
// FUN_0040e330: priority 0xffff..0 (higher = farther back), and within one
// priority the most recently queued draw goes first (lists were push-front).
// ============================================================================
#pragma once
#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ch {

// Vertex colour bytes (sprite +0x64..+0x67). RGB 0x80 == 1.0; above 0x80
// adds specular brightness (LUTs at VA 0x005ff148 / 0x005ff248).
struct Rgba8 {
    std::uint8_t r = 0x80, g = 0x80, b = 0x80, a = 0xFF;
};

struct SrcRect { std::int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; };   // texels, x1/y1 exclusive

// Sprite object flags (+0x58), as interpreted by FUN_00407ad0.
enum SpriteFlag : std::uint16_t {
    kSprScale       = 0x01,   // size *= scale% / 100
    kSprRotate      = 0x02,   // rotate by `rotation` degrees about the pivot
    kSprPivotCentre = 0x04,   // pivot = centre of the quad (else pivotX/Y)
    kSprCentre      = 0x08,   // (x, y) is the quad centre
    kSprFlipU       = 0x10,
    kSprFlipV       = 0x20,
    kSprSrcRect     = 0x40,   // use `src`; otherwise the whole image
    kSprBank        = 0x80,   // texture comes from a bank (set by FUN_00407870)
};

// Sprite blend byte (+0x5c), as set up in FUN_0040e650.
enum SpriteBlend : std::uint8_t {
    kBlendTest  = 0,          // alpha test only (alpha > 8), no blending
    kBlendAlpha = 1,          // SRCALPHA / INVSRCALPHA when vertex alpha < 0xff
    kBlendAdd   = 2,          // SRCALPHA / DESTALPHA (== ONE on an RGB target)
};

// Mirrors the sprite object (task-relative offsets in TitleTask are +0x38).
struct SpriteCmd {
    std::uint16_t bank  = 0;            // FUN_00407870 arg 1 (0x5a = Title.bin)
    std::uint16_t frame = 0;            // FUN_00407870 arg 2
    std::int32_t  x = 0, y = 0;         // +0x68 / +0x6c (FUN_004079c0 args)
    std::int16_t  w = 0, h = 0;         // +0x38 / +0x3a, 0 = image size
    SrcRect       src{};                // +0x78..+0x7e, with kSprSrcRect
    std::uint16_t flags    = kSprBank;  // +0x58
    std::uint8_t  blend    = kBlendTest;// +0x5c
    std::uint8_t  aux60    = 0;         // +0x60, copied to quad +8 (meaning unknown)
    Rgba8         color{};              // +0x64..+0x67 (B, G, R, A in memory)
    std::int16_t  scaleX = 100, scaleY = 100;   // +0x70 / +0x72, percent
    std::int16_t  rotation = 0;         // +0x74, degrees clockwise
    float         pivotX = 0, pivotY = 0;       // +0x50 / +0x54
    std::uint16_t priority = 100;       // +0x76
};

// FUN_00410310(x, y, w, h, prio, argb, 1)
struct FillCmd {
    std::int32_t  x = 0, y = 0, w = 0, h = 0;
    std::uint16_t priority = 0;
    std::uint32_t argb = 0;
    std::uint8_t  blend = kBlendAlpha;   // FUN_00410310 last argument; kBlendAdd lightens
};

// FUN_0040ec50(x, y, prio, argb, 0, text) and GDI text sprites (FUN_00408e30).
// fontPx 0 = the built-in debug font; otherwise UTF-8 through TextRenderer.
struct TextCmd {
    std::int32_t  x = 0, y = 0;
    std::uint16_t priority = 0;
    std::uint32_t argb = 0;
    std::string   text;
    std::uint16_t fontPx = 0;
    bool          bold = false;
    bool          centred = false;   // x, y is the centre (texture w/2, h/2 subtracted)
};

// Backend for bank-based sprites (BankSprites in sprite_bank.hpp).
class ISpriteBanks {
public:
    virtual ~ISpriteBanks() = default;
    virtual void loadBank(std::uint16_t bank, std::vector<std::uint8_t> bytes) = 0;   // FUN_00416790
    virtual void unloadBank(std::uint16_t bank) = 0;                                  // FUN_0041f4e0
    // Image size of a frame (sprite +0x38/+0x3a after FUN_00407870); {0, 0} if missing.
    [[nodiscard]] virtual std::pair<int, int> frameSize(std::uint16_t bank, std::uint16_t frame) = 0;
    virtual void drawSprite(SDL_Renderer* r, const SpriteCmd& c) = 0;
    virtual void drawText(SDL_Renderer* r, const TextCmd& c) = 0;
};

class DrawList {
public:
    void sprite(const SpriteCmd& c) { cmds_.emplace_back(c); }
    void fill(const FillCmd& c)     { cmds_.emplace_back(c); }
    void text(TextCmd c)            { cmds_.emplace_back(std::move(c)); }
    void clear() noexcept           { cmds_.clear(); }
    [[nodiscard]] bool empty() const noexcept { return cmds_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return cmds_.size(); }
    [[nodiscard]] std::size_t countSprites() const noexcept;

    void render(SDL_Renderer* r, ISpriteBanks& banks) const;
    // Debug: one line per command in render order (tools/composite_dump.py reads it).
    void dump(std::FILE* f, int tag) const;

private:
    using Cmd = std::variant<SpriteCmd, FillCmd, TextCmd>;
    [[nodiscard]] std::vector<std::size_t> renderOrder() const;
    std::vector<Cmd> cmds_;
};

} // namespace ch
