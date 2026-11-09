// ============================================================================
// kojin_task.cpp
// ============================================================================
#include "kojin_task.hpp"
#include "kojin_blend.hpp"
#include "../state/game_logic.hpp"
#include "../state/exe_image.hpp"
#include "../option/options.hpp"

#include <algorithm>
#include <cstdio>

namespace ch {

using ysub::drawRect;
using ysub::fill;

namespace {
constexpr std::uint32_t kSe = 5;
constexpr std::uint32_t kKojinState = 0x7a4e60;   // s16: 0 idle, 1 shown, 2 fading out, >= 3 scene over
constexpr std::uint32_t kLeftPane   = 0x7d6a31;   // u8: 0 status card, else FUN_004b4000
constexpr std::uint32_t kTab        = 0x7d6a32;   // u8: 0..4, 6 = close
constexpr std::uint32_t kPrevTab    = 0x7d6a33;
constexpr std::uint32_t kShown      = 0x7d6a34;   // s16: index into the unit list
constexpr std::uint32_t kPicked     = 0x7d57c2;   // s16: HanTask's last picked unit
constexpr std::uint32_t kSkills = 0x6c2dc8, kItems = 0x6d5118, kJobs = 0x6b2d8a;
constexpr std::uint32_t kPoints     = 0x7a511c;   // s32: school points
// 技能 tab state.
constexpr std::uint32_t kSkillDrag  = 0x7d6a36;   // s16: 0 enter, 1 idle, 2 dragging from the grid, 3 from the bar
constexpr std::uint32_t kSkillCol   = 0x7d6a38;   // s16 grid cell of the selected skill
constexpr std::uint32_t kSkillRow   = 0x7d6a3a;
constexpr std::uint32_t kSkillId    = 0x7d6a3c;   // s16
constexpr std::uint32_t kBuyState   = 0x7d6a40;   // u8: 0 unavailable, 1 can learn, 2 learned
constexpr std::uint32_t kBarSlot    = 0x7d6a44;   // s16: bar slot a skill was taken from
constexpr std::uint32_t kBarCat     = 0x7d6a46;   // s16 icon cell of that skill
constexpr std::uint32_t kBarSub     = 0x7d6a48;
constexpr std::uint32_t kPrice      = 0x7d6a4c;   // s32
// 装備 tab state.
constexpr std::uint32_t kItemDrag   = 0x7d6a50;   // s16: 0 enter, 1 idle, 2 from storage, 3 from the bar
constexpr std::uint32_t kItemCol    = 0x7d6a52;   // s16 storage cell of the selected item
constexpr std::uint32_t kItemRow    = 0x7d6a54;
constexpr std::uint32_t kItemId     = 0x7d6a56;   // s16, -1 none
constexpr std::uint32_t kItemPage   = 0x7d6a5a;   // s16: 0..9, 12 x 7 items a page
constexpr std::uint32_t kItemPrev   = 0x7d6a5c;   // u8: storage state before the drag
constexpr std::uint32_t kItemBarK   = 0x7d6a5e;   // s16: item slot it came from
constexpr std::uint32_t kItemBarPos = 0x7d6a60;   // s16: its bar position
constexpr std::uint32_t kItemIndex  = 0x7d7c26;   // u16: item id - 1
constexpr std::uint32_t kStore      = 0x7aacaa;   // u16 per item id: bit 0 owned, 1-7 owner, 8-11 state
constexpr int kTextPx = 12;                        // +0xc8c is never set (GDI default); 12 px fits the panels

// Skill detail labels: target (skill +0x18) and range (+0x19), the strings
// FUN_0045c650 pushes (JP exe / ch.exe addresses). Some of ch.exe's end in
// a carriage return, which is dropped.
constexpr std::array<std::uint32_t, 7> kTargetJp{0x50bdcc, 0x50bdc4, 0x50bdbc, 0x50bdb4, 0x50bdac, 0x50bda4, 0x50bd98};
constexpr std::array<std::uint32_t, 7> kTargetTw{0x5a0b20, 0x5a0b18, 0x5a0b0c, 0x5a0b04, 0x5a0afc, 0x5a0af4, 0x5a0ae8};
constexpr std::array<std::uint32_t, 6> kRangeJp{0x50bd80, 0x50bdc4, 0x50bdbc, 0x50bdb4, 0x50bd74, 0x50bd68};
constexpr std::array<std::uint32_t, 6> kRangeTw{0x5a0ad0, 0x5a0b18, 0x5a0b0c, 0x5a0b04, 0x5a0ac4, 0x5a0ab8};

std::string label(GameVersion v, std::uint32_t va) {
    std::string s = exe::text(v, va);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

// Item names by id (0 is empty): ch.exe PTR_DAT_006e3150 / JP 0x5fbaa8.
const std::vector<std::string>& itemNames(GameVersion v) {
    return exe::strings(v, v == GameVersion::Tw ? 0x6e3150 : 0x5fbaa8, 0x169);
}

// Kojin rect-table rows (DAT_0061bae0 + 16 i).
enum : int {
    kPaneFrame = 0, kCardFrame = 1, kTabFrame = 8, kTabLit = 9,
    kSkillIconSet = 0xe, kSkillIcons = 0xf, kSkillIconsDim = 0x10, kSkillNames = 0x12,
    kItemIcons = 0x1d, kItemIconsAlt = 0x1e, kCursor = 0x24,
    kDigits = 0x3f, kJobIcons = 0x41, kJobNames = 0x42, kArrows = 0x44, kTooltips = 0x45,
    kItemFrame = 0x46, kItemStatIcons = 0x4b, kBestEffect = 0x4c, kInfinite = 0x5b, kTabFlash = 0x5c,
};

// Equipment slot y positions by slot count (FUN_004b2100's local table): {frame y, height}.
constexpr std::array<std::pair<int, int>, 9> kItemColumn{{
    {0, 0}, {0x152, 0x41}, {300, 0x67}, {0x106, 0x8d}, {0xe0, 0xb3},
    {0xba, 0xd9}, {0x94, 0xff}, {0x6e, 0x125}, {0x2b, 0x168},
}};

// FUN_004b28b0: digit colour by comparison with the baseline (source y offset).
int compare(int baseline, int current) {
    if (baseline == current) return 0;
    return baseline < current ? 0x98 : 0x130;
}
} // namespace

KojinTask::KojinTask(GameContext& ctx) : ctx_(ctx) {}

KojinTask::~KojinTask() {   // FUN_004b8be0: FUN_0041f4e0(0xd), FUN_004d1bf0 frees the font
    for (AnmPlayer::NodeId n : fxNode_) if (fxFile_.loaded()) fx_.freeRoot(n);
    ctx_.sprites->unloadBank(kBank);
    ctx_.sprites->unloadBank(0x11);
}

ysub::Rect KojinTask::rect(int i) const { return ysub::rectAt(0x61bae0 + std::uint32_t(i) * 0x10, ctx_.version); }

int KojinTask::shownUnit() const {
    const int i = std::clamp<int>(ctx_.work.s16(kShown), 0, int(units_.size()) - 1);
    return units_[std::size_t(i)];
}

// ------------------------------------------------------------------ start ---
// FUN_004b8d50
Co<> KojinTask::start(std::uint32_t) {
    GameWork& w = ctx_.work;
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\menu\\KojinData.bin"));
    w.set8(kTab, 0);
    fade_ = 0;
    do {
        switch (w.s16(kKojinState)) {
            case 0:
                needOpen_ = true;
                break;
            case 1:
                if (needOpen_) { needOpen_ = false; open(); reset(); }
                w.set16(0x7e117a, 0);
                frame();
                fade_ = 0;
                break;
            case 2:
                w.set16(0x7e117a, 0);
                frame();
                fill(ctx_, 0, 0, 0x400, 0x300, 0, std::uint32_t(fade_ * 0xff / 0x1e) << 24 | 0x10101u);
                if (++fade_ == 0x1e) { w.set8(kTab, 0); w.set16(kKojinState, 0); }
                break;
            default: break;
        }
        co_await yieldFrame();
    } while (w.s16(kKojinState) < 3);
}

// ------------------------------------------------------------------- open ---
// FUN_004b5600: the units in groups (with their group), then the waiting
// members; show HanTask's last picked unit.
void KojinTask::open() {
    GameWork& w = ctx_.work;
    w.set16(kShown, 0);
    // TextLists: 6, 10, 1 x 5, 4, 4 lines; files KSCapabilityUP / ItemExp /
    // SkillExp / JobExp (the other tabs' lists come with them).
    skillText_.setFont(kTextPx, 10);
    skillText_.load(ctx_, "data\\YsText\\SkillExp.ybc");
    for (auto& l : skillLine_) l.setFont(kTextPx, 1);
    itemText_.setFont(kTextPx, 4);
    itemText_.load(ctx_, "data\\YsText\\ItemExp.ybc");
    itemRarity_.setFont(kTextPx, 1);
    jobText_.setFont(kTextPx, 4);
    jobText_.load(ctx_, "data\\YsText\\JobExp.ybc");
    capText_.setFont(kTextPx, 6);
    capText_.load(ctx_, "data\\YsText\\KSCapabilityUP.ybc");
    if (!fxFile_.loaded()) {
        fxFile_.load(ctx_.readData("data\\AllResult\\AnmAllResult.bin"), 0x11, *ctx_.sprites);   // FUN_004b55a0
        for (auto& n : fxNode_) n = fx_.newRoot(fxFile_);
    }
    units_.fill(-1);
    unitGroup_.fill(-1);
    unitCount_ = 0;
    for (int g = 0; g < 5; ++g) {
        const std::uint32_t grp = groups::kBase + std::uint32_t(g) * groups::kStride;
        if (w.s16(grp) == -1) continue;
        for (std::uint32_t k = 0; k < 4; ++k) {
            const int id = w.s16(grp + 0x10 + k * 2);
            if (id == -1 || unitCount_ >= int(units_.size())) continue;
            units_[std::size_t(unitCount_)] = static_cast<std::int16_t>(id & 0xff);
            unitGroup_[std::size_t(unitCount_)] = static_cast<std::int8_t>(g);
            ++unitCount_;
        }
    }
    for (int i = 0; i < w.s16(0x7d57de) && unitCount_ < int(units_.size()); ++i) {
        const int id = w.s16At(0x7d57e2, i);
        if (id != -1) units_[std::size_t(unitCount_++)] = static_cast<std::int16_t>(id);
    }
    for (int i = 0; i < unitCount_; ++i)
        if (units_[std::size_t(i)] == w.s16(kPicked)) w.set16(kShown, i);
    job_ = w.s16(GameWork::unitRecord(shownUnit()) + 6);
    computeUnit(shownUnit(), -1, true, 2);
    if (!blend_) blend_ = std::make_unique<KojinBlend>(*this, ctx_);
    blend_->open();
}

// FUN_004b5a00
void KojinTask::reset() {
    GameWork& w = ctx_.work;
    w.set8(kLeftPane, 0);
    w.set16(0x7d6a4a, -1); w.set16(0x7d6a58, -1);
    w.set16(0x7d6a50, 0); w.set16(0x7d6a52, -1); w.set16(0x7d6a56, -1);
    w.set16(0x7d6a36, 0); w.set16(0x7d6a38, -1); w.set16(0x7d6a3a, -1); w.set16(0x7d6a3c, -1);
    w.set16(0x7d6a3e, 0);
    w.set16(0x7d6f66, 0);
    jobPicked_ = false;
    computeUnit(shownUnit(), -1, true, 2);      // baseline: without equipment
    computeUnit(shownUnit(), -1, false, 0);     // shown: as equipped
}

// FUN_004b66f0: copy the unit's record (with the previewed job), derive
// its values, optionally keep them as the baseline, and refresh which
// items and skills it may use. Mode 1 unequips everything for real,
// mode 2 only in the copy.
void KojinTask::computeUnit(int unit, int previewJob, bool baseline, int mode) {
    computeRecord(GameWork::unitRecord(unit), previewJob, baseline, mode);
}

void KojinTask::computeRecord(std::uint32_t rec, int previewJob, bool baseline, int mode) {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    jobCount_.fill(0);
    for (int i = 0; i < unitCount_; ++i) {
        const int j = w.s16(GameWork::unitRecord(units_[std::size_t(i)]) + 6);
        if (j >= 0 && j < int(jobCount_.size())) ++jobCount_[std::size_t(j)];
    }
    currentJob_ = w.s16(rec + 6);
    if (previewJob < 1) {
        jobShown_ = job_ = w.s16(rec + 6);
        itemSlots_ = logic::sdU8(kJobs + 2 + std::uint32_t(job_) * 0x40, v);
        skillSlots_ = logic::sdU8(kJobs + 1 + std::uint32_t(job_) * 0x40, v);
    } else {
        jobShown_ = static_cast<std::int16_t>(previewJob);
    }
    for (std::size_t i = 0; i < copy_.b.size(); ++i) copy_.b[i] = w.u8(rec + std::uint32_t(i));
    copy_.set16(6, jobShown_);
    if (mode == 1) {
        for (std::uint32_t k = 0; k < 8; ++k) {
            const int s = w.s16(rec + 0x52 + k * 2);
            if (s != 0) { w.set8(rec + 0xb8 + std::uint32_t(s - 1) * 0xc, 3); w.set16(rec + 0x52 + k * 2, 0); }
            const int it = w.s16(rec + 0x62 + k * 2);
            if (it != 0) {
                const std::uint32_t at = 0x7aacaa + std::uint32_t(it) * 2;
                w.set16(at, (w.u16(at) & 0xf0ff) | 0x100);
                w.set16(rec + 0x62 + k * 2, 0);
            }
        }
    } else if (mode == 2) {
        for (std::size_t k = 0; k < 8; ++k) { copy_.set16(0x52 + k * 2, 0); copy_.set16(0x62 + k * 2, 0); }
    }
    stats::derive(copy_, derived_, bonus_, v);
    int sum = 0;
    for (std::size_t i = 0; i < 7; ++i) sum += derived_.u8(4 + i);
    total_ = std::clamp(sum, 0, 999);
    if (baseline) { baseline_ = derived_; baselineTotal_ = total_; }
    for (int it = 1; it <= 0x168; ++it) stats::refreshItemState(w, v, rec, it);
    for (int sk = 1; sk <= 0x54; ++sk) stats::refreshSkillState(w, v, rec, sk);
    // DAT_007d7704 / 7706: the strongest "0x19" effect of the equipped items.
    w.set16(0x7d7704, 0); w.set16(0x7d7706, 0);
    for (std::uint32_t k = 0; k < 8; ++k) {
        const int it = w.s16(rec + 0x62 + k * 2);
        if (it == 0) continue;
        for (std::uint32_t j = 0; j < 3; ++j) {
            if (logic::sdU8(kItems + 0x35 + j + std::uint32_t(it) * 0x38, v) != 0x19) continue;
            const int e = logic::sdU8(kItems + 0x28 + std::uint32_t(it) * 0x38, v);
            const int p = logic::sdU8(0x6f408b + std::uint32_t(e) * 0x10, v);
            if (w.s16(0x7d7706) < p) { w.set16(0x7d7706, p); w.set16(0x7d7704, e); }
        }
    }
}

// ------------------------------------------------------------------ frame ---
// FUN_004b5fc0
void KojinTask::frame() {
    GameWork& w = ctx_.work;
    cursor_ = 0;
    keyTabs_ = false;
    tapPlaced_ = false;
    swipe_ = 0;
    if (ui::modern() && blend_) {                          // New style: the one-page screen
        blend_->frame();
        const auto& m = ctx_.mouse();
        if (!ctx_.engine.input().touch().used) drawRect(ctx_, rect(kCursor), 0x33, m.x, m.y);
        return;
    }
    if (ui::modern() && w.u8(kLeftPane) == 0) {          // New style: a swipe across the card steps the student
        const auto& g = ctx_.engine.input().touch();
        const float dx = g.dragPos.x - g.dragStart.x, dy = g.dragPos.y - g.dragStart.y;
        if (g.dragEnded && g.dragStart.y < 0x1a5 && dx * dx > 0x80 * 0x80 && dx * dx > 4 * dy * dy) swipe_ = dx < 0 ? 1 : -1;
    }
    drawRect(ctx_, rect(kTabFrame), 0x4f, 8, 0x1b4);
    drawRect(ctx_, rect(kPaneFrame), 0x4f, 0, 2);
    drawRect(ctx_, rect(kPaneFrame), 0x4f, 0, 0x1a5);
    if (w.u8(kLeftPane) == 0) drawCard();
    else drawPicker();
    flashes_.draw(ctx_);

    for (int i = 0; i < 7; ++i) {
        if (i == 5) continue;
        if (!(ysub::hitEntry(ctx_, 0x19 + i).pressed & legacy::kMouseLeft)) continue;
        const int prev = w.u8(kTab);
        w.set8(kPrevTab, prev);
        w.set8(kTab, i);
        if (prev == i) continue;
        reset();
        ctx_.audio->sePlay(kSe);
        flashes_.add(rect(kTabFlash + i), 0x3e, 8, i == 6 ? 0x2ce : i * 0x2f + 0x1b4, 6);
        if (i == 6) { w.set8(kTab, prev); w.set16(kKojinState, 2); }
    }
    switch (w.u8(kTab)) {
        case 0: tabSkills(); break;
        case 1: tabItems(); break;
        case 2: tabJobs(); break;
        case 3: tabStats(); break;
        case 4: tabStudents(); break;
        default: break;
    }
    if (keyTabs_) {                                       // DIK_UP / DIK_DOWN cycle tabs 0..4
        const auto& in = ctx_.engine.input();
        int t = -1;
        if (in.pressed(legacy::Action::Up)) t = w.u8(kTab) == 0 ? 4 : w.u8(kTab) - 1;
        if (in.pressed(legacy::Action::Down)) t = w.u8(kTab) == 4 ? 0 : w.u8(kTab) + 1;
        if (t >= 0) {
            reset();
            w.set8(kTab, t);
            ctx_.audio->sePlay(kSe);
            flashes_.add(rect(kTabFlash + t), 0x3e, 8, t * 0x2f + 0x1b4, 6);
        }
    }
    const int tab = w.u8(kTab);
    if (tab < 3) { drawEquipItems(); drawEquipSkills(); }
    if (tab != 5 && tab != 6) drawRect(ctx_, rect(kTabLit), 0x48, 8, tab * 0x2f + 0x1b4, 0, (tab + 6) * 0x2c);

    const auto& m = ctx_.mouse();
    if (ui::modern() && ctx_.engine.input().touch().used) return;   // no pointer under a finger
    if (cursor_ == 0) drawRect(ctx_, rect(kCursor), 0x33, m.x, m.y);
    else drawRect(ctx_, rect(kCursor + cursor_), 0x33, m.x - 0x12, m.y - 0x12);
}

// ---------------------------------------------------------------- helpers ---
// FUN_004b7230: `digits` digits from rect `rectIndex`, whose x field is the
// screen pitch and y field the source pitch; src shifted by (srcOffX, srcOffY).
void KojinTask::drawDigits(int x, int y, int srcOffX, int srcOffY, int digits, int prio, int rectIndex,
                           std::uint32_t value, bool suppress) {
    const ysub::Rect r = rect(rectIndex);
    bool skipping = suppress;
    for (int i = 0; i < digits; ++i) {
        std::uint32_t div = 1;
        for (int k = 0; k < digits - 1 - i; ++k) div *= 10;
        const int d = int(value / div % 10);
        if (d != 0 || i + 1 == digits) skipping = false;
        if (skipping) continue;
        ysub::drawSub(ctx_, {.bank = r.bank, .frame = r.frame, .sx = srcOffX + r.sx + r.y * d, .sy = srcOffY + r.sy,
                             .w = r.w, .h = r.h}, prio, x + r.x * i, y);
    }
}

// FUN_004b7e20 -> FUN_004b7e90 (number on): a value with its number
// (FUN_004b7120, layout DAT_0061c120 + 10 row: {rect, prio, x, y, digits})
// and gauge (DAT_0061c288 + 8 gauge: {rect, -, x, y}, scaled to value / max;
// gaugeRow picks the gauge colour).
void KojinTask::drawValueRow(int row, int gauge, int max, int value, int prio, int srcOffY, int gaugeRow, bool number) {
    const GameVersion v = ctx_.version;
    const int val = std::clamp(value, 0, max);
    if (number) {
        const std::uint32_t n = 0x61c120 + std::uint32_t(row) * 10;
        drawDigits(logic::sdS16(n + 4, v), logic::sdS16(n + 6, v), 0, srcOffY, logic::sdU8(n + 8, v),
                   logic::sdS16(n + 2, v), logic::sdS16(n, v), std::uint32_t(val), true);
    }
    if (gauge == -1) return;
    const std::uint32_t g = 0x61c288 + std::uint32_t(gauge) * 8;
    const ysub::Rect r = rect(logic::sdS16(g, v));
    ysub::drawSub(ctx_, {.bank = kBank, .frame = r.frame, .sx = r.sx, .sy = r.sy + gaugeRow * r.h, .w = r.w, .h = r.h,
                         .scaleX = int(std::uint32_t(val * 100) / std::uint32_t(max) % 0x65)},
                  prio, logic::sdS16(g + 4, v), logic::sdS16(g + 6, v));
}

// FUN_004b4d70: skill icon (category column, sub row) in one of its looks.
void KojinTask::drawSkillIcon(int x, int y, int cat, int sub, int style, int prio) {
    int which = kSkillIcons;
    std::uint8_t alpha = 0xff, rgb = 0x80, blend = kBlendAlpha;
    switch (style) {
        case 2: alpha = 0x60; break;
        case 3: case 7: break;
        case 4: which = kSkillIconsDim; alpha = 0x60; break;
        case 5: which = kSkillIconsDim; break;
        case 6: alpha = 0xc0; rgb = 0xb0; blend = kBlendAdd; break;
        case 8: which = kSkillIconSet; cat = sub = 0; break;
        default: return;
    }
    const ysub::Rect r = rect(which);
    ysub::drawSub(ctx_, {.bank = kBank, .frame = r.frame, .sx = r.sx + cat * r.w, .sy = r.sy + sub * r.h,
                         .w = r.w, .h = r.h, .alpha = alpha, .rgb = rgb, .blend = blend}, prio, x, y);
}

// FUN_004b4f40: skill name plate, 21 rows per column, 7 per category.
void KojinTask::drawSkillName(int x, int y, int cat, int sub, int prio) {
    const ysub::Rect r = rect(kSkillNames);
    const int n = cat * 7 + sub;
    ysub::drawSub(ctx_, {.bank = r.bank, .frame = r.frame, .sx = r.sx + r.w * (n / 0x15), .sy = r.sy + r.h * (n % 0x15),
                         .w = r.w, .h = r.h}, prio, x, y);
}

// FUN_004b4b00: item icon, 12 per row; styles 3 / 4 glow when the item is
// held by the shown unit.
void KojinTask::drawItemIcon(int x, int y, int index, int style, int prio) {
    int which = kItemIcons;
    std::uint8_t alpha = 0xff, rgb = 0x80, blend = kBlendAlpha;
    int col = index % 0xc, row = index / 0xc;
    switch (style) {
        case 1: break;
        case 2: which = kItemIconsAlt; break;
        case 3: case 4: {
            which = style == 3 ? kItemIcons : kItemIconsAlt;
            const int owner = ctx_.work.u16At(0x7aacac, index) >> 1 & 0x7f;
            if (owner == shownUnit()) { alpha = 0xc0; rgb = 0xb0; blend = kBlendAdd; }
            else alpha = 0x60;
            break;
        }
        case 5: alpha = 0x60; break;
        case 6: which = kSkillIconSet; col = row = 0; break;
        default: return;
    }
    const ysub::Rect r = rect(which);
    ysub::drawSub(ctx_, {.bank = kBank, .frame = r.frame, .sx = col * r.w + r.sx, .sy = row * r.h + r.sy,
                         .w = r.w, .h = r.h, .alpha = alpha, .rgb = rgb, .blend = blend}, prio, x, y);
}

// FUN_004b8460 / FUN_004b84f0: job emblem; jobs pair up in cells (5 per row).
void KojinTask::drawJobIcon(int x, int y, int job, int prio) {
    const int j = job - 1, cell = j / 2, half = j % 2 + 1;
    const ysub::Rect r = rect(kJobIcons);
    drawRect(ctx_, r, prio, x, y, (cell % 5) * r.w * 2 + (half == 2 ? r.w : 0), (cell / 5) * r.h);
}

// FUN_004b8700 / FUN_004b8790: job name plate.
void KojinTask::drawJobName(int x, int y, int job, int prio) {
    const int j = job - 1, cell = j / 2, half = j % 2 + 1;
    const ysub::Rect r = rect(kJobNames);
    drawRect(ctx_, r, prio, x, y, 0, cell * r.h * 2 + (half == 2 ? r.h : 0));
}

// ------------------------------------------------------------ status card ---
// FUN_004b28f0: the shown unit's portrait and values, each number coloured
// against the unequipped baseline.
void KojinTask::drawCard() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const int unit = shownUnit();
    const stats::Derived& d = derived_;
    const stats::Derived& b = baseline_;
    ysub::drawSub(ctx_, {.bank = rect(kCardFrame).bank, .frame = rect(kCardFrame).frame}, 0x4e, 0x118, 0x1f);
    ysub::drawSub(ctx_, {.bank = kBank, .frame = static_cast<std::uint16_t>(unit + 0xb)}, 0x4e, 0, 0x1f);

