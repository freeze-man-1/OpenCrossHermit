// ============================================================================
// option_task.cpp
// ============================================================================
#include "option_task.hpp"
#include "options.hpp"
#include "../state/game_logic.hpp"
#include "../text/sjis.hpp"
#include "../ysub/ysub.hpp"

#include <algorithm>

namespace ch {

namespace {
// DAT_006067a8: {x, y, w, h} hit cells relative to the panel; DAT_006068a8:
// source rects in frame 1.
constexpr int kPanelX = 0x138, kPanelY = 299;
constexpr std::uint32_t kBack = 0x6067a8, kBgmCells = 0x6067b8, kSeCells = 0x606808, kVoiceCells = 0x606858;
constexpr std::uint32_t kPanelSrc = 0x6068a8, kBackLitSrc = 0x6068b0, kKnobSrc = 0x6068b8;
constexpr std::uint32_t kCells[3] = {kBgmCells, kSeCells, kVoiceCells};
constexpr std::uint32_t kLevels[3] = {options::kBgm, options::kSe, options::kVoice};
// The port's panel, just under the original's and as wide.
constexpr int kPortX = kPanelX, kPortY = 473, kPortW = 400, kPortRowMax = 30, kPortSwitchX = kPortX + 290, kPortSwitchW = 92;
// What the mouse can be over in that panel. The language button opens a list of them.
enum PortItem { kItemFullscreen, kItemInfinite, kItemInstantWin, kItemUiStyle, kItemTutorial, kItemLanguage, kItemChapters,
                kItemWindow, kItemScale, kItemBattleView };
constexpr int kLanguageW = 184, kLanguageX = kPortSwitchX + kPortSwitchW - kLanguageW;
constexpr int kListRowH = 28;
constexpr std::size_t kListRows = 14;
constexpr int kListPrio = 0x20;
} // namespace

// FUN_0044d440: FUN_0041d280 stops both streams (the music and the voice),
// FUN_0040d610 the sounds, then the bank and the SE channels 1..19
// (FUN_0040d120, which seStopAll covers).
OptionTask::~OptionTask() {
    ctx_.audio->bgmStop();
    ctx_.audio->voiceStop();
    ctx_.audio->seStopAll();
    ctx_.sprites->unloadBank(kBank);
}

int OptionTask::hover(std::uint32_t table, int count) const {
    const auto& m = ctx_.mouse();
    for (int i = 0; i < count; ++i) {
        const std::uint32_t r = table + std::uint32_t(i) * 8;
        const int x = kPanelX + logic::sdS16(r, ctx_.version), y = kPanelY + logic::sdS16(r + 2, ctx_.version);
        if (x < m.x && m.x < x + logic::sdS16(r + 4, ctx_.version) && y < m.y && m.y < y + logic::sdS16(r + 6, ctx_.version))
            return i;
    }
    return -1;
}

// FUN_0044d560 / FUN_0044d730
Co<> OptionTask::start(std::uint32_t) {
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\Tactics\\Option.bin"));
    for (std::size_t i = 0; i < 3; ++i) level_[i] = ctx_.work.s32(kLevels[i]);   // FUN_0044d610
    ctx_.audio->bgmPlay(3, 1);
    const auto fade = [&](bool out) {                               // FUN_00410930(out, 0x1e)
        fadeLeft_ = 0x1e;
        fadeStep_ = 0xff0000 / 0x1e;
        if (out) fade_ = 0; else { fade_ = 0xff0000; fadeStep_ = -fadeStep_; }
    };
    const auto stepFade = [&] {                                     // FUN_00410a60
        if (fadeLeft_ == 0) return true;
        --fadeLeft_;
        fade_ = std::clamp(fade_ + fadeStep_, 0xff, 0xff0000);
        return false;
    };
    co_await yieldFrame();
    fade(false);
    draw();
    co_await yieldFrame();
    while (!stepFade()) { draw(); co_await yieldFrame(); }
    while (!input()) { draw(); co_await yieldFrame(); }
    draw();
    co_await yieldFrame();
    fade(true);
    while (!stepFade()) { draw(); co_await yieldFrame(); }
    ctx_.audio->bgmFadeOut(0x5a);
    while (!ctx_.audio->bgmFadeFinished()) { draw(); co_await yieldFrame(); }
    ctx_.audio->seStopAll();
    ctx_.scenes.request(chapters_ ? SceneId::AdvMenu : SceneId::Title);
    options::save(ctx_.work);                                       // FUN_004e3070
}

// Fullscreen, the window size (not on the web, where the page sizes it), the
// scaling, the battle's wider view, infinite time, the instant win, the UI style, the language if it can
// change, Tutorial in the TW build, the chapter select.
std::vector<int> OptionTask::portItems() const {
    std::vector<int> items{kItemFullscreen};
#ifndef __EMSCRIPTEN__
    items.push_back(kItemWindow);
#endif
    items.insert(items.end(), {kItemScale, kItemBattleView, kItemInfinite, kItemInstantWin, kItemUiStyle});
    if (options::languages().switchTo) items.push_back(kItemLanguage);
    if (ctx_.version == GameVersion::Tw) items.push_back(kItemTutorial);
    items.push_back(kItemChapters);
    return items;
}

int OptionTask::portRows() const { return int(portItems().size()); }

// A row's pitch: the original 30, less when the rows would run off the screen.
int OptionTask::portRowH() const { return std::min(kPortRowMax, (0x300 - 4 - 12 - kPortY) / std::max(portRows(), 1)); }

int OptionTask::rowY(int item) const {
    const std::vector<int> items = portItems();
    const auto at = std::find(items.begin(), items.end(), item);
    return kPortY + 7 + int(at - items.begin()) * portRowH();
}

int OptionTask::portHover() const {
    const auto& m = ctx_.mouse();
    const std::vector<int> items = portItems();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int y = kPortY + 7 + int(i) * portRowH();
        if (m.y < y || m.y >= y + portRowH() - 3) continue;
        if (items[i] == kItemLanguage || items[i] == kItemWindow || items[i] == kItemScale)   // the wide buttons
            return m.x >= kLanguageX && m.x < kPortSwitchX + kPortSwitchW ? items[i] : -1;
        if (m.x < kPortSwitchX || m.x >= kPortSwitchX + kPortSwitchW) return -1;
        return items[i];
    }
    return -1;
}

