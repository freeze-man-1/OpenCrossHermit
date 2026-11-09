// ============================================================================
// adv_scene.cpp
// ============================================================================
#include "adv_scene.hpp"
#include "adv_data.hpp"
#include "../state/game_logic.hpp"
#include "../text/sjis.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace ch {


namespace {
// Speaker record fields (DAT_00624520, s16[19]).
enum Spk { kWin, kX, kY, kFaceOn, kFaceKind, kFaceX, kFaceY, kTextX, kTextY, kCurX, kCurY,
           kIconOn, kIconX, kIconY, kPanelX, kPanelY, kPanelW, kPanelH, kFrameType };

// FUN_004cd850: min(max * cnt / len, max)
int lerp(int cnt, int len, int max) {
    if (len <= 0) return max;
    return std::min(max, max * (cnt & 0xFFFF) / (len & 0xFFFF));
}

// FUN_004cd5b0: '\\' plus one byte breaks the line. Returns UTF-8 lines.
std::vector<std::string> splitLines(const std::string& sjis) {
    std::vector<std::string> parts(1);
    for (std::size_t i = 0; i < sjis.size();) {
        if (sjis[i] == '\\') { parts.emplace_back(); i += 2; continue; }
        const bool dbl = static_cast<unsigned char>(sjis[i]) >= 0x80 && i + 1 < sjis.size();
        parts.back().append(sjis, i, dbl ? 2 : 1);
        i += dbl ? 2 : 1;
    }
    for (auto& p : parts) p = gameTextToUtf8(p);
    return parts;
}

// FUN_004d0860: ADV SE list (PTR_s_data_Sound_plw_ARSND_SE_LUP00_wa_00624aa8),
// SE group 2, played by opcode 0x99.
constexpr std::array<std::string_view, 7> kAdvSePaths{
    "data\\Sound\\plw\\ARSND\\SE-LUP00.wav", "data\\Sound\\plw\\ARSND\\SE-SKUP00.wav",
    "data\\Sound\\plw\\ARSND\\SE-SUP00.wav", "data\\Sound\\plw\\ARSND\\SE-041.wav",
    "data\\Sound\\plw\\ARSND\\SE-042.wav", "data\\Sound\\plw\\ADVSND\\SE-028.wav",
    "data\\Sound\\plw\\ADVSND\\SE-029.wav"};
constexpr std::uint32_t kAdvSeGroup = 2;

const adv_tables::Speaker& speaker(GameVersion v, int i) {
    const auto& s = adv_tables::layout(v).speakers;
    return s[static_cast<std::size_t>(std::clamp(i, 0, int(s.size()) - 1))];
}
} // namespace

// =============================================================== NewGame ===
// FUN_0049f4e0 (+ the new-game work reset done before it by the title).
Co<> WeekStartTask::start(std::uint32_t) {
    logic::advanceWeek(ctx_.work, ctx_.version);     // FUN_004d3510
    ctx_.audio->bgmFadeOut(0x5A);
    while (!ctx_.audio->bgmFadeFinished()) co_await yieldFrame();
    ctx_.audio->bgmStop();                         // FUN_004db120
    ctx_.adv = AdvRequest{"Data\\Adv\\dat\\CH001.ybc", 8};   // FUN_004ce210
    // Debug: CH_ADV=<file>[:entry] runs another script from data\adv\dat.
    if (const char* v = SDL_getenv("CH_ADV")) {
        const std::string arg = v;
        const auto colon = arg.find(':');
        ctx_.adv = AdvRequest{"Data\\Adv\\dat\\" + arg.substr(0, colon),
                              colon == std::string::npos ? -1 : std::atoi(arg.c_str() + colon + 1)};
    }
    ctx_.scenes.request(SceneId::AdvScript);       // FUN_00439e30(6)
}

// =================================================================== Adv ===
// FUN_004cdbe0 builds the parts: the 5 backgrounds (FUN_004c6d00), 20
// layers (FUN_004cb750 -> FUN_004cb7f0: free), 10 buffers (FUN_004caa70:
// 10 lines each, FUN_004cab00, none in use, the font FUN_004cb0d0), 10
// windows (FUN_004c9460 -> FUN_004c9550: not in use, FUN_004c9590), the
// choice menu, the fade; FUN_004cdd00 takes them down again (FUN_004cab80
// / FUN_004cab40 the lines, FUN_004c94e0 the windows, FUN_004cb790 the
// layers and FUN_004c8f30, which do nothing more).
AdvTask::AdvTask(GameContext& ctx) : ctx_(ctx), vm_(*this, ctx.work) {
    for (std::size_t i = 0; i < windows_.size(); ++i) windows_[i].index = static_cast<std::uint8_t>(i);
}

// FUN_004d0750 -> FUN_004d0790: every script slot ends (FUN_004ce090),
// the pictures' banks go (FUN_004cded0: the 0x38 loaded ones, unless the
// task keeps them, +0x92e1) and the ADV sounds (group 2).
AdvTask::~AdvTask() {           // FUN_004d0750
    ctx_.audio->voiceStop();
    ctx_.sprites->unloadBank(kUiBank);
    for (std::size_t bank = 0; bank < bankLoaded_.size(); ++bank)
        if (bankLoaded_[bank]) ctx_.sprites->unloadBank(static_cast<std::uint16_t>(bank));
    for (const Window& w : windows_)
        for (std::uint8_t b = 0; b < 2; ++b) ctx_.sprites->unloadBank(faceBank(w, b));
}

std::vector<std::uint8_t> AdvTask::loadFile(std::string_view legacyPath) {
    return ctx_.readData(legacyPath);
}

// FUN_004d1a80
Co<> AdvTask::start(std::uint32_t) {
    // FUN_004d0860: UI atlas into bank 0x14 (FUN_004cde10), ADV SE list into group 2.
    ctx_.sprites->loadBank(kUiBank, ctx_.readData("data\\adv\\bin\\Gybc_00.bin"));
    ctx_.audio->seLoadGroup(kAdvSeGroup, kAdvSePaths);
    resetAll();
    if (!code_.empty()) {
        vm_.loadCode(std::move(code_), 0x7f);
    } else if (ctx_.adv.path.empty()) {
        SDL_Log("AdvTask: no script queued");
        ctx_.scenes.request(SceneId::Title);
        co_return;
    } else {
        vm_.load(ctx_.adv.path, ctx_.adv.entry);
    }

    for (;;) {
        // Debug: Tab runs the script on to its end (or the next choice)
        // without waiting, every opcode and flag still executed.
        // CH_AUTOSKIP does the same at every script's start and after every
        // choice is answered (for unattended test runs).
        const bool tab = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] != 0;
        const bool autoSkip = SDL_getenv("CH_AUTOSKIP") != nullptr && !playerTurn_;
        if (((tab && !tabHeld_) || autoSkip) && !vm_.fastForward()) {
            if (!autoSkip) SDL_Log("AdvTask: skipping ahead");
            stopVoice();
            vm_.setFastForward(true);
        }
        tabHeld_ = tab;
        // The chapter select (scene 0x16) has no way out in the original: Esc leaves a chapter for
        // the select's menus, and those menus (menu001-008, SubChack) for the title.
        if (vm_.entry() == static_cast<std::int32_t>(SceneId::AdvMenu) && embeddedDone_ == nullptr &&
            ctx_.engine.input().pressed(legacy::Action::Menu)) {
            stopVoice();
            std::string name = vm_.path().substr(vm_.path().find_last_of('\\') + 1);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (name.starts_with("menu") || name.starts_with("subchack")) {
                ctx_.scenes.request(SceneId::Title);
                co_return;
            }
            break;
        }
        bool running = vm_.tick();
        // At most a few thousand steps a frame: a script that polls in a loop
        // (waiting on the battle) moves on a frame at a time.
        for (int n = 0; running && vm_.fastForward() && n < 4000; ++n) running = vm_.tick();
        if (pendingFaces_ && !vm_.fastForward()) {
            pendingFaces_ = false;
            for (Window& w : windows_)
                if (w.used) for (std::uint8_t b = 0; b < 2; ++b) if (w.faceId[b] >= 0) loadFace(w, b);
        }
        drawFrameAll();
        if (static int beat = 0; SDL_getenv("CH_YBCTRACE") && ++beat % 120 == 0)
            for (const Window& w : windows_)
                if (w.used) SDL_Log("adv: window %d state %d face %d", w.index, w.state, w.faceState);
        if (!running) break;
        co_await yieldFrame();
    }

    if (embeddedDone_ != nullptr) { *embeddedDone_ = true; co_return; }
    // FUN_004d1a80: DAT_007e0f04 is this task's +0x92dc, the entry value the
    // script was loaded with (0x7f chains keep it): the scene to run next.
    if (vm_.entry() != -1) {
        SDL_Log("AdvTask: script ended, next scene 0x%02x", unsigned(vm_.entry()));
        ctx_.scenes.request(static_cast<SceneId>(vm_.entry()));
    } else {
        // The original requests nothing here and leaves a black screen;
        // return to the title instead.
        SDL_Log("AdvTask: script ended without a next scene, back to title");
        ctx_.scenes.request(SceneId::Title);
    }
}

