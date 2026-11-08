// ============================================================================
// workroom_task.cpp
// ============================================================================
#include "workroom_task.hpp"
#include "workroom_tables.hpp"
#include "../state/exe_image.hpp"
#include "../adv/adv_data.hpp"
#include "../text/sjis.hpp"
#include "../lang/language_pack.hpp"

#include <algorithm>
#include <string>

namespace ch::workroom {
const std::vector<News>& news(GameVersion v) {
    static std::array<std::vector<News>, 2> cache;
    static int generation = textGeneration();
    if (generation != textGeneration()) {
        generation = textGeneration();
        cache = {};
    }
    auto& out = cache[std::size_t(v)];
    if (out.empty()) {
        const exe::Image& img = exe::own(v);
        const std::uint32_t table = v == GameVersion::Jp ? 0x66a928 : 0x750a40;
        for (std::uint32_t r = table + 8; img.contains(r, 8) && std::int8_t(img.u8(r + 1)) != -1 && out.size() < 256; r += 8)
            out.push_back({std::int8_t(img.u8(r + 1)), std::int8_t(img.u8(r + 2)), exe::text(v, img.u32(r + 4))});
    }
    return out;
}
} // namespace ch::workroom

namespace ch {

using namespace workroom;
using ysub::drawRect;
using ysub::fadeBlack;
using ysub::hit;

namespace {
// Rect-table rows by their original address (0x61b050 + 16 * index).
enum : std::size_t {
    kCursor = 0, kMapBg = 1, kTopBg = 2, kMonthDigits = 6, kWeekDigits = 7,
    kTopButtonA = 14, kTopButtonB = 15, kBackFlash = 16, kNewsScrollFlash = 17,
    kTextScrollFlash = 21, kPlaceLabels = 25,
};
constexpr std::uint32_t kSe = 5;   // FUN_004db2b0(5)

// A rect-table entry, or what a language pack's layout.txt draws in its place (as the opening's
// HataTask::opSprite): the date's words and digits, whose English needs other shapes.
void drawLaid(GameContext& ctx, const ysub::Rect& r, int prio, int x, int y, int offX = 0, int offY = 0) {
    const int sx = r.sx + offX, sy = r.sy + offY;
    const auto* parts = lang::spriteParts("data/workroom/workroom.bin", r.frame, sx, sy, sx + r.w, sy + r.h);
    if (!parts) {
        drawRect(ctx, r, prio, x, y, offX, offY);
        return;
    }
    for (const lang::SpritePart& p : *parts)
        drawRect(ctx, {.bank = r.bank, .frame = std::uint16_t(p.frame), .sx = std::int16_t(p.x0), .sy = std::int16_t(p.y0),
                       .w = std::int16_t(p.x1 - p.x0), .h = std::int16_t(p.y1 - p.y0)}, prio, x + p.dx, y + p.dy);
}

// Scroll buttons, top to bottom: to top, up, down, to bottom.
constexpr std::array<int, 4> kScrollCmd{2, 0, 1, 3};

int dateKey(int month, int week) { return month * 5 - 1 + week; }
} // namespace

WorkRoomTask::~WorkRoomTask() {   // FUN_004a1430
    anm_.freeRoot(anmRoot_);
    ctx_.sprites->unloadBank(kBank);
    ctx_.sprites->unloadBank(kBgBank);
    ctx_.sprites->unloadBank(kAnmBank);
}

// FUN_004a1570
Co<> WorkRoomTask::start(std::uint32_t) {
    init();
    for (int state = 1; state != 4;) {
        switch (state) {
            case 1: state = co_await topScreen(); break;
            case 2: state = co_await mapScreen(); break;
            default: state = 4; break;    // state 3 (FUN_004a0020) is never entered
        }
    }
    co_await yieldFrame();
    if (!ctx_.work.workRoomEventSeen()) {
        ctx_.work.setWorkRoomEventSeen(true);                 // DAT_007a4e62
        ctx_.adv = AdvRequest{"Data\\Adv\\dat\\CH002.ybc", 9};
        ctx_.scenes.request(SceneId::AdvScript);
    } else {
        ctx_.work.setAreaBgmOn(true);                         // DAT_007e11a0
        ctx_.scenes.request(SceneId::HanKojin);
    }
}

// FUN_0049f6c0
void WorkRoomTask::init() {
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\WorkRoom\\WorkRoom.bin"));
    const auto& bgNames = adv_media::bgNames(ctx_.version);
    const int bg = std::clamp<int>(ctx_.work.day(), 0, int(bgNames.size()) - 1);
    ctx_.sprites->loadBank(kBgBank, ctx_.readData("data\\adv\\bin\\" + bgNames[std::size_t(bg)]));
    if (!ctx_.work.areaBgmOn()) ctx_.audio->bgmPlay(static_cast<std::uint32_t>(ctx_.work.areaBgm()), 1);   // FUN_004db010

    // 12 px GDI font (CreateFontA(0xc, ...)) for all three lists.
    placeText_.setFont(12, 9);
    placeText_.load(ctx_, "data\\YsText\\MapPos0401.ybc");
    placeText_.select(0, 9);
    newsText_.setFont(12, 9);
    newsText_.load(ctx_, "data\\YsText\\words.ybc");
    newsText_.select(0, 9);
    newsTitles_.setFont(12, 0x29);

    // Titles whose date has come, newest first (exchange sort, as the original).
    const auto& news = workroom::news(ctx_.version);
    const int now = dateKey(ctx_.work.month(), ctx_.work.week());
    std::vector<std::pair<int, int>> list;            // {date, table row (1-based)}
    for (std::size_t i = 0; i < news.size(); ++i) {
        const int d = dateKey(news[i].month, news[i].week);
        if (d <= now) list.emplace_back(d, int(i) + 1);
    }
    for (std::size_t i = 0; i < list.size(); ++i)
        for (std::size_t j = i + 1; j < list.size(); ++j)
            if (list[i].first < list[j].first) std::swap(list[i], list[j]);
    newsRow_.clear();
    for (const auto& [d, row] : list) {
        newsRow_.push_back(static_cast<std::int16_t>(row));
        newsTitles_.addUtf8(news[std::size_t(row - 1)].title);
    }

    anmFile_.load(ctx_.readData("data\\AllResult\\AnmAllResult.bin"), kAnmBank, *ctx_.sprites);   // FUN_0049f660
    anmRoot_ = anm_.newRoot(anmFile_);
}

void WorkRoomTask::drawCursorAndFlashes() {
    const auto& m = ctx_.mouse();
    drawRect(ctx_, rectRow(ctx_.version, kCursor), 0x33, m.x, m.y);
    flashes_.draw(ctx_);                                 // FUN_004d69e0
}

// ------------------------------------------------------------ top screen ---
// FUN_0049faa0: date plates over the day's background.
void WorkRoomTask::drawTop() {
    for (auto [row, prio] : {std::pair{3, 0x39}, {4, 0x39}, {5, 0x39}, {10, 0x3a}, {11, 0x3a}}) {
        const ysub::Rect& r = rectRow(ctx_.version, std::size_t(row));
        drawLaid(ctx_, r, prio, r.x, r.y);
    }
    const int month = (ctx_.work.month() - 1) % 12, week = (ctx_.work.week() - 1) % 9;
    const ysub::Rect& md = rectRow(ctx_.version, kMonthDigits);
    drawLaid(ctx_, md, 0x39, md.x, md.y, (month / 6) * md.w, (month % 6) * md.h);
    const ysub::Rect& wd = rectRow(ctx_.version, kWeekDigits);
    drawLaid(ctx_, wd, 0x39, wd.x, wd.y, (week / 5) * wd.w, (week % 5) * wd.h);
    drawRect(ctx_, rectRow(ctx_.version, kTopBg), 0x3d, 0, 0);
    drawCursorAndFlashes();
}

// FUN_0049fd50: returns 2 (map) or 4 (leave).
Co<int> WorkRoomTask::topScreen() {
    constexpr int kFade = 0x1f;
    for (int i = 0; i < kFade; ++i) {
        fadeBlack(ctx_, 0xFF - i * 0xFF / (kFade - 1));
        drawTop();
        co_await yieldFrame();
    }
    int next = 1;
    for (;;) {
        if (hit(ctx_, 0x28e, 0x2d6, 0x8d, 0x1e).pressed & legacy::kMouseLeft) {
            next = 2;
            ctx_.audio->sePlay(kSe);
            const ysub::Rect& r = rectRow(ctx_.version, kTopButtonA);
            flashes_.add(r, 0x38, r.x, r.y, 8);
            break;
        }
        if (hit(ctx_, 0x34f, 0x2d6, 0x8d, 0x1e).pressed & legacy::kMouseLeft) {
            next = 4;
            ctx_.audio->sePlay(kSe);
            const ysub::Rect& r = rectRow(ctx_.version, kTopButtonB);
            flashes_.add(r, 0x38, r.x, r.y, 8);
            break;
        }
        drawTop();
        co_await yieldFrame();
    }
    for (int i = 0; i < kFade; ++i) {
        fadeBlack(ctx_, i * 0xFF / kFade);
        drawTop();
        co_await yieldFrame();
    }
    co_return next;
}

// ------------------------------------------------------------ map screen ---
// FUN_004a0ad0: hovering a place shows its label; clicking pins it and
// shows its description.
void WorkRoomTask::pickSpot() {
    hoverSpot_ = -1;
    for (std::size_t i = 0; i < kSpots.size(); ++i) {
        const Spot& s = kSpots[i];
        const ysub::Hit h = hit(ctx_, s.x, s.y, s.w, s.h);
        if (!h.over) continue;
        hoverSpot_ = static_cast<std::int16_t>(i);
        hoverX_ = s.labelX; hoverY_ = s.labelY;
        if (h.pressed & legacy::kMouseLeft) {
            ctx_.audio->sePlay(kSe);
            placeText_.select(int(i) + 1, 9);
            viewer_ = 0;
            pickedNews_ = -1;
            pickedSpot_ = static_cast<std::int16_t>(i);
            pickedX_ = s.labelX; pickedY_ = s.labelY;
            pinX_ = s.pinX; pinY_ = s.pinY;
            return;
        }
    }
}

// FUN_004a01a0
void WorkRoomTask::drawMap() {
    // News titles: 41 rows of 14 px; hover glow and the picked row in additive fills.
    const int top = newsTitles_.pos();
    for (int row = 0; row < 0x29 && row + top <= newsTitles_.count() - 1; ++row) {
        const int y = 0x4f + row * 0xe;
        const ysub::Hit h = hit(ctx_, 0x3c, y, 0xb7, 0xd);
        if (h.over) {
            ysub::fill(ctx_, 0x3c, y, 0xb7, 0xd, 0x38, 0x8020a000u, kBlendAdd);
            if (h.pressed & legacy::kMouseLeft) {
                ctx_.audio->sePlay(kSe);
                viewer_ = 1;
                pickedSpot_ = -1;
                pickedNews_ = static_cast<std::int16_t>(row + top);
                newsText_.select(newsRow_[std::size_t(pickedNews_)], 9);
            }
        }
        if (pickedNews_ == row + top) ysub::fill(ctx_, 0x3c, y, 0xb7, 0xd, 0x38, 0xa0c0a000u, kBlendAdd);
    }
    for (int i = 0; i < 4; ++i) {                        // news list scroll buttons
        if (!(hit(ctx_, 0xf8, i * 0x91 + 0x4e, 0x14, 0x8d).pressed & legacy::kMouseLeft)) continue;
        ctx_.audio->sePlay(kSe);
        newsTitles_.scroll(kScrollCmd[std::size_t(i)]);
        flashes_.add(rectRow(ctx_.version, kNewsScrollFlash + std::size_t(i)), 0x3c, 0xf6, i * 0x91 + 0x4c, 8);
    }
    newsTitles_.draw(ctx_, 0x3c, 0x4f, 0x3c);

    if (viewer_ == 0 || viewer_ == 1) {                  // text viewer and its scroll buttons
        ysub::TextList& text = viewer_ == 0 ? placeText_ : newsText_;
        for (int i = 0; i < 4; ++i) {
            if (!(hit(ctx_, 0x3ab, i * 0x22 + 0x232, 0x14, 0x1e).pressed & legacy::kMouseLeft)) continue;
            ctx_.audio->sePlay(kSe);
            text.scroll(kScrollCmd[std::size_t(i)]);
            flashes_.add(rectRow(ctx_.version, kTextScrollFlash + std::size_t(i)), 0x3c, 0x3a9, i * 0x22 + 0x230, 8);
        }
        text.draw(ctx_, 0x120, 0x236, 0x3c);
    }
    drawRect(ctx_, rectRow(ctx_.version, kMapBg), 0x3d, 0, 0);
    drawCursorAndFlashes();
}

// FUN_004a0760: returns 1 (back to the top screen).
Co<int> WorkRoomTask::mapScreen() {
    anm_.play(anmRoot_, 0, 4, 0);
    viewer_ = 0;
    pickedNews_ = -1;
    pickedSpot_ = -1;
    flashes_.clear();
    placeText_.select(0, 9);
    for (int i = 0; i < 0x10; ++i) {
        fadeBlack(ctx_, 0xFF - i * 0xFF / 0xf);
        drawMap();
        co_await yieldFrame();
    }
    const AnmPlayer::SpriteTemplate pin{.color = {0x80, 0x80, 0x80, 0xFF}};   // FUN_004077c0 defaults
    const ysub::Rect& labels = rectRow(ctx_.version, kPlaceLabels);
    for (;;) {
        pickSpot();
        if (hoverSpot_ != -1)
            drawLaid(ctx_, labels, 0x3c, hoverX_, hoverY_, (hoverSpot_ / 20) * 200, (hoverSpot_ % 20) * 0x32);
        if (pickedSpot_ != -1) {
            drawLaid(ctx_, labels, 0x3c, pickedX_, pickedY_, (pickedSpot_ / 20) * 200, (pickedSpot_ % 20) * 0x32);
            anm_.update(anmRoot_);
            anm_.draw(anmRoot_, ctx_.draw, *ctx_.sprites, pin, pinX_, pinY_, 0x3b);
        }
        if (hit(ctx_, 0x42, 0x299, 0xc2, 0x1c).pressed & legacy::kMouseLeft) break;   // back
        drawMap();
        co_await yieldFrame();
    }
    ctx_.audio->sePlay(kSe);
    flashes_.add(rectRow(ctx_.version, kBackFlash), 0x3c, 0x36, 0x295, 8);
    for (int i = 0; i < 4; ++i) { drawMap(); co_await yieldFrame(); }
    for (int i = 0; i < 0x10; ++i) {
        fadeBlack(ctx_, i * 0xFF / 0xf);
        drawMap();
        co_await yieldFrame();
    }
    co_return 1;
}

} // namespace ch