namespace {
// The list opens under the button, or as low as still fits.
int listY(int rowY, int rowH, std::size_t rows) {
    return std::min(rowY + rowH - 2, 0x300 - 4 - int(rows) * kListRowH);
}
} // namespace

OptionTask::ListChoices OptionTask::listChoices() const {
    ListChoices out;
    const auto& p = options::port();
    if (listFor_ == kItemLanguage) {
        const auto& l = options::languages();
        for (const auto& c : l.all) {
            if (c.id == l.current) out.current = int(out.names.size());
            out.names.push_back(c.name);
        }
    } else if (listFor_ == kItemWindow) {
        for (const SDL_Point& s : ctx_.engine.windowSizes()) {
            if (s.x == p.windowW && s.y == p.windowH) out.current = int(out.names.size());
            out.names.push_back(std::to_string(s.x) + " x " + std::to_string(s.y));
        }
    } else if (listFor_ == kItemScale) {
        for (int m = 0; m < int(legacy::ScaleMode::Count); ++m) {
            if (legacy::ScaleMode(m) == p.scale) out.current = m;
            out.names.push_back(scaleLabel(legacy::ScaleMode(m)));
        }
    }
    if (out.names.size() > kListRows) out.names.resize(kListRows);
    return out;
}

int OptionTask::listHover() const {
    const auto& m = ctx_.mouse();
    const std::size_t rows = listChoices().names.size();
    const int y = listY(rowY(listFor_), portRowH(), rows);
    if (m.x < kLanguageX || m.x >= kLanguageX + kLanguageW || m.y < y || m.y >= y + int(rows) * kListRowH) return -1;
    return (m.y - y) / kListRowH;
}

