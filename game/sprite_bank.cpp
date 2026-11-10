// ============================================================================
// sprite_bank.cpp
// ============================================================================
#include "sprite_bank.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace ch {

namespace {
legacy::TexturePtr upload(SDL_Renderer* r, const Image& img) {
    legacy::TexturePtr tex(SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                             img.w, img.h));
    if (!tex) throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
    SDL_UpdateTexture(tex.get(), nullptr, img.argb.data(), img.w * 4);
    SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
    return tex;
}

// Diffuse / specular LUTs (VA 0x005ff148 / 0x005ff248), as formulas.
constexpr std::uint8_t diffuseLut(std::uint8_t c)  { return static_cast<std::uint8_t>(std::min(255, c * 2)); }
constexpr std::uint8_t specularLut(std::uint8_t c) {
    return c <= 0x80 ? 0 : c == 0xFF ? 0xFF : static_cast<std::uint8_t>((c - 0x80) * 2);
}
static_assert(diffuseLut(0x80) == 0xFF && diffuseLut(0x40) == 0x80 && diffuseLut(0x7F) == 0xFE);
static_assert(specularLut(0x80) == 0 && specularLut(0x81) == 2 && specularLut(0xC0) == 0x80 && specularLut(0xFF) == 0xFF);

// 5x7 glyphs, MSB of the low 5 bits is the left column. Stand-in for the GDI
// font FUN_0040ec50 rendered with; covers what the ported scenes print.
constexpr char kGlyphChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.- ";
constexpr std::uint8_t kGlyphs[][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},{0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};
constexpr int kGlyphCount = static_cast<int>(sizeof kGlyphs / sizeof kGlyphs[0]);
static_assert(kGlyphCount == sizeof kGlyphChars - 1);
constexpr int kGlyphW = 5, kGlyphH = 7, kAdvance = 6;
} // namespace

// FUN_0041f0e0: replaces the bank. The original created every texture up
// front (and FUN_00416a10 pre-touched them); here each entry is decoded and
// uploaded on first draw so large banks (opdata.bin: 48 x 1024^2) only cost
// GPU memory for frames actually shown. A bad entry leaves an empty frame.
void BankSprites::loadBank(std::uint16_t bank, std::vector<std::uint8_t> bytes) {
    if (bank >= kBanks) throw std::out_of_range("BankSprites: bank >= 256");
    unloadBank(bank);
    Bank& b = banks_[bank];
    b.bytes = std::move(bytes);
    for (auto entry : splitBank(b.bytes)) b.frames.push_back(Frame{.raw = entry});
    SDL_Log("bank 0x%02x loaded: %zu frames", bank, b.frames.size());
}

void BankSprites::unloadBank(std::uint16_t bank) {
    if (bank < kBanks) banks_[bank].clear();
}

std::pair<int, int> BankSprites::frameSize(std::uint16_t bank, std::uint16_t index) {
    const Bank& b = banks_[bank % kBanks];
    return index < b.frames.size() ? entrySize(b.frames[index].raw) : std::pair<int, int>{0, 0};
}

BankSprites::Frame* BankSprites::frame(std::uint16_t bank, std::uint16_t index) {
    Bank& b = banks_[bank % kBanks];
    if (index >= b.frames.size()) return nullptr;
    Frame& f = b.frames[index];
    if (!f.decoded) {
        f.decoded = true;
        std::string why;
        Image img = decodeBankEntry(f.raw, &why);
        if (img.empty()) {
            SDL_Log("bank 0x%02x frame %u skipped: %s", bank, index, why.c_str());
        } else {
            f.alphaMask.assign((img.argb.size() + 7) / 8, 0);   // texel alpha is 1-bit
            for (std::size_t p = 0; p < img.argb.size(); ++p)
                if (img.argb[p] >> 24) f.alphaMask[p >> 3] |= std::uint8_t(1u << (p & 7));
            f.tex = upload(renderer_, img);
            f.w = img.w;
            f.h = img.h;
        }
    }
    return f.tex ? &f : nullptr;
}