    const auto row16 = [&](int row, int gauge, int max, std::size_t off) {
        drawValueRow(row, gauge, max, d.s16(off), 0x3b, compare(b.s16(off), d.s16(off)));
    };
    const auto row8 = [&](int row, int gauge, int max, std::size_t off) {
        drawValueRow(row, gauge, max, d.u8(off), 0x3b, compare(b.u8(off), d.u8(off)));
    };
    row16(0, 0, 999, 0x14);
    row16(1, 1, 999, 0x1a);
    drawValueRow(2, 2, 99, d.s32(0x20) / 0xe10, 0x3b, compare(b.s32(0x20) / 0xe10, d.s32(0x20) / 0xe10));
    row8(4, -1, 0x20, 0x2c);
    row8(7, -1, 0x7f, 0x37);
    row8(8, -1, 99, 0x44);
    drawValueRow(9, -1, 999, d.u16(0x34), 0x3b, compare(b.u16(0x34), d.u16(0x34)));
    row8(10, 6, 0xff, 0x4c);
    row8(11, 7, 0xff, 0x4d);
    row8(12, 8, 0xff, 0x4f);
    row8(13, -1, 99, 0x4e);
    row8(14, -1, 99, 0x50);
    row8(15, -1, 99, 0xe);

    const logic::UnitSummary sum = logic::unitSummary(w, v, unit);
    for (int i = 0; i < 7; ++i) {                    // stat levels with a progress line
        const int y = i * 0x12;
        fill(ctx_, 0x2c7, y + 0x10e, 0x47, 3, 0x3a, 0x10101u, kBlendTest);
        fill(ctx_, 0x2c8, y + 0x10f, 0x45, 1, 0x39, 0x3831u, kBlendTest);
        const std::size_t off = 4 + std::size_t(i);
        const int colour = compare(b.u8(off), d.u8(off));
        drawValueRow(0x10 + i, 9 + i, 0x87, d.u8(off), 0x3b, colour);
        if (colour != 0x98)
            fill(ctx_, 0x2c8, y + 0x10f, sum.stats[std::size_t(i)].percent * 0x45 / 100, 1, 0x38, 0xa68cu, kBlendTest);
    }
    drawValueRow(0x17, 0x10, 999, total_, 0x3b, compare(baselineTotal_, total_));

