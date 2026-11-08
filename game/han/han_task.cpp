// ============================================================================
// han_task.cpp
// ============================================================================
#include "han_task.hpp"
#include "../adv/adv_scene.hpp"
#include "../option/options.hpp"
#include "../state/exe_image.hpp"
#include "../state/game_logic.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace ch {

using ysub::drawRect;
using ysub::fill;

namespace {
constexpr std::uint32_t kSe = 5;            // FUN_004db2b0(5): button
constexpr std::uint32_t kSeYes = 6, kSeNo = 1;

// Work fields (ch.exe VAs) used by the class screen.
constexpr std::uint32_t kKojinState  = 0x7a4e60;   // s16: 0 idle, 1 show KojinTask, 99 scene over
constexpr std::uint32_t kPlanFixed   = 0x7a55fa;   // s16: groups cannot be changed (cleared at init)
constexpr std::uint32_t kGroups      = 0x7aaa12;   // 5 x 0x1c: +0 leader, +3 lecture flag, +6.. choice, +0x10 members[4]
constexpr std::uint32_t kGroupStride = 0x1c;
constexpr std::uint32_t kEventSet    = 0x7aab0a;   // s16: an adventure is chosen
constexpr std::uint32_t kEventId     = 0x7aab16;   // s16: chosen adventure (0x73bed0 row)
constexpr std::uint32_t kSelGroup    = 0x7d57d8;   // s8: selected group, -1 none (FUN_004a8bc0 reads it)
constexpr std::uint32_t kCanCommit   = 0x7d6a26;   // u8
constexpr std::uint32_t kSaveMode    = 0x7e11a8;   // s16: 1 load, 2 save (SLProg)
constexpr std::uint32_t kEvents      = 0x73bed0;   // 0x100-byte adventure records

// Waiting lists (members not in a group, guests not leading one).
constexpr std::uint32_t kWaitCount   = 0x7d57de;   // s16, list at 0x7d57e2; 0x7d57e0 roster size
constexpr std::uint32_t kWaitList    = 0x7d57e2;
constexpr std::uint32_t kWaitPage    = 0x7d57db;   // u8, max at 0x7d57dc
constexpr std::uint32_t kWaitSort    = 0x7d57da;   // u8: 0 level, 1 job, 2 total stats
constexpr std::uint32_t kGuestCount  = 0x7d58b0;   // s16, list at 0x7d58b4; 0x7d58b2 guest total
constexpr std::uint32_t kGuestList   = 0x7d58b4;
constexpr std::uint32_t kGuestPage   = 0x7d58ac;   // s16, max at 0x7d58ae
constexpr std::uint32_t kGuestSort   = 0x7d58aa;   // s16
// The unit being dragged (state 6).
constexpr std::uint32_t kDragLeader  = 0x7d598e;   // u8: 0 member, 1 guest (leader)
constexpr std::uint32_t kDragFromGrp = 0x7d598f;   // u8: 0 from a waiting list, 1 from a group
constexpr std::uint32_t kDragGroup   = 0x7d5990;   // s16
constexpr std::uint32_t kDragIndex   = 0x7d5992;   // s16: list index or member slot
constexpr std::uint32_t kDragId      = 0x7d5994;   // s16
constexpr std::uint32_t kDragGrade   = 0x7d5996;   // s16: source group's affinity grade
constexpr std::uint32_t kPicked      = 0x7d57c2;   // s16: last unit picked up (frame drawn round it)
// Unit info popup (drawn by FUN_004a3240, piece 4).
constexpr std::uint32_t kPopupOpen   = 0x7d57c6;

// The chosen adventure.
constexpr std::uint32_t kEventCat    = 0x7aab18;   // s16: list (category) it was picked from
constexpr std::uint32_t kEventShown  = 0x7aab08;   // s16: category the panel shows
// This week's adventures (0x10-byte records, newest first): +0 id, +2 1,
// +3 type-1, +4 sub, +5 category, +6 under way, +8 deadline (-1 none),
// +0xa duration, +0xc weeks under way, +0xe weeks listed.
constexpr std::uint32_t kAdvList     = 0x7a5662;
constexpr std::uint32_t kAdvCount    = 0x7a5b62;
constexpr std::uint32_t kAdvSeen     = 0x7a55fc;   // u8[0x65]: already listed
// Lectures each hermit can teach: 20 x 1000 bytes of 10-byte {u8 known,
// u8 used, s16 id, s16 +4 of the lecture, s16 0}, counts at 0x7aa9ea.
constexpr std::uint32_t kHermitLectures = 0x7a5bca;
constexpr std::uint32_t kLectureSeen = 0x7a5b64;   // u8[0x65]
constexpr std::uint32_t kLectures    = 0x74bed0;   // 0x60-byte lecture records

// Panel rows built by FUN_004a1c70 (adventures, 3 categories) and
// FUN_004a2980 (lectures, 3 levels).
constexpr std::uint32_t advEntry(int cat, int i) { return 0x7d599a + std::uint32_t(i) * 10 + std::uint32_t(cat) * 800; }
constexpr std::uint32_t lecEntry(int lvl, int i) { return 0x7d630c + std::uint32_t(i) * 6 + std::uint32_t(lvl) * 600; }

void selectAdventure(GameWork& w, int cat, int i) {
    const std::uint32_t rec = advEntry(cat, i);
    w.set16(0x7aab0a, 1);
    w.set16(0x7aab0c, i & 1);
    w.set16(0x7aab0e, i / 2);
    w.set16(0x7aab10, w.s16(rec));          // type
    w.set16(0x7aab12, w.s16(rec + 2));      // sub
    w.set16(0x7aab16, w.s16(rec + 4));      // id
    w.set16(kEventCat, cat);
    w.set16(0x7aab14, i);
}

constexpr std::uint32_t group(int g) { return kGroups + std::uint32_t(g) * kGroupStride; }

// Rect-table rows (DAT_0061b208 + 16 i).
enum : int {
    kFrame = 0, kMenuHover = 5, kSelectedGroup = 0x20, kPlanFixedMark = 0x22,
    kDigitsSmall = 0x2b, kDigitsLarge = 0x2c, kTitleHover = 0x3a,
    kDialog = 0x3e, kDialogYes = 0x3f, kDialogNo = 0x40,
    kMemberFace = 1, kGuestFace = 2, kMemberName = 3, kGuestName = 4,
    kPickedFrame = 0x21, kJobIcons = 0x2d,
};
constexpr int kMenuHit = 0x2c;               // hit-table rows of the six menu entries

// Dialog lines: ch.exe DAT_005a09c8 / 5a0a24 / 5a09fc, JP exe 0x50b3a4 / 50b3fc / 50b3dc.
struct DialogText { std::uint32_t title, commit, sure; };
constexpr DialogText kDialogJp{0x50b3a4, 0x50b3fc, 0x50b3dc};
constexpr DialogText kDialogTw{0x5a09c8, 0x5a0a24, 0x5a09fc};

} // namespace

// FUN_004ab650 (FUN_004ab760 deleting): FUN_0041f4e0(0xc), FUN_004d1bf0 frees
// the font, the YsText lists' and the anm player's destructors are the members'.
HanTask::~HanTask() {
    for (AnmPlayer::NodeId n : affinityAnm_) anm_.freeRoot(n);
    ctx_.sprites->unloadBank(kBank);
    ctx_.sprites->unloadBank(kAnmBank);
}

ysub::Rect HanTask::rect(int i) const { return ysub::rectAt(0x61b208 + std::uint32_t(i) * 0x10, ctx_.version); }

HanTask::Button HanTask::button(int b) const {
    const std::uint32_t a = 0x61b628 + std::uint32_t(b) * 0x12;
    const auto s = [&](std::uint32_t o) { return logic::sdS16(a + o, ctx_.version); };
    return {s(0), s(2), s(4), s(6), s(8), s(10), s(12), s(14),
            logic::sdU8(a + 0x10, ctx_.version), logic::sdU8(a + 0x11, ctx_.version)};
}

// ------------------------------------------------------------------ start ---
// FUN_004ab7a0
Co<> HanTask::start(std::uint32_t) {
    init();
    exit_ = 0;
    again_ = true;
    do {
        const int command = co_await visit();
        co_await yieldFrame();
        switch (command) {
            case 1: exit_ = 2; again_ = false; break;      // LOAD
            case 2: exit_ = 3; again_ = false; break;      // SAVE
            case 3: exit_ = 1; again_ = false; break;      // 教務室
            case 7: exit_ = 4; again_ = false; break;      // Title
            case 8:                                        // 予定進行
                writeSortie();                             // FUN_004a6a10
                exit_ = 0; again_ = false;
                if (ctx_.work.month() == 0xf && ctx_.work.week() == 5) {
                    // FUN_0049ee30: the last week's wrap-up, ported with AllResult.
                    exit_ = 5;
                }
                break;
            default: break;
        }
    } while (again_);
    ctx_.work.set16(kKojinState, 99);
    switch (exit_) {
        case 0:
            ctx_.audio->bgmFadeOut(0x5a);
            while (!ctx_.audio->bgmFadeFinished()) co_await yieldFrame();
            ctx_.audio->bgmStop();
            ctx_.scenes.request(SceneId::Preparation);
            break;
        case 1: ctx_.scenes.request(SceneId::WorkRoom); break;
        case 2: case 3: ctx_.scenes.request(SceneId::SaveLoad); break;
        case 4: ctx_.scenes.request(SceneId::Title); break;
        case 5: ctx_.scenes.request(SceneId::WeekStart); break;
        default: break;
    }
    flashes_.clear();
    co_await yieldFrame();
}