void OptionTask::drawList() {
    const ListChoices c = listChoices();
    const std::size_t rows = c.names.size();
    const int y = listY(rowY(listFor_), portRowH(), rows), hot = listHover();
    ysub::fill(ctx_, kLanguageX, y, kLanguageW, int(rows) * kListRowH, kListPrio, 0xff20281eu);
    ysub::outline(ctx_, kLanguageX, y, kLanguageX + kLanguageW - 1, y + int(rows) * kListRowH - 1, kListPrio - 1, 0xffc8ccb8u);
    for (std::size_t i = 0; i < rows; ++i) {
        const int ry = y + int(i) * kListRowH;
        const bool current = int(i) == c.current;
        if (current || hot == int(i))
            ysub::fill(ctx_, kLanguageX + 1, ry + 1, kLanguageW - 2, kListRowH - 2, kListPrio - 2, current ? 0xff7d8a68u : 0xff3c4a38u);
        ctx_.draw.text({.x = kLanguageX + kLanguageW / 2, .y = ry + kListRowH / 2, .priority = kListPrio - 3, .argb = 0xffffffffu,
                        .text = c.names[i], .fontPx = 16, .bold = true, .centred = true});
    }
}

void OptionTask::choose(int row) {
    if (row < 0 || row == listChoices().current) return;
    auto& p = options::port();
    ctx_.audio->sePlay(5);
    if (listFor_ == kItemLanguage) {
        const auto& l = options::languages();
        storeLevels();                           // the restart reads the volumes back
        options::save(ctx_.work);
        l.switchTo(l.all[std::size_t(row)].id);
        return;
    }
    if (listFor_ == kItemWindow) {
        const SDL_Point s = ctx_.engine.windowSizes()[std::size_t(row)];
        p.windowW = s.x;
        p.windowH = s.y;
        ctx_.engine.setWindowSize(s.x, s.y);
        // The window moves and changes size under the pointer: keep it on this button,
        // not wherever it lands (the OK button, often).
        ctx_.engine.keepMouseAt(kLanguageX + kLanguageW / 2, rowY(kItemWindow) + (portRowH() - 3) / 2);
    } else if (listFor_ == kItemScale) {
        p.scale = legacy::ScaleMode(row);
        ctx_.engine.setScaleMode(p.scale);
    }
    options::savePort();
}

void OptionTask::storeLevels() {
    for (std::size_t i = 0; i < 3; ++i) ctx_.work.set32(kLevels[i], level_[i]);
}

// On the press, not the release as above: a browser allows fullscreen only
// during a mouse event, and the request runs at the release.
void OptionTask::portInput() {
    const auto& mouse = ctx_.mouse();
    if (listFor_ >= 0) {
        // Closes on the release: the original's controls act on it, and must not see this one.
        if (mouse.pressed & legacy::kMouseLeft) {
            choose(listHover());
            listClosing_ = true;
        }
        if (listClosing_ && (mouse.released & legacy::kMouseLeft)) {
            listFor_ = -1;
            listClosing_ = false;
        }
        return;
    }
    auto& p = options::port();
    if ((mouse.pressed & legacy::kMouseLeft) == 0) return;
    switch (portHover()) {
        case kItemFullscreen:
            p.fullscreen = !ctx_.engine.fullscreen();
            ctx_.engine.setFullscreen(p.fullscreen);
            break;
        case kItemWindow:
        case kItemScale:
        case kItemLanguage:
            ctx_.audio->sePlay(5);
            listFor_ = portHover();
            return;
        case kItemBattleView:
            p.wideBattle = !p.wideBattle;
            break;
        case kItemInfinite:
            p.infiniteUnitTime = !p.infiniteUnitTime;
            break;
        case kItemInstantWin:
            p.instantWin = !p.instantWin;
            break;
        case kItemUiStyle:
            p.modernUi = !p.modernUi;
            break;
        case kItemTutorial:
            p.twTutorial = !p.twTutorial;
            break;
        case kItemChapters:     // left as the back button leaves, input() then ends the screen
            ctx_.audio->sePlay(5);
            chapters_ = true;
            return;
        default: return;
    }
    ctx_.audio->sePlay(5);
    options::savePort();
}

std::string OptionTask::scaleLabel(legacy::ScaleMode m) const {
    const bool tw = ctx_.version == GameVersion::Tw;
    switch (m) {
        case legacy::ScaleMode::Sharp:   return tw ? "銳利" : tr("シャープ");
        case legacy::ScaleMode::Integer: return tw ? "整數倍" : tr("整数倍");
        case legacy::ScaleMode::Stretch: return tw ? "拉伸" : tr("引き伸ばし");
        default:                         return tw ? "平滑" : tr("なめらか");
    }
}

