// ============================================================================
// tactics_script.cpp
// ============================================================================
#include "tactics_script.hpp"
#include "../adv/adv_scene.hpp"
#include "../state/game_logic.hpp"
#include "../ysub/ysub.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace ch {

namespace {
constexpr std::uint32_t kScriptNames = 0x60bc78;   // char* [400]
constexpr int kMapCount = 400;
constexpr std::uint32_t kBattle = 0x809348;        // FUN_004e2760 / FUN_004e29e0: 0xb0 bytes
constexpr int kMaxSteps = 200000;                  // a check pass that never ends
constexpr std::int32_t kDimFull = 0x9800;          // DAT_0059e8bc
constexpr std::uint16_t kBannerBank = 0x5b;         // data\Tactics\TactStart\<per map> (FUN_00451bf0)
constexpr std::uint32_t kBannerNames = 0x60c2b8;    // char* per map
constexpr std::uint32_t kBannerTiming = 0x60cabc;   // s16: -, in step, hold, out step, x, y, wait for a click
constexpr std::uint32_t kBannerFrames = 0x60cb04;   // {s16 bank, frame}

bool trace() { return SDL_getenv("CH_SCRIPT") != nullptr; }

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t o) {
    if (o + 4 > b.size()) return 0;
    return std::uint32_t(b[o]) | std::uint32_t(b[o + 1]) << 8 | std::uint32_t(b[o + 2]) << 16 | std::uint32_t(b[o + 3]) << 24;
}

// The typed value blocks: type 0 s32 x10 at +0, 1 s16 x20 at +0x28,
// 2 u8 x40 at +0x50, 3 bits at +0x78 (bit 31 of the first word is index 0).
std::int32_t readValue(const GameWork& w, std::uint32_t base, int type, std::uint32_t i) {
    switch (type) {
        case 0: return w.s32(base + 4 * i);
        case 1: return w.s16(base + 0x28 + 2 * i);
        case 2: return static_cast<std::int8_t>(w.u8(base + 0x50 + i));   // read signed
        case 3: return (w.u32(base + 0x78 + 4 * (i >> 5)) >> (31 - (i & 31))) & 1;
        default: return 0;
    }
}
void writeValue(GameWork& w, std::uint32_t base, int type, std::uint32_t i, std::int32_t v) {
    switch (type) {
        case 0: w.set32(base + 4 * i, v); break;
        case 1: w.set16(base + 0x28 + 2 * i, static_cast<std::int16_t>(v)); break;
        case 2: w.set8(base + 0x50 + i, static_cast<std::uint8_t>(v)); break;
        case 3: {
            const std::uint32_t at = base + 0x78 + 4 * (i >> 5), bit = 1u << (31 - (i & 31));
            w.set32(at, v != 0 ? (w.u32(at) | bit) : (w.u32(at) & ~bit));
            break;
        }
        default: break;
    }
}
// Index limits of the battle's value set (the asserts in the functions).
bool battleIndexOk(int type, std::uint32_t i) {
    static constexpr std::uint32_t kLimit[4] = {10, 20, 40, 320};
    return type >= 0 && type < 4 && i < kLimit[type];
}
} // namespace

StageScript::StageScript(GameContext& ctx, TacticsMap& map, TacticsUnits& units)
    : ctx_(ctx), map_(map), units_(units), checkVm_(*this, ctx.work) {}

StageScript::~StageScript() {
    if (bannerLoaded_) ctx_.sprites->unloadBank(kBannerBank);
}