// ------------------------------------------------------------------- init ---
// FUN_004a5ce0, with FUN_004a5a60 inlined.
void HanTask::init() {
    GameWork& w = ctx_.work;
    const int shownCategory = w.s16(kEventCat);      // DAT_007aab18, before the lists are rebuilt
    w.setWorkRoomEventSeen(true);                    // DAT_007a4e62 = 1
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\menu\\HanData.bin"));
    anmFile_.load(ctx_.readData("data\\menu\\AnmHan.bin"), kAnmBank, *ctx_.sprites);   // FUN_004a8140
    for (std::size_t g = 0; g < 5; ++g) {                                            // FUN_00409ef0
        affinityAnm_[g] = anm_.newRoot(anmFile_);
        affinityShown_[g] = -1;
    }

    // 15 px GDI font (CreateFontA(0xf, ...)).
    tacticsText_.setFont(15, 10);
    tacticsText_.load(ctx_, "data\\YsText\\TacticsExp.ybc");
    lectureText_.setFont(15, 10);
    lectureText_.load(ctx_, "data\\YsText\\LectureExp.ybc");
    dialog_.setFont(15, 5);

    fade_ = 0;
    state_ = 2;
    w.set16(0x7d5982, 0);
    w.set8(0x7d5984, 0); w.set8(0x7d5985, 0); w.set8(0x7d5986, 0);
    w.set16(0x7d5988, -1);
    w.set8(0x7d598a, 0); w.set8(0x7d598b, 0); w.set8(0x7d598c, 0); w.set8(0x7d598d, 0);
    w.set8(0x7d57d9, 0xff);
    w.set16(kKojinState, 0);
    w.set16(0x7d57c6, 0);                            // popup closed
    w.set16(0x7d57cc, 0);
    w.set16(0x7d57ce, 0);
    adventureEntry_ = lectureEntry_ = -1;            // +0x128e / +0x1292
    statRanks();                                     // FUN_004a9ab0
    w.set16(kPlanFixed, 0);
    addWeekAdventures();                             // FUN_004a17b0
    learnLectures();                                 // FUN_004a2ba0

    selectGroup(-1);                                 // FUN_004a8b50(-1)
    buildAdventureLists();                           // FUN_004a1c70 (may fix the plan to a forced event)
    buildWaitingLists();                             // FUN_004a5f40
    if (w.s16(kPlanFixed) != 0) {
        for (int g = 0; g < 5; ++g) w.set8(group(g) + 3, w.s16(group(g)) == -1 ? 1 : 0);
    }
    // Keep last visit's adventure selected if it is still listed.
    if (w.s16(kEventSet) != 0 && w.s16(kPlanFixed) == 0) {
        w.set16(kEventSet, 0);
        const std::uint32_t cat = std::uint32_t(shownCategory);
        for (int c = 0; c < w.u16(0x7d62fa + cat * 2); ++c) {
            const std::uint32_t rec = advEntry(shownCategory, c);
            if (w.u16(kEventId) != w.u16(rec + 4)) continue;
            selectAdventure(w, shownCategory, c);
            break;
        }
    }
    if (!w.areaBgmOn()) ctx_.audio->bgmPlay(static_cast<std::uint32_t>(w.areaBgm()), 1);   // FUN_004db010
    sortWaiting(ctx_.work, 0, 1);                    // FUN_004a8bf0(0, 1) / (0, 0)
    sortWaiting(ctx_.work, 0, 0);
}

// FUN_004a8b50: select a group and list its leader's lectures.
void HanTask::selectGroup(int g) {
    ctx_.work.set8(kSelGroup, g);
    buildLectureList(g >= 0 ? ctx_.work.s16(group(g)) - 0x65 : -1);
}

// ---------------------------------------------------------- group status ---
// FUN_004a95f0. Also records the roster slots of each group's leader and
// members (DAT_007aaaa4.. / 7aaaae.. / 7aaab8.. / 7aaae0..).
HanTask::GroupStatus HanTask::groupStatus(GameWork& w, int g) {
    const std::uint32_t gi = std::uint32_t(g);
    w.set16(0x7aaaae + gi * 2, -1);
    w.set16(0x7aaaa4 + gi * 2, -1);
    for (std::uint32_t k = 0; k < 4; ++k) {
        w.set16(0x7aaae0 + gi * 8 + k * 2, -1);
        w.set16(0x7aaab8 + gi * 8 + k * 2, -1);
    }
    GroupStatus st;
    const int leader = w.s16(group(g));
    if (leader == -1) return st;

    std::array<int, 5> ids{leader};
    int n = 1, members = 0;
    w.set16(0x7aaaae + gi * 2, logic::rosterSlot(w, leader));
    w.set16(0x7aaaa4 + gi * 2, leader);
    for (std::uint32_t k = 0; k < 4; ++k) {
        const int id = w.s16(group(g) + 0x10 + k * 2);
        if (id == -1) continue;
        ids[std::size_t(n++)] = id;
        ++members;
        w.set16(0x7aaae0 + gi * 8 + k * 2, logic::rosterSlot(w, id));
        w.set16(0x7aaab8 + gi * 8 + k * 2, id);
    }
    if (members == 0) { st.kind = 1; return st; }

    int sum = 0, pairs = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (ids[std::size_t(i)] != ids[std::size_t(j)]) { sum += logic::affinity(w, ids[std::size_t(i)], ids[std::size_t(j)]); ++pairs; }
    st.affinity = static_cast<std::int16_t>(sum / pairs);
    if (w.u8(group(g) + 3) == 1) {                   // lecture
        if (w.s16(group(g) + 8) < 0) st.kind = 5;
        else {
            st.kind = 4;
            st.a = w.s16(group(g) + 6); st.b = w.s16(group(g) + 8);
            st.c = w.s16(group(g) + 0xa); st.d = w.s16(group(g) + 0xc);
        }
    } else if (w.s16(kEventSet) == 0) {
        st.kind = 3;
    } else {
        st.kind = 2;
        st.a = w.s16(0x7aab10); st.b = w.s16(0x7aab12);
        st.c = w.s16(kEventId); st.d = w.s16(0x7aab14);
    }
    constexpr std::array<int, 6> kGrades{0x10, 0x1f, 0x2e, 0x3d, 0x4c, 0x5b};
    st.grade = 7;
    for (std::size_t i = 0; i < kGrades.size(); ++i)
        if (st.affinity < kGrades[i]) { st.grade = static_cast<std::int16_t>(i + 1); break; }
    return st;
}

// FUN_004a7d30: some group is busy, every group with members has its choice
// made, and if any goes on the adventure, every group it requires (bits
// 11-15 of its type-2 conditions) is going.
bool HanTask::canCommit() {
    GameWork& w = ctx_.work;
    std::array<int, 5> kind{};
    bool adventure = false;
    if (w.s16(kPlanFixed) == 0) {
        bool allIdle = true;
        for (int g = 0; g < 5; ++g) {
            kind[std::size_t(g)] = groupStatus(w, g).kind;
            switch (kind[std::size_t(g)]) {
                case 0: break;
                case 1: case 3: case 5: return false;
                case 2: adventure = true; allIdle = false; break;
                case 4: allIdle = false; break;
                default: return false;
            }
        }
        if (allIdle) return false;
        if (!adventure) return true;          // lectures only (0x4a7e8a; the decompile merges this with the line above)
    } else if (w.s16(kPlanFixed) == 1) {
        for (int g = 0; g < 5; ++g) {
            kind[std::size_t(g)] = groupStatus(w, g).kind;
            if (kind[std::size_t(g)] == 2) adventure = true;
            else if (kind[std::size_t(g)] != 0) return false;
        }
    } else {
        return false;
    }
    if (adventure) {
        const auto ev = std::uint32_t(w.u16(kEventId));
        const int conditions = logic::sdU8(kEvents + 0xf + ev * 0x100, ctx_.version);
        for (int c = 0; c < conditions; ++c) {
            const auto cond = static_cast<std::uint16_t>(logic::sdS16(kEvents + 0x12 + std::uint32_t(c) * 4 + ev * 0x100, ctx_.version));
            if ((cond & 0x7ff) != 2) continue;
            for (int g = 0; g < 5; ++g)
                if ((cond >> (11 + g) & 1) && kind[std::size_t(g)] != 2) return false;
        }
    }
    return true;
}

// ------------------------------------------------------------- one visit ---
// FUN_004a7c40: returns the command (1 load, 2 save, 3 教務室, 7 title, 8 commit).
Co<int> HanTask::visit() {
    command_ = 0;
    running_ = true;
    fade_ = 0;
    co_await yieldFrame(8);
    state_ = 2;
    commitPending_ = false;
    do {
        cursor_ = 0;
        ctx_.work.set8(kCanCommit, canCommit());
        co_await screenState();
        draw();
        drawPopup();                                 // FUN_004a3240
        co_await yieldFrame();
    } while (running_);
    if (commitPending_) commitAdventure();          // FUN_004a1920
    co_return command_;
}

// ---------------------------------------------------------- screen state ---
Co<> HanTask::fadeTo(int nextState, int command) {
    fill(ctx_, 0, 0, 0x400, 0x300, 0, std::uint32_t(fade_ * 0xff / 0x1e) << 24 | 0x10101u);
    if (fade_++ == 0x1e) {
        fade_ = 0;
        if (nextState >= 0) state_ = static_cast<std::int16_t>(nextState);
        if (command > 0) { running_ = false; command_ = static_cast<std::int16_t>(command); }
    }
    co_return;
}

// FUN_004a6f90
Co<> HanTask::screenState() {
    GameWork& w = ctx_.work;
    switch (state_) {
        case 0:
            switch (menu()) {
                case 1: state_ = 0xb; break;
                case 2: state_ = 0xc; break;
                case 3: state_ = 7; break;
                case 4: state_ = 8; break;
                case 6: if (hasTutorial()) state_ = 0xa; break;
                case 7: state_ = 0xd; dialogBuilt_ = false; break;
                default: break;            // 5: no action
            }
            clickButtons();
            break;
        case 2:                            // fade in
            fill(ctx_, 0, 0, 0x400, 0x300, 0, std::uint32_t(0xff - fade_ * 0xff / 0x1e) << 24 | 0x10101u);
            if (fade_++ == 0x1e) { fade_ = 0; state_ = 0; }
            break;
        case 3: co_await fadeTo(-1, 8); break;
        case 7:
            co_await fadeTo(-1, 3);
            w.setAreaBgmOn(true);          // DAT_007e11a0: keep the area BGM through WorkRoom
            break;
        case 8: {                          // fade out, then hand over to KojinTask
            const bool done = fade_ == 0x1e;
            co_await fadeTo(9, 0);
            if (done) w.set16(kKojinState, 1);
            flashes_.clear();
            break;
        }
        case 9:
            for (int i = 0; i < 0x1e; ++i) {
                fill(ctx_, 0, 0, 0x400, 0x300, 0, std::uint32_t(0xff - i * 0xff / 0x1e) << 24 | 0x10101u);
                co_await yieldFrame();
            }
            // KojinTask shows itself now and clears DAT_007a4e60 when closed.
            while (w.s16(kKojinState) != 0) co_await yieldFrame();
            fill(ctx_, 0, 0, 0x400, 0x300, 0, 0xff010101u);
            state_ = 2;
            statRanks();                   // FUN_004a9ab0
            break;
        case 0xb: {
            const bool done = fade_ == 0x1e;
            co_await fadeTo(-1, 1);
            if (done) w.set16(kSaveMode, 1);
            break;
        }
        case 0xc: {
            const bool done = fade_ == 0x1e;
            co_await fadeTo(-1, 2);
            if (done) w.set16(kSaveMode, 2);
            break;
        }
        case 6: drag(); break;
        case 0xd: co_await confirm(false); break;
        case 0xe: co_await confirm(true); break;
        case 0xf: co_await fadeTo(-1, 7); break;
        case 0xa: {
            const bool done = fade_ == 0x1e;
            co_await fadeTo(-1, 0);
            if (done) co_await tutorialMenu();
            break;
        }
        default: break;
    }
}

// State 0xa (JP 0x45755c; TW keeps the state, 0x4a7333, but no menu entry for it): the
// tutorial menu, mm001.ybc, run over a black screen, then the fade in and
// the area BGM again. The port gives TW the entry too when its Tutorial
// option is on, as the title does.
bool HanTask::hasTutorial() const {
    return ctx_.version == GameVersion::Jp || options::port().twTutorial;
}

Co<> HanTask::tutorialMenu() {
    bool done = false;
    ctx_.adv = AdvRequest{"Data\\ADV\\DAT\\mm001.ybc", -1};
    ctx_.tasks.link(std::make_unique<AdvTask>(ctx_, &done), 0, 0);
    while (!done) co_await yieldFrame();
    fill(ctx_, 0, 0, 0x400, 0x300, 1, 0xff010101u);
    fade_ = 0;
    state_ = 2;
    ctx_.work.setAreaBgmOn(false);                                      // DAT_007e11a0
    ctx_.audio->bgmPlay(static_cast<std::uint32_t>(ctx_.work.areaBgm()), 1);   // FUN_004db010
}