void OptionTask::drawPortPanel() {
    constexpr int kFramePrio = 0x5f, kTextPrio = 0x50;
    constexpr std::uint32_t kPale = 0xffc8ccb8u, kText = 0xffffffffu, kDim = 0xff9aa08cu;
    constexpr std::uint32_t kOn = 0xff7d8a68u, kOff = 0xff20281eu;
    const int rowsShown = portRows(), kPortRowH = portRowH(), height = rowsShown * kPortRowH + 12;
    ysub::fill(ctx_, kPortX, kPortY, kPortW, height, kFramePrio, 0xff3c4a38u);
    ysub::outline(ctx_, kPortX, kPortY, kPortX + kPortW - 1, kPortY + height - 1, kFramePrio - 1, kPale);
    const bool tw = ctx_.version == GameVersion::Tw;
    const int hot = listFor_ >= 0 ? -1 : portHover();
    const auto rowY = [kPortRowH](int row) { return kPortY + 7 + row * kPortRowH; };
    const auto label = [&](int row, const std::string& text) {
        ctx_.draw.text({.x = kPortX + 14, .y = rowY(row) + 3, .priority = kTextPrio, .argb = kText, .text = text,
                        .fontPx = 18, .bold = true});
    };
    const auto button = [&](int x, int row, int w, const std::string& text, bool on, bool lit) {
        const int y = rowY(row);
        ysub::fill(ctx_, x, y, w, kPortRowH - 3, kFramePrio - 1, on ? kOn : kOff);
        if (lit) ysub::outline(ctx_, x, y, x + w - 1, y + kPortRowH - 4, kTextPrio, kText);
        ctx_.draw.text({.x = x + w / 2, .y = y + (kPortRowH - 3) / 2, .priority = kTextPrio,
                        .argb = on ? kText : kDim, .text = text, .fontPx = 16, .bold = true, .centred = true});
    };
    const auto toggle = [&](int row, const std::string& text, bool on, PortItem item) {
        label(row, text);
        button(kPortSwitchX, row, kPortSwitchW, on ? "ON" : "OFF", on, hot == item);
    };
    int row = 0;
    toggle(row++, tw ? "全螢幕 (F11)" : tr("フルスクリーン (F11)"), ctx_.engine.fullscreen(), kItemFullscreen);
#ifndef __EMSCRIPTEN__
    label(row, tw ? "視窗大小" : tr("ウィンドウサイズ"));
    button(kLanguageX, row, kLanguageW, std::to_string(options::port().windowW) + " x " + std::to_string(options::port().windowH) + " ▼",
           !ctx_.engine.fullscreen(), hot == kItemWindow);
    ++row;
#endif
    label(row, tw ? "畫面縮放" : tr("拡大方法"));
    button(kLanguageX, row, kLanguageW, scaleLabel(options::port().scale) + " ▼", false, hot == kItemScale);
    ++row;
    label(row, tw ? "戰鬥視野" : tr("戦闘の視野"));
    button(kPortSwitchX, row, kPortSwitchW, options::port().wideBattle ? (tw ? "寬" : tr("ワイド")) : (tw ? "原版" : tr("オリジナル")),
           options::port().wideBattle, hot == kItemBattleView);
    ++row;
    toggle(row++, tw ? "單位時間無限 (單人)" : tr("ユニット時間無限 (シングル)"), options::port().infiniteUnitTime, kItemInfinite);
    toggle(row++, tw ? "按Enter立即勝利 (單人)" : tr("Enterで即勝利 (シングル)"), options::port().instantWin, kItemInstantWin);
    label(row, tw ? "介面風格" : tr("UIスタイル"));
    button(kPortSwitchX, row, kPortSwitchW, ui::modern() ? (tw ? "新" : tr("新")) : (tw ? "經典" : tr("クラシック")),
           ui::modern(), hot == kItemUiStyle);
    ++row;
    if (const auto& l = options::languages(); l.switchTo) {
        label(row, tw ? "語言" : tr("言語"));
        std::string name;
        for (const auto& c : l.all)
            if (c.id == l.current) name = c.name;
        button(kLanguageX, row, kLanguageW, name + " ▼", false, hot == kItemLanguage);
        ++row;
    }
    if (listFor_ >= 0) drawList();
    if (tw) toggle(row++, "標題畫面顯示教學", options::port().twTutorial, kItemTutorial);
    // The original's debug chapter select (scene 0x16): no menu in either exe leads to it.
    label(row, tw ? "章節選擇 (除錯)" : tr("章選択 (デバッグ)"));
    button(kPortSwitchX, row, kPortSwitchW, tw ? "開啟" : tr("開く"), false, hot == kItemChapters);
}

