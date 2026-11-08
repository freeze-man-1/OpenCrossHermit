// ============================================================================
// anm_player.cpp
// ============================================================================
#include "anm_player.hpp"
#include "../bank_format.hpp"

#include <SDL.h>

#include <algorithm>
#include <stdexcept>

namespace ch {

// ------------------------------------------------------------- AnmFile ---
std::uint8_t AnmFile::u8(std::uint32_t off) const {
    if (off >= bytes_.size()) throw std::out_of_range("AnmFile: read past end");
    return bytes_[off];
}

std::uint16_t AnmFile::u16(std::uint32_t off) const {
    return static_cast<std::uint16_t>(u8(off) | u8(off + 1) << 8);
}

std::uint32_t AnmFile::block(int i) const {
    // Container lookup (FUN_004214f0): base + u32[base + 8 + 4i]. An empty
    // block points at the next one; the original relies on that (set 2 aliases set 3).
    const std::uint32_t at = 8 + 4u * static_cast<std::uint32_t>(i);
    return u16(at) | std::uint32_t(u16(at + 2)) << 16;
}

void AnmFile::load(std::vector<std::uint8_t> bytes, std::uint16_t bank, ISpriteBanks& banks, int palette) {
    bytes_ = std::move(bytes);
    bank_ = bank;
    const auto blocks = splitBank(bytes_);
    if (blocks.size() < 7) throw std::runtime_error("AnmFile: fewer than 7 blocks");
    std::vector<std::uint8_t> sprites(blocks[6].begin(), blocks[6].end());
    if (palette > 0) applyPalette(sprites, blocks, palette);
    banks.loadBank(bank, std::move(sprites));
}

// FUN_00409b70 with a palette index -> FUN_0041ec40 / FUN_0041f0e0: block 7
// entry `palette - 1` (256 BGRX colours) replaces the palette of every BMP
// sprite whose block 8 byte is 0. The colour-key rules then apply as usual.
void AnmFile::applyPalette(std::vector<std::uint8_t>& sprites,
                           const std::vector<std::span<const std::uint8_t>>& blocks, int palette) {
    if (blocks.size() < 8) return;
    const auto pals = splitBank(blocks[7]);
    if (palette - 1 >= int(pals.size()) || pals[std::size_t(palette - 1)].size() < 0x400) {
        SDL_Log("AnmFile: palette %d missing", palette);
        return;
    }
    const auto pal = pals[std::size_t(palette - 1)];
    const auto keep = blocks.size() > 8 ? blocks[8] : std::span<const std::uint8_t>{};
    const auto entries = splitBank(sprites);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i < keep.size() && keep[i] != 0) continue;
        const auto e = entries[i];
        if (e.size() < 0x36 || e[0] != 'B' || e[1] != 'M') continue;
        const std::uint32_t pixOff = std::uint32_t(e[10]) | std::uint32_t(e[11]) << 8 | std::uint32_t(e[12]) << 16 |
                                     std::uint32_t(e[13]) << 24;
        const std::size_t n = std::min<std::size_t>(pixOff > 0x36 ? (pixOff - 0x36) & ~std::size_t{3} : 0, 0x400);
        const std::size_t at = std::size_t(e.data() - sprites.data()) + 0x36;
        if (at + n <= sprites.size()) std::copy_n(pal.begin(), n, sprites.begin() + std::ptrdiff_t(at));
    }
}

int AnmFile::animCount(int set) const {
    if (set < 0 || set > 3) return 0;
    const std::uint32_t b = block(set);
    return static_cast<int>(u16(b + 4) | std::uint32_t(u16(b + 6)) << 16);
}

std::uint32_t AnmFile::animStart(int set, int no) const {
    const std::uint32_t b = block(set);
    const std::uint32_t at = b + 8 + 4u * static_cast<std::uint32_t>(no);
    return b + (u16(at) | std::uint32_t(u16(at + 2)) << 16);
}