    // Attack and range rows use the gauge colour of the job's class.
    constexpr std::array<int, 6> kClassGauge{0, 0, 1, 2, 3, 4};
    const int cls = std::clamp<int>(logic::sdU8(kJobs + std::uint32_t(jobShown_) * 0x40, v), 0, 5);
    const int gaugeRow = kClassGauge[std::size_t(cls)];
    const int atk = d.s16(0x3a) + (d.u16(0x3c) >> 1), atkBase = b.s16(0x3a) + (b.u16(0x3c) >> 1);
    drawValueRow(6, 5, 0xff, atk, 0x3b, compare(atkBase, atk), gaugeRow);
    if (d.s16(0x38) == 999) {
        drawValueRow(5, 4, 999, d.s16(0x38), 0x3b, compare(b.s16(0x38), d.s16(0x38)), gaugeRow, false);
        drawRect(ctx_, rect(kInfinite), 0x3b, 0x1f2, 0xd0);
    } else {
        drawValueRow(5, 4, 999, d.s16(0x38), 0x3b, compare(b.s16(0x38), d.s16(0x38)), gaugeRow);
    }

    // Speed grades: index into a threshold table, arrow coloured by change.
    const auto grade = [](const std::array<int, 8>& t, int value, bool descending) {
        int i = 0;
        while (i < 7 && (descending ? value < t[std::size_t(i)] : t[std::size_t(i)] < value)) ++i;
        return i;
    };
    const auto arrow = [&](int y, int base, int cur) {
        const int colour = base == cur ? 0 : base < cur ? 2 : 1;
        drawRect(ctx_, rect(kArrows), 0x3b, 0x24b, y, std::clamp(cur, 0, 6) * 0x16, colour * 0x98);   // FUN_004b8830
    };
    constexpr std::array<int, 8> kWait{0x1e, 0x3c, 0x78, 0xb4, 0xf0, 300, 600, 999};
    constexpr std::array<int, 8> kMove{999, 0x96, 0x7d, 100, 0x50, 0x3c, 0x28, 0x14};
    arrow(0x48, grade(kWait, b.s16(0x18), false), grade(kWait, d.s16(0x18), false));
    arrow(0x5d, grade(kWait, b.s16(0x1e), false), grade(kWait, d.s16(0x1e), false));
    arrow(0x94, grade(kMove, b.u16(0x2e), true), grade(kMove, d.u16(0x2e), true));
    drawJobIcon(0x2c7, 0x80, jobShown_, 0x43);
    drawJobName(0x28b, 0x49, jobShown_, 0x42);
    drawCardSkills();
    drawCardItems();

    // Hover help for the value labels (hit-table rows 1..9).
    struct Tip { int hit, x, y, label; };
    constexpr std::array<Tip, 9> kTips{{{1, 0x21a, 0x30, 10}, {2, 0x21a, 0x30, 0xb}, {3, 0x21a, 0x30, 0xc},
                                        {4, 0x21a, 0x30, 0xd}, {5, 0x21a, 0xba, 0xe}, {6, 0x21a, 0xba, 0xf},
                                        {7, 0x21a, 0xba, 0x10}, {8, 0x21a, 0x12e, 0x11}, {9, 0x21a, 0x12e, 0x12}}};
    for (const Tip& t : kTips)
        if (ysub::hitEntry(ctx_, t.hit).over) drawRect(ctx_, rect(kTooltips), 0, t.x, t.y, 0, t.label * 0xe);   // FUN_004b8930
    if (ysub::clicked(ctx_, 0x21, 0x38, 0xe8, 0x147)) {   // the portrait speaks
        const int line = logic::unitVoiceLine(unit, 0, 0);
        if (line >= 0) ctx_.audio->voicePlay(logic::unitVoicePath(line, v));
    }
    drawEffects();
}

// AnmAllResult effects: 0 at (0x2db, 0xb0), 1 rank up at (0x2f4, 0x38),
// 2 on the learned skill's cell, the rest at the pointer.
void KojinTask::drawEffects() {
    if (!fxFile_.loaded()) return;
    static const AnmPlayer::SpriteTemplate kSprite{.color = {0x80, 0x80, 0x80, 0xFF}};   // +0x3a564
    const GameWork& w = ctx_.work;
    const auto& m = ctx_.mouse();
    for (std::size_t i = 0; i < fxNode_.size(); ++i) {
        int x = m.x, y = m.y;
        if (i == 0) { x = 0x2db; y = 0xb0; }
        else if (i == 1) { x = 0x2f4; y = 0x38; }
        else if (i == 2) { x = w.s16(kSkillCol) * 0x24 + 0x103; y = w.s16(kSkillRow) * 0x24 + 0x1fc; }
        fx_.update(fxNode_[i]);
        fx_.draw(fxNode_[i], ctx_.draw, *ctx_.sprites, kSprite, x, y, 0);
    }
}

// FUN_004b1f30: equipped skills with their name and level.
void KojinTask::drawCardSkills() {
    const GameVersion v = ctx_.version;
    const std::uint32_t rec = GameWork::unitRecord(shownUnit());
    for (int k = 0; k < skillSlots_; ++k) {
        const int s = ctx_.work.u16(rec + 0x52 + std::uint32_t(k) * 2);
        if (s == 0) continue;
        const std::uint32_t S = kSkills + std::uint32_t(s) * 0x48;
        const int cat = logic::sdU8(S + 4, v) - 1, sub = logic::sdU8(S + 7, v) - 1;
        const int y = k * 0x26;
        drawSkillIcon(0x341, y + 0x46, cat, sub, 3, 0x40);
        drawSkillName(0x365, y + 0x49, cat, sub, 0x40);
        drawDigits(0x3ad, y + 0x59, 0, 0, 3, 0x4a, kDigits, std::uint32_t(logic::sdS16(S + 8, v)), true);
    }
}

// FUN_004b2100: equipped items under the skills: name and up to three of
// their effect values (an icon per effect kind).
void KojinTask::drawCardItems() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    if (itemSlots_ != 0 && itemSlots_ < kItemColumn.size()) {
        const ysub::Rect r = rect(kItemFrame);
        ysub::drawSub(ctx_, {.bank = r.bank, .frame = r.frame, .sx = r.sx, .sy = r.sy, .w = r.w,
                             .h = kItemColumn[itemSlots_].second}, 0x4d, 0x33d, kItemColumn[itemSlots_].first);
    }
    const std::uint32_t rec = GameWork::unitRecord(shownUnit());
    const int y0 = kItemColumn[std::min<std::size_t>(skillSlots_, kItemColumn.size() - 1)].second;
    // Effect kind (item +0x35..+0x37) - 8 -> the item field shown.
    struct Field { std::uint8_t off; bool word; };
    constexpr std::array<Field, 22> kEffect{{{0xc, true}, {0xe, true}, {0x10, true}, {0x12, true}, {0x14, true},
                                             {0x16, true}, {0x18, false}, {0x1a, true}, {0x1c, false}, {0x1d, false},
                                             {0x1e, false}, {0x1f, false}, {0x20, false}, {0x21, false}, {0x22, true},
                                             {0x24, true}, {0x26, true}, {0x28, false}, {0x2c, true}, {0x30, true},
                                             {0x32, true}, {0x34, false}}};
    const auto& names = itemNames(ctx_.version);
    for (int k = 0; k < itemSlots_; ++k) {
        const int it = w.s16(rec + 0x62 + std::uint32_t(k) * 2);
        if (it == 0) continue;
        const int y = y0 + k * 0x26;
        drawItemIcon(0x341, y + 0x47, it - 1, 1, 0x3f);
        if (std::size_t(it) < names.size())
            ctx_.draw.text({.x = 0x369 + 0x78 / 2, .y = y + 0x49 + 6, .priority = 0x3d, .argb = 0xFFFFFFFFu,
                            .text = std::string(names[std::size_t(it)]), .fontPx = 12, .centred = true});
        const std::uint32_t I = kItems + std::uint32_t(it) * 0x38;
        int shown = 0;
        for (std::uint32_t j = 0; j < 3; ++j) {
            const int kind = logic::sdU8(I + 0x35 + j, v);
            int value = -1;
            if (kind >= 8 && kind - 8 < int(kEffect.size())) {
                const Field f = kEffect[std::size_t(kind - 8)];
                value = f.word ? logic::sdS16(I + f.off, v) : logic::sdU8(I + f.off, v);
            }
            if (value < 0) continue;
            drawRect(ctx_, rect(kItemStatIcons), 0x4a, shown * 0x2b + 0x367, y + 0x5c, 0, (kind - 8) * 0x10);
            drawDigits(shown * 0x2b + 0x377, y + 0x5d, 0, 0, 2, 0x4a, kDigits, std::uint32_t(value), true);
            ++shown;
        }
    }
    if (w.s16(0x7d7704) != 0) drawRect(ctx_, rect(kBestEffect), 0x4a, 0x1d1, 0x108, 0, w.s16(0x7d7704) * 0x18);
}

// FUN_004b5050 / FUN_004b52f0: the equipment bar along the bottom (skills,
// then items). Picking them up belongs to the skill and item tabs.
void KojinTask::drawEquipItems() {
    GameWork& w = ctx_.work;
    const std::uint32_t rec = GameWork::unitRecord(shownUnit());
    equippedItems_ = w.s16(kItemDrag) == 3 ? 1 : 0;
    for (int k = 0; k < itemSlots_; ++k) {
        const std::uint32_t slot = rec + 0x62 + std::uint32_t(k) * 2;
        const int it = w.s16(slot);
        if (it == 0) continue;
        ++equippedItems_;
        const int pos = skillSlots_ + k, x = pos * 0x24 + 0x16e;
        drawItemIcon(x, 0x1be, it - 1, 1, 0x3f);
        // In the 装備 tab an equipped item can be picked up.
        if (w.s16(kItemDrag) != 1 || w.u8(kTab) != 1 || w.u8(kLeftPane) != 0 || tapPlaced_) continue;
        const ysub::Hit h = ysub::hit(ctx_, x + 1, 0x1bf, 0x23, 0x23);
        if (!h.over) continue;
        drawRect(ctx_, rect(0x13), 0x35, x, 0x1be);
        cursor_ = 1;
        if (h.pressed & legacy::kMouseLeft) {
            ctx_.audio->sePlay(kSe);
            w.set16(kItemBarK, k); w.set16(kItemBarPos, pos);
            retap_ = w.s16(kItemId) == it;
            w.set16(kItemDrag, 3);
            w.set16(kItemId, it);
            w.set16(slot, 0);
        }
    }
}