// States 0xd (back to the title) and 0xe (commit the week).
Co<> HanTask::confirm(bool commit) {
    const DialogText& t = ctx_.version == GameVersion::Tw ? kDialogTw : kDialogJp;
    if (!dialogBuilt_) {
        if (commit) {
            // A random member from units 1..11 (or 0x23) says their line.
            // The original pushes the loop counter (12) for unit 0x23.
            std::array<int, 12> ids{};
            int n = 0;
            for (int u = 1; u < 0xc; ++u)
                if (logic::inRoster(ctx_.work, u)) ids[std::size_t(n++)] = u;
            if (logic::inRoster(ctx_.work, 0x23)) ids[std::size_t(n++)] = 0xc;
            if (n > 0) voice(ids[std::size_t(logic::rand() % n)], 10, 0);
        }
        dialog_.reset(5);
        dialog_.addUtf8(exe::text(ctx_.version, commit ? t.commit : t.title));
        dialog_.addUtf8(exe::text(ctx_.version, t.sure));
        dialogBuilt_ = true;
    }
    constexpr int x = 0x179, y = 0x152;
    dialog_.draw(ctx_, x + 10, y + 10, 0x66);
    if ((ysub::hit(ctx_, x + 0x1c, y + 0x34, 0x66, 0x1d).pressed & legacy::kMouseLeft)) {
        const ysub::Rect r = rect(kDialogYes);
        flashes_.add(r, 0x67, x + r.x, y + r.y, 2);
        if (commit) { state_ = 3; commitPending_ = true; ctx_.audio->sePlay(kSe); }
        else        { state_ = 0xf; ctx_.audio->sePlay(kSeYes); }
    }
    if ((ysub::hit(ctx_, x + 0x8c, y + 0x34, 0x66, 0x1d).pressed & legacy::kMouseLeft)) {
        const ysub::Rect r = rect(kDialogNo);
        flashes_.add(r, 0x67, x + r.x, y + r.y, 2);
        ctx_.audio->sePlay(kSeNo);
        state_ = 0;
    }
    drawRect(ctx_, rect(kDialog), 0x68, x, y);
    fill(ctx_, 0x16a, 0x143, 300, 0x78, 0x69, 0xd8080808u);
    co_return;
}

// FUN_004aa320: six menu entries (hover glow) and the title button.
int HanTask::menu() {
    int picked = -1;
    for (int i = 0; i < 6; ++i) {
        const ysub::Hit h = ysub::hitEntry(ctx_, kMenuHit + i);
        if (!h.over) continue;
        if (h.held & legacy::kMouseLeft) picked = i;
        // Deviation: the original plays the click on every held frame, which
        // repeats on an entry that does nothing (5, and TW's Tutorial); once per press here.
        if (h.pressed & legacy::kMouseLeft) ctx_.audio->sePlay(kSe);
        const ysub::Rect r = rect(kMenuHover + i);
        drawRect(ctx_, r, 0x7c, r.x, r.y);
    }
    ++picked;
    const ysub::Hit h = ysub::hit(ctx_, 0x39e, 0xc, 0x5a, 0x14);
    if (h.over) {
        flashes_.add(rect(kTitleHover), 0x70, 0x39e, 10, 1);
        if (h.held & legacy::kMouseLeft) { ctx_.audio->sePlay(kSe); picked = 7; }
    }
    return picked;
}

// --------------------------------------------------------------- buttons ---
// FUN_004aa4b0: first table entry under a click, or -1.
int HanTask::buttonUnderPointer() {
    for (int b = 0;; ++b) {
        const Button bt = button(b);
        if (bt.hit < 0) return -1;
        if (ysub::hitEntry(ctx_, bt.hit, bt.hitX, bt.hitY).pressed & legacy::kMouseLeft) return b;
    }
}

// FUN_004aa590: 8-frame press flash, the rect's source shifted by the button's offsets.
void HanTask::flashButton(int b) {
    const Button bt = button(b);
    ysub::Rect r = rect(bt.rect);
    r.sx = static_cast<std::int16_t>(r.sx + bt.flashX);
    r.sy = static_cast<std::int16_t>(r.sy + bt.flashY);
    flashes_.add(r, 0x7e, bt.x, bt.y, 8);
}

// FUN_004ab0d0: a button in its selected look.
void HanTask::drawButton(int b, int offX, int offY, int prio) {
    const Button bt = button(b);
    if (bt.enabled == 0) return;
    drawRect(ctx_, rect(bt.rect), prio, bt.x, bt.y, offX, offY);
}

// FUN_004a3ca0
void HanTask::clickButtons() {
    GameWork& w = ctx_.work;
    const int sel = w.s8(kSelGroup);
    const int b = buttonUnderPointer();
    const bool fixed = w.s16(kPlanFixed) != 0;
    const auto selLecture = [&] { return sel != -1 && w.u8(group(sel) + 3) != 0; };
    const auto selAdventure = [&] { return sel != -1 && w.u8(group(sel) + 3) == 0; };
    // Paged counters: 0 first, 1 previous, 2 next, 3 last.
    const auto page16 = [&](std::uint32_t va, std::uint32_t maxVa, int cmd) {
        const int v = w.s16(va), max = w.s16(maxVa);
        switch (cmd) {
            case 0: w.set16(va, 0); break;
            case 1: if (v != 0) w.set16(va, v - 1); break;
            case 2: if (v != max) w.set16(va, v + 1); break;
            case 3: w.set16(va, max); break;
            default: break;
        }
    };
    constexpr std::array<int, 4> kScrollCmd{2, 0, 1, 3};   // top, up, down, bottom
    if (b < 0) return;

    if (b == 0) {                                          // 予定進行
        ctx_.audio->sePlay(kSe);
        if (w.u8(kCanCommit) == 1) { flashButton(b); dialogBuilt_ = false; state_ = 0xe; }
    } else if (b <= 5) {                                   // group: adventure
        if (w.s16(group(b - 1)) != -1) { flashButton(b); ctx_.audio->sePlay(kSe); w.set8(group(b - 1) + 3, 0); }
    } else if (b <= 10) {                                  // group: lecture
        const int g = b - 6;
        if (w.s16(group(g)) != -1) {
            w.set8(group(g) + 3, 1);
            flashButton(b); ctx_.audio->sePlay(kSe);
            if (fixed) w.set8(group(g) + 3, 0);
        }
    } else if (b <= 0xd) {                                 // member sort: level / job / total stats
        flashButton(b); ctx_.audio->sePlay(kSe);
        sortWaiting(w, b - 0xb, 0);
        w.set8(kWaitSort, b - 0xb);
    } else if (b <= 0x11) {                                // member list pages
        flashButton(b); ctx_.audio->sePlay(kSe);
        const int v = w.u8(0x7d57db), max = w.u8(0x7d57dc);
        switch (b - 0xe) {
            case 0: w.set8(0x7d57db, 0); break;
            case 1: if (v != 0) w.set8(0x7d57db, v - 1); break;
            case 2: if (v != max) w.set8(0x7d57db, v + 1); break;
            case 3: w.set8(0x7d57db, max); break;
            default: break;
        }
    } else if (b <= 0x14) {
        // 0x12..0x14: guest sort, disabled in this build
    } else if (b <= 0x18) {                                // guest list pages
        page16(0x7d58ac, 0x7d58ae, b - 0x15);
        ctx_.audio->sePlay(kSe); flashButton(b);
    } else if (b <= 0x1d) {                                // group mode toggle
        const int g = b - 0x19;
        if (w.s16(group(g)) == -1) {
            w.set8(group(g) + 3, 1);
        } else {
            ctx_.audio->sePlay(kSe); flashButton(b);
            if (!fixed) w.set8(group(g) + 3, w.u8(group(g) + 3) == 0 ? 1 : 0);
            else        w.set8(group(g) + 3, 0);
        }
    } else if (b <= 0x1f) {                                // adventure panel tabs
        if (sel != -1 && w.u8(group(sel) + 3) != 1 && !fixed) {
            ctx_.audio->sePlay(kSe); flashButton(b);
            w.set8(0x7d5984, b - 0x1e);
        }
    } else if (b <= 0x23) {                                // adventure info tabs (概要 / 目的 / 条件 / 戦力)
        if (selAdventure()) {
            ctx_.audio->sePlay(kSe); flashButton(b);
            w.set16(0x7d5982, b - 0x20);
        }
    } else if (b <= 0x27) {                                // adventure info pages
        if (selAdventure()) {
            const std::uint32_t k = std::uint32_t(w.s16(0x7aab08)) * 2;
            page16(0x7d6300 + k, 0x7d6306 + k, b - 0x24);
            ctx_.audio->sePlay(kSe); flashButton(b);
        }
    } else if (b <= 0x2b) {                                // adventure text scroll
        if (selAdventure()) {
            tacticsText_.scroll(kScrollCmd[std::size_t(b - 0x28)]);
            ctx_.audio->sePlay(kSe); flashButton(b);
        }
    } else if (b <= 0x2e) {                                // lecture panel tabs
        if (selLecture() && !fixed) {
            ctx_.audio->sePlay(kSe); flashButton(b);
            w.set8(0x7d598a, b - 0x2c);
        }
    } else if (b <= 0x32) {                                // lecture list pages
        if (selLecture() && !fixed) {
            ctx_.audio->sePlay(kSe); flashButton(b);
            const std::uint32_t k = std::uint32_t(w.s8(0x7d598a)) * 2;
            page16(0x7d6a1a + k, 0x7d6a20 + k, b - 0x2f);
        }
    } else if (b <= 0x36) {                                // lecture text scroll
        if (selLecture() && !fixed) {
            ctx_.audio->sePlay(kSe); flashButton(b);
            lectureText_.scroll(kScrollCmd[std::size_t(b - 0x33)]);
        }
    }
}

void HanTask::voice(int unit, int category, int extra) {
    const int line = logic::unitVoiceLine(unit, category, extra);
    if (line >= 0) ctx_.audio->voicePlay(logic::unitVoicePath(line, ctx_.version));
}

// --------------------------------------------------------- waiting lists ---
// FUN_004ab250 / FUN_004ab3e0: append if absent. FUN_004ab2e0 / FUN_004ab470: remove.
static void listAdd(GameWork& w, std::uint32_t countVa, std::uint32_t listVa, int id) {
    const int n = w.s16(countVa);
    for (int i = 0; i < n; ++i) if (w.s16(listVa + std::uint32_t(i) * 2) == id) return;
    w.set16(listVa + std::uint32_t(n) * 2, id);
    w.set16(countVa, n + 1);
}
static bool listRemove(GameWork& w, std::uint32_t countVa, std::uint32_t listVa, int id) {
    const int n = w.s16(countVa);
    for (int i = 0; i < n; ++i) {
        if (w.s16(listVa + std::uint32_t(i) * 2) != id) continue;
        for (int k = i; k < n; ++k) w.set16(listVa + std::uint32_t(k) * 2, w.s16(listVa + std::uint32_t(k + 1) * 2));
        w.set16(countVa, n - 1);
        return true;
    }
    return false;
}
// Remove entry `index` (the drag source) and close the gap.
static void listErase(GameWork& w, std::uint32_t countVa, std::uint32_t listVa, int index) {
    const int n = w.s16(countVa) - 1;
    w.set16(countVa, n);
    for (int k = index; k < n; ++k) w.set16(listVa + std::uint32_t(k) * 2, w.s16(listVa + std::uint32_t(k + 1) * 2));
}