bool StageScript::load(int mapId) {
    GameWork& w = ctx_.work;
    // The battle's values start cleared (FUN_004e26e0, from the battle
    // task's constructor).
    for (std::uint32_t i = 0; i < 0xb0; ++i) w.set8(kBattle + i, 0);
    // FUN_00496bc0: the areas numbered as they are, off, open to all teams
    // (FUN_00496c60, a copy of area records, has no caller); FUN_00497800
    // clears the dimming.
    for (std::size_t i = 0; i < areas_.size(); ++i) areas_[i] = Area{static_cast<std::uint8_t>(i), false, 0xffff};
    dimming_ = false;
    dimValue_ = 0;
    timers_ = {};
    intervals_ = {};
    appears_ = {};                                   // FUN_00455c20
    groupPool_.fill(-1);
    banner_ = {};                                    // FUN_004562f0
    points_ = {};
    marks_ = {};
    resultMessage_ = 0;
    file_.clear();
    if (mapId < 0 || mapId >= kMapCount) return false;
    // FUN_00451bf0: the stage's banner pictures.
    if (const auto bat = static_cast<std::uint32_t>(logic::sdS32(kBannerNames + std::uint32_t(mapId) * 4, ctx_.version)); bat != 0) {
        std::string banner;
        for (std::uint32_t i = 0; i < 0x20; ++i) {
            const auto c = static_cast<char>(logic::sdU8(bat + i, ctx_.version));
            if (c == 0) break;
            banner.push_back(c);
        }
        try {
            if (bannerLoaded_) ctx_.sprites->unloadBank(kBannerBank);
            ctx_.sprites->loadBank(kBannerBank, ctx_.readData("data\\Tactics\\TactStart\\" + banner));
            bannerLoaded_ = true;
        } catch (const std::exception& ex) {
            SDL_Log("[script] %s: %s", banner.c_str(), ex.what());
        }
    }
    std::string name;
    const auto at = static_cast<std::uint32_t>(logic::sdS32(kScriptNames + std::uint32_t(mapId) * 4, ctx_.version));
    for (std::uint32_t i = 0; i < 0x20; ++i) {
        const auto c = static_cast<char>(logic::sdU8(at + i, ctx_.version));
        if (c == 0) break;
        name.push_back(c);
    }
    try {
        file_ = ctx_.readData("data\\Tactics\\Script\\" + name);
    } catch (const std::exception& ex) {
        SDL_Log("[script] %s: %s", name.c_str(), ex.what());
        return false;
    }
    SDL_Log("[script] map %d: %s, %zu bytes", mapId, name.c_str(), file_.size());
    return !file_.empty();
}

// FUN_00455060: entry `index` of block `block`, up to the next entry.
std::vector<std::uint8_t> StageScript::chunk(int block, int index) const {
    if (file_.empty() || block < 0 || std::uint32_t(block) >= le32(file_, 4) || index < 0) return {};
    const std::size_t b = le32(file_, 8 + 4 * std::size_t(block));
    const std::uint32_t n = le32(file_, b + 4);
    if (std::uint32_t(index) + 1 >= n) return {};
    const std::size_t s = b + le32(file_, b + 8 + 4 * std::size_t(index));
    const std::size_t e = b + le32(file_, b + 8 + 4 * std::size_t(index + 1));
    // An empty entry is a 4-byte placeholder (the lstrcmp in FUN_004551c0).
    if (e <= s + 4 || e > file_.size()) return {};
    return {file_.begin() + std::ptrdiff_t(s), file_.begin() + std::ptrdiff_t(e)};
}

bool StageScript::check(int block, int index) {
    auto code = chunk(block, index);
    if (code.empty()) return false;
    // FUN_00497720: FUN_00496d80(0), FUN_00496df0(0), the checking flag
    // (FUN_00497850 clears it); FUN_004ce460, FUN_004ce030, run to the end,
    // FUN_004ce060; FUN_00496e30 reads the result.
    mode_ = false;
    found_ = false;
    checking_ = true;
    try {
        checkVm_.loadCode(std::move(code), 0x7f);
        int steps = 0;
        while (checkVm_.tick() && ++steps < kMaxSteps) {}
        if (steps >= kMaxSteps) SDL_Log("[script] chunk %d/%d: the check pass did not end", block, index);
    } catch (const std::exception& ex) {
        SDL_Log("[script] chunk %d/%d: %s", block, index, ex.what());
        found_ = false;
    }
    checking_ = false;
    mode_ = found_;
    if (trace()) SDL_Log("[script] check %d/%d -> %d", block, index, int(found_));
    return found_;
}

void StageScript::startEvent(int block, int index, bool endsBattle, bool runScript, bool fade, bool pause) {
    event_ = Event{};
    ++eventSerial_;
    event_.active = true;
    event_.endsBattle = endsBattle;
    event_.runScript = runScript;
    event_.fade = fade;
    event_.pause = pause;
    if (pause) units_.requestHold(1, false);         // FUN_004549d0: FUN_00467850(1, 0)
    SDL_Log("[script] event %d/%d%s%s", block, index, runScript ? "" : " (no script)", endsBattle ? ", ends the battle" : "");
    if (runScript) {
        auto code = chunk(block, index);
        if (code.empty()) {
            event_.advDone = event_.localDone = true;
            return;
        }
        mode_ = true;
        // The original loads the event UI (data\\adv\\bin\\Gybc_00.bin) into
        // bank 0x14 once per battle (FUN_004548e0) and frees the event's
        // banks 10..0x58 but 0x14 afterwards (FUN_00454ee0); the ADV task
        // loads and frees its own.
        // In network play the dialog's end is our own (localDone) until all have reported.
        ctx_.tasks.link(std::make_unique<AdvTask>(ctx_, std::move(code), static_cast<IYbcHost*>(this),
                                                  netDone_ ? &event_.localDone : &event_.advDone), 0, 0);
    } else {
        event_.advDone = event_.localDone = true;
    }
}