void KojinTask::drawEquipSkills() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const std::uint32_t rec = GameWork::unitRecord(shownUnit());
    equippedSkills_ = w.s16(kSkillDrag) == 3 ? 1 : 0;
    for (int k = 0; k < skillSlots_; ++k) {
        const std::uint32_t slot = rec + 0x52 + std::uint32_t(k) * 2;
        const int s = w.u16(slot);
        if (s == 0) continue;
        ++equippedSkills_;
        const std::uint32_t S = kSkills + std::uint32_t(s) * 0x48;
        const int cat = logic::sdU8(S + 4, v) - 1, sub = logic::sdU8(S + 7, v) - 1;
        const int x = k * 0x24 + 0x16e;
        drawSkillIcon(x, 0x1be, cat, sub, 7, 0x40);
        // In the 技能 tab an equipped skill can be picked up (FUN_004b10b0).
        if (w.s16(kSkillDrag) != 1 || w.u8(kTab) != 0 || w.u8(kLeftPane) != 0 || tapPlaced_) continue;
        const ysub::Hit h = ysub::hit(ctx_, x + 1, 0x1bf, 0x23, 0x23);
        if (!h.over) continue;
        drawRect(ctx_, rect(0x13), 0x35, x, 0x1be);
        cursor_ = 1;
        if (h.pressed & legacy::kMouseLeft) {
            ctx_.audio->sePlay(kSe);
            w.set16(kBarCat, cat); w.set16(kBarSub, sub); w.set16(kBarSlot, k);
            retap_ = w.s16(kSkillId) == s;
            w.set16(kSkillDrag, 3);
            w.set16(kSkillId, s);
            w.set16(slot, 0);
        }
    }
}

// FUN_004b4000: every listed unit (3 x 10), lit when it has learned the
// selected skill (技能) or holds the selected item (装備); click to show it.
void KojinTask::drawPicker() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    constexpr int x0 = 0x79, y0 = 0x53;
    ysub::drawSub(ctx_, {.bank = rect(7).bank, .frame = rect(7).frame}, 0x4e, 0x74, 0x52);
    int hoverCol = 0, hoverRow = 0;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 10; ++col) {
            const int i = row * 10 + col;
            const int id = units_[std::size_t(i)];
            if (id == -1) continue;
            const int x = x0 + col * 0x4e, y = y0 + row * 0x61;
            const ysub::Hit h = ysub::hit(ctx_, x, y, 0x44, 0x54);
            if (h.over) {
                hoverCol = col; hoverRow = row;
                if (h.pressed & legacy::kMouseLeft) {
                    w.set8(kLeftPane, 0);
                    w.set16(kShown, i);
                    ctx_.audio->sePlay(kSe);
                    computeUnit(shownUnit(), -1, true, 2);
                    computeUnit(shownUnit(), -1, false, 0);
                }
            }
            std::uint8_t alpha = 0x80;
            if (w.u8(kTab) == 0) {
                const int skill = w.s16(kSkillId);
                if (skill >= 0) {
                    const int st = w.u8(GameWork::unitRecord(id) + 0xb8 + std::uint32_t(skill - 1) * 0xc);
                    if (st == 3 || (st > 4 && st < 7)) alpha = 0xff;
                }
            } else if (w.u8(kTab) == 1 && w.s16(0x7d6a52) >= 0 && w.s16(0x7d6a54) >= 0) {
                const int item = w.u16(0x7d7c26);
                const int st = w.u16At(0x7aacac, item);
                const int state = st >> 8 & 0xf;
                if ((st & 1) && state > 2 && state < 5 && id == (st >> 1 & 0x7f)) alpha = 0xff;
            }
            drawUnitCell(x, y, i, alpha);
        }
    }
    drawRect(ctx_, rect(0x15), 6, x0 + hoverCol * 0x4e, y0 + hoverRow * 0x61);
}

// One unit's cell: face (rect 10, 14 per column), name (rect 0xc, FUN_004b82f0
// with row 0), class badge (rect 0x5a, FUN_004b7fc0) and its group's badge
// (rect 0x47, FUN_004b8080).
void KojinTask::drawUnitCell(int x, int y, int i, std::uint8_t alpha) {
    const GameVersion v = ctx_.version;
    const int id = units_[std::size_t(i)];
    const ysub::Rect face = rect(10), name = rect(0xc);
    const int f = id - 1;
    drawRect(ctx_, face, 0x46, x, y, face.w * (f / 0xe), face.h * (f % 0xe), alpha);
    drawRect(ctx_, name, 0x46, x + 4, y + 0x4b, 0, name.h * f, alpha);
    const int cls = logic::sdU8(kJobs + std::uint32_t(ctx_.work.s16(GameWork::unitRecord(id) + 6)) * 0x40, v) - 1;
    drawRect(ctx_, rect(0x5a), 0x44, x, y, (cls & 0xff) * 0x14, 0, alpha);
    if (unitGroup_[std::size_t(i)] != -1)
        drawRect(ctx_, rect(0x47), 0x44, x + 0x3e, y, 0, unitGroup_[std::size_t(i)] * 0xe, alpha);
}

// FUN_004b3bb0: 生徒一覧, every listed unit; click or left / right to show one.
void KojinTask::tabStudents() {
    GameWork& w = ctx_.work;
    ysub::drawSub(ctx_, {.bank = rect(6).bank, .frame = rect(6).frame}, 0x4e, 0xd7, 0x1a7);
    keyTabs_ = true;
    int pick = -1;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 10; ++col) {
            const int i = row * 10 + col;
            if (units_[std::size_t(i)] == -1) continue;
            if (ysub::clicked(ctx_, col * 0x4e + 0xea, row * 0x61 + 0x1d4, 0x44, 0x54)) {
                pick = i;
                ctx_.audio->sePlay(kSe);
            }
            drawUnitCell(col * 0x4e + 0xe5, row * 0x61 + 0x1cd, i, 0xff);
        }
    }
    const auto& in = ctx_.engine.input();
    if (stepPrev() || stepNext()) {
        ctx_.audio->sePlay(kSe);
        stepUnit(stepNext());
    } else if (pick >= 0) {
        w.set16(kShown, pick);
        computeUnit(shownUnit(), -1, true, 2);
        computeUnit(shownUnit(), -1, false, 0);
    }
    const int sel = w.s16(kShown);
    w.set16(0x7d7708, sel % 10);
    w.set16(0x7d770a, sel / 10);
    drawRect(ctx_, rect(0x15), 6, (sel % 10) * 0x4e + 0xe4, (sel / 10) * 0x61 + 0x1cb);
}

// ------------------------------------------------------------- 職業変更 ---
// FUN_004b12a0: the 15 jobs (paired by the unit's +0x72), locked until
// opened in its progress (+0xf8 + job). Picking one previews it without
// equipment; 変更 makes it the unit's job and unequips everything.
void KojinTask::tabJobs() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const int unit = shownUnit();
    const std::uint32_t rec = GameWork::unitRecord(unit);
    keyTabs_ = true;
    constexpr std::array<int, 15> kBase{0xb, 0xd, 0xf, 0x11, 0x13, 1, 3, 5, 7, 9, 0x15, 0x17, 0x19, 0x1b, 0x1d};
    const int pair = w.u8(rec + 0x72);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 5; ++col) {
            const int x = col * 0x5a + 0xe2, y = row * 0x5a + 0x1ef;
            const int job = kBase[std::size_t(row * 5 + col)] - 1 + pair;
            if (w.s8(GameWork::unitProgress(unit) + 0xf8 + std::uint32_t(job)) == 0) {
                drawRect(ctx_, rect(0x27), 0x42, x, y);                       // locked
                continue;
            }
            if (ysub::clicked(ctx_, x, y, 0x50, 0x50)) {
                ctx_.audio->sePlay(kSe);
                jobCol_ = static_cast<std::int16_t>(col); jobRow_ = static_cast<std::int16_t>(row);
                job_ = static_cast<std::int16_t>(job);
                computeUnit(unit, job_, false, 2);
            }
            const ysub::Rect plate = rect(0x43);
            ysub::drawSub(ctx_, {.bank = kBank, .frame = plate.frame, .sx = plate.sx + 1, .sy = plate.sy + (job - 1) * 0x18,
                                 .w = plate.w - 1, .h = plate.h}, 0x42, col * 0x5a + 0xdd, row * 0x5a + 0x22b);
            drawJobIcon(col * 0x5a + 0xf4, row * 0x5a + 0x1e8, job, 0x43);
            {   // FUN_004b71b0: how many listed units have this job
                const std::uint32_t n = 0x61c120 + 0x22 * 10;
                drawDigits(col * 0x5a + 0xe1, row * 0x5a + 0x1ef, 0, 0, logic::sdU8(n + 8, v), 0x3b, logic::sdS16(n, v),
                           job < int(jobCount_.size()) ? jobCount_[std::size_t(job)] : 0, false);
            }
            if (currentJob_ == job) {
                if (!jobPicked_) {
                    jobPicked_ = true;
                    jobCol_ = static_cast<std::int16_t>(col); jobRow_ = static_cast<std::int16_t>(row);
                    job_ = static_cast<std::int16_t>(job);
                    computeUnit(unit, job_, true, 2);
                }
                fill(ctx_, x, y, 0x50, 0x50, 0x41, 0x70010101u);              // the current job
            }
        }
    }
    drawRect(ctx_, rect(0x14), 0x3e, jobCol_ * 0x5a + 0xdd, jobRow_ * 0x5a + 0x1ea);
    drawJobName(0x2aa, 0x1c2, jobShown_, 0x42);

    // Rule markers of the shown job (DAT_00738cef: 16 conditions, 1..3).
    constexpr std::array<std::pair<int, int>, 16> kRule{{
        {0x31a, 0x1e6}, {0x31a, 0x1f9}, {0x31a, 0x235}, {0x31a, 0x248}, {0x31a, 0x271}, {0x31a, 0x284},
        {0x31a, 0x297}, {0x3c6, 0x1e6}, {0x3c6, 0x1f9}, {0x3c6, 0x20c}, {0x3c6, 0x21f}, {0x3c6, 0x235},
        {0x3c6, 0x248}, {0x3c6, 0x25b}, {0x3c6, 0x284}, {0x3c6, 0x297}}};
    for (std::size_t i = 0; i < kRule.size(); ++i) {
        const int mark = logic::sdU8(0x738cef + std::uint32_t(i) + std::uint32_t(jobShown_) * 0x32, v);
        if (mark < 1 || mark > 3) continue;
        drawRect(ctx_, rect(0x47 + mark), 0x49, kRule[i].first, kRule[i].second);
    }

    // 変更
    if (job_ == currentJob_) {
        fill(ctx_, 0x35a, 0x1bf, 0x9e, 0x23, 0x47, 0x70010101u);
    } else {
        const ysub::Hit h = ysub::hit(ctx_, 0x360, 0x1c3, 0x8a, 0x1c);
        if (h.over && (h.held & legacy::kMouseLeft)) {
            ctx_.audio->sePlay(0xb);
            const int line = logic::unitVoiceLine(unit, 5, 5);
            if (line >= 0) ctx_.audio->voicePlay(logic::unitVoicePath(line, v));
            if (fxFile_.loaded()) fx_.play(fxNode_[0], 0, 3, 0);
            w.set16(rec + 6, job_);
            computeUnit(unit, job_, true, 1);                  // unequips everything
            computeUnit(unit, job_, true, 2);
            itemSlots_ = logic::sdU8(kJobs + 2 + std::uint32_t(job_) * 0x40, v);
            skillSlots_ = logic::sdU8(kJobs + 1 + std::uint32_t(job_) * 0x40, v);
            const ysub::Rect r = rect(0x23);
            flashes_.add(r, 0x48, r.x, r.y, 8);
        }
    }

    ysub::drawSub(ctx_, {.bank = rect(4).bank, .frame = rect(4).frame}, 0x4e, 0xd7, 0x1a7);
    const int newSkills = logic::sdU8(kJobs + 1 + std::uint32_t(job_) * 0x40, v);
    const int newItems = logic::sdU8(kJobs + 2 + std::uint32_t(job_) * 0x40, v);
    const auto countRow = [&](int row, int value) {
        const std::uint32_t n = 0x61c120 + std::uint32_t(row) * 10;
        drawDigits(logic::sdS16(n + 4, v), logic::sdS16(n + 6, v), 0, 0, logic::sdU8(n + 8, v),
                   logic::sdS16(n + 2, v), logic::sdS16(n, v), std::uint32_t(value), true);
    };
    countRow(0x20, newSkills);
    countRow(0x21, newItems);
    fill(ctx_, 0x33d, 0x2c, 0xad, 0x168, 0x37, 0x70010101u);
    fill(ctx_, 0x16f, 0x1bf, 0x120, 0x24, 0x37, 0x70010101u);
    if (jobTextShown_ != job_) jobText_.select(job_, 4);
    jobText_.draw(ctx_, 0x2b2, 0x2b6, 0x3d);
    jobTextShown_ = job_;
    for (int k = 0; k < 8; ++k)
        fill(ctx_, k * 0x24 + 0x16f, 0x1bf, 0x22, 0x22, 0x36, k < newSkills ? 0x700000a0u : 0x7000a000u);
    switch (itemScrollButton()) {
        case 1: jobText_.scroll(0); break;
        case 2: jobText_.scroll(1); break;
        default: break;
    }
    const auto& in = ctx_.engine.input();
    if (stepPrev() || stepNext()) {
        ctx_.audio->sePlay(kSe);
        const int cur = w.s16(kShown);
        int next = cur;
        for (int i = 0; i < unitCount_; ++i) {
            next = stepNext() ? (cur + 1 + i) % unitCount_ : (cur - (i + 1) + unitCount_) % unitCount_;
            if (units_[std::size_t(next)] != -1) break;
        }
        w.set16(kShown, next);
        computeUnit(shownUnit(), -1, true, 2);
        jobPicked_ = false;
    }
}