// FUN_004a8bf0(mode, list): sort the member (list 0) or guest (list 1)
// waiting list by rank (0), job class (1) or summed stat levels (2).
// Members read their live record, guests the 0x6f5088 template table.
void HanTask::sortWaiting(GameWork& w, int mode, int list) {
    const std::uint32_t countVa = list == 0 ? kWaitCount : kGuestCount;
    const std::uint32_t listVa = list == 0 ? kWaitList : kGuestList;
    const int n = std::min<int>(w.s16(countVa), 0x46);
    if (list != 0 && list != 1) return;
    std::array<std::int16_t, 0x46> id{}, rank{}, cls{}, stats{}, out{};
    std::array<bool, 0x46> left{};
    for (int i = 0; i < n; ++i) {
        const std::size_t k = std::size_t(i);
        id[k] = w.s16(listVa + std::uint32_t(i) * 2);
        const std::uint32_t rec = list == 0 ? GameWork::unitRecord(id[k]) : 0x6f5088 + std::uint32_t(id[k]) * 0x4a0;
        const auto byte = [&](std::uint32_t o) { return list == 0 ? w.u8(rec + o) : logic::sdU8(rec + o, ctx_.version); };
        const int job = list == 0 ? w.s16(rec + 6) : logic::sdS16(rec + 6, ctx_.version);
        rank[k] = byte(0x50);
        cls[k] = logic::sdU8(0x6b2d8a + std::uint32_t(job) * 0x40, ctx_.version);
        int sum = 0;
        for (std::uint32_t s7 = 0; s7 < 7; ++s7) sum += byte(0xc + s7 * 8);
        stats[k] = static_cast<std::int16_t>(sum);
        left[k] = true;
    }
    const auto selectionSort = [&](std::array<std::int16_t, 0x46>& key) {
        for (int i = 0; i < n - 1; ++i)
            for (int j = i + 1; j < n; ++j)
                if (key[std::size_t(i)] < key[std::size_t(j)]) {
                    std::swap(key[std::size_t(i)], key[std::size_t(j)]);
                    std::swap(id[std::size_t(i)], id[std::size_t(j)]);
                }
        out = id;
    };
    switch (mode) {
        case 0: selectionSort(rank); break;
        case 1: {
            int o = 0;
            for (int c = 0; c < 6; ++c)
                for (int i = 0; i < n; ++i)
                    if (cls[std::size_t(i)] == c && left[std::size_t(i)]) { out[std::size_t(o++)] = id[std::size_t(i)]; left[std::size_t(i)] = false; }
            break;
        }
        case 2: selectionSort(stats); break;
        default: return;
    }
    for (int i = 0; i < n; ++i) w.set16(listVa + std::uint32_t(i) * 2, out[std::size_t(i)]);
}

// FUN_004a5f40: rebuild both waiting lists from the roster, drop groups
// whose leader or members have left, and select the first led group.
void HanTask::buildWaitingLists() {
    GameWork& w = ctx_.work;
    const int resetChoices = w.s16(0x7a55f8);        // set by New Game: start every group on lectures
    w.set16(0x7a55f8, 0);
    for (int g = 0; g < 5 && resetChoices != 0; ++g) {
        w.set8(group(g) + 3, 1);
        w.set8(group(g) + 4, 0xff);
        w.set16(group(g) + 6, -1);
        w.set16(group(g) + 8, -1);
    }
    w.set8(kWaitPage, 0);
    w.set8(kWaitSort, 0);
    w.set16(kWaitCount, 0);
    w.set16(0x7d57e0, w.s16(0x7a5260));
    for (int i = 0; i < w.s16(0x7a5260); ++i) listAdd(w, kWaitCount, kWaitList, w.s16(0x7a5210 + std::uint32_t(i) * 2));
    w.set16(kGuestPage, 0);
    w.set16(kGuestSort, 0);
    w.set16(kGuestCount, 0);
    w.set16(0x7d58b2, w.s16(0x7a528a));
    for (int i = 0; i < w.s16(0x7a528a); ++i) listAdd(w, kGuestCount, kGuestList, w.s16(0x7a5262 + std::uint32_t(i) * 2));

    bool first = true;
    for (int g = 0; g < 5; ++g) {
        const std::uint32_t gi = std::uint32_t(g);
        if (resetChoices != 0) w.set8(group(g) + 3, 1);
        const int leader = w.s16(group(g));
        if (!logic::inRoster(w, leader)) {
            w.set16(0x7aaaae + gi * 2, -1);
            w.set16(0x7aaaa4 + gi * 2, -1);
            w.set8(group(g) + 3, 1);
            w.set16(group(g), -1);
        } else {
            listRemove(w, kGuestCount, kGuestList, leader);
            if (first) { selectGroup(g); first = false; }
            if (resetChoices != 0) w.set8(group(g) + 3, 0);   // led groups start on the adventure
        }
    }
    for (int g = 0; g < 5; ++g) {
        const std::uint32_t gi = std::uint32_t(g);
        w.set8(group(g) + 0xe, 0);                    // member count
        for (std::uint32_t k = 0; k < 4; ++k) {
            const std::uint32_t slot = group(g) + 0x10 + k * 2;
            const int id = w.s16(slot);
            const bool keep = logic::inRoster(w, id) && w.s16(group(g)) != -1 &&
                              listRemove(w, kWaitCount, kWaitList, id);
            if (keep) {
                w.set16(slot, static_cast<std::int8_t>(id));
                w.set8(group(g) + 0xe, w.u8(group(g) + 0xe) + 1);
            } else {
                w.set16(0x7aaae0 + gi * 8 + k * 2, -1);
                w.set16(0x7aaab8 + gi * 8 + k * 2, -1);
                w.set16(slot, -1);
            }
        }
    }
}

// The right-click popup target (DAT_007d57c6..d6); FUN_004a3240 draws it.
void HanTask::openPopup(int unit, int grp, int boxX, int boxY, int hitX, int hitY) {
    GameWork& w = ctx_.work;
    if (w.s16(kPopupOpen) == 0) w.set16(0x7d57ca, grp);
    w.set16(kPopupOpen, w.s16(kPopupOpen) == 0);
    w.set16(0x7d57c8, unit);
    w.set16(0x7d57cc, boxX); w.set16(0x7d57ce, boxY);
    w.set16(0x7d57d0, hitX); w.set16(0x7d57d2, hitY);
    w.set16(0x7d57d4, 0x44); w.set16(0x7d57d6, 0x40);
}

// FUN_004aadb0: face cell `index` (members: 14 per column, guests: 7).
void HanTask::drawPortrait(int x, int y, int kind, int index, std::uint8_t alpha, int prio) {
    const ysub::Rect r = rect(kind == 0 ? kMemberFace : kGuestFace);
    const int rows = kind == 0 ? 0xe : 7;
    const int i = index & 0xff;
    drawRect(ctx_, r, prio, x, y, r.w * (i / rows), r.h * (i % rows), alpha);
}

// FUN_004aaf60: name plate row `index`.
void HanTask::drawName(int x, int y, int kind, int index, std::uint8_t alpha, int prio) {
    const ysub::Rect r = rect(kind == 0 ? kMemberName : kGuestName);
    drawRect(ctx_, r, prio, x, y, 0, r.h * (index & 0xff), alpha);
}

// FUN_004aacf0: job class badge (DAT_006b2d8a: class of each job, 1-based).
void HanTask::drawJobIcon(int x, int y, int unit, std::uint8_t alpha, int prio) {
    const int job = ctx_.work.s16(GameWork::unitRecord(unit) + 6);
    const int cls = logic::sdU8(0x6b2d8a + std::uint32_t(job) * 0x40, ctx_.version) - 1;
    drawRect(ctx_, rect(kJobIcons), prio, x, y, (cls & 0xff) * 0x14, 0, alpha);
}

// FUN_004a6460: the two waiting rows under the groups. Left-press picks a
// unit up (state 6), right-press opens its info popup.
void HanTask::drawWaiting() {
    GameWork& w = ctx_.work;
    int next = state_;
    const auto pageMax = [&](std::uint32_t countVa) { return std::max(0, w.s16(countVa) - 5); };

    w.set8(kWaitPage + 1, pageMax(kWaitCount));
    if (w.s8(kWaitPage + 1) < w.s8(kWaitPage)) w.set8(kWaitPage, w.s8(kWaitPage + 1));
    for (int i = 0; i < 5; ++i) {
        const int index = w.s8(kWaitPage) + i;
        if (index >= w.s16(kWaitCount)) break;
        const int id = w.s16(kWaitList + std::uint32_t(index) * 2);
        const int x = i * 0x4e;
        const ysub::Hit h = ysub::hit(ctx_, x + 0x79, 0x241, 0x44, 0x40);
        std::uint8_t alpha = 0xff;
        if (state_ == 0 && h.over) {
            cursor_ = 1;
            alpha = 0x80;
            if (h.pressed & legacy::kMouseRight) {
                ctx_.audio->sePlay(kSe);
                openPopup(id, -1, x + 0xc0, 0x23b, x + 0x79, 0x241);
            }
            if (h.pressed & legacy::kMouseLeft) {
                w.set8(kDragFromGrp, 0); w.set8(kDragLeader, 0);
                w.set16(kDragGroup, -1); w.set16(kDragIndex, index);
                w.set16(kPicked, id); w.set16(kDragId, id);
                voice(id, 9, 5);
                next = 6;
            }
        }
        if (state_ == 6 && w.u8(kDragFromGrp) == 0 && index == w.s16(kDragIndex) && w.u8(kDragLeader) == 0) alpha = 0x80;
        if (w.s16(kPicked) == id) drawRect(ctx_, rect(kPickedFrame), 0x71, x + 0x71, 0x23a);
        drawPortrait(x + 0x75, 0x23d, 0, id - 1, alpha, 0x77);
        drawName(x + 0x79, 0x288, 0, id - 1, alpha, 0x77);
        drawJobIcon(x + 0x75, 0x23d, id, alpha, 0x76);
    }

    w.set16(kGuestPage + 2, pageMax(kGuestCount));
    for (int i = 0; i < 5; ++i) {
        const int index = w.s16(kGuestPage) + i;
        if (index >= w.s16(kGuestCount)) break;
        const int id = w.s16(kGuestList + std::uint32_t(index) * 2);
        const int x = i * 0x4e;
        const ysub::Hit h = ysub::hit(ctx_, x + 0x79, 0x2a2, 0x44, 0x40);
        std::uint8_t alpha = 0xff;
        if (state_ == 0 && h.over) {
            cursor_ = 1;
            alpha = 0x80;
            if (h.pressed & legacy::kMouseLeft) {
                ctx_.audio->sePlay(kSe);
                w.set8(kDragFromGrp, 0); w.set8(kDragLeader, 1);
                w.set16(kDragGroup, -1); w.set16(kDragIndex, index);
                w.set16(kDragId, id);
                next = 6;
            }
        }
        if (state_ == 6 && w.u8(kDragFromGrp) == 0 && index == w.s16(kDragIndex) && w.u8(kDragLeader) == 1) alpha = 0x80;
        drawPortrait(x + 0x75, 0x29e, 1, id - 0x65, alpha, 0x77);
        drawName(x + 0x79, 0x2e9, 1, id - 0x65, alpha, 0x77);
    }
    state_ = static_cast<std::int16_t>(next);
}

