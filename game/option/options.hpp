// ============================================================================
// options.hpp — the option block DAT_00809320 (0x28 bytes, Option.bin)
//
//   +0x1c BGM, +0x20 SE, +0x24 voice volume levels 0..9 (defaults 6, 4, 9);
//   +0x0c..+0x18 are 1 by default and not read by the option screen.
//   FUN_004e2c60  reset to defaults (with the boot work reset)
//   FUN_004e2f50  read Option.bin (kept only if 0x28 bytes, levels 0..9)
//   FUN_004e2d00  apply the volumes
//   FUN_004e3070  write Option.bin
// ============================================================================
#pragma once
#include "../audio.hpp"
#include "../game_context.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ch::options {

inline constexpr std::uint32_t kBlock = 0x809320, kSize = 0x28;
inline constexpr std::uint32_t kBgm = 0x80933c, kSe = 0x809340, kVoice = 0x809344;

void reset(GameWork& w);
void load(GameWork& w);
void apply(GameWork& w, IAudio& audio, GameVersion v);
void save(const GameWork& w);

// The port's own settings, in their own file (Option.bin keeps the original's layout).
struct PortOptions {
    bool fullscreen = false;
    bool infiniteUnitTime = false;   // single player: a unit's time on the field does not run out
    bool twTutorial = false;         // Traditional Chinese: the JP title's Tutorial item (ch.exe has none)
    bool instantWin = false;         // single player: Confirm (Enter / Z) wins a battle at once (rank 2)
    int windowW = 1024, windowH = 768;   // the window when not fullscreen (desktop builds; the web page sizes its own)
    legacy::ScaleMode scale = legacy::ScaleMode::Smooth;   // how the 1024x768 frame fills the window or screen
    bool modernUi = false;           // UI style: false = Classic (the original's), true = New (touch friendly)
};
// "smooth", "sharp", "integer", "stretch": the file's spelling of a ScaleMode.
[[nodiscard]] const char* scaleName(legacy::ScaleMode m);
bool parseScale(std::string_view name, legacy::ScaleMode& out);
[[nodiscard]] PortOptions& port();
void loadPort();
void savePort();
// What the option screen can switch to; main.cpp fills this in.
struct Language { std::string id, name; };
struct Languages {
    std::vector<Language> all;
    std::string current;
    std::function<void(const std::string& id)> switchTo;   // restarts the game in it, on the option screen
};
[[nodiscard]] Languages& languages();
// FUN_004e2ec0 / FUN_004e2ef0 / FUN_004e2f20: millibels for a level.
[[nodiscard]] std::int32_t bgmVolume(int level, GameVersion v);
[[nodiscard]] std::int32_t seVolume(int level, GameVersion v);
[[nodiscard]] std::int32_t voiceVolume(int level, GameVersion v);

} // namespace ch::options

namespace ch::ui {
// The UI style the player picked (Option screen): the new, touch-friendly one,
// or the original's. Display only: input must still turn into the same game
// commands either way, as network play is lockstep.
[[nodiscard]] inline bool modern() { return options::port().modernUi; }
} // namespace ch::ui
