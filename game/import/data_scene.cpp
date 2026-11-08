// ============================================================================
// data_scene.cpp
// ============================================================================
#include "data_scene.hpp"
#include "../state/exe_image.hpp"
#include "../text/text_renderer.hpp"

#include <algorithm>
#include <filesystem>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace ch {

namespace {
std::vector<std::string>& webQueue() { static std::vector<std::string> q; return q; }
bool& webSynced() { static bool s = false; return s; }

constexpr SDL_Color kText{0xe8, 0xe6, 0xdf, 0xff}, kDim{0x9a, 0xa3, 0xb2, 0xff}, kGood{0x7f, 0xd8, 0x8f, 0xff},
                    kBad{0xf2, 0x8b, 0x82, 0xff}, kAccent{0xf0, 0xc0, 0x60, 0xff};
constexpr std::size_t kPackRows = 4;
constexpr int kPackY = 224, kPackRowH = 34;

std::string baseName(const std::string& p) {
    const auto s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}
} // namespace

#ifdef __EMSCRIPTEN__
// Called by web/shell.html for each file the player chose (written to /import/).
extern "C" EMSCRIPTEN_KEEPALIVE void ch_web_add_file(const char* path) { webQueue().emplace_back(path); }
extern "C" EMSCRIPTEN_KEEPALIVE void ch_web_synced(int) { webSynced() = true; }
#endif

DataScene::DataScene(Launch launch, bool canReturn) : launch_(std::move(launch)), canReturn_(canReturn) {}
DataScene::~DataScene() = default;

void DataScene::onEnter(legacy::Engine& e) {
    text_ = std::make_unique<TextRenderer>(e.renderer());
    lang::activate({});
    have_ = import::installed();
    packs_ = lang::packs();
    SDL_ShowCursor(SDL_ENABLE);
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.chShowImport) Module.chShowImport(true); });
#endif
    layout();
}

void DataScene::layout() {
    buttons_.clear();
    const bool busy = imp_.running() || syncing_;
    std::string missing;
    const bool ready = imp_.ready(&missing);
    int x = 64;
    auto add = [&](int w, std::string label, bool on, std::function<void()> fn) {
        buttons_.push_back({{x, 672, w, 48}, std::move(label), on, std::move(fn)});
        x += w + 16;
    };
    add(200, "Import", ready && !busy, [this] {
        message_.clear();
        imp_.start();
        if (!imp_.error().empty()) message_ = imp_.error();
    });
    auto play = [this](const std::string& v) {
        lang::savePreferred(v);
#ifdef __EMSCRIPTEN__
        EM_ASM({ if (Module.chShowImport) Module.chShowImport(false); });
#endif
        SDL_ShowCursor(SDL_DISABLE);
        launch_(v);
    };
    add(220, "Play  日本語", have_.jp && !busy, [play] { play("jp"); });
    add(220, "Play  繁體中文", have_.tw && !busy, [play] { play("tw"); });
    if (canReturn_) add(160, "Back", !busy && (have_.jp || have_.tw), [play, this] { play(lang::preferred(have_)); });
    for (std::size_t i = 0; i < packs_.size() && i < kPackRows; ++i) {
        const lang::Pack p = packs_[i];
        const int y = kPackY + int(i) * kPackRowH;
        buttons_.push_back({{770, y, 70, 30}, "Play", have_.jp && !busy, [play, p] { play(p.id); }, 14});
        buttons_.push_back({{848, y, 90, 30}, "Remove", !busy, [this, p] {
            if (lang::removePack(p.id)) {
                packs_ = lang::packs();
                message_ = p.name + " removed.";
                startSync(message_);
            }
        }, 14});
    }
}

void DataScene::startSync(std::string doneMessage) {
#ifdef __EMSCRIPTEN__
    webSynced() = false;
    syncing_ = true;
    syncedMessage_ = std::move(doneMessage);
    EM_ASM({ FS.syncfs(false, function(err) {
        if (err) console.error('IDBFS', err);
        Module.ccall('ch_web_synced', null, ['number'], [err ? 1 : 0]);
    }); });
#else
    (void)doneMessage;
#endif
}