// FUN_004a5440: each group's members (4 slots) and its leader.
void HanTask::drawGroups() {
    GameWork& w = ctx_.work;
    for (int g = 0; g < 5; ++g) {
        if (w.s16(group(g)) == -1) continue;
        const int y = g * 0x61;
        if (w.s16(group(g) + 6) != -1 && w.s16(group(g) + 8) != -1)
            drawLectureItem(0x60, y + 0x96, w.s16(group(g) + 6), w.s16(group(g) + 8), 0xff, 0x73);
        for (int k = 0; k < 4; ++k) {
            const int id = w.s16(group(g) + 0x10 + std::uint32_t(k) * 2);
            if (id == -1) continue;
            const int x = k * 0x4e;
            std::uint8_t alpha = 0xff;
            const ysub::Hit h = ysub::hit(ctx_, x + 0xe0, y + 0x53, 0x44, 0x40);
            if (state_ == 0 && h.over) {
                cursor_ = 1;
                alpha = 0x80;
                if (h.pressed & legacy::kMouseRight) {
                    ctx_.audio->sePlay(kSe);
                    openPopup(id, g, x + 0x131, y + 0x4c, x + 0xe0, y + 0x53);
                    w.set8(0x7d57c4, 0);
                }
                if (h.pressed & legacy::kMouseLeft) {
                    w.set8(kDragFromGrp, 1); w.set8(kDragLeader, 0);
                    w.set16(kDragGroup, g); w.set16(kDragIndex, k);
                    w.set16(kPicked, id); w.set16(kDragId, id);
                    w.set16(kDragGrade, groupStatus(w, g).grade);
                    voice(id, 9, 5);
                    state_ = 6;
                }
            }
            if (state_ == 6 && w.u8(kDragLeader) == 0 && w.s16(kDragGroup) == g && w.s16(kDragIndex) == k) alpha = 0x80;
            if (w.s16(kPicked) == id) drawRect(ctx_, rect(kPickedFrame), 0x71, x + 0xd8, y + 0x4c);
            drawPortrait(x + 0xdc, y + 0x4f, 0, id - 1, alpha, 0x77);
            drawName(x + 0xe0, y + 0x9a, 0, id - 1, alpha, 0x77);
            drawJobIcon(x + 0xdc, y + 0x4f, id, alpha, 0x76);
        }
    }
    for (int g = 0; g < 5; ++g) {
        const int leader = w.s16(group(g));
        if (leader == -1) continue;
        const int y = g * 0x61;
        std::uint8_t alpha = 0xff;
        const ysub::Hit h = ysub::hit(ctx_, 0x10, y + 0x53, 0x44, 0x40);
        if (state_ == 0 && h.over) {
            cursor_ = 1;
            alpha = 0x80;
            if (h.pressed & legacy::kMouseLeft) {
                ctx_.audio->sePlay(kSe);
                w.set8(kDragFromGrp, 1); w.set8(kDragLeader, 1);
                w.set16(kDragGroup, g); w.set16(kDragIndex, -1);
                w.set16(kDragId, leader);
                state_ = 6;
            }
        }
        if (state_ == 6 && w.u8(kDragLeader) == 1 && w.s16(kDragGroup) == g) alpha = 0x80;
        drawPortrait(0xc, y + 0x4f, 1, leader - 0x65, alpha, 0x77);
        drawName(0x10, y + 0x9a, 1, leader - 0x65, alpha, 0x77);
    }
}

// FUN_004a81a0's first loop: each staffed group's affinity animation,
// AnmHan set 0 clip 8 - grade, restarted when the grade changes.
void HanTask::drawAffinity() {
    static const AnmPlayer::SpriteTemplate kSprite{.color = {0x80, 0x80, 0x80, 0xFF}};   // +0x240, FUN_004077c0 defaults
    for (int g = 0; g < 5; ++g) {
        const GroupStatus st = groupStatus(ctx_.work, g);
        if (st.kind < 2 || st.kind > 5) continue;
        const std::size_t gi = std::size_t(g);
        const auto clip = static_cast<std::int16_t>(8 - st.grade);
        if (clip != affinityShown_[gi]) anm_.play(affinityAnm_[gi], 0, clip, 0);
        affinityShown_[gi] = clip;
        anm_.update(affinityAnm_[gi]);
        anm_.draw(affinityAnm_[gi], ctx_.draw, *ctx_.sprites, kSprite, 0xac, g * 0x61 + 0x8e, 0x6e);
    }
}

// FUN_004a4680, state 6: the picked-up unit follows the pointer; releasing
// over a slot places it (swapping with whoever is there), releasing
// elsewhere sends a unit taken from a group back to its waiting list.
void HanTask::drag() {
    GameWork& w = ctx_.work;
    cursor_ = 2;
    const bool released = !(ysub::hitEntry(ctx_, 0).held & legacy::kMouseLeft);
    if (released) ctx_.audio->sePlay(kSe);
    bool dropped = false;
    const int id = w.s16(kDragId);
    const bool fromGroup = w.u8(kDragFromGrp) == 1;
    const int srcGroup = w.s16(kDragGroup), srcIndex = w.s16(kDragIndex);
    const auto member = [&](int g, int k) { return group(g) + 0x10 + std::uint32_t(k) * 2; };
    // Members of group `g` go back to the member waiting list.
    const auto disband = [&](int g) {
        for (int k = 0; k < 4; ++k) {
            const int m = w.s16(member(g, k));
            if (m == -1) continue;
            w.set16(kWaitList + std::uint32_t(w.s16(kWaitCount)) * 2, m);
            w.set16(kWaitCount, w.s16(kWaitCount) + 1);
            w.set16(member(g, k), -1);
        }
    };

    if (w.u8(kDragLeader) == 0) {                      // a member: onto a member slot
        for (int g = 0; g < 5; ++g) {
            if (w.s16(group(g)) == -1) continue;
            for (int k = 0; k < 4; ++k) {
                if (!ysub::hit(ctx_, k * 0x4e + 0xe0, g * 0x61 + 0x53, 0x44, 0x40).over) continue;
                drawRect(ctx_, rect(kPickedFrame), 0x71, k * 0x4e + 0xd8, g * 0x61 + 0x4c);
                selectGroup(g);
                if (!released) continue;
                ctx_.audio->sePlay(kSe);
                dropped = true;
                const int there = w.s16(member(g, k));
                if (!fromGroup) {
                    w.set16(member(g, k), id);
                    listErase(w, kWaitCount, kWaitList, srcIndex);
                    if (there == -1) w.set8(group(g) + 0xe, w.u8(group(g) + 0xe) + 1);
                    else {                                     // the replaced member goes to the end of the list
                        w.set16(kWaitList + std::uint32_t(w.s16(kWaitCount)) * 2, there);
                        w.set16(kWaitCount, w.s16(kWaitCount) + 1);
                    }
                } else {
                    w.set16(member(g, k), id);
                    w.set16(member(srcGroup, srcIndex), there);        // -1 or the swapped member
                }
                voice(id, 1, groupStatus(w, g).grade);
                sortWaiting(w, w.u8(kWaitSort), 0);
            }
        }
    }
    if (w.u8(kDragLeader) == 1) {                      // a guest: onto a leader slot
        for (int g = 0; g < 5; ++g) {
            if (!ysub::hit(ctx_, 0x10, g * 0x61 + 0x53, 0x44, 0x40).over) continue;
            drawRect(ctx_, rect(kPickedFrame), 0x71, 8, g * 0x61 + 0x4c);
            if (!released) continue;
            ctx_.audio->sePlay(kSe);
            dropped = true;
            const int there = w.s16(group(g));
            if (there == -1) {
                if (!fromGroup) {
                    w.set16(group(g), id);
                    listErase(w, kGuestCount, kGuestList, srcIndex);
                    if (w.s16(kPlanFixed) != 0) w.set8(group(g) + 3, 0);
                } else {                                       // move a whole group's lead to an empty slot
                    w.set16(group(g), id);
                    w.set16(group(srcGroup), -1);
                    disband(srcGroup);
                    w.set8(group(srcGroup) + 3, 1);
                    w.set16(group(srcGroup) + 6, -1);
                    w.set16(group(srcGroup) + 8, -1);
                    w.set8(group(g) + 3, w.s16(kPlanFixed) == 0 ? 1 : 0);
                }
            } else if (!fromGroup) {                           // replace the leader
                w.set16(group(g), id);
                listErase(w, kGuestCount, kGuestList, srcIndex);
                w.set16(kGuestList + std::uint32_t(w.s16(kGuestCount)) * 2, there);
                w.set16(kGuestCount, w.s16(kGuestCount) + 1);
            } else {                                           // swap two leaders
                w.set16(group(g), id);
                w.set16(group(srcGroup), there);
                w.set16(group(srcGroup) + 6, -1);
                w.set16(group(srcGroup) + 8, -1);
            }
            w.set16(group(g) + 6, -1);
            w.set16(group(g) + 8, -1);
            selectGroup(g);
            sortWaiting(w, w.s16(kGuestSort), 1);
        }
    }

    const auto& m = ctx_.mouse();
    const int leader = w.u8(kDragLeader);
    const int face = leader == 0 ? id - 1 : id - 0x65;
    drawPortrait(m.x - 0x26, m.y - 0x24, leader, face, 0xff, 0x6a);
    if (leader == 0) drawJobIcon(m.x - 0x26, m.y - 0x24, id, 0xff, 0x69);

    if (!released) return;
    if (!dropped && fromGroup) {                       // dropped nowhere: back to the waiting list
        if (leader == 0) {
            voice(id, 2, w.s16(kDragGrade));
            w.set16(kWaitList + std::uint32_t(w.s16(kWaitCount)) * 2, id);
            w.set16(kWaitCount, w.s16(kWaitCount) + 1);
            w.set16(member(srcGroup, srcIndex), -1);
        } else {
            ctx_.audio->sePlay(kSe);
            w.set16(kGuestList + std::uint32_t(w.s16(kGuestCount)) * 2, id);
            w.set16(kGuestCount, w.s16(kGuestCount) + 1);
            w.set16(group(srcGroup), -1);
            disband(srcGroup);
            w.set8(group(srcGroup) + 3, 1);
            w.set16(group(srcGroup) + 6, -1);
            w.set16(group(srcGroup) + 8, -1);
        }
        sortWaiting(w, w.u8(kWaitSort), 0);
        sortWaiting(w, w.s16(kGuestSort), 1);
    }
    w.set8(kDragFromGrp, 0xff); w.set8(kDragLeader, 0xff);
    w.set16(kDragGroup, -1); w.set16(kDragIndex, -1); w.set16(kDragId, -1);
    state_ = 0;
}

