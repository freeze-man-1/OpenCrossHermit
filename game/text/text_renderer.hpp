// ============================================================================
// text_renderer.hpp — UTF-8 text to textures with SDL_ttf (replaces the GDI
// CreateFontA + DrawText path of FUN_00408e30). The original asks for
// "ＭＳ ゴシック"; fonts/msgothic.ttc is used if present in the assets,
// otherwise the bundled Noto Sans CJK JP.
// ============================================================================
#pragma once
#include "../../engine/engine_core.hpp"

#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <SDL_ttf.h>

namespace ch {

class TextRenderer {
public:
    explicit TextRenderer(SDL_Renderer* r);
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    [[nodiscard]] bool available() const noexcept { return !fontPath_.empty(); }

    struct Glyphs { SDL_Texture* tex = nullptr; int w = 0, h = 0; };
    // White text with alpha, cached by (text, px, bold, face). Null texture on failure.
    // Face 1 is the serif (Noto Serif CJK, from fonts/ or the system), else the UI font.
    Glyphs render(const std::string& utf8, int px, bool bold, int face = 0);

private:
    TTF_Font* font(int px, bool bold, int face);

    SDL_Renderer* renderer_;
    std::string   fontPath_;
    long          faceIndex_ = 0;
    std::string   serifPath_, serifBoldPath_;   // empty when there is no serif
    std::map<std::tuple<int, bool, int>, TTF_Font*> fonts_;

    using Key = std::tuple<std::string, int, bool, int>;
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            return std::hash<std::string>{}(std::get<0>(k)) ^ (std::size_t(std::get<1>(k)) << 1) ^ std::size_t(std::get<2>(k)) ^
                   (std::size_t(std::get<3>(k)) << 2);
        }
    };
    struct Entry { legacy::TexturePtr tex; int w, h; std::list<Key>::iterator lru; };
    std::unordered_map<Key, Entry, KeyHash> cache_;
    std::list<Key> lru_;
    static constexpr std::size_t kCacheSize = 256;
};

} // namespace ch