void StageScript::runIf(bool endsBattle, int block, int index) {
    if (check(block, index)) startEvent(block, index, endsBattle, true, false, true);
    else if (endsBattle) startEvent(block, index, true, false, false, true);
}

// FUN_00454cf0, shortened: the script, then (+0x50) the undim, then the end.
void StageScript::finishEvent() {
    if (trace()) SDL_Log("[script] event over (view %d,%d)", map_.scrollX(), map_.scrollY());
    if (event_.endsBattle) battleOver_ = true;       // +0x171 / +0x172
    else if (event_.pause) units_.requestHold(1, true);   // FUN_00467850(1, 1)
    event_.active = false;
    held_ = nullptr;
}

void StageScript::update() {
    // Network play: report our dialog's end once, and count the event's
    // dialog over when everyone present has (netEventDone).
    if (netDone_ && event_.active && event_.runScript) {
        if (event_.localDone && !event_.reported) {
            event_.reported = true;
            netDone_(eventSerial_);
        }
        if (!event_.advDone && (event_.doneMask & netPlayers_) == netPlayers_) event_.advDone = true;
    }
    if (event_.localDone) held_ = nullptr;
    // The event's VM goes with its ADV task: once that is done (a skip can
    // finish it before the camera or the dimming), nothing waits on it.
    if (event_.advDone) held_ = nullptr;
    // The camera (FUN_00498310): an eighth of the way a frame, at least 8 px.
    if (scrolling_) {
        const auto step = [](int from, int to) {
            const int d = to - from;
            if (std::abs(d) <= 8) return to;
            return from + (d > 0 ? std::max(8, d / 8) : std::min(-8, d / 8));
        };
        const int x = step(map_.scrollX(), scrollTargetX_), y = step(map_.scrollY(), scrollTargetY_);
        map_.scrollTo(x, y);
        const bool arrived = map_.scrollX() == scrollTargetX_ && map_.scrollY() == scrollTargetY_;
        const bool stuck = map_.scrollX() != x || map_.scrollY() != y;   // clamped at the map's edge
        if (arrived || stuck) {
            scrolling_ = false;
            release();
        }
    }
    // FUN_00497aa0: the dimming.
    if (dimming_) {
        dimValue_ += dimIn_ ? dimStep_ : -dimStep_;
        if (dimIn_ && dimValue_ >= kDimFull) { dimValue_ = kDimFull; dimming_ = false; }
        if (!dimIn_ && dimValue_ <= 0) { dimValue_ = 0; dimming_ = false; }
        if (!dimming_) release();
    }
    tickBanner();
    if (event_.active) {
        static int beat = 0;
        if (trace() && ++beat % 120 == 0)
            SDL_Log("[script] event: adv %d dim %d/%d held %d scroll %d", int(event_.advDone), int(dimming_), dimValue_,
                    int(held_ != nullptr), int(scrolling_));
        if (!event_.advDone) return;
        // FUN_00454cf0 case 3: an event that undims waits for FUN_00454c80.
        if (event_.fade && !event_.released) return;
        if (event_.fade && !event_.undimming) {
            event_.undimming = true;
            if (dimValue_ > 0) dim(0x3c, false);     // FUN_00497980(0x3c)
        }
        if (dimming_) return;
        finishEvent();
        return;
    }
    if (timersOn_ && !battleOver_) {
        tickTimers();
        tickAppears();
    }
}

// FUN_00455ec0: one appear timer that runs out a frame (FUN_00455fb0).
void StageScript::tickAppears() {
    for (Appear& a : appears_) {
        if (!a.used || --a.left >= 1) continue;
        const Appear fired = a;
        a = Appear{};
        if (fired.single) {
            units_.appearUnit(fired.who, fired.cx, fired.cy, 10);    // FUN_00498740 -> FUN_0047fcf0 (FUN_00498840: empty)
        } else {
            const int n = units_.appearGroup(fired.who, fired.cx, fired.cy, fired.range);   // FUN_004988d0
            if (trace()) SDL_Log("[script] appear timer: group %d, %d units near %d,%d", fired.who, n, fired.cx, fired.cy);
        }
        return;
    }
}

// FUN_004569b0: the pool's groups in random order; the first whose enemies
// are all off the field (FUN_00430980), -1 if none.
int StageScript::pickGroup() {
    std::vector<int> pool;
    for (const std::int16_t g : groupPool_) if (g != -1) pool.push_back(g);
    const std::size_t n = pool.size();
    for (std::size_t i = 0; i < n; ++i)
        std::swap(pool[std::size_t(units_.random()) % n], pool[std::size_t(units_.random()) % n]);
    for (const int g : pool)
        if (units_.groupClear(g)) return g;
    return -1;
}