void DataScene::fixedUpdate(legacy::Engine& e, double) {
    auto files = e.takeDroppedFiles();
    for (auto& f : webQueue()) files.push_back(std::move(f));
    webQueue().clear();
    if (!imp_.running())
        for (const auto& f : files) {
            if (lang::isPackFile(f)) {
                lang::Pack p;
                std::string error;
                if (lang::installPack(f, p, error)) {
                    message_ = p.name + ": language pack added.";
                    packs_ = lang::packs();
                } else {
                    message_ = baseName(f) + ": language pack failed: " + error;
                }
#ifdef __EMSCRIPTEN__
                std::error_code ec;
                if (f.starts_with("/import/")) std::filesystem::remove(f, ec);
#endif
                startSync(message_);
                continue;
            }
            const auto& in = imp_.add(f);
            message_ = baseName(f) + ": " + import::kindName(in.kind) + (in.kind == import::Kind::Unknown ? " — " + in.note : "");
        }

    if (imp_.running()) {
        imp_.step(25);
        if (!imp_.running()) {
            message_ = imp_.error().empty() ? "Import finished." : "Import failed: " + imp_.error();
            exe::reload();
            have_ = import::installed();
#ifdef __EMSCRIPTEN__
            std::error_code ec;
            for (const auto& in : imp_.inputs())
                if (in.path.starts_with("/import/")) std::filesystem::remove(in.path, ec);
            const std::string done = imp_.error().empty() ? "Import finished; the files are kept in this browser." : message_;
            message_ += " Saving the files in the browser...";
            startSync(done);
#endif
        }
    }
    if (syncing_ && webSynced()) {
        syncing_ = false;
        message_ = syncedMessage_;
    }

    layout();
    const auto& m = e.input().mouse();
    hover_ = -1;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const SDL_Rect& r = buttons_[i].r;
        if (m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h) hover_ = int(i);
    }
    if ((m.pressed & legacy::kMouseLeft) && hover_ >= 0 && buttons_[std::size_t(hover_)].enabled)
        buttons_[std::size_t(hover_)].click();
}

