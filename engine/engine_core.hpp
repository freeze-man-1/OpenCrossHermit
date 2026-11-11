// ============================================================================
// engine_core.hpp — SDL2 platform layer shared by every ported subsystem
// ============================================================================
#pragma once
#include <SDL.h>

#include "touch_gestures.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace legacy {

// ---------------------------------------------------------------- RAII ----
struct SdlDeleter {
    void operator()(SDL_Window* p)   const noexcept { SDL_DestroyWindow(p); }
    void operator()(SDL_Renderer* p) const noexcept { SDL_DestroyRenderer(p); }
    void operator()(SDL_Texture* p)  const noexcept { SDL_DestroyTexture(p); }
    void operator()(SDL_RWops* p)    const noexcept { SDL_RWclose(p); }
};
using WindowPtr   = std::unique_ptr<SDL_Window,   SdlDeleter>;
using RendererPtr = std::unique_ptr<SDL_Renderer, SdlDeleter>;
using TexturePtr  = std::unique_ptr<SDL_Texture,  SdlDeleter>;
using RWopsPtr    = std::unique_ptr<SDL_RWops,    SdlDeleter>;

// ------------------------------------------- Endian-safe binary reader ----
// Replaces Ghidra-style `*(int*)(buf + 0x1c)` casts. Never type-puns, never
// assumes host endianness or alignment; bounds-checked on every read.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) noexcept : data_(data) {}

    std::uint8_t  u8()    { need(1); return data_[pos_++]; }
    std::uint16_t u16le() {
        need(2);
        auto v = static_cast<std::uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2; return v;
    }
    std::uint32_t u32le() {
        need(4);
        std::uint32_t v = std::uint32_t(data_[pos_])
                        | std::uint32_t(data_[pos_ + 1]) << 8
                        | std::uint32_t(data_[pos_ + 2]) << 16
                        | std::uint32_t(data_[pos_ + 3]) << 24;
        pos_ += 4; return v;
    }
    std::int8_t  s8()    { return static_cast<std::int8_t>(u8()); }
    std::int16_t s16le() { return static_cast<std::int16_t>(u16le()); }   // well-defined in C++20
    std::int32_t s32le() { return static_cast<std::int32_t>(u32le()); }

    std::span<const std::uint8_t> bytes(std::size_t n) {
        need(n); auto s = data_.subspan(pos_, n); pos_ += n; return s;
    }
    // Fixed-width NUL-padded field; raw bytes preserved (Shift-JIS safe).
    std::string fixedStr(std::size_t len) {
        auto b = bytes(len);
        std::size_t n = 0; while (n < b.size() && b[n] != 0) ++n;
        return { reinterpret_cast<const char*>(b.data()), n };
    }
    void seek(std::size_t off) { if (off > data_.size()) throw std::out_of_range("ByteReader::seek"); pos_ = off; }
    void skip(std::size_t n)   { need(n); pos_ += n; }
    [[nodiscard]] std::size_t tell()      const noexcept { return pos_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }

private:
    void need(std::size_t n) const {
        if (n > data_.size() - pos_) throw std::out_of_range("ByteReader: read past end");
    }
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

// ------------------------------------------------- Virtual file system ----
namespace vfs {
    std::string normalize(std::string_view legacyPath);              // "C:\\GAME\\DATA\\MAP.DAT" -> "game/data/map.dat"
    // Game data, imported from the player's discs (game/import): <data>/<variant>/
    // first ("jp" / "tw", the files that differ between the releases), then
    // <data>/common/.
    std::vector<std::uint8_t> readAsset(std::string_view legacyPath);
    bool assetExists(std::string_view legacyPath);
    std::string assetPath(std::string_view legacyPath);
    const std::string& dataRoot();
    void setDataRoot(std::string root);
    void setVariant(std::string_view variant);
    const std::string& variant();
    // A folder under <data> tried before the variant (a language pack's), or empty.
    void setOverlay(std::string_view dir);
    // Sees each file readAsset returns, by normalized path (a pack's images).
    void setAssetFilter(std::function<void(const std::string&, std::vector<std::uint8_t>&)> filter);
    // readAsset returns these bytes from now on instead of the file's; a new filter forgets them.
    void cacheAsset(const std::string& normalizedPath, std::vector<std::uint8_t> bytes);
    // Files that ship with the port: resources/ (fonts, the importer's patch).
    std::vector<std::uint8_t> readResource(std::string_view name);
    const std::string& resourceRoot();
    std::optional<std::vector<std::uint8_t>> readSave(std::string_view name);
    void writeSave(std::string_view name, std::span<const std::uint8_t> data);
    bool saveStorageReady();                                         // IDBFS hydration is async on web

