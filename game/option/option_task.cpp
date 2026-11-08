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
constexpr int kPortX = kPanelX, kPortY = 473, kPortW = 400, kPortRowH = 30, kPortSwitchX = kPortX + 290, kPortSwitchW = 92;
// What the mouse can be over in that panel. The language button opens a list of them.
enum PortItem { kItemFullscreen, kItemInfinite, kItemInstantWin, kItemTutorial, kItemLanguage, kItemChapters };
constexpr int kLanguageW = 184, kLanguageX = kPortSwitchX + kPortSwitchW - kLanguageW;
constexpr int kListRowH = 28;
constexpr std::size_t kListRows = 8;
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

// Fullscreen, infinite time, the instant win, the language if it can change,
// Tutorial in the TW build, the chapter select.
std::vector<int> OptionTask::portItems() const {
    std::vector<int> items{kItemFullscreen, kItemInfinite, kItemInstantWin};
    if (options::languages().switchTo) items.push_back(kItemLanguage);
    if (ctx_.version == GameVersion::Tw) items.push_back(kItemTutorial);
    items.push_back(kItemChapters);
    return items;
}

int OptionTask::portRows() const { return int(portItems().size()); }

int OptionTask::languageRowY() const {
    const std::vector<int> items = portItems();
    const auto at = std::find(items.begin(), items.end(), int(kItemLanguage));
    return kPortY + 7 + int(at - items.begin()) * kPortRowH;
}

int OptionTask::portHover() const {
    const auto& m = ctx_.mouse();
    const std::vector<int> items = portItems();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int y = kPortY + 7 + int(i) * kPortRowH;
        if (m.y < y || m.y >= y + kPortRowH - 3) continue;
        if (items[i] == kItemLanguage) return m.x >= kLanguageX && m.x < kPortSwitchX + kPortSwitchW ? kItemLanguage : -1;
        if (m.x < kPortSwitchX || m.x >= kPortSwitchX + kPortSwitchW) return -1;
        return items[i];
    }
    return -1;
}

namespace {
// The list opens under the button, or as low as still fits.
int listY(int rowY, std::size_t rows) {
    return std::min(rowY + kPortRowH - 2, 0x300 - 4 - int(rows) * kListRowH);
}
} // namespace

int OptionTask::listHover() const {
    const auto& m = ctx_.mouse();
    const std::size_t rows = std::min(options::languages().all.size(), kListRows);
    const int y = listY(languageRowY(), rows);
    if (m.x < kLanguageX || m.x >= kLanguageX + kLanguageW || m.y < y || m.y >= y + int(rows) * kListRowH) return -1;
    return (m.y - y) / kListRowH;
}

void OptionTask::drawLanguageList() {
    const auto& l = options::languages();
    const std::size_t rows = std::min(l.all.size(), kListRows);
    const int y = listY(languageRowY(), rows), hot = listHover();
    ysub::fill(ctx_, kLanguageX, y, kLanguageW, int(rows) * kListRowH, kListPrio, 0xff20281eu);
    ysub::outline(ctx_, kLanguageX, y, kLanguageX + kLanguageW - 1, y + int(rows) * kListRowH - 1, kListPrio - 1, 0xffc8ccb8u);
    for (std::size_t i = 0; i < rows; ++i) {
        const int ry = y + int(i) * kListRowH;
        const bool current = l.all[i].id == l.current;
        if (current || hot == int(i))
            ysub::fill(ctx_, kLanguageX + 1, ry + 1, kLanguageW - 2, kListRowH - 2, kListPrio - 2, current ? 0xff7d8a68u : 0xff3c4a38u);
        ctx_.draw.text({.x = kLanguageX + kLanguageW / 2, .y = ry + kListRowH / 2, .priority = kListPrio - 3, .argb = 0xffffffffu,
                        .text = l.all[i].name, .fontPx = 16, .bold = true, .centred = true});
    }
}

void OptionTask::storeLevels() {
    for (std::size_t i = 0; i < 3; ++i) ctx_.work.set32(kLevels[i], level_[i]);
}

