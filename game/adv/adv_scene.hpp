// ============================================================================
// adv_scene.hpp — ADV (story) scene: YbcTask + message windows
//
//   scene 7  FUN_0049f4e0  WeekStartTask: advance week, fade BGM, CH001.ybc -> WorkRoom
//   scene 6  FUN_004d1a80  AdvTask: tick the YBC VM until the script ends
//            (FUN_004d1820 creates it, FUN_004d1910 its constructor;
//            FUN_004d1a40 -> FUN_004d1960 -> FUN_004d19e0 its destructor,
//            which runs FUN_004cdd00)
//
// Message system (ADV work area offsets in brackets):
//   10 windows   [+0x6f60, 0xe0 each]  frame + panel + face, FUN_004ca1b0 / FUN_004c99e0
//   10 buffers   [+0x430, 0xab8 each]  up to 10 text lines, FUN_004cb220
//   speaker      [+0x1cc]               selected by opcode 0x2b from DAT_00624520
// Pictures and effects:
//   screen fade                          opcode 0x38, FUN_004c76b0 / FUN_004c7770
//   backgrounds  [+0x1ce, 0x12 each] x5  opcode 0x3b, FUN_004c6e50 / FUN_004c7190
//   layers       [+0x228, 0x1a each] x20 opcodes 0x43/0x45/0x46/0x48/0x49/0x4a/0x4e/0x4f,
//                                         drawn by FUN_004cba60 from bank 0x15
//   choice menu  [+0x7820]               opcode 0x3d, FUN_004c7cd0 / FUN_004c7f80 / FUN_004c8290
//   voice                                opcode 0x41, FUN_004c45c0
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "ybc_vm.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ch {

class WeekStartTask final : public Task {
public:
    explicit WeekStartTask(GameContext& ctx) : ctx_(ctx) {}   // FUN_0049f410
    ~WeekStartTask() override = default;  // FUN_0049f460 / FUN_0049f4a0: nothing of its own
    Co<> start(std::uint32_t) override;   // FUN_0049f4e0
private:
    GameContext& ctx_;
};

class AdvTask final : public Task, private IYbcHost {
public:
    explicit AdvTask(GameContext& ctx);
    // Run inside another scene (AllResult's MVP script, FUN_004ce210 +
    // FUN_004ce8f0 called from that task): no scene change at the end,
    // *done is set instead.
    AdvTask(GameContext& ctx, bool* done) : AdvTask(ctx) { embeddedDone_ = done; }
    // A script chunk already in memory (the battle's events, FUN_004549d0):
    // opcodes the message system does not know go to `extension`, which
    // also answers their waits and the end-of-script BGM stop.
    AdvTask(GameContext& ctx, std::vector<std::uint8_t> code, IYbcHost* extension, bool* done)
        : AdvTask(ctx) { embeddedDone_ = done; code_ = std::move(code); extension_ = extension; }
    ~AdvTask() override;
    Co<> start(std::uint32_t) override;   // FUN_004d1a80

private:
    bool* embeddedDone_ = nullptr;
    std::vector<std::uint8_t> code_;
    IYbcHost* extension_ = nullptr;
    static constexpr std::uint16_t kUiBank = 0x14;   // data\adv\bin\Gybc_00.bin
    static constexpr std::uint16_t kLayerBank = 0x15;   // opcode 0x43 picture
    static constexpr std::uint16_t kBgBank = 0x30;      // + slot * 2 + buffer
    static constexpr int kTextPx = 16;               // the default 0x14 cell height as an em size