// FUN_00497c90: the dimming (FUN_00497c50 tells whether it runs), and
// FUN_00456650: the banner, centred on its point.
void StageScript::draw() const {
    const int a = dimValue_ >> 8;
    // FUN_00497c90 (its fill: JP 0x44d824, TW 0x497cda): the whole screen at
    // priority 0x65, so the banner (100) and anything nearer stay bright.
    if (a > 0) ysub::fill(ctx_, 0, 0, 0x400, 0x300, 0x65, std::uint32_t(a) << 24);
    if (banner_.on && bannerLoaded_) {
        const GameVersion v = ctx_.version;
        const std::uint32_t f = kBannerFrames + std::uint32_t(banner_.idx) * 4;
        SpriteCmd c{.bank = static_cast<std::uint16_t>(logic::sdS16(f, v)), .frame = static_cast<std::uint16_t>(logic::sdS16(f + 2, v)),
                    .x = banner_.x + logic::sdS16(kBannerTiming + 8, v), .y = banner_.y + logic::sdS16(kBannerTiming + 0xa, v),
                    .flags = std::uint16_t(kSprBank | kSprCentre), .blend = kBlendAlpha,
                    .color = {0x80, 0x80, 0x80, static_cast<std::uint8_t>(banner_.alpha >> 8)},
                    .priority = static_cast<std::uint16_t>(banner_.prio)};
        ctx_.draw.sprite(c);
    }
}

// FUN_004563d0: in by the table's step, then a click (or its hold), out
// again; the script goes on at the end (FUN_004977a0).
void StageScript::tickBanner() {
    if (!banner_.on) return;
    const GameVersion v = ctx_.version;
    switch (banner_.phase) {
        case 0:
            banner_.alpha = 0;
            banner_.step = logic::sdS16(kBannerTiming + 2, v);
            ++banner_.phase;
            [[fallthrough]];
        case 1:
            banner_.alpha += banner_.step;
            if (banner_.alpha > 0xfeff) {
                banner_.alpha = 0xff00;
                if (logic::sdS16(kBannerTiming + 0xc, v) == 0) {
                    banner_.hold = logic::sdS16(kBannerTiming + 4, v);
                    banner_.phase = 3;
                } else {
                    ++banner_.phase;
                }
            }
            break;
        case 2: {
            const bool skip = (held_ && held_->fastForward()) || SDL_getenv("CH_AUTOSKIP");
            if ((ctx_.mouse().pressed & legacy::kMouseLeft) || ctx_.engine.input().pressed(legacy::Action::Confirm) || skip) {
                banner_.phase = 4;
                banner_.step = logic::sdS16(kBannerTiming + 6, v);
            }
            break;
        }
        case 3:
            if (--banner_.hold <= 0) {
                banner_.phase = 4;
                banner_.step = logic::sdS16(kBannerTiming + 6, v);
            }
            break;
        case 4:
            banner_.alpha -= banner_.step;
            if (banner_.alpha < 0x100) {
                banner_.alpha = 0;
                ++banner_.phase;
            }
            break;
        default:
            banner_.on = false;
            release();
            break;
    }
}

// FUN_00434c40 (map 17, 0x88 sub-command 0x1b): one of the groups 1..12
// (14 and 15 once units 0x2d / 0x2e of the enemy table are gone) whose
// enemies are all off the field comes back whole at one of four places,
// and the next raid is set for 30 seconds to 4.8 minutes on (chunk 0x14).
void StageScript::raid() {
    const GameVersion v = ctx_.version;
    groupPool_.fill(-1);
    std::size_t n = 0;
    for (int g = 1; g <= 0xc; ++g) groupPool_[n++] = static_cast<std::int16_t>(g);
    const auto gone = [&](int record) {
        const TacticsUnits::Unit& u = units_.unit(TacticsUnits::kSlots - record);
        const int at = u.data.u8(0xf);
        return u.used && at >= 2 && at <= 4;     // FUN_00469220
    };
    if (gone(0x2d)) groupPool_[n++] = 0xe;
    if (gone(0x2e)) groupPool_[n++] = 0xf;
    if (const int g = pickGroup(); g != -1) {
        units_.reviveGroup(g);                       // FUN_00498e60
        units_.setGroupField(g, 3, 1);
        units_.setGroupField(g, 1, 0);
        const std::uint32_t at = 0x5ff7e4 + std::uint32_t(units_.random() & 3) * 4;
        units_.setGroupField(g, 2, static_cast<std::int8_t>(logic::sdU8(at + 3, v)));
        const int k = units_.appearGroup(g, static_cast<std::int8_t>(logic::sdU8(at, v)), static_cast<std::int8_t>(logic::sdU8(at + 1, v)),
                                         static_cast<std::int8_t>(logic::sdU8(at + 2, v)));
        if (trace()) SDL_Log("[script] raid: group %d, %d units", g, k);
    }
    const int frames = units_.random() % 0x3f48 + 0x708;
    for (Timer& t : timers_)                         // FUN_004554c0 -> FUN_00498fc0(.., 0, 0x14, ..)
        if (!t.used) { t = Timer{true, 0, 0x14, frames, frames}; break; }
}