    // Save folder files by exact name (no case folding), e.g. the original's
    // "SaveData\\*.sav". CH_SAVEDIR replaces the save root on desktop.
    struct SaveFile { std::string name; std::int64_t mtime = 0; };   // mtime: seconds since the epoch
    std::vector<SaveFile> listSaveFolder(std::string_view folder);
    std::optional<std::vector<std::uint8_t>> readSaveFile(std::string_view folder, std::string_view name);
    void writeSaveFile(std::string_view folder, std::string_view name, std::span<const std::uint8_t> data);
    bool removeSaveFile(std::string_view folder, std::string_view name);
    bool saveFileExists(std::string_view folder, std::string_view name);
}

// ---------------------------------------------------- Texture helpers ----
// DirectDraw 16bpp RGB565 surface (+ optional DDCKEY source color key).
TexturePtr textureFromRGB565(SDL_Renderer* r, std::span<const std::uint8_t> le565,
                             int w, int h, std::optional<std::uint16_t> colorKey);
// 8bpp palettized surface (palette as 0xAARRGGBB).
TexturePtr textureFromIndexed8(SDL_Renderer* r, std::span<const std::uint8_t> indices,
                               std::span<const std::uint32_t, 256> paletteARGB,
                               int w, int h, std::optional<std::uint8_t> keyIndex);

// ----------------------------------------------------------- Input --------
enum class Action : std::uint8_t { Up, Down, Left, Right, Confirm, Cancel, Menu, Skip, Count };   // Skip = Space (DIK 0x39)

// Bit layout of the legacy DirectInput-derived masks (1 = L, 2 = R, 4 = M).
enum MouseButton : std::uint32_t { kMouseLeft = 1u, kMouseRight = 2u, kMouseMiddle = 4u };

// Sampled once per logic tick, exactly like the original per-frame DIMOUSESTATE poll.
struct MouseState {
    std::int32_t  x = 0, y = 0;   // logical canvas coordinates
    std::uint32_t held     = 0;   // buttons down this tick
    std::uint32_t pressed  = 0;   // (prev ^ cur) & cur
    std::uint32_t released = 0;   // (prev ^ cur) & prev
    std::int32_t  wheel    = 0;   // wheel notches this tick, positive = away from the user (a port addition)
};