// ------------------------------------------------------------ 能力値UP ---
// Per stat, class (1..5) and +0x72 pair: up to five icons of the values the
// stat feeds (FUN_004abb60's local table, 8 blocks of 0x35).
constexpr std::array<std::array<std::int16_t, 0x35>, 8> kStatIcons{{
    {1, 9, 11, 5, 6, 1, 9, 11, 5, 6, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 9, 11, 6, 15, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 9, 11, 0, 0, 1, 1, 1},
    {8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 5, 6, 8, 14, 1, 5, 6, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 8, 14, 1, 0, 0, 2, 2, 2},
    {13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 5, 6, 15, 13, 18, 5, 6, 15, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 13, 18, 0, 0, 0, 3, 3, 3},
    {19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 19, 2, 12, 6, 15, 19, 2, 12, 6, 15, 19, 2, 12, 0, 0, 19, 2, 12, 0, 0, 4, 4, 4},
    {10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 0, 0, 0, 10, 2, 6, 15, 0, 10, 2, 6, 15, 0, 5, 5, 5},
    {1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 1, 11, 18, 0, 0, 6, 6, 6},
    {2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 2, 12, 19, 0, 0, 7, 7, 7},
}};

// FUN_004abb60: raise stat levels with school points. Work fields:
// 0x7d71fa s16[] levels added, 0x7d7230 s32[] their exp cost, 0x7d7254
// s16[] 2 = cannot afford, 0x7d71ea / 0x7d721a base / new levels,
// 0x7d720a the unit's places before, 0x7d6f6a every listed unit's levels
// (+total), 0x7d6f64 selected stat, 0x7d6f66 phase, 0x7d7250 total cost,
// 0x7d722c levels added, 0x7d72b4 predicted rank, 0x7d7264 record copy.
void KojinTask::tabStats() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    constexpr std::uint32_t kAdd = 0x7d71fa, kCost = 0x7d7230, kState = 0x7d7254, kBase = 0x7d71ea,
                            kNew = 0x7d721a, kPlace = 0x7d720a, kAll = 0x7d6f6a, kSel = 0x7d6f64,
                            kPhase = 0x7d6f66, kTotalCost = 0x7d7250, kTotalAdd = 0x7d722c,
                            kRankAfter = 0x7d72b4, kCopy = 0x7d7264;
    const auto add = [&](int c) { return w.s16At(kAdd, c); };
    const auto cost = [&](int c) { return w.s32At(kCost, c); };
    const auto clearAdds = [&](int n) {
        for (int c = 0; c < n; ++c) {
            w.set16At(kAdd, c, 0);
            w.set32(kCost + std::uint32_t(c) * 4, 0);
            w.set16At(kState, c, 0);
        }
    };
    const auto copyRecord = [&] {
        const std::uint32_t r = GameWork::unitRecord(shownUnit());
        for (std::uint32_t i = 0; i < 0x4a0; ++i) w.set8(kCopy + i, w.u8(r + i));
    };
    const auto levelStart = [&](int level) { return logic::statProgress(w, level, 0).levelStart; };
    const auto levelEnd = [&](int level) { return logic::statProgress(w, level, 0).levelEnd; };
    const auto grouped = [&](int x, int y, int srcOffY, int digits, int rectIndex, int pitch, std::uint32_t value) {
        drawGrouped(x, y, srcOffY, digits, 0x4a, rectIndex, value, pitch);
    };
    const int cap = logic::levelCap(w);
    keyTabs_ = true;
    bool any = false;
    for (int c = 0; c < 7; ++c) any = any || add(c) != 0;

    // 元に戻す: drop all planned levels.
    if (!any) fill(ctx_, 0x392, 0x2d6, 0x5b, 0x1e, 0x37, 0x70010101u);
    else if (ysub::clicked(ctx_, 0x393, 0x2d7, 0x59, 0x1c)) {
        ctx_.audio->sePlay(1);
        clearAdds(10);
        computeUnit(shownUnit(), -1, true, 2);
        computeUnit(shownUnit(), -1, false, 0);
        copyRecord();
        w.set16(kPhase, 1);
    }
    const bool canEdit = w.u8(kRankAfter) != 0x32;        // not at rank 50
    bool changed = false, anyShown = false;

    if (w.s16(kPhase) < 2) {                               // (re)start: places among the listed units
        copyRecord();
        w.set16(kSel, -1);
        clearAdds(8);
        w.set32(kTotalCost, 0);
        int n = 0;
        for (int i = 0; i < 0x1e; ++i) {
            const int id = units_[std::size_t(i)];
            if (id == -1) continue;
            int sum = 0;
            for (std::uint32_t s7 = 0; s7 < 7; ++s7) {
                const int lv = w.u8(GameWork::unitRecord(id) + 0xc + s7 * 8);
                w.set16(kAll + s7 * 2 + std::uint32_t(n) * 0x10, lv);
                sum += lv;
            }
            w.set16(kAll + 7 * 2 + std::uint32_t(n) * 0x10, sum);
            ++n;
        }
        w.set16(0x7d6f68, n);
        const int me = w.s16(kShown);
        for (std::uint32_t s8 = 0; s8 < 8; ++s8) {
            int place = 1;
            for (int j = 0; j < n; ++j)
                if (j != me && w.u16(kAll + s8 * 2 + std::uint32_t(me) * 0x10) < w.u16(kAll + s8 * 2 + std::uint32_t(j) * 0x10)) ++place;
            w.set16(kPlace + s8 * 2, place);
        }
        w.set16(kPhase, 2);
    }
    const int unit = shownUnit();
    const std::uint32_t rec = GameWork::unitRecord(unit);
    if (w.s16(kPhase) == 2) {
        for (int c = 0; c < 7; ++c) {
            const std::uint32_t cu = std::uint32_t(c);
            const int base = w.u8(rec + 0xc + cu * 8);
            w.set16(kBase + cu * 2, base);
            int can = 2, state = 0;                         // 2 can add, 1 at cap, 0 cannot afford
            if (base + add(c) == cap) {
                can = 1;
            } else {
                int need = 0;                               // exp to reach every planned level, plus one more here
                for (int j = 0; j < 7; ++j) {
                    const std::uint32_t ju = std::uint32_t(j);
                    const int lv = w.u8(rec + 0xc + ju * 8) + add(j);
                    const int exp = w.s32(rec + 0x10 + ju * 8);
                    if (j == c) need += levelEnd(lv) - exp;
                    else if (const int d = levelStart(lv) - exp; d > 0) need += d;
                }
                if (w.s32(kPoints) < need || logic::unitSummary(w, v, unit).toCap < need) { can = 0; state = 2; }
            }
            const int y = c * 0x22;
            if (canEdit && (hitTouch(0x1b1, y + 0x1e9, 0x15, 0x12).pressed & legacy::kMouseLeft) && can == 2) {
                flashes_.add(rect(0x53), 0x4c, 0x1a7, y + 0x1e1, 8);
                ctx_.audio->sePlay(kSe);
                changed = true;
                w.set16(kAdd + cu * 2, add(c) + 1);
                w.set32(kCost + cu * 4, levelStart(base + add(c)) - w.s32(rec + 0x10 + cu * 8));
            }
            if ((hitTouch(0x1d5, y + 0x1e9, 0x15, 0x12).pressed & legacy::kMouseLeft) && add(c) != 0) {
                w.set16(kAdd + cu * 2, add(c) - 1);
                flashes_.add(rect(0x54), 0x4c, 0x1cc, y + 0x1e1, 8);
                ctx_.audio->sePlay(kSe);
                w.set32(kCost + cu * 4, add(c) == 0 ? 0 : levelStart(base + add(c)) - w.s32(rec + 0x10 + cu * 8));
                state = 0;
                changed = true;
            }
            if (add(c) == 0) fill(ctx_, 0x1ce, y + 0x1e5, 0x1f, 0x19, 0x37, 0x70010101u);
            w.set16(kState + cu * 2, state);
        }
        if (changed) {                                     // preview on the copy
            for (std::uint32_t k = 0; k < 8; ++k)
                w.set8(kCopy + 0xc + k * 8, w.s16(kBase + k * 2) + w.s16(kAdd + k * 2));
            computeRecord(kCopy, -1, false, 0);
        }
    }

    ysub::drawSub(ctx_, {.bank = rect(5).bank, .frame = rect(5).frame}, 0x4e, 0xd7, 0x1a7);
    const int cls = logic::sdU8(kJobs + std::uint32_t(w.s16(rec + 6)) * 0x40, v);
    const int pair = w.u8(rec + 0x72);
    capShown_ = w.s16(kSel);
    for (int c = 0; c < 7; ++c) {
        if (ysub::clicked(ctx_, 0xe2, c * 0x22 + 0x1e5, 0x1a9, 0x17)) w.set16(kSel, c);
        for (int k = 0; k < 5; ++k) {
            const int idx = (cls - 1) * 10 + (pair - 1) * 5 + k;
            if (idx < 0 || idx >= 0x35) continue;
            const int icon = kStatIcons[std::size_t(c)][std::size_t(idx)];
            if (icon == 0) continue;
            drawRect(ctx_, rect(0x50), 0x4a, k * 0x19 + 0x29a, c * 0x23 + 0x1e6, (icon - 1) * 0x16, 0);
            if (ysub::hit(ctx_, k * 0x18 + 0x29c, c * 0x23 + 0x1e8, 0x12, 0x12).over)
                drawRect(ctx_, rect(kTooltips), 0x4a, 0x2c8, 0x1cc, 0, (icon - 1) * 0xe);   // FUN_004b8930
        }
    }
    if (hitTouch(0x3e1, 0x209, 0x14, 0x2a).pressed & legacy::kMouseLeft) {
        flashes_.add(rect(0x55), 0x4c, 0x3df, 0x207, 8);
        capText_.scroll(0);
        ctx_.audio->sePlay(kSe);
    }
    if (hitTouch(0x3e1, 0x238, 0x14, 0x2a).pressed & legacy::kMouseLeft) {
        flashes_.add(rect(0x56), 0x4c, 0x3df, 0x236, 8);
        capText_.scroll(1);
        ctx_.audio->sePlay(kSe);
    }
    if (const int sel = w.s16(kSel); sel >= 0) {
        if (capShown_ != sel) capText_.select(sel, 6);
        drawRect(ctx_, rect(0x16), 0x36, 0xdf, sel * 0x22 + 0x1e1, 0, 0, 0x90);   // FUN_004d2950
        drawRect(ctx_, rect(0x57), 0x4b, 0x32c, 0x1c7, 0, sel * 0x14);
        drawRect(ctx_, rect(0x58), 0x4b, 0x3b0, 0x1ca, 0, sel * 0x10);
        drawRect(ctx_, rect(0x59), 0x4b, 0x3a4, 0x1eb, 0, sel * 0x10);
        capText_.draw(ctx_, 0x325, 0x20d, 0x3d);
    }

    // Per stat: new level, added levels, place, exp still needed for the next.
    w.set32(kTotalAdd, 0);
    w.set32(kTotalCost, 0);
    int sumNew = 0;
    const int listed = w.s16(0x7d6f68), me = w.s16(kShown);
    const auto swapColour = [](int c) { return c == 0x98 ? 0x130 : c == 0x130 ? 0x98 : c; };
    for (int c = 0; c < 7; ++c) {
        const std::uint32_t cu = std::uint32_t(c);
        const int y = c * 0x22;
        const int lv = w.s16(kBase + cu * 2) + add(c);
        w.set16(kNew + cu * 2, lv);
        sumNew += lv;
        const bool marked = add(c) != 0;
        anyShown = anyShown || marked;
        grouped(0x16c, y + 0x1ea, marked ? 0x98 : 0, 3, 0x3e, 10, std::uint32_t(lv));
        grouped(0x22b, y + 0x1eb, marked ? 0x98 : 0, 3, 0x3f, 8, std::uint32_t(add(c)));
        int place = 1;
        for (int j = 0; j < listed; ++j)
            if (j != me && w.u16(kNew + cu * 2) < w.u16(kAll + cu * 2 + std::uint32_t(j) * 0x10)) ++place;
        grouped(0x19b, y + 0x1eb, swapColour(compare(w.s16(kPlace + cu * 2), place)), 2, 0x3f, 8, std::uint32_t(place));
        if (w.u8(rec + 0xc + cu * 8) + add(c) == cap) {
            fill(ctx_, 0x1a9, y + 0x1e5, 0x1f, 0x19, 0x37, 0x70010101u);
            fill(ctx_, 0x237, y + 0x1e5, 0x53, 0x19, 0x37, 0x70010101u);
        } else {
            const int state = w.s16(kState + cu * 2);
            if (state == 2) fill(ctx_, 0x1a9, y + 0x1e5, 0x1f, 0x19, 0x37, 0x70010101u);
            const int next = levelStart(w.u8(rec + 0xc + cu * 8) + 1 + add(c));
            const int have = cost(c) + w.s32(rec + 0x10 + cu * 8);
            grouped(0x285, y + 0x1eb, state * 0x98, 7, 0x3f, 8, std::uint32_t(next - have));
        }
        w.set32(kTotalAdd, w.s32(kTotalAdd) + add(c));
        w.set32(kTotalCost, w.s32(kTotalCost) + cost(c));
    }
    if (changed) {                                         // the rank the plan would reach
        const int rank = logic::rankForTotal(logic::unitSummary(w, v, unit).total + w.s32(kTotalCost), v);
        w.set8(kRankAfter, rank);
        derived_.set8(0xe, rank);
        w.set8(kCopy + 0xc + 7 * 8, w.s16(kBase + 7 * 2) + w.s16(kAdd + 7 * 2));   // the original's leftover index 7
        computeRecord(kCopy, -1, false, 0);
    }
    int totalPlace = 1;
    for (int j = 0; j < listed; ++j)
        if (j != me && sumNew < w.u16(kAll + 7 * 2 + std::uint32_t(j) * 0x10)) ++totalPlace;
    grouped(0x16c, 0x2dc, anyShown ? 0x98 : 0, 3, 0x3e, 10, std::uint32_t(sumNew));
    grouped(0x19b, 0x2df, swapColour(compare(w.s16(kPlace + 7 * 2), totalPlace)), 2, 0x3f, 8, std::uint32_t(totalPlace));
    grouped(0x22b, 0x2df, w.s32(kTotalAdd) == 0 ? 0 : 0x98, 3, 0x3f, 8, std::uint32_t(w.s32(kTotalAdd)));
    grouped(0x3f4, 0x273, 0, 9, 0x3f, 8, std::uint32_t(w.s32(kPoints)));
    grouped(0x3f4, 0x295, 0, 9, 0x3f, 8, std::uint32_t(w.s32(kTotalCost)));
    grouped(0x3f4, 0x2b7, 0, 9, 0x3f, 8, std::uint32_t(w.s32(kPoints) - w.s32(kTotalCost)));

    // 決定: apply the plan.
    if (!any) {
        fill(ctx_, 0x32b, 0x2d6, 0x5d, 0x1e, 0x37, 0x70010101u);
    } else if (ysub::clicked(ctx_, 0x32c, 0x2d7, 0x5a, 0x1c)) {
        ctx_.audio->sePlay(kSe);
        voice(unit, 7);
        if (baseline_.u8(0xe) < derived_.u8(0xe)) {
            if (fxFile_.loaded()) fx_.play(fxNode_[1], 0, 1, 0);
            ctx_.audio->sePlay(0xb);
        }
        flashes_.add(rect(0x51), 0x4c, 800, 0x2d4, 8);
        for (std::uint32_t c = 0; c < 7; ++c) {
            w.set8(rec + 0xc + c * 8, w.u8(rec + 0xc + c * 8) + w.u8(kAdd + c * 2));
            w.set32(rec + 0x10 + c * 8, std::int64_t(w.s32(rec + 0x10 + c * 8)) + w.s32(kCost + c * 4));
        }
        w.set8(rec + 0x50, logic::unitSummary(w, v, unit).rank);
        w.set16(kSel, -1);
        const int spent = w.s32(kTotalCost);
        clearAdds(8);
        w.set32(kPoints, w.s32(kPoints) - spent);
        const std::uint32_t score = GameWork::unitProgress(unit) + 0x11c;
        w.set32(score, std::int64_t(w.s32(score)) + spent);
        computeRecord(rec, -1, true, 2);
        computeRecord(kCopy, -1, false, 0);
        w.set8(kRankAfter, logic::rankForTotal(logic::unitSummary(w, v, unit).total, v));
        w.set16(kPhase, 1);
    }

    const auto& in = ctx_.engine.input();
    if (stepPrev() || stepNext()) {
        ctx_.audio->sePlay(kSe);
        w.set16(kSel, -1);
        clearAdds(10);
        stepUnit(stepNext());
        copyRecord();
        w.set16(kPhase, 1);
    }
}