// -------------------------------------------------------------- opcodes ---
void AdvTask::exec(YbcVm& vm, std::uint16_t op, std::uint32_t rec) {
    auto ev = [&](std::uint32_t off) { return vm.value(rec, off); };
    switch (op) {
        // FUN_004c2670 / FUN_004c2730 (FUN_004c1e20: the slot's wait, N frames)
        case 0x0c: case 0x0d: case 0x0e:   // wait N frames (0x0d/0x0e: or click)
            waitCnt_ = 0;
            waitLen_ = static_cast<std::uint16_t>(ev(4));
            if (op == 0x0e) waitTarget_ = vm.script().u32(vm.script().block(0) + rec + 8);   // FUN_004c27f0
            break;
        case 0x0f: case 0xac:              // FUN_004c21f0 / FUN_004c22b0: the VM loads the next script
            break;
        case 0x11: {                       // FUN_004c4b80: text lines into the speaker's buffer
            if (speakerWindow_ == -1) break;
            addText(vm.script().text(vm.field16(rec, 4)));
            break;
        }
        case 0x14:                         // FUN_004c5500: the byte at +0x9228, which nothing reads
            break;
        case 0x15: resetAll(); break;      // FUN_004c5590
        case 0x16: case 0x17:              // FUN_004c59a0 / FUN_004c5ab0 (0x17 waits)
            setupWindow(ev(4)); setFace(ev(4), ev(8), ev(0xc));
            break;
        case 0x18:                         // FUN_004c5bc0: 0/1 open, 2/3 close; 1 and 3 wait
        case 0x23: {                       // FUN_004c66e0: the same for the face
            const int how = ev(8);
            if (how < 0 || how > 3) break;   // the dispatch's 3: held until released
            const bool show = how <= 1;
            if (op == 0x18) { if (show) openWindow(ev(4), 0); else closeWindow(ev(4), 0); }   // FUN_004cc7d0 / FUN_004cc8d0
            else if (show) showFace(ev(4), 0); else hideFace(ev(4), 0);                     // FUN_004cc3e0 / FUN_004cc500
            vm.setWait(how & 1);
            break;
        }
        case 0x1a:                         // FUN_004c5d40
            setupWindow(ev(8)); setFace(ev(8), ev(0xc), ev(0x10));
            closeWindow(ev(4), 0); hideFace(ev(4), 0);
            break;
        case 0x1b: setupWindow(ev(4)); setFace(ev(4), ev(8), ev(0xc)); openWindow(ev(4), 0); break;   // FUN_004c5ea0
        case 0x1c: closeWindow(ev(4), 0); break;                                                    // FUN_004c5fc0
        case 0x1d: case 0x1e:              // FUN_004c6050 / FUN_004c61c0
            setupWindow(ev(4)); setFace(ev(4), ev(8), ev(0xc));
            showFace(ev(4), 0); openWindow(ev(4), ev(0x10));
            break;
        case 0x1f: case 0x20: closeWindow(ev(4), 0); hideFace(ev(4), ev(8)); break;   // FUN_004c6330 / FUN_004c6400
        case 0x21: setFace(ev(4), ev(8), ev(0xc)); break;                             // FUN_004c64d0
        case 0x22: case 0x29:              // FUN_004c65d0 / FUN_004c6b60: set the face and show it
            setFace(ev(4), ev(8), ev(0xc)); showFace(ev(4), 0);
            break;
        case 0x2a: hideFace(ev(4), 0); break;                                         // FUN_004c6c70
        case 0x25: case 0x26: swapFaces(ev(4), ev(8)); break;   // FUN_004c6860 / FUN_004c6920
        case 0x27: case 0x28: setFace(ev(4), -1, ev(8)); break;   // FUN_004c69e0 / FUN_004c6aa0: expression only
        case 0x2b: {                       // FUN_004c4dc0: select speaker
            const auto& r = speaker(ctx_.version, ev(4));
            speakerWindow_ = static_cast<std::int8_t>(r[kWin]);
            cursorX_ = r[kCurX];
            cursorY_ = r[kCurY];
            break;
        }
        case 0x2c: if (Buffer* b = currentBuffer()) clearBuffer(*b); break;   // FUN_004c4eb0
        case 0x2d: if (Buffer* b = currentBuffer()) {   // FUN_004c4f30 -> FUN_004caf50
            const int v = ev(4);
            if (v == -1) b->useRevealCustom = false;
            else { b->revealCustom = static_cast<std::uint16_t>(v); b->useRevealCustom = true; }
            break;
        }
        case 0x2e: if (Buffer* b = currentBuffer()) {   // FUN_004c4ff0 -> FUN_004cb030 (resets +4, as the original)
            const int v = ev(4);
            if (v == -1) b->useRevealCustom = false;
            else { b->holdCustom = static_cast<std::uint16_t>(v); b->useHoldCustom = true; }
            break;
        }
        case 0x2f: if (Buffer* b = currentBuffer()) {   // FUN_004c50b0 -> FUN_004cb120
            const int w = ev(4), h = ev(8);
            if (w == -1) b->useFontCustom = false;
            else { b->fontWc = static_cast<std::int16_t>(w); b->fontHc = static_cast<std::int16_t>(h); b->useFontCustom = true; }
            break;
        }
        case 0x30: if (Buffer* b = currentBuffer()) b->useFontCustom = false; break;   // FUN_004c51a0: FUN_004cb120(-1, -1)
        case 0x33: cursorCnt_ = 0; cursorFrame_ = 0; break;                            // FUN_004c4210
        case 0x3f:                         // FUN_004c44d0: BGM change, fade out, then play
            bgmNext_ = static_cast<std::int16_t>(ev(4));
            ctx_.audio->bgmFadeOut(0x5A);
            break;
        case 0x40: ctx_.audio->bgmFadeOut(0x5A); break;   // FUN_004c4570
        case 0x42: stopVoice(); break;                    // FUN_004c46f0 -> FUN_0041e500 on the voice stream
        case 0x98:                         // FUN_004c4840 -> FUN_0040d390: SE (group +8, index +4, slot +0xc)
            ctx_.audio->sePlayIn(static_cast<std::uint32_t>(ev(8)), static_cast<std::uint32_t>(ev(4)), ev(0xc));
            break;
        case 0x9a: ctx_.audio->seStopSlot(ev(4)); break;   // FUN_004c4a60 -> FUN_0040d4d0
        case 0x9b: ctx_.audio->seStopAll(); break;         // FUN_004c4af0 -> FUN_0040d610
        case 0xa8:                         // FUN_004c43e0 (FUN_004c1e20): a wait, cursor or timed by DAT_007f4491
            waitCnt_ = 0;
            waitLen_ = static_cast<std::uint16_t>(ev(4));
            cursorCnt_ = 0; cursorFrame_ = 0;
            break;
        case 0x34: {                       // FUN_004c4280: clickable areas
            const auto word = [&](std::uint32_t off) { return vm.script().u32(vm.script().block(0) + rec + off); };
            hotspots_ = Hotspots{.rec = rec, .hasDest = (word(8) >> 28) != 3,
                                 .rightJump = word(0xc) != 0xFFFFFFFFu, .outsideJump = word(0x10) != 0xFFFFFFFFu,
                                 .rightTarget = word(0xc), .outsideTarget = word(0x10)};
            break;
        }
        case 0x90: logic::joinRoster(ctx_.work, ctx_.version, ev(4), -1, -1); break;   // FUN_004d0900 -> FUN_004d3e90
        case 0x91: logic::leaveRoster(ctx_.work, ev(4)); break;                         // FUN_004d0990 -> FUN_004d4250
        case 0x99:                         // FUN_004c4950: ADV SE (group 2, index +8, slot +4)
            ctx_.audio->sePlayIn(kAdvSeGroup, static_cast<std::uint32_t>(ev(8)), ev(4));
            break;
        case 0x37: case 0x38: setFade(static_cast<std::uint16_t>(ev(4))); break;   // FUN_004c53a0 / FUN_004c5430 (0x38 waits)
        case 0x3a: case 0x3b: setBackground(ev(4), ev(8), ev(0xc)); break;          // FUN_004c56c0 / FUN_004c5810 (0x3b waits)
        case 0x3d: startChoice(vm, rec); break;                               // FUN_004c52a0
        case 0x41: playVoice(vm.field16(rec, 4), vm.field16(rec, 6)); break;  // FUN_004c45c0
        case 0x43: {                       // FUN_004c28b0: block-1 path into bank 0x15
            const std::string path = vm.script().name(vm.field16(rec, 4));
            try {
                ctx_.sprites->loadBank(kLayerBank, ctx_.readData(path));   // FUN_004cde10
                bankLoaded_[kLayerBank] = true;
            } catch (const std::exception& ex) {
                SDL_Log("adv: picture %s: %s", path.c_str(), ex.what());
            }
            break;
        }
        case 0x45: case 0x46: {            // FUN_004c2af0 / FUN_004c2d80: sub-rect layer (0x46 scaled)
            Layer& l = layers_[static_cast<std::size_t>(std::clamp(ev(4), 0, 0x13))];
            l.used = true;
            l.kind = op == 0x45 ? 2 : 3;
            l.x = static_cast<std::int16_t>(ev(8));        l.y = static_cast<std::int16_t>(ev(0xc));
            l.frame = static_cast<std::uint16_t>(ev(0x10)); l.priority = static_cast<std::uint16_t>(ev(0x14));
            l.sx = static_cast<std::int16_t>(ev(0x18));    l.sy = static_cast<std::int16_t>(ev(0x1c));
            l.sw = static_cast<std::int16_t>(ev(0x20));    l.sh = static_cast<std::int16_t>(ev(0x24));
            if (op == 0x46) { l.scaleX = static_cast<std::int16_t>(ev(0x28)); l.scaleY = static_cast<std::int16_t>(ev(0x2c)); }
            break;
        }
        case 0x48: {                       // FUN_004c3400: tinted layer
            Layer& l = layers_[static_cast<std::size_t>(std::clamp(ev(4), 0, 0x13))];
            l.used = true;
            l.kind = 5;
            l.x = static_cast<std::int16_t>(ev(8));        l.y = static_cast<std::int16_t>(ev(0xc));
            l.frame = static_cast<std::uint16_t>(ev(0x10)); l.priority = static_cast<std::uint16_t>(ev(0x14));
            l.a = static_cast<std::uint8_t>(ev(0x18));
            l.r = static_cast<std::uint8_t>(ev(0x1c)); l.g = static_cast<std::uint8_t>(ev(0x20)); l.b = static_cast<std::uint8_t>(ev(0x24));
            break;
        }
        case 0x44: {                       // FUN_004c2950: plain layer
            Layer& l = layer(ev(4));
            l.used = true;
            l.kind = 1;                    // FUN_004cb9f0
            l.x = static_cast<std::int16_t>(ev(8));        l.y = static_cast<std::int16_t>(ev(0xc));   // FUN_004cb850
            l.frame = static_cast<std::uint16_t>(ev(0x10)); l.priority = static_cast<std::uint16_t>(ev(0x14));   // FUN_004cb970 / FUN_004cb9b0
            break;
        }
        case 0x47: {                       // FUN_004c3080: tinted sub-rect layer
            Layer& l = layer(ev(4));
            l.used = true;
            l.kind = 4;
            l.x = static_cast<std::int16_t>(ev(8));        l.y = static_cast<std::int16_t>(ev(0xc));
            l.frame = static_cast<std::uint16_t>(ev(0x10)); l.priority = static_cast<std::uint16_t>(ev(0x14));
            l.sx = static_cast<std::int16_t>(ev(0x18));    l.sy = static_cast<std::int16_t>(ev(0x1c));   // FUN_004cb910
            l.sw = static_cast<std::int16_t>(ev(0x20));    l.sh = static_cast<std::int16_t>(ev(0x24));
            l.a = static_cast<std::uint8_t>(ev(0x28));     // FUN_004cb8e0
            l.r = static_cast<std::uint8_t>(ev(0x2c)); l.g = static_cast<std::uint8_t>(ev(0x30)); l.b = static_cast<std::uint8_t>(ev(0x34));   // FUN_004cb890
            break;
        }
        // One field of a layer: FUN_004c37b0 frame, FUN_004c3890 priority,
        // FUN_004c3970 scale (FUN_004cba20), FUN_004c3ce0 alpha, FUN_004c3dc0
        // the source rectangle.
        case 0x4b: layer(ev(4)).frame = static_cast<std::uint16_t>(ev(8)); break;
        case 0x4c: layer(ev(4)).priority = static_cast<std::uint16_t>(ev(8)); break;
        case 0x4d: {
            Layer& l = layer(ev(4));
            l.scaleX = static_cast<std::int16_t>(ev(8)); l.scaleY = static_cast<std::int16_t>(ev(0xc));
            break;
        }
        case 0x50: layer(ev(4)).a = static_cast<std::uint8_t>(ev(8)); break;
        case 0x51: {
            Layer& l = layer(ev(4));
            l.sx = static_cast<std::int16_t>(ev(8));  l.sy = static_cast<std::int16_t>(ev(0xc));
            l.sw = static_cast<std::int16_t>(ev(0x10)); l.sh = static_cast<std::int16_t>(ev(0x14));
            break;
        }
        case 0x49: layers_[static_cast<std::size_t>(std::clamp(ev(4), 0, 0x13))].used = false; break;   // FUN_004c3690
        case 0x4a: for (Layer& l : layers_) l.used = false; break;                                      // FUN_004c3730
        case 0x4e: {                       // FUN_004c3a80: move layer
            Layer& l = layers_[static_cast<std::size_t>(std::clamp(ev(4), 0, 0x13))];
            l.x = static_cast<std::int16_t>(ev(8)); l.y = static_cast<std::int16_t>(ev(0xc));
            break;
        }
        case 0x4f: {                       // FUN_004c3b90: tint layer
            Layer& l = layers_[static_cast<std::size_t>(std::clamp(ev(4), 0, 0x13))];
            l.r = static_cast<std::uint8_t>(ev(8)); l.g = static_cast<std::uint8_t>(ev(0xc)); l.b = static_cast<std::uint8_t>(ev(0x10));
            break;
        }
        case 0x97:                         // FUN_004c4750: area BGM id; 0 = fade the BGM out
            ctx_.work.setAreaBgm(ev(4));
            ctx_.work.setAreaBgmOn(ev(8) != 0);
            if (ev(8) == 0) ctx_.audio->bgmFadeOut(0x5A);
            break;
        default:
            // The battle's opcodes (0x6c..0xa7, FUN_0042d1a0 onwards) call into
            // the battle (FUN_00497xxx..FUN_00499xxx); its StageScript is the
            // extension. Outside a battle they have nothing to act on.
            if (extension_) extension_->exec(vm, op, rec);
            else if (unported_.insert(op).second) SDL_Log("ybc: opcode 0x%02x not ported yet", op);
            break;
    }
}

