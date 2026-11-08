// ============================================================================
// workroom_tables.hpp — WorkRoom tables
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "../ysub/ysub.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ch::workroom {

// ch.exe 0x61b050 (the JP build's copy at 0x50b130 is the same): 26 rows of
// bank, frame, src x, y, w, h, default x, y.
inline ysub::Rect rectRow(GameVersion v, std::size_t i) { return ysub::rectAt(0x61b050 + std::uint32_t(i) * 16, v); }

// FUN_004a0ad0: map locations. Hit rect, label position, pin position.
struct Spot { std::int16_t x, y, w, h, labelX, labelY, pinX, pinY; };
constexpr std::array<Spot, 26> kSpots{{
    {361, 100, 112, 16, 324, 80, 372, 100},
    {392, 144, 123, 15, 353, 123, 401, 146},
    {453, 189, 131, 16, 409, 168, 464, 189},
    {312, 221, 152, 21, 288, 208, 322, 223},
    {494, 297, 130, 20, 458, 282, 509, 300},
    {321, 328, 94, 16, 275, 309, 331, 328},
    {430, 375, 160, 16, 411, 356, 437, 378},
    {507, 401, 63, 16, 446, 384, 518, 406},
    {324, 414, 70, 16, 275, 396, 332, 419},
    {506, 441, 129, 16, 467, 423, 515, 441},
    {418, 473, 156, 20, 392, 457, 430, 474},
    {569, 131, 158, 22, 553, 118, 582, 137},
    {617, 231, 136, 18, 583, 212, 631, 236},
    {710, 315, 75, 16, 652, 295, 720, 314},
    {626, 349, 90, 16, 578, 333, 634, 353},
    {681, 380, 151, 20, 661, 368, 694, 384},
    {615, 419, 86, 16, 559, 399, 630, 421},
    {612, 504, 132, 17, 575, 487, 624, 505},
    {796, 128, 110, 16, 753, 109, 806, 129},
    {713, 182, 178, 16, 704, 164, 725, 183},
    {834, 212, 146, 17, 804, 196, 842, 214},
    {816, 271, 117, 16, 776, 254, 829, 274},
    {853, 313, 127, 16, 812, 295, 864, 316},
    {803, 359, 130, 16, 770, 342, 815, 365},
    {850, 415, 134, 16, 816, 396, 862, 420},
    {775, 447, 111, 16, 727, 429, 785, 448},
}};

// News titles (rows 1..), shown once their month/week has come: ch.exe
// 0x750a40 / JP 0x66a928, 8-byte rows {id, month, week, 0, char*}, month -1 ends.
struct News { std::int8_t month, week; std::string title; };
const std::vector<News>& news(GameVersion v);

} // namespace ch::workroom