    // The original's window object has one-line accessors the setters here
    // write through directly: FUN_004c9520 (in use), FUN_004ca060 (x, y),
    // FUN_004ca0b0 (frame fade length), FUN_004ca0f0 (frame delay: count 0,
    // length), FUN_004ca130 (four fields, -1 keeping one), FUN_004ca720
    // (panel x, y), FUN_004ca960 (text x, y), FUN_004c9610 (face fade
    // length), FUN_004c9650 (face delay), and the face buffer +0x32:
    // FUN_004ca9b0 reads it, FUN_004ca9e0 tests it for 0, FUN_004caa20
    // flips it.
    struct Window {                  // 0xe0-byte object at +0x6f60
        std::uint8_t  index = 0;                     // position in windows_ (bank ids derive from it)
        bool          used = false;                  // +0
        std::uint16_t state = 0, prevState = 0;      // +2, +4: 1 closed, 2 open, 3 in, 4 out, 5 hold, 6/7 delayed in/out
        std::uint16_t cnt = 0;                       // +6
        std::int16_t  frameType = 0, nextFrameType = 0;   // +8, +10
        std::int16_t  x = 0, y = 0;                  // +0xc, +0xe
        std::uint16_t fadeLen = 0;                   // +0x10
        std::int16_t  delayCnt = 0, delayLen = 0;    // +0x12, +0x14
        std::int16_t  panelX = 0, panelY = 0, panelW = 0, panelH = 0;   // +0x16..+0x1c
        std::int16_t  textX = 0, textY = 0;          // +0x1e, +0x20
        std::uint16_t faceState = 0, faceCnt = 0, faceLen = 0;          // +0x22..+0x26
        std::int16_t  faceDelayCnt = 0, faceDelayLen = 0;               // +0x28, +0x2a
        std::int16_t  faceX = 0, faceY = 0;          // +0x2c, +0x2e
        std::uint16_t faceKind = 0;                  // +0x30
        std::uint8_t  faceBuf = 0;                   // +0x32
        std::array<std::int16_t, 2> faceId{-1, -1};  // +0x34
        // What each face bank holds (kind << 16 | id), -1 empty; a face
        // already in its bank is not decoded again.
        std::array<std::int32_t, 2> faceLoaded{-1, -1};
        std::array<std::int16_t, 2> expr{};          // +0x38
        std::array<std::int16_t, 2> prevExpr{};      // +0x3c
        std::int16_t  icon = 0, iconX = 0, iconY = 0;   // +0x4c, +0x56, +0x58: name plate on / centre
        std::string   name;                          // +0x5c texture (FUN_004ca770), UTF-8
    };
    struct Line {                    // 0x110-byte line at buffer +0x18
        std::uint16_t state = 0;     // 0 off, 1 shown, 2 queued, 3 fading in
        std::int16_t  fadeCnt = 0, fadeLen = 0, holdCnt = 0, holdLen = 0;
        std::uint16_t done = 0;
        std::string   text;          // UTF-8
        std::uint16_t px = kTextPx;
    };
    // The buffer's getters pick the custom value when its flag is set:
    // FUN_004cafa0 the reveal time, FUN_004cb080 the hold time,
    // FUN_004cb180 / FUN_004cb1d0 the font width / height.
    struct Buffer {                  // 0xab8-byte buffer at +0x430
        std::uint16_t reveal = 0x18, revealCustom = 0; bool useRevealCustom = false;   // +0, +2, +4
        std::uint16_t hold = 0xc, holdCustom = 0;      bool useHoldCustom = false;     // +6, +8, +10
        // FUN_004cb0d0 (from the buffer's constructor): 10 x 0x14, the
        // CreateFontA width and height.
        std::int16_t  fontW = 10, fontWc = 0, fontH = 0x14, fontHc = 0; bool useFontCustom = false;   // +0xc..+0x14
        std::uint16_t count = 0;                                                         // +0x16
        std::array<Line, 10> lines{};
    };