// The fast-forward's end of each wait: what the wait would have done when it
// finished; the player's choices and hotspots stop it.
bool AdvTask::skipWait(std::uint16_t op) {
    switch (op) {
        case 0x34: case 0x3d: playerTurn_ = true; return false;
        case 0x32: revealAll(); return true;
        case 0x3f: ctx_.audio->bgmStop(); ctx_.audio->bgmPlay(static_cast<std::uint32_t>(bgmNext_), 1); return true;
        case 0x40: case 0x97: ctx_.audio->bgmStop(); return true;
        default: return extension_ ? extension_->skipWait(op) : true;
    }
}

bool AdvTask::waitDone(std::uint16_t op) {
    switch (op) {
        case 0x0c: return std::uint16_t(waitLen_ - 1) < waitCnt_++ || waitLen_ == 0;
        case 0x0d: case 0x0e: {            // FUN_004ccb90: Space or a click ends it; 0x0e's click jumps
            bool done = false;
            if (ctx_.skipHeld()) { done = true; stopVoice(); }
            if (clicked()) {
                if (op == 0x0e) vm_.jump(waitTarget_);
                done = true;
            }
            return std::uint16_t(waitLen_ - 1) < waitCnt_++ || waitLen_ == 0 || done;
        }
        case 0x17: case 0x1e: case 0x20: return windowsIdle() && facesIdle();
        case 0x18: case 0x19: case 0x1b: case 0x1c: return windowsIdle();
        case 0x22: case 0x23: case 0x24: case 0x26: case 0x28: case 0x29: case 0x2a: return facesIdle();
        case 0x31:                         // FUN_004cd2a0: every buffer done (FUN_004cb5f0: no line still showing)
            return buffersRevealed();
        case 0x32: {                       // FUN_004cd350: a left click or Space reveals everything
            if ((ctx_.mouse().pressed & legacy::kMouseLeft) || ctx_.skipHeld()) {
                revealAll();
                stopVoice();
                return true;
            }
            return buffersRevealed();
        }
        case 0x34: return !(playerTurn_ = !tickHotspots());
        case 0x38: case 0x39: return fadeIdle();
        case 0x3b: case 0x3c: return backgroundsIdle();
        case 0x3d: return !(playerTurn_ = !tickChoice());
        case 0xa8:                         // the cursor wait (0x33), or the timed one (0x0d) when DAT_007f4491 is set
            if (ctx_.work.u8(stage::kNetwork) != 0) {
                bool done = false;
                if (ctx_.skipHeld()) { done = true; stopVoice(); }
                if (clicked()) done = true;
                return std::uint16_t(waitLen_ - 1) < waitCnt_++ || waitLen_ == 0 || done;
            }
            [[fallthrough]];
        case 0x33: {                       // FUN_004cce20: blinking cursor, any button
            if (speakerWindow_ != -1 && windows_[std::size_t(speakerWindow_)].state == 2) {
                if (5 < cursorCnt_++) { cursorFrame_ = (cursorFrame_ + 1) & 3; cursorCnt_ = 0; }
                drawCursor(cursorX_, cursorY_, 0x1A8 + cursorFrame_ * 0x12, 0, 0x12, 0x18, 0x36);
            }
            if (ctx_.skipHeld()) { stopVoice(); return true; }
            return clicked();
        }
        case 0x3f:
            if (!ctx_.audio->bgmFadeFinished()) return false;
            ctx_.audio->bgmPlay(static_cast<std::uint32_t>(bgmNext_), 1);   // FUN_004db010
            return true;
        case 0x40: case 0x97:
            if (!ctx_.audio->bgmFadeFinished()) return false;
            ctx_.audio->bgmStop();
            return true;
        default:
            return extension_ ? extension_->waitDone(op) : true;   // unported waits finish at once
    }
}

