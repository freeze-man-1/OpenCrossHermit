// ============================================================================
// options.cpp
// ============================================================================
#include "options.hpp"
#include "../../engine/engine_core.hpp"
#include "../state/game_logic.hpp"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <span>
#include <string>

namespace ch::options {

namespace {
constexpr std::string_view kFile = "Option.bin", kPortFile = "port_options.txt";
std::int32_t level(const GameWork& w, std::uint32_t at) { return w.s32(at) % 10; }
} // namespace

// FUN_004e2c60 (also run by FUN_004e1ad0 at boot).
void reset(GameWork& w) {
    w.fill(kBlock, kSize, 0);
    for (std::uint32_t k = 3; k < 7; ++k) w.set32(kBlock + k * 4, 1);
    w.set32(kBgm, 6);
    w.set32(kSe, 4);
    w.set32(kVoice, 9);
}

void load(GameWork& w) {
    const auto bytes = legacy::vfs::readSave(kFile);
    if (!bytes || bytes->size() != kSize) return;
    const auto at = [&](std::size_t o) { std::int32_t v; std::memcpy(&v, bytes->data() + o, 4); return v; };
    for (std::size_t o : {0x1cu, 0x20u, 0x24u})
        if (at(o) < 0 || at(o) > 9) return;
    for (std::uint32_t i = 0; i < kSize; ++i) w.set8(kBlock + i, (*bytes)[i]);
}

PortOptions& port() {
    static PortOptions p;
    return p;
}

const char* scaleName(legacy::ScaleMode m) {
    switch (m) {
        case legacy::ScaleMode::Sharp:   return "sharp";
        case legacy::ScaleMode::Integer: return "integer";
        case legacy::ScaleMode::Stretch: return "stretch";
        default:                         return "smooth";
    }
}

bool parseScale(std::string_view name, legacy::ScaleMode& out) {
    for (int i = 0; i < int(legacy::ScaleMode::Count); ++i)
        if (name == scaleName(legacy::ScaleMode(i))) {
            out = legacy::ScaleMode(i);
            return true;
        }
    return false;
}

void loadPort() {
    const auto bytes = legacy::vfs::readSave(kPortFile);
    if (!bytes) return;
    const std::string s(bytes->begin(), bytes->end());
    port().fullscreen = s.find("fullscreen=1") != std::string::npos;
    port().infiniteUnitTime = s.find("infinite_unit_time=1") != std::string::npos;
    port().twTutorial = s.find("tw_tutorial=1") != std::string::npos;
    port().instantWin = s.find("instant_win=1") != std::string::npos;
    // The value after "key=", up to the line's end.
    const auto value = [&](std::string_view key) -> std::string {
        const std::size_t at = s.find(std::string("\n") + std::string(key) + "=");
        const std::size_t from = at != std::string::npos ? at + key.size() + 2
                               : s.starts_with(std::string(key) + "=") ? key.size() + 1 : std::string::npos;
        if (from == std::string::npos) return {};
        return s.substr(from, s.find('\n', from) - from);
    };
    if (int w = 0, h = 0; std::sscanf(value("window").c_str(), "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240 &&
                          w <= 16384 && h <= 16384) {
        port().windowW = w;
        port().windowH = h;
    }
    parseScale(value("scale"), port().scale);
    port().modernUi = value("ui_style") == "new";
}

void savePort() {
    const std::string s = std::string("fullscreen=") + (port().fullscreen ? "1" : "0") + "\ninfinite_unit_time=" +
                          (port().infiniteUnitTime ? "1" : "0") + "\ntw_tutorial=" + (port().twTutorial ? "1" : "0") +
                          "\ninstant_win=" + (port().instantWin ? "1" : "0") + "\nwindow=" + std::to_string(port().windowW) +
                          "x" + std::to_string(port().windowH) + "\nscale=" + scaleName(port().scale) +
                          "\nui_style=" + (port().modernUi ? "new" : "classic") + "\n";
    try {
        legacy::vfs::writeSave(kPortFile, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
    } catch (const std::exception& ex) {
        SDL_Log("[option] %s", ex.what());
    }
}

Languages& languages() {
    static Languages l;
    return l;
}

std::int32_t bgmVolume(int lv, GameVersion v) { return logic::sdS32(0x738c18 + std::uint32_t(lv) * 4, v); }
std::int32_t seVolume(int lv, GameVersion v) { return logic::sdS32(0x738c40 + std::uint32_t(lv) * 4, v); }
std::int32_t voiceVolume(int lv, GameVersion v) { return logic::sdS32(0x738c68 + std::uint32_t(lv) * 4, v); }

// FUN_004e2d00: each level kept to 0..9, then FUN_004e2da0 the music
// (FUN_004e2ec0's table), FUN_004e2e00 the sounds (FUN_004e2ef0) and
// FUN_004e2e60 the voices (FUN_004e2f20), each once the system exists
// (DAT_007a49fc).
void apply(GameWork& w, IAudio& audio, GameVersion v) {
    for (std::uint32_t at : {kBgm, kSe, kVoice}) w.set32(at, level(w, at));
    audio.setVolume(IAudio::Channel::Bgm, bgmVolume(w.s32(kBgm), v));
    audio.setVolume(IAudio::Channel::Se, seVolume(w.s32(kSe), v));
    audio.setVolume(IAudio::Channel::Voice, voiceVolume(w.s32(kVoice), v));
}

void save(const GameWork& w) {
    std::vector<std::uint8_t> bytes(kSize);
    for (std::uint32_t i = 0; i < kSize; ++i) bytes[i] = w.u8(kBlock + i);
    try {
        legacy::vfs::writeSave(kFile, bytes);
    } catch (const std::exception& ex) {
        SDL_Log("[option] %s", ex.what());
    }
}

} // namespace ch::options