// FUN_004b7590: `digits` digits of `value` from rect `rectIndex` (source
// pitch +0xe), each advancing pitch + gap, every third from the right
// gap + groupGap more with a comma (frame 7, 6 x 6 at 0x291, 0x7b).
// Flags: 1 skip leading zeros, 2 group spacing in the width, 4 right-align.
void KojinTask::drawGrouped(int x, int y, int srcOffY, int digits, int prio, int rectIndex, std::uint32_t value,
                            int pitch, int gap, int groupGap, int flags) {
    const ysub::Rect r = rect(rectIndex);
    bool skipping = (flags & 1) != 0;
    int width = (digits != 0 ? (digits - 1) * gap : 0) + digits * pitch;
    if ((flags & 2) && digits / 3 != 0) width += digits / 3 * (groupGap + gap);
    int dx = x - ((flags & 4) ? width : 0);
    const int commaY = y - 8 + r.h;
    for (int i = 0; i < digits; ++i) {
        std::uint32_t div = 1;
        for (int k = 0; k < digits - 1 - i; ++k) div *= 10;
        const bool last = i + 1 == digits;
        const int d = int(value / div % 10);
        if (d != 0 || last) skipping = false;
        if (!skipping)
            ysub::drawSub(ctx_, {.bank = r.bank, .frame = r.frame, .sx = r.sx + r.y * d, .sy = r.sy + srcOffY,
                                 .w = r.w, .h = r.h}, prio, dx, y);
        const int after = dx + gap + pitch;
        dx = after;
        if ((digits - (i + 1)) % 3 == 0) {
            dx = after + gap + groupGap;
            if (!last && !skipping)
                ysub::drawSub(ctx_, {.bank = kBank, .frame = 7, .sx = 0x291, .sy = srcOffY + 0x7b, .w = 6, .h = 6},
                              prio, after, commaY);
        }
    }
}

int KojinTask::gridSkill(int col, int row) const {
    return logic::sdS16(0x61ba30 + std::uint32_t(col + row * 0xc) * 2, ctx_.version);
}

// FUN_004b6fb0: the next / previous listed unit, then show it.
void KojinTask::stepUnit(bool forward) {
    GameWork& w = ctx_.work;
    if (unitCount_ == 0) return;
    const int cur = w.s16(kShown);
    int next = cur;
    for (int i = 0; i < unitCount_; ++i) {
        next = forward ? (cur + 1 + i) % unitCount_ : (cur - (i + 1) + unitCount_) % unitCount_;
        if (units_[std::size_t(next)] != -1) break;
    }
    w.set16(kShown, next);
    computeUnit(shownUnit(), -1, true, 2);
    computeUnit(shownUnit(), -1, false, 0);
}

// The port's New UI style: small buttons take a fingertip (at least 34 x 32,
// centred on the drawn button); the arrow keys or a swipe step the student.
ysub::Hit KojinTask::hitTouch(int x, int y, int w, int h) const {
    if (ui::modern()) {
        const int gw = std::max(w, 0x22), gh = std::max(h, 0x20);
        x -= (gw - w) / 2; y -= (gh - h) / 2; w = gw; h = gh;
    }
    return ysub::hit(ctx_, x, y, w, h);
}

bool KojinTask::stepPrev() const { return ctx_.engine.input().pressed(legacy::Action::Left) || swipe_ < 0; }
bool KojinTask::stepNext() const { return ctx_.engine.input().pressed(legacy::Action::Right) || swipe_ > 0; }

// FUN_004b6d90: the text panel's scroll buttons (top, up, down, bottom).
int KojinTask::textScrollButton() {
    struct Btn { int hitY, fx, fy, sx, sy, w, h; };
    constexpr std::array<Btn, 4> kBtn{{{0x25d, 0x3e4, 0x265, 0x3d5, 0x1a, 0x10, 0x12},
                                       {0x283, 0x3e4, 0x28e, 0x3d5, 0x59, 0x10, 0xf},
                                       {0x2a9, 0x3e5, 0x2b6, 0x3d6, 0x9b, 0xe, 0xd},
                                       {0x2cf, 0x3e4, 0x2db, 0x3d5, 0xd5, 0xf, 0x11}}};
    for (std::size_t i = 0; i < kBtn.size(); ++i) {
        if (!(hitTouch(0x3e2, kBtn[i].hitY, 0x14, 0x23).pressed & legacy::kMouseLeft)) continue;
        ctx_.audio->sePlay(kSe);
        flashes_.add({.bank = kBank, .frame = 7, .sx = static_cast<std::int16_t>(kBtn[i].sx),
                       .sy = static_cast<std::int16_t>(kBtn[i].sy), .w = static_cast<std::int16_t>(kBtn[i].w),
                       .h = static_cast<std::int16_t>(kBtn[i].h)}, 0x4c, kBtn[i].fx, kBtn[i].fy, 4);
        return int(i) + 1;
    }
    return 0;
}