// FUN_00455560 (one-shot) then FUN_00455950 (repeating); one fires a frame.
void StageScript::tickTimers() {
    for (Timer& t : timers_) {
        if (!t.used || --t.left >= 1) continue;
        const Timer fired = t;
        t = Timer{};
        if (trace()) SDL_Log("[script] timer -> chunk %d/%d", fired.block, fired.index);
        if (check(fired.block, fired.index)) startEvent(fired.block, fired.index, false, true, false, true);
        return;
    }
    for (Timer& t : intervals_) {
        if (!t.used || --t.left != 0) continue;
        if (trace()) SDL_Log("[script] interval -> chunk %d/%d", t.block, t.index);
        // As the original: the event and the restart read the first entry,
        // so a repeating timer other than entry 0 fires only once.
        if (check(t.block, t.index)) startEvent(intervals_[0].block, intervals_[0].index, false, true, false, true);
        intervals_[0].left = intervals_[0].period;
        return;
    }
}

// FUN_00496e70 / FUN_00496f10 and the rest of FUN_00430400.
int StageScript::areaAt(int cx, int cy) const {
    const int a = map_.cell(cx, cy, 2);
    if (a < 0 || a >= int(areas_.size()) || !areas_[std::size_t(a)].on) return 0;
    return areas_[std::size_t(a)].id;
}

void StageScript::checkAreas() {
    tally_ = {};
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        const TacticsUnits::Unit& u = units_.unit(slot);
        if (!u.used || !u.onField()) continue;
        const int a = map_.cell(u.cx(), u.cy(), 2);  // FUN_00430730 -> FUN_0043bb90
        if (a < 0 || a >= 32) continue;
        Tally& t = tally_[std::size_t(a)][std::size_t(u.team() & 0xf)];
        ++t.count;
        const int cls = logic::sdU8(0x6b2d8a + std::uint32_t(u.data.u16(0xc)) * 0x40, ctx_.version);
        if (cls >= 1 && cls <= 5) t.classes = static_cast<std::uint8_t>(t.classes | 1u << (cls - 1));
    }
    if (event_.active || file_.empty()) return;
    for (int slot = 0; slot < TacticsUnits::kSlots; ++slot) {
        TacticsUnits::Unit& u = units_.unitRef(slot);
        if (!u.used || !u.onField()) continue;
        const tactics::Cell here{u.cx(), u.cy()};
        if (here == u.areaCell) continue;
        u.areaCell = here;
        const int a = areaAt(here.first, here.second);
        if (a != 0 && (areas_[std::size_t(std::min(a, 99))].teams >> std::min(u.team(), 15) & 1) != 0) {
            if (a == u.area) continue;
            entered_ = slot;                         // FUN_00497530
            if (trace()) SDL_Log("[script] unit %d enters area %d", slot, a);
            if (check(1, a)) startEvent(1, a, false, true, false, true);
        }
        u.area = static_cast<std::int8_t>(a);
        if (event_.active) return;
    }
}

// FUN_004e2760 / FUN_004e29e0 (FUN_004e2720 / FUN_004e29a0 pass -1 for
// the block, which is the battle's own).
std::int32_t StageScript::value(int type, std::uint32_t index) const {
    return battleIndexOk(type, index) ? readValue(ctx_.work, kBattle, type, index) : 0;
}

void StageScript::setValue(int type, std::uint32_t index, std::int32_t v) {
    if (trace() && value(type, index) != v) SDL_Log("[script] value %d/%u = %d", type, index, v);
    if (battleIndexOk(type, index)) writeValue(ctx_.work, kBattle, type, index, v);
}

// ---------------------------------------------------------------- camera ---
void StageScript::hold(YbcVm& vm) {
    if (checking_) return;                           // the check pass never waits
    vm.setHold(true);
    held_ = &vm;
}

void StageScript::release() {
    if (held_) held_->setHold(false);
    held_ = nullptr;
}