class InputState {
public:
    [[nodiscard]] bool held(Action a)    const noexcept { return held_[idx(a)]; }
    [[nodiscard]] bool pressed(Action a) const noexcept { return latched_[idx(a)]; } // edge, survives until a logic tick consumes it
    [[nodiscard]] const MouseState& mouse() const noexcept { return mouse_; }
    // Touch screen gestures this tick (engine/touch_gestures.hpp). By default one
    // finger also works the mouse above: tap = left click, long-press = right
    // click, drag = left drag. setTouchMouse(false) leaves the mouse alone.
    [[nodiscard]] const touch::Gestures& touch() const noexcept { return touch_.gestures(); }
    void setTouchMouse(bool on) { touch_.setEmulateMouse(on); }
    [[nodiscard]] bool touchMouse() const noexcept { return touch_.emulateMouse(); }
    // With the stand-in off, a UI turns the gestures into mouse steps itself;
    // they reach mouse() from the next tick on, as the stand-in's do.
    void queueMouse(const touch::MouseStep& s) { touch_.addMouseStep(s); }
    // Text entry (the original used the IME edit box, FUN_00425a10..): while
    // enabled, typed UTF-8 text and Backspace / Enter presses collect per tick.
    struct TextInput { std::string typed; int backspaces = 0; bool enter = false; bool selectAll = false; };   // selectAll: Ctrl+A
    [[nodiscard]] const TextInput& text() const noexcept { return text_; }
    void setTextInput(bool on) noexcept;
private:
    friend class Engine;
    void onText(const char* utf8) { if (textOn_) text_.typed += utf8; }
    // CH_TEXT: "^M" presses Enter, "^A" Ctrl+A, the rest is typed (while text entry is on).
    void injectText(const std::string& s) {
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '^' && i + 1 < s.size() && (s[i + 1] == 'M' || s[i + 1] == 'A')) {
                if (s[i + 1] == 'M') latched_[idx(Action::Confirm)] = true;   // Enter is Confirm too
                if (textOn_) (s[i + 1] == 'M' ? text_.enter : text_.selectAll) = true;
                ++i;
            } else if (textOn_) {
                text_.typed.push_back(s[i]);
            }
        }
    }
    void onKey(SDL_Scancode sc, bool down) noexcept;
    void onMouseMove(int x, int y) noexcept { mouse_.x = x; mouse_.y = y; }
    void onMouseButton(std::uint8_t sdlButton, bool down) noexcept;
    void onWheel(int notches) noexcept { wheel_ += notches; }
    void beginTick() noexcept;
    void applyTouch();   // the touch's next mouse steps, up to one tick's worth
    void injectClick() noexcept { mouseLatch_ |= kMouseLeft; }
    void consumeTick() noexcept { latched_.fill(false); mouseLatch_ = 0; text_ = {}; touch_.endTick(); }
    void releaseAll()  noexcept { held_.fill(false); latched_.fill(false); mouseDown_ = mouseLatch_ = 0; touch_.cancel(); }
    static constexpr std::size_t idx(Action a) noexcept { return static_cast<std::size_t>(a); }
    std::array<bool, static_cast<std::size_t>(Action::Count)> held_{}, latched_{};
    std::uint32_t mouseDown_ = 0, mouseLatch_ = 0, mousePrev_ = 0;
    std::int32_t  wheel_ = 0;
    MouseState    mouse_;
    TextInput     text_;
    bool          textOn_ = false;
    touch::Recognizer touch_;
};

// ------------------------------------------------------------ Engine ------
// How the logicalW x logicalH frame fills the window. All but Stretch keep the
// aspect and leave black bars (pillarbox on a wide window, letterbox on a tall one).
enum class ScaleMode : std::uint8_t {
    Smooth,    // as large as fits, bilinear
    Sharp,     // as large as fits, nearest-neighbour
    Integer,   // the largest whole multiple that fits, nearest; fits like Sharp below 1x
    Stretch,   // fills the window, aspect ignored
    Count
};

struct EngineConfig {
    std::string title    = "Legacy Port";
    int         logicalW = 640;          // replace with DDraw SetDisplayMode() values
    int         logicalH = 480;
    int         windowScale = 2;
    int         windowW = 0, windowH = 0;   // window size in points; 0: logical size x windowScale
    double      tickHz   = 60.0;         // replace with original logic rate
    ScaleMode   scaleMode = ScaleMode::Integer;
    std::string orgName  = "legacy-port";
    std::string appName  = "game";
};

class Engine;

// One piece of a split view (Engine::setPieces): `src` of a layer's frame
// lands on `dst` in the window's output (pixels). Layer 0 is the scene's own
// frame; the others are transparent where nothing was drawn.
struct ViewPiece {
    SDL_Rect     src{};
    SDL_FRect    dst{};
    std::uint8_t layer = 0;
    bool         input = true;   // the pointer can be on it (off: it only shows)
};