// --------------------------------------------------------------- 技能 tab ---
// FUN_004ae370: the 12 x 7 skill grid (one cell per skill, its state in the
// unit record's slot table +0xb0), learning skills with school points, and
// dragging learned skills onto / off the equipment bar.
// Slot states: 0 none, 1 / 2 / 4 learnable, 3 learned, 5 / 6 equipped.
void KojinTask::tabSkills() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const int unit = shownUnit();
    const std::uint32_t rec = GameWork::unitRecord(unit);
    const auto slotState = [&](int skill) { return rec + 0xb8 + std::uint32_t(skill - 1) * 0xc; };
    const int jobSkillSlots = logic::sdU8(kJobs + 1 + std::uint32_t(job_) * 0x40, v);
    const auto& m = ctx_.mouse();
    cursor_ = 0;

    switch (w.s16(kSkillDrag)) {
        case 0:
            skillShown_ = -1;
            w.set16(kSkillDrag, 1);
            break;
        case 1: {
            keyTabs_ = true;
            // Left / right: previous / next unit (DIK 0xcb / 0xcd).
            const bool prev = stepPrev();
            const bool next = stepNext();
            if (prev || next) {
                ctx_.audio->sePlay(kSe);
                stepUnit(next);
                w.set16(kSkillCol, -1); w.set16(kSkillRow, -1); w.set16(kSkillId, -1);
                w.set16(0x7d6a3e, 0);
            }
            // New style: with a learned skill selected, tapping a slot puts it
            // there (the old one back to the grid), as dropping it there would.
            if (const int sel = w.s16(kSkillId); ui::modern() && w.u8(kLeftPane) == 0 && sel > 0 && w.u8(slotState(sel)) == 3) {
                for (int k = 0; k < jobSkillSlots; ++k) {
                    const int x = k * 0x24 + 0x16e;
                    drawRect(ctx_, rect(0x13), 0x35, x, 0x1be);
                    if (!(hitTouch(x + 1, 0x1bf, 0x23, 0x23).pressed & legacy::kMouseLeft)) continue;
                    const std::uint32_t slot = rec + 0x52 + std::uint32_t(k) * 2;
                    if (const int old = w.s16(slot); old != 0) w.set8(slotState(old), 3);
                    w.set16(slot, sel);
                    w.set8(slotState(sel), 6);
                    ctx_.audio->sePlay(kSe);
                    computeUnit(unit, -1, false, 0);
                    tapPlaced_ = true;
                    break;
                }
            }
            constexpr std::array<int, 4> kScroll{2, 0, 1, 3};
            if (const int b = textScrollButton(); b > 0) skillText_.scroll(kScroll[std::size_t(b - 1)]);
            break;
        }
        case 2: case 3: {                                  // dragging onto the bar
            const bool fromBar = w.s16(kSkillDrag) == 3;
            cursor_ = 2;
            int result = 0;                                // 1 dropped nowhere, 2 swapped, 3 placed
            if (!(ysub::hitEntry(ctx_, 0).held & legacy::kMouseLeft)) {
                ctx_.audio->sePlay(kSe);
                w.set16(kSkillDrag, 1);
                result = 1;
            }
            const int id = w.s16(kSkillId);
            int placedAt = -1;
            for (int k = 0; k < jobSkillSlots; ++k) {
                if (!ysub::hit(ctx_, k * 0x24 + 0x16f, 0x1bf, 0x23, 0x23).over) continue;
                drawRect(ctx_, rect(0x13), 0x35, k * 0x24 + 0x16e, 0x1be);
                if (result != 1) continue;
                placedAt = k;
                const std::uint32_t slot = rec + 0x52 + std::uint32_t(k) * 2;
                const int old = w.s16(slot);
                w.set16(slot, id);
                if (old == 0) result = 3;
                else {
                    if (fromBar) w.set16(rec + 0x52 + std::uint32_t(w.s16(kBarSlot)) * 2, old);
                    else w.set8(slotState(old), 3);
                    result = 2;
                }
            }
            drawSkillIcon(m.x - 0x10, m.y - 0x10, fromBar ? w.s16(kBarCat) : w.s16(kSkillCol),
                          fromBar ? w.s16(kBarSub) : w.s16(kSkillRow), 7, 0x34);
            // New style: a second tap on the selected skill's slot takes it off.
            if (ui::modern() && fromBar && result == 3 && retap_ && placedAt == w.s16(kBarSlot)) {
                w.set16(rec + 0x52 + std::uint32_t(placedAt) * 2, 0);
                result = 1;
            }
            if (result == 1) w.set8(slotState(id), 3);    // back to the grid, unequipped
            if (result != 0) computeUnit(unit, -1, false, 0);
            if (fromBar) {
                const int k = w.s16(kBarSlot);
                drawRect(ctx_, rect(0x13), 0x35, k * 0x24 + 0x16e, 0x1be);
                drawSkillIcon(k * 0x24 + 0x16f, 0x1bf, w.s16(kBarCat), w.s16(kBarSub), 6, 0x40);
            }
            break;
        }
        default: break;
    }

    // The grid.
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 0xc; ++col) {
            const int id = gridSkill(col, row);
            const int state = id > 0 ? w.u8(slotState(id)) : 0;
            const int x = col * 0x24 + 0xf0, y = row * 0x24 + 0x1e9;
            if (w.s16(kSkillDrag) == 1) {
                const ysub::Hit h = ysub::hit(ctx_, x, y, 0x23, 0x23);
                if (h.over) {
                    if (state != 0) ysub::box(ctx_, x, y, 0x24, 0x24, 0x3e, 0xffffffffu);   // FUN_004d2690
                    if (state == 3) cursor_ = 1;
                    if (w.u8(kLeftPane) != 0) cursor_ = 0;
                    if (h.pressed & legacy::kMouseLeft) {
                        const auto select = [&] {
                            w.set16(kSkillCol, col); w.set16(kSkillRow, row); w.set16(kSkillId, id);
                            w.set16(0x7d6a3e, row * 0xc + col);
                            ctx_.audio->sePlay(kSe);
                        };
                        switch (state) {
                            case 2: case 4: case 5: case 6: select(); break;
                            case 3:
                                select();
                                if (w.u8(kLeftPane) == 0) { w.set16(kSkillDrag, 2); w.set8(slotState(id), 6); }
                                else cursor_ = 0;
                                break;
                            default: break;
                        }
                    }
                }
            }
            drawSkillIcon(x + 1, y, col, row, state, 0x48);
        }
    }
    if (w.s16(kSkillCol) < 0 || w.s16(kSkillRow) < 0) w.set8(kBuyState, 0);
    else drawRect(ctx_, rect(0x13), 0x3e, w.s16(kSkillCol) * 0x24 + 0xf1, w.s16(kSkillRow) * 0x24 + 0x1ea);

    // The selected skill: icon, name, MP, price and its detail lines.
    w.set32(kPrice, 0);
    const int id = w.s16(kSkillId);
    if (id < 0) {
        drawSkillIcon(0x2a9, 0x1e9, 0, 0, 8, 0x40);
        w.set8(kBuyState, 0);
    } else {
        switch (w.u8(slotState(id))) {
            case 0: case 7: case 8: w.set8(kBuyState, 0); break;
            case 1: case 2: case 4: w.set8(kBuyState, 1); break;
            case 5: case 6: w.set8(kBuyState, 2); break;
            default: break;                                // 3 keeps the previous state
        }
        const std::uint32_t S = kSkills + std::uint32_t(id) * 0x48;
        const int cat = logic::sdU8(S + 4, v) - 1, sub = logic::sdU8(S + 7, v) - 1;
        drawSkillIcon(0x2a9, 0x1e9, cat, sub, 3, 0x40);
        drawSkillName(0x2cd, 0x1ed, cat, sub, 0x40);
        drawDigits(0x313, 0x1ff, 0, 0, 3, 0x4a, kDigits, std::uint32_t(logic::sdS16(S + 8, v)), true);
        w.set32(kPrice, logic::sdS32(S + 0x40, v));
        if (skillShown_ != id) {
            skillText_.select(id, 10);
            for (auto& l : skillLine_) l.reset(1);
            const bool tw = v == GameVersion::Tw;
            const auto num = [](int n) { char b[16]; std::snprintf(b, sizeof b, "%4d", n); return std::string(b); };
            skillLine_[0].addUtf8(num(std::uint16_t(logic::sdS16(S + 0xa, v))));
            skillLine_[1].addUtf8(num(std::uint16_t(logic::sdS16(S + 0xe, v))));
            const int target = logic::sdU8(S + 0x18, v), range = logic::sdU8(S + 0x19, v);
            skillLine_[2].addUtf8(target < 7 ? label(v, (tw ? kTargetTw : kTargetJp)[std::size_t(target)]) : "!!!!ERROR!!!!");
            skillLine_[3].addUtf8(range < 6 ? label(v, (tw ? kRangeTw : kRangeJp)[std::size_t(range)]) : "!!!!ERROR!!!!");
        }
        skillLine_[0].draw(ctx_, 0x304, 0x22d, 0x3d);
        skillLine_[1].draw(ctx_, 0x304, 0x240, 0x3d);
        skillLine_[2].draw(ctx_, 0x3a2, 0x22d, 0x3d);
        skillLine_[3].draw(ctx_, 0x3a2, 0x240, 0x3d);
        skillText_.draw(ctx_, 0x2b2, 0x25e, 0x3d);
        skillShown_ = static_cast<std::int16_t>(id);
    }

    ysub::drawSub(ctx_, {.bank = rect(2).bank, .frame = rect(2).frame}, 0x4e, 0xd7, 0x1a7);
    // Equipped / available skill slots.
    const auto countRow = [&](int row, int value) {
        const std::uint32_t n = 0x61c120 + std::uint32_t(row) * 10;
        drawDigits(logic::sdS16(n + 4, v), logic::sdS16(n + 6, v), 0, 0, logic::sdU8(n + 8, v),
                   logic::sdS16(n + 2, v), logic::sdS16(n, v), std::uint32_t(value), true);   // FUN_004b70a0
    };
    countRow(0x20, equippedSkills_);
    countRow(0x21, skillSlots_);
    for (int k = jobSkillSlots; k < 8; ++k) fill(ctx_, k * 0x24 + 0x16f, 0x1bf, 0x22, 0x22, 0x36, 0x7000a000u);

    // Learn button: costs the price in school points; may raise the rank.
    if (logic::unitSummary(w, v, unit).toCap == 0) w.set8(kBuyState, 0);
    const int points = w.s32(kPoints), price = w.s32(kPrice);
    if (w.u8(kBuyState) == 0) {
        fill(ctx_, 0x354, 0x1bf, 0xa3, 0x25, 0x37, 0x70010101u);
    } else if (w.u8(kBuyState) == 1) {
        const ysub::Hit h = ysub::hit(ctx_, 0x360, 0x1c3, 0x8a, 0x1c);
        if (points - price < 0) fill(ctx_, 0x354, 0x1bf, 0xa3, 0x25, 0x37, 0x70010101u);
        if (h.over && (h.pressed & legacy::kMouseLeft) && points - price >= 0) {
            const ysub::Rect r = rect(0x1a);
            flashes_.add(r, 0x47, r.x, r.y, 8);
            ctx_.audio->sePlay(0xb);
            const int line = logic::unitVoiceLine(unit, 6, 0);
            if (line >= 0) ctx_.audio->voicePlay(logic::unitVoicePath(line, v));
            if (fxFile_.loaded()) fx_.play(fxNode_[2], 0, 1, 0);
            w.set8(slotState(id), 3);
            const std::uint32_t score = GameWork::unitProgress(unit) + 0x11c;
            w.set32(score, std::int64_t(w.s32(score)) + price);
            const int oldRank = derived_.u8(0xe);
            const int rank = logic::unitSummary(w, v, unit).rank;
            w.set8(rec + 0x50, rank);
            if (oldRank < rank) {
                if (fxFile_.loaded()) fx_.play(fxNode_[1], 0, 1, 0);
                ctx_.audio->sePlay(0xb);
            }
            computeUnit(unit, -1, true, 2);
            computeUnit(unit, -1, false, 0);
            w.set32(kPoints, points - price);
            w.set8(kBuyState, 2);
        }
    } else if (w.u8(kBuyState) == 2) {
        w.set32(kPrice, 0);
        const ysub::Rect r = rect(0x1b);
        flashes_.add(r, 0x48, r.x, r.y, 1);
    }
    const int left = w.s32(kPoints) - w.s32(kPrice);
    drawGrouped(0x3ec, 0x1ed, 0, 9, 0x4a, kDigits, std::uint32_t(w.s32(kPoints)));
    drawGrouped(0x3ec, 0x200, 0, 9, 0x4a, kDigits, std::uint32_t(w.s32(kPrice)));
    drawGrouped(0x3ec, 0x213, left < 0 ? 0x130 : 0x98, 9, 0x4a, kDigits, std::uint32_t(left < 0 ? -left : left));

    // The other button switches the left pane (FUN_004b4000).
    if (ysub::clicked(ctx_, 0x2b6, 0x1c3, 0x8a, 0x1c)) {
        ctx_.audio->sePlay(kSe);
        w.set8(kLeftPane, w.u8(kLeftPane) == 0 ? 1 : 0);
    }
    if (w.u8(kLeftPane) != 0) { const ysub::Rect r = rect(0x19); drawRect(ctx_, r, 0x47, r.x, r.y); }
}

void KojinTask::voice(int unit, int category) {
    const int line = logic::unitVoiceLine(unit, category, 0);
    if (line >= 0) ctx_.audio->voicePlay(logic::unitVoicePath(line, ctx_.version));
}

// FUN_004b1110: may `item` go into item slot `slot`? Yes if that slot holds
// an item of the same type, else if fewer than the type's limit (row 0 of
// DAT_006e45f8) are equipped.
bool KojinTask::canEquip(std::uint32_t rec, int slot, int item) const {
    const GameVersion v = ctx_.version;
    const int type = std::uint16_t(logic::sdS16(kItems + 2 + std::uint32_t(item) * 0x38, v));
    std::array<int, 0x14> count{};
    for (int k = 0; k < itemSlots_; ++k) {
        const int it = ctx_.work.u16(rec + 0x62 + std::uint32_t(k) * 2);
        if (it == 0) continue;
        const int t = std::uint16_t(logic::sdS16(kItems + 2 + std::uint32_t(it) * 0x38, v));
        ++count[std::size_t(t) % count.size()];
        if (k == slot && t == type) return true;
    }
    return count[std::size_t(type) % count.size()] < logic::sdU8(0x6e45f8 + std::uint32_t(type), v);
}

// FUN_004b0f80: storage page buttons (hit rows 0x22..0x25).
int KojinTask::pageButton() {
    for (int i = 0; i < 4; ++i) {
        if (!(ysub::hitEntry(ctx_, 0x22 + i).pressed & legacy::kMouseLeft)) continue;
        ctx_.audio->sePlay(kSe);
        const ysub::Rect r = rect(0x1f + i);
        flashes_.add(r, 0x3e, r.x, r.y, 4);
        return i + 1;
    }
    return 0;
}

// FUN_004b6c60: item text scroll buttons (hit rows 0x20 / 0x21).
int KojinTask::itemScrollButton() {
    for (int i = 0; i < 2; ++i) {
        if (!(ysub::hitEntry(ctx_, 0x20 + i).pressed & legacy::kMouseLeft)) continue;
        ctx_.audio->sePlay(kSe);
        const ysub::Rect r = rect(0x17 + i);
        flashes_.add(r, 0x4c, r.x, r.y, 4);
        return i + 1;
    }
    return 0;
}