// On the press, not the release as above: a browser allows fullscreen only
// during a mouse event, and the request runs at the release.
void OptionTask::portInput() {
    const auto& mouse = ctx_.mouse();
    if (listOpen_) {
        // Closes on the release: the original's controls act on it, and must not see this one.
        if (mouse.pressed & legacy::kMouseLeft) {
            const auto& l = options::languages();
            const int row = listHover();
            if (row >= 0 && l.all[std::size_t(row)].id != l.current) {
                ctx_.audio->sePlay(5);
                storeLevels();                           // the restart reads the volumes back
                options::save(ctx_.work);
                l.switchTo(l.all[std::size_t(row)].id);
            }
            listClosing_ = true;
        }
        if (listClosing_ && (mouse.released & legacy::kMouseLeft)) listOpen_ = listClosing_ = false;
        return;
    }
    if ((mouse.pressed & legacy::kMouseLeft) == 0) return;
    auto& p = options::port();
    switch (portHover()) {
        case kItemFullscreen:
            p.fullscreen = !ctx_.engine.fullscreen();
            ctx_.engine.setFullscreen(p.fullscreen);
            break;
        case kItemInfinite:
            p.infiniteUnitTime = !p.infiniteUnitTime;
            break;
        case kItemInstantWin:
            p.instantWin = !p.instantWin;
            break;
        case kItemTutorial:
            p.twTutorial = !p.twTutorial;
            break;
        case kItemLanguage:
            ctx_.audio->sePlay(5);
            listOpen_ = true;
            return;
        case kItemChapters:     // left as the back button leaves, input() then ends the screen
            ctx_.audio->sePlay(5);
            chapters_ = true;
            return;
        default: return;
    }
    ctx_.audio->sePlay(5);
    options::savePort();
}

void OptionTask::drawPortPanel() {
    constexpr int kFramePrio = 0x5f, kTextPrio = 0x50;
    constexpr std::uint32_t kPale = 0xffc8ccb8u, kText = 0xffffffffu, kDim = 0xff9aa08cu;
    constexpr std::uint32_t kOn = 0xff7d8a68u, kOff = 0xff20281eu;
    const int rowsShown = portRows(), height = rowsShown * kPortRowH + 12;
    ysub::fill(ctx_, kPortX, kPortY, kPortW, height, kFramePrio, 0xff3c4a38u);
    ysub::outline(ctx_, kPortX, kPortY, kPortX + kPortW - 1, kPortY + height - 1, kFramePrio - 1, kPale);
    const bool tw = ctx_.version == GameVersion::Tw;
    const int hot = listOpen_ ? -1 : portHover();
    const auto rowY = [](int row) { return kPortY + 7 + row * kPortRowH; };
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
    toggle(row++, tw ? "單位時間無限 (單人)" : tr("ユニット時間無限 (シングル)"), options::port().infiniteUnitTime, kItemInfinite);
    toggle(row++, tw ? "按Enter立即勝利 (單人)" : tr("Enterで即勝利 (シングル)"), options::port().instantWin, kItemInstantWin);
    if (const auto& l = options::languages(); l.switchTo) {
        label(row, tw ? "語言" : tr("言語"));
        std::string name;
        for (const auto& c : l.all)
            if (c.id == l.current) name = c.name;
        button(kLanguageX, row, kLanguageW, name + " ▼", false, hot == kItemLanguage);
        ++row;
    }
    if (listOpen_) drawLanguageList();
    if (tw) toggle(row++, "標題畫面顯示教學", options::port().twTutorial, kItemTutorial);
    // The original's debug chapter select (scene 0x16): no menu in either exe leads to it.
    label(row, tw ? "章節選擇 (除錯)" : tr("章選択 (デバッグ)"));
    button(kPortSwitchX, row, kPortSwitchW, tw ? "開啟" : tr("開く"), false, hot == kItemChapters);
}

bool OptionTask::input() {
    const bool listWasOpen = listOpen_;
    portInput();
    if (chapters_) {
        storeLevels();
        options::apply(ctx_.work, *ctx_.audio, ctx_.version);
        return true;
    }
    if (listWasOpen || listOpen_) {
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
