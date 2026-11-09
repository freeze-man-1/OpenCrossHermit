// ============================================================================
// engine_core.cpp
// ============================================================================
#include "engine_core.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cmath>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace legacy {

// ------------------------------------------------------------- Input ------
void InputState::setTextInput(bool on) noexcept {
    if (on == textOn_) return;
    textOn_ = on;
    if (on) SDL_StartTextInput(); else SDL_StopTextInput();
}

void InputState::onKey(SDL_Scancode sc, bool down) noexcept {
    if (down && textOn_) {
        if (sc == SDL_SCANCODE_BACKSPACE) ++text_.backspaces;
        if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) text_.enter = true;
        if (sc == SDL_SCANCODE_A && (SDL_GetModState() & KMOD_CTRL)) text_.selectAll = true;
    }
    Action a;
    switch (sc) {
        case SDL_SCANCODE_UP:    case SDL_SCANCODE_W: a = Action::Up;    break;
        case SDL_SCANCODE_DOWN:  case SDL_SCANCODE_S: a = Action::Down;  break;
        case SDL_SCANCODE_LEFT:  case SDL_SCANCODE_A: a = Action::Left;  break;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: a = Action::Right; break;
        case SDL_SCANCODE_Z: case SDL_SCANCODE_RETURN:                          a = Action::Confirm; break;
        case SDL_SCANCODE_SPACE:                                                a = Action::Skip;    break;
        case SDL_SCANCODE_X: case SDL_SCANCODE_BACKSPACE:                       a = Action::Cancel;  break;
        case SDL_SCANCODE_ESCAPE:                                               a = Action::Menu;    break;
        default: return;
    }
    const auto i = idx(a);
    if (down && !held_[i]) latched_[i] = true;
    held_[i] = down;
}

void InputState::onMouseButton(std::uint8_t sdlButton, bool down) noexcept {
    std::uint32_t bit = 0;
    switch (sdlButton) {
        case SDL_BUTTON_LEFT:   bit = kMouseLeft;   break;
        case SDL_BUTTON_RIGHT:  bit = kMouseRight;  break;
        case SDL_BUTTON_MIDDLE: bit = kMouseMiddle; break;
        default: return;
    }
    if (down) { mouseDown_ |= bit; mouseLatch_ |= bit; }
    else      { mouseDown_ &= ~bit; }
}

// A press that is released again before the next tick still reports as held
// for one tick, so fast clicks are never lost (the original polled and could).
void InputState::beginTick() noexcept {
    const std::uint32_t cur = mouseDown_ | mouseLatch_;
    mouse_.held     = cur;
    mouse_.pressed  = (mousePrev_ ^ cur) & cur;
    mouse_.released = (mousePrev_ ^ cur) & mousePrev_;
    mousePrev_      = cur;
    mouse_.wheel    = wheel_;
    wheel_          = 0;
}

void InputState::applyTouch() {
    while (touch_.hasMouseStep()) {
        const touch::MouseStep s = touch_.popMouseStep();
        onMouseMove(static_cast<int>(s.at.x), static_cast<int>(s.at.y));
        if (s.button != touch::MouseStep::None)
            onMouseButton(s.button == touch::MouseStep::Left ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT, s.down);
        if (s.endTick) break;
    }
}

// ------------------------------------------------------------ Engine ------
Engine::Engine(EngineConfig cfg) : cfg_(std::move(cfg)) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0)
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest-neighbour: preserve pixel art
    // Fingers work the mouse through touch::Recognizer (long-press is the right
    // button), not SDL's left-button-only stand-in.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#ifdef __EMSCRIPTEN__
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif

#ifdef __EMSCRIPTEN__
    // The page's CSS scales the canvas. A resizable window would instead take
    // its size from the canvas's CSS size, which here depends on that same
    // canvas's size, and it collapses to nothing.
    const Uint32 windowFlags = SDL_WINDOW_ALLOW_HIGHDPI;