    struct ScreenFade {              // FUN_004c76b0 / FUN_004c7770
        bool          active = false;
        std::uint16_t mode = 0;      // 0 black, 1/100 white, 2 clear, 3/5 from black/white, 4/6 to black/white
        std::uint16_t cnt = 0, len = 0;
    };
    // Its accessors, inlined here: FUN_004c6dd0 (+0, the slot's index),
    // FUN_004c6d70 / FUN_004c6da0 (+1 on / off), FUN_004c6e00 (+4 / +6),
    // FUN_004c7030 (+0xc), FUN_004c7070 / FUN_004c70a0 / FUN_004c70e0 (+2:
    // read, is 0, flip), FUN_004c7130 (reset, resetAll()). The script
    // object's constructor and destructor (FUN_004cdbe0 / FUN_004cdd00) build
    // and drop its members with FUN_004c7650, FUN_004c8f00, FUN_004c6d40 and
    // FUN_004c7680, and the 10 windows (0x298 bytes, 5 sprites each) with
    // FUN_004c7b90 / FUN_004c7be0 / FUN_004c7c30 / FUN_004c7c80; here the
    // members' own initialisers.
    struct BgSlot {                  // 0x12 bytes at +0x1ce
        bool          used = false;  // +1
        std::uint8_t  buf = 0;       // +2: buffer shown (the other holds the previous picture)
        std::int16_t  x = 0, y = 0;  // +4, +6
        std::uint8_t  state = 0;     // +8: 0 off, 1 shown, 2 fade in, 3 fade out, 4 from white, 5 to white, 6 crossfade
        std::int16_t  cnt = 0;       // +10
        std::uint16_t len = 0;       // +0xc
        std::array<std::uint16_t, 2> picture{0xFFFF, 0xFFFF};   // +0xe: kBgNames index per buffer
    };
    // The layer's setters: FUN_004cb820 (free), FUN_004cb9f0 (kind),
    // FUN_004cb970 (frame), FUN_004cb9b0 (priority), FUN_004cb850 (x, y),
    // FUN_004cb890 (r, g, b), FUN_004cb8e0 (alpha), FUN_004cb910 (source
    // rect), FUN_004cba20 (scale).
    struct Layer {                   // 0x1a bytes at +0x228
        bool          used = false;  // +0
        std::uint8_t  kind = 0;      // +1: 1 plain, 2 sub-rect, 3 sub-rect scaled, 4 sub-rect tinted, 5 tinted
        std::uint16_t frame = 0, priority = 0;          // +2, +4
        std::int16_t  x = 0, y = 0;                     // +6, +8
        std::uint8_t  r = 0x80, g = 0x80, b = 0x80, a = 0xFF;   // +0xa..+0xd
        std::int16_t  sx = 0, sy = 0, sw = 0, sh = 0;   // +0xe..+0x14
        std::int16_t  scaleX = 100, scaleY = 100;       // +0x16, +0x18
    };
    struct ChoiceItem {              // 0x298 bytes at choice +0x18
        std::uint16_t state = 0;     // 1 delay, 2 fade in, 3 shown, 4 delay, 5 fade out, 6 done
        std::int16_t  x = 0, y = 0, w = 0, h = 0;       // +0x1a..+0x20
        std::int16_t  delay = 0, cnt = 0, inLen = 0x10, outLen = 0x10;   // +0x22..+0x28
        std::vector<std::string> lines;                 // +0x2a count, UTF-8
    };
    struct Hotspots {                // +0x9230, opcode 0x34 (FUN_004c4280)
        std::uint32_t rec = 0;       // +0x9230; areas at rec+0x14, 5 words each
        bool          hasDest = false;           // +0x9244: rec+8 is not the -1 operand
        bool          rightJump = false, outsideJump = false;   // +0x9245 / +0x9246
        std::uint32_t rightTarget = 0, outsideTarget = 0;       // +0x923c / +0x9240
    };
    struct Choice {                  // +0x7820
        std::uint32_t dest = 0;      // record offset of the destination operand
        bool          windowed = true;                   // +0x1a0a
        std::uint16_t state = 0;     // +8: 1 delay, 2 fade in, 3 shown, 4 delay, 5 fade out, 6 done, 7 no window
        std::int16_t  delay = 0, cnt = 0, inLen = 0x10, outLen = 0x10;   // +0xc..+0x12
        std::int16_t  nextX = 0, nextY = 0;             // +0x14, +0x16
        std::vector<ChoiceItem> items;                  // +0xa count
    };

    // IYbcHost
    std::vector<std::uint8_t> loadFile(std::string_view legacyPath) override;
    void exec(YbcVm& vm, std::uint16_t op, std::uint32_t rec) override;
    bool waitDone(std::uint16_t op) override;
    bool skipWait(std::uint16_t op) override;
    void scriptEnded() override { ctx_.audio->voiceStop(); if (extension_) extension_->scriptEnded(); }

    // window / face control (FUN_004cc600, cc0f0, cc810, cc910, cc420, cc540)
    Window& windowOf(int speaker);
    void setupWindow(int speaker);
    void setFace(int speaker, int face, int expr);
    // (FUN_004cc7d0, FUN_004cc8d0, FUN_004cc3e0 and FUN_004cc500 are these
    // four with no delay.)
    void openWindow(int speaker, int delay);
    void closeWindow(int speaker, int delay);
    void showFace(int speaker, int delay);
    void hideFace(int speaker, int delay);
    void swapFaces(int a, int b);                        // FUN_004cc9f0
    void resetAll();                                     // opcode 0x15