// ------------------------------------------------- adventures / lectures ---
// FUN_004a9ab0: each member's place (1 = best) among the roster in each
// stat level and in their total, for the info popup.
void HanTask::statRanks() {
    GameWork& w = ctx_.work;
    const int n = std::min<int>(w.s16(0x7a5260), int(statRank_.size()));
    std::array<int, 0x28> total{};
    const auto level = [&](int i, int stat) {
        const int id = w.s16(0x7a5210 + std::uint32_t(i) * 2);
        return stat < 7 ? int(w.u8(GameWork::unitRecord(id) + 0xc + std::uint32_t(stat) * 8)) : total[std::size_t(i)];
    };
    for (int i = 0; i < n; ++i)
        for (int stat = 0; stat < 7; ++stat) total[std::size_t(i)] += level(i, stat);
    for (int i = 0; i < n; ++i)
        for (int stat = 0; stat < 8; ++stat) {
            int place = 1;
            for (int j = 0; j < n; ++j)
                if (j != i && (level(i, stat) & 0xffff) < (level(j, stat) & 0xffff)) ++place;
            statRank_[std::size_t(i)][std::size_t(stat)] = static_cast<std::int16_t>(place);
        }
}

// FUN_004a17b0: list each adventure (0x73bed0 records 1..100) that opens
// this week; FUN_004a9d90 inserts it at the front of the week's list.
void HanTask::addWeekAdventures() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    for (int id = 1; id < 0x65; ++id) {
        const std::uint32_t ev = kEvents + std::uint32_t(id) * 0x100;
        if (w.u8(kAdvSeen + std::uint32_t(id)) != 0) continue;
        if (logic::sdU8(ev + 1, v) == 0 || logic::sdU8(ev + 2, v) != w.month() || logic::sdU8(ev + 3, v) != w.week()) continue;
        if (logic::sdU8(ev + 0xe, v) == 0 || logic::sdU8(ev + 6, v) == 0) continue;

        const int type = logic::sdU8(ev + 6, v);
        int deadline = logic::sdU8(ev + 5, v);
        if (deadline == 99) deadline = -1;
        const int cat = type - 1 == 3 ? 0 : deadline == -1 ? 1 : 2;
        const int n = w.s16(kAdvCount);
        w.set16(kAdvCount, n + 1);
        for (int k = n; k > 0; --k) {                     // the original moves every field but +2
            const std::uint32_t to = kAdvList + std::uint32_t(k) * 0x10, from = to - 0x10;
            w.set16(to, w.s16(from));
            for (std::uint32_t o : {3u, 4u, 6u, 5u}) w.set8(to + o, w.u8(from + o));
            for (std::uint32_t o : {8u, 0xau, 0xcu, 0xeu}) w.set16(to + o, w.s16(from + o));
        }
        w.set16(kAdvList, id);
        w.set8(kAdvList + 2, 1);
        w.set8(kAdvList + 3, type - 1);
        w.set8(kAdvList + 4, logic::sdU8(ev + 7, v) - 1);
        w.set8(kAdvList + 5, cat);
        w.set8(kAdvList + 6, 0);
        w.set16(kAdvList + 8, deadline);
        w.set16(kAdvList + 0xa, logic::sdU8(ev + 4, v));
        w.set16(kAdvList + 0xc, 0);
        w.set16(kAdvList + 0xe, 0);
        w.set8(kAdvSeen + std::uint32_t(id), 1);
    }
}

// FUN_004a2ba0: lectures (0x74bed0 records 1..100) that have opened by
// now are added to every hermit flagged in their +0x3a table (FUN_004a24a0).
void HanTask::learnLectures() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    const int now = w.month() * 6 + w.week();
    for (int id = 1; id < 0x65; ++id) {
        const std::uint32_t lec = kLectures + std::uint32_t(id) * 0x60;
        if (w.u8(kLectureSeen + std::uint32_t(id)) != 0) continue;
        if (logic::sdU8(lec + 2, v) * 6 + logic::sdU8(lec + 3, v) > now) continue;
        if (logic::sdS16(lec + 6, v) == 0 || logic::sdS16(lec + 8, v) == 0) continue;
        for (int h = 0; h < 0x14; ++h) {
            if (logic::sdU8(lec + 0x3a + std::uint32_t(h), v) == 0) continue;
            const std::uint32_t base = kHermitLectures + std::uint32_t(h) * 1000;
            const int n = w.s16(0x7aa9ea + std::uint32_t(h) * 2);
            for (int k = n; k > 0; --k) {
                const std::uint32_t to = base + std::uint32_t(k) * 10, from = to - 10;
                w.set8(to, w.u8(from)); w.set8(to + 1, w.u8(from + 1));
                for (std::uint32_t o : {2u, 4u, 6u}) w.set16(to + o, w.s16(from + o));
            }
            w.set8(base, 1);
            w.set8(base + 1, 0);
            w.set16(base + 2, id);
            w.set16(base + 4, logic::sdS16(lec + 4, v));
            w.set16(base + 6, 0);
            w.set16(0x7aa9ea + std::uint32_t(h) * 2, n + 1);
        }
        w.set8(kLectureSeen + std::uint32_t(id), 1);
    }
}

// FUN_004a1c70: sort the week's adventures that are not under way into the
// three panel lists (0 forced, 1 open-ended, 2 with a deadline). A forced
// adventure fixes the plan (DAT_007a55fa) and is chosen at once.
void HanTask::buildAdventureLists() {
    GameWork& w = ctx_.work;
    bool forced = false;
    for (std::uint32_t c = 0; c < 3; ++c) w.set16(0x7d62fa + c * 2, 0);
    for (int u = 0; u < w.s16(kAdvCount); ++u) {
        const std::uint32_t a = kAdvList + std::uint32_t(u) * 0x10;
        const int cat = w.u8(a + 5);
        if (w.u8(a + 6) != 0) continue;
        const int n = w.s16(0x7d62fa + std::uint32_t(cat) * 2);
        w.set16(0x7d62fa + std::uint32_t(cat) * 2, n + 1);
        const std::uint32_t e = advEntry(cat, n);
        w.set16(e, w.u8(a + 3));
        w.set16(e + 2, w.u8(a + 4));
        w.set16(e + 4, w.s16(a));
        w.set16(e + 8, u);
        w.set16(e + 6, w.s16(a + 8) < 1 ? w.s16(a + 8) : w.s16(a + 8) - w.s16(a + 0xe));   // weeks left
        if (cat == 0) {
            w.set16(kPlanFixed, 1);
            if (!forced) { forced = true; selectAdventure(w, 0, n); w.set16(0x7aab0c, 0); w.set16(0x7aab0e, 0); }
        }
    }
    for (std::uint32_t c = 0; c < 3; ++c) {
        w.set16(0x7d6300 + c * 2, 0);
        const int rows = (w.u16(0x7d62fa + c * 2) + 1) / 2;
        w.set16(0x7d6306 + c * 2, rows < 5 ? 0 : rows - 4);
    }
    if (w.s16(0x7d62fe) == 0) w.set8(0x7d5984, 1);          // nothing with a deadline: open the other tab
}

// FUN_004a2980: the lectures hermit `hermit` (guest id - 0x65) can still
// give, by level, for the lecture panel.
void HanTask::buildLectureList(int hermit) {
    GameWork& w = ctx_.work;
    for (std::uint32_t l = 0; l < 3; ++l) {
        w.set16(0x7d6a14 + l * 2, 0);
        w.set16(0x7d6a1a + l * 2, 0);
        w.set16(0x7d6a20 + l * 2, 0);
    }
    if (hermit < 0) return;
    for (int i = 0; i < 100; ++i) {
        const std::uint32_t k = kHermitLectures + std::uint32_t(i) * 10 + std::uint32_t(hermit) * 1000;
        if (w.u8(k) == 0 || w.u8(k + 1) != 0) continue;
        const int id = w.s16(k + 2);
        const int lvl = logic::sdS16(kLectures + 6 + std::uint32_t(id) * 0x60, ctx_.version) - 1;
        if (lvl < 0 || lvl > 2) continue;
        const int n = w.s16(0x7d6a14 + std::uint32_t(lvl) * 2);
        w.set16(0x7d6a14 + std::uint32_t(lvl) * 2, n + 1);
        const std::uint32_t e = lecEntry(lvl, n);
        w.set16(e, logic::sdS16(kLectures + 8 + std::uint32_t(id) * 0x60, ctx_.version) - 1);
        w.set16(e + 2, id);
        w.set16(e + 4, n);
    }
    for (std::uint32_t l = 0; l < 3; ++l) {
        const int n = w.u16(0x7d6a14 + l * 2);
        if (n > 10) w.set16(0x7d6a20 + l * 2, (n - 9) / 2);
    }
}

void HanTask::drawCell(int rectIndex, int prio, int x, int y, int sx, int sy, int w, int h, std::uint8_t alpha) {
    ysub::Rect r = rect(rectIndex);
    r.sx = static_cast<std::int16_t>(sx); r.sy = static_cast<std::int16_t>(sy);
    r.w = static_cast<std::int16_t>(w); r.h = static_cast<std::int16_t>(h);
    drawRect(ctx_, r, prio, x, y, 0, 0, alpha);
}

// FUN_004aa7e0: an adventure's name plate (by type and sub index) and,
// in the lists, its weeks left: "--" for none, a badge for 1, else 2 digits.
void HanTask::drawAdventureItem(int x, int y, int type, int sub, bool title, int remaining,
                                std::uint8_t alpha, int prio) {
    int row = 0, plate = 0x27;
    switch (type) {
        case 0: row = 1; sub = std::clamp(sub, 0, 10); plate = 0x28; break;
        case 1: row = 2; sub = std::clamp(sub, 0, 10); plate = 0x29; break;
        case 2: row = 3; sub = std::clamp(sub, 0, 10); plate = 0x2a; break;
        case 3: row = 0; sub = std::clamp(sub, 0, 0x1f); remaining = 1; break;
        default: break;
    }
    if (!title) {
        constexpr int kDigits = 0x26;
        const ysub::Rect d = rect(kDigits);
        const int sy = d.sy + d.h * row;
        const auto rem = static_cast<std::int8_t>(remaining);
        if (rem == -1) {
            drawCell(kDigits, prio, x + 0x83, y + 5, d.sx + d.w * 10, sy, d.w, d.h, alpha);
            drawCell(kDigits, prio, x + 0x8d, y + 5, d.sx + d.w * 10, sy, d.w, d.h, alpha);
        } else if (rem == 1) {
            drawCell(kDigits, prio, x + 0x74, y, 0x11c, row * 0x16 + 0x1a2, 0x2c, 0x16, alpha);
        } else {
            drawCell(kDigits, prio, x + 0x83, y + 5, d.sx + d.w * (rem / 10), sy, d.w, d.h, alpha);
            drawCell(kDigits, prio, x + 0x8d, y + 5, d.sx + d.w * (rem % 10), sy, d.w, d.h, alpha);
        }
    }
    const ysub::Rect t = rect(plate);
    drawCell(plate, prio, x, y, t.sx + (sub / 0x17) * t.w, t.sy + (sub % 0x17) * t.h, title ? 0x74 : t.w, t.h, alpha);
}