// FUN_00498010 (with FUN_004977d0, the camera's flag DAT_007e0f08).
void StageScript::scrollToCell(int cx, int cy) {
    if (cx < 0 || cy < 0 || cx >= map_.cellsW() || cy >= map_.cellsH()) { release(); return; }
    const int x = std::clamp(cx * 0x20 - 0x1f0, 0, std::max(0, map_.cellsW() * 0x20 - 0x400));
    const int y = std::clamp(cy * 0x10 - 0x115, 0, std::max(0, map_.cellsH() * 0x10 - 0x238));
    if (checking_ || (x >> 5 == map_.scrollX() >> 5 && y >> 4 == map_.scrollY() >> 4)) {
        if (checking_) map_.scrollTo(x, y);
        scrolling_ = false;
        release();
        return;
    }
    scrollTargetX_ = x;
    scrollTargetY_ = y;
    scrolling_ = true;
}

void StageScript::scrollToUnit(int slot) {
    if (slot < 0 || slot >= TacticsUnits::kSlots) { release(); return; }
    const TacticsUnits::Unit& u = units_.unit(slot);
    scrollToCell(u.cx(), u.cy());
}

void StageScript::dim(int frames, bool in) {
    frames = std::max(frames, 1);
    dimIn_ = in;
    dimValue_ = in ? 0 : kDimFull;
    dimStep_ = std::max<std::int32_t>(1, kDimFull / frames);
    dimming_ = !checking_;
    if (checking_) { dimValue_ = in ? kDimFull : 0; release(); }
}

