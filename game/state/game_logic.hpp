// ============================================================================
// game_logic.hpp — calendar, roster and unit bookkeeping on GameWork
// (src\yoshimaru\ysub.cpp, src\common)
//
//   FUN_004e1b60 / 4e1be0 / 49e930  newGame         New Game work reset
//   FUN_004d3510                    advanceWeek     every WeekStart
//   FUN_004d3e90 / FUN_004d4250     joinRoster / leaveRoster (opcodes 0x90 / 0x91)
//   FUN_004d46e0                    inRoster
//   FUN_004d3aa0                    openJobs        jobs a unit qualifies for
//   FUN_004d58e0                    unitSummary     stat exp + equipment -> rank
//   FUN_004d56a0                    statProgress    exp -> level progress
//   FUN_004e3170 / FUN_004e31c0     setSeen / clearSeen (0x7d4f3c flags)
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "game_work.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ch::logic {

// Read-only tables from ch.exe .rdata/.data, addressed by ch.exe VA
// (exe::logicData: the imported static/ch_data.bin).
std::uint8_t  sdU8(std::uint32_t va, GameVersion v = GameVersion::Jp);
std::int16_t  sdS16(std::uint32_t va, GameVersion v = GameVersion::Jp);
std::int32_t  sdS32(std::uint32_t va, GameVersion v = GameVersion::Jp);
std::span<const std::uint8_t> sdBytes(std::uint32_t va, std::size_t n, GameVersion v = GameVersion::Jp);

// FUN_004d1cb0: the CRT rand() (MSVC LCG, 0..0x7fff, seed 1).
[[nodiscard]] int rand();
// FUN_004d1c60: srand(time(0)).
void srand(std::uint32_t seed);
// FUN_004d45c0: slot of `id` in the member (id < 0x65) or guest list, or -1.
[[nodiscard]] int rosterSlot(const GameWork& w, int id);
// FUN_004d3a20: affinity of unit a towards b (0x7d3d70 table, 0x44 stride).
[[nodiscard]] int affinity(const GameWork& w, int a, int b);
// FUN_004d3960: change a's affinity towards b by delta, kept in 1..100.
void adjustAffinity(GameWork& w, int a, int b, int delta);

// FUN_004d48a0: pick a unit voice line for an event category (0..0xb),
// or -1. Units 1..12 and 0x23 have lines; `extra` picks a variant in
// some categories (affinity grade on drop, etc.). Index into unitVoicePath.
[[nodiscard]] int unitVoiceLine(int unit, int category, int extra);
// FUN_004d47b0: PTR_s_data_sound_voice_v901_v901_001_0_00624adc[index].
[[nodiscard]] std::string unitVoicePath(int index, GameVersion v);

void newGame(GameWork& w, GameVersion v);
void advanceWeek(GameWork& w, GameVersion v);
void refreshEquipment(GameWork& w);              // FUN_0049e8f0: FUN_004d31f0 + FUN_004d34a0
// Save data (the .SAV format of the original, 0x306d4 bytes).
[[nodiscard]] std::vector<std::uint8_t> saveGame(GameWork& w);
[[nodiscard]] bool saveValid(std::span<const std::uint8_t> bytes);
// saveValid, then what a save from outside the game must also get right: the
// roster lists and the date in range. Says what was wrong.
enum class SaveProblem { None, Size, Format, Checksum, Contents };
[[nodiscard]] SaveProblem saveCheck(std::span<const std::uint8_t> bytes);
bool loadGame(GameWork& w, std::span<const std::uint8_t> bytes);
[[nodiscard]] bool inRoster(const GameWork& w, int id);
void joinRoster(GameWork& w, GameVersion v, int id, int party, int slot);
void leaveRoster(GameWork& w, int id);
void openJobs(GameWork& w, int unit);
void setSeen(GameWork& w, int i);
[[nodiscard]] bool seen(const GameWork& w, int i);   // FUN_004e3210
void clearSeen(GameWork& w, int i);

struct StatProgress {          // FUN_004d56a0 result, 8 ints in this order
    std::int32_t levelStart = 0;   // exp at the start of the level
    std::int32_t levelEnd = 0;     // exp at the next level
    std::int32_t levelSpan = 0;
    std::int32_t toNext = 0;
    std::int32_t intoLevel = 0;    // exp past levelStart
    std::int32_t capTotal = 0;     // exp of the level cap (100, or 0x87 on hard)
    std::int32_t toMax = 0;        // capTotal - exp
    std::int32_t percent = 0;      // 0..100 through the level
};
[[nodiscard]] StatProgress statProgress(const GameWork& w, int level, std::int32_t exp);