// FUN_004aa6c0: a lecture's name plate (HanData frames 5..7 by level).
void HanTask::drawLectureItem(int x, int y, int level, int sub, std::uint8_t alpha, int prio) {
    sub = std::clamp(sub, 0, 0x50);
    const int frame = level == 1 ? 6 : level == 2 ? 7 : 5;
    drawRect(ctx_, {.bank = kBank, .frame = static_cast<std::uint16_t>(frame),
                    .sx = static_cast<std::int16_t>(sub / 0x17 * 0x74), .sy = static_cast<std::int16_t>(sub % 0x17 * 0x16),
                    .w = 0x74, .h = 0x16}, prio, x, y, 0, 0, alpha);
}

// FUN_004a1ff0: the week's adventures (8 per page, two columns) for an
// adventure group, the chosen one's name over the groups, and its text
// (TacticsExp entry 4 * id - 3 + info tab).
void HanTask::drawAdventurePanel() {
    GameWork& w = ctx_.work;
    const int sel = w.s8(kSelGroup);
    const bool active = w.u8(group(sel) + 3) == 0;        // the original reads it with sel = -1 too
    const int cat = w.s16(kPlanFixed) != 0 ? 0 : w.u8(0x7d5984) == 0 ? 2 : 1;
    w.set16(kEventShown, cat);
    const int count = w.s16(0x7d62fa + std::uint32_t(cat) * 2);
    if (count != 0) {
        const int page = w.s16(0x7d6300 + std::uint32_t(cat) * 2);
        for (int i = 0; i < 8; ++i) {
            const int idx = i + page * 2;
            if (idx >= count) break;
            const int col = i & 1, row = i / 2;
            std::uint8_t alpha = 0xcc;     // never set when inactive: the debug build's 0xcccccccc stack fill
            if (active) {
                const ysub::Hit h = ysub::hit(ctx_, col * 0xa9 + 0x238, row * 0x17 + 0x48, 0xa2, 0x14);
                alpha = 0x80;
                if (h.over) {
                    alpha = 0xff;
                    if (h.pressed & legacy::kMouseLeft) {
                        ctx_.audio->sePlay(kSe);
                        selectAdventure(w, cat, idx);
                        w.set16(0x7aab0c, col);
                        w.set16(0x7aab0e, row);
                    }
                }
                if (w.s16(kEventCat) == cat && w.s16(0x7aab14) == idx) {
                    alpha = 0xff;
                    drawRect(ctx_, rect(0x23), 0x71, col * 0xa9 + 0x229, row * 0x17 + 0x44);
                }
            }
            const std::uint32_t e = advEntry(cat, idx);
            drawAdventureItem(col * 0xa9 + 0x237, row * 0x17 + 0x47, w.s16(e), w.s16(e + 2), false,
                              w.s8(e + 6), alpha, 0x75);
        }
    }
    if (w.s16(kEventSet) != 0) {
        drawAdventureItem(0x60, 0x2f, w.s16(0x7aab10), w.s16(0x7aab12), true, 0, 0xff, 0x75);
        const int entry = w.s16(0x7d5982) - 3 + w.s16(kEventId) * 4;
        if (adventureEntry_ != entry) {
            tacticsText_.select(entry, 0xb);
            adventureEntry_ = static_cast<std::int16_t>(entry);
        }
        tacticsText_.draw(ctx_, 0x244, 0xd8, 0x79);
    }
}

// FUN_004a2da0: the selected leader's lectures of the current level for a
// lecture group, and the chosen lecture's text (LectureExp).
void HanTask::drawLecturePanel() {
    GameWork& w = ctx_.work;
    const int sel = w.s8(kSelGroup);
    const bool active = w.u8(group(sel) + 3) != 0;
    const int lvl = w.u8(0x7d598a);
    const int count = w.u16(0x7d6a14 + std::uint32_t(lvl) * 2);
    if (count == 0) return;
    const int page = w.s16(0x7d6a1a + std::uint32_t(lvl) * 2);
    for (int i = 0; i < 10; ++i) {
        const int idx = i + page * 2;
        if (i >= count) break;                             // the original compares the row, not idx
        const int col = i & 1, row = i / 2;
        const int x = col * 0x79 + 0x298, y = row * 0x17 + 0x1be;
        const std::uint32_t e = lecEntry(lvl, idx);
        std::uint8_t alpha = 0x80;
        if (active) {
            const ysub::Hit h = ysub::hit(ctx_, x, y, 0x70, 0x16);
            if (h.over) {
                alpha = 0xff;
                if (h.pressed & legacy::kMouseLeft) {
                    ctx_.audio->sePlay(kSe);
                    const int g = w.s8(kSelGroup);
                    w.set16(group(g) + 6, lvl);
                    w.set16(group(g) + 8, w.s16(e));
                    w.set16(group(g) + 0xa, w.s16(e + 2));
                    w.set16(group(g) + 0xc, w.s16(e + 4));
                }
            }
        }
        if (w.s16(group(sel) + 6) == lvl && w.s16(group(sel) + 8) == w.s16(e)) {
            alpha = 0xff;
            drawRect(ctx_, rect(0x24), 0x71, col * 0x79 + 0x28b, row * 0x17 + 0x1bb);
        }
        drawLectureItem(x, y, lvl, w.s16(e), alpha, 0x73);
    }
    const int id = w.s16(group(sel) + 0xa);
    if (id != 0 && id != -1) {
        if (lectureEntry_ != id) {
            lectureText_.select(id, 10);
            w.set8(0x7d598a, id < 1 ? 0 : logic::sdS16(kLectures + 6 + std::uint32_t(id) * 0x60, ctx_.version) - 1);
            lectureEntry_ = static_cast<std::int16_t>(id);
        }
        lectureText_.draw(ctx_, 0x244, 0x23e, 0x79);
    }
}

// FUN_004a1920 (after 予定進行 is confirmed): start the chosen adventure
// (or drop it if it takes no time), age the week's list, and drop those
// past their deadline.
void HanTask::commitAdventure() {
    GameWork& w = ctx_.work;
    bool none = true;
    int index = 0, id = 0;
    for (int g = 0; g < 5; ++g) {
        const GroupStatus st = groupStatus(w, g);
        if (st.kind != 2) continue;
        none = false;
        id = st.c;
        index = -1;
        for (int u = 0; u < w.s16(kAdvCount); ++u)          // FUN_004aa2a0
            if (w.s16(kAdvList + std::uint32_t(u) * 0x10) == id) { index = u; break; }
    }
    const auto remove = [&](int which) {                    // FUN_004aa0e0
        const int n = w.s16(kAdvCount);
        int u = 0;
        while (u < n && w.s16(kAdvList + std::uint32_t(u) * 0x10) != which) ++u;
        for (; u < n - 1; ++u) {
            const std::uint32_t to = kAdvList + std::uint32_t(u) * 0x10, from = to + 0x10;
            w.set16(to, w.s16(from));
            for (std::uint32_t o : {3u, 4u, 6u, 5u}) w.set8(to + o, w.u8(from + o));
            for (std::uint32_t o : {8u, 0xau, 0xcu, 0xeu}) w.set16(to + o, w.s16(from + o));
        }
        w.set16(kAdvCount, n - 1);
    };
    if (!none) {
        const std::uint32_t a = kAdvList + std::uint32_t(index & 0xffff) * 0x10;
        if (w.s16(a + 0xa) < 1) remove(id);
        else { w.set8(a + 6, 1); w.set16(a + 0xc, 0); }
    }
    std::vector<int> expired;
    for (int u = 0; u < w.s16(kAdvCount); ++u) {
        const std::uint32_t a = kAdvList + std::uint32_t(u) * 0x10;
        w.set16(a + 0xe, w.s16(a + 0xe) + 1);
        if (w.u8(a + 6) == 0) w.set16(a + 0xc, 0);
        else if (w.u8(a + 6) == 1) {
            w.set16(a + 0xc, w.s16(a + 0xc) + 1);
            if (w.s16(a + 0xa) <= w.s16(a + 0xc)) w.set8(a + 6, 0);
        }
        if (w.s16(a + 8) >= 0 && w.s16(a + 8) <= w.s16(a + 0xe)) expired.push_back(w.s16(a));
    }
    for (int e : expired) remove(e);
}

// FUN_004a6a10 (command 8): record each group's status (0x7a529a + 0x70 g)
// and, for the adventure, who goes into each of its stages: leaders at
// +0x60, members at +0x36 with their group at +0xe (type 1: every
// adventure group; type 2: the groups flagged in that stage's condition).
void HanTask::writeSortie() {
    GameWork& w = ctx_.work;
    const GameVersion v = ctx_.version;
    std::array<GroupStatus, 5> st{};
    int going = 0;
    for (int g = 0; g < 5; ++g) {
        st[std::size_t(g)] = groupStatus(w, g);
        const std::uint32_t r = 0x7a529a + std::uint32_t(g) * 0x70;
        const GroupStatus& s = st[std::size_t(g)];
        for (auto [o, val] : {std::pair{0u, s.kind}, {2u, s.affinity}, {4u, s.grade}, {6u, s.a},
                              {8u, s.b}, {0xau, s.c}, {0xcu, s.d}})
            w.set16(r + o, val);
        if (s.kind == 2) ++going;
    }
    for (std::uint32_t g = 0; g < 5; ++g) w.set16(0x7a5304 + g * 0x70, 0);
    if (going == 0) { w.set16(0x7a5296, 0); w.set16(0x7a5298, 0); return; }

    const auto ev = std::uint32_t(w.s16(kEventId));
    const std::uint32_t evRec = kEvents + ev * 0x100;
    const int stages = logic::sdU8(evRec + 0xf, v);
    w.set16(0x7a5296, 0);
    w.set16(0x7a5298, stages);
    w.set16(0x7a5294, int(ev));
    const int type = logic::sdS16(evRec + 0x12, v) & 0x7ff;      // the first stage's type, for every stage
    for (int s = 0; s < stages; ++s) {
        const std::uint32_t r = std::uint32_t(s) * 0x70;
        w.set16(0x7a5308 + r, logic::sdS16(evRec + 0x10 + std::uint32_t(s) * 4, v));
        w.set16(0x7a52f8 + r, 0);
        w.set16(0x7a5304 + r, 0);
        int n = 0;
        const auto cond = static_cast<std::uint16_t>(logic::sdS16(evRec + 0x12 + std::uint32_t(s) * 4, v));
        for (int g = 0; g < 5 && (type == 1 || type == 2); ++g) {
            const bool goes = type == 1 ? st[std::size_t(g)].kind == 2 : (cond >> (11 + g) & 1) != 0;
            if (!goes) continue;
            const int leaders = w.s16(0x7a5304 + r);
            w.set16(0x7a5304 + r, leaders + 1);
            w.set16(0x7a52fa + std::uint32_t(leaders) * 2 + r, w.s16(group(g)));
            for (std::uint32_t k = 0; k < 4; ++k) {
                const int m = w.s16(group(g) + 0x10 + k * 2);
                if (m == -1) continue;
                w.set16(0x7a52d0 + std::uint32_t(n) * 2 + r, m);
                w.set16(0x7a52a8 + std::uint32_t(n) * 2 + r, g);
                ++n;
            }
        }
        w.set16(0x7a52f8 + r, n);
    }
}

