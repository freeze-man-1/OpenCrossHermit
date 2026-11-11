// ============================================================================
// tactics_wide.hpp — the battle on a wide window or a phone (a port addition)
//
// The original's battle is a 1024 x 768 frame: the map's view (rows 30..597)
// between the top bar and the panels, the roster's faces over its sides. On a
// window much wider than that the frame leaves black at both sides, and on a
// phone its buttons are too small for a finger. While the battle runs free
// (switched off for now: only the debug CH_WIDE=1 turns it on, and the
// battle otherwise keeps its own frame, widened) this shows the frame in pieces instead
// (Engine::setPieces): the map's view as large as the window allows, the
// roster down the left, the minimap and the command bar's buttons, enlarged,
// down the right — in the black where there is some, else over the map's
// edges — and the unit panel and the help lines only while they have
// something to show. Every piece is the game's own drawing, and the pointer
// maps back through the piece it is on, so the battle reads the same mouse
// it always did: nothing here gives an order itself.
//
// A finger (the engine's mouse stand-in is off meanwhile):
//   on the HUD     tap = left click, drag = left drag (the minimap, a face
//                  onto a unit), long-press = right click
//   on the map     tap one of ours = pick it; tap anywhere else with ours
//                  picked = the order there (the right button), and with
//                  only waiting units picked a tap on one of ours calls
//                  them in; drag = move the view; long-press = the order
//                  there whatever is under it, or, dragged on, the box
//   two fingers    move the view
// A long-press on one of ours on the field picks it and opens the wheel: its
// stances and its skills round the finger, the bar's own buttons shown
// there. Sliding onto one and letting go, or a tap on one, clicks that
// button; a skill is then held ready, and the next tap on a target uses it.
// A tap just beside a unit counts as on it.
// With several picked the wheel is the group's: the stances, and the four
// groups to keep them as. A drag that starts on one of ours is its order:
// the unit (or, picked with others, all of them) goes where the finger lifts.
// The groups (the number keys') are four buttons under the view: a tap picks
// a group, a second tap shows it; a tap on an empty one, or a long-press (a
// right click), keeps the picked units as that group.
// A click on the unit panel folds it down to its portrait, and back.
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "tactics_map.hpp"
#include "tactics_units.hpp"

#include <vector>

namespace ch {

class TacticsWide {
public:
    TacticsWide(GameContext& ctx, TacticsMap& map, TacticsUnits& units) : ctx_(ctx), map_(map), units_(units) {}
    ~TacticsWide();

    // Once a frame, before the battle reads the pointer. `free`: the battle
    // runs with nothing over it (no event, sortie or network talk); `menu`:
    // its menu is up, which then shows over the view, enlarged.
    void frame(bool free, bool menu);
    void draw();                                     // its own buttons and marks
    // The port's keys for the eight skills, beside F1..F8.
    static constexpr int kSkillKeys[8] = {SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T,
                                          SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_V, SDL_SCANCODE_B};

    [[nodiscard]] bool on() const noexcept { return on_; }
    // Where the pointer is: over the map's view, or on the HUD. (Both when off: the classic frame.)
    [[nodiscard]] bool onMap() const;
    [[nodiscard]] bool onHud() const;
    // TacticsMap::edges for the window's own edges (a mouse only).
    [[nodiscard]] int edges() const;
    // False until a mouse has moved: a phone has none, and its pointer rests in the corner.
    [[nodiscard]] bool mouseLive() const noexcept { return mouseLive_; }

private:
    void layout(SDL_Point out);
    void buttons();
    void touch();
    void click(legacy::touch::Point at, bool right);
    // The wheel: the bar buttons it shows, where each is in the window, the one at a window point.
    [[nodiscard]] std::vector<int> wheelButtons() const;
    [[nodiscard]] SDL_FRect wheelSlot(int i, int n, bool picked) const;
    [[nodiscard]] int wheelPick(SDL_FPoint p) const;
    void wheelClick(int pick);
    [[nodiscard]] legacy::touch::Point snap(legacy::touch::Point at) const;
    void ping(legacy::touch::Point at);
    [[nodiscard]] bool skillHeld() const;
    [[nodiscard]] SDL_Rect wheelSrc(int code) const;
    void group(int g, bool keep);
    void drawKeys();

    GameContext&  ctx_;
    TacticsMap&   map_;
    TacticsUnits& units_;
    bool on_ = false, menu_ = false;
    bool fingerSized_ = false, resized_ = false;     // the layout's size, and that it changed under a finger
    bool folded_ = false;                            // the unit panel down to its portrait (a click on it)
    bool mouseLive_ = false, mouseKnown_ = false;
    SDL_FPoint mouseFirst_{};
    // A finger's gesture.
    bool fingerWas_ = false;
    int  downLayer_ = 0;                             // the layer it came down on
    bool held_ = false, boxing_ = false;             // a long-press on the map, and its box
    legacy::touch::Point holdAt_;
    bool panning_ = false, hudDrag_ = false;
    int  panX_ = 0, panY_ = 0;                       // the view when the drag began
    legacy::touch::Point pingAt_;                    // where a tap's order went, shown a moment
    int  ping_ = 0;
    std::uint32_t pingRgb_ = 0;
    bool wheel_ = false;
    char wheelKind_ = 't';                           // 'a' stances and skills, 's' skills, 't' the two as tabs
    bool wheelMany_ = false;                         // the group's wheel
    bool tabTurned_ = false;                         // turned by resting on it
    int  wheelTab_ = 0, tabDwell_ = 0;               // 0 skills, 1 stances; ticks the finger has been on the tab
    bool fling_ = false, flingPick_ = false;         // a drag from one of ours, to be picked first
    int  flingUnit_ = -1, flingX_ = 0, flingY_ = 0;  // which, and where it was
    int  kept_ = -1, keptShow_ = 0;                  // the group just kept, lit a moment
    int  wheelUnit_ = -1, wheelOver_ = -1;           // whose, and the button under the finger
    legacy::touch::Point wheelBack_{};               // where the finger went down, in the battle's view
    SDL_FPoint wheelAt_{};                           // its centre in the window
    float wheelCell_ = 0;                            // a button's size there
    int  hudClick_ = 0;                              // ticks a click of the wheel's counts as on the HUD
    SDL_FRect mapDst_{};                             // the map's view in the window
};

} // namespace ch
