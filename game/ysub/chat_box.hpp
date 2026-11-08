#pragma once
// tSysChatBox (system\syschatbox.cpp): a fixed ring of text lines, each a
// D3DX font text in the original (0x8c-byte SpFont records, FUN_00409300 /
// FUN_00409360). Fields: +0 the line count, +2 the lines written (held
// between count and 2 count once full), +4 the oldest line once full, +6
// newest first, +8 the lines.
//   FUN_00425090 / FUN_00425150  constructor: all zero
//   FUN_004250d0 / FUN_00425110  destructor: free the lines, zero
//   FUN_004251a0 / FUN_00425450  free the lines (FUN_00425380 their array delete)
//   FUN_004251e0                 create(font, count, colour): count empty lines
//   FUN_00425420                 newest first (the battle's log)
//   FUN_004254c0                 add a line
//   FUN_00425620                 draw: count rows `pitch` apart
// Here a line is a string; the draw list's text is the font.
#include <string>
#include <vector>

namespace ch {

class ChatBox {
public:
    ChatBox() = default;
    explicit ChatBox(int count, bool newestFirst = false) : newestFirst_(newestFirst) { create(count); }

    // FUN_004251e0 (the font and colour are the caller's draw call's here).
    void create(int count) {
        lines_.assign(std::size_t(count), {});
        written_ = 0;
        head_ = 0;
    }
    void setNewestFirst(bool on) { newestFirst_ = on; }   // FUN_00425420
    [[nodiscard]] int count() const { return int(lines_.size()); }

    // FUN_004254c0: into slot written % count; once full the oldest moves on.
    void add(std::string text) {
        const int n = count();
        if (n == 0) return;   // the original asserts rcnt != 0
        lines_[std::size_t(written_ % n)] = std::move(text);
        if (n <= written_ && ++head_ >= n) head_ = 0;
        if (++written_ >= n * 2) written_ = n;
    }

    // FUN_00425620: every row (empty ones too) from the oldest down, or
    // from the newest down when newest first; draw(row, line) per row.
    template <class Draw> void draw(Draw&& draw) const {
        const int n = count();
        if (!newestFirst_) {
            int k = head_;
            for (int i = 0; i < n; ++i) {
                draw(i, lines_[std::size_t(k)]);
                if (++k >= n) k = 0;
            }
        } else if (written_ != 0) {
            int k = (written_ - 1) % n;
            for (int i = 0; i < n; ++i) {
                draw(i, lines_[std::size_t(k)]);
                if (--k < 0) k = n - 1;
            }
        }
    }

private:
    std::vector<std::string> lines_;
    int written_ = 0, head_ = 0;
    bool newestFirst_ = false;
};

} // namespace ch