// -------------------------------------------------------------- opcodes ---
void StageScript::exec(YbcVm& vm, std::uint16_t op, std::uint32_t rec) {
    const auto ev = [&](std::uint32_t off) { return vm.value(rec, off); };
    if (trace() && !checking_ && op >= 0x60) SDL_Log("[script] op %02x %d %d %d", op, ev(4), ev(8), ev(0xc));
    switch (op) {
        // The story values (0x5c..0x6b, 0x9e..0xa5) are the VM's own (ybc_vm.cpp).
        case 0x6c: hold(vm); break;                     // FUN_0042d1a0: four operands, then only the hold flag
        case 0x6f: case 0x70:                           // FUN_0042dc00 / FUN_0042dcf0 -> FUN_004987c0 -> FUN_0047fcf0
            units_.appearUnit(ev(4), ev(8), ev(0xc), 10);
            break;
        case 0x8d:                                      // FUN_0042f0a0 -> FUN_004992d0: 0 a hold, 1 a cut scene
            if (ev(4) == 0) units_.requestHold(1, false);
            else if (ev(4) == 1) units_.requestHold(2, true);
            break;
        // The rest call one battle function each (FUN_0042d460 0x71,
        // FUN_0042d520 0x72, FUN_0042d5e0 0x73, FUN_0042d870 0x74, FUN_0042d960
        // 0x75, FUN_0042d9f0 0x76, FUN_0042da80 0x77, FUN_0042dac0 0x78,
        // FUN_0042dbc0 0x79, FUN_0042dde0 0x7a, FUN_0042de60 0x7b, FUN_0042dee0
        // 0x7c, FUN_0042e010 0x7d, FUN_0042d6d0 0x7e, FUN_0042d7b0 0x7f,
        // FUN_0042e340 0x82, FUN_0042e570 0x84, FUN_0042e6c0 0x85, FUN_0042e9e0
        // 0x88, FUN_0042ebc0 0x89, FUN_0042ed90 0x8a, FUN_0042eef0 0x8b,
        // FUN_0042f020 0x8c, FUN_0042f120 0x8e, FUN_0042f1e0 0x8f, FUN_0042f2d0
        // 0x92, FUN_0042fbe0 0xa6, FUN_0042fc60 0xa7); 0x75, 0x76, 0x78 and
        // 0x8e also set the hold flag (+0x92e0).
        case 0x71: if (ev(4) >= 0 && ev(4) < 100) areas_[std::size_t(ev(4))].on = ev(8) != 0; break;
        case 0x72:                                      // FUN_00497350: renumber the areas
            if (ev(4) >= 0 && ev(4) < 100 && ev(8) >= 0 && ev(8) < 100 && ev(4) != ev(8))
                for (Area& a : areas_)
                    if (a.id == ev(4)) a.id = static_cast<std::uint8_t>(ev(8));
            break;
        case 0x73: map_.setCell(ev(4), ev(8), 2, static_cast<std::uint8_t>(ev(0xc))); break;   // FUN_00497460 -> FUN_0043bd00
        case 0x75: hold(vm); dim(ev(4), true); break;
        case 0x76: hold(vm); dim(ev(4), false); break;
        case 0x78: hold(vm); scrollToCell(ev(4), ev(8)); break;
        case 0x7c: case 0x7d: {                         // FUN_00498fc0 / FUN_00499050 -> FUN_00455420 / FUN_004558e0
            const int n = ev(4);
            if (n < 0 || n >= 16) break;
            Timer& t = (op == 0x7c ? timers_ : intervals_)[std::size_t(n)];
            t = Timer{true, ev(8), ev(0xc), std::max(ev(0x10), 1), std::max(ev(0x10), 1)};
            break;
        }
        case 0x7e: vm.store(rec, 4, map_.cell(ev(8), ev(0xc), 2)); break;   // FUN_004974b0 -> FUN_0043bb90
        case 0x74:                                      // FUN_00497160
            if (ev(4) == 0 || ev(4) == 1) {
                points_[std::size_t(ev(4) * 2)] = static_cast<std::int16_t>(ev(8));
                points_[std::size_t(ev(4) * 2 + 1)] = static_cast<std::int16_t>(ev(0xc));
            }
            break;
        case 0x7f:                                      // FUN_004970d0
            if (ev(4) == 0 || ev(4) == 1) marks_[std::size_t(ev(4))] = static_cast<std::int16_t>(ev(8));
            break;
        case 0x77: case 0x79: case 0x7a: case 0x7b: break;   // FUN_00497a40, FUN_00498280, FUN_00498920, FUN_004989b0: empty
        case 0x84: units_.setTeamGroupField(units_.viewer(), ev(4), ev(8), ev(0xc)); break;   // FUN_00498d30
        case 0x89: units_.playScriptEffect(ev(4), ev(8), ev(0xc), ev(0x10), ev(0x18), ev(0x1c)); break;   // FUN_00499370
        case 0x8a: units_.playScriptEffect(ev(4), ev(8) * 0x20 + 0x10, ev(0xc) * 0x10 + 8, ev(0x10), 0, 0); break;   // FUN_004993d0
        case 0x8b: units_.playScriptEffect(ev(4), 0, 0, ev(8), 2, ev(0xc)); break;   // FUN_00499430
        case 0x8c: units_.stopScriptEffect(ev(4)); break;   // FUN_004994e0
        case 0x8f: {                                    // FUN_00499110 -> FUN_00466410
            const int slot = ev(4);
            int x = 0, y = 0;
            if (slot >= 0 && slot < TacticsUnits::kSlots && units_.unit(slot).used) {
                x = units_.unit(slot).px();
                y = units_.unit(slot).py();
            }
            vm.store(rec, 4, x);
            vm.store(rec, 8, y);
            break;
        }
        case 0x82: {
            const int n = units_.appearGroup(ev(4), ev(8), ev(0xc), ev(0x10));
            if (trace()) SDL_Log("[script] group %d: %d units appear near %d,%d", ev(4), n, ev(8), ev(0xc));
            break;
        }
        case 0x85: units_.setGroupField(ev(4), ev(8), ev(0xc)); break;   // FUN_00498db0 -> FUN_0047f4b0
        case 0x88: sub88(vm, rec); break;
        case 0x8e: hold(vm); scrollToUnit(ev(4)); break;
        case 0x92:                                      // FUN_004997d0: queries
            switch (ev(8)) {
                case 0:                                 // FUN_00497560: the team of the unit that stepped in
                    vm.store(rec, 4, entered_ >= 0 ? units_.unit(entered_).team() : 0);
                    break;
                case 7: vm.store(rec, 4, entered_); break;   // FUN_00497500
                case 1: vm.store(rec, 4, 0xfa - ev(0xc)); break;   // FUN_00499160: a record's slot
                case 2: case 3: {                       // FUN_00496fd0 / FUN_00497040: a random number in [a, b]
                    const int lo = ev(0xc), span = ev(0x10) - lo + 1;
                    vm.store(rec, 4, span > 0 ? units_.random() % span + lo : lo);
                    break;
                }
                case 4: vm.store(rec, 4, entered_ >= 0 ? 0xfa - entered_ : 0); break;   // FUN_00497630
                case 5: { const int g = pickGroup(); vm.store(rec, 4, g == -1 ? 0xffff : g); break; }   // FUN_00499280
                case 6: {                               // FUN_00499010 -> FUN_004554c0: a free timer
                    int free = -1;
                    for (std::size_t i = 0; i < timers_.size() && free == -1; ++i) if (!timers_[i].used) free = int(i);
                    vm.store(rec, 4, free);
                    break;
                }
                case 8: vm.store(rec, 4, ctx_.work.s16(0x7f4488)); break;   // FUN_004970a0: the map
                default:
                    if (unported_.insert(std::uint16_t(0x9200 | (ev(8) & 0xff))).second)
                        SDL_Log("[script] opcode 0x92 query %d not ported yet", ev(8));
                    vm.store(rec, 4, 0);
                    break;
            }
            break;
        case 0xa6: found_ = ev(4) != 0; break;          // FUN_00496df0
        case 0xa7: vm.store(rec, 4, mode_ ? 1 : 0); break;   // FUN_00496dc0
        default:
            // The ADV's own opcodes do nothing in the check pass.
            if ((op >= 0x60 && op < 0xa8) && unported_.insert(op).second)
                SDL_Log("[script] battle opcode 0x%02x not ported yet", op);
            break;
    }
}