// ------------------------------------------------------- window control ---
AdvTask::Window& AdvTask::windowOf(int spk) {
    return windows_[static_cast<std::size_t>(std::clamp<int>(speaker(ctx_.version, spk)[kWin], 0, 9))];
}

// FUN_004cc600
void AdvTask::setupWindow(int spk) {
    const auto& r = speaker(ctx_.version, spk);
    Window& w = windowOf(spk);
    w.faceKind = static_cast<std::uint16_t>(r[kFaceKind]);
    w.used = true;
    w.x = r[kX]; w.y = r[kY];
    w.textX = r[kTextX]; w.textY = r[kTextY];
    w.icon = r[kIconOn];
    w.panelX = r[kPanelX]; w.panelY = r[kPanelY]; w.panelW = r[kPanelW]; w.panelH = r[kPanelH];
    if (w.icon == 1) { w.iconX = r[kIconX]; w.iconY = r[kIconY]; }
    switch (w.state) {
        case 0: case 1: w.state = 1; w.cnt = 0; w.frameType = r[kFrameType]; w.fadeLen = 8; break;
        case 2:   // compares the frame type with the window id, as the original does
            if (w.frameType != r[kWin]) { w.state = 5; w.cnt = 0; w.nextFrameType = r[kFrameType]; }
            break;
        case 3: case 4: case 6: case 7: w.frameType = r[kFrameType]; break;
        case 5: w.nextFrameType = r[kFrameType]; break;
    }
}

// FUN_004cc0f0 (face pictures and the name plate are not drawn yet)
void AdvTask::setFace(int spk, int face, int expr) {
    const auto& r = speaker(ctx_.version, spk);
    Window& w = windowOf(spk);
    Buffer& b = buffers_[static_cast<std::size_t>(std::clamp<int>(r[kWin], 0, 9))];
    b.reveal = 0x18; b.useRevealCustom = false;   // FUN_004caf10(0x18)
    b.hold = 0xc;    b.useHoldCustom = false;     // FUN_004caff0(0xc)
    if (face == -1) face = w.faceId[w.faceBuf];
    if (r[kFaceOn] == 0) {
        w.faceLen = 8; w.faceState = 1; w.faceCnt = 0;
        return;
    }
    // FUN_004ca770: the plate shows the name of the face being set.
    const auto& names = adv_tables::names(ctx_.version);
    w.name = face >= 0 && face < int(names.size()) ? std::string(names[std::size_t(face)]) : std::string();
    w.faceX = r[kFaceX]; w.faceY = r[kFaceY]; w.faceKind = static_cast<std::uint16_t>(r[kFaceKind]);
    const auto f = static_cast<std::int16_t>(face), e = static_cast<std::int16_t>(expr);
    if (w.faceState == 2) {
        if (w.faceId[w.faceBuf] == f) {
            if (w.expr[w.faceBuf] != e) {
                w.faceLen = 0x14; w.faceState = 6; w.faceCnt = 0;
                w.prevExpr[w.faceBuf] = w.expr[w.faceBuf];
                w.expr[w.faceBuf] = e;
            }
        } else {
            w.faceLen = 0x28; w.faceState = 5; w.faceCnt = 0;
            w.faceBuf ^= 1;
            w.faceId[w.faceBuf] = f;
            w.expr[w.faceBuf] = e;
            loadFace(w, w.faceBuf);
        }
    } else {
        if (w.faceId[w.faceBuf] != f) {
            w.faceId[w.faceBuf] = f;
            loadFace(w, w.faceBuf);
        }
        w.expr[w.faceBuf] = e;
    }
}

// FUN_004cc9f0 (opcodes 0x25 / 0x26, FUN_004c6860 / FUN_004c6920): the two
// speakers trade the faces and expressions their windows show.
void AdvTask::swapFaces(int a, int b) {
    const Window& wa = windowOf(a);
    const int fa = wa.faceId[wa.faceBuf], ea = wa.expr[wa.faceBuf];
    const Window& wb = windowOf(b);
    const int fb = wb.faceId[wb.faceBuf], eb = wb.expr[wb.faceBuf];
    setFace(a, fb, eb);
    setFace(b, fa, ea);
}

// FUN_004cc810
void AdvTask::openWindow(int spk, int delay) {
    Window& w = windowOf(spk);
    if (w.state == 1 || w.state == 4 || w.state == 7) {
        w.fadeLen = 0x2d;
        w.delayCnt = 0; w.delayLen = static_cast<std::int16_t>(delay);
        w.state = delay == 0 ? 3 : 6; w.cnt = 0;
    }
}