bool OptionTask::input() {
    const bool listWasOpen = listFor_ >= 0;
    portInput();
    if (chapters_) {
        storeLevels();
        options::apply(ctx_.work, *ctx_.audio, ctx_.version);
        return true;
    }
    if (listWasOpen || listFor_ >= 0) {
        hover_.fill(-1);
        return false;
    }
    hover_ = {hover(kBack, 1), hover(kBgmCells, 10), hover(kSeCells, 10), hover(kVoiceCells, 10)};
    bool back = false;
    const bool any = hover_[0] != -1 || hover_[1] != -1 || hover_[2] != -1 || hover_[3] != -1;
    if (any && (ctx_.mouse().released & legacy::kMouseLeft) != 0) {
        const GameVersion v = ctx_.version;
        if (hover_[0] != -1) {
            ctx_.audio->sePlay(5);
            storeLevels();
            options::apply(ctx_.work, *ctx_.audio, v);
            back = true;
        } else if (hover_[1] != -1) {
            if (level_[0] != hover_[1]) {
                level_[0] = hover_[1];
                ctx_.audio->setVolume(IAudio::Channel::Bgm, options::bgmVolume(level_[0], v));
            }
        } else if (hover_[2] != -1) {
            if (level_[1] != hover_[2]) {
                level_[1] = hover_[2];
                ctx_.audio->setVolume(IAudio::Channel::Se, options::seVolume(level_[1], v));
                ctx_.audio->sePlayIn(0, 0xb, 0);
            }
        } else if (level_[2] != hover_[3]) {
            level_[2] = hover_[3];
            // The original previews with the BGM table (FUN_004e2ec0); the
            // voice table only applies once the screen is left.
            ctx_.audio->setVolume(IAudio::Channel::Voice, options::bgmVolume(level_[2], v));
            ctx_.audio->voicePlay("data\\sound\\Voice\\v006\\v006_043_01.mp3");
        }
    }
    if ((ctx_.mouse().held & legacy::kMouseLeft) == 0) hover_.fill(-1);
    return back;
}

void OptionTask::draw() {
    const GameVersion v = ctx_.version;
    const auto part = [&](std::uint32_t src, int prio, int x, int y) {   // FUN_0044e1a0, frame 1 (FUN_0044e140 its set 0x5a call)
        ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kBank), .frame = 1, .sx = logic::sdS16(src, v),
                             .sy = logic::sdS16(src + 2, v), .w = logic::sdS16(src + 4, v), .h = logic::sdS16(src + 6, v)},
                      prio, x, y);
    };
    part(kPanelSrc, 0x5f, kPanelX, kPanelY);
    drawPortPanel();
    if (hover_[0] != -1)
        part(kBackLitSrc, 0x50, kPanelX + logic::sdS16(kBack + 8, v), kPanelY + logic::sdS16(kBack + 10, v));
    for (std::size_t i = 0; i < 3; ++i) {
        const std::uint32_t cell = kCells[i] + std::uint32_t(level_[i]) * 8;
        part(kKnobSrc + std::uint32_t(level_[i]) * 8,   // FUN_0044df40
             0x50, kPanelX + logic::sdS16(cell, v), kPanelY + logic::sdS16(cell + 2, v));
    }
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kBank), .frame = 2}, 100, 0, 0);
    if (fade_ >> 16 != 0)
        ysub::fill(ctx_, 0, 0, 0x403, 0x300, 4, std::uint32_t(fade_ >> 16 & 0xff) << 24);
    const auto& m = ctx_.mouse();
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kBank), .frame = 0}, 1, m.x, m.y);
}

} // namespace ch
