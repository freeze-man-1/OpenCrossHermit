#pragma once
// tSysListBoxCtrl (system\syslistboxctrl.cpp): the cursor of a scrolling list
// of `count` items, `disp` rows shown. Fields (shorts): +0 count, +2 the
// cursor's row, +4 the first item shown, +6 rows shown. The chosen item is
// top + row.
//   FUN_00428120 / FUN_004281d0  constructor (FUN_00428160 / FUN_004281a0 its empty destructor)
//   FUN_00428220  init(count, disp): cursor and top to 0 (asserts 0 < disp)
//   FUN_004282b0  setRow: a shown row, when it is an item
//   FUN_00428310  index; FUN_00428350 disp; FUN_00428380 count; FUN_004283b0 top
//   FUN_004283e0  select(index): from the first item, down until there
//   FUN_00428720  first; FUN_00428760 last
//   FUN_00428810 -> FUN_00428520  up; FUN_00428850 -> FUN_004285f0  down
// (FUN_00428480, select counting up from the last item, has no caller.)
#include <algorithm>

namespace ch {

class ListBoxCtrl {
public:
    ListBoxCtrl() = default;
    ListBoxCtrl(int count, int disp) { init(count, disp); }

    void init(int count, int disp) {   // FUN_00428220
        count_ = count;
        row_ = top_ = 0;
        disp_ = std::max(disp, 1);
    }
    void setRow(int row) {             // FUN_004282b0
        if (count_ != 0 && row < disp_ && row < count_) row_ = row;
    }
    [[nodiscard]] int index() const { return row_ + top_; }   // FUN_00428310
    [[nodiscard]] int disp() const { return disp_; }          // FUN_00428350
    [[nodiscard]] int count() const { return count_; }        // FUN_00428380
    [[nodiscard]] int top() const { return top_; }            // FUN_004283b0

    void select(int index) {           // FUN_004283e0
        if (count_ == 0) index = 0;
        index = std::clamp(index, 0, std::max(count_ - 1, 0));
        first();
        while (this->index() != index) down();
    }
    void first() { row_ = top_ = 0; }  // FUN_00428720
    void last() {                      // FUN_00428760
        row_ = std::max(disp_ - 1, 0);
        top_ = std::max(count_ - disp_, 0);
        if (row_ >= count_) row_ = count_ - 1;
        if (row_ < 0) row_ = 0;
    }
    void up() {                        // FUN_00428520
        if (--row_ < 0) {
            row_ = 0;
            if (--top_ < 0) top_ = 0;
        }
    }
    void down() {                      // FUN_004285f0
        const int most = std::max(count_ - disp_, 0);
        if (++row_ < disp_) {
            if (row_ >= count_) row_ = count_ - 1;
        } else {
            row_ = disp_ - 1;
            if (++top_ > most) top_ = most;
        }
        row_ = std::max(row_, 0);
        top_ = std::max(top_, 0);
    }

private:
    int count_ = 0, row_ = 0, top_ = 0, disp_ = 1;
};

} // namespace ch
