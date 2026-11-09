// ============================================================================
// kojin_blend.hpp — the character screen in the New UI style: one page in
// the manner of FFT's equipment screen (summary on top, gear and skills,
// details at the side) drawn from this game's own art. A port addition.
//
// It runs inside KojinTask (KojinTask::frame hands the frame over while
// ui::modern()), and every rule goes through KojinTask and game_logic as the
// classic tabs do: the same records, derived values, equip checks, learning,
// class change and stat raising, so the numbers and saves match.
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "../ysub/ysub.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ch {

class KojinTask;

class KojinBlend {
public:
    KojinBlend(KojinTask& task, GameContext& ctx);
    void open();     // the screen opened (KojinTask::open)
    void frame();    // one frame, in place of the classic tabs

    // What the summary shows, taken from the task's derived values.
    struct Snap {
        std::array<int, 25> v{};
        bool operator==(const Snap&) const = default;
    };

private:
    struct Box {
        int x = 0, y = 0, w = 0, h = 0;
        [[nodiscard]] bool in(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    };
    enum class Kind : std::uint8_t { None, Gear, Skill, Item, SkillCard };

    // ---- drawing
    void fill(int x, int y, int w, int h, std::uint32_t argb, int prio);
    void well(const Box& b, int prio, bool lit = false, bool gold = false);     // a black slot with a silver line
    void stone(const Box& b, int prio);                                       // the frosted stone, tiled
    void frameBorder(const Box& b, int t, int prio);                          // the gold rail frame
    void panel(const Box& b, int prio);
    void plate(const Box& b, bool gold, int prio, bool lit = false, int capW = 26, std::uint8_t alpha = 0xff);   // silver / gold title plate
    void vgrad(const Box& b, std::uint32_t top, std::uint32_t bottom, int prio);
    void outline(const Box& b, std::uint32_t argb, int prio);
    void cell(const Box& b, int prio, bool hot, bool mine, bool sel);          // a list cell
    void legend(const std::vector<std::pair<int, std::string>>& entries, int y);
    bool tool(const Box& b, const std::string& label, int prio);
    bool arrow(const Box& b, const std::string& glyph, int px, int prio, bool enabled);
    void lock(int x, int y, int prio);
    void roundBox(const Box& b, int r, std::uint32_t fillArgb, std::uint32_t lineArgb, int prio);
    [[nodiscard]] int buttonW(const std::string& label, int px = 14) const;
    void sub(int frame, int sx, int sy, int sw, int sh, int x, int y, int dw, int dh, int prio,
             std::uint8_t alpha = 0xff, std::uint8_t rgb = 0x80, std::uint16_t flags = 0);
    int  text(int x, int y, const std::string& s, int px, std::uint32_t argb, int prio, bool bold = false,
              int align = 0, bool shadow = true);                              // align: 0 left, 1 centre, 2 right; returns width
    [[nodiscard]] int textW(const std::string& s, int px, bool bold = false) const;
    // The same in the headings' small capitals, or in the plain UI font (keys, tools).
    int  caps(int x, int y, const std::string& s, int px, std::uint32_t argb, int prio, bool bold = true, int align = 0, bool shadow = true);
    int  sans(int x, int y, const std::string& s, int px, std::uint32_t argb, int prio, bool bold = false, int align = 0, bool shadow = false);
    enum Style : std::uint8_t { kSerif, kCaps, kSans };
    Style style_ = kSerif;                            // text() and textW()'s face
    std::vector<std::string> wrap(const std::string& s, int px, int width) const;
    int  paragraph(int x, int y, int w, const std::string& s, int px, std::uint32_t argb, int prio, int maxLines = 99);
    bool button(const Box& b, const std::string& label, int prio, bool on = false, bool enabled = true, int px = 14);
    void itemIcon(int item, int x, int y, int size, int prio, std::uint8_t alpha = 0xff);
    void skillIcon(int skill, int x, int y, int size, int prio, std::uint8_t alpha = 0xff);
    void jobIcon(int job, int x, int y, int size, int prio, int look = 0);     // look 1: locked silhouette
    void face(int unit, int x, int y, int w, int h, int prio, std::uint8_t alpha);
    void statIcon(int kind, int x, int y, int prio);                          // item effect kind 8..

    // ---- input
    [[nodiscard]] bool over(const Box& b) const;
    [[nodiscard]] bool clicked(const Box& b) const;
    [[nodiscard]] bool pressed(const Box& b) const;