#else
    const Uint32 windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
    window_.reset(SDL_CreateWindow(cfg_.title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg_.logicalW * cfg_.windowScale, cfg_.logicalH * cfg_.windowScale,
        windowFlags));
    if (!window_) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    renderer_.reset(SDL_CreateRenderer(window_.get(), -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!renderer_) throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

    applyScaling();

#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (Module.chPreMounted) return;   // web/shell.html mounts /save before main()
        if (!FS.analyzePath('/save').exists) FS.mkdir('/save');
        FS.mount(IDBFS, {}, '/save');
        Module.legacySaveReady = false;
        FS.syncfs(true, function(err) {
            if (err) console.error('IDBFS load', err);
            Module.legacySaveReady = true;
        });
    });
#endif

    perfFreq_    = SDL_GetPerformanceFrequency();
    lastCounter_ = SDL_GetPerformanceCounter();

    if (const char* s = SDL_getenv("CH_SHOTS")) {
        for (const char* p = s; *p;) {
            char* end = nullptr;
            const unsigned long long v = std::strtoull(p, &end, 10);
            if (end == p) break;
            shotTicks_.push_back(v);
            p = *end ? end + 1 : end;
        }
    }
    if (const char* s = SDL_getenv("CH_AUTOCLICK")) autoClick_ = std::strtoull(s, nullptr, 10);
    // "x,y" or "x,y@tick;x,y@tick;...": pointer parked there from that tick on.
    // A ":d" / ":u" suffix also presses / releases the left button at that
    // tick, ":r" / ":s" the right one.
    if (const char* s = SDL_getenv("CH_TEXT")) {
        std::string all = s;
        for (std::size_t at = 0; at < all.size();) {
            std::size_t end = all.find(';', at);
            if (end == std::string::npos) end = all.size();
            const std::string item = all.substr(at, end - at);
            if (const std::size_t sep = item.rfind('@'); sep != std::string::npos)
                textScript_.emplace_back(std::strtoull(item.c_str() + sep + 1, nullptr, 10), item.substr(0, sep));
            at = end + 1;
        }
    }
    // CH_KEYS=scancode@tick;...: that SDL scancode (41 is Escape) pressed at that tick, released 2 later.
    if (const char* s = SDL_getenv("CH_KEYS")) {
        std::string all = s;
        for (std::size_t at = 0; at < all.size();) {
            std::size_t end = all.find(';', at);
            if (end == std::string::npos) end = all.size();
            const std::string item = all.substr(at, end - at);
            if (const std::size_t sep = item.rfind('@'); sep != std::string::npos)
                keyScript_.push_back({std::strtoull(item.c_str() + sep + 1, nullptr, 10),
                                      static_cast<SDL_Scancode>(std::atoi(item.c_str()))});
            at = end + 1;
        }
    }
    if (const char* s = SDL_getenv("CH_MOUSE")) {
        for (const char* p = s; *p;) {
            char* end = nullptr;
            const long x = std::strtol(p, &end, 10);
            const long y = *end == ',' ? std::strtol(end + 1, &end, 10) : 0;
            const unsigned long long at = *end == '@' ? std::strtoull(end + 1, &end, 10) : 0;
            int button = 0;
            if (*end == ':') {
                switch (end[1]) { case 'd': button = 1; break; case 'u': button = -1; break;
                                  case 'r': button = 2; break; case 's': button = -2; break; default: break; }
                end += 2;
            }
            mouseScript_.push_back({at, SDL_Point{static_cast<int>(x), static_cast<int>(y)}, button});
            if (*end != ';') break;
            p = end + 1;
        }
    }
}

Engine::~Engine() {
    scene_.reset();
    pendingScene_.reset();
    renderer_.reset();
    window_.reset();
    SDL_Quit();
}

