// ============================================================================
// sprite_bank.hpp — 256 sprite banks of GPU textures (WinCon+0xb210 manager)
//
//   FUN_0041f090 / FUN_0041f0e0  load bank from .bin   -> loadBank
//   FUN_0041f4e0                 free one bank         -> unloadBank
//   FUN_0041f480                 free all 256 banks    -> unloadAll
//   FUN_004079c0                 draw (x, y)           -> drawSprite
// ============================================================================
#pragma once
#include "bank_format.hpp"
#include "draw_list.hpp"
#include "../engine/engine_core.hpp"
#include "text/text_renderer.hpp"

#include <array>
#include <span>
#include <vector>

namespace ch {

class BankSprites final : public ISpriteBanks {
public:
    explicit BankSprites(SDL_Renderer* r) : renderer_(r), text_(r) {}

    void loadBank(std::uint16_t bank, std::vector<std::uint8_t> bytes) override;
    void unloadBank(std::uint16_t bank) override;
    std::pair<int, int> frameSize(std::uint16_t bank, std::uint16_t frame) override;
    void unloadAll() noexcept { for (auto& b : banks_) b.clear(); }
    void drawSprite(SDL_Renderer* r, const SpriteCmd& c) override;
    void drawText(SDL_Renderer* r, const TextCmd& c) override;

private:
    struct Frame {
        std::span<const std::uint8_t> raw;   // entry inside Bank::bytes, decoded on first draw
        bool               decoded = false;
        legacy::TexturePtr tex{};
        legacy::TexturePtr silhouette{};      // white RGB + image alpha, built on first specular use
        std::vector<std::uint8_t> alphaMask{};  // 1 bit per texel, to build `silhouette`
        int w = 0, h = 0;
    };
    static constexpr std::size_t kBanks = 0x100;

    SDL_Texture* fontAtlas(SDL_Renderer* r);
    SDL_Texture* silhouette(SDL_Renderer* r, Frame& f);

    struct Bank {
        std::vector<std::uint8_t> bytes;
        std::vector<Frame>        frames;
        void clear() { frames.clear(); bytes.clear(); bytes.shrink_to_fit(); }
    };

    Frame* frame(std::uint16_t bank, std::uint16_t index);

    SDL_Renderer*                            renderer_;
    std::array<Bank, kBanks>                 banks_;
    legacy::TexturePtr                       font_;
    TextRenderer                             text_;
};

} // namespace ch