    // message buffer (FUN_004cade0, FUN_004cabd0, FUN_004cd5b0)
    Buffer* currentBuffer();
    void clearBuffer(Buffer& b);
    void addText(const std::string& sjis);

    // per-frame drawing (tail of FUN_004ce8f0)
    void drawFrame(Window& w);                           // FUN_004ca1b0
    void drawFrameParts(int x, int y, int type, int alpha, int prio);   // FUN_004ca600
    void drawText(Window& w, Buffer& b);                 // FUN_004cd4d0 + FUN_004cb220
    void tickFace(Window& w);                            // FUN_004c99e0
    void loadFace(Window& w, std::uint8_t buf);          // FUN_004cbf90
    void drawFaceBase(const Window& w, int alpha, std::uint8_t buf, int prio);             // FUN_004c9690
    void drawFaceExpr(const Window& w, int alpha, int expr, std::uint8_t buf, int prio);   // FUN_004c97a0
    [[nodiscard]] static std::uint16_t faceBank(const Window& w, std::uint8_t buf) {
        return static_cast<std::uint16_t>(0x16 + w.index * 2 + buf);   // window +0x40 / +0x42
    }
    void drawCursor(int x, int y, int sx, int sy, int w, int h, int prio);   // FUN_004d29e0 mode 1

    // pictures and effects
    void setFade(std::uint16_t mode);                    // FUN_004c76b0
    void drawFade();                                     // FUN_004c7770
    [[nodiscard]] bool fadeIdle() const;                 // FUN_004c7ab0
    void setBackground(int slot, int picture, int mode); // FUN_004c5810 + FUN_004c6e50
    void drawBackground(int slot);                       // FUN_004c7190
    [[nodiscard]] bool backgroundsIdle() const;          // FUN_004cd1e0
    void drawLayer(const Layer& l);                      // FUN_004cba60
    void playVoice(int group, int index);                // FUN_004c45c0
    void startChoice(YbcVm& vm, std::uint32_t rec);      // FUN_004c52a0 (+ FUN_004c7cd0 / FUN_004c7f80)
    [[nodiscard]] bool tickChoice();                     // FUN_004c8290
    [[nodiscard]] bool tickHotspots();                   // FUN_004c8f60
    void revealAll();                                    // FUN_004cb6a0 x10
    void stopVoice() { ctx_.audio->voiceStop(); }        // FUN_0041e500 on the voice stream
    void drawFrameAll();

    [[nodiscard]] bool windowsIdle() const;              // FUN_004cd060
    [[nodiscard]] bool facesIdle() const;                // FUN_004cd120
    [[nodiscard]] bool buffersRevealed() const;          // FUN_004cd350
    // FUN_004ccb30 hands the waits the mouse's held and pressed buttons.
    [[nodiscard]] bool clicked() const { return (ctx_.mouse().pressed & 7) != 0; }

    GameContext&            ctx_;
    YbcVm                   vm_;
    std::array<Window, 10>  windows_{};
    std::array<Buffer, 10>  buffers_{};
    ScreenFade              fade_{};
    std::array<BgSlot, 5>   backgrounds_{};
    std::array<Layer, 0x14> layers_{};
    Layer& layer(int i) { return layers_[static_cast<std::size_t>(i < 0 ? 0 : i > 0x13 ? 0x13 : i)]; }
    Choice                  choice_{};
    Hotspots                hotspots_{};
    std::uint32_t           waitTarget_ = 0;             // slot +0x2c: opcode 0x0e click target
    std::array<bool, 0x38>  bankLoaded_{};       // +0x92a0: banks this task loaded (freed on exit)
    std::int8_t             speakerWindow_ = -1;   // +0x1cc
    std::int16_t            cursorX_ = 0, cursorY_ = 0;   // slot +0x34 / +0x36
    std::int16_t            cursorCnt_ = 0, cursorFrame_ = 0;   // slot +0x30 / +0x32
    std::uint16_t           waitCnt_ = 0, waitLen_ = 0;       // slot +0x28 / +0x2a
    std::int16_t            bgmNext_ = 0;                     // +0x92d8
    bool                    tabHeld_ = false;                 // debug skip key, for its press edge
    bool                    playerTurn_ = false;              // the skip stopped at a choice / hotspots
    bool                    pendingFaces_ = false;            // faces set while skipping, not loaded yet
    std::set<std::uint16_t> unported_;
};

} // namespace ch