void Engine::applyScaling() {
    SDL_Renderer* r = renderer_.get();
    if (cfg_.scaleMode == ScaleMode::Stretch) {
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(r, &ow, &oh);         // physical pixels (high-DPI aware)
        SDL_RenderSetLogicalSize(r, 0, 0);
        SDL_RenderSetScale(r, float(ow) / float(cfg_.logicalW), float(oh) / float(cfg_.logicalH));
    } else {
        SDL_RenderSetLogicalSize(r, cfg_.logicalW, cfg_.logicalH);  // letterbox + mouse remap
        SDL_RenderSetIntegerScale(r, cfg_.scaleMode == ScaleMode::Integer ? SDL_TRUE : SDL_FALSE);
    }
}

void Engine::setFullscreen(bool on) {
    if (SDL_SetWindowFullscreen(window_.get(), on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
        SDL_Log("[engine] fullscreen: %s", SDL_GetError());
    applyScaling();
}

void Engine::run() {
    running_     = true;
    lastCounter_ = SDL_GetPerformanceCounter();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(&Engine::tickThunk, this, 0 /*rAF*/, 1 /*simulate infinite loop*/);
#else
    while (running_) frame();
#endif
}

void Engine::tickThunk(void* self) { static_cast<Engine*>(self)->frame(); }

void Engine::frame() {
    if (pendingScene_) {
        scene_ = std::move(pendingScene_);
        scene_->onEnter(*this);
    }

    // 1. Events (replaces WndProc / GetMessage / DirectInput polling)
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: requestQuit(); break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (e.type == SDL_KEYDOWN && !e.key.repeat)
                    for (const auto& [key, fn] : hotkeys_)
                        if (key == e.key.keysym.scancode) fn();
                if (!e.key.repeat) input_.onKey(e.key.keysym.scancode, e.type == SDL_KEYDOWN);
                break;
            case SDL_DROPFILE:
                if (e.drop.file) {
                    dropped_.emplace_back(e.drop.file);
                    SDL_free(e.drop.file);
                }
                break;
            case SDL_TEXTINPUT:
                input_.onText(e.text.text);
                break;
            case SDL_MOUSEMOTION:   // already remapped to logical coords by SDL_RenderSetLogicalSize
                if (e.motion.which == SDL_TOUCH_MOUSEID) break;   // fingers come as SDL_FINGER*
                input_.onMouseMove(e.motion.x, e.motion.y);
                break;
            case SDL_MOUSEWHEEL:
                if (e.wheel.which == SDL_TOUCH_MOUSEID) break;
                input_.onWheel(e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (e.button.which == SDL_TOUCH_MOUSEID) break;
                input_.onMouseMove(e.button.x, e.button.y);
                input_.onMouseButton(e.button.button, e.type == SDL_MOUSEBUTTONDOWN);
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP: {
                // The renderer has normalized the finger to the logical viewport (letterbox included).
                const touch::Point p{std::clamp(e.tfinger.x * float(cfg_.logicalW), 0.f, float(cfg_.logicalW - 1)),
                                     std::clamp(e.tfinger.y * float(cfg_.logicalH), 0.f, float(cfg_.logicalH - 1))};
                auto& t = input_.touch_;
                if (e.type == SDL_FINGERDOWN)        t.down(e.tfinger.fingerId, p, e.tfinger.timestamp);
                else if (e.type == SDL_FINGERMOTION) t.move(e.tfinger.fingerId, p, e.tfinger.timestamp);
                else                                 t.up(e.tfinger.fingerId, p, e.tfinger.timestamp);
                break;
            }
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)    applyScaling();
                else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) input_.releaseAll();
                break;
            default: break;
        }
    }

    input_.touch_.update(SDL_GetTicks());   // a long-press fires while the finger stays still

    // 2. Fixed-timestep logic at the original tick rate
    const std::uint64_t now = SDL_GetPerformanceCounter();
    // Debug: CH_PERF names each frame that took long (a stall, wherever it was).
    static const bool perf = SDL_getenv("CH_PERF") != nullptr;
    if (perf && double(now - lastCounter_) / double(perfFreq_) > 0.1)
        SDL_Log("[perf] slow frame before tick %llu: %.0f ms", static_cast<unsigned long long>(tickCount_),
                double(now - lastCounter_) * 1000.0 / double(perfFreq_));
    const double frameSec = std::min(double(now - lastCounter_) / double(perfFreq_), kMaxFrameSec);
    lastCounter_  = now;
    accumulator_ += frameSec;

    const double dt = tickDt();
    int steps = 0;
    while (accumulator_ >= dt && steps < kMaxStepsPerFrame) {
        if (autoClick_ && tickCount_ % autoClick_ == autoClick_ - 1) input_.injectClick();
        for (const auto& [at, pt, button] : mouseScript_) {
            if (at > tickCount_) continue;
            input_.onMouseMove(pt.x, pt.y);                          // last one due wins
            if (button != 0 && at == tickCount_)
                input_.onMouseButton(std::abs(button) == 2 ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT, button > 0);
        }
        for (const auto& [at, key] : keyScript_)
            if (at == tickCount_ || at + 2 == tickCount_) input_.onKey(key, at == tickCount_);
        input_.applyTouch();
        input_.beginTick();
        // Debug: CH_INPUT_LOG logs each tick's mouse button edges and touch gestures.
        static const bool inputLog = SDL_getenv("CH_INPUT_LOG") != nullptr;
        if (const auto& m = input_.mouse(); inputLog && (m.pressed || m.released))
            SDL_Log("[input] tick %llu mouse %d,%d pressed %u released %u", static_cast<unsigned long long>(tickCount_),
                    m.x, m.y, m.pressed, m.released);
        if (const auto& g = input_.touch(); inputLog && (g.tap || g.longPress || g.dragBegan || g.dragEnded))
            SDL_Log("[input] tick %llu touch%s%s%s%s at %.0f,%.0f", static_cast<unsigned long long>(tickCount_),
                    g.tap ? " tap" : "", g.longPress ? " long-press" : "", g.dragBegan ? " drag-begin" : "",
                    g.dragEnded ? " drag-end" : "",
                    double(g.tap ? g.tapAt.x : g.longPress ? g.longPressAt.x : g.dragBegan ? g.dragStart.x : g.dragPos.x),
                    double(g.tap ? g.tapAt.y : g.longPress ? g.longPressAt.y : g.dragBegan ? g.dragStart.y : g.dragPos.y));
        for (const auto& [at, text] : textScript_)
            if (at == tickCount_) input_.injectText(text);
        if (scene_) scene_->fixedUpdate(*this, dt);
        input_.consumeTick();
        accumulator_ -= dt;
        ++steps;
        ++tickCount_;
    }
    if (steps == kMaxStepsPerFrame) accumulator_ = std::fmod(accumulator_, dt);

    // 3. Render (replaces Blt/BltFast + Flip)
    SDL_Renderer* r = renderer_.get();
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    if (scene_) scene_->render(*this, r, accumulator_ / dt);
    // Several ticks can run per frame, so a requested tick fires once it has passed.
    if (auto due = std::find_if(shotTicks_.begin(), shotTicks_.end(), [&](std::uint64_t t) { return t <= tickCount_; });
        due != shotTicks_.end()) {
        const std::uint64_t requested = *due;
        shotTicks_.erase(due);
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(r, &w, &h);
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)) {
            SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            const std::string name = "shot_" + std::to_string(requested) + ".bmp";
            SDL_SaveBMP(s, name.c_str());
            SDL_FreeSurface(s);
        }
    }
    SDL_RenderPresent(r);

    if (!running_) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
    }
}

