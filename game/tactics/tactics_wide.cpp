// ============================================================================
// tactics_wide.cpp
// ============================================================================
#include "tactics_wide.hpp"
#include "../option/options.hpp"
#include "../ysub/ysub.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace ch {

namespace {
using legacy::ViewPiece;
using legacy::touch::MouseStep;
using legacy::touch::Point;

constexpr int kHud = 1, kCursor = 2;                 // the draw list's layers (0: the map's view)
// The port's own two buttons, drawn on the HUD's layer where the frame has
// the map's view (so nothing of the HUD's is there).
constexpr SDL_Rect kAll{60, 520, 72, 72}, kNone{140, 520, 72, 72};
// Parts of the frame's HUD: the menu button and the clock in the top bar, the
// unit panel, the help lines, the objectives button.
constexpr SDL_Rect kMenu{0, 0, 104, 30}, kClock{244, 0, 384, 30}, kPanel{0, 600, 0x18c, 0xa8}, kHelp{460, 692, 325, 76},
                   kGoals{0x3d1, 0x26f, 0x33, 0x28};
// The panel folded: its portrait. The battle's menu with the port's goals above it.
constexpr SDL_Rect kPortrait{8, 618, 60, 62}, kMenuBox{200, 120, 624, 310};
// More of the port's own, drawn there too: the four groups, the wheel's "keep as group" and its tab.
constexpr SDL_Rect kGroup[4] = {{230, 528, 60, 60}, {294, 528, 60, 60}, {358, 528, 60, 60}, {422, 528, 60, 60}};
constexpr SDL_Rect kKeep[4] = {{230, 460, 60, 60}, {294, 460, 60, 60}, {358, 460, 60, 60}, {422, 460, 60, 60}};
constexpr SDL_Rect kTab{500, 460, 60, 60};
constexpr int kTabCode = 100, kKeepCode = 200;       // the wheel's buttons past the bar's 0..15
constexpr int kPing = 24;                            // ticks an order's mark shows

bool in(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
} // namespace

TacticsWide::~TacticsWide() {
    if (on_) ctx_.engine.input().setTouchMouse(true);
}

bool TacticsWide::onMap() const { return !on_ || (hudClick_ == 0 && ctx_.engine.pointerLayer() == 0); }
bool TacticsWide::onHud() const { return !on_ || hudClick_ > 0 || ctx_.engine.pointerLayer() == kHud; }

int TacticsWide::edges() const {
    if (!mouseLive_ || ctx_.engine.pointerIsTouch() || ctx_.mouse().held != 0) return 0;
    const SDL_Point out = ctx_.engine.outputSize();
    const SDL_FPoint p = ctx_.engine.pointerOutput();
    return (p.y < 2 ? 1 : p.y > float(out.y - 3) ? 2 : 0) | (p.x < 2 ? 4 : p.x > float(out.x - 3) ? 8 : 0);
}

void TacticsWide::frame(bool free, bool menu) {
    auto& engine = ctx_.engine;
    const SDL_Point out = engine.outputSize();
    if (!mouseLive_ && !engine.pointerIsTouch()) {
        const SDL_FPoint p = engine.pointerOutput();
        if (!std::exchange(mouseKnown_, true)) mouseFirst_ = p;
        else mouseLive_ = p.x != mouseFirst_.x || p.y != mouseFirst_.y;
    }
    // Debug: CH_WIDE=1 / 0 turns it on or off whatever the UI style.
    static const char* const force = SDL_getenv("CH_WIDE");
    const bool want = force ? force[0] == '1' : ui::modern();
    const bool on = free && want && out.y > 0 && out.x * 10 >= out.y * 16;
    menu_ = on && menu;
    if (ping_ > 0) --ping_;
    if (hudClick_ > 0) --hudClick_;
    if (keptShow_ > 0) --keptShow_;
    if (!on || menu_) wheel_ = fling_ = false;
    // Debug: CH_WHEEL=a / s / t picks the wheel tried.
    static const char* const kind = SDL_getenv("CH_WHEEL");
    if (kind && kind[0]) wheelKind_ = kind[0];
    if (on != on_) {
        on_ = on;
        engine.input().setTouchMouse(!on);
        held_ = boxing_ = panning_ = hudDrag_ = false;
    }
    if (!on_) return;
    layout(out);
    buttons();
    touch();
}

void TacticsWide::layout(SDL_Point out) {
    const float W = float(out.x), H = float(out.y);
    // Sized for a finger once one has touched the screen, smaller for a mouse.
    const bool finger = ctx_.engine.input().touch().used;
    const float k = H / 780.f * (finger ? 1.f : 0.85f);
    if (finger != std::exchange(fingerSized_, finger)) resized_ = true;
    const bool live = !menu_;                        // under the menu nothing else takes the pointer
    std::vector<ViewPiece> pieces;
    const auto add = [&](const SDL_Rect& src, float x, float y, float w, float h, int layer = kHud, bool input = true) {
        pieces.push_back({.src = src, .dst = {x, y, w, h}, .layer = std::uint8_t(layer), .input = input && live});
    };

    // The map's view, as large as fits.
    // A window wider than the view shows more of the map at its sides (TacticsMap::setMargin).
    const int margin = options::port().wideBattle
                           ? std::clamp((out.x * TacticsMap::kViewH / out.y - TacticsMap::kViewW) / 2, 0, int(legacy::Engine::kMaxMargin))
                           : 0;
    map_.setMargin(margin);
    const int viewW = TacticsMap::kViewW + 2 * margin;
    const float s = std::min(W / float(viewW), H / float(TacticsMap::kViewH));
    const float mw = float(viewW) * s, mh = float(TacticsMap::kViewH) * s, mx = (W - mw) / 2, my = (H - mh) / 2;
    add({-margin, TacticsMap::kViewY, viewW, TacticsMap::kViewH}, mx, my, mw, mh, 0);
    mapDst_ = {mx, my, mw, mh};

    // The two columns: beside the view where the window is wider, else over its edges.
    const float ww = 141.f * k;
    const float lx = std::max(0.f, mx - ww), rx = std::min(W - ww, mx + mw);

    const float cs = 1.2f * k;
    add(kClock, (W - float(kClock.w) * cs) / 2, 0, float(kClock.w) * cs, float(kClock.h) * cs, kHud, false);
    const SDL_Rect& panel = folded_ ? kPortrait : kPanel;
    const float px = std::max(mx, lx + ww), pw = units_.panelShown() >= 0 ? float(panel.w) * k : 0.f;
    if (units_.panelShown() >= 0) add(panel, px, H - float(panel.h) * k, pw, float(panel.h) * k);
    if (units_.helpShown()) {
        const float hs = 1.25f * k;
        add(kHelp, std::max((W - float(kHelp.w) * hs) / 2, px + pw), H - float(kHelp.h) * hs, float(kHelp.w) * hs, float(kHelp.h) * hs, kHud, false);
    }

    // Left: the menu button, then the roster (its second column, faces 10..19, beside the first).
    const float menuH = float(kMenu.h) * ww / float(kMenu.w);
    add(kMenu, lx, 0, ww, menuH);
    {
        int top = 1 << 30, bottom = 0;
        SDL_Rect f{};
        for (int i = 0; i < TacticsUnits::kSlots; ++i)
            if (units_.faceBox(units_.unit(i), f)) {
                top = std::min(top, f.y);
                bottom = std::max(bottom, f.y + f.h);
            }
        if (bottom > top) {
            const float y0 = menuH + 3 * k;
            const float fs = std::min(ww / 96.f, (H - y0) / float(bottom - top));
            for (int i = 0; i < TacticsUnits::kSlots; ++i) {
                const TacticsUnits::Unit& u = units_.unit(i);
                if (!units_.faceBox(u, f)) continue;
                // With the time left beside it: right of the face in the first column, left in the second.
                const SDL_Rect src = u.face < 10 ? SDL_Rect{f.x, f.y, 96, f.h} : SDL_Rect{f.x - 0x28, f.y, 0x28 + f.w, f.h};
                add(src, u.face < 10 ? lx : lx + ww, y0 + float(f.y - top) * fs, float(src.w) * fs, float(src.h) * fs);
            }
        }
    }

    // Right: the minimap, then the bar's buttons two abreast.
    float y = 0;
    if (map_.hasMinimap()) {
        const float ms = std::min(ww / float(map_.miniW()), 0.22f * H / float(map_.miniH()));
        const float w = float(map_.miniW()) * ms, h = float(map_.miniH()) * ms;
        add({TacticsMap::kMiniX, TacticsMap::kMiniY, map_.miniW(), map_.miniH()}, rx + (ww - w) / 2, 0, w, h);
        y = h + 4 * k;
    }
    constexpr int kRows = 9;
    const float cell = std::min(ww / 2, (H - y) / float(kRows));
    const float gx = rx + (ww - 2 * cell) / 2;
    const auto at = [&](const SDL_Rect& src, int col, int row, float h = 0) {
        if (h <= 0) h = cell;
        add(src, gx + float(col) * cell, y + float(row) * cell + (cell - h) / 2, cell, h);
    };
    const auto bar = [&](int button, int col, int row) {
        const SDL_Rect b = units_.barBox(button);
        at({b.x - 1, b.y - 1, b.w + 2, b.h + 2}, col, row);
    };
    for (int i = 0; i < 8; ++i) bar(4 + i, i % 2, i / 2);            // the skills
    for (int i = 0; i < 4; ++i) bar(12 + i, i % 2, 4 + i / 2);       // the stances
    bar(1, 0, 6);                                                    // the call-in mark
    bar(2, 1, 6);                                                    // retreat
    bar(0, 0, 7);                                                    // the skill switch
    at(kGoals, 1, 7, cell * float(kGoals.h) / float(kGoals.w));
    at(kAll, 0, 8);
    at(kNone, 1, 8);
    // The groups, under the view beside the column.
    {
        const float c = cell * 0.82f, x0 = std::min(rx, mx + mw) - 4 * c - 4 * k;
        for (int i = 0; i < 4; ++i) add(kGroup[i], x0 + float(i) * c, H - c, c, c);
    }

    // The wheel, over the rest.
    if (wheel_) {
        wheelCell_ = 78.f * H / 780.f;
        const float reach = 1.9f * wheelCell_;
        wheelAt_.x = std::clamp(wheelAt_.x, std::min(reach, W / 2), std::max(W - reach, W / 2));
        wheelAt_.y = std::clamp(wheelAt_.y, std::min(reach, H / 2), std::max(H - reach, H / 2));
        const std::vector<int> list = wheelButtons();
        for (int i = 0; i < int(list.size()); ++i) {
            const SDL_FRect d = wheelSlot(i, int(list.size()), i == wheelOver_);
            add(wheelSrc(list[std::size_t(i)]), d.x, d.y, d.w, d.h, kHud, false);
        }
    }
    // The pointer the battle draws, where the mouse is in the window.
    if (mouseLive_ && !ctx_.engine.pointerIsTouch()) {
        const auto& m = ctx_.mouse();
        SDL_Rect src{m.x - 48, m.y - 48, 144, 144};
        const SDL_Rect whole{-map_.margin(), 0, 1024 + 2 * map_.margin(), 768};
        SDL_Rect clip{};
        if (SDL_IntersectRect(&src, &whole, &clip)) {
            const float c = std::max(1.f, H / 780.f);
            const SDL_FPoint p = ctx_.engine.pointerOutput();
            add(clip, p.x + float(clip.x - m.x) * c, p.y + float(clip.y - m.y) * c, float(clip.w) * c, float(clip.h) * c, kCursor, false);
        }
    }
    if (menu_) {
        const float s2 = std::min({1.5f * H / 780.f, 0.94f * W / float(kMenuBox.w), 0.94f * H / float(kMenuBox.h)});
        const float w = float(kMenuBox.w) * s2, h = float(kMenuBox.h) * s2;
        // Before the pointer's piece, the last.
        const auto at = pieces.empty() || pieces.back().layer != kCursor ? pieces.end() : pieces.end() - 1;
        pieces.insert(at, {.src = kMenuBox, .dst = {(W - w) / 2, (H - h) / 2, w, h}, .layer = std::uint8_t(kHud), .input = true});
    }
    ctx_.engine.setPieces(std::move(pieces));
}

// The port's own buttons: every unit of ours in view (Ctrl + A), and none.
void TacticsWide::buttons() {
    const auto& m = ctx_.mouse();
    const bool right = (m.pressed & legacy::kMouseRight) != 0;
    if (menu_ || !((m.pressed & legacy::kMouseLeft) || right) || ctx_.engine.pointerLayer() != kHud) return;
    for (int i = 0; i < 4; ++i)
        if (in(kGroup[i], m.x, m.y)) group(i + 1, right);
    if (right) return;
    if (in(kPanel, m.x, m.y)) {
        folded_ = !folded_;
        ctx_.audio->sePlay(5);
    } else if (in(kAll, m.x, m.y)) {
        units_.selectOnScreen();
        ctx_.audio->sePlay(5);
    } else if (in(kNone, m.x, m.y)) {
        units_.clearSelection();
        ctx_.audio->sePlay(5);
    }
}

// A group's button: the group picked (a second time: shown), or the picked units kept as it.
void TacticsWide::group(int g, bool keep) {
    if (keep || (units_.groupSize(g) == 0 && units_.ownSelected() > 0)) {
        units_.storeGroup(g);
        kept_ = g;
        keptShow_ = 30;
    } else if (int centre = -1; units_.recallGroup(g, centre) && centre >= 0) {
        map_.centreOn(units_.unit(centre).px(), units_.unit(centre).py());
    }
    if (!keep && units_.groupSize(g) > 0) {
        kept_ = g;
        keptShow_ = 12;
    }
    ctx_.audio->sePlay(5);
}

// The wheel's buttons. One unit's: its skills and the stances (as two tabs,
// together, or the skills alone). Several units': the stances, and the groups
// to keep them as.
std::vector<int> TacticsWide::wheelButtons() const {
    const std::vector<int> stances{12, 13, 14, 15};
    std::vector<int> list;
    if (wheelMany_) {
        list = stances;
        for (int i = 0; i < 4; ++i) list.push_back(kKeepCode + i);
        return list;
    }
    std::vector<int> skills;
    if (wheelUnit_ >= 0)
        for (int c = 0; c < 8; ++c)
            if (units_.unit(wheelUnit_).data.u16(0x84 + std::uint32_t(c) * 2) != 0) skills.push_back(4 + c);
    if (skills.empty()) return stances;
    if (wheelKind_ == 's') return skills;
    if (wheelKind_ == 'a') {
        list = stances;
        list.insert(list.end(), skills.begin(), skills.end());
        return list;
    }
    list = wheelTab_ == 0 ? skills : stances;
    list.insert(list.begin(), kTabCode);             // first: at the top on either tab
    return list;
}

SDL_Rect TacticsWide::wheelSrc(int code) const {
    if (code == kTabCode) return kTab;
    if (code >= kKeepCode) return kKeep[code - kKeepCode];
    const SDL_Rect b = units_.barBox(code);
    return {b.x - 1, b.y - 1, b.w + 2, b.h + 2};
}

SDL_FRect TacticsWide::wheelSlot(int i, int n, bool picked) const {
    const float a = (float(i) / float(std::max(n, 1)) - 0.25f) * 6.2831853f;
    const float r = wheelCell_ * (n > 8 ? 1.75f : 1.35f), c = wheelCell_ * (picked ? 1.25f : 1.f);
    return {wheelAt_.x + std::cos(a) * r - c / 2, wheelAt_.y + std::sin(a) * r - c / 2, c, c};
}

int TacticsWide::wheelPick(SDL_FPoint p) const {
    if (std::hypot(p.x - wheelAt_.x, p.y - wheelAt_.y) < wheelCell_ * 0.6f) return -1;
    const int n = int(wheelButtons().size());
    int best = -1;
    float nearest = wheelCell_ * 0.8f;
    for (int i = 0; i < n; ++i) {
        const SDL_FRect d = wheelSlot(i, n, false);
        const float far = std::hypot(p.x - (d.x + d.w / 2), p.y - (d.y + d.h / 2));
        if (far < nearest) {
            nearest = far;
            best = i;
        }
    }
    return best;
}

// The bar's own button takes the click, as if the pointer were on it.
void TacticsWide::wheelClick(int pick) {
    const std::vector<int> list = wheelButtons();
    if (pick >= 0 && pick < int(list.size()) && list[std::size_t(pick)] == kTabCode) {
        wheelTab_ ^= 1;                              // the other tab, the wheel still open
        wheelOver_ = -1;
        tabDwell_ = 0;
        ctx_.audio->sePlay(5);
        return;
    }
    if (pick >= 0 && pick < int(list.size()) && list[std::size_t(pick)] >= kKeepCode) {
        group(list[std::size_t(pick)] - kKeepCode + 1, true);
    } else if (pick >= 0 && pick < int(list.size())) {
        const SDL_Rect b = units_.barBox(list[std::size_t(pick)]);
        click({float(b.x + b.w / 2), float(b.y + b.h / 2)}, false);
        ctx_.engine.input().queueMouse({.at = wheelBack_, .endTick = true});   // and back, off the button's help
        hudClick_ = 4;
    }
    wheel_ = false;
    wheelOver_ = -1;
}

// The one of ours picked holds a skill ready: the next tap is its target, whoever is there.
bool TacticsWide::skillHeld() const {
    if (units_.ownSelected() != 1) return false;
    for (int i = 0; i < TacticsUnits::kSlots; ++i) {
        const TacticsUnits::Unit& u = units_.unit(i);
        if (u.selected && u.team() == units_.viewer()) return (u.netReady >= 0 ? u.netReady : u.data.u8(0x9d)) != 0;
    }
    return false;
}

// A tap just off a unit: on it. With ours picked only an enemy draws the tap
// (the ground beside one of ours is somewhere to walk).
Point TacticsWide::snap(Point at) const {
    if (units_.pointedUnit(int(at.x), int(at.y))) return at;
    const bool picked = units_.ownSelected() > 0, held = skillHeld();
    for (const float r : {10.f, 20.f}) {
        if (picked && r > 10.f) break;
        for (int i = 0; i < 8; ++i) {
            const float a = float(i) * 0.7853982f;
            const Point p{at.x + std::cos(a) * r, at.y + std::sin(a) * r * 0.75f};
            const TacticsUnits::Unit* u = units_.pointedUnit(int(p.x), int(p.y));
            if (u && (!picked || held || units_.relation(u->team()) == 1)) return p;
        }
    }
    return at;
}

// The mark of an order: red on an enemy, green on our side, else yellow.
void TacticsWide::ping(Point at) {
    const TacticsUnits::Unit* u = units_.pointedUnit(int(at.x), int(at.y));
    const int rel = u ? units_.relation(u->team()) : -1;
    pingRgb_ = rel == 1 ? 0xff5038u : rel == 0 || rel == 4 ? 0x70ff80u : 0xffffa0u;
    pingAt_ = at;
    ping_ = kPing;
}

void TacticsWide::click(Point at, bool right) {
    auto& in = ctx_.engine.input();
    const MouseStep::Button b = right ? MouseStep::Right : MouseStep::Left;
    in.queueMouse({.at = at, .endTick = true});
    in.queueMouse({.at = at, .button = b, .down = true, .endTick = true});
    in.queueMouse({.at = at, .button = b, .down = false, .endTick = true});
}

void TacticsWide::touch() {
    auto& input = ctx_.engine.input();
    const auto& g = input.touch();
    const int layer = ctx_.engine.pointerLayer();
    if (g.fingers > 0 && !fingerWas_) downLayer_ = layer;
    else if (g.fingers == 0 && !fingerWas_ && (g.tap || g.lifted)) downLayer_ = layer;   // down and up within the tick
    fingerWas_ = g.fingers > 0;
    // The first touch resizes the HUD under the finger: what it means is where it is now.
    if (resized_ && (g.tap || g.longPress || g.dragBegan)) downLayer_ = layer;
    if (g.fingers == 0) resized_ = false;

    // A second finger ends what one was doing.
    if (g.fingers > 1) {
        if (boxing_ || hudDrag_) input.queueMouse({.at = g.at, .button = MouseStep::Left, .down = false, .endTick = true});
        held_ = boxing_ = panning_ = hudDrag_ = false;
    }
    if (g.twoFinger && (g.pan.x != 0 || g.pan.y != 0))
        map_.scrollTo(map_.viewX() - int(std::lround(g.pan.x)), map_.scrollY() - int(std::lround(g.pan.y)));

    if (menu_) {
        if (g.tap) click(g.tapAt, false);
        if (g.longPress) click(g.longPressAt, true);
        held_ = boxing_ = panning_ = hudDrag_ = false;
        return;
    }
    if (wheel_) {
        const SDL_FPoint p = ctx_.engine.pointerOutput();
        if (g.fingers > 1 || g.dragBegan || g.longPress) {
            wheel_ = false;
        } else if (g.fingers == 1) {
            // Resting on the tab turns to it, the finger still down.
            const int over = wheelPick(p);
            const std::vector<int> list = wheelButtons();
            const bool tab = over >= 0 && list[std::size_t(over)] == kTabCode;
            if (!tab) tabTurned_ = false;
            tabDwell_ = tab && !tabTurned_ ? tabDwell_ + 1 : 0;
            wheelOver_ = over;
            if (tabDwell_ >= 18) {
                wheelClick(over);
                tabTurned_ = true;                   // not again until the finger has left it
            }
        } else if (g.lifted || g.tap) {
            // Let go on a button: that one. On the middle the wheel stays for a tap; a tap elsewhere shuts it.
            const int pick = wheelPick(p);
            if (pick >= 0 && !(tabTurned_ && g.lifted && !g.tap)) wheelClick(pick);
            else if (g.tap) wheel_ = false;
            tabTurned_ = false;
        }
        if (wheel_ || g.tap || g.lifted) {
            held_ = boxing_ = panning_ = hudDrag_ = false;
            return;
        }
    }
    if (g.tap) {
        if (downLayer_ == kHud) {
            click(g.tapAt, false);
        } else if (downLayer_ == 0) {
            // One of ours: pick it. Else, with ours picked, the order there.
            const Point at = snap(g.tapAt);
            const TacticsUnits::Unit* u = units_.pointedUnit(int(at.x), int(at.y));
            const bool ours = u && u->team() == units_.viewer();
            const bool order = units_.ownSelected() > 0 && (!ours || units_.callReady() || skillHeld());
            click(at, order);
            if (order) ping(at);
        }
    }
    if (g.longPress) {
        const TacticsUnits::Unit* u = downLayer_ == 0 ? units_.pointedUnit(int(g.longPressAt.x), int(g.longPressAt.y)) : nullptr;
        if (downLayer_ == kHud) {
            click(g.longPressAt, true);
        } else if (u && u->team() == units_.viewer() && u->onField() && !units_.callReady() && !skillHeld()) {
            // One of several picked: the group's wheel, the pick kept.
            wheelMany_ = u->selected && units_.ownSelected() > 1;
            if (!wheelMany_) click(g.longPressAt, false);
            wheelTab_ = tabDwell_ = 0;
            tabTurned_ = false;
            wheel_ = true;
            wheelUnit_ = u->slot;
            wheelOver_ = -1;
            wheelAt_ = ctx_.engine.pointerOutput();
            wheelBack_ = g.longPressAt;
            ctx_.audio->sePlay(5);
        } else if (downLayer_ == 0) {
            held_ = true;
            holdAt_ = g.longPressAt;
        }
    }
    if (held_ && g.fingers == 1) {
        if (!boxing_ && std::hypot(g.at.x - holdAt_.x, g.at.y - holdAt_.y) > 14.f) {
            boxing_ = true;
            input.queueMouse({.at = holdAt_, .endTick = true});
            input.queueMouse({.at = holdAt_, .button = MouseStep::Left, .down = true, .endTick = true});
        }
        if (boxing_) input.queueMouse({.at = g.at});
    }
    if (held_ && g.lifted) {
        if (boxing_) {
            input.queueMouse({.at = g.at, .endTick = true});
            input.queueMouse({.at = g.at, .button = MouseStep::Left, .down = false, .endTick = true});
        } else {
            click(holdAt_, true);
            if (units_.ownSelected() > 0) ping(holdAt_);
        }
        held_ = boxing_ = false;
    }

    if (g.dragBegan) {
        const TacticsUnits::Unit* u = downLayer_ == 0 ? units_.pointedUnit(int(g.dragStart.x), int(g.dragStart.y)) : nullptr;
        if (u && u->team() == units_.viewer() && u->onField() && !units_.callReady() && !skillHeld()) {
            fling_ = true;
            flingPick_ = !u->selected;
            flingUnit_ = u->slot;
            flingX_ = u->px();
            flingY_ = u->py();
        } else if (downLayer_ == 0) {
            panning_ = true;
            panX_ = map_.viewX();
            panY_ = map_.scrollY();
        } else if (downLayer_ == kHud) {
            hudDrag_ = true;
            input.queueMouse({.at = g.dragStart, .endTick = true});
            input.queueMouse({.at = g.dragStart, .button = MouseStep::Left, .down = true, .endTick = true});
        }
    }
    if (panning_ && (g.dragActive || g.dragEnded))
        map_.scrollTo(panX_ - int(std::lround(g.dragPos.x - g.dragStart.x)), panY_ - int(std::lround(g.dragPos.y - g.dragStart.y)));
    if (hudDrag_ && g.dragActive) input.queueMouse({.at = g.dragPos});
    if (g.dragEnded) {
        if (hudDrag_) {
            input.queueMouse({.at = g.dragPos, .endTick = true});
            input.queueMouse({.at = g.dragPos, .button = MouseStep::Left, .down = false, .endTick = true});
        }
        // Let go back by the unit: no order after all.
        if (fling_ && std::hypot(g.dragPos.x - g.dragStart.x, g.dragPos.y - g.dragStart.y) >= 48.f * float(ctx_.engine.outputSize().y) / 780.f) {
            // The unit where it is now, then the order where the finger lifted.
            const TacticsUnits::Unit& u = units_.unit(flingUnit_);
            if (flingPick_) click({g.dragStart.x + float(u.px() - flingX_), g.dragStart.y + float(u.py() - flingY_)}, false);
            const Point to = snap(g.dragPos);
            click(to, true);
            ping(to);
        }
        panning_ = hudDrag_ = fling_ = false;
    }
}

// The port's keys on the bar's buttons, for a mouse's player: each skill's
// key on the one unit picked, C on the stance it gives next.
void TacticsWide::drawKeys() {
    if (ctx_.engine.input().touch().used || units_.ownSelected() < 1) return;
    static constexpr const char* kNames[8] = {"Q", "E", "R", "T", "F", "G", "V", "B"};
    const auto key = [&](int button, const char* name) {
        const SDL_Rect b = units_.barBox(button);
        ctx_.draw.text({.x = b.x + 8, .y = b.y + 9, .priority = 1, .argb = 0xff000000u, .text = name, .fontPx = 13, .bold = true, .centred = true});
        ctx_.draw.text({.x = b.x + 7, .y = b.y + 8, .priority = 0, .argb = 0xffffe060u, .text = name, .fontPx = 13, .bold = true, .centred = true});
    };
    const TacticsUnits::Unit* one = nullptr;
    for (int i = 0; i < TacticsUnits::kSlots && !one; ++i)
        if (const auto& u = units_.unit(i); u.used && u.selected && u.team() == units_.viewer()) one = &u;
    if (!one) return;
    if (units_.ownSelected() == 1)
        for (int c = 0; c < 8; ++c)
            if (one->data.u16(0x84 + std::uint32_t(c) * 2) != 0) key(4 + c, kNames[c]);
    if (const int next = units_.nextStance(); next >= 0) key(12 + next, "C");
}

void TacticsWide::draw() {
    if (!on_) {
        drawKeys();
        return;
    }
    const auto was = ctx_.draw.setLayer(kHud);
    drawKeys();
    const auto button = [&](const SDL_Rect& r, const char* top, const char* bottom) {
        ysub::fill(ctx_, r.x, r.y, r.w, r.h, 199, 0xe0181410u);
        ysub::box(ctx_, r.x, r.y, r.w - 1, r.h - 1, 198, 0xffa89060u);
        ysub::box(ctx_, r.x + 2, r.y + 2, r.w - 5, r.h - 5, 198, 0xff504530u);
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        if (bottom) {
            ctx_.draw.text({.x = cx, .y = cy - 11, .priority = 197, .argb = 0xffffffffu, .text = top, .fontPx = 17, .bold = true, .centred = true});
            ctx_.draw.text({.x = cx, .y = cy + 11, .priority = 197, .argb = 0xffffffffu, .text = bottom, .fontPx = 17, .bold = true, .centred = true});
        } else {
            ctx_.draw.text({.x = cx, .y = cy, .priority = 197, .argb = 0xffffffffu, .text = top, .fontPx = 17, .bold = true, .centred = true});
        }
    };
    button(kAll, "Select", "all");
    button(kNone, "Clear", nullptr);
    static constexpr const char* kDigits[4] = {"1", "2", "3", "4"};
    for (int i = 0; i < 4; ++i) {
        const int n = units_.groupSize(i + 1);
        const std::string count = n > 0 ? std::to_string(n) + (n == 1 ? " unit" : " units") : std::string("+");
        button(kGroup[i], kDigits[i], count.c_str());
        if (n == 0) ysub::fill(ctx_, kGroup[i].x, kGroup[i].y, kGroup[i].w, kGroup[i].h, 196, 0x80000000u);
        if (keptShow_ > 0 && kept_ == i + 1) ysub::box(ctx_, kGroup[i].x + 1, kGroup[i].y + 1, kGroup[i].w - 3, kGroup[i].h - 3, 196, 0xffffe060u);
        button(kKeep[i], "Keep", kDigits[i]);
    }
    button(kTab, wheelTab_ == 0 ? "Stance" : "Skills", nullptr);
    ctx_.draw.setLayer(0);
    // A long-press on the map: the order goes there when the finger lifts (a box if it moves on).
    if (held_ && !boxing_) {
        const int x = int(holdAt_.x), y = int(holdAt_.y);
        ysub::box(ctx_, x - 22, y - 22, 44, 44, 2, 0xffffff80u);
        ysub::box(ctx_, x - 21, y - 21, 42, 42, 2, 0xffffff80u);
    }
    // Where a finger's order went: a ring closing in.
    if (ping_ > 0) {
        const int x = int(pingAt_.x), y = int(pingAt_.y), r = 6 + ping_, a = std::min(0xff, ping_ * 0x18);
        ysub::box(ctx_, x - r, y - r / 2, 2 * r, r, 2, std::uint32_t(a) << 24 | pingRgb_);
        ysub::box(ctx_, x - r + 1, y - r / 2 + 1, 2 * r - 2, r - 2, 2, std::uint32_t(a) << 24 | pingRgb_);
    }
    // A drag from one of ours: the way its order goes.
    if (fling_) {
        const auto& g = ctx_.engine.input().touch();
        const TacticsUnits::Unit& u = units_.unit(flingUnit_);
        const float x0 = g.dragStart.x + float(u.px() - flingX_), y0 = g.dragStart.y + float(u.py() - flingY_);
        const float dx = g.dragPos.x - x0, dy = g.dragPos.y - y0;
        const int n = std::max(1, int(std::hypot(dx, dy) / 12.f));
        // Red on an enemy, as the order's mark will be.
        const Point to = snap(g.dragPos);
        const TacticsUnits::Unit* on = units_.pointedUnit(int(to.x), int(to.y));
        const std::uint32_t argb = on && units_.relation(on->team()) == 1 ? 0xffff5038u : 0xffffffa0u;
        for (int i = 1; i < n; ++i)
            ysub::fill(ctx_, int(x0 + dx * float(i) / float(n)) - 1, int(y0 + dy * float(i) / float(n)) - 1, 4, 4, 2, argb);
        ysub::box(ctx_, int(to.x) - 14, int(to.y) - 7, 28, 14, 2, argb);
        ysub::box(ctx_, int(to.x) - 13, int(to.y) - 6, 26, 12, 2, argb);
    }
    // Behind the wheel: the map dimmed round it.
    if (wheel_ && mapDst_.w > 0) {
        const float s = mapDst_.w / float(TacticsMap::kViewW + 2 * map_.margin());
        const int n = int(wheelButtons().size());
        const int x = int((wheelAt_.x - mapDst_.x) / s) - map_.margin(), y = int((wheelAt_.y - mapDst_.y) / s) + TacticsMap::kViewY;
        const int r = int(wheelCell_ * ((n > 8 ? 1.75f : 1.35f) + 0.7f) / s);
        ysub::fill(ctx_, x - r, y - r * 2 / 3, 2 * r, r * 4 / 3, 3, 0x58000000u);
        ysub::fill(ctx_, x - r * 2 / 3, y - r, r * 4 / 3, 2 * r, 3, 0x58000000u);
    }
    ctx_.draw.setLayer(was);
}

} // namespace ch
