// ============================================================================
// audio.hpp — system BGM / SE bridge (src/COMMON SysBgm* wrappers)
//
//   FUN_004db060  SysBgmPlay(track 1..34, flags) -> bgmPlay
//   FUN_004db230  BgmFadeOut(frames)             -> bgmFadeOut
//   FUN_004db270  BgmFadeFinished()              -> bgmFadeFinished
//   FUN_0041e3d0  BgmIsRunning()                 -> bgmIsPlaying
//   FUN_0041e500  BgmStop()                      -> bgmStop
//   FUN_004db2b0  SysSePlay(id)                  -> sePlay
//   FUN_004db300 / FUN_0040d610  SeStopAll()     -> seStopAll
//   FUN_0041e500 + FUN_0041e190 on the voice stream (+0x8f00), not looped
//                                                -> voicePlay / voiceStop
// ============================================================================
#pragma once
#include <SDL.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace ch {

class IAudio {
public:
    virtual ~IAudio() = default;
    virtual void loadSystemSe() = 0;   // FUN_0040c780 at boot
    virtual void bgmPlay(std::uint32_t track, std::uint32_t flags) = 0;
    virtual void bgmFadeOut(std::uint32_t frames) = 0;
    [[nodiscard]] virtual bool bgmFadeFinished() const = 0;
    [[nodiscard]] virtual bool bgmIsPlaying() const = 0;
    virtual void bgmStop() = 0;
    virtual void sePlay(std::uint32_t id) = 0;
    virtual void seStopAll() = 0;
    // FUN_0040c780(group, list): load a numbered SE list into group 1..19
    // (group 0 is the system list). FUN_0040d390(group, index, slot): play;
    // slot 0..0x3f first stops what that slot played last, -1 = no slot.
    virtual void seLoadGroup(std::uint32_t group, std::span<const std::string_view> paths) = 0;
    virtual void sePlayIn(std::uint32_t group, std::uint32_t index, std::int32_t slot) = 0;
    // FUN_0040d4d0(slot): stop what slot 0..0x3f played last, if it still plays.
    virtual void seStopSlot(std::int32_t slot) = 0;
    // Voice stream: a second player beside BGM. Playing stops the previous line.
    virtual void voicePlay(std::string_view legacyPath) = 0;
    virtual void voiceStop() = 0;
    // Option volumes in millibels (DAT_00738c18 / 40 / 68 by level):
    // FUN_0041da10 on the BGM / voice streams, FUN_0040d6c0 the SE master.
    enum class Channel { Bgm, Se, Voice };
    virtual void setVolume(Channel c, std::int32_t millibel) = 0;
    virtual void tick() = 0;   // once per logic frame
};

// Silent stand-in until the PLW/WAV loaders are ported. Fades still take
// their real duration so task timing matches the original.
class NullAudio final : public IAudio {
public:
    void loadSystemSe() override {}
    void bgmPlay(std::uint32_t track, std::uint32_t flags) override {
        SDL_Log("[audio] bgmPlay track=%u flags=%u", track, flags);
        fadeLeft_ = 0;
        playing_ = true;
    }
    void bgmFadeOut(std::uint32_t frames) override {
        SDL_Log("[audio] bgmFadeOut %u frames", frames);
        fadeLeft_ = frames;
    }
    bool bgmFadeFinished() const override { return fadeLeft_ == 0; }
    bool bgmIsPlaying() const override { return playing_; }
    void bgmStop() override { playing_ = false; fadeLeft_ = 0; }
    void sePlay(std::uint32_t id) override { SDL_Log("[audio] sePlay %u", id); }
    void seStopAll() override {}
    void seLoadGroup(std::uint32_t, std::span<const std::string_view>) override {}
    void sePlayIn(std::uint32_t group, std::uint32_t index, std::int32_t slot) override {
        SDL_Log("[audio] se group %u #%u slot %d", group, index, slot);
    }
    void seStopSlot(std::int32_t) override {}
    void voicePlay(std::string_view path) override { SDL_Log("[audio] voice %.*s", int(path.size()), path.data()); }
    void voiceStop() override {}
    void setVolume(Channel c, std::int32_t mb) override { SDL_Log("[audio] volume %d = %d mB", int(c), int(mb)); }
    void tick() override { if (fadeLeft_ && --fadeLeft_ == 0) playing_ = false; }
private:
    std::uint32_t fadeLeft_ = 0;
    bool          playing_  = false;
};

#ifdef CH_HAVE_MIXER
// SDL_mixer backend; returns nullptr if the device cannot be opened.
std::unique_ptr<IAudio> makeMixerAudio();
#endif

} // namespace ch
