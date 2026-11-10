// ============================================================================
// kojin_task.hpp — KojinTask, the roster (名簿) screen of scene 9
// (src\yoshimaru\kojin)
//
// Runs beside HanTask for the whole scene and shows itself while
// DAT_007a4e60 is 1 (HanTask's 名簿 button); 2 fades it out, 99 ends it.
//
//   FUN_004b8d50  Start: the DAT_007a4e60 loop
//   FUN_004b5600  open: unit list, TextLists, first unit
//   FUN_004b5a00  reset the tab state
//   FUN_004b5fc0  one frame: frames, tab strip, the open tab, cursor
//   FUN_004b66f0  compute the shown unit (record copy, derived values)
//   FUN_004b28f0  status card (left pane), FUN_004b1f30 skills, FUN_004b2100 items
//   FUN_004b5050 / FUN_004b52f0  equipment bar (items / skills)
//
// Task work area (offsets kept in comments): +0x594 unit count, +0x596
// unit ids, +0x5fa their group, +0x570 / +0x582 item / skill slots of the
// job, +0x632 job, +0x634 record copy, +0xad4 derived, +0xb84 baseline
// derived, +0xc34 bonus, +0xc80 / +0xc82 baseline / current stat total,
// +0xc84 job shown.
//
// Ported tabs: 技能 (FUN_004ae370), 装備 (FUN_004afb70), 生徒一覧
// (FUN_004b3bb0), 職業変更 (FUN_004b12a0), 能力値UP (FUN_004abb60).
// ============================================================================
#pragma once
#include "../anm/anm_player.hpp"
#include "../game_context.hpp"
#include "../state/unit_stats.hpp"
#include "../ysub/ysub.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace ch {

class KojinBlend;

class KojinTask final : public Task {
public:
    // FUN_004b8a90; its 12px font (CreateFontA, +0x323) is the engine's text font here.
    explicit KojinTask(GameContext& ctx);
    ~KojinTask() override;                      // FUN_004b8be0 / FUN_004b8d10
    Co<> start(std::uint32_t) override;         // FUN_004b8d50

private:
    friend class KojinBlend;                     // the New style's one-page screen
    static constexpr std::uint16_t kBank = 0xd;      // data\menu\KojinData.bin

    void open();                                 // FUN_004b5600
    void reset();                                // FUN_004b5a00
    void frame();                                // FUN_004b5fc0
    void computeUnit(int unit, int previewJob, bool baseline, int mode);   // FUN_004b6690
    void computeRecord(std::uint32_t rec, int previewJob, bool baseline, int mode);   // FUN_004b66f0
    [[nodiscard]] int shownUnit() const;         // +0x596[DAT_007d6a34]

    void drawCard();                             // FUN_004b28f0
    void drawCardSkills();                       // FUN_004b1f30
    void drawCardItems();                        // FUN_004b2100
    void drawEquipItems();                       // FUN_004b5050
    void drawEquipSkills();                      // FUN_004b52f0
    void drawEffects();
    void drawPicker();                           // FUN_004b4000: left pane, pick a unit
    void drawUnitCell(int x, int y, int listIndex, std::uint8_t alpha);   // FUN_004b8140 / 82f0 / 7fc0 / 8080
    void tabStudents();                          // FUN_004b3bb0: 生徒一覧
    void tabJobs();                              // FUN_004b12a0: 職業変更
    void tabStats();                             // FUN_004abb60: 能力値UP                          // FUN_004b28f0's AnmAllResult loop

    // 技能 tab (FUN_004ae370).
    void tabSkills();
    void stepUnit(bool forward);                 // FUN_004b6fb0 + recompute
    [[nodiscard]] ysub::Hit hitTouch(int x, int y, int w, int h) const;   // New style: grown to take a fingertip
    [[nodiscard]] bool stepPrev() const;          // left key or a swipe right
    [[nodiscard]] bool stepNext() const;
    [[nodiscard]] int textScrollButton();        // FUN_004b6d90: 1..4 or 0
    [[nodiscard]] int gridSkill(int col, int row) const;   // FUN_004ae2f0 (DAT_0061ba30)

    // 装備 tab (FUN_004afb70).
    void tabItems();
    void drawItemInfo(int item);                 // FUN_004b4520
    [[nodiscard]] bool canEquip(std::uint32_t rec, int slot, int item) const;   // FUN_004b1110
    [[nodiscard]] int pageButton();              // FUN_004b0f80: 1..4 or 0
    [[nodiscard]] int itemScrollButton();        // FUN_004b6c60: 1..2 or 0
    void voice(int unit, int category);