// FUN_004cc910
void AdvTask::closeWindow(int spk, int delay) {
    Window& w = windowOf(spk);
    if (w.state == 2 || w.state == 3 || w.state == 5 || w.state == 6) {
        w.fadeLen = 0x2d;
        w.delayCnt = 0; w.delayLen = static_cast<std::int16_t>(delay);
        w.state = delay == 0 ? 4 : 7; w.cnt = 0;
    }
}

// FUN_004cc420
void AdvTask::showFace(int spk, int delay) {
    Window& w = windowOf(spk);
    const auto s = w.faceState;
    if (s == 0 || s == 1 || s == 4 || s == 8) {
        w.faceLen = 0x3c;
        w.faceDelayCnt = 0; w.faceDelayLen = static_cast<std::int16_t>(delay);
        w.faceState = delay == 0 ? 3 : 7; w.faceCnt = 0;
    }
}

// FUN_004cc540
void AdvTask::hideFace(int spk, int delay) {
    Window& w = windowOf(spk);
    const auto s = w.faceState;
    if (s == 2 || s == 3 || s == 5) {
        w.faceLen = 0x3c;
        w.faceDelayCnt = 0; w.faceDelayLen = static_cast<std::int16_t>(delay);
        w.faceState = delay == 0 ? 4 : 8; w.faceCnt = 0;
    }
}

// opcode 0x15: FUN_004c9590 x10, FUN_004cade0 + FUN_004cb120(-1, -1) x10
void AdvTask::resetAll() {
    for (Window& w : windows_) {
        const bool used = w.used;
        const std::uint8_t index = w.index;   // fixed slot: selects the face banks
        w = Window{};
        w.used = used;
        w.index = index;
    }
    for (Buffer& b : buffers_) { clearBuffer(b); b.useFontCustom = false; }
    for (BgSlot& b : backgrounds_) {              // FUN_004c7130 on each of the five
        b.state = 0;
        b.cnt = 0;
        b.picture = {0xFFFF, 0xFFFF};
        b.used = false;                           // FUN_004c6da0
    }
}

// --------------------------------------------------------------- buffers ---
AdvTask::Buffer* AdvTask::currentBuffer() {
    return speakerWindow_ < 0 ? nullptr : &buffers_[std::size_t(std::min<int>(speakerWindow_, 9))];
}

// FUN_004cade0
void AdvTask::clearBuffer(Buffer& b) {
    b.count = 0;
    for (Line& l : b.lines) {
        l = Line{};
        l.fadeLen = static_cast<std::int16_t>(b.useRevealCustom ? b.revealCustom : b.reveal);
        l.holdLen = static_cast<std::int16_t>(b.useHoldCustom ? b.holdCustom : b.hold);
    }
}

// FUN_004cd5b0 + FUN_004cabd0: '\' + one byte breaks the line; the last line
// gets the buffer's hold time, the others 0.
void AdvTask::addText(const std::string& sjis) {
    Buffer* b = currentBuffer();
    if (!b) return;
    std::vector<std::string> parts(1);
    for (std::size_t i = 0; i < sjis.size();) {
        if (sjis[i] == '\\') { parts.emplace_back(); i += 2; continue; }
        const bool dbl = static_cast<unsigned char>(sjis[i]) >= 0x80 && i + 1 < sjis.size();
        parts.back().append(sjis, i, dbl ? 2 : 1);
        i += dbl ? 2 : 1;
    }
    const int h = b->useFontCustom ? b->fontHc : b->fontH;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (b->count > 9) b->count = 9;
        Line& l = b->lines[b->count];
        l.state = 2;
        l.fadeCnt = 0;
        l.fadeLen = static_cast<std::int16_t>(b->useRevealCustom ? b->revealCustom : b->reveal);
        l.holdCnt = 0;
        l.holdLen = k + 1 == parts.size()
            ? static_cast<std::int16_t>(b->useHoldCustom ? b->holdCustom : b->hold) : std::int16_t{0};
        l.done = 0;
        l.text = gameTextToUtf8(parts[k]);
        // CreateFontA's height is the cell's; the renderer's size is the
        // em's, about four fifths of it (0x14 -> 16).
        l.px = static_cast<std::uint16_t>(h > 0 ? h * 4 / 5 : kTextPx);
        if (++b->count > 9) b->count = 9;
    }
}

// ----------------------------------------------------------- idle checks ---
bool AdvTask::windowsIdle() const {
    return std::none_of(windows_.begin(), windows_.end(),
                        [](const Window& w) { return w.used && w.state > 2 && w.state < 8; });
}

bool AdvTask::facesIdle() const {
    return std::none_of(windows_.begin(), windows_.end(),
                        [](const Window& w) { return w.used && w.faceState > 2 && w.faceState < 9; });
}

// FUN_004cd350: every buffer has finished revealing its last line.
bool AdvTask::buffersRevealed() const {
    return std::all_of(buffers_.begin(), buffers_.end(), [](const Buffer& b) {
        return b.count == 0 || b.lines[b.count - 1u].done == 1;
    });
}

// ---------------------------------------------------------------- drawing ---
void AdvTask::drawFrameAll() {
    // Mouse pointer from the UI atlas (FUN_004d29e0, prio 0x33).
    drawCursor(ctx_.mouse().x, ctx_.mouse().y, 0x1A8, 0x18, 0x14, 0x1E, 0x33);
    for (std::size_t i = 0; i < windows_.size(); ++i) {
        drawFrame(windows_[i]);
        tickFace(windows_[i]);
        drawText(windows_[i], buffers_[i]);
    }
    for (int slot = 0; slot < 5; ++slot)
        if (backgrounds_[std::size_t(slot)].used) drawBackground(slot);
    drawFade();
    for (const Layer& l : layers_) drawLayer(l);
}

void AdvTask::drawCursor(int x, int y, int sx, int sy, int w, int h, int prio) {
    ctx_.draw.sprite({.bank = kUiBank, .frame = 0, .x = x, .y = y,
                      .w = static_cast<std::int16_t>(w), .h = static_cast<std::int16_t>(h),
                      .src = {static_cast<std::int16_t>(sx), static_cast<std::int16_t>(sy),
                              static_cast<std::int16_t>(sx + w), static_cast<std::int16_t>(sy + h)},
                      .flags = kSprSrcRect | kSprBank, .blend = kBlendAlpha,
                      .priority = static_cast<std::uint16_t>(prio)});
}

// FUN_004ca600 -> FUN_004cbdb0 per part
void AdvTask::drawFrameParts(int x, int y, int type, int alpha, int prio) {
    const auto& frames = adv_tables::layout(ctx_.version).frames;
    if (type < 0 || type >= int(frames.size())) return;
    for (const auto& p : frames[std::size_t(type)]) {
        {
            ctx_.draw.sprite({.bank = kUiBank, .frame = 0, .x = x + p.dx, .y = y + p.dy, .w = p.w, .h = p.h,
                              .src = {p.sx, p.sy, static_cast<std::int16_t>(p.sx + p.w),
                                      static_cast<std::int16_t>(p.sy + p.h)},
                              .flags = kSprSrcRect | kSprBank, .blend = kBlendAlpha,
                              .color = {0x80, 0x80, 0x80, static_cast<std::uint8_t>(alpha)},
                              .priority = static_cast<std::uint16_t>(prio)});
        }
    }
}

// FUN_004ca1b0
void AdvTask::drawFrame(Window& w) {
    auto panel = [&](int alpha) {
        ctx_.draw.fill({.x = w.x + w.panelX, .y = w.y + w.panelY, .w = w.panelW, .h = w.panelH,
                        .priority = 0x43, .argb = std::uint32_t(alpha & 0xFF) << 24 | 0x223438u});
    };
    switch (w.state) {
        case 3: {
            drawFrameParts(w.x, w.y, w.frameType, lerp(w.cnt, w.fadeLen, 0xFF), 0x42);
            panel(lerp(w.cnt, w.fadeLen, 0xCC));
            if (w.fadeLen < ++w.cnt) { w.cnt = 0; w.state = 2; }
            break;
        }
        case 4: {
            drawFrameParts(w.x, w.y, w.frameType, 0xFF - lerp(w.cnt, w.fadeLen, 0xFF), 0x42);
            panel(0xCC - lerp(w.cnt, w.fadeLen, 0xCC));
            if (w.fadeLen < ++w.cnt) { w.cnt = 0; w.state = 1; }
            break;
        }
        case 5:
            if (w.fadeLen < ++w.cnt) { w.cnt = 0; w.state = 2; }
            break;
        case 6:
            if (w.delayLen < ++w.delayCnt) { w.delayCnt = 0; w.state = 3; }
            break;
        case 7:
            if (w.delayLen < ++w.delayCnt) { w.delayCnt = 0; w.state = 4; }
            [[fallthrough]];   // as in the original: still drawn open while waiting to close
        case 2:
            drawFrameParts(w.x, w.y, w.frameType, 0xFF, 0x42);
            panel(0xCC);
            if (w.icon == 1 && !w.name.empty())     // FUN_004ca860: name plate, 20 px bold
                ctx_.draw.text({.x = w.iconX, .y = w.iconY, .priority = 0x35, .argb = 0xFFFFFFFFu,
                                .text = w.name, .fontPx = 20, .bold = true, .centred = true});
            break;
        default:
            break;
    }
}