AnmFile::Cell AnmFile::cell(std::uint32_t index) const {
    const std::uint32_t c = block(4) + index * 10;
    return {s16(c), s16(c + 2), s16(c + 4), u8(c + 6), u8(c + 7), u8(c + 8)};
}

AnmFile::TexPos AnmFile::texPos(std::uint32_t index) const {
    const std::uint32_t t = block(5) + index * 8;
    return {u16(t), u16(t + 2), u16(t + 4), u16(t + 6)};
}

// ----------------------------------------------------------- AnmPlayer ---
// FUN_0040b5e0 + FUN_00409ef0: first free pool slot, reset.
AnmPlayer::NodeId AnmPlayer::alloc() {
    for (NodeId i = 0; i < static_cast<NodeId>(kPoolSize); ++i) {
        if (!pool_[i].used) {
            pool_[i] = Node{};
            pool_[i].used = true;
            pool_[i].top = i;
            return i;
        }
    }
    SDL_Log("AnmPlayer: node pool exhausted");
    return kNone;
}

AnmPlayer::NodeId AnmPlayer::newRoot(const AnmFile& file) {
    const NodeId n = alloc();
    if (n != kNone) pool_[n].file = &file;
    return n;
}

void AnmPlayer::freeRoot(NodeId root) {
    if (root == kNone) return;
    release(root);       // frees the children
    pool_[root].used = false;
}

// FUN_00409ff0
void AnmPlayer::play(NodeId root, int set, int no, std::uint8_t flags) {
    Node& r = pool_[root];
    for (NodeId c = r.next; c != kNone;) {
        const NodeId next = pool_[c].next;
        if (!pool_[c].external) release(c);
        c = next;
    }
    if (!r.file || !r.file->loaded() || set > 3 || no >= r.file->animCount(set)) return;
    r.flags = flags;
    r.depth = 0;
    r.prioClass = 7;
    r.x = r.y = 0;
    r.top = root;
    r.ended = false;
    r.loop = 0;
    start(root, set, no);
}

// FUN_0040a400
void AnmPlayer::start(NodeId n, int set, int no) {
    Node& node = pool_[n];
    if (set < 0 || set > 3 || no < 0 || no >= node.file->animCount(set)) {
        SDL_Log("AnmPlayer: bad animation %d/%d", set, no);
        return;
    }
    node.cmd = node.file->animStart(set, no);
    enter(n);
}

// FUN_0040a520: side effects of arriving at a record.
void AnmPlayer::enter(NodeId n) {
    Node& node = pool_[n];
    if (!node.cmd) return;
    const AnmFile& f = *node.file;
    const std::uint8_t op = f.u8(node.cmd);
    switch (op & 3) {
        case 0: {
            node.frame  = node.cmd;
            node.frames = f.s16(node.cmd + 8);
            if (op & 4) pool_[node.top].signal9 = true;
            if (op & 8) pool_[node.top].signal10 = true;
            if ((node.flags & 0x40) || node.depth == 0) {
                const std::uint32_t idx = f.u8(node.cmd + 2) | (f.u16(node.cmd + 6) & 0xF) << 8;
                if (idx < 0xFFF) node.prioClass = f.cell(idx).prioClass;
            }
            break;
        }
        case 3:
            if (node.loop == 0) node.loop = f.u8(node.cmd + 1) == 0x7F ? 0x7F : f.u8(node.cmd + 1) + 1;
            break;
        default:
            break;
    }
}

