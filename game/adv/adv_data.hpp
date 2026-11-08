// ============================================================================
// adv_data.hpp — the ADV scenes' tables, read from the executables
// ============================================================================
// Layouts by ch.exe address (the logic data, exe_image.hpp); names and file
// lists from the running version's own exe.
#pragma once
#include "../game_context.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ch::adv_tables {

// DAT_00624520: window, x, y, faceOn, faceKind, faceX, faceY, textX, textY, cursorX, cursorY,
// iconOn, iconX, iconY, panelX, panelY, panelW, panelH, frameType
using Speaker = std::array<std::int16_t, 19>;
// PTR_DAT_00624834: per frame type {dx, dy, srcX, srcY, w, h}
struct FramePart { std::int16_t dx, dy, sx, sy, w, h; };
// PTR_s_sc000_bin_00622668: 60-byte records, name, offX, offY, partW, partH,
// then 8 x {frame, srcX, srcY}.
struct ScFace { std::string file; std::int16_t offX, offY, partW, partH; std::array<std::array<std::int16_t, 3>, 8> expr; };
// PTR_s_mc000_bin_00622250: 8-byte records, name, x, y.
struct McFace { std::string file; std::int16_t x, y; };

struct Layout {
    std::vector<Speaker> speakers;
    std::vector<std::vector<FramePart>> frames;
    std::vector<ScFace> scFaces;
    std::vector<McFace> mcFaces;
};
const Layout& layout(GameVersion v);

// Speaker names by face id (FUN_004ca770): ch.exe PTR_DAT_00621f2c / JP 0x530ce8.
const std::vector<std::string>& names(GameVersion v);

} // namespace ch::adv_tables

namespace ch::adv_media {

// data\adv\bin\ + name (FUN_004c6e50): ch.exe 0x621bbc / JP 0x52ff64.
const std::vector<std::string>& bgNames(GameVersion v);
// data\sound\voice\ + the voice file of a group (FUN_004c45c0): ch.exe
// 0x621988 / JP 0x512140, 141 groups pointing into one flat list; reads
// past a group run on into the next, as in the original. "" past the end.
std::string voice(GameVersion v, int group, int index);

} // namespace ch::adv_media