// FUN_004cbf90: data\adv\bin\ + sc###.bin (bust) or mc###.bin (portrait)
// (the path joined with FUN_004cd7b0 / FUN_004cd810, strcpy / strcat).
void AdvTask::loadFace(Window& w, std::uint8_t buf) {
    const int id = w.faceId[buf];
    const auto& faces = adv_tables::layout(ctx_.version);
    std::string_view file;
    if (w.faceKind == 0 && id >= 0 && id < int(faces.scFaces.size())) file = faces.scFaces[std::size_t(id)].file;
    if (w.faceKind == 1 && id >= 0 && id < int(faces.mcFaces.size())) file = faces.mcFaces[std::size_t(id)].file;
    if (file.empty()) { SDL_Log("adv: no face file for kind %u id %d", w.faceKind, id); return; }
    const std::int32_t key = std::int32_t(w.faceKind) << 16 | (id & 0xffff);
    if (w.faceLoaded[buf] == key) return;
    // While skipping ahead only the last face of each bank is loaded, when
    // the skip stops (pendingFaces_).
    if (vm_.fastForward()) { pendingFaces_ = true; return; }
    w.faceLoaded[buf] = key;
    try {
        ctx_.sprites->loadBank(faceBank(w, buf), ctx_.readData("data\\adv\\bin\\" + std::string(file)));
    } catch (const std::exception& ex) {
        SDL_Log("adv: face %.*s: %s", int(file.size()), file.data(), ex.what());
    }
}

void AdvTask::drawFaceBase(const Window& w, int alpha, std::uint8_t buf, int prio) {
    const std::int16_t fw = w.faceKind == 0 ? 0x100 : 0x200, fh = w.faceKind == 0 ? 0x128 : 0x300;
    ctx_.draw.sprite({.bank = faceBank(w, buf), .frame = 0, .x = w.faceX, .y = w.faceY, .w = fw, .h = fh,
                      .src = {0, 0, fw, fh}, .flags = kSprSrcRect | kSprBank, .blend = kBlendAlpha,
                      .color = {0x80, 0x80, 0x80, static_cast<std::uint8_t>(std::clamp(alpha, 0, 255))},
                      .priority = static_cast<std::uint16_t>(prio)});
}

void AdvTask::drawFaceExpr(const Window& w, int alpha, int expr, std::uint8_t buf, int prio) {
    const int id = w.faceId[buf];
    int x = w.faceX, y = w.faceY, frame = 0, sx = 0, sy = 0, pw = 0x80, ph = 0x80;
    if (w.faceKind == 0) {
        const auto& faces = adv_tables::layout(ctx_.version).scFaces;
        if (id < 0 || id >= int(faces.size()) || expr < 0 || expr > 7) return;
        const auto& f = faces[std::size_t(id)];
        const auto& e = f.expr[std::size_t(expr)];
        x += f.offX; y += f.offY; pw = f.partW; ph = f.partH;
        frame = e[0]; sx = e[1]; sy = e[2];
    } else {
        const auto& faces = adv_tables::layout(ctx_.version).mcFaces;
        if (id < 0 || id >= int(faces.size()) || expr < 0) return;
        const auto& f = faces[std::size_t(id)];
        x += f.x; y += f.y;
        sx = (expr & 3) << 7;
        sy = ((expr >> 2) << 7) + 0x300;
    }
    ctx_.draw.sprite({.bank = faceBank(w, buf), .frame = static_cast<std::uint16_t>(frame), .x = x, .y = y,
                      .w = static_cast<std::int16_t>(pw), .h = static_cast<std::int16_t>(ph),
                      .src = {static_cast<std::int16_t>(sx), static_cast<std::int16_t>(sy),
                              static_cast<std::int16_t>(sx + pw), static_cast<std::int16_t>(sy + ph)},
                      .flags = kSprSrcRect | kSprBank, .blend = kBlendAlpha,
                      .color = {0x80, 0x80, 0x80, static_cast<std::uint8_t>(std::clamp(alpha, 0, 255))},
                      .priority = static_cast<std::uint16_t>(prio)});
}

// FUN_004c99e0. Layer priorities: busts in front of the panel (0x3b..0x40),
// portraits behind the window (0x44..0x49).
void AdvTask::tickFace(Window& w) {
    const int p0 = w.faceKind == 0 ? 0x3b : 0x44;
    const int oldExpr = p0 + 1, oldBase = p0 + 2, prevExpr = p0 + 3, expr = p0 + 4, base = p0 + 5;
    const std::uint8_t cur = w.faceBuf, other = cur ^ 1;
    switch (w.faceState) {
        case 3: {   // fade in
            const int a = lerp(w.faceCnt, w.faceLen, 0xFF);
            drawFaceBase(w, a, cur, base);
            drawFaceExpr(w, a, w.expr[cur], cur, expr);
            if (w.faceLen < w.faceCnt++) { w.faceCnt = 0; w.faceState = 2; }
            break;
        }
        case 4: {   // fade out
            const int a = 0xFF - lerp(w.faceCnt, w.faceLen, 0xFF);
            drawFaceBase(w, a, cur, base);
            drawFaceExpr(w, a, w.expr[cur], cur, expr);
            if (w.faceLen < w.faceCnt++) { w.faceCnt = 0; w.faceState = 1; }
            break;
        }
        case 5: {   // crossfade to the other buffer
            const int a = lerp(w.faceCnt, w.faceLen, 0xFF);
            drawFaceBase(w, 0xFF - a, other, oldBase);
            drawFaceExpr(w, 0xFF - a, w.expr[other], other, oldExpr);
            drawFaceBase(w, a, cur, base);
            drawFaceExpr(w, a, w.expr[cur], cur, expr);
            if (w.faceLen < w.faceCnt++) { w.faceCnt = 0; w.faceState = 2; }
            break;
        }
        case 6: {   // expression change
            const int a = 0xFF - lerp(w.faceCnt, w.faceLen, 0xFF);
            drawFaceExpr(w, a, w.prevExpr[cur], cur, prevExpr);
            drawFaceExpr(w, 0xFF, w.expr[cur], cur, expr);
            drawFaceBase(w, 0xFF, cur, base);
            if (w.faceLen < ++w.faceCnt) { w.faceCnt = 0; w.faceState = 2; }
            break;
        }
        case 7:     // delayed fade in
            if (w.faceDelayLen < w.faceDelayCnt++) { w.faceDelayCnt = 0; w.faceState = 3; }
            break;
        case 8:     // delayed fade out: still shown while waiting
            if (w.faceDelayLen < w.faceDelayCnt++) { w.faceDelayCnt = 0; w.faceState = 4; }
            [[fallthrough]];
        case 2:
            drawFaceBase(w, 0xFF, cur, base);
            drawFaceExpr(w, 0xFF, w.expr[cur], cur, expr);
            break;
        default:
            break;
    }
}