// ------------------------------------------------------------- VFS --------
namespace vfs {
namespace {

// The player's imported game files (game/import): CH_DATA, else gamedata/
// next to the binary when it exists (a portable install), else the user's
// profile. Web: /gamedata, an IndexedDB mount (web/shell.html).
std::string& dataRootRef() {
    static std::string root = [] {
        auto slash = [](std::string d) { if (!d.empty() && d.back() != '/') d += '/'; return d; };
        if (const char* dir = SDL_getenv("CH_DATA"); dir && *dir) return slash(dir);
#ifdef __EMSCRIPTEN__
        return std::string("/gamedata/");
#else
        std::unique_ptr<char, decltype(&SDL_free)> base(SDL_GetBasePath(), &SDL_free);
        const std::string local = std::string(base ? base.get() : "./") + "gamedata/";
        std::error_code ec;
        if (std::filesystem::is_directory(local, ec)) return local;
        std::unique_ptr<char, decltype(&SDL_free)> pref(SDL_GetPrefPath("legacy-port", "game"), &SDL_free);
        return pref ? std::string(pref.get()) + "gamedata/" : local;
#endif
    }();
    return root;
}

// Files that ship with the port (fonts, the importer's patch): resources/
// next to the binary, CH_RESOURCES to override. Web: preloaded /resources.
const std::string& resourceRootRef() {
    static const std::string root = [] {
        if (const char* dir = SDL_getenv("CH_RESOURCES"); dir && *dir) {
            std::string d(dir);
            if (d.back() != '/') d += '/';
            return d;
        }
#ifdef __EMSCRIPTEN__
        return std::string("/resources/");
#else
        std::unique_ptr<char, decltype(&SDL_free)> base(SDL_GetBasePath(), &SDL_free);
        return std::string(base ? base.get() : "./") + "resources/";
#endif
    }();
    return root;
}

std::string& variantRef() { static std::string v = "jp"; return v; }
std::string& overlayRef() { static std::string v; return v; }
// readAsset's cache is shared with the thread that paints a language pack's images.
std::mutex& assetCacheMutex() { static std::mutex m; return m; }
std::unordered_map<std::string, std::vector<std::uint8_t>>& assetCacheRef() {
    static std::unordered_map<std::string, std::vector<std::uint8_t>> cache;
    return cache;
}

std::function<void(const std::string&, std::vector<std::uint8_t>&)>& assetFilterRef() {
    static std::function<void(const std::string&, std::vector<std::uint8_t>&)> f;
    return f;
}

bool isFile(const std::string& full) {
    std::error_code ec;
    return std::filesystem::is_regular_file(full, ec);
}

std::string saveRoot() {
    if (const char* dir = SDL_getenv("CH_SAVEDIR"); dir && *dir) {
        std::string d(dir);
        if (d.back() != '/') d += '/';
        return d;
    }
#ifdef __EMSCRIPTEN__
    return "/save/";
#else
    std::unique_ptr<char, decltype(&SDL_free)> pref(SDL_GetPrefPath("legacy-port", "game"), &SDL_free);
    return pref ? std::string(pref.get()) : std::string("./");
#endif
}

std::vector<std::uint8_t> readFile(const std::string& full) {
    RWopsPtr rw(SDL_RWFromFile(full.c_str(), "rb"));
    if (!rw) throw std::runtime_error("vfs: cannot open '" + full + "': " + SDL_GetError());
    const Sint64 size = SDL_RWsize(rw.get());
    if (size < 0) throw std::runtime_error("vfs: cannot size '" + full + "'");
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(size));
    if (!buf.empty() && SDL_RWread(rw.get(), buf.data(), 1, buf.size()) != buf.size())
        throw std::runtime_error("vfs: short read '" + full + "'");
    return buf;
}
} // namespace

