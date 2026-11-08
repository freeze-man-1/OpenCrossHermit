// ============================================================================
// workroom_task.hpp — WorkRoomTask, scene 8 (src\yoshimaru\workroom)
//
// The weekly hub between CH001 and the Han/Kojin events.
//   FUN_004a1570  Start: state loop, then CH002 (first visit of the week) or scene 9
//   FUN_0049f6c0  init: banks, BGM, YsText lists, news list, pin animation
//   FUN_0049fd50  state 1, top screen: date plates and two buttons
//   FUN_004a0760  state 2, map screen: 26 places, news list, text viewer
//   FUN_004a01a0  map screen draw/input, FUN_004a0ad0 place hotspots
//
// Work area (0x14580 bytes): +0x30 state, +0x34.. hover/selection,
// +0x17c / +0x65a0 / +0xc9c4 three TextLists, +0x12de8 Flashes,
// +0x13dac sorted news {date, row}, +0x1457c news count.
// ============================================================================
#pragma once
#include "../anm/anm_player.hpp"
#include "../game_context.hpp"
#include "../ysub/ysub.hpp"

#include <cstdint>
#include <vector>

namespace ch {

class WorkRoomTask final : public Task {
public:
    explicit WorkRoomTask(GameContext& ctx) : ctx_(ctx) {}
    ~WorkRoomTask() override;                   // FUN_004a1430 (FUN_004a1530 deletes)
    Co<> start(std::uint32_t) override;         // FUN_004a1570

private:
    static constexpr std::uint16_t kBank = 10;      // data\WorkRoom\WorkRoom.bin
    static constexpr std::uint16_t kBgBank = 0xb;   // data\adv\bin\ + background of the day
    static constexpr std::uint16_t kAnmBank = 0x10; // data\AllResult\AnmAllResult.bin

    void init();                                // FUN_0049f6c0
    Co<int> topScreen();                        // FUN_0049fd50
    void drawTop();                             // FUN_0049faa0
    Co<int> mapScreen();                        // FUN_004a0760
    void drawMap();                             // FUN_004a01a0
    void pickSpot();                            // FUN_004a0ad0
    void drawCursorAndFlashes();

    GameContext&    ctx_;
    ysub::TextList  placeText_, newsText_, newsTitles_;   // +0x17c, +0x65a0, +0xc9c4
    ysub::Flashes   flashes_;                             // +0x12de8
    std::vector<std::int16_t> newsRow_;         // +0x13dae: table row of each listed title
    AnmFile         anmFile_;                   // +0x50
    AnmPlayer       anm_;
    AnmPlayer::NodeId anmRoot_ = AnmPlayer::kNone;   // +0xa4

    std::int16_t hoverSpot_ = -1, hoverX_ = 0, hoverY_ = 0;          // +0x34..+0x38
    std::int16_t viewer_ = 0;                                        // +0x3a: 0 place text, 1 news text
    std::int16_t pickedSpot_ = -1, pickedX_ = 0, pickedY_ = 0;       // +0x3c..+0x40
    std::int16_t pinX_ = 0, pinY_ = 0;                               // +0x42, +0x44
    std::int16_t pickedNews_ = -1;                                   // +0x46
};

} // namespace ch
