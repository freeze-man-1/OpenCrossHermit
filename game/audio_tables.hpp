// ============================================================================
// audio_tables.hpp — path tables extracted from CH.exe .data
// ============================================================================
#pragma once
#include <array>
#include <string_view>

namespace ch {

// VA 0x006b2cf4, indexed by SysBgmPlay track (1..34). Empty = no file.
inline constexpr std::array<std::string_view, 35> kBgmPaths = {
    "",
    "data\\sound\\bgm\\M-001.mp3", "data\\sound\\bgm\\M-002.mp3", "data\\sound\\bgm\\M-003.mp3",
    "data\\sound\\bgm\\M-004.mp3", "data\\sound\\bgm\\M-005.mp3", "data\\sound\\bgm\\M-006.mp3",
    "data\\sound\\bgm\\M-007.mp3", "data\\sound\\bgm\\M-008.mp3", "data\\sound\\bgm\\M-009.mp3",
    "data\\sound\\bgm\\M-010.mp3", "data\\sound\\bgm\\M-011.mp3", "data\\sound\\bgm\\M-012.mp3",
    "data\\sound\\bgm\\M-013.mp3", "",                            "data\\sound\\bgm\\M-015.mp3",
    "data\\sound\\bgm\\M-016.mp3", "data\\sound\\bgm\\M-017.mp3", "",
    "data\\sound\\bgm\\M-019.mp3", "data\\sound\\bgm\\M-020.mp3", "data\\sound\\bgm\\M-021.mp3",
    "data\\sound\\bgm\\M-022.mp3", "data\\sound\\bgm\\PH001.mp3", "data\\sound\\bgm\\PH002.mp3",
    "data\\sound\\bgm\\PH003.mp3", "data\\sound\\bgm\\PH004.mp3", "data\\sound\\bgm\\PH005.mp3",
    "data\\sound\\bgm\\PH006.mp3", "data\\sound\\bgm\\PH007.mp3", "data\\sound\\bgm\\PH008.mp3",
    "data\\sound\\bgm\\Opening.mp3", "data\\sound\\bgm\\TitleSE.mp3", "data\\sound\\bgm\\Ending.mp3",
    "data\\sound\\bgm\\SE-001.mp3",
};

// VA 0x0061b010, loaded at boot by FUN_0040c780; index = SysSePlay id.
inline constexpr std::array<std::string_view, 12> kSystemSePaths = {
    "data\\sound\\plw\\Common\\E_SE04.WAV",   "data\\sound\\plw\\Common\\OFF.wav",
    "data\\sound\\plw\\Common\\Up.wav",       "data\\sound\\plw\\Common\\Down.wav",
    "data\\sound\\plw\\Common\\Switch.wav",   "data\\sound\\plw\\Common\\on.wav",
    "data\\sound\\plw\\Common\\E_SE08_2.WAV", "data\\sound\\plw\\Common\\Pop0.WAV",
    "data\\sound\\plw\\Common\\Pop1.WAV",     "data\\sound\\plw\\Common\\kin.wav",
    "data\\sound\\plw\\Common\\nta0.WAV",     "data\\sound\\plw\\Common\\nta1.WAV",
};

} // namespace ch
