// ============================================================================
// anm_player.hpp — AnmData keyframe animation system (src/system, 0x409a70..)
//
// File (FUN_00409b70), a bank-style container:
//   block 0..3  animation sets; each a sub-container of command streams
//   block 4     cell table, 10 bytes per cell
//   block 5     TexPos table, u16[4] per sprite: canvas w, h, trim x, y
//   block 6     sprite bank (BMP / DX entries), loaded into `bank`
//   block 7     optional palettes (container of 256 BGRX colours)
//   block 8     per sprite: 1 = keep its own palette
//
// Command stream: 10-byte records, type = byte0 & 3, byte0 & 0x80 = last.
//   0 frame  cells in bytes 2..5 (+ nibbles of u16 @6), duration u16 @8
//   1 spawn  child: set 3 anim u16 @2 at offset (s16 @4, s16 @6), class byte1
//   2 sound  SE id u16 @8 (0xffff = none)
//   3 loop   count byte1 (0x7f = forever), jump s16 @2 records (byte0 & 0x40 = forward)
//
// Cell (10 bytes): s16 dx, s16 dy, s16 sprite, u8 mode(&3: 0 opaque, 1 alpha,
//   2 additive), u8 alpha, u8 priority class, u8 unused.
//
//   FUN_00409ef0 root init        -> AnmPlayer::newRoot
//   FUN_00409ff0 play             -> play
//   FUN_0040a100 update           -> update      (FUN_0040a800 step, FUN_0040a520 enter)
//   FUN_0040a330 draw             -> draw        (FUN_0040b0f0 per node)
//   FUN_0040ada0 / FUN_0040af40   -> spawnChild
//   FUN_0040ab80 release          -> release
// The original's alternate timing path (node +0x2c/+0x48) is never set by the
// ported callers and is not implemented.
// ============================================================================
#pragma once
#include "../draw_list.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace ch {

class AnmFile {
public:
    struct Cell { std::int16_t dx, dy, sprite; std::uint8_t mode, alpha, prioClass; };
    struct TexPos { std::uint16_t canvasW, canvasH, trimX, trimY; };

    // Loads the file; block 6 goes into `banks` as sprite bank `bank`,
    // recoloured with block 7 palette `palette - 1` when `palette` > 0.
    void load(std::vector<std::uint8_t> bytes, std::uint16_t bank, ISpriteBanks& banks, int palette = 0);
    [[nodiscard]] bool loaded() const noexcept { return !bytes_.empty(); }
    [[nodiscard]] std::uint16_t bank() const noexcept { return bank_; }

    [[nodiscard]] int animCount(int set) const;                 // FUN_00409f70 -> rcnt
    [[nodiscard]] std::uint32_t animStart(int set, int no) const;   // byte offset of first record
    [[nodiscard]] Cell cell(std::uint32_t index) const;         // FUN_00409fb0
    [[nodiscard]] TexPos texPos(std::uint32_t index) const;     // FUN_0041ed00

    [[nodiscard]] std::uint8_t  u8(std::uint32_t off) const;
    [[nodiscard]] std::uint16_t u16(std::uint32_t off) const;
    [[nodiscard]] std::int16_t  s16(std::uint32_t off) const { return static_cast<std::int16_t>(u16(off)); }

private:
    [[nodiscard]] std::uint32_t block(int i) const;
    static void applyPalette(std::vector<std::uint8_t>& sprites,
                             const std::vector<std::span<const std::uint8_t>>& blocks, int palette);
    std::vector<std::uint8_t> bytes_;
    std::uint16_t bank_ = 0;
};

class AnmPlayer {
public:
    using NodeId = std::int32_t;
    static constexpr NodeId kNone = -1;
    static constexpr std::size_t kPoolSize = 0x800;   // DAT_00765728, 0x58-byte nodes

    // Sprite settings the caller prepared (the original passes a sprite object).
    struct SpriteTemplate {
        Rgba8         color{};
        std::uint16_t flags = 0;
        std::int16_t  scaleX = 100, scaleY = 100;
    };

    explicit AnmPlayer(std::function<void(std::uint32_t)> playSe = {}) : playSe_(std::move(playSe)) {}

    NodeId newRoot(const AnmFile& file);
    void   play(NodeId root, int set, int no, std::uint8_t flags);   // FUN_00409ff0
    // FUN_0040a100: true once the root's clip has ended (or none is playing).
    bool   update(NodeId root);
    void   draw(NodeId root, DrawList& out, ISpriteBanks& banks, const SpriteTemplate& spr,
                int x, int y, std::uint16_t prio) const;             // FUN_0040a330
    void   draw(NodeId root, DrawList& out, ISpriteBanks& banks, const SpriteTemplate& spr,
                int x, int y, std::uint16_t prioA, std::uint16_t prioB, std::uint16_t prioC) const;   // FUN_0040a290
    void   freeRoot(NodeId root);
    // The frames with flag 4 / 8 (+9 / +10 of the top node) the root's tree
    // reached since the last call; the battle's hit and landing moments
    // (FUN_0040a520 queues the node's packet on them, FUN_00482a30 and
    // FUN_004918d0 drain the queues). Clears them.
    struct Signals { bool hit = false, land = false; };
    Signals takeSignals(NodeId root);

private:
    struct Node {
        bool          used = false, updated = false, external = false;   // +0, +1, +2
        std::uint8_t  flags = 0;              // +3: 1 flipX, 2 flipY, 8 tint, 0x40 own class
        std::uint32_t cmd = 0;                // +4: byte offset of current record, 0 = none
        std::uint8_t  depth = 0;              // +8
        bool          signal9 = false, signal10 = false, ended = false;   // +9, +10, +0xb
        std::int16_t  frames = 0, loop = 0;   // +0xe, +0x10
        std::int16_t  x = 0, y = 0;           // +0x12, +0x14
        std::int32_t  prioClass = 0;          // +0x18
        const AnmFile* file = nullptr;        // +0x1c
        NodeId        next = kNone, top = kNone;   // +0x20, +0x24
        std::uint32_t frame = 0;              // +0x28: last frame record shown
        std::array<std::uint8_t, 3> tint{0x80, 0x80, 0x80};   // +0x54..+0x56 (R, G, B)
    };

    NodeId alloc();
    void   start(NodeId n, int set, int no);   // FUN_0040a400
    void   enter(NodeId n);                    // FUN_0040a520
    bool   step(NodeId n);                     // FUN_0040a800: true when ended
    void   release(NodeId n);                  // FUN_0040ab80
    void   spawnChild(NodeId parent, std::uint32_t rec);   // FUN_0040ada0
    void   drawNode(const Node& n, DrawList& out, ISpriteBanks& banks, const SpriteTemplate& spr,
                    int x, int y, std::uint16_t prioA, std::uint16_t prioB, std::uint16_t prioC) const;

    std::array<Node, kPoolSize>        pool_{};
    std::function<void(std::uint32_t)> playSe_;
};

} // namespace ch