// FUN_004cd4d0 + FUN_004cb220
void AdvTask::drawText(Window& w, Buffer& b) {
    if (w.state == 1) {
        if (w.prevState != 1) clearBuffer(b);
    } else if (w.state == 2) {
        int y = w.textY;
        for (std::uint16_t i = 0; i < b.count; ++i) {
            Line& l = b.lines[i];
            const bool prevDone = i == 0 || b.lines[i - 1u].done != 0;
            bool show = false;
            int alpha = 0xFF;
            switch (l.state) {
                case 1: show = true; break;
                case 2: if (prevDone) l.state = 3; break;
                case 3:
                    if (l.fadeLen < ++l.fadeCnt) { l.state = 1; l.fadeCnt = l.fadeLen; }
                    show = true;
                    alpha = l.fadeLen > 0 ? l.fadeCnt * 0xFF / l.fadeLen : 0xFF;
                    break;
                default: break;
            }
            if (show) {
                ctx_.draw.text({.x = w.textX, .y = y, .priority = 0x37,
                                .argb = std::uint32_t(std::clamp(alpha, 0, 255)) << 24 | 0xFFFFFFu,
                                .text = l.text, .fontPx = l.px});
                y += l.px;
            }
            if ((i == 0 || b.lines[i - 1u].done == 1) && l.holdLen < ++l.holdCnt) {
                l.holdCnt = l.holdLen;
                l.done = 1;
            }
        }
    }
    w.prevState = w.state;
}

// ------------------------------------------------------------ screen fade ---
// FUN_004c76b0. Fades run 0x3c frames, the static modes 0x10.
void AdvTask::setFade(std::uint16_t mode) {
    fade_ = {.active = mode != 2, .mode = mode, .cnt = 0,
             .len = static_cast<std::uint16_t>(mode >= 3 && mode <= 6 ? 0x3C : 0x10)};
}

bool AdvTask::fadeIdle() const {   // FUN_004c7ab0
    return !fade_.active || fade_.mode == 0 || fade_.mode == 2 || fade_.mode == 100;
}

// FUN_004c7770: full-screen black (alpha) or white (additive) at prio 0x34,
// in front of the pictures and behind the message windows.
void AdvTask::drawFade() {
    if (!fade_.active) return;
    std::uint8_t c = 1, a = 0xFF, blend = kBlendAlpha;
    auto step = [&](std::uint16_t next) {
        if (fade_.len < fade_.cnt++) fade_.mode = next;
    };
    switch (fade_.mode) {
        case 0:   break;
        case 1: case 100: c = 0xFF; blend = kBlendAdd; break;
        case 3: a = static_cast<std::uint8_t>(0xFF - lerp(fade_.cnt, fade_.len, 0xFF)); step(2); break;
        case 4: a = static_cast<std::uint8_t>(lerp(fade_.cnt, fade_.len, 0xFF));        step(0); break;
        case 5: c = 0xFF; blend = kBlendAdd;
                a = static_cast<std::uint8_t>(0xFF - lerp(fade_.cnt, fade_.len, 0xFF)); step(2); break;
        case 6: c = 0xFF; blend = kBlendAdd;
                a = static_cast<std::uint8_t>(lerp(fade_.cnt, fade_.len, 0xFF));        step(100); break;
        default:  return;
    }
    ctx_.draw.fill({.x = 0, .y = 0, .w = 0x400, .h = 0x300, .priority = 0x34,
                    .argb = std::uint32_t(a) << 24 | std::uint32_t(c) * 0x010101u, .blend = blend});
}

// ------------------------------------------------------------ backgrounds ---
// FUN_004c5810 + FUN_004c6e50. Modes: 0 hide, 1 show, 2 fade in, 3 fade out,
// 4 in from white, 5 out to white, 6 crossfade (fade in if nothing shown).
void AdvTask::setBackground(int slotIndex, int picture, int mode) {
    BgSlot& s = backgrounds_[static_cast<std::size_t>(std::clamp(slotIndex, 0, 4))];
    s.used = true;
    s.x = s.y = 0;
    s.cnt = 0;
    std::uint8_t cur = s.buf;
    bool change = static_cast<std::uint16_t>(picture) != s.picture[cur];
    switch (mode) {
        case 0:  change = false; s.len = 0; break;
        case 1:  s.len = 0; break;
        case 2: case 3: case 4: case 5: s.len = 0x3C; break;
        case 6:
            s.len = 0x3C;
            if (s.state == 0) mode = 2;
            else cur = s.buf ^= 1;
            break;
        default: break;
    }
    s.state = static_cast<std::uint8_t>(mode);
    if (!change) return;
    s.picture[cur] = static_cast<std::uint16_t>(picture);
    const auto& bgNames = adv_media::bgNames(ctx_.version);
    if (picture < 0 || picture >= int(bgNames.size())) {
        SDL_Log("adv: background %d out of range", picture);
        return;
    }
    const auto bank = static_cast<std::uint16_t>(kBgBank + cur + slotIndex * 2);
    const std::string path = "data\\adv\\bin\\" + bgNames[std::size_t(picture)];
    try {
        ctx_.sprites->loadBank(bank, ctx_.readData(path));   // FUN_004cde10
        bankLoaded_[bank] = true;
    } catch (const std::exception& ex) {
        SDL_Log("adv: background %s: %s", path.c_str(), ex.what());
    }
}

bool AdvTask::backgroundsIdle() const {   // FUN_004cd1e0
    return std::none_of(backgrounds_.begin(), backgrounds_.end(),
                        [](const BgSlot& s) { return s.used && s.state > 1 && s.state < 7; });
}

// FUN_004c7190 (each picture through FUN_004cbca0: the colour and alpha
// when given, -1 leaving them). Slot n draws at prio 0x4a + 2n; the previous picture of a
// crossfade sits one step behind. color > 0x80 brightens toward white.
void AdvTask::drawBackground(int slotIndex) {
    BgSlot& s = backgrounds_[std::size_t(slotIndex)];
    const std::uint8_t cur = s.buf, other = cur ^ 1;
    auto pic = [&](std::uint8_t buf, int prio, int alpha, int color) {
        ctx_.draw.sprite({.bank = static_cast<std::uint16_t>(kBgBank + buf + slotIndex * 2), .frame = 0,
                          .x = s.x, .y = s.y, .flags = kSprBank,
                          .blend = alpha >= 0 ? kBlendAlpha : kBlendTest,
                          .color = {static_cast<std::uint8_t>(color), static_cast<std::uint8_t>(color),
                                    static_cast<std::uint8_t>(color), static_cast<std::uint8_t>(alpha >= 0 ? alpha : 0xFF)},
                          .priority = static_cast<std::uint16_t>(prio)});
    };
    const int prio = 0x4A + slotIndex * 2;
    auto step = [&](std::uint8_t next) {
        if (s.len < static_cast<std::uint16_t>(++s.cnt)) { s.cnt = 0; s.state = next; }
    };
    switch (s.state) {
        case 1: pic(cur, prio, -1, 0x80); break;
        case 2: pic(cur, prio, lerp(s.cnt, s.len, 0xFF), 0x80); step(1); break;
        case 3: pic(cur, prio, 0xFF - lerp(s.cnt, s.len, 0xFF), 0x80); step(0); break;
        case 4: pic(cur, prio, 0xFF, 0xFF - lerp(s.cnt, s.len, 0x7F)); step(1); break;
        case 5: pic(cur, prio, 0xFF, lerp(s.cnt, s.len, 0x7F) + 0x80); step(0); break;
        case 6:
            pic(cur, prio, lerp(s.cnt, s.len, 0xFF), 0x80);
            pic(other, prio + 1, 0xFF, 0x80);
            step(1);
            break;
        default: break;
    }
}

// ----------------------------------------------------------------- layers ---
// FUN_004cba60 (FUN_004d2ba0 / 2ca0 / 2dd0 / 3050 / 30c0), bank 0x15.
void AdvTask::drawLayer(const Layer& l) {
    if (!l.used || l.kind < 1 || l.kind > 5) return;
    SpriteCmd c{.bank = kLayerBank, .frame = l.frame, .x = l.x, .y = l.y, .flags = kSprBank,
                .blend = kBlendAlpha, .color = {0x80, 0x80, 0x80, 0xFF}, .priority = l.priority};
    if (l.kind >= 2 && l.kind <= 4) {
        c.flags |= kSprSrcRect;
        c.w = l.sw; c.h = l.sh;
        c.src = {l.sx, l.sy, static_cast<std::int16_t>(l.sx + l.sw), static_cast<std::int16_t>(l.sy + l.sh)};
    }
    if (l.kind == 3) { c.flags |= kSprScale; c.scaleX = l.scaleX; c.scaleY = l.scaleY; }
    if (l.kind >= 4) c.color = {l.r, l.g, l.b, l.a};
    ctx_.draw.sprite(c);
}