    // FUN_004b7590: grouped digits (source pitch = rect +0xe), with a comma glyph between groups.
    void drawGrouped(int x, int y, int srcOffY, int digits, int prio, int rectIndex, std::uint32_t value,
                     int pitch = 8, int gap = 1, int groupGap = 4, int flags = 7);
    // Drawing helpers over the Kojin rect table (DAT_0061bae0 + 16 i).
    [[nodiscard]] ysub::Rect rect(int i) const;
    void drawDigits(int x, int y, int srcOffX, int srcOffY, int digits, int prio, int rectIndex,
                    std::uint32_t value, bool suppress);                          // FUN_004b7230
    void drawValueRow(int row, int gauge, int max, int value, int prio, int srcOffY,
                      int gaugeRow = 0, bool number = true);                     // FUN_004b7e90
    void drawSkillIcon(int x, int y, int cat, int sub, int style, int prio);     // FUN_004b4d70
    void drawSkillName(int x, int y, int cat, int sub, int prio);                // FUN_004b4f40
    void drawItemIcon(int x, int y, int index, int style, int prio);             // FUN_004b4b00
    void drawJobIcon(int x, int y, int job, int prio);                           // FUN_004b8460
    void drawJobName(int x, int y, int job, int prio);                           // FUN_004b8700

    GameContext&   ctx_;
    ysub::Flashes  flashes_;
    std::unique_ptr<KojinBlend> blend_;          // New style
    bool           needOpen_ = true;             // DAT_007a4ae0
    std::int16_t   fade_ = 0;

    std::array<std::int16_t, 0x32> units_{};     // +0x596
    std::array<std::int8_t, 0x32>  unitGroup_{}; // +0x5fa
    int            unitCount_ = 0;               // +0x594
    std::uint8_t   itemSlots_ = 0, skillSlots_ = 0;   // +0x570 / +0x582
    std::int16_t   job_ = 0, jobShown_ = 0;      // +0x632 (chosen) / +0xc84 (shown)
    std::int16_t   currentJob_ = 0;              // +0x30: the record's job
    std::int16_t   jobCol_ = 0, jobRow_ = 0;     // +0x62e / +0x630
    bool           jobPicked_ = false;           // +0x62c
    ysub::TextList jobText_;                     // JobExp, 4 lines
    std::int16_t   jobTextShown_ = -1;           // +0x391d8
    ysub::TextList capText_;                     // KSCapabilityUP, 6 lines
    std::int16_t   capShown_ = -1;               // +0x391da
    std::array<std::uint8_t, 0x28> jobCount_{};  // +0x41: roster units per job
    stats::Record  copy_;                        // +0x634
    stats::Derived derived_, baseline_;          // +0xad4 / +0xb84
    stats::Bonus   bonus_;                       // +0xc34
    int            total_ = 0, baselineTotal_ = 0;    // +0xc82 / +0xc80
    std::uint8_t   cursor_ = 0;                  // DAT_007d6a30
    bool           keyTabs_ = false;             // +0xc88: up / down switch tabs this frame
    bool           tapPlaced_ = false;           // New style: a tap equipped into a slot this frame
    bool           retap_ = false;               // New style: the slot tapped held the selection already
    int            swipe_ = 0;                   // New style: -1 / 1 a swipe stepped the student this frame
    std::uint8_t   equippedSkills_ = 0, equippedItems_ = 0;   // +0xc87 / +0xc86

    ysub::TextList skillText_;                   // SkillExp, 10 lines
    std::array<ysub::TextList, 4> skillLine_;    // the four one-line detail fields
    std::int16_t   skillShown_ = -1;             // +0x391d6
    ysub::TextList itemText_, itemRarity_;       // ItemExp (4 lines), rarity (1 line)
    std::int16_t   itemShown_ = -1;              // +0x391d4
    AnmFile        fxFile_;                      // +0x3a1a0: data\AllResult\AnmAllResult.bin
    AnmPlayer      fx_;
    std::array<AnmPlayer::NodeId, 10> fxNode_{}; // +0x3a1f4 + 0x58 i: 1 rank up, 2 learned
};

} // namespace ch