    // ---- model
    [[nodiscard]] int unit() const;
    [[nodiscard]] std::uint32_t rec() const;
    [[nodiscard]] Snap snap() const;
    Snap trial(const std::function<void()>& change);
    void recompute();
    [[nodiscard]] std::string unitName(int id) const;
    [[nodiscard]] std::string fullName(int id) const;
    [[nodiscard]] std::string className(int job) const;
    [[nodiscard]] std::string itemName(int item) const;
    [[nodiscard]] std::string passage(ysub::TextList& list, std::map<int, std::string>& cache, int entry);
    struct SkillInfo { std::string name, text; bool passive = false; int cat = 0, tier = 0, mp = 0, price = 0, prep = 0, recov = 0, aim = 0, range = 0; };
    [[nodiscard]] SkillInfo skillInfo(int id);
    [[nodiscard]] int itemType(int item) const;
    [[nodiscard]] int ownerOf(int item) const;
    [[nodiscard]] int itemState(int item) const;
    [[nodiscard]] bool wearable(int item) const;
    [[nodiscard]] int gearIn(int k) const;
    [[nodiscard]] int skillIn(int k) const;
    [[nodiscard]] int skillState(int id) const;
    [[nodiscard]] int gearTarget(int item) const;
    [[nodiscard]] int skillTarget() const;
    [[nodiscard]] bool isOpen(int job) const;
    [[nodiscard]] int jobLevel(int job) const;
    void equipItem(int k, int item);
    void equipSkill(int k, int id);
    void takeFrom(int item);
    void putOn(int k, int item);
    bool learn(int id);
    void changeClass(int job);
    void showStudent(int i);
    Snap* previewFor(Kind kind, int id);

    // stats up
    [[nodiscard]] int statLevel(int c) const;
    [[nodiscard]] int statCost(int c, int add) const;
    [[nodiscard]] int totalCost(const std::array<int, 7>& adds) const;
    [[nodiscard]] bool canRaise(int c) const;
    bool stepStat(int c, bool up);
    void raiseStats();
    void applyStats();

    // ---- parts of the page
    void drawBackground();
    void drawHeader();
    void drawTop();
    void drawStats();
    void drawStatTip(int c, int y);
    void drawMain();
    void drawSlots();
    void drawBrowser();
    void drawClasses();
    void drawSide();
    void sideDetails(const Box& b);
    int  hero(const Box& b, const std::function<void(int, int)>& icon, const std::string& name, const std::string& sub1,
              const std::string& sub2 = {});
    int  kv(int x, int y, int w, const std::string& k, const std::string& value, std::uint32_t argb);
    int  desc(int x, int y, int w, const std::string& body, int bottom);
    bool actStrip(int y, const std::vector<std::pair<std::string, std::uint32_t>>& notes, const std::string& label, bool on = false,
                  bool enabled = true);
    [[nodiscard]] int actStripH(const std::vector<std::pair<std::string, std::uint32_t>>& notes, bool button) const;
    void sideItem(const Box& b, int item, bool actions);
    void sideSkill(const Box& b, int id, bool actions);
    void sideClass(const Box& b, int job);
    void sideLocked(const Box& b, int job);
    void sidePicker(const Box& b);
    void drawKeys();
    void radar(int cx, int cy, int r, int job, int prio);

    KojinTask&   t_;
    GameContext& ctx_;

    // pointer
    int  mx_ = 0, my_ = 0, downX_ = -1, downY_ = -1;
    bool press_ = false, release_ = false, held_ = false, dragged_ = false;
    int  wheel_ = 0;

    // page state
    Kind focus_ = Kind::None;  int focusK_ = -1;      // a slot chosen on the left
    Kind card_ = Kind::None;   int cardId_ = 0;       // an item or skill clicked in a list
    Kind shown_ = Kind::None;  int shownId_ = 0;      // pointed at in a slot
    int  browse_ = 0;                                 // 1 Gear List, 2 Skill List
    int  itemFilter_ = 0, listScroll_ = 0;
    bool classes_ = false;  int classPick_ = 0, classShown_ = 0;
    std::array<int, 7> adds_{};                       // attribute levels waiting for Apply
    int  holdC_ = -1, holdTimer_ = 0, holdN_ = 0; bool holdUp_ = true;
    int  stripScroll_ = 0;
    int  tipAttr_ = -1, tipY_ = 0;
    int  learnAsk_ = 0;                               // a skill whose Learn waits for a second press

    // what the pointer is on this frame, for next frame's preview: 1 a picker row, 2 a list cell or
    // card, 3 a class
    int  wantKind_ = 0, wantId_ = 0;
    // preview of the summary for what the pointer is on
    bool havePreview_ = false;  Snap preview_{};
    std::uint64_t previewKey_ = ~0ull;

    ysub::TextList skillText_, itemText_, jobText_, capText_;
    std::map<int, std::string> skillCache_, itemCache_, jobCache_, capCache_;
    std::map<std::string, std::string> profileNames_; // short name → full name, from the glossary's profiles
    std::string unitShown_;                           // for the Escape order: overlays close first
};

} // namespace ch