// ----------------------------------------------------------------- popup ---
// FUN_004a3240: the right-click info card, open while the pointer stays on
// the unit: job and rank, the last five weeks, adventure / lecture counts,
// the 7 stat levels and their total with roster places, and for a group
// at a lecture the levels the lecture would add.
void HanTask::drawPopup() {
    GameWork& w = ctx_.work;
    if (w.s16(kPopupOpen) == 0) return;
    if (!ysub::hit(ctx_, w.s16(0x7d57d0), w.s16(0x7d57d2), w.s16(0x7d57d4), w.s16(0x7d57d6)).over) {
        w.set16(kPopupOpen, 0);
        return;
    }
    const int unit = w.s16(0x7d57c8);
    const std::uint32_t rec = GameWork::unitRecord(unit);
    const int x = w.s16(0x7d57cc), y = w.s16(0x7d57ce);
    const int jx = x + 0x59, jy = y + 5;
    constexpr int kDigitsFlags = ysub::kDigitsNoZeros | ysub::kDigitsRight;
    drawRect(ctx_, rect(0x2e), 0x6f, x, y);
    const int job = w.s16(rec + 6) - 1;
    drawCell(0x31, 0x6c, jx, jy, job / 10 * 0x52, job % 10 * 0x18 + 0xbc, 0x52, 0x18, 0xff);
    ysub::drawDigits(ctx_, rect(0x34), jx + 0x6e, jy + 6, 2, 0x6e, w.u8(rec + 0x50), 10, 1, 0, kDigitsFlags);

    // History: one 3-byte record per week since April week 1 (+0x24 in the unit's progress).
    const int weeks = (w.month() - 4) * 5 + w.week() - 1;
    const std::uint32_t prog = GameWork::unitProgress(unit);
    int lectures = 0, adventures = 0;
    for (int i = 0; i < weeks; ++i) {
        const int kind = w.s8(prog + 0x24 + std::uint32_t(i) * 3);
        if (kind == 1) ++lectures;
        else if (kind == 2) ++adventures;
    }
    ysub::drawDigits(ctx_, rect(0x35), jx + 0x15e, jy + 0x91, 2, 0x6e, std::uint32_t(adventures), 10, 1, 0, kDigitsFlags);
    ysub::drawDigits(ctx_, rect(0x35), jx + 0x15e, jy + 0xa3, 2, 0x6e, std::uint32_t(lectures), 10, 1, 0, kDigitsFlags);
    for (int i = 0; i < std::min(weeks, 5); ++i) {
        const std::uint32_t h = prog + 0x24 + std::uint32_t(weeks - i - 1) * 3;
        const int hx = x + 0x155, hy = y + 0x23 + i * 0x16;
        switch (w.s8(h)) {
            case 1: drawLectureItem(hx, hy, w.s8(h + 1), w.s8(h + 2), 0xff, 0); break;
            case 2: drawAdventureItem(hx, hy, w.s8(h + 1), w.s8(h + 2), true, 0, 0xff, 0); break;
            case 3: drawRect(ctx_, rect(0x37), 0, hx, hy); break;
            default: break;
        }
    }

    // Stats: level bar (/0x87), progress line to the next level, level and roster place.
    const int slot = std::clamp(logic::rosterSlot(w, unit), 0, int(statRank_.size()) - 1);
    int total = 0;
    for (int i = 0; i < 8; ++i) {
        const int ry = i * 0x12;
        int value = 0, bar = 0;
        if (i < 7) {
            const std::uint32_t s7 = rec + 0xc + std::uint32_t(i) * 8;
            const logic::StatProgress p = logic::statProgress(w, w.u8(s7), w.s32(s7 + 4));
            drawCell(0x30, 0x6e, x + 0x65, y + 0x35 + ry, 0xf6, 0x10c, p.percent * 0x44 / 100, 1, 0xff);
            value = w.u8(s7);
            total += value;
            bar = value * 0x44 / 0x87;
        } else {
            value = total;
            bar = total * 0x44 / 0x3b1;
        }
        drawCell(0x2f, 0x6e, x + 0x65, y + 0x29 + ry, 0xf6, i * 10 + 0xbc, bar, 10, 0xff);
        ysub::drawDigits(ctx_, rect(0x33), x + 0xcc, y + 0x28 + ry, 3, 0x6e, std::uint32_t(value), 10, 1, 0, kDigitsFlags);
        ysub::drawDigits(ctx_, rect(0x34), x + 0xcc + 0x24, y + 0x28 + ry, 2, 0x6e,
                         std::uint32_t(statRank_[std::size_t(slot)][std::size_t(i)]), 10, 1, 0, kDigitsFlags);
    }

    const int grp = w.s16(0x7d57ca);
    if (grp == -1) return;
    const GroupStatus st = groupStatus(w, grp);
    int gained = 0;
    if (st.kind == 4) {                                  // levels the chosen lecture would add
        const auto gain = logic::lectureGain(w, ctx_.version, unit, w.s16(group(grp) + 0xa), st.affinity);
        for (int c = 0; c < 7; ++c) {
            const std::uint32_t s7 = rec + 0xc + std::uint32_t(c) * 8;
            int up = logic::expToLevel(w.s32(s7 + 4) + gain[std::size_t(c)], ctx_.version) - w.u8(s7);
            gained += up;
            const bool more = up > 7;
            if (more) up = 7;
            const int ry = y + 0x29 + c * 0x12;
            if (gain[std::size_t(c)] == 0) continue;
            if (up == 0) { drawRect(ctx_, rect(0x38), 0x6e, x + 0x104, ry); continue; }
            for (int k = 0; k < up; ++k)
                drawRect(ctx_, rect(0x39), 0x6e, x + 0x104 + k * 10, ry, k == 6 && more ? 0x14 : 10, 0);
        }
    }
    if (w.u8(0x7d57c4) == 0) {                           // one remark per opening
        if (st.kind == 4) { w.set8(0x7d57c4, 1); voice(unit, 4, gained); }
        if (st.kind == 2) { w.set8(0x7d57c4, 1); voice(unit, 3, st.grade); }
    }
}

// ------------------------------------------------------------------ draw ---
// FUN_004a81a0
void HanTask::draw() {
    GameWork& w = ctx_.work;
    const bool fixed = w.s16(kPlanFixed) != 0;
    drawAffinity();

    drawRect(ctx_, rect(kFrame), 0x80, 0, 0);
    const ysub::Rect small = rect(kDigitsSmall), large = rect(kDigitsLarge);
    const auto num = [&](const ysub::Rect& r, int x, int y, std::uint32_t va) {
        ysub::drawNumber(ctx_, r, x, y, 2, 0x7a, std::uint32_t(w.s16(va)));
    };
    num(small, 0x36, 0x244, 0x7d57de);
    num(small, 0x54, 0x244, 0x7d57e0);
    num(small, 0x36, 0x2a5, 0x7d58b0);
    num(small, 0x54, 0x2a5, 0x7d58b2);
    num(large, 0x3e2, 0x1dd, 0x7d6a14);
    num(large, 0x3e2, 0x1fc, 0x7d6a16);
    num(large, 0x3e2, 0x21b, 0x7d6a18);
    num(large, 0x3e2, 0x70, 0x7d62fe);
    num(large, 0x3e2, 0x8f, 0x7d62fc);
    if (fixed) drawRect(ctx_, rect(kPlanFixedMark), 0x78, 0x3a1, 0x47);
    drawWaiting();                                   // FUN_004a6460
    drawGroups();                                    // FUN_004a5440
    drawAdventurePanel();                            // FUN_004a1ff0
    drawLecturePanel();                              // FUN_004a2da0

    if (!fixed) {
        const int lt = w.s8(0x7d598a);
        if (lt >= 0 && lt <= 2) drawButton(0x2c + lt, 0, 0, 0x7f);
        const int at = w.u8(0x7d5984);
        if (at <= 1) drawButton(0x1e + at, 0, 0, 0x7f);
    } else {
        fill(ctx_, 0x3a2, 0x68, 0x5a, 0x3c, 0x70, 0x90010101u);
    }

    bool noAdventure = true;
    for (int g = 0; g < 5; ++g) {
        const int y = g * 0x61;
        if (w.s16(group(g)) == -1) {
            fill(ctx_, 6, y + 0x4d, 0x212, 0x5f, 0x70, 0x90010101u);
        } else if (ysub::hit(ctx_, 10, y + 0x51, 0x20a, 0x57).pressed & legacy::kMouseLeft) {
            selectGroup(g);
            ctx_.audio->sePlay(kSe);
        }
        if (w.u8(group(g) + 3) == 0) {
            noAdventure = false;
            drawButton(g + 1, 0, 0, 0x7f);
            drawButton(g + 0x19, 0, g * 0x1a, 0x7f);
            fill(ctx_, 0x60, y + 0x96, 0x74, 0x16, 0x70, 0x90010101u);
        } else {
            drawButton(g + 6, 0, 0, 0x7f);
        }
    }
    if (noAdventure) fill(ctx_, 0x61, 0x30, 0x74, 0x16, 0x70, 0x90010101u);

    const int sel = w.s8(kSelGroup);
    if (sel != -1) {
        if (w.s16(group(sel)) == -1) selectGroup(-1);
        else {
            drawRect(ctx_, rect(kSelectedGroup), 0x71, 3, sel * 0x61 + 0x4a);
            const int leader = w.s16(group(sel)) - 0x65;
            drawPortrait(0x23d, 0x1d3, 1, leader, 0xff, 0x77);   // lecture panel: the leader teaching
            drawName(0x241, 0x21e, 1, leader, 0xff, 0x77);
        }
    }
    if (w.u8(kCanCommit) == 0) fill(ctx_, 0xd6, 0x28, 0x142, 0x23, 0x70, 0x90010101u);

    const int it = w.s16(0x7d5982);
    if (it >= 0 && it <= 3) drawButton(0x20 + it, 0, 0, 0x7f);
    const int lt = w.u8(0x7d57da);
    if (lt <= 2) drawButton(0xb + lt, 0, 0, 0x7f);

    // Pointer: 40x48 cells at (0x326 + 0x28 * shape, 0x300) in HanData frame 0.
    if (cursor_ <= 2) {
        const auto& m = ctx_.mouse();
        drawRect(ctx_, {.bank = kBank, .frame = 0, .sx = static_cast<std::int16_t>(0x326 + 0x28 * cursor_), .sy = 0x300,
                        .w = 0x28, .h = 0x30}, 0x65, m.x, m.y);
    }

    // Dim the panel that does not apply to the selected group.
    bool dimAdventure = true, dimLecture = true;
    if (sel != -1 && w.s16(group(sel)) != -1) {
        if (w.u8(group(sel) + 3) == 0) dimAdventure = false;
        else dimLecture = false;
    }
    if (dimAdventure) fill(ctx_, 0x232, 0x27, 0x1cd, 0x184, 0x70, 0x90010101u);
    if (dimLecture)   fill(ctx_, 0x232, 0x1b6, 0x1cd, 0x145, 0x70, 0x90010101u);
    flashes_.draw(ctx_);
}

} // namespace ch
