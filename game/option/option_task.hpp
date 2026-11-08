// ============================================================================
// option_task.hpp — OptionTask, scene 3 (src\YOSHIDA\OptionTask.cpp)
//
// Three ten-step volume rows (BGM, SE, voice) and a back button over the
// title background; the levels go to the option block and Option.bin. Under
// them the port adds fullscreen, infinite unit time, the language and, in the
// TW build, the title's Tutorial item (options::PortOptions, options::Languages).
//   FUN_0044d560  Start: FUN_0044d660 bank 0x5a, FUN_0044d730 loop, then
//                 FUN_004e3070 writes Option.bin
//   FUN_0044d810  fade in / input (FUN_0044dad0) / fade out
//   FUN_0044d970  draw: FUN_0044dd90 panel and knobs, FUN_0044dfd0
//                 background, FUN_00410b10 fade, FUN_0044e080 cursor
// ============================================================================
#pragma once
#include "../game_context.hpp"

#include <vector>
#include <array>
#include <cstdint>

namespace ch {

class OptionTask final : public Task {
public:
    explicit OptionTask(GameContext& ctx) : ctx_(ctx) {}
    ~OptionTask() override;                      // FUN_0044d440 (FUN_0044d520 deletes)
    Co<> start(std::uint32_t) override;

private:
    static constexpr std::uint16_t kBank = 0x5a;   // data\Tactics\Option.bin

    bool input();                                  // FUN_0044dad0: true on the back button
    void draw();                                   // FUN_0044d970
    [[nodiscard]] int hover(std::uint32_t table, int count) const;   // FUN_0044d9d0
    // The port's panel under the original's.
    void portInput();
    void drawPortPanel();
    void storeLevels();                            // FUN_0044d5b0
    [[nodiscard]] int languageRowY() const;
    [[nodiscard]] int listHover() const;           // the list's row under the mouse, or -1
    void drawLanguageList();
    [[nodiscard]] std::vector<int> portItems() const;   // the panel's PortItem per row
    [[nodiscard]] int portRows() const;
    [[nodiscard]] int portHover() const;           // a PortItem under the mouse, or -1

    GameContext& ctx_;
    std::array<int, 4> hover_{-1, -1, -1, -1};     // +0x38: back, BGM, SE, voice cell under a held button
    std::array<int, 3> level_{};                   // +0x48: BGM, SE, voice
    // Screen fade (FUN_00410930 / FUN_00410a60): alpha << 16 in 8.16.
    std::int32_t fade_ = 0xff0000, fadeStep_ = 0;
    int          fadeLeft_ = 0;
    bool         listOpen_ = false, listClosing_ = false;   // the language list
    bool         chapters_ = false;                         // leaving for the chapter select
};

} // namespace ch