// FUN_0040a800
bool AnmPlayer::step(NodeId n) {
    Node& node = pool_[n];
    node.updated = true;
    if (node.ended) {
        node.cmd = 0;
        release(n);
        return true;
    }
    if (!node.cmd) return true;
    const AnmFile& f = *node.file;

    auto advance = [&] { node.cmd += 10; enter(n); };
    for (;;) {
        const std::uint8_t op = f.u8(node.cmd);
        switch (op & 3) {
            case 0:
                if (--node.frames > 0) return false;
                if (op & 0x80) { node.ended = true; return false; }
                advance();
                if ((f.u8(node.cmd) & 3) == 0) return false;
                continue;
            case 1:
                if (node.depth < 8 && f.s16(node.cmd + 2) != -1) spawnChild(n, node.cmd);
                if (op & 0x80) { node.ended = true; return false; }
                advance();
                continue;
            case 2:
                if (f.u16(node.cmd + 8) != 0xFFFF && playSe_) playSe_(f.u16(node.cmd + 8));
                if (op & 0x80) { node.ended = true; return false; }
                advance();
                continue;
            case 3:
                if (node.loop != 0x7F) {
                    if (node.loop == 0 || node.loop == 1) {
                        if (op & 0x80) { node.ended = true; return false; }
                        node.loop = 0;
                        advance();
                        continue;
                    }
                    --node.loop;
                }
                node.cmd += static_cast<std::uint32_t>(((op & 0x40) ? 10 : -10) * f.s16(node.cmd + 2));
                enter(n);
                continue;
        }
    }
}

// FUN_0040ab80: a root frees its non-external children; a child unlinks itself.
void AnmPlayer::release(NodeId n) {
    Node& node = pool_[n];
    if (node.depth == 0) {
        for (NodeId c = node.next; c != kNone;) {
            const NodeId next = pool_[c].next;
            if (!pool_[c].external) release(c);
            c = next;
        }
        return;
    }
    NodeId prev = node.top;
    while (prev != kNone && pool_[prev].next != n) prev = pool_[prev].next;
    if (prev != kNone) pool_[prev].next = node.next;
    node.used = false;
}

// FUN_0040ada0: child inserted right after the root.
void AnmPlayer::spawnChild(NodeId parent, std::uint32_t rec) {
    const NodeId c = alloc();
    if (c == kNone) return;
    Node& p = pool_[parent];
    Node& child = pool_[c];
    const AnmFile& f = *p.file;
    child.flags    = p.flags & 0xBF;
    child.top      = p.top;
    child.file     = p.file;
    child.external = p.external;
    child.next     = pool_[p.top].next;
    pool_[p.top].next = c;
    child.x = static_cast<std::int16_t>((child.flags & 1) ? p.x - f.s16(rec + 4) : p.x + f.s16(rec + 4));
    child.y = static_cast<std::int16_t>((child.flags & 2) ? p.y - f.s16(rec + 6) : p.y + f.s16(rec + 6));
    child.depth     = static_cast<std::uint8_t>(p.depth + 1);
    child.prioClass = static_cast<std::int8_t>(f.u8(rec + 1));
    start(c, 3, f.u16(rec + 2));
    step(c);
}

// FUN_0040a100: step the root, then every child not yet stepped this frame
// (children spawned during the pass are stepped on creation).
AnmPlayer::Signals AnmPlayer::takeSignals(NodeId root) {
    Node& n = pool_[root];
    const Signals s{n.signal9, n.signal10};
    n.signal9 = n.signal10 = false;
    return s;
}

bool AnmPlayer::update(NodeId root) {
    for (NodeId c = pool_[root].next; c != kNone; c = pool_[c].next) pool_[c].updated = false;
    const bool done = pool_[root].cmd == 0 || step(root);
    for (bool again = true; again;) {
        again = false;
        for (NodeId c = pool_[root].next; c != kNone; c = pool_[c].next) {
            if (!pool_[c].updated) { step(c); again = true; break; }
        }
    }
    return done;
}

// FUN_0040a330 -> FUN_0040a290: root first, then children in list order,
// sorted by class (FUN_0040b890), then drawn from the highest class down;
// one priority for all three classes.
void AnmPlayer::draw(NodeId root, DrawList& out, ISpriteBanks& banks, const SpriteTemplate& spr,
                     int x, int y, std::uint16_t prio) const {
    draw(root, out, banks, spr, x, y, prio, prio, prio);
}

