// ============================================================================
// mixer_audio.cpp — SDL_mixer backend for IAudio (replaces DirectShow BGM +
// DirectSound SE). Built only when CH_HAVE_MIXER is defined.
// ============================================================================
#ifdef CH_HAVE_MIXER
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "audio.hpp"
#include "audio_tables.hpp"
#include "../engine/engine_core.hpp"

#include <SDL_mixer.h>

#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace ch {

namespace {
constexpr int kTickHz = 60;
constexpr int kVoiceChannel = 0;   // reserved: SE never lands here

struct MusicDeleter { void operator()(Mix_Music* m) const noexcept { Mix_FreeMusic(m); } };
struct ChunkDeleter { void operator()(Mix_Chunk* c) const noexcept { Mix_FreeChunk(c); } };

class MixerAudio final : public IAudio {
public:
    ~MixerAudio() override {
        Mix_HaltMusic();
        Mix_HaltChannel(-1);
        music_.reset();
        voice_.reset();
        for (auto& c : se_) c.reset();
        for (auto& g : groups_) g.clear();
#ifdef __EMSCRIPTEN__
        // SDL 2.30's close leaves the output node's handler in place, and an
        // event already queued then runs it on the freed device.
        EM_ASM({
            const audio = Module['SDL2'] && Module['SDL2'].audio;
            if (audio && audio.scriptProcessorNode) audio.scriptProcessorNode.onaudioprocess = null;
        });
#endif
        Mix_CloseAudio();
        Mix_Quit();
    }

    void loadSystemSe() override {
        for (std::size_t i = 0; i < kSystemSePaths.size(); ++i) {
            try {
                auto bytes = legacy::vfs::readAsset(kSystemSePaths[i]);
                SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
                se_[i].reset(Mix_LoadWAV_RW(rw, 1));                 // decodes, copies
                if (!se_[i]) SDL_Log("[audio] SE %zu: %s", i, Mix_GetError());
            } catch (const std::exception& ex) {
                SDL_Log("[audio] SE %zu: %s", i, ex.what());
            }
        }
    }

    // FUN_0041e230: flags != 0 loops the track.
    void bgmPlay(std::uint32_t track, std::uint32_t flags) override {
        Mix_HaltMusic();
        music_.reset();
        if (track >= kBgmPaths.size() || kBgmPaths[track].empty()) {
            SDL_Log("[audio] bgm track %u has no file", track);
            return;
        }
        try {
            musicBytes_ = legacy::vfs::readAsset(kBgmPaths[track]);   // must outlive the stream
        } catch (const std::exception& ex) {
            SDL_Log("[audio] bgm %u: %s", track, ex.what());
            return;
        }
        SDL_RWops* rw = SDL_RWFromConstMem(musicBytes_.data(), static_cast<int>(musicBytes_.size()));
        music_.reset(Mix_LoadMUS_RW(rw, 1));
        if (!music_) { SDL_Log("[audio] bgm %u: %s", track, Mix_GetError()); return; }
        Mix_PlayMusic(music_.get(), flags ? -1 : 1);
    }

    void bgmFadeOut(std::uint32_t frames) override {
        if (Mix_PlayingMusic()) Mix_FadeOutMusic(static_cast<int>(frames * 1000 / kTickHz));
    }
    bool bgmFadeFinished() const override { return Mix_PlayingMusic() == 0; }
    bool bgmIsPlaying() const override { return Mix_PlayingMusic() != 0; }
    void bgmStop() override { Mix_HaltMusic(); music_.reset(); }

    void sePlay(std::uint32_t id) override {
        if (id < se_.size() && se_[id]) Mix_PlayChannel(-1, se_[id].get(), 0);
    }
    void seStopAll() override {
        for (int ch = kVoiceChannel + 1; ch < Mix_AllocateChannels(-1); ++ch) Mix_HaltChannel(ch);
    }

    void seLoadGroup(std::uint32_t group, std::span<const std::string_view> paths) override {
        if (group == 0 || group >= groups_.size()) return;
        auto& g = groups_[group];
        // FUN_0040d120: the group's own buffers are released first, which
        // stops only that group's sounds; others (a scene's last SE) play on.
        for (int ch = kVoiceChannel + 1; ch < Mix_AllocateChannels(-1); ++ch) {
            Mix_Chunk* playing = Mix_Playing(ch) ? Mix_GetChunk(ch) : nullptr;
            for (const auto& c : g)
                if (playing != nullptr && c.get() == playing) Mix_HaltChannel(ch);
        }
        g.clear();
        for (std::string_view path : paths) {
            std::unique_ptr<Mix_Chunk, ChunkDeleter> chunk;
            try {
                auto bytes = legacy::vfs::readAsset(path);
                SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
                chunk.reset(Mix_LoadWAV_RW(rw, 1));
                if (!chunk) SDL_Log("[audio] SE %.*s: %s", int(path.size()), path.data(), Mix_GetError());
            } catch (const std::exception& ex) {
                SDL_Log("[audio] SE %.*s: %s", int(path.size()), path.data(), ex.what());
            }
            g.push_back(std::move(chunk));
        }
    }