std::string normalize(std::string_view p) {
    if (p.size() >= 2 && p[1] == ':') p.remove_prefix(2);       // drop drive letter
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        if (c == '\\') c = '/';
        if (c == '/' && !out.empty() && out.back() == '/') continue;   // Win32 accepts doubled separators
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    while (out.starts_with("./") || out.starts_with("/"))
        out.erase(0, out.starts_with("./") ? 2 : 1);
    return out;
}

const std::string& dataRoot() { return dataRootRef(); }
void setDataRoot(std::string root) {
    if (!root.empty() && root.back() != '/') root += '/';
    dataRootRef() = std::move(root);
}
const std::string& resourceRoot() { return resourceRootRef(); }
void setVariant(std::string_view v) { variantRef() = std::string(v); }
const std::string& variant() { return variantRef(); }
void setOverlay(std::string_view dir) { overlayRef() = std::string(dir); }
void setAssetFilter(std::function<void(const std::string&, std::vector<std::uint8_t>&)> filter) {
    assetFilterRef() = std::move(filter);
    const std::lock_guard lock(assetCacheMutex());
    assetCacheRef().clear();
}
void cacheAsset(const std::string& rel, std::vector<std::uint8_t> bytes) {
    const std::lock_guard lock(assetCacheMutex());
    assetCacheRef()[rel] = std::move(bytes);
}

