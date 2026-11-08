// ============================================================================
// net_game.hpp — the network game's data and its hand-over to the battle
//
//   - The map table (DAT_005ffcb0, 128 x 8 bytes, FUN_0044c200): versus
//     entries 0..63 (11 real: stages 72, 71, 101..109), co-operative entries
//     64..127 (10 real: story stages), each with its texts (PTR_DAT_006001b0,
//     0x51 strings: the title, 4 x 0x14 lines of objectives per side) and
//     the most units a player may bring (+7).
//   - The roster a player picks from: the eight ready-made units
//     (DAT_00605f18, FUN_0044a960) until a save is loaded, then that save's
//     party (FUN_0044a6e0 -> FUN_004b9340).
//   - The room's settings (the host's) and each player's choice travel as
//     the relay's room options and player data (encodeOptions / encodePick).
//   - FUN_00448a30: once the game starts every client writes the same battle
//     work from them: the stage, every player's units (+0xa4 = the owner's
//     slot), the sides (DAT_007f44e4), the start cells (DAT_007f44f4), the
//     co-operative flag (DAT_007f4504) and our own slot (DAT_007f4490).
// ============================================================================
#pragma once
#include "../game_context.hpp"

#include <array>
#include <string>
#include <vector>

namespace ch::netgame {

inline constexpr int kMapEntries = 128;
inline constexpr int kCoopBase = 64;          // the co-operative half of the table
inline constexpr int kRosterMax = 30;         // FUN_0044ac50: 0x1e roster places
inline constexpr std::size_t kRecord = 0xb0;   // one battle unit record (0x7f4518 + i * 0xb0)

struct MapEntry {
    bool coop = false;
    int stage = 0;          // the stage (DAT_007f4488)
    int text = 0;           // its texts (PTR_DAT_006001b0 + text * 0x51)
    int maxUnits = 0;       // the most units one player may bring (+7)
};
[[nodiscard]] MapEntry mapEntry(int index, GameVersion v);               // FUN_0044c200
// The entries of one half (versus or co-operative) the room may choose (FUN_0044bd70).
[[nodiscard]] std::vector<int> mapList(bool coop, const GameWork& w, GameVersion v);
[[nodiscard]] std::string mapTitle(int index, GameVersion v);            // FUN_0044c2c0
// A string of the exe's data in its build's code page (Big5 / Shift-JIS), as UTF-8.
[[nodiscard]] std::string exeText(std::uint32_t va, GameVersion v);
// Line `line` (0..0x13) of the objectives shown for side `side` (0..3).
[[nodiscard]] std::string mapLine(int index, int side, int line, GameVersion v);   // FUN_0044c390

using Record = std::array<std::uint8_t, kRecord>;
[[nodiscard]] std::vector<Record> defaultRoster(GameVersion v);          // FUN_0044a960
[[nodiscard]] std::vector<Record> saveRoster(const GameWork& w, GameVersion v);   // FUN_0044a6e0

// The host's settings: the map entry and each slot's side (0..3; 4 random).
struct Options { int map = 0; std::array<std::uint8_t, 4> side{0, 1, 2, 3}; };
[[nodiscard]] std::vector<std::uint8_t> encodeOptions(const Options& o);
[[nodiscard]] bool decodeOptions(const std::vector<std::uint8_t>& b, Options& o);
// A player's choice: the units they bring.
[[nodiscard]] std::vector<std::uint8_t> encodePick(const std::vector<Record>& units);
[[nodiscard]] std::vector<Record> decodePick(const std::vector<std::uint8_t>& b);

// FUN_00448a30 (+ FUN_0044baa0's random sides from the relay's seed): the
// battle work for the room the relay has just started. False if a setting
// is unusable (the game should not go to the battle).
bool writeBattleWork(GameContext& ctx, const net::Session& s);

} // namespace ch::netgame