struct UnitSummary {           // FUN_004d58e0 result
    std::int32_t statExp = 0;      // sum of the 7 stats' exp
    std::int32_t equipValue = 0;   // FUN_004d5850
    std::int32_t total = 0;
    std::int32_t toCap = 0;        // DAT_006253c4 - total, >= 0
    std::uint8_t rank = 0;         // FUN_004d5590
    std::int32_t score = 0;        // unit progress +0x11c
    std::array<StatProgress, 7> stats{};
};
[[nodiscard]] UnitSummary unitSummary(const GameWork& w, GameVersion v, int unit);
// FUN_004d5590: rank (1..50) reached with summed exp + equipment value.
[[nodiscard]] int rankForTotal(std::int32_t total, GameVersion v);

// FUN_004d5600: stat level reached with `exp` (exp curve DAT_006e4308), 1..0x87.
[[nodiscard]] int expToLevel(std::int32_t exp, GameVersion v);
// FUN_004d3600: exp lecture `lecture` (0x74bed0 record) would give `unit`
// in each of the 7 stats, plus [7] the overflow pool, at group affinity
// `affinity`. Gains are capped by the rank cap and each stat's level cap.
[[nodiscard]] std::array<std::int32_t, 8> lectureGain(const GameWork& w, GameVersion v, int unit, int lecture, int affinity);

// The story's values, typed 0 s32, 1 s16, 2 u8, 3 bits (most significant
// bit first):
//   the global ones at 0x7f7c18 (s32 x64, s16 x128 at +0x100, u8 x256 at
//   +0x200, bits x2048 at +0x300): FUN_004e1da0 reads (u8 signed),
//   FUN_004e1fa0 writes;
//   each map's at 0x7f8020 + map * 0xb0 (s32 x10, s16 x20 at +0x28, u8 x40
//   at +0x50, bits x320 at +0x78): FUN_004e21f0 reads, FUN_004e2480
//   writes (FUN_004e2440 / map -1: the current map, DAT_007f4488).
// Out-of-range types or indices read 0 and write nothing (the original
// asserts).
[[nodiscard]] std::int32_t globalValue(const GameWork& w, int type, std::uint32_t index);
void setGlobalValue(GameWork& w, int type, std::uint32_t index, std::int32_t value);
[[nodiscard]] std::int32_t mapValue(const GameWork& w, int map, int type, std::uint32_t index);
void setMapValue(GameWork& w, int map, int type, std::uint32_t index, std::int32_t value);
// FUN_004e2760 / FUN_004e29e0: the battle's own block at 0x809348 (the map
// argument is not used), laid out as a map's.
[[nodiscard]] std::int32_t battleValue(const GameWork& w, int type, std::uint32_t index);
void setBattleValue(GameWork& w, int type, std::uint32_t index, std::int32_t value);
// FUN_004e1a10 (from FUN_0049e1b0 at boot): the story's values, the units,
// the party records, the stage block and the player's name ("PlayerName")
// cleared. The options' reset it runs (FUN_004e1ad0 -> FUN_004e2c60) is
// options::reset.
void bootReset(GameWork& w);

// The rounds of chained stages (comscenario.cpp). FUN_004daae0: DAT_007f4504.
[[nodiscard]] int roundFlag(const GameWork& w);
// FUN_004dab50, after each stage of a chain: each party member's battle
// tallies (+0xa8, +0xaa, +0xac s16, +0xae s8) add into the round's
// (0x8093fa, 8 bytes a member, kept to 0x7fff / 0x7f), the rank's points
// (1: 10, 2: 7, 3: 5, 4: 1, else 0) into 0x80968a, the best "all
// enemies gone" (0x7f4510) into 0x80968e, the kills' worth (0x7f4512)
// into 0x809690, the stage's bases into 0x80967a's flags, one more stage
// (0x8093f8).
void addRoundStage(GameWork& w);
// FUN_004dae70, when the chain ends: the totals back into the party
// records, the rank from the average points (10+ 1, 7+ 2, 5+ 3, 1+ 4,
// else 5) and the round's other marks into the stage's results.
void finishRound(GameWork& w);

} // namespace ch::logic