// ------------------------------------------------------------------ voice ---
// FUN_004c45c0.
void AdvTask::playVoice(int group, int index) {
    if (ctx_.skipHeld() || vm_.fastForward()) return;   // FUN_004128f0(0x39, 0): no voice while fast-forwarding
    const std::string file = adv_media::voice(ctx_.version, group, index);
    if (file.empty()) return;
    ctx_.audio->voicePlay("data\\sound\\voice\\" + file);
}

// ------------------------------------------------------------ choice menu ---
// FUN_004c52a0 + FUN_004c7cd0 (reset) + FUN_004c7f80 (one item per text).
// Operands: +4 destination, +8 item count, +0xc.. text indices (raw words).
void AdvTask::startChoice(YbcVm& vm, std::uint32_t rec) {
    const auto& r = speaker(ctx_.version, 4);              // +0x1a09: record 4 unless +0x1a08 is set
    choice_ = Choice{};
    choice_.dest = rec;
    choice_.windowed = true;                 // DAT_007df1aa is never 2 in this build
    choice_.state = 1;
    choice_.nextX = r[kTextX];
    choice_.nextY = static_cast<std::int16_t>(r[kTextY] - 8);
    const std::uint32_t count = std::min<std::uint32_t>(vm.script().u32(vm.script().block(0) + rec + 8), 10);
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t textIndex = vm.script().u32(vm.script().block(0) + rec + 0xc + k * 4);
        ChoiceItem it;
        it.lines = splitLines(vm.script().text(textIndex));
        if (it.lines.size() > 10) it.lines.resize(10);
        it.x = choice_.nextX;
        it.y = choice_.nextY;
        it.w = 400;
        it.h = static_cast<std::int16_t>(int(it.lines.size()) * 0x16 - 2);
        it.state = 1;
        it.delay = 0x10;
        choice_.nextY = static_cast<std::int16_t>(choice_.nextY + int(it.lines.size()) * 0x16);
        choice_.items.push_back(std::move(it));
    }
}

// FUN_004c8290, polled while opcode 0x3d waits. Clicks count only once the
// window and every item are fully shown. Returns true when all has faded out.
bool AdvTask::tickChoice() {
    Choice& c = choice_;
    const auto& r = speaker(ctx_.version, 4);
    bool clickable = true, done = true;
    auto window = [&](int frameAlpha, int panelAlpha) {
        drawFrameParts(r[kX], r[kY], r[kFrameType], frameAlpha, 0x39);
        ctx_.draw.fill({.x = r[kX] + r[kPanelX], .y = r[kY] + r[kPanelY], .w = r[kPanelW], .h = r[kPanelH],
                        .priority = 0x3A, .argb = std::uint32_t(panelAlpha & 0xFF) << 24 | 0x010101u});
    };
    switch (c.state) {
        case 1: clickable = done = false; if (c.delay-- == 0) c.state = 2; break;
        case 2:
            clickable = done = false;
            window(lerp(c.cnt, c.inLen, 0xFF), lerp(c.cnt, c.inLen, 0xCC));
            if (c.inLen < c.cnt++) { c.cnt = 0; c.delay = 0x10; c.state = 3; }
            break;
        case 3: done = false; window(0xFF, 0xCC); break;
        case 4: clickable = done = false; window(0xFF, 0xCC); if (c.delay-- == 0) c.state = 5; break;
        case 5:
            clickable = done = false;
            window(0xFF - lerp(c.cnt, c.inLen, 0xFF), 0xCC - lerp(c.cnt, c.inLen, 0xCC));
            if (c.outLen < c.cnt++) { c.cnt = 0; c.state = 6; }
            clickable = false;
            break;
        case 6: clickable = false; break;
        default: break;
    }

    for (ChoiceItem& it : c.items) {
        int alpha = -1;
        switch (it.state) {
            case 1: clickable = done = false; if (it.delay-- == 0) it.state = 2; break;
            case 2:
                clickable = done = false;
                alpha = lerp(it.cnt, it.inLen, 0xFF);
                if (it.inLen < it.cnt++) { it.cnt = 0; it.delay = 0; it.state = 3; }
                break;
            case 3: done = false; alpha = 0xFF; break;
            case 4: clickable = done = false; alpha = 0xFF; if (it.delay-- == 0) it.state = 5; break;
            case 5:
                clickable = done = false;
                alpha = 0xFF - lerp(it.cnt, it.outLen, 0xFF);
                if (it.outLen < it.cnt++) { it.cnt = 0; it.state = 6; }
                break;
            case 6: clickable = false; break;
            default: break;
        }
        if (alpha < 0) continue;
        int y = it.y;
        for (const std::string& line : it.lines) {      // FUN_004cbf00, 20 px GDI font
            ctx_.draw.text({.x = it.x, .y = y, .priority = 0x37,
                            .argb = std::uint32_t(alpha & 0xFF) << 24 | 0xFFFFFFu, .text = line, .fontPx = 20});
            y += 0x16;
        }
    }

    if (clickable) {
        const auto& m = ctx_.mouse();
        for (std::size_t k = 0; k < c.items.size(); ++k) {
            const ChoiceItem& it = c.items[k];
            if (m.x < it.x || m.x >= it.x + it.w || m.y < it.y || m.y >= it.y + it.h) continue;   // FUN_004d5ec0
            if (m.pressed & legacy::kMouseLeft) {
                vm_.store(c.dest, 4, static_cast<std::int32_t>(k));
                if (c.windowed) c.state = 4;
                for (ChoiceItem& o : c.items) o.state = 4;
            }
            ctx_.draw.fill({.x = it.x, .y = it.y + 1, .w = it.w, .h = it.h + 4, .priority = 0x38,
                            .argb = 0xA0A0A020u});
            break;
        }
    }
    return done;
}

// --------------------------------------------------------------- hotspots ---
// FUN_004c8f60, polled while opcode 0x34 waits. Areas are 5 words: x, y, w,
// h operands and a raw jump target. A left click on area i stores i and
// jumps; a right click (if enabled) stores -1, a left click outside -2.
bool AdvTask::tickHotspots() {
    const Hotspots& h = hotspots_;
    const YbcScript& f = vm_.script();
    const std::uint32_t count = f.u16(f.block(0) + h.rec + 4);
    const auto& m = ctx_.mouse();
    int hits = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t e = 0x14 + i * 0x14;
        const int x = vm_.value(h.rec, e), y = vm_.value(h.rec, e + 4);
        const int w = vm_.value(h.rec, e + 8), hh = vm_.value(h.rec, e + 0xc);
        if (m.x < x || m.x >= x + w || m.y < y || m.y >= y + hh) continue;   // FUN_004d5ec0
        ++hits;
        if (m.pressed & legacy::kMouseLeft) {
            if (h.hasDest) vm_.store(h.rec, 8, static_cast<std::int32_t>(i));
            vm_.jump(f.u32(f.block(0) + h.rec + e + 0x10));
            return true;
        }
    }
    if (h.outsideJump && hits == 0 && (m.pressed & legacy::kMouseLeft)) {
        if (h.hasDest) vm_.store(h.rec, 8, -2);
        vm_.jump(h.outsideTarget);
        return true;
    }
    if (h.rightJump && (m.pressed & legacy::kMouseRight)) {
        if (h.hasDest) vm_.store(h.rec, 8, -1);
        vm_.jump(h.rightTarget);
        return true;
    }
    return false;
}

// FUN_004cb6a0 on every buffer: lines still queued or fading in show at once.
void AdvTask::revealAll() {
    for (Buffer& b : buffers_)
        for (std::uint16_t i = 0; i < b.count; ++i) {
            Line& l = b.lines[i];
            if (l.state == 2 || l.state == 3) { l.state = 1; l.fadeCnt = l.fadeLen; }
            l.holdCnt = l.holdLen;
            l.done = 1;
        }
}

} // namespace ch