// FUN_004b4520: the selected item's effects, type and per-type limit, and
// which of the five class pairs (row by the unit's +0x72) may use it.
void KojinTask::drawItemInfo(int item) {
    const GameVersion v = ctx_.version;
    const std::uint32_t I = kItems + std::uint32_t(item) * 0x38;
    struct Field { std::uint8_t off; bool word; };
    constexpr std::array<Field, 22> kEffect{{{0xc, true}, {0xe, true}, {0x10, true}, {0x12, true}, {0x14, true},
                                             {0x16, true}, {0x18, false}, {0x1a, true}, {0x1c, false}, {0x1d, false},
                                             {0x1e, false}, {0x1f, false}, {0x20, false}, {0x21, false}, {0x22, true},
                                             {0x24, true}, {0x26, true}, {0x28, false}, {0x2c, true}, {0x30, true},
                                             {0x32, true}, {0x34, false}}};
    int n = 0;
    for (std::uint32_t j = 0; j < 3; ++j) {
        const int kind = static_cast<std::int8_t>(logic::sdU8(I + 0x35 + j, v));
        int value = -1;
        if (kind >= 8 && kind - 8 < int(kEffect.size())) {
            const Field f = kEffect[std::size_t(kind - 8)];
            value = static_cast<std::int16_t>(f.word ? logic::sdS16(I + f.off, v) : logic::sdU8(I + f.off, v));
        }
        if (value < 0) continue;
        drawRect(ctx_, rect(kItemStatIcons), 0x4a, 0x2ae, n * 0x13 + 0x22c, 0, (kind - 8) * 0x10);
        if (kind == 0x19) drawRect(ctx_, rect(0x4d), 0x4a, 0x2ea, n * 0x13 + 0x22e, 0, (value - 1) * 0x10);
        else drawDigits(0x300, n * 0x13 + 0x22e, 0, 0, 2, 0x4a, kDigits, std::uint32_t(value), true);
        ++n;
    }
    drawRect(ctx_, rect(0x4e), 0x4a, 0x394, 0x1ed, 0, std::min((item - 1) % 0xc, 8) * 0xe);
    const int type = std::uint16_t(logic::sdS16(I + 2, v));
    drawDigits(0x3b1, 0x200, 0, 0, 2, 0x4a, kDigits, logic::sdU8(0x6e45f8 + std::uint32_t(type), v), true);
    const std::uint32_t rec = GameWork::unitRecord(shownUnit());
    for (int c = 0; c < 5; ++c) {
        const int row = ctx_.work.u8(rec + 0x72) + c * 2;
        const bool ok = logic::sdU8(0x6e45f8 + std::uint32_t(type) + std::uint32_t(row) * 0xf, v) != 0;
        drawRect(ctx_, rect(0x4f), 0x4a, 0x3be, c * 0x13 + 0x22f, 0, ok ? 0 : 0xe);
    }
}

// --------------------------------------------------------------- 装備 tab ---
// FUN_004afb70: the item storage (12 x 7 a page, 10 pages) and dragging
// items onto / off the equipment bar.
void KojinTask::tabItems() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const int unit = shownUnit();
    const std::uint32_t rec = GameWork::unitRecord(unit);
    const int jobSkillSlots = logic::sdU8(kJobs + 1 + std::uint32_t(job_) * 0x40, v);
    const int jobItemSlots = logic::sdU8(kJobs + 2 + std::uint32_t(job_) * 0x40, v);
    const auto store = [&](int id) { return kStore + std::uint32_t(id) * 2; };
    const auto setState = [&](int id, int st) { w.set16(store(id), (w.u16(store(id)) & 0xf0ff) | (st & 0xf) << 8); };
    const auto& m = ctx_.mouse();

    ysub::drawSub(ctx_, {.bank = rect(3).bank, .frame = rect(3).frame}, 0x4e, 0xd7, 0x1a7);
    const auto countRow = [&](int row, int value) {
        const std::uint32_t nr = 0x61c120 + std::uint32_t(row) * 10;
        drawDigits(logic::sdS16(nr + 4, v), logic::sdS16(nr + 6, v), 0, 0, logic::sdU8(nr + 8, v),
                   logic::sdS16(nr + 2, v), logic::sdS16(nr, v), std::uint32_t(value), true);
    };
    countRow(0x20, equippedItems_);
    countRow(0x21, itemSlots_);
    const int dimmed = w.u8(kLeftPane) != 0 ? 8 : jobSkillSlots;   // skills cannot change here
    for (int k = 0; k < dimmed; ++k) fill(ctx_, k * 0x24 + 0x16f, 0x1bf, 0x22, 0x22, 0x37, 0x70010101u);
    cursor_ = 0;

    const auto overBar = [&](int k) { return ysub::hit(ctx_, (jobSkillSlots + k) * 0x24 + 0x16f, 0x1bf, 0x23, 0x23).over; };
    const auto lightBar = [&](int k) { drawRect(ctx_, rect(0x13), 0x35, (jobSkillSlots + k) * 0x24 + 0x16e, 0x1be); };
    const int id = w.s16(kItemId);
    switch (w.s16(kItemDrag)) {
        case 0:
            itemShown_ = -1;
            w.set16(kItemDrag, 1);
            break;
        case 1: {
            keyTabs_ = true;
            const bool prev = stepPrev();
            const bool next = stepNext();
            if (prev || next) {
                ctx_.audio->sePlay(kSe);
                stepUnit(next);
                w.set16(kItemCol, -1);
                w.set16(kItemId, -1);
            }
            // New style: with an item from storage selected, tapping a slot
            // equips it there (the old one back to storage), as dropping it would.
            if (const int sel = w.s16(kItemId); ui::modern() && w.u8(kLeftPane) == 0 && sel > 0 && (w.u16(store(sel)) >> 8 & 0xf) == 1) {
                for (int k = 0; k < jobItemSlots; ++k) {
                    if (canEquip(rec, k, sel)) lightBar(k);
                    if (!(hitTouch((jobSkillSlots + k) * 0x24 + 0x16f, 0x1bf, 0x23, 0x23).pressed & legacy::kMouseLeft)) continue;
                    tapPlaced_ = true;
                    if (!canEquip(rec, k, sel)) { voice(unit, 8); break; }
                    voice(unit, 0xb);
                    w.set16(store(sel), (w.u16(store(sel)) & 0xf0ff & 0xff01) | 0x300 | (unit & 0x7f) << 1);
                    const std::uint32_t slot = rec + 0x62 + std::uint32_t(k) * 2;
                    if (const int old = w.u16(slot); old != 0) setState(old, 1);
                    w.set16(slot, sel);
                    computeUnit(unit, -1, false, 0);
                    break;
                }
            }
            (void)itemScrollButton();          // called twice a frame in the original; this result is unused
            switch (pageButton()) {
                case 1: w.set16(kItemPage, 0); break;
                case 2: if (w.s16(kItemPage) != 0) w.set16(kItemPage, w.s16(kItemPage) - 1); break;
                case 3: if (w.s16(kItemPage) != 9) w.set16(kItemPage, w.s16(kItemPage) + 1); break;
                case 4: w.set16(kItemPage, 9); break;
                default: break;
            }
            break;
        }
        case 2: {                                          // from storage
            cursor_ = 2;
            if (!(ysub::hitEntry(ctx_, 0).held & legacy::kMouseLeft)) {
                ctx_.audio->sePlay(kSe);
                w.set16(kItemDrag, 1);
                bool placed = false;
                for (int k = 0; k < jobItemSlots; ++k) {
                    if (!overBar(k)) continue;
                    lightBar(k);
                    if (!canEquip(rec, k, id)) { voice(unit, 8); continue; }
                    voice(unit, 0xb);
                    const std::uint32_t slot = rec + 0x62 + std::uint32_t(k) * 2;
                    if (const int old = w.u16(slot); old != 0) setState(old, 1);
                    w.set16(slot, id);
                    placed = true;
                }
                if (!placed) setState(id, w.u8(kItemPrev));
                computeUnit(unit, -1, false, 0);
            } else {
                int style = 1;
                for (int k = 0; k < jobItemSlots; ++k) {
                    if (!overBar(k)) continue;
                    lightBar(k);
                    style = canEquip(rec, k, id) ? 1 : 2;
                }
                drawItemIcon(m.x - 0x10, m.y - 0x10, id - 1, style, 0x34);
            }
            break;
        }
        case 3: {                                          // from the bar
            cursor_ = 2;
            const int pos = w.s16(kItemBarPos);
            drawRect(ctx_, rect(0x13), 0x35, pos * 0x24 + 0x16e, 0x1be);
            drawItemIcon(pos * 0x24 + 0x16f, 0x1bf, id - 1, 5, 0x3f);
            if (!(ysub::hitEntry(ctx_, 0).held & legacy::kMouseLeft)) {
                w.set16(kItemDrag, 1);
                ctx_.audio->sePlay(kSe);
                bool placed = false;
                for (int k = 0; k < jobItemSlots; ++k) {
                    if (!overBar(k)) continue;
                    const std::uint32_t slot = rec + 0x62 + std::uint32_t(k) * 2;
                    if (ui::modern() && retap_ && k == w.s16(kItemBarK)) break;   // New style: a second tap takes it off
                    if (w.u16(slot) != 0) w.set16(rec + 0x62 + std::uint32_t(w.s16(kItemBarK)) * 2, w.u16(slot));
                    w.set16(slot, id);
                    ctx_.audio->sePlay(kSe);
                    placed = true;
                }
                if (!placed) setState(id, 1);              // back to storage
                computeUnit(unit, -1, false, 0);
            } else {
                drawItemIcon(m.x - 0x10, m.y - 0x10, id - 1, 1, 0x34);
                for (int k = 0; k < jobItemSlots; ++k) if (overBar(k)) lightBar(k);
            }
            break;
        }
        default: break;
    }

    // Storage: owned items of this page.
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 0xc; ++col) {
            const int index = row * 0xc + col + w.s16(kItemPage) * 0xc;
            const int item = index + 1;
            if (item > 0x168) continue;
            const int word = w.u16(store(item));
            if (!(word & 1)) continue;
            const int state = word >> 8 & 0xf;
            const int x = col * 0x24 + 0xf0, y = row * 0x24 + 0x1e9;
            if (w.s16(kItemDrag) == 1) {
                const ysub::Hit h = ysub::hit(ctx_, x, y, 0x23, 0x23);
                if (h.over) {
                    ysub::box(ctx_, x, y, 0x24, 0x24, 0x3e, 0xffffffffu);   // FUN_004d2690
                    if (state == 1) cursor_ = 1;
                    if (w.u8(kLeftPane) != 0) cursor_ = 0;
                    if (h.pressed & legacy::kMouseLeft) {
                        w.set16(kItemIndex, index);
                        ctx_.audio->sePlay(kSe);
                        w.set16(kItemCol, col); w.set16(kItemRow, row); w.set16(kItemId, item);
                        if (w.u8(kLeftPane) == 0 && state == 1) {
                            cursor_ = 2;
                            w.set16(kItemDrag, 2);
                            w.set8(kItemPrev, state);
                            const int held = (word & 0xf0ff) | 0x300;                  // state 3, then the owner
                            w.set16(store(item), (held & 0xff01) | (unit & 0x7f) << 1);
                        } else {
                            cursor_ = 0;
                        }
                    }
                }
            }
            drawItemIcon(x + 1, y, index, w.u16(store(item)) >> 8 & 0xf, 0x3f);
            if (w.s16(kItemId) == item) drawRect(ctx_, rect(0x13), 0x35, x + 1, y + 1);
        }
    }

    // The selected item.
    const int sel = w.s16(kItemId);
    if (sel < 1) {
        drawItemIcon(0x2a9, 0x1e9, sel - 1, 6, 0x3f);
    } else {
        drawItemIcon(0x2a9, 0x1e9, sel - 1, 1, 0x3f);
        drawItemInfo(sel);
        if (itemShown_ != sel) {
            itemText_.select(sel, 4);
            itemRarity_.reset(1);
            constexpr std::array<const char*, 8> kRarity{"VULGAR", "Common", "Superior", "Rare",
                                                         "Mystic", "Hory", "Divine", "Special"};
            const int r = std::uint16_t(logic::sdS16(kItems + 8 + std::uint32_t(sel) * 0x38, v)) - 1;
            itemRarity_.addUtf8(r >= 0 && r < 8 ? kRarity[std::size_t(r)] : "!!!!ERROR!!!!");
        }
        itemRarity_.draw(ctx_, 0x3a9, 0x212, 0x3d);
        itemText_.draw(ctx_, 0x2b2, 0x2b6, 0x3d);
        itemShown_ = static_cast<std::int16_t>(sel);
        const auto& names = itemNames(v);
        if (std::size_t(sel) < names.size())
            ctx_.draw.text({.x = 0x2d0 + 0x78 / 2, .y = 0x1ed + 6, .priority = 0x3d, .argb = 0xFFFFFFFFu,
                            .text = std::string(names[std::size_t(sel)]), .fontPx = 12, .centred = true});
    }
    switch (itemScrollButton()) {
        case 1: itemText_.scroll(0); break;
        case 2: itemText_.scroll(1); break;
        default: break;
    }
    if (ysub::clicked(ctx_, 0x2b6, 0x1c3, 0x8a, 0x1c)) {
        ctx_.audio->sePlay(kSe);
        w.set8(kLeftPane, w.u8(kLeftPane) == 0 ? 1 : 0);
    }
    if (w.u8(kLeftPane) != 0) { const ysub::Rect r = rect(0x19); drawRect(ctx_, r, 0x48, r.x, r.y); }
    for (int k = 0; k < jobSkillSlots; ++k) fill(ctx_, k * 0x24 + 0x16f, 0x1bf, 0x22, 0x22, 0x36, 0x700000a0u);
}

} // namespace ch