// FUN_00499560. Sub-commands that do not wait release the hold at once.
void StageScript::sub88(YbcVm& vm, std::uint32_t rec) {
    const auto ev = [&](std::uint32_t off) { return vm.value(rec, off); };
    hold(vm);
    switch (ev(4)) {
        case 0:                                          // FUN_004992d0: 0 a hold, 1 a cut scene
            if (ev(8) == 0) units_.requestHold(1, false);
            else if (ev(8) == 1) units_.requestHold(2, true);
            break;
        case 1: units_.playScriptEffect(ev(8), 0, 0, ev(0xc), 2, ev(0x10)); break;   // FUN_00499430
        case 2: units_.playScriptEffect(ev(8), ev(0xc) * 0x20 + 0x10, ev(0x10) * 0x10 + 8, ev(0x14), 0, 0); break;   // FUN_004993d0
        case 3: units_.playScriptEffect(ev(8), ev(0xc), ev(0x10), ev(0x14), 1, 0); break;   // FUN_00499480
        case 4: units_.stopScriptEffect(ev(8)); break;   // FUN_004994e0
        case 0x18: units_.withdraw(entered_); break;     // FUN_00498a40 -> FUN_004801e0
        case 0x19: units_.changeSide(ev(8), ev(0xc)); break;   // FUN_00498f30 -> FUN_004809a0
        case 0x1a:                                       // FUN_004990a0 -> FUN_00456340: waits for its end
            if (checking_) break;
            banner_ = Banner{true, 0, std::clamp(ev(0x18), 0, 11), ev(8), ev(0xc), ev(0x10) == 0 ? 100 : ev(0x10), 0, 0, 0};
            return;
        case 0x1b: raid(); break;                        // FUN_00499520
        case 0x22: resultMessage_ = static_cast<std::uint8_t>(ev(8)); break;   // FUN_004976e0 -> FUN_004567b0
        case 10: scrollToUnit(ev(8)); return;            // FUN_00497df0
        case 0x14:                                       // FUN_004972b0: the teams an area reacts to
            if (ev(8) >= 0 && ev(8) < 100) areas_[std::size_t(ev(8))].teams = static_cast<std::uint16_t>(ev(0xc));
            break;
        case 0x1c: case 0x1d:                            // FUN_00498c40 / FUN_00498c90 -> FUN_00498be0: an appear timer
            if (ev(0xc) >= 0 && ev(0xc) < int(appears_.size())) {
                Appear& a = appears_[std::size_t(ev(0xc))];
                a = Appear{};
                if (ev(8) > 0) {                         // 0 frames clears it (FUN_00455cb0)
                    a = Appear{true, ev(4) == 0x1d, ev(0x10), ev(0x14), ev(0x18), ev(4) == 0x1d ? 10 : ev(0x1c), ev(8)};
                    if (trace()) SDL_Log("[script] appear timer %d: %s %d in %d frames", ev(0xc), a.single ? "unit" : "group", a.who, a.left);
                }
            }
            break;
        case 0x1e:                                       // FUN_00498ce0: clear an appear timer
            if (ev(8) >= 0 && ev(8) < int(appears_.size())) appears_[std::size_t(ev(8))] = Appear{};
            break;
        case 0x1f: groupPool_.fill(-1); break;           // FUN_00499200 -> FUN_004567f0
        case 0x20:                                       // FUN_00499240 -> FUN_00456960: a group into the pool's first free place
            for (std::int16_t& g : groupPool_) if (g == -1) { g = static_cast<std::int16_t>(ev(8)); break; }
            break;
        case 0x21: units_.reviveGroup(ev(8)); break;     // FUN_00498e60 -> FUN_0047f9d0
        case 0x15:                                       // FUN_00497d10: remember the view's centre
            savedX_ = std::min(map_.scrollX() + 0x200, map_.cellsW() * 0x20);
            savedY_ = std::min(map_.scrollY() + 0x11d, map_.cellsH() * 0x10);
            break;
        case 0x16: scrollToCell(savedX_ >> 5, savedY_ >> 4); return;   // FUN_00497f90
        case 0x17: scrollToUnit(entered_); return;       // FUN_00497ec0
        default: {
            const std::uint16_t key = static_cast<std::uint16_t>(0x8800 | (ev(4) & 0xff));
            if (unported_.insert(key).second) SDL_Log("[script] opcode 0x88 sub-command %d not ported yet", ev(4));
            break;
        }
    }
    release();
}

} // namespace ch