void DataScene::text(SDL_Renderer* r, const std::string& s, int x, int y, int px, SDL_Color c, bool bold) {
    if (s.empty() || !text_ || !text_->available()) return;
    const auto g = text_->render(s, px, bold);
    if (!g.tex) return;
    SDL_SetTextureColorMod(g.tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(g.tex, c.a);
    const SDL_Rect dst{x, y, g.w, g.h};
    SDL_RenderCopy(r, g.tex, nullptr, &dst);
}

void DataScene::render(legacy::Engine&, SDL_Renderer* r, double) {
    SDL_SetRenderDrawColor(r, 0x12, 0x17, 0x22, 0xff);
    SDL_RenderClear(r);
    text(r, "Cross Hermit — game files", 64, 40, 30, kAccent, true);
#ifdef __EMSCRIPTEN__
    text(r, "This port plays your own copy of the game. Choose the files with the buttons", 64, 96, 17, kText);
    text(r, "above the game (or drop them on the page). They stay in this browser.", 64, 120, 17, kText);
#else
    text(r, "This port plays your own copy of the game. Drop these files onto this window", 64, 96, 17, kText);
    text(r, "(or run: game --import FILE...):", 64, 120, 17, kText);
#endif
    auto has = [&](import::Kind k) {
        return std::any_of(imp_.inputs().begin(), imp_.inputs().end(), [k](const import::Input& i) { return i.kind == k; });
    };
    struct Need { std::string what, detail; bool done; };
    const Need needs[] = {
        {"Cross Hermit Update 1.2.exe", "the Japanese 1.2 update, needed for either release",
         have_.jpExe || has(import::Kind::JpUpdate) || has(import::Kind::JpExe)},
        {"The Japanese disc", "EB2002CH.iso, or data.z from the disc", have_.jp || has(import::Kind::JpDisc)},
        {"The Traditional Chinese disc", "its disc image, or cd1 from the disc", have_.tw || has(import::Kind::TwDisc)},
    };
    int y = 170;
    for (const auto& n : needs) {
        text(r, n.done ? "✓" : "•", 72, y, 18, n.done ? kGood : kDim, true);
        text(r, n.what, 100, y, 18, kText, true);
        text(r, n.detail, 100, y + 24, 15, kDim);
        y += 58;
    }
    text(r, "One disc is enough; with both, either language can be played.", 100, y + 2, 15, kDim);

    text(r, "Language packs", 600, 170, 18, kText, true);
    const bool needJp = !packs_.empty() && !have_.jp;
    text(r, needJp ? "These need the Japanese disc imported." : "Add a language pack .zip the same way as the files.", 600, 194, 15, needJp ? kAccent : kDim);
    if (packs_.empty()) text(r, "None yet.", 600, kPackY + 6, 16, kDim);
    for (std::size_t i = 0; i < packs_.size() && i < kPackRows; ++i)
        text(r, packs_[i].name, 600, kPackY + int(i) * kPackRowH + 5, 16, kText);
    if (packs_.size() > kPackRows)
        text(r, "and " + std::to_string(packs_.size() - kPackRows) + " more: remove one to see them.", 600, kPackY + int(kPackRows) * kPackRowH, 14, kDim);

    y = 390;
    text(r, "Installed:", 64, y, 17, kText, true);
    std::string inst;
    if (have_.jp) inst += "Japanese";
    if (have_.tw) inst += std::string(inst.empty() ? "" : ", ") + "Traditional Chinese";
    text(r, inst.empty() ? "nothing yet" : inst, 170, y, 17, inst.empty() ? kDim : kGood);
    y += 36;
    if (!imp_.inputs().empty()) text(r, "Added:", 64, y, 17, kText, true);
    for (const auto& in : imp_.inputs()) {
        if (y > 560) break;
        const bool ok = in.kind != import::Kind::Unknown;
        text(r, baseName(in.path), 170, y, 16, ok ? kText : kBad);
        text(r, ok ? import::kindName(in.kind) : in.note, 560, y, 15, ok ? kDim : kBad);
        y += 24;
    }

    std::string missing;
    if (imp_.running() || syncing_) {
        const SDL_Rect bar{64, 600, 896, 18}, fill{64, 600, int(896 * imp_.progress()), 18};
        SDL_SetRenderDrawColor(r, 0x2a, 0x33, 0x44, 0xff);
        SDL_RenderFillRect(r, &bar);
        SDL_SetRenderDrawColor(r, 0xf0, 0xc0, 0x60, 0xff);
        SDL_RenderFillRect(r, &fill);
        text(r, syncing_ ? message_ : imp_.status(), 64, 626, 15, kDim);
    } else if (!message_.empty()) {
        text(r, message_, 64, 610, 16, message_.find("failed") != std::string::npos ? kBad : kText);
    } else if (!imp_.inputs().empty() && !imp_.ready(&missing)) {
        text(r, "Still needed: " + missing, 64, 610, 16, kAccent);
    }

    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const auto& b = buttons_[i];
        const bool hot = int(i) == hover_ && b.enabled;
        SDL_SetRenderDrawColor(r, hot ? 0x3b : 0x26, hot ? 0x4a : 0x30, hot ? 0x63 : 0x42, 0xff);
        SDL_RenderFillRect(r, &b.r);
        SDL_SetRenderDrawColor(r, b.enabled ? 0xf0 : 0x4a, b.enabled ? 0xc0 : 0x52, b.enabled ? 0x60 : 0x62, 0xff);
        SDL_RenderDrawRect(r, &b.r);
        if (b.px == 18) text(r, b.label, b.r.x + 18, b.r.y + 12, 18, b.enabled ? kText : kDim, true);
        else text(r, b.label, b.r.x + 10, b.r.y + 6, b.px, b.enabled ? kText : kDim, true);
    }
    text(r, "F9: this screen", 860, 740, 13, kDim);
}

} // namespace ch
