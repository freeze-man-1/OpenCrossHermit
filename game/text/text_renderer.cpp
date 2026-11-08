// ============================================================================
// text_renderer.cpp
// ============================================================================
#include "text_renderer.hpp"

#include <SDL_ttf.h>

namespace ch {

TextRenderer::TextRenderer(SDL_Renderer* r) : renderer_(r) {
    if (TTF_WasInit() == 0 && TTF_Init() != 0) {
        SDL_Log("[text] TTF_Init: %s", TTF_GetError());
        return;
    }
    // The port's own fonts (resources/fonts); a player's MS Gothic can sit there too.
    const std::string& root = legacy::vfs::resourceRoot();
    for (const char* name : {"fonts/msgothic.ttc", "fonts/NotoSansCJK-Regular.ttc"}) {
        if (SDL_RWops* rw = SDL_RWFromFile((root + name).c_str(), "rb")) {
            SDL_RWclose(rw);
            fontPath_ = root + name;
            // msgothic.ttc: face 0 is MS Gothic. NotoSansCJK.ttc: face 0 is Japanese.
            faceIndex_ = 0;
            SDL_Log("[text] font %s", fontPath_.c_str());
            return;
        }
    }
    SDL_Log("[text] no font in %sfonts/; text falls back to the debug font", root.c_str());
}

TextRenderer::~TextRenderer() {
    cache_.clear();
    for (auto& [k, f] : fonts_) TTF_CloseFont(f);
}

TTF_Font* TextRenderer::font(int px, bool bold) {
    auto it = fonts_.find({px, bold});
    if (it != fonts_.end()) return it->second;
    TTF_Font* f = TTF_OpenFontIndex(fontPath_.c_str(), px, faceIndex_);
    if (!f) { SDL_Log("[text] TTF_OpenFont: %s", TTF_GetError()); return nullptr; }
    TTF_SetFontStyle(f, bold ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);
    fonts_[{px, bold}] = f;
    return f;
}

TextRenderer::Glyphs TextRenderer::render(const std::string& utf8, int px, bool bold) {
    if (!available() || utf8.empty()) return {};
    Key key{utf8, px, bold};
    if (auto it = cache_.find(key); it != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return {it->second.tex.get(), it->second.w, it->second.h};
    }
    TTF_Font* f = font(px, bold);
    if (!f) return {};
    SDL_Surface* s = TTF_RenderUTF8_Blended(f, utf8.c_str(), SDL_Color{255, 255, 255, 255});
    if (!s) return {};
    legacy::TexturePtr tex(SDL_CreateTextureFromSurface(renderer_, s));
    const int w = s->w, h = s->h;
    SDL_FreeSurface(s);
    if (!tex) return {};
    SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);

    if (cache_.size() >= kCacheSize) {
        cache_.erase(lru_.back());
        lru_.pop_back();
    }
    lru_.push_front(key);
    auto& e = cache_[key];
    e = Entry{std::move(tex), w, h, lru_.begin()};
    return {e.tex.get(), w, h};
}

} // namespace ch