    void sePlayIn(std::uint32_t group, std::uint32_t index, std::int32_t slot) override {
        if (group >= groups_.size() || slot < -1 || slot >= int(slotChannel_.size())) return;
        Mix_Chunk* chunk = group == 0 ? (index < se_.size() ? se_[index].get() : nullptr)
                                      : (index < groups_[group].size() ? groups_[group][index].get() : nullptr);
        if (!chunk) return;
        if (slot >= 0) {   // FUN_0040d4d0: stop the slot's previous sound if it is still that sound
            const auto [prevCh, prevChunk] = slotChannel_[std::size_t(slot)];
            if (prevCh >= 0 && Mix_Playing(prevCh) && Mix_GetChunk(prevCh) == prevChunk) Mix_HaltChannel(prevCh);
        }
        const int ch = Mix_PlayChannel(-1, chunk, 0);
        if (slot >= 0) slotChannel_[std::size_t(slot)] = {ch, chunk};
    }

    void seStopSlot(std::int32_t slot) override {
        if (slot < 0 || slot >= int(slotChannel_.size())) return;
        auto& [ch, chunk] = slotChannel_[std::size_t(slot)];
        if (ch >= 0 && Mix_Playing(ch) && Mix_GetChunk(ch) == chunk) Mix_HaltChannel(ch);
        ch = -1;
    }

    void voicePlay(std::string_view path) override {
        voiceStop();
        try {
            auto bytes = legacy::vfs::readAsset(path);
            SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
            voice_.reset(Mix_LoadWAV_RW(rw, 1));                     // decodes, copies
        } catch (const std::exception& ex) {
            SDL_Log("[audio] voice %.*s: %s", int(path.size()), path.data(), ex.what());
            return;
        }
        if (!voice_) { SDL_Log("[audio] voice: %s", Mix_GetError()); return; }
        Mix_PlayChannel(kVoiceChannel, voice_.get(), 0);
    }
    void voiceStop() override {
        Mix_HaltChannel(kVoiceChannel);
        voice_.reset();
    }
    // DirectSound attenuation: 0 mB is full volume, -10000 silence. The SE
    // table goes up to +2000, which the original passed on; here it stays at full.
    void setVolume(Channel c, std::int32_t mb) override {
        const double gain = mb >= 0 ? 1.0 : mb <= -10000 ? 0.0 : std::pow(10.0, double(mb) / 2000.0);
        const int vol = static_cast<int>(gain * MIX_MAX_VOLUME + 0.5);
        switch (c) {
            case Channel::Bgm: Mix_VolumeMusic(vol); break;
            case Channel::Voice: Mix_Volume(kVoiceChannel, vol); break;
            case Channel::Se:
                for (int ch = kVoiceChannel + 1; ch < Mix_AllocateChannels(-1); ++ch) Mix_Volume(ch, vol);
                break;
        }
    }
    void tick() override {}

private:
    std::unique_ptr<Mix_Music, MusicDeleter>                                 music_;
    std::vector<std::uint8_t>                                                musicBytes_;
    std::array<std::unique_ptr<Mix_Chunk, ChunkDeleter>, kSystemSePaths.size()> se_;
    std::unique_ptr<Mix_Chunk, ChunkDeleter>                                 voice_;
    std::array<std::vector<std::unique_ptr<Mix_Chunk, ChunkDeleter>>, 20>   groups_;      // 0x14 groups
    std::array<std::pair<int, Mix_Chunk*>, 0x40>                             slotChannel_ = makeSlots();
    static std::array<std::pair<int, Mix_Chunk*>, 0x40> makeSlots() {
        std::array<std::pair<int, Mix_Chunk*>, 0x40> a{};
        a.fill({-1, nullptr});
        return a;
    }
};
} // namespace

std::unique_ptr<IAudio> makeMixerAudio() {
    if ((Mix_Init(MIX_INIT_MP3) & MIX_INIT_MP3) == 0)
        SDL_Log("[audio] MP3 support missing: %s", Mix_GetError());
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        SDL_Log("[audio] Mix_OpenAudio: %s", Mix_GetError());
        return nullptr;
    }
    Mix_AllocateChannels(16);
    Mix_ReserveChannels(1);   // channel 0 = voice stream
    return std::make_unique<MixerAudio>();
}

} // namespace ch
#endif // CH_HAVE_MIXER