// FUN_00407ad0 (quad geometry) + FUN_0040e650 (render states).
void BankSprites::drawSprite(SDL_Renderer* r, const SpriteCmd& c) {
    Frame* fp = frame(c.bank, c.frame);
    if (!fp) {
        // Missing art stays visible while porting.
        const SDL_Rect box{c.x, c.y, c.w ? c.w : 16, c.h ? c.h : 16};
        SDL_SetRenderDrawColor(r, 255, 0, 255, 255);
        SDL_RenderDrawRect(r, &box);
        return;
    }
    Frame& f = *fp;

    // Quad size: +0x38/+0x3a, optionally scaled by percent.
    const float baseW = float(c.w ? c.w : f.w), baseH = float(c.h ? c.h : f.h);
    const float dw = (c.flags & kSprScale) ? baseW * c.scaleX / 100.0f : baseW;
    const float dh = (c.flags & kSprScale) ? baseH * c.scaleY / 100.0f : baseH;

    SDL_Rect src{0, 0, f.w, f.h};
    if (c.flags & kSprSrcRect) src = {c.src.x0, c.src.y0, c.src.x1 - c.src.x0, c.src.y1 - c.src.y0};

    float px = float(c.x), py = float(c.y);
    if (c.flags & kSprCentre) { px -= dw / 2.0f; py -= dh / 2.0f; }
    const SDL_FRect dst{px, py, dw, dh};

    const int flip = ((c.flags & kSprFlipU) ? SDL_FLIP_HORIZONTAL : 0) |
                     ((c.flags & kSprFlipV) ? SDL_FLIP_VERTICAL : 0);
    const int angle = (c.flags & kSprRotate) ? c.rotation % 360 : 0;
    const SDL_FPoint pivot = (c.flags & kSprPivotCentre) ? SDL_FPoint{dw / 2.0f, dh / 2.0f}
                                                         : SDL_FPoint{c.pivotX, c.pivotY};

    // Blend byte. Texels are 1-bit alpha, so the alpha test equals blending
    // with the vertex alpha forced to opaque.
    std::uint8_t  alpha = c.color.a;
    SDL_BlendMode mode  = SDL_BLENDMODE_BLEND;
    if (c.blend == kBlendAdd)                          mode = SDL_BLENDMODE_ADD;
    else if (!(c.blend == kBlendAlpha && alpha != 0xFF)) alpha = alpha > 8 ? 0xFF : 0;   // alpha test
    if (alpha == 0) return;

    auto copy = [&](SDL_Texture* t) {
        SDL_RenderCopyExF(r, t, &src, &dst, angle, angle ? &pivot : nullptr,
                          static_cast<SDL_RendererFlip>(flip));
    };

    SDL_SetTextureBlendMode(f.tex.get(), mode);
    SDL_SetTextureColorMod(f.tex.get(), diffuseLut(c.color.r), diffuseLut(c.color.g), diffuseLut(c.color.b));
    SDL_SetTextureAlphaMod(f.tex.get(), alpha);
    copy(f.tex.get());

    // D3D specular is added after the texture stage; masked by texel alpha.
    const std::uint8_t sr = specularLut(c.color.r), sg = specularLut(c.color.g), sb = specularLut(c.color.b);
    if (sr | sg | sb) {
        SDL_Texture* sil = silhouette(r, f);
        SDL_SetTextureColorMod(sil, sr, sg, sb);
        SDL_SetTextureAlphaMod(sil, alpha);
        copy(sil);
    }
}

SDL_Texture* BankSprites::silhouette(SDL_Renderer* r, Frame& f) {
    if (!f.silhouette) {
        Image img{f.w, f.h, std::vector<std::uint32_t>(std::size_t(f.w) * f.h)};
        for (std::size_t p = 0; p < img.argb.size(); ++p)
            img.argb[p] = (f.alphaMask[p >> 3] >> (p & 7) & 1) ? 0xFFFFFFFFu : 0x00FFFFFFu;
        f.silhouette = upload(r, img);
        SDL_SetTextureBlendMode(f.silhouette.get(), SDL_BLENDMODE_ADD);
    }
    return f.silhouette.get();
}

SDL_Texture* BankSprites::fontAtlas(SDL_Renderer* r) {
    if (font_) return font_.get();
    Image img{kGlyphCount * kGlyphW, kGlyphH, {}};
    img.argb.assign(std::size_t(img.w) * img.h, 0);
    for (int g = 0; g < kGlyphCount; ++g)
        for (int y = 0; y < kGlyphH; ++y)
            for (int x = 0; x < kGlyphW; ++x)
                if (kGlyphs[g][y] & (0x10 >> x)) img.argb[std::size_t(y) * img.w + g * kGlyphW + x] = 0xFFFFFFFFu;
    font_ = upload(r, img);
    return font_.get();
}

int BankSprites::textWidth(const std::string& utf8, int px, bool bold, int face) {
    if (utf8.empty() || !px || !text_.available()) return 0;
    return text_.render(utf8, px, bold, face).w;
}

void BankSprites::drawText(SDL_Renderer* r, const TextCmd& c) {
    if (c.fontPx && text_.available()) {
        const auto g = text_.render(c.text, c.fontPx, c.bold, c.face);
        if (!g.tex) return;
        SDL_SetTextureColorMod(g.tex, std::uint8_t(c.argb >> 16), std::uint8_t(c.argb >> 8), std::uint8_t(c.argb));
        SDL_SetTextureAlphaMod(g.tex, std::uint8_t(c.argb >> 24));
        const SDL_Rect dst{c.centred ? c.x - g.w / 2 : c.x, c.centred ? c.y - g.h / 2 : c.y, g.w, g.h};
        SDL_RenderCopy(r, g.tex, nullptr, &dst);
        return;
    }
    SDL_Texture* atlas = fontAtlas(r);
    SDL_SetTextureColorMod(atlas, std::uint8_t(c.argb >> 16), std::uint8_t(c.argb >> 8), std::uint8_t(c.argb));
    SDL_SetTextureAlphaMod(atlas, std::uint8_t(c.argb >> 24));
    int x = c.x;
    for (char ch : c.text) {
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        const char* hit = std::char_traits<char>::find(kGlyphChars, kGlyphCount, up);
        if (hit && up != ' ') {
            const SDL_Rect src{int(hit - kGlyphChars) * kGlyphW, 0, kGlyphW, kGlyphH};
            const SDL_Rect dst{x, c.y, kGlyphW, kGlyphH};
            SDL_RenderCopy(r, atlas, &src, &dst);
        }
        x += kAdvance;
    }
}

} // namespace ch
