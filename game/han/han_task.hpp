// ============================================================================
// han_task.hpp — HanTask, the class screen of scene 9 (src\yoshimaru\han)
//
// The weekly planning screen: five groups (班), each led by a guest unit,
// sent on an adventure or to a lecture; a top menu (LOAD / SAVE / 教務室 /
// 名簿 / Title) and 予定進行 to commit the week. KojinTask (名簿) runs beside
// it and is woken through DAT_007a4e60.
//
//   FUN_004ab7a0  Start: command loop, then the next scene
//   FUN_004a5ce0  init (FUN_004a5a60 banks, fonts, work fields)
//   FUN_004a7c40  one visit of the screen, returns a command
//     FUN_004a7d30  can the week be committed (DAT_007d6a26)
//     FUN_004a6f90  screen state machine (fades, menu, confirm dialogs)
//     FUN_004a81a0  draw
//     FUN_004a3240  unit info popup (right-click a unit)
//   FUN_004a95f0  group status
//   FUN_004aa320  top menu, FUN_004a3ca0 button clicks
//
// Work area: +0x76bc exit, +0x76c0 loop again, +0x76c2 cursor shape,
// +0x76c4 screen running, +0x76c6 state, +0x76c8 fade counter,
// +0x7950 command, +0x1294 dialog text built, +0x30 commit pending.
// ============================================================================
#pragma once
#include "../anm/anm_player.hpp"
#include "../game_context.hpp"
#include "../ysub/ysub.hpp"

#include <array>
#include <cstdint>

namespace ch {

class HanTask final : public Task {
public:
    explicit HanTask(GameContext& ctx) : ctx_(ctx) {}   // FUN_004ab570: anm player, sprite, flashes, 3 text lists
    ~HanTask() override;
    Co<> start(std::uint32_t) override;         // FUN_004ab7a0

    // FUN_004a95f0 result (7 shorts).
    struct GroupStatus {
        std::int16_t kind = 0;      // 0 no leader, 1 no members, 2/3 adventure set/unset, 4/5 lecture set/unset
        std::int16_t affinity = 0;  // mean pairwise affinity
        std::int16_t grade = 0;     // 1..7 from the mean
        std::int16_t a = 0, b = 0, c = 0, d = 0;   // chosen adventure / lecture fields
    };
    [[nodiscard]] static GroupStatus groupStatus(GameWork& w, int group);

private:
    static constexpr std::uint16_t kBank = 0xc;      // data\menu\HanData.bin
    static constexpr std::uint16_t kAnmBank = 0x10;  // data\menu\AnmHan.bin

    void init();                                 // FUN_004a5ce0 / FUN_004a5a60
    Co<int> visit();                             // FUN_004a7c40
    [[nodiscard]] bool canCommit();              // FUN_004a7d30
    Co<> screenState();                          // FUN_004a6f90
    Co<> fadeTo(int nextState, int command);     // the 30-frame fade-outs of FUN_004a6f90
    Co<> confirm(bool commit);                   // states 0xd / 0xe
    Co<> tutorialMenu();                         // state 0xa: mm001.ybc (JP)
    [[nodiscard]] bool hasTutorial() const;      // the menu's Tutorial entry does something
    int  menu();                                 // FUN_004aa320
    void clickButtons();                         // FUN_004a3ca0
    int  buttonUnderPointer();                   // FUN_004aa4b0
    void flashButton(int b);                     // FUN_004aa590
    void drawButton(int b, int offX, int offY, int prio);   // FUN_004ab0d0
    void selectGroup(int g);                     // FUN_004a8b50
    void draw();                                 // FUN_004a81a0
    void voice(int unit, int category, int extra);   // FUN_004d48a0 -> FUN_004d47b0

    // Groups and the waiting lists (piece 2).
    void buildWaitingLists();                    // FUN_004a5f40
    void drawWaiting();                          // FUN_004a6460
    void drawGroups();                           // FUN_004a5440
    void drawAffinity();
    void sortWaiting(GameWork& w, int mode, int list);   // FUN_004a8bf0

    // Adventures and lectures (piece 3).
    void statRanks();                            // FUN_004a9ab0
    void addWeekAdventures();                    // FUN_004a17b0 / FUN_004a9d90
    void learnLectures();                        // FUN_004a2ba0 / FUN_004a24a0
    void buildAdventureLists();                  // FUN_004a1c70
    void buildLectureList(int hermit);           // FUN_004a2980
    void drawAdventurePanel();                   // FUN_004a1ff0
    void drawLecturePanel();                     // FUN_004a2da0
    void drawAdventureItem(int x, int y, int type, int sub, bool title, int remaining,
                           std::uint8_t alpha, int prio);                 // FUN_004aa7e0
    void drawLectureItem(int x, int y, int level, int sub, std::uint8_t alpha, int prio);   // FUN_004aa6c0
    void drawCell(int rectIndex, int prio, int x, int y, int sx, int sy, int w, int h, std::uint8_t alpha);
    void commitAdventure();                      // FUN_004a1920
    void writeSortie();                          // FUN_004a6a10
    void drawPopup();                            // FUN_004a3240 (piece 4)                         // FUN_004a81a0's AnmHan loop
    void drag();                                 // FUN_004a4680, state 6
    void openPopup(int unit, int group, int boxX, int boxY, int hitX, int hitY);
    void drawPortrait(int x, int y, int kind, int index, std::uint8_t alpha, int prio);   // FUN_004aadb0
    void drawName(int x, int y, int kind, int index, std::uint8_t alpha, int prio);       // FUN_004aaf60
    void drawJobIcon(int x, int y, int unit, std::uint8_t alpha, int prio);               // FUN_004aacf0

    [[nodiscard]] ysub::Rect rect(int i) const;  // DAT_0061b208 + 16 i
    struct Button { std::int16_t hit, hitX, hitY, rect, x, y, flashX, flashY; std::uint8_t mode, enabled; };
    [[nodiscard]] Button button(int b) const;    // DAT_0061b628 + 0x12 b

    GameContext&    ctx_;
    ysub::TextList  tacticsText_, lectureText_, dialog_;   // TacticsExp / LectureExp / 5-line dialog
    ysub::Flashes   flashes_;
    AnmFile         anmFile_;                    // +0x34
    AnmPlayer       anm_;
    std::array<AnmPlayer::NodeId, 5> affinityAnm_{AnmPlayer::kNone, AnmPlayer::kNone, AnmPlayer::kNone,
                                                  AnmPlayer::kNone, AnmPlayer::kNone};   // +0x88 + 0x58 g
    std::array<std::int16_t, 5> affinityShown_{-1, -1, -1, -1, -1};                      // +0x2c0

    std::int16_t exit_ = 0;         // +0x76bc
    bool         again_ = true;     // +0x76c0
    std::uint8_t cursor_ = 0;       // +0x76c2: 0 arrow, 1/2 other shapes
    bool         running_ = false;  // +0x76c4
    std::int16_t state_ = 2;        // +0x76c6
    std::int16_t fade_ = 0;         // +0x76c8
    std::int16_t command_ = 0;      // +0x7950
    bool         dialogBuilt_ = false;   // +0x1294
    bool         commitPending_ = false; // +0x30
    std::int16_t adventureEntry_ = -1;   // +0x128e: TacticsExp entry shown
    std::int16_t lectureEntry_ = -1;     // +0x1292: LectureExp entry shown
    std::array<std::array<std::int16_t, 8>, 0x28> statRank_{};   // +0x76ca: rank of each member per stat (7 + total)
};

} // namespace ch