std::string assetPath(std::string_view legacyPath) {
    const std::string rel = normalize(legacyPath);
    if (!overlayRef().empty()) {
        std::string pack = dataRoot() + overlayRef() + "/" + rel;
        if (isFile(pack)) return pack;
    }
    std::string full = dataRoot() + variant() + "/" + rel;
    if (isFile(full)) return full;
    return dataRoot() + "common/" + rel;
}

bool assetExists(std::string_view legacyPath) { return isFile(assetPath(legacyPath)); }

std::vector<std::uint8_t> readAsset(std::string_view legacyPath) {
    {
        const std::lock_guard lock(assetCacheMutex());
        if (const auto it = assetCacheRef().find(normalize(legacyPath)); it != assetCacheRef().end()) return it->second;
    }
    auto bytes = readFile(assetPath(legacyPath));
    if (assetFilterRef()) assetFilterRef()(normalize(legacyPath), bytes);
    return bytes;
}

std::vector<std::uint8_t> readResource(std::string_view name) {
    return readFile(resourceRoot() + std::string(name));
}

std::optional<std::vector<std::uint8_t>> readSave(std::string_view name) {
    const std::string full = saveRoot() + normalize(name);
    RWopsPtr probe(SDL_RWFromFile(full.c_str(), "rb"));
    if (!probe) return std::nullopt;
    probe.reset();
    return readFile(full);
}

void writeSave(std::string_view name, std::span<const std::uint8_t> data) {
    const std::string full = saveRoot() + normalize(name);
    RWopsPtr rw(SDL_RWFromFile(full.c_str(), "wb"));
    if (!rw) throw std::runtime_error("vfs: cannot write '" + full + "': " + SDL_GetError());
    if (!data.empty() && SDL_RWwrite(rw.get(), data.data(), 1, data.size()) != data.size())
        throw std::runtime_error("vfs: short write '" + full + "'");
    rw.reset();
#ifdef __EMSCRIPTEN__
    EM_ASM({ FS.syncfs(false, function(err) { if (err) console.error('IDBFS flush', err); }); });
#endif
}

namespace {
std::filesystem::path folderPath(std::string_view folder) {
    std::filesystem::path p(saveRoot());
    std::string f(folder);
    for (char& c : f) if (c == '\\') c = '/';
    return p / f;
}
} // namespace

std::vector<SaveFile> listSaveFolder(std::string_view folder) {
    std::vector<SaveFile> out;
    std::error_code ec;
    const auto dir = folderPath(folder);
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        // Not clock_cast: Emscripten's libc++ lacks it.
        const auto t = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                           e.last_write_time(ec) - std::filesystem::file_time_type::clock::now());
        out.push_back({e.path().filename().string(),
                       std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count()});
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> readSaveFile(std::string_view folder, std::string_view name) {
    const auto p = folderPath(folder) / std::string(name);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) return std::nullopt;
    return readFile(p.string());
}