class IScene {
public:
    virtual ~IScene() = default;
    virtual void onEnter(Engine&) {}
    virtual void fixedUpdate(Engine&, double dt) = 0;               // dt == 1/tickHz, constant
    virtual void render(Engine&, SDL_Renderer*, double alpha) = 0;  // alpha in [0,1) for optional interpolation
};

class Engine {
public:
    explicit Engine(EngineConfig cfg);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void setScene(std::unique_ptr<IScene> s) noexcept { pendingScene_ = std::move(s); } // deferred to frame boundary
    void run();
    void requestQuit() noexcept { running_ = false; }

    [[nodiscard]] SDL_Renderer*       renderer()  const noexcept { return renderer_.get(); }
    [[nodiscard]] const InputState&   input()     const noexcept { return input_; }
    [[nodiscard]] InputState&         input()           noexcept { return input_; }
    [[nodiscard]] const EngineConfig& config()    const noexcept { return cfg_; }
    [[nodiscard]] double              tickDt()    const noexcept { return 1.0 / cfg_.tickHz; }
    [[nodiscard]] std::uint64_t       tickCount() const noexcept { return tickCount_; }

    // Files dropped onto the window (or handed over by the web page), oldest first.
    void dropFile(std::string path) { dropped_.push_back(std::move(path)); }
    [[nodiscard]] std::vector<std::string> takeDroppedFiles() { return std::exchange(dropped_, {}); }
    // A key that runs `fn` whatever the scene (handled before the scene sees input).
    void setHotkey(SDL_Scancode key, std::function<void()> fn) { hotkeys_.emplace_back(key, std::move(fn)); }
    // Fills the screen, keeping the aspect (applyScaling). In a browser it only
    // takes effect during a key or mouse press, so call it from one.
    // Fullscreen is the desktop's resolution, the frame scaled per scaleMode.
    void setFullscreen(bool on);
    [[nodiscard]] bool fullscreen() const noexcept {
        return (SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    }
    // The windowed size in points, centred; while fullscreen it applies on leaving.
    void setWindowSize(int w, int h);
    void setScaleMode(ScaleMode m) noexcept { cfg_.scaleMode = m; }
    // Window sizes worth offering: common ones that fit the desktop, plus the current one.
    [[nodiscard]] std::vector<SDL_Point> windowSizes() const;
    // Where the frame lands in the window's output, in pixels (high-DPI aware).
    [[nodiscard]] SDL_Rect frameRect() const;
    // A point in window coordinates (mouse events), or normalized 0..1 (touch
    // fingers), as logical coordinates, kept inside the frame.
    [[nodiscard]] SDL_Point logicalFromWindow(float x, float y) const;
    [[nodiscard]] SDL_Point logicalFromNormalized(float x, float y) const;

    // ---- Split view (a port addition, for wide windows and phones): while a
    // scene keeps pieces set, the window shows those instead of the one frame,
    // and the pointer maps back through the piece it is on. A press on a
    // layer 0 piece stays with it until the button (or finger) lifts, kept
    // inside its `src`. Off every piece the pointer is on layer -1, at the
    // nearest point of the first piece. The scene sets the pieces each tick it
    // wants them (GameSystem clears them before the tasks run).
    static constexpr int kLayers = 3;
    void setPieces(std::vector<ViewPiece> pieces) { pieces_ = std::move(pieces); }
    [[nodiscard]] bool split() const noexcept { return !pieces_.empty(); }
    [[nodiscard]] SDL_Texture* layerTarget(int layer);          // 1..kLayers-1, made when first asked for
    [[nodiscard]] int pointerLayer() const noexcept { return split() ? pointerLayer_ : 0; }
    [[nodiscard]] SDL_FPoint pointerOutput() const noexcept { return pointerOut_; }   // output pixels
    [[nodiscard]] bool pointerIsTouch() const noexcept { return pointerTouch_; }      // a finger moved it last
    [[nodiscard]] SDL_Point outputSize() const;
    // ---- Side margins (a port addition, for a wider battle view): the frame
    // grows by `m` logical pixels at each side, so logical x runs -m ..
    // logicalW + m - 1 and x 0 stays where it was. The scene sets it each tick
    // it wants them (GameSystem clears it before the tasks run); the frame's
    // target follows at the next render.
    static constexpr int kMaxMargin = 171;                       // 4:3 out to 16:9
    void setMargin(int m) noexcept { margin_ = std::clamp(m, 0, kMaxMargin); }
    [[nodiscard]] int margin() const noexcept { return margin_; }
    [[nodiscard]] int frameW() const noexcept { return cfg_.logicalW + 2 * margin_; }
    // Puts the pointer on a logical point now and again as the window settles after a
    // resize (it moves and changes size under the pointer), for half a second.
    void keepMouseAt(int x, int y);

private:
    static void tickThunk(void* self);
    void warpMouse();
    SDL_Point     keepMouse_{};
    std::uint32_t keepMouseUntil_ = 0;   // SDL_GetTicks() deadline, 0 when none
    void frame();
    // The pointer at a normalized window point, as logical coordinates; notes where it is.
    // grab: 1 a press there, -1 the last button or finger lifted, 0 a move.
    SDL_Point pointerAt(float nx, float ny, int grab, bool touch);
    std::vector<ViewPiece>   pieces_;
    std::optional<ViewPiece> grab_;
    std::array<TexturePtr, kLayers - 1> layers_;
    std::array<int, kLayers - 1> layerW_{};          // each one's width when made
    int        margin_ = 0, frameTexW_ = 0;          // the margin now, and the frame target's width
    void makeFrame();
    int        pointerLayer_ = 0;
    SDL_FPoint pointerOut_{};
    bool       pointerTouch_ = false;
    int        fingersDown_ = 0;

    static constexpr double kMaxFrameSec      = 0.25;  // tab-switch / breakpoint guard
    static constexpr int    kMaxStepsPerFrame = 8;     // spiral-of-death guard

    EngineConfig            cfg_;
    WindowPtr               window_;
    RendererPtr             renderer_;
    TexturePtr              frame_;   // frameW() x logicalH target the scene draws into, then scaled to the window
    std::unique_ptr<IScene> scene_, pendingScene_;
    InputState              input_;
    std::uint64_t           perfFreq_ = 1, lastCounter_ = 0, tickCount_ = 0;
    double                  accumulator_ = 0.0;
    bool                    running_ = false;
    std::vector<std::string> dropped_;
    std::vector<std::pair<SDL_Scancode, std::function<void()>>> hotkeys_;

    // Debug hooks (environment): CH_SHOTS="tick,tick,..." saves shot_<tick>.bmp
    // after rendering that tick; CH_AUTOCLICK=N clicks every N ticks;
    // CH_MOUSE=x,y[@tick];... parks the pointer there (from that tick on);
    // CH_TOUCH works one finger.
    std::vector<std::uint64_t> shotTicks_;
    std::uint64_t              autoClick_ = 0;
    struct MouseStep { std::uint64_t at; SDL_Point pt; int button; };   // 1/-1 left press/release, 2/-2 right
    std::vector<MouseStep>     mouseScript_;   // CH_MOUSE
    bool                       mouseScriptWindow_ = false;   // CH_MOUSE_WINDOW: its points are window coordinates, mapped like real mouse events
    std::vector<std::pair<std::uint64_t, std::string>> textScript_;   // CH_TEXT=text@tick;...
    struct KeyStep { std::uint64_t at; SDL_Scancode key; };
    std::vector<KeyStep>       keyScript_;     // CH_KEYS=scancode@tick;...
    struct TouchStep { std::uint64_t at; SDL_Point pt; int kind; };     // 1 down, 0 move, -1 up
    std::vector<TouchStep>     touchScript_;   // CH_TOUCH=x,y@tick:d;x,y@tick:m;x,y@tick:u (one finger, output pixels)
};

} // namespace legacy