// FUN_0040a290: three priorities by cell class (0 -> b, 1..0xc -> a, else c).
void AnmPlayer::draw(NodeId root, DrawList& out, ISpriteBanks& banks, const SpriteTemplate& spr,
                     int x, int y, std::uint16_t prioA, std::uint16_t prioB, std::uint16_t prioC) const {
    if (root == kNone) return;
    std::vector<NodeId> list{root};
    for (NodeId c = pool_[root].next; c != kNone; c = pool_[c].next) list.push_back(c);
    // FUN_0040b890: the original's exchange sort on node +0x18, ascending
    // (kept as is, so equal classes end up in the same order).
    for (std::size_t i = 0; i + 1 < list.size(); ++i)
        for (std::size_t j = i + 1; j < list.size(); ++j)
            if (pool_[list[j]].prioClass < pool_[list[i]].prioClass) std::swap(list[i], list[j]);
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        drawNode(pool_[*it], out, banks, spr, x, y, prioA, prioB, prioC);
}

// FUN_0040b0f0
void AnmPlayer::drawNode(const Node& n, DrawList& out, ISpriteBanks& banks, const SpriteTemplate& spr,
                         int x, int y, std::uint16_t prioA, std::uint16_t prioB, std::uint16_t prioC) const {
    if (!n.frame) return;
    const AnmFile& f = *n.file;
    const std::uint16_t hi = f.u16(n.frame + 6);
    const std::array<std::uint32_t, 4> cells{
        f.u8(n.frame + 2) | (hi & 0x000Fu) << 8,
        f.u8(n.frame + 3) | (hi & 0x00F0u) << 4,
        f.u8(n.frame + 4) | (hi & 0x0F00u),
        f.u8(n.frame + 5) | (hi & 0xF000u) >> 4,
    };
    Rgba8 color = spr.color;
    if (n.flags & 8) { color.r = n.tint[0]; color.g = n.tint[1]; color.b = n.tint[2]; }

    for (int i = 3; i >= 0; --i) {
        if (cells[i] >= 0xFFF) continue;
        const AnmFile::Cell c = f.cell(cells[i]);
        std::uint8_t blend = kBlendTest;
        color.a = 0xFF;
        if ((c.mode & 3) == 1 && c.alpha != 0xFF) { color.a = c.alpha; blend = kBlendAlpha; }
        else if ((c.mode & 3) == 2)              { color.a = c.alpha; blend = kBlendAdd; }

        const auto [imgW, imgH] = banks.frameSize(f.bank(), static_cast<std::uint16_t>(c.sprite));
        const AnmFile::TexPos tp = f.texPos(static_cast<std::uint32_t>(c.sprite));
        const int ox = (n.flags & 1) ? tp.canvasW - (imgW + tp.trimX) : tp.trimX;
        const int oy = (n.flags & 2) ? tp.canvasH - (imgH + tp.trimY) : tp.trimY;
        int dx = ox - (tp.canvasW >> 1) + n.x + ((n.flags & 1) ? -c.dx : c.dx);
        // Original bug, kept: a vertically flipped cell drops the canvas
        // offset and node position (`mov`, not `add`, at 0x0040b414).
        int dy = (n.flags & 2) ? -c.dy : oy - (tp.canvasH >> 1) + n.y + c.dy;

        std::uint16_t flags = static_cast<std::uint16_t>((spr.flags & ~0x30u) | kSprBank);
        if (n.flags & 1) flags |= kSprFlipU;
        if (n.flags & 2) flags |= kSprFlipV;
        if ((flags & kSprScale) && spr.scaleX != 100) dx = spr.scaleX * dx / 100;
        if ((flags & kSprScale) && spr.scaleY != 100) dy = spr.scaleY * dy / 100;

        const std::uint16_t prio = c.prioClass == 0 ? prioB : c.prioClass < 0xD ? prioA : prioC;
        out.sprite({.bank = f.bank(), .frame = static_cast<std::uint16_t>(c.sprite), .x = x + dx, .y = y + dy,
                    .flags = flags, .blend = blend, .color = color, .scaleX = spr.scaleX,
                    .scaleY = spr.scaleY, .priority = prio});
    }
}

} // namespace ch