void writeSaveFile(std::string_view folder, std::string_view name, std::span<const std::uint8_t> data) {
    std::error_code ec;
    std::filesystem::create_directories(folderPath(folder), ec);      // CreateDirectoryA in FUN_004d7830
    const std::string full = (folderPath(folder) / std::string(name)).string();
    RWopsPtr rw(SDL_RWFromFile(full.c_str(), "wb"));
    if (!rw) throw std::runtime_error("vfs: cannot write '" + full + "': " + SDL_GetError());
    if (!data.empty() && SDL_RWwrite(rw.get(), data.data(), 1, data.size()) != data.size())
        throw std::runtime_error("vfs: short write '" + full + "'");
    rw.reset();
#ifdef __EMSCRIPTEN__
    EM_ASM({ FS.syncfs(false, function(err) { if (err) console.error('IDBFS flush', err); }); });
#endif
}

bool removeSaveFile(std::string_view folder, std::string_view name) {
    std::error_code ec;
    const bool ok = std::filesystem::remove(folderPath(folder) / std::string(name), ec);
#ifdef __EMSCRIPTEN__
    EM_ASM({ FS.syncfs(false, function(err) { if (err) console.error('IDBFS flush', err); }); });
#endif
    return ok;
}

bool saveFileExists(std::string_view folder, std::string_view name) {
    std::error_code ec;
    return std::filesystem::exists(folderPath(folder) / std::string(name), ec);
}

bool saveStorageReady() {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({ return Module.legacySaveReady ? 1 : 0; }) != 0;
#else
    return true;
#endif
}
} // namespace vfs

// --------------------------------------------------------- Textures -------
namespace {
TexturePtr uploadARGB(SDL_Renderer* r, const std::vector<std::uint32_t>& px, int w, int h) {
    TexturePtr tex(SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h));
    if (!tex) throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
    SDL_UpdateTexture(tex.get(), nullptr, px.data(), w * 4);
    SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
    return tex;
}
std::size_t checkedArea(int w, int h, std::size_t have, std::size_t bpp) {
    if (w <= 0 || h <= 0) throw std::invalid_argument("texture: bad dimensions");
    const std::size_t n = std::size_t(w) * std::size_t(h);
    if (have < n * bpp) throw std::invalid_argument("texture: source buffer too small");
    return n;
}
} // namespace

TexturePtr textureFromRGB565(SDL_Renderer* r, std::span<const std::uint8_t> le565,
                             int w, int h, std::optional<std::uint16_t> colorKey) {
    const std::size_t n = checkedArea(w, h, le565.size(), 2);
    std::vector<std::uint32_t> argb(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto p = static_cast<std::uint16_t>(le565[2 * i] | (le565[2 * i + 1] << 8));
        const std::uint32_t r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
        const std::uint32_t a  = (colorKey && p == *colorKey) ? 0u : 0xFFu;
        argb[i] = (a << 24)
                | (((r5 << 3) | (r5 >> 2)) << 16)
                | (((g6 << 2) | (g6 >> 4)) << 8)
                |  ((b5 << 3) | (b5 >> 2));
    }
    return uploadARGB(r, argb, w, h);
}

TexturePtr textureFromIndexed8(SDL_Renderer* r, std::span<const std::uint8_t> indices,
                               std::span<const std::uint32_t, 256> pal,
                               int w, int h, std::optional<std::uint8_t> keyIndex) {
    const std::size_t n = checkedArea(w, h, indices.size(), 1);
    std::vector<std::uint32_t> argb(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t ix = indices[i];
        argb[i] = (keyIndex && ix == *keyIndex) ? 0u : (pal[ix] | 0xFF000000u);
    }
    return uploadARGB(r, argb, w, h);
}

} // namespace legacy
