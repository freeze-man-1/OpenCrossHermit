// ============================================================================
// tactics_task.cpp
// ============================================================================
#include "tactics_task.hpp"
#include "tactics_hud.hpp"
#include "../preparation/preparation_task.hpp"
#include "../net/net_game.hpp"
#include "../text/sjis.hpp"
#include "../ysub/ysub.hpp"
#include "../state/game_logic.hpp"
#include "../option/options.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace ch {

namespace {
constexpr int kMapPrio = 0xf00;   // FUN_0043fd20(0xf00)
constexpr int kMenuButtonW = 100; // the MENU button's end of the top bar (common.bin frame 0)

// FUN_00452cf0: the trial maps (0x4c..0x50, 0x62), when not chained on,
// bring a fixed party (0xb0-byte records) instead of the work's: those
// not out of action (+0xa4 below 4) are counted, copied to 0x7f4518 and
// set five to a row from the start cell (+0x9b / +0x9c). True when it did.
bool fixedParty(GameWork& w, GameVersion v) {
    const int map = w.s16(stage::kMap);
    if (w.u8(stage::kChained) != 0 || map < 0x4c || (map > 0x50 && map != 0x62)) return false;
    std::uint32_t table;
    int count;
    switch (map - 0x4c) {
        case 0:  table = 0x606948; count = 5; break;
        case 1:  table = 0x606cb8; count = 8; break;
        case 2:  table = 0x607238; count = 3; break;
        case 3:  table = 0x607448; count = 4; break;
        case 4:  table = 0x607708; count = 1; break;   // the tutorial
        default: table = 0x607d38; count = 0xc; w.set8(stage::kPartyOut, 0); break;
    }
    int n = 0;
    for (int k = 0; k < count; ++k)
        if (logic::sdU8(table + std::uint32_t(k) * 0xb0 + 0xa4, v) < 4) ++n;
    w.set8(stage::kPartyCount, std::uint8_t(n));
    w.set8(0x7f448e, 0);
    w.set32(0x7f4508, 0);
    for (int k = 0; k < n; ++k) {
        const std::uint32_t u = stage::kParty + std::uint32_t(k) * 0xb0;
        w.copy(u, logic::sdBytes(table + std::uint32_t(k) * 0xb0, 0xb0, v));
        if (w.u8(u + 0xa4) < 4) {
            w.set8(u + 0x9b, std::uint8_t(w.s16(stage::kStartX) + k % 5));
            w.set8(u + 0x9c, std::uint8_t(w.s16(stage::kStartY) + k / 5));
        }
    }
    return true;
}
}

// FUN_00451470 (FUN_00451630 the deleting form): the task's banks and
// sound groups go (FUN_0040d120: group 1 and 7..19), the units' work
// (FUN_00465e60) with the units.
TacticsTask::~TacticsTask() {
    ctx_.sprites->unloadBank(kCommonBank);
    ctx_.audio->seLoadGroup(1, {});
    for (std::uint32_t g = 7; g < 7 + 13; ++g) ctx_.audio->seLoadGroup(g, {});
}

// FUN_00458ff0: SE group 1, the units' sounds (PTR_..._0060cb40, up to
// the first empty path). FUN_00459080: each of the twelve characters' nine
// lines, "data\\Sound\\plw\\UnitVoice\\tv%03d%s" with the suffixes
// PTR_s__0007_wav_0060cd04, into SE group character + 6.
void TacticsTask::loadSounds() {
    const GameVersion v = ctx_.version;
    const auto text = [&](std::uint32_t va) {
        std::string out;
        for (std::uint32_t a = va; a != 0; ++a) {
            const std::uint8_t c = logic::sdU8(a, v);
            if (c == 0) break;
            out.push_back(char(c));
        }
        return out;
    };
    std::vector<std::string> paths;
    for (std::uint32_t i = 0; i < 0x100; ++i) {
        std::string p = text(std::uint32_t(logic::sdS32(0x60cb40 + i * 4, v)));
        if (p.empty()) break;
        paths.push_back(std::move(p));
    }
    std::vector<std::string_view> views(paths.begin(), paths.end());
    ctx_.audio->seLoadGroup(1, views);
    for (int ch = 1; ch <= 12; ++ch) {
        std::vector<std::string> lines;
        for (std::uint32_t i = 0; i < 9; ++i) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "data\\Sound\\plw\\UnitVoice\\tv%03d%s", ch,
                          text(std::uint32_t(logic::sdS32(0x60cd04 + i * 4, v))).c_str());
            lines.emplace_back(buf);
        }
        std::vector<std::string_view> lv(lines.begin(), lines.end());
        ctx_.audio->seLoadGroup(std::uint32_t(ch + 6), lv);
    }
}

Co<> TacticsTask::start(std::uint32_t) {
    GameWork& w = ctx_.work;
    // The task's steps (+0x34), which this coroutine stands in for:
    // FUN_004519c0 runs the step's setup and FUN_00451a60 its frame: 0
    // FUN_00451f50 / FUN_004538d0 (on to 2, or 1 in network play), 1
    // FUN_00452f90 (nothing) / the network handshake (the player number),
    // 2 FUN_00452fc0 / FUN_004539b0 (the battle).
    // FUN_00451e20: the loading caption, in the colour FUN_00451d40 sets
    // (white) and FUN_00451d80 dims a step each time loading moves on.
    ctx_.draw.text({.x = 0x1b8, .y = 0x17c, .priority = 1, .argb = 0xffffffff, .text = "Tactics Scene", .fontPx = 16});
    ctx_.draw.text({.x = 0x1b8, .y = 400, .priority = 1, .argb = 0xffffffff, .text = "Now Loading....", .fontPx = 16});
    co_await yieldFrame();
    mapId_ = w.s16(stage::kMap);
    if (const char* m = SDL_getenv("CH_MAP")) {      // debug: fight another map
        // The stage table row (0x6ad1d0) holds the map number, its start
        // cell and its time limits; the party stays the save's.
        const std::uint8_t party = w.u8(stage::kPartyCount);
        PreparationTask::loadStage(w, ctx_.version, static_cast<int>(std::strtol(m, nullptr, 0)), false);
        w.set8(stage::kPartyCount, party);
        // Debug: CH_PARTY=<n> brings the save's own first n party members
        // (its groups first, as the network game's roster) instead.
        if (const char* p = SDL_getenv("CH_PARTY")) {
            const auto roster = netgame::saveRoster(w, ctx_.version);
            const int n = std::min<int>(std::atoi(p), std::min<int>(int(roster.size()), 20));
            for (int i = 0; i < n; ++i)
                for (std::uint32_t b = 0; b < netgame::kRecord; ++b)
                    w.set8(stage::kParty + std::uint32_t(i) * 0xb0 + b, roster[std::size_t(i)][b]);
            w.set8(stage::kPartyCount, std::uint8_t(n));
        }
        mapId_ = w.s16(stage::kMap);
    }
    if (!map_.load(ctx_, mapId_)) {
        finish(2);
        co_return;
    }
    ctx_.sprites->loadBank(kCommonBank, ctx_.readData("data\\Tactics\\common.bin"));
    loadSounds();                                    // FUN_00451b10's FUN_00458ff0 / FUN_00459080
    // Network play: we see the battle as our slot's team, every player's
    // units come in (net/net_game.cpp wrote them), and our orders go to the
    // relay instead of to the units (net/lockstep.hpp).
    net_ = ctx_.net.state() == net::Session::State::InGame && ctx_.net.lockstep.running() ? &ctx_.net : nullptr;
    if (net_) {
        units_.setNetwork(w.u8(stage::kNetwork) != 0);
        units_.setNetSink([this](const net::Command& c) { net_->lockstep.issue(c); });
        netSeen_ = net_->chatCount();                // the room's lines stay in the room
        std::uint8_t present = 0;
        for (const auto& m : net_->room().members) if (m.slot >= 0 && m.slot < 4) present = std::uint8_t(present | 1u << m.slot);
        script_.setNetGate(present, [this](std::uint32_t serial) {
            net_->lockstep.issue({.type = net::kCmdEventDone, .unit = -1, .extra = std::int16_t(serial)});
        });
        units_.setViewer(net_->room().you);          // FUN_00453930: our slot (+0x2f0d4)
    }
    // FUN_00452040: the trial maps' fixed party, else the work's (clamped
    // and its places cleared in build()).
    const bool fixed = !net_ && fixedParty(w, ctx_.version);
    units_.build(map_, mapId_, fixed);
    blinds_ = 1;                                     // FUN_00453f10 case 0: the blinds open
    blindsT_ = -0x400;
    script_.load(mapId_);
    // FUN_00451f50 (phase 0): the music the story left playing gives way to
    // one of the stage's three battle tracks (DAT_007f44bb).
    ctx_.audio->bgmPlay(w.u8(stage::kBgm + std::uint32_t(units_.random() % 3)), 1);   // FUN_004db010
    clock_.start(w.s32(stage::kTimeLimit));                   // FUN_00467aa0(DAT_007f44a8)
    // Debug: CH_TIME_LIMIT=<frames> shortens the stage's time limit.
    if (const char* tl = SDL_getenv("CH_TIME_LIMIT")) clock_.start(std::max(std::atoi(tl), 1));
    SDL_Log("[tactics] map %d: %d x %d cells", mapId_, map_.cellsW(), map_.cellsH());
    // FUN_00452fc0 -> FUN_00499c90: the sortie's record. The stage row's +4
    // (DAT_007f448f) lets the player choose who goes; the start cell, its
    // range (+0xf) and the most / fewest who may go (+6 / +5).
    sortie_ = {};
    sortie_.on = w.u8(stage::kSortie) != 0;
    sortie_.sx = w.s16(stage::kStartX);
    sortie_.sy = w.s16(stage::kStartY);
    if (const char* at = SDL_getenv("CH_START"))     // debug: the sortie's start cell "x,y"
        std::sscanf(at, "%d,%d", &sortie_.sx, &sortie_.sy);
    sortie_.range = w.u8(stage::kStartRange);
    sortie_.most = w.u8(stage::kMost);
    sortie_.fewest = w.u8(stage::kFewest);
    units_.setFrozen(true);                          // +0x108b34 0 until case 4
    units_.setOpening(true);                         // +0x108b48 (FUN_00499c90)
    SDL_Log("[tactics] sortie %s: start %d,%d range %d, %d..%d units", sortie_.on ? "on" : "off", sortie_.sx, sortie_.sy,
            sortie_.range, sortie_.fewest, sortie_.most);
    // FUN_00499da0, once a frame until the battle starts (case 0xd: the
    // battle loop below, which also starts the timers, FUN_00455160).
    // Cases 7 and 0xa wait for the opening event's two states: its script
    // done (FUN_00454c40 2), then, after the start banner lets it go
    // (FUN_00454c80), its end (0).
    // In network play these frames already run on the relay's turns, as
    // FUN_004539b0 syncs every frame from the first: the opening's dialog
    // ends for all when every player's report (0x42) is in, and the battle
    // starts on the same frame everywhere.
    const auto netTurn = [&] {
        if (const net::Lockstep::Turn* t = net_->lockstep.beginFrame()) {
            for (const auto& [slot, c] : t->commands) {
                if (c.type == net::kCmdEventDone) script_.netEventDone(slot, std::uint32_t(std::uint16_t(c.extra)));   // a dialog over
                else if (c.type == net::kCmdPlayerLeft) script_.dropPlayer(slot);
                else units_.applyNet(slot, c);
            }
            units_.endNetTurn();
        }
    };
    const auto netEndFrame = [&] {
        net_->lockstep.endFrame([&](std::uint32_t& seed, std::uint32_t& rolls, std::uint32_t& hash, std::uint8_t& refused) {
            seed = units_.checkSeed();
            rolls = units_.checkRolls();
            hash = units_.stateHash();
            refused = units_.takeRefused();
            // Debug: CH_NET_DUMP logs every unit at each check, to find where two sides part.
            static const bool dump = SDL_getenv("CH_NET_DUMP") != nullptr;
            if (dump)
                for (int i = 0; i < TacticsUnits::kSlots; ++i)
                    if (const auto& u = units_.unit(i); u.used)
                        SDL_Log("[netdump] f%lld u%d %d,%d s%d/%d f%d hp%d t%d", (long long)net_->lockstep.frame(), i, u.x, u.y,
                                u.state, u.sub, u.data.u8(0xf), u.data.u16(0x16), u.team());
        });
    };
    if (net_) net_->loaded();                        // the relay's clock may start
    bool partyOut = false;
    const auto sortieFrame = [&] {
        switch (sortie_.phase) {
            case kSortieCamera:               // the camera on the start (FUN_0043b400)
                map_.centreOn(sortie_.sx * TacticsMap::kCellW + TacticsMap::kCellW / 2,
                              sortie_.sy * TacticsMap::kCellH + TacticsMap::kCellH / 2);
                if (!sortie_.on) {
                    sortie_.phase = kSortieFree;
                } else if (sortie_.wait != 0) {
                    --sortie_.wait;
                } else if (sortie_.shown == sortie_.range) {
                    ++sortie_.phase;
                } else {
                    ++sortie_.shown;                 // the lit start area grows (FUN_0049b9f0)
                    sortie_.wait = 5;
                }
                break;
            case kSortieCards:                // the cards and the Sortie button
                units_.setCards(true);
                scroll();
                sortieMouse();
                break;
            case kSortieChosen:
                if (sortie_.wait != 0) {
                    --sortie_.wait;
                } else {
                    sortie_.button = 0;              // +0x108b47
                    ++sortie_.phase;
                }
                break;
            case kSortieDeploy:               // FUN_0049a310
                units_.setCards(false);
                units_.deploySelected(sortie_.sx, sortie_.sy, sortie_.range);
                partyOut = true;
                ++sortie_.phase;
                break;
            case kSortieFree:                 // three seconds of the units acting (+0x108b34 1)
                units_.setFrozen(false);
                ++sortie_.phase;
                sortie_.wait = 0xb4;
                break;
            case kSortieFreeWait:
                if (sortie_.wait != 0) {
                    --sortie_.wait;
                } else {
                    units_.setFrozen(true);
                    ++sortie_.phase;
                }
                break;
            case kSortieOpening:              // the opening event (FUN_004551c0 / FUN_004549d0)
                // (A versus map in network play has none: FUN_00456b20 / FUN_004daae0.)
                // It undims (5th 1) and holds nobody (7th 0): the units stay
                // frozen from case 5 until the battle starts, and even an
                // event with no script lasts until the start banner lets it go.
                if (net_ && w.u8(stage::kRoundFlag) == 0) script_.startEvent(0, 0, false, false, true, false);
                else if (script_.check(0, 1)) script_.startEvent(0, 1, false, true, true, false);
                else script_.startEvent(0, 0, false, false, true, false);
                ++sortie_.phase;
                break;
            case kSortieOpeningScript:        // its script done (2), or no event left
                if (script_.eventState() != 1) ++sortie_.phase;
                break;
            case kSortieBanner1:              // the start banner when the stage has one (DAT_007f44b9)
                ++sortie_.phase;
                if (w.u8(stage::kBanner1) != 0) {
                    startBanner(0);
                } else {
                    script_.releaseEvent();          // FUN_00454c80
                    ++sortie_.phase;
                }
                break;
            case kSortieBanner1Wait:
                if (tickBanner()) {
                    script_.releaseEvent();          // FUN_00454c80
                    ++sortie_.phase;
                }
                break;
            case kSortieOpeningEnd:           // the event's undim and end
                if (script_.eventState() == 0) ++sortie_.phase;
                break;
            case kSortieBanner2:              // the second banner (DAT_007f44ba)
                ++sortie_.phase;
                if (w.u8(stage::kBanner2) != 0) startBanner(1);
                else ++sortie_.phase;
                break;
            case kSortieBanner2Wait:
                if (tickBanner()) ++sortie_.phase;
                break;
            default: break;
        }
        // Stand-in: a stage without the cards places its party from its
        // opening event, or the player calls them in through a unit of
        // theirs on the field (a tower, FUN_004812c0). Only should nothing
        // of ours stand on the field once the opening is over (nobody could
        // call), the party goes out in order up to the stage's limit.
        // (Not in network play: it asks after the viewer's units, which
        // differ between the players, and would place a party on one side only.)
        if (sortie_.phase == kSortieBanner1 && !partyOut && !net_) {
            partyOut = true;
            if (!units_.anyOwnOnField()) {
                SDL_Log("[tactics] map %d: the opening placed no party member; the party goes out in order", mapId_);
                units_.deployParty(units_.partyCount());
            }
        }
        map_.fogFrame();
        units_.stampSight(map_);
        if (sortie_.on && sortie_.phase <= kSortieDeploy) map_.see(sortie_.sx, sortie_.sy, sortie_.shown);   // FUN_0049b9f0
        units_.update();
        script_.update();
    };
    while (sortie_.phase < kSortieDone) {
        if (!net_) {
            sortieFrame();
        } else {
            for (int n = net_->lockstep.framesToRun(); n > 0 && sortie_.phase < kSortieDone; --n) {
                netTurn();
                sortieFrame();
                netEndFrame();
            }
        }
        draw();
        co_await yieldFrame();
    }
    units_.setOpening(false);                        // FUN_00499da0 case 0xd
    units_.setFrozen(false);
    bool started = false;
    // Debug: CH_PERF logs the time each frame's work takes, averaged.
    const bool perf = SDL_getenv("CH_PERF") != nullptr;
    std::uint64_t perfStart = SDL_GetPerformanceCounter(), perfTicks = 0;
    int perfFrames = 0;
    for (;;) {
        // The port's wide view, while the battle is the player's to play.
        wide_.frame(menu_ != 2 && !script_.eventRunning() && units_.inputOpen() && !netChatOpen_ && !net_ && blinds_ == 0, menu_ == 1);
        // FUN_004539b0: with the menu or the objectives up (+0x30) nothing
        // runs but FUN_00453d00 and the drawing.
        if (menu_ != 0) {
            const int end = menuStep();
            draw();
            if (end != 0) break;
            co_await yieldFrame();
            continue;
        }
        // The key that closes an event's last line must not also win the
        // battle on the same frame.
        const bool eventAtStart = script_.eventRunning();
        if (!script_.eventRunning()) {
            if (!started) {
                started = true;
                units_.setFrozen(false);
                script_.enableTimers();
                if (net_ && SDL_getenv("CH_NET_TRACE"))
                    SDL_Log("[net] the battle starts on frame %lld (hash %08x)", (long long)net_->lockstep.frame(), units_.stateHash());
            }
            if (net_) netTalk();                     // FUN_00457890 / FUN_00457aa0 / FUN_00458180
            if (units_.inputOpen() && !netChatOpen_) {   // +0x108b36 / 38 (FUN_004678c0)
                scroll();
                // FUN_0046b470: FUN_0049b180 (the command bar) runs before
                // FUN_0046ea10 / FUN_0046f440 (the map) and keeps a click
                // it takes from them (+0x2f150).
                // FUN_0046e9c0 -> FUN_00481610 first: the call drag.
                const bool calling = units_.callDrag(barInput());
                if (!keys() && !units_.commandBar(barInput()) && !calling) mouse();
            }
        }
        {
            const auto& m = ctx_.mouse();
            if (m.pressed & legacy::kMouseLeft) {
                pressX_ = m.x;
                pressY_ = m.y;
            }
        }
        if (started && !eventAtStart && !openMenu()) openObjectives();   // FUN_0046b470 -> FUN_00458580; FUN_00453e10 -> FUN_00458be0
        // One battle frame. In network play only the frames of turns the
        // relay has sent may run (none while we wait, two to catch up), each
        // turn's commands at its first frame, every player alike.
        bool over = false;
        const auto frame = [&] {
            if (const int t = units_.timersWanted(); t >= 0 && started) script_.setTimers(t != 0);   // FUN_00455160
            if (started && !script_.eventRunning()) clock_.tick();   // FUN_00467b60
            map_.fogFrame();                         // FUN_0043b350 -> FUN_0043d230
            units_.stampSight(map_);
            units_.update();
            script_.update();
            script_.checkAreas();
            if (script_.battleOver()) { over = true; return; }
            if (started && !script_.eventRunning()) {
                // The port's option (or CH_DEBUG / CH_WINKEY): Confirm wins
                // the stage at once (the original has no such key).
                static const bool winEnv = SDL_getenv("CH_WINKEY") != nullptr || SDL_getenv("CH_DEBUG") != nullptr;
                const bool winKey = winEnv || options::port().instantWin;
                if (winKey && !net_ && !eventAtStart && ctx_.engine.input().pressed(legacy::Action::Confirm)) {
                    rank_ = 2;
                    over = true;
                    return;
                }
                if (!checks_.run(mapId_)) {
                    // Stand-in for the maps whose checks are not ported: the
                    // field is cleared of one side.
                    if (units_.standing(false) == 0) {
                        SDL_Log("[tactics] no enemies left standing");
                        rank_ = 2;
                        over = true;
                    } else if (units_.standing(true) == 0) {
                        SDL_Log("[tactics] the party has fallen");
                        rank_ = 5;
                        over = true;
                    }
                }
            }
        };
        if (!net_) {
            // Debug: CH_ORDER=<target slot>@<battle frame>: every unit of ours
            // takes the right-click order on that unit then.
            static const char* order = SDL_getenv("CH_ORDER");
            static const char* hp = SDL_getenv("CH_HP");
            static const char* wipe = SDL_getenv("CH_WIPE");
            static const char* move = SDL_getenv("CH_MOVE");
            static int battleFrame = 0;
            if ((order || hp || wipe || move) && started && ++battleFrame) {
                // CH_MOVE=<x>,<y>@<battle frame>: every unit of ours walks to that cell then.
                if (int mx = -1, my = -1, mat = -1; move && std::sscanf(move, "%d,%d@%d", &mx, &my, &mat) == 3 && battleFrame == mat) {
                    units_.clearSelection();
                    for (int i = 0; i < TacticsUnits::kSlots; ++i)
                        if (units_.unit(i).used && units_.unit(i).team() == units_.viewer()) units_.selectSlot(i, true);
                    SDL_Log("[tactics] debug move: %d units to %d,%d", units_.selectedCount(), mx, my);
                    units_.orderAt(mx, my, -1);
                }
                // CH_WIPE=<team>@<battle frame>[,...]: that team's units on the field to 0 HP then.
                for (const char* p = wipe; p && *p;) {
                    int team = -1, at = -1;
                    if (std::sscanf(p, "%d@%d", &team, &at) == 2 && battleFrame == at) units_.debugWipe(team);
                    p = std::strchr(p, ',');
                    if (p) ++p;
                }
                int target = -1, at = -1;
                if (order && std::sscanf(order, "%d@%d", &target, &at) == 2 && battleFrame == at) {
                    units_.clearSelection();
                    for (int i = 0; i < TacticsUnits::kSlots; ++i)
                        if (units_.unit(i).used && units_.unit(i).team() == units_.viewer()) units_.selectSlot(i, true);
                    const auto& t = units_.unit(target);
                    SDL_Log("[tactics] debug order: %d units at unit %d", units_.selectedCount(), target);
                    if (!units_.orderCall(target)) units_.orderAt(t.cx(), t.cy(), target);
                }
                // CH_HP=<slot>=<hp>@<battle frame>: that unit's HP then.
                int slot = -1, value = 0;
                if (hp && std::sscanf(hp, "%d=%d@%d", &slot, &value, &at) == 3 && battleFrame == at) units_.debugHp(slot, value);
            }
            frame();
            // Debug: CH_HASH=<n> logs the units' state every n battle frames (the network check's hash),
            // to compare two runs of the same battle.
            static const int hashEvery = [] { const char* v = SDL_getenv("CH_HASH"); return v ? std::atoi(v) : 0; }();
            static int hashFrame = 0;
            if (hashEvery > 0 && started && ++hashFrame % hashEvery == 0)
                SDL_Log("[hash] f%d %08x seed %08x", hashFrame, units_.stateHash(), units_.checkSeed());
        } else {
            for (int n = net_->lockstep.framesToRun(); n > 0 && !over; --n) {
                netTurn();
                // Debug: CH_NET_WIPE=<team>@<frame> drops that team's units to 0 HP
                // on that battle frame, on every machine alike.
                static const char* wipe = SDL_getenv("CH_NET_WIPE");
                if (wipe) {
                    int team = -1;
                    long long at = -1;
                    if (std::sscanf(wipe, "%d@%lld", &team, &at) == 2 && net_->lockstep.frame() == at) units_.debugWipe(team);
                }
                frame();
                netEndFrame();
            }
        }
        draw();
        if (over) break;
        if (perf) {
            const std::uint64_t now = SDL_GetPerformanceCounter();
            perfTicks += now - perfStart;
            // A single slow frame (a stall the average would hide).
            if (const double ms = double(now - perfStart) * 1000.0 / double(SDL_GetPerformanceFrequency()); ms > 50.0)
                SDL_Log("[perf] slow tactics frame: %.1f ms", ms);
            if (++perfFrames == 300) {
                SDL_Log("[perf] tactics: %.2f ms per frame (logic + draw lists)",
                        double(perfTicks) * 1000.0 / double(SDL_GetPerformanceFrequency()) / perfFrames);
                perfTicks = 0;
                perfFrames = 0;
            }
        }
        co_await yieldFrame();
        perfStart = SDL_GetPerformanceCounter();
    }
    if (!quit_) {
        // FUN_00453f10 cases 0xf..0x13 once the battle is decided: 64 frames
        // (DAT_007a4184 up by 4), the result's banner by rank (1 -> 2 ..
        // 5 -> 6; stage 0x2d banner 0xc) setting DAT_005ff720 (0 a win,
        // 1 the middle ranks, 2 a loss), then the blinds close.
        const int r = rank_ > 0 ? rank_ : checks_.rank() >= 0 ? checks_.rank() : 2;
        if (tactics::traceUnits()) SDL_Log("[tactics] the battle ends: rank %d", r);
        for (int i = 0; i < 0x40; ++i) {
            draw();
            co_await yieldFrame();
        }
        if (r >= 1 && r <= 5) {
            static constexpr int kComing[6] = {0, 0, 1, 1, 2, 2};
            startBanner(w.s16(stage::kMap) == 0x2d ? 0xc : r + 1);
            ctx_.comingResult = kComing[r];
            while (!tickBanner()) {
                draw();
                co_await yieldFrame();
            }
        }
        blinds_ = 2;
        blindsT_ = 0x4ff;
        while (blinds_ != 0) {
            draw();
            co_await yieldFrame();
        }
    }
    // FUN_00451670: ten frames of "Please Wait....." (FUN_00451f00), the
    // sounds stopped, then the music faded.
    for (int i = 0; i < 10; ++i) {
        ctx_.draw.text({.x = 0x1b8, .y = 0x17c, .priority = 1, .argb = 0xffffffff, .text = "Please Wait.....", .fontPx = 16});
        co_await yieldFrame();
    }
    ctx_.audio->seStopAll();
    ctx_.audio->bgmFadeOut(0x3c);                    // FUN_00451670: FUN_004db230(0x3c)
    while (!ctx_.audio->bgmFadeFinished()) co_await yieldFrame();
    if (quit_) {                                     // FUN_00451670: +0x31 -> the title (task 1)
        SDL_Log("[tactics] the battle was given up");
        if (net_) ctx_.net.leaveRoom();               // FUN_00419240 / FUN_00417cc0: out of the session
        ctx_.scenes.request(SceneId::Title);
        co_return;
    }
    // FUN_00451670 in network play: back to the room (task 0xf); a
    // co-operative map (FUN_004daae0: DAT_007f4504) first hands our
    // survivors back (FUN_00473dc0) and shows the stage's result (task 0xb),
    // which goes on to the week's result and, for a save's party, the save
    // screen and then the room: what they found and learnt stays theirs.
    if (net_) {
        net_->gameOver();
        if (w.u8(stage::kRoundFlag) != 0) {
            coopHandBack();
            ctx_.scenes.request(SceneId::TacticsResult);
        } else {
            ctx_.scenes.request(SceneId::NetJoin);
        }
        co_return;
    }
    finish(rank_ > 0 ? rank_ : checks_.rank() >= 0 ? checks_.rank() : 2);
}

// FUN_0043b350 -> FUN_0043c700: with no button held, the pointer on the
// screen's edge (FUN_0043c920) scrolls 2 x 8 px a frame (FUN_00415040
// passes the step through), clamped to the map. The arrow keys are the
// port's.
void TacticsTask::scroll() {
    const auto& in = ctx_.engine.input();
    const auto& m = ctx_.mouse();
    const int edge = wide_.on() ? wide_.edges() : m.held == 0 && (wide_.mouseLive() || ctx_.engine.pointerIsTouch()) ? TacticsMap::edges(m.x, m.y, map_.margin()) : 0;
    int dx = 0, dy = 0;
    if (in.held(legacy::Action::Left) || (edge & 4)) dx -= 0x10;
    if (in.held(legacy::Action::Right) || (edge & 8)) dx += 0x10;
    if (in.held(legacy::Action::Up) || (edge & 1)) dy -= 0x10;
    if (in.held(legacy::Action::Down) || (edge & 2)) dy += 0x10;
    if (dx != 0 || dy != 0) map_.scrollTo(map_.viewX() + dx, map_.scrollY() + dy);
}

// FUN_004128f0(key, 1): pressed this frame (SDL scancodes for the
// DirectInput codes).
bool TacticsTask::keyHit(int scancode) {
    int n = 0;
    const std::uint8_t* now = SDL_GetKeyboardState(&n);
    if (scancode < 0 || scancode >= n || scancode >= int(keysWere_.size())) return false;
    return now[scancode] != 0 && keysWere_[std::size_t(scancode)] == 0;
}

// FUN_00494690: the first of the ten groups whose key (DAT_00619280: the
// keypad digit, then the row digit) was pressed, else -1.
int TacticsTask::groupKey() {
    static constexpr int kPad[10] = {SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3,
                                     SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7,
                                     SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9};
    static constexpr int kRow[10] = {SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
                                     SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9};
    for (int g = 0; g < 10; ++g)
        if (keyHit(kPad[g]) || keyHit(kRow[g])) return g;
    return -1;
}

// FUN_0046ea10's keys, before the mouse: a number (FUN_004945b0) picks
// its group (FUN_004942b0), Ctrl + A (FUN_004947b0 -> FUN_00494820)
// every unit of ours in view (FUN_004948f0), Ctrl + a number
// (FUN_00494620) stores the picked units as that group (FUN_00494030).
// True when a key took the frame.
bool TacticsTask::keys() {
    const bool ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
    const int g = groupKey();
    const bool all = ctrl && keyHit(SDL_SCANCODE_A);
    fkey_ = -1;
    for (int i = 0; i < 8 && fkey_ < 0; ++i)
        if (keyHit(SDL_SCANCODE_F1 + i)) fkey_ = i;
    // The port's keys for the left hand (W A S D move the view): Q E R T and
    // F G V B the eight skills, C the next stance.
    for (int i = 0; i < 8 && fkey_ < 0 && !ctrl; ++i)
        if (keyHit(TacticsWide::kSkillKeys[i])) fkey_ = i;
    stanceKey_ = !ctrl && keyHit(SDL_SCANCODE_C);
    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
    shiftUp_ = shiftWas_ && !shift;
    shiftWas_ = shift;
    {
        int n = 0;
        const std::uint8_t* now = SDL_GetKeyboardState(&n);
        for (int i = 0; i < n && i < int(keysWere_.size()); ++i) keysWere_[std::size_t(i)] = now[i];
    }
    if (!ctrl && g >= 0) {
        int centre = -1;
        if (units_.recallGroup(g, centre)) {
            if (centre >= 0) map_.centreOn(units_.unit(centre).px(), units_.unit(centre).py());
            return true;
        }
    }
    if (all) {
        units_.selectOnScreen();
        return true;
    }
    if (ctrl && g >= 0) units_.storeGroup(g);
    return false;
}

TacticsUnits::BarInput TacticsTask::barInput() const {
    const auto& m = ctx_.mouse();
    return {.x = m.x, .y = m.y, .pressed = (m.pressed & legacy::kMouseLeft) != 0,
            .held = (m.held & legacy::kMouseLeft) != 0, .released = (m.released & legacy::kMouseLeft) != 0,
            .dragging = dragging_ && dragged_, .shift = (SDL_GetModState() & KMOD_SHIFT) != 0, .shiftUp = shiftUp_,
            .fkey = fkey_, .stance = stanceKey_ ? units_.nextStance() : -1, .sortie = sortie_.phase < kSortieDone, .faces = wide_.onHud()};
}

void TacticsTask::mouse() {
    const auto& m = ctx_.mouse();
    if (clickAge_ < 1000) ++clickAge_;
    // FUN_0046f440 -> FUN_00493c40: with the flare toggle on, a click on the
    // map (or the minimap) sends a flare there and turns the toggle off.
    if (net_ && netFlareMode_ && (m.pressed & legacy::kMouseLeft)) {
        int px = -1, py = -1, cx = 0, cy = 0;
        if (map_.hasMinimap() && m.x >= TacticsMap::kMiniX && m.y >= TacticsMap::kMiniY &&
            m.x < TacticsMap::kMiniX + map_.miniW() && m.y < TacticsMap::kMiniY + map_.miniH()) {
            px = (m.x - TacticsMap::kMiniX) * map_.cellsW() * TacticsMap::kCellW / map_.miniW();
            py = (m.y - TacticsMap::kMiniY) * map_.cellsH() * TacticsMap::kCellH / map_.miniH();
        } else if (map_.cellAt(m.x, m.y, cx, cy)) {
            px = m.x + map_.viewX();
            py = m.y - TacticsMap::kViewY + map_.scrollY();
        }
        if (px >= 0) {
            sendFlare(px, py);
            netFlareMode_ = false;                   // FUN_00458130(0)
            return;
        }
    }
    // FUN_004406f0 -> FUN_00440980: pressing on the minimap centres the view
    // on that point (and follows the pointer while held).
    if (map_.hasMinimap() && (m.held & legacy::kMouseLeft) && !dragging_ && m.x >= TacticsMap::kMiniX &&
        m.y >= TacticsMap::kMiniY && m.x < TacticsMap::kMiniX + map_.miniW() && m.y < TacticsMap::kMiniY + map_.miniH()) {
        // (FUN_00440d40: the point on the minimap; FUN_00440dd0 turns it into
        // map pixels, FUN_00440e60 the other way, for the network's flare.)
        const int px = (m.x - TacticsMap::kMiniX) * map_.cellsW() * TacticsMap::kCellW / map_.miniW();
        const int py = (m.y - TacticsMap::kMiniY) * map_.cellsH() * TacticsMap::kCellH / map_.miniH();
        map_.scrollTo(px - 0x200, py - 0x11d);
        return;
    }
    // FUN_004406f0: the right button on the minimap (FUN_00440d40, its
    // rectangle with the edges) orders the picked units to that point
    // (FUN_00440dd0: minimap pixels to map pixels).
    if (map_.hasMinimap() && (m.pressed & legacy::kMouseRight) && units_.selectedCount() > 0 && m.x >= TacticsMap::kMiniX &&
        m.y >= TacticsMap::kMiniY && m.x <= TacticsMap::kMiniX + map_.miniW() && m.y <= TacticsMap::kMiniY + map_.miniH()) {
        const int px = std::min((m.x - TacticsMap::kMiniX) * map_.cellsW() * TacticsMap::kCellW / map_.miniW(),
                                map_.cellsW() * TacticsMap::kCellW - 1);
        const int py = std::min((m.y - TacticsMap::kMiniY) * map_.cellsH() * TacticsMap::kCellH / map_.miniH(),
                                map_.cellsH() * TacticsMap::kCellH - 1);
        units_.orderMinimap(px / TacticsMap::kCellW, py / TacticsMap::kCellH);
        return;
    }
    // The roster faces (FUN_0046ea10's face test): a click picks the unit,
    // a second click on a picked unit on the field centres the view on it.
    // A face the call drag armed takes its click on the release, unless the
    // drag got going (FUN_0046ea10 picks faces on the let-go, FUN_0046eda0).
    if (units_.callDragging()) faceHeld_ = -1;
    if ((m.released & legacy::kMouseLeft) && faceHeld_ >= 0) {
        const int slot = std::exchange(faceHeld_, -1);
        const TacticsUnits::Unit& u = units_.unit(slot);
        if (u.selected && u.onField()) map_.centreOn(u.px(), u.py());
        units_.selectSlot(slot, (SDL_GetModState() & KMOD_SHIFT) != 0);
        return;
    }
    if ((m.pressed & legacy::kMouseLeft) && !dragging_) {
        if (const int slot = wide_.onHud() ? units_.rosterAt(m.x, m.y) : -1; slot >= 0) {
            if (units_.callArmed()) {
                faceHeld_ = slot;
                return;
            }
            const TacticsUnits::Unit& u = units_.unit(slot);
            if (u.selected && u.onField()) map_.centreOn(u.px(), u.py());
            units_.selectSlot(slot, (SDL_GetModState() & KMOD_SHIFT) != 0);
            startDrag(true);                         // a box may go on over more faces
            return;
        }
        // FUN_0046f8b0(.., 1): a click in either roster column (DAT_00617eb0:
        // x 0..49 and 974..1023, y 30..599) that misses the faces picks
        // nothing on the map below it (but may start a box over the faces).
        if (!wide_.on() && rosterColumn(m.x, m.y, m.x, m.y) != 0) {
            startDrag(true);
            return;
        }
    }
    int cx = 0, cy = 0;
    const bool onMap = map_.cellAt(m.x, m.y, cx, cy) && wide_.onMap();
    const int wx = m.x + map_.viewX(), wy = m.y - TacticsMap::kViewY + map_.scrollY();
    // The original reads Shift from its key mask (DAT_007a49fc +0x194c & 0x40000).
    const bool toggle = (SDL_GetModState() & KMOD_SHIFT) != 0;
    if ((m.pressed & legacy::kMouseLeft) && onMap) startDrag(false);
    if (dragging_ && (m.held & legacy::kMouseLeft) && (std::abs(wx - dragX_) > 4 || std::abs(wy - dragY_) > 4))
        dragged_ = true;
    if (dragging_ && (m.released & legacy::kMouseLeft)) {
        const int sx0 = dragX_ - map_.viewX(), sy0 = dragY_ + TacticsMap::kViewY - map_.scrollY();
        // FUN_0046f8b0(.., 1) -> FUN_0046eda0: a box over the roster's faces picks those.
        if (dragged_ && wide_.onHud() && units_.selectFaces(sx0, sy0, m.x, m.y, toggle, false) > 0) {
            dragging_ = false;
            return;
        }
        // A press on the roster that never became a box was its click.
        if (!dragged_ && dragFace_) {
            dragging_ = false;
            return;
        }
        // FUN_0046f8b0(.., 0): a box wholly inside a roster column picks nothing.
        if (dragged_ && !wide_.on() && rosterColumn(std::min(sx0, m.x), std::min(sy0, m.y), std::max(sx0, m.x), std::max(sy0, m.y)) != 0) {
            dragging_ = false;
            return;
        }
        if (dragged_) {
            if (wide_.onMap()) units_.selectBox(dragX_, dragY_, wx, wy, toggle);
        }
        else if (onMap) {
            // FUN_0046efa0(0): a double click on one of ours on the field
            // without Shift picks its like in view (FUN_00494a50).
            const bool twice = clickAge_ < 30 && clickX_ == cx && clickY_ == cy;
            // FUN_0046efa0(0) takes the unit under the pointer (+0x30992,
            // its box), else the one on the cell.
            const TacticsUnits::Unit* u = units_.pointedUnit(m.x, m.y);
            if (u) units_.selectSlot(u->slot, toggle);
            else units_.selectAt(cx, cy, toggle);
            if (!u) u = units_.at(cx, cy);
            if (twice && !toggle && u && u->selected) units_.selectLike(*u);
            clickAge_ = 0;
            clickX_ = cx;
            clickY_ = cy;
        }
        dragging_ = false;
    }
    // FUN_0048d720: every selected unit takes the order: its skill on the
    // unit under the pointer where the skill fits it (FUN_0046c350 /
    // FUN_0046c480: a heal on ours, an attack on an enemy), else a walk.
    if ((m.pressed & legacy::kMouseRight) && onMap && units_.selectedCount() > 0) {
        const TacticsUnits::Unit* t = units_.pointedUnit(m.x, m.y);
        if (t && units_.orderCall(t->slot)) return;  // ours waiting picked: a unit of ours calls them in
        units_.orderAt(cx, cy, t ? t->slot : -1);
    }
}

// FUN_00473dc0(0): our units still in use (FUN_0046a4c0: ours) back into the
// party records, in order, as the one stage fought of one (0x7a5296 /
// 0x7a5298), its units by character (0x7a52d0, 0x7a52f8), and the stage's
// group for the results (0x7a5294: 0x25..0x28 and 0x96..0x9a themselves,
// 0x30..0x44 as 0x30, else 0x25). (Its 1, a chained stage, keeps the
// records' +0xa8..+0xae too; no network map chains.)
void TacticsTask::coopHandBack() {
    GameWork& w = ctx_.work;
    const int count = w.u8(stage::kPartyCount);
    std::vector<const TacticsUnits::Unit*> kept;
    for (int i = 0; i < count && i < TacticsUnits::kSlots; ++i) {
        const auto& u = units_.unit(i);
        if (u.used && u.team() == units_.viewer()) kept.push_back(&u);
    }
    w.fill(stage::kParty, 0x3700, 0);
    for (std::size_t k = 0; k < kept.size(); ++k)
        for (std::uint32_t b = 0; b < 0xb0; ++b) w.set8(stage::kParty + std::uint32_t(k) * 0xb0 + b, kept[k]->data.u8(b));
    w.set8(stage::kPartyCount, std::uint8_t(kept.size()));
    w.set16(0x7a5296, 1);
    w.set16(0x7a5298, 1);
    w.set16(0x7a52f8, std::int16_t(kept.size()));
    w.fill(0x7a52d0, 0x28, 0);
    for (std::size_t k = 0; k < kept.size() && k < 0x14; ++k) w.set16At(0x7a52d0, k, w.s16(0x7f451a + std::uint32_t(k) * 0xb0));
    const int map = w.s16(stage::kMap);
    w.set16(0x7a5294, (map >= 0x25 && map <= 0x28) || (map >= 0x96 && map <= 0x9a) ? map : map >= 0x30 && map <= 0x44 ? 0x30 : 0x25);
    SDL_Log("[net] co-operative battle over: %zu of our units back to the party", kept.size());
}

// ------------------------------------------------------ network talk ---
// The battle's chat and flares (FUN_00457890 .. FUN_00458450) travel as the
// relay's chat lines, outside the lockstep: they change nothing in the
// battle. A flare's line is "\x02x,y" (map pixels); a line for our team
// only starts with "\x01" (+0x18c), as in the room.
namespace {
constexpr int kTalkX = 0x1cc, kTalkLogY = 0x2cd, kTalkLineY = 0x2b9;
constexpr int kToggleX = 0x3d2, kTeamToggleY = 0x298, kFlareToggleY = 0x2c4, kToggleSize = 0x2a;
bool inBox(int x, int y, int bx, int by) { return x >= bx && y >= by && x < bx + kToggleSize && y < by + kToggleSize; }
} // namespace

// FUN_00458060: the sender is on our side (DAT_007f44e4).
bool TacticsTask::sameSide(int slot) const {
    const int me = net_ ? net_->room().you : 0;
    if (slot < 0 || slot > 3 || me < 0 || me > 3) return false;
    return ctx_.work.u8(stage::kSlotSide + std::uint32_t(slot)) == ctx_.work.u8(stage::kSlotSide + std::uint32_t(me));
}

// FUN_00493c40: our flare at once and to the others (FUN_00458380, message
// 0x10: all, or our side with the team toggle on).
void TacticsTask::sendFlare(int px, int py) {
    char line[32];
    std::snprintf(line, sizeof line, "%s\x02%d,%d", netChatTeam_ ? "\x01" : "", px, py);
    net_->chat(line);
    flareAt(px, py, netChatTeam_ ? 1 : 0);
}

// FUN_00493960 on the minimap point of map pixel (px, py) (FUN_00440e60).
void TacticsTask::flareAt(int px, int py, int side) {
    if (!map_.hasMinimap() || map_.cellsW() == 0 || map_.cellsH() == 0) return;
    const int mx = TacticsMap::kMiniX + px * map_.miniW() / (map_.cellsW() * TacticsMap::kCellW);
    const int my = TacticsMap::kMiniY + py * map_.miniH() / (map_.cellsH() * TacticsMap::kCellH);
    if (tactics::traceUnits()) SDL_Log("[net] flare at %d,%d (side %d)", px, py, side);
    units_.flare(mx, my, side);
}

// Once a frame in network play: FUN_00457890 (Enter opens the line, a
// second Enter sends it, FUN_00457aa0 -> FUN_00457e00; the team toggle at
// 0x3d2, 0x298), FUN_00458180 (the flare toggle at 0x3d2, 0x2c4), and the
// lines that came in (FUN_00457f50 / FUN_00458450: ours are not shown
// again, a team line only to its side).
void TacticsTask::netTalk() {
    auto& in = ctx_.engine.input();
    const auto& m = ctx_.mouse();
    // What was typed this tick goes in before its Enter is looked at. The
    // line is UTF-8 from the system's input method (any language), up to
    // 0x7e characters (FUN_00457aa0's limit, counted in characters here).
    if (netChatOpen_) {
        const auto& t = in.text();
        for (int i = 0; i < t.backspaces && !netChatLine_.empty(); ++i) net::popUtf8(netChatLine_);
        net::appendUtf8(netChatLine_, t.typed, 0x7e);
    }
    // (With the line closed text entry is off: Enter arrives as Confirm.)
    if (netChatOpen_ ? in.text().enter : in.pressed(legacy::Action::Confirm)) {
        if (SDL_getenv("CH_NET_TRACE")) SDL_Log("[net] battle chat %s", netChatOpen_ ? "sent" : "opened");
        if (!netChatOpen_) {
            netChatOpen_ = true;
            in.setTextInput(true);
        } else {
            if (!netChatLine_.empty()) {
                net_->chat(netChatTeam_ ? std::string(1, '\x01') + netChatLine_ : netChatLine_);
                netLog_.add(net_->playerName + ": " + netChatLine_);
            }
            netChatLine_.clear();
            netChatOpen_ = false;                    // FUN_00457830
            in.setTextInput(false);
        }
    }
    if ((m.released & legacy::kMouseLeft) && inBox(m.x, m.y, kToggleX, kTeamToggleY) && inBox(pressX_, pressY_, kToggleX, kTeamToggleY))
        netChatTeam_ = !netChatTeam_;
    if ((m.released & legacy::kMouseLeft) && inBox(m.x, m.y, kToggleX, kFlareToggleY) && inBox(pressX_, pressY_, kToggleX, kFlareToggleY))
        netFlareMode_ = !netFlareMode_;
    for (const auto& c : net_->chatLog()) {
        if (c.seq < netSeen_) continue;
        netSeen_ = c.seq + 1;
        if (c.slot == net_->room().you) continue;  // our own came back
        std::string t = c.text;
        bool team = false;
        if (!t.empty() && t[0] == '\x01') { team = true; t.erase(0, 1); }
        if (team && !sameSide(c.slot)) continue;
        if (!t.empty() && t[0] == '\x02') {
            int px = 0, py = 0;
            if (std::sscanf(t.c_str() + 1, "%d,%d", &px, &py) == 2) flareAt(px, py, team ? 1 : 0);
            continue;
        }
        std::string who = "?";
        for (const auto& mb : net_->room().members) if (mb.slot == c.slot) who = mb.name;
        const std::string line = who + (team ? " (team): " : ": ") + t;
        if (SDL_getenv("CH_NET_TRACE")) SDL_Log("[net] battle chat shown: %s", line.c_str());
        netLog_.add(line);
    }
}

// FUN_00457c40: the two toggles (FUN_00457d00 common.bin frames 0x23f /
// 0x23e, FUN_00458280 0x23d / 0x23c), the log newest first (FUN_00425620 at 0x1cc, 0x2cd,
// 10 apart) and the line
// being typed (FUN_00426820 at 0x1cc, 0x2b9).
void TacticsTask::drawTalk() {
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kCommonBank), .frame = static_cast<std::uint16_t>(netChatTeam_ ? 0x23e : 0x23f)},
                  0xc6, kToggleX, kTeamToggleY);
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kCommonBank), .frame = static_cast<std::uint16_t>(netFlareMode_ ? 0x23c : 0x23d)},
                  0xc6, kToggleX, kFlareToggleY);
    netLog_.draw([&](int row, const std::string& line) {
        if (!line.empty())
            ctx_.draw.text({.x = kTalkX, .y = kTalkLogY + row * 10, .priority = 0xc4, .argb = 0xffffffff,
                            .text = line, .fontPx = 10});
    });
    if (netChatOpen_)
        ctx_.draw.text({.x = kTalkX, .y = kTalkLineY, .priority = 0xc4, .argb = 0xffffff80u,
                        .text = "> " + netChatLine_ + ((SDL_GetTicks() / 400) % 2 ? "_" : ""), .fontPx = 13});
}

// FUN_00472f40 / FUN_00473390 / FUN_00473570 in style 1: digits 8 x 14
// from common.bin frame 0x14 (0..4 at y 24, 5..9 at y 40), the colon at
// (40, 24), the slash at (48, 24), the dash at (40, 40); 7 px apart.
void TacticsTask::drawGlyph(int sx, int sy, int x, int prio) {
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kCommonBank), .frame = 0x14, .sx = sx, .sy = sy, .w = 8, .h = 14},
                  prio, x, 6);
}

void TacticsTask::drawTime(int x, std::int32_t frames, int prio) {
    const int s = frames / 0x3c;
    const int parts[3] = {s / 0xe10 % 0x3c, s / 0x3c % 0x3c, s % 0x3c};
    for (int k = 0; k < 3; ++k) {
        for (const int d : {parts[k] / 10, parts[k] % 10}) {
            drawGlyph(d % 5 * 8, d < 5 ? 24 : 40, x, prio);
            x += 7;
        }
        if (k < 2) { drawGlyph(40, 24, x, prio); x += 7; }
    }
}

// FUN_00467c20: the elapsed time, and the time left over the limit (dashes
// when the stage has none).
void TacticsTask::drawClock(int prio) {
    drawTime(0x230, clock_.elapsed, prio);
    if (clock_.limit == 0x7fffffff) {
        for (const int x0 : {0x148, 0x18e})
            for (int k = 0, x = x0; k < 3; ++k) {
                drawGlyph(40, 40, x, prio); x += 7;
                drawGlyph(40, 40, x, prio); x += 7;
                if (k < 2) { drawGlyph(40, 24, x, prio); x += 7; }
            }
    } else {
        drawTime(0x148, clock_.left, prio);
        drawTime(0x18e, clock_.limit, prio);
    }
    drawGlyph(48, 24, 0x184, prio);
}

// FUN_0046f8b0: 1 / 2 when the screen box (x0, y0)..(x1, y1) lies in the
// left / right roster column (DAT_00617ed0 / DAT_00617ee0 for a box,
// DAT_00617eb0 / DAT_00617ec0 for a point, their half sizes overlapping).
int TacticsTask::rosterColumn(int x0, int y0, int x1, int y1) const {
    const GameVersion v = ctx_.version;
    const auto r = [&](std::uint32_t a) { return logic::sdS32(a, v); };
    int hit = 0;
    const int margin = map_.margin();                // the columns keep to the wider view's edges (TacticsUnits::faceX)
    if (x0 != x1 || y0 != y1) {
        if (r(0x617ed0) - margin <= x0 && x1 <= r(0x617ed8) - margin && r(0x617ed4) <= y0 && y1 <= r(0x617edc)) hit |= 1;
        if (r(0x617ee0) + margin <= x0 && x1 <= r(0x617ee8) + margin && r(0x617ee4) <= y0 && y1 <= r(0x617eec)) hit |= 2;
        return hit;
    }
    for (int k = 0; k < 2; ++k) {                    // FUN_0046fff0 with a zero-size point
        const std::uint32_t b = 0x617eb0 + std::uint32_t(k) * 0x10;
        const int hw = r(b + 8) / 2, hh = r(b + 0xc) / 2, cx = r(b) + hw + (k == 0 ? -margin : margin), cy = r(b + 4) + hh;
        if (std::abs(cx - x0) <= hw && std::abs(cy - y0) <= hh) hit |= 1 << k;
    }
    return hit;
}

// The left button went down at the pointer: a box may follow. `face`: it
// started on the roster, so letting go without a box picks nothing more.
void TacticsTask::startDrag(bool face) {
    const auto& m = ctx_.mouse();
    dragging_ = true;
    dragged_ = false;
    dragFace_ = face;
    dragX_ = m.x + map_.viewX();
    dragY_ = m.y - TacticsMap::kViewY + map_.scrollY();
}

// The sortie's clicks (FUN_00499da0 case 1): a roster face picks a waiting
// party member (Shift toggles); the button (x 0x1e0..0x307, y 0x2de..0x2f7)
// is off while the count is outside fewest..most, pressed while held over,
// and sends them on release (voice 5).
void TacticsTask::sortieMouse() {
    const auto& m = ctx_.mouse();
    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
    const bool over = m.x > 0x1df && m.x < 0x308 && m.y > 0x2dd && m.y < 0x2f8;
    // FUN_0046ea10 with +0x108b37 1: the faces by click or by a box over
    // them (FUN_0046eda0), never the map's units (FUN_0046efa0).
    if ((m.pressed & legacy::kMouseLeft) && !over) {
        startDrag(true);
        if (const int slot = units_.rosterAt(m.x, m.y); slot >= 0) {
            units_.selectCard(slot, shift);
            return;
        }
    }
    if (dragging_) {
        const int wx = m.x + map_.viewX(), wy = m.y - TacticsMap::kViewY + map_.scrollY();
        if ((m.held & legacy::kMouseLeft) && (std::abs(wx - dragX_) > 4 || std::abs(wy - dragY_) > 4)) dragged_ = true;
        if (m.released & legacy::kMouseLeft) {
            if (dragged_)
                units_.selectFaces(dragX_ - map_.viewX(), dragY_ + TacticsMap::kViewY - map_.scrollY(), m.x, m.y, shift, true);
            dragging_ = false;
        }
        if (dragging_) return;
    }
    const int count = units_.selectedCount();
    if (count < sortie_.fewest || count > sortie_.most) {
        sortie_.button = 2;
        return;
    }
    sortie_.button = 0;
    if (!over) return;
    if (m.held & legacy::kMouseLeft) sortie_.button = 1;
    if (m.released & legacy::kMouseLeft) {
        sortie_.wait = 4;
        ++sortie_.phase;
    }
}

// FUN_004998b0: banner `type` (0 the stage's start picture, 1 common.bin
// 0x235) fades in.
void TacticsTask::startBanner(int type) {
    startBanner_ = {};
    startBanner_.on = true;
    startBanner_.type = type;
}

// FUN_00499910 by DAT_0059eaa4 [type] {-, in step, hold, out step, x, y,
// wait for a click}: in, then held for its time or until a click (or
// Enter; not in network play), then out; true once done.
bool TacticsTask::tickBanner() {
    StartBanner& b = startBanner_;
    if (!b.on) return true;
    const GameVersion v = ctx_.version;
    const std::uint32_t row = 0x59eaa4 + std::uint32_t(b.type) * 0xe;
    b.x = logic::sdS16(row + 8, v);
    b.y = logic::sdS16(row + 0xa, v);
    switch (b.step) {
        case 0:
            b.alpha = 0;
            b.rate = logic::sdS16(row + 2, v);
            b.step = 1;
            [[fallthrough]];
        case 1:
            b.alpha += b.rate;
            if (b.alpha > 0xfeff) {
                b.alpha = 0xff00;
                // In network play (FUN_00456b20) no banner waits for a click:
                // it holds for its time, the same on every machine.
                if (logic::sdS16(row + 0xc, v) == 0 || net_) {
                    b.hold = logic::sdS16(row + 4, v);
                    b.step = 3;
                } else {
                    b.step = 2;
                }
            }
            break;
        case 2:
            if ((ctx_.mouse().pressed & legacy::kMouseLeft) || ctx_.engine.input().pressed(legacy::Action::Confirm) || SDL_getenv("CH_AUTOSKIP")) {
                b.step = 4;
                b.rate = logic::sdS16(row + 6, v);
            }
            break;
        case 3:
            if (--b.hold <= 0) {
                b.step = 4;
                b.rate = logic::sdS16(row + 6, v);
            }
            break;
        case 4:
            b.alpha -= b.rate;
            if (b.alpha < 0x100) {
                b.alpha = 0;
                ++b.step;
            }
            break;
        default:
            b.on = false;
            return true;
    }
    return false;
}

// FUN_00499b80: the banner's picture (DAT_0061a014 [type] {bank, frame})
// centred on its point, at its fade.
void TacticsTask::drawBanner() const {
    if (!startBanner_.on) return;
    const GameVersion v = ctx_.version;
    const std::uint32_t f = 0x61a014 + std::uint32_t(startBanner_.type) * 4;
    SpriteCmd c{.bank = static_cast<std::uint16_t>(logic::sdS16(f, v)), .frame = static_cast<std::uint16_t>(logic::sdS16(f + 2, v)),
                .x = startBanner_.x, .y = startBanner_.y, .flags = std::uint16_t(kSprBank | kSprCentre), .blend = kBlendAlpha,
                .color = {0x80, 0x80, 0x80, static_cast<std::uint8_t>(startBanner_.alpha >> 8)}, .priority = 100};
    ctx_.draw.sprite(c);
}

// FUN_00495fe0: up to six digits (no leading zeros) centred on x, from the
// glyph table DAT_00619748 [style * 10 + digit] {bank, frame, x0, y0, x1, y1
// inclusive, w, h, flags}, 10 px apart.
void TacticsTask::drawDigits(int x, int y, int value, int style, std::uint32_t argb, int prio) const {
    tactics::drawDigits(ctx_, x, y, prio, value, 0xff, style, argb);
}

// FUN_0049a600: the sortie panel while the cards are up (cases 0..3):
// common.bin frame 9 at (0x1cc, 0x260), frame 10's rows (DAT_0061a048:
// the base, the pressed and the off button), the fewest and most who may
// go and the count picked, red when it is out of range.
void TacticsTask::drawSortie() const {
    if (!sortie_.on || !sortie_.panel || sortie_.phase >= kSortieFree) return;
    const GameVersion v = ctx_.version;
    constexpr int kPrio = 0xc6;
    const auto part = [&](int row, int x, int y) {
        const std::uint32_t r = 0x61a048 + std::uint32_t(row) * 8;
        const auto s = [&](std::uint32_t o) { return std::int16_t(logic::sdS16(r + o, v)); };
        SpriteCmd c{.bank = kCommonBank, .frame = 10, .x = x, .y = y, .w = std::int16_t(s(4) - s(0) + 1), .h = std::int16_t(s(6) - s(2) + 1),
                    .src = {s(0), s(2), std::int16_t(s(4) + 1), std::int16_t(s(6) + 1)},
                    .flags = std::uint16_t(kSprBank | kSprSrcRect), .blend = kBlendTest, .priority = kPrio};
        ctx_.draw.sprite(c);
    };
    if (sortie_.button == 1) part(1, 0x1cf, 0x2d8);
    else if (sortie_.button == 2) part(2, 0x1cf, 0x2d8);
    part(0, 0x1cc, 0x2af);
    ctx_.draw.sprite(SpriteCmd{.bank = kCommonBank, .frame = 9, .x = 0x1cc, .y = 0x260, .flags = kSprBank, .priority = kPrio});
    // (FUN_0049a9b0: style 0 grey 0xff808080, style 4 red 0xff802020, at 0xc5.)
    drawDigits(0x2f8, 0x2af, sortie_.fewest, 0, 0xff808080u, kPrio - 1);
    drawDigits(0x2f8, 0x2c2, sortie_.most, 0, 0xff808080u, kPrio - 1);
    const int count = units_.selectedCount();
    const bool out = count < sortie_.fewest || count > sortie_.most;
    drawDigits(0x270, 0x2ba, count, out ? 4 : 0, out ? 0xff802020u : 0xff808080u, kPrio - 1);
}

// FUN_00458580 (not in network play, during an event, before the battle
// proper or for a watcher): Esc, or a click begun and ended on the button
// at (9..0x58, 3..0x18), opens the menu (sound 5).
bool TacticsTask::openMenu() {
    const auto& m = ctx_.mouse();
    if (script_.eventRunning()) return false;
    const int left = wide_.on() ? 0 : -map_.margin();   // the port's wider view has the button at its left edge
    const auto in = [left](int x, int y) { return x >= left + 9 && x <= left + 0x58 && y >= 3 && y <= 0x18; };
    if (!ctx_.engine.input().pressed(legacy::Action::Menu) &&
        !((m.released & legacy::kMouseLeft) && in(m.x, m.y) && in(pressX_, pressY_)))
        return false;
    ctx_.audio->sePlay(5);
    menu_ = 1;
    return true;
}

// FUN_00458be0 (the same conditions): Space, or a click begun and ended on
// (0x3d1, 0x26f, 0x33 x 0x28), shows the stage's objectives (banner 7 +
// the script's message, FUN_004998b0) unless the stage has none to show
// (+0x30b28), sound 5.
bool TacticsTask::openObjectives() {
    objectivesShade_ = false;
    const auto& m = ctx_.mouse();
    if (script_.eventRunning()) return false;
    const auto in = [](int x, int y) { return x >= 0x3d1 && x < 0x3d1 + 0x33 && y >= 0x26f && y < 0x26f + 0x28; };
    if (!ctx_.engine.input().pressed(legacy::Action::Skip) &&
        !((m.released & legacy::kMouseLeft) && in(m.x, m.y) && in(pressX_, pressY_)))
        return false;
    if (startBanner_.on) return false;
    startBanner(7 + script_.resultMessage());
    ctx_.audio->sePlay(5);
    menu_ = 2;
    return true;
}

// FUN_00453d00. The menu (FUN_004586d0 at (0x179, 0x144)): FUN_00458800
// finds the button under the pointer (DAT_0060cb18: "continue" (27, 52,
// 103 x 28), "give up" (140, 52, 102 x 28)), Esc counting as continue;
// released on one (sound 5) it continues, or gives up: no result
// (DAT_005ff720 -1) and +0x31, which ends the battle. The objectives
// (FUN_00458d70) shade the screen at half the banner's fade
// (+0x11804c) until FUN_00499910 has run its course.
int TacticsTask::menuStep() {
    const GameVersion v = ctx_.version;
    const auto& m = ctx_.mouse();
    if (menu_ == 2) {
        objectivesShade_ = true;
        if (tickBanner()) {
            menu_ = 0;
            objectivesShade_ = false;
        }
        return 0;
    }
    constexpr int kX = 0x179, kY = 0x144;
    menuButton_ = -1;
    bool esc = ctx_.engine.input().pressed(legacy::Action::Menu);
    if (!esc) {
        for (int i = 0; i < 2; ++i) {
            const std::uint32_t e = 0x60cb18 + std::uint32_t(i) * 8;
            const int x = kX + logic::sdS16(e, v), y = kY + logic::sdS16(e + 2, v);
            if (m.x > x && m.x < x + logic::sdS16(e + 4, v) && m.y > y && m.y < y + logic::sdS16(e + 6, v)) {
                menuButton_ = i;
                break;
            }
        }
    }
    if (esc || (menuButton_ >= 0 && (m.released & legacy::kMouseLeft))) {
        ctx_.audio->sePlay(5);
        menu_ = 0;
        if (!esc && menuButton_ == 1) {
            ctx_.comingResult = -1;
            quit_ = true;
            return 1;
        }
        menuButton_ = -1;
    }
    return 0;
}

// FUN_00458930: the menu, common.bin frame 7's (0, 0, 0x10e x 0x5b), at
// priority 4; while held over a button FUN_00458a50 lays its pressed
// look over it (DAT_0060cb28 {x, y, w, h} from frame 7, at DAT_0060cb38
// in the menu), priority 3. During an event the menu draws its own
// pointer (FUN_00458f10: frame 0x1b3, priority 1).
void TacticsTask::drawMenu() const {
    const GameVersion v = ctx_.version;
    const auto& m = ctx_.mouse();
    if (menu_ == 2 || objectivesShade_) {
        ysub::fill(ctx_, 0, 0, 0x400, 0x300, 0x65, std::uint32_t((startBanner_.alpha >> 8) / 2) << 24);
        // FUN_00458e30: the objectives button pressed.
        ctx_.draw.sprite(SpriteCmd{.bank = kCommonBank, .frame = 0x23b, .x = 0x3d3, .y = 0x26d, .flags = kSprBank,
                                   .priority = 0xc6});
        return;
    }
    if (menu_ != 1) return;
    constexpr int kX = 0x179, kY = 0x144;
    ctx_.draw.sprite(SpriteCmd{.bank = kCommonBank, .frame = 7, .x = kX, .y = kY, .w = 0x10e, .h = 0x5b,
                               .src = {0, 0, 0x10e, 0x5b}, .flags = std::uint16_t(kSprBank | kSprSrcRect), .priority = 4});
    if (menuButton_ >= 0 && (m.held & legacy::kMouseLeft)) {
        const std::uint32_t r = 0x60cb28 + std::uint32_t(menuButton_) * 8;
        const std::uint32_t o = 0x60cb38 + std::uint32_t(menuButton_) * 4;
        const std::int16_t sx = logic::sdS16(r, v), sy = logic::sdS16(r + 2, v), sw = logic::sdS16(r + 4, v),
                           sh = logic::sdS16(r + 6, v);
        ctx_.draw.sprite(SpriteCmd{.bank = kCommonBank, .frame = 7, .x = kX + logic::sdS16(o, v), .y = kY + logic::sdS16(o + 2, v),
                                   .w = sw, .h = sh, .src = {sx, sy, std::int16_t(sx + sw), std::int16_t(sy + sh)},
                                   .flags = std::uint16_t(kSprBank | kSprSrcRect), .priority = 3});
    }
    if (script_.eventRunning())
        ctx_.draw.sprite(SpriteCmd{.bank = kCommonBank, .frame = 0x1b3, .x = m.x, .y = m.y, .flags = kSprBank, .priority = 1});
    drawGoals();
}

// The port's own: above the menu, what the stage's PERFECT asks for
// (StageChecks::goals), each with an empty box, a check once done or a red
// cross once it can no longer be met, and how far along it is; under them
// the best result still to be had. Japanese through tr() (the packs'
// port.tsv), Chinese for the TW release.
void TacticsTask::drawGoals() const {
    const Goals goals = checks_.goals(mapId_);
    if (!goals.known) return;
    const bool tw = ctx_.version == GameVersion::Tw;
    const auto say = [&](const char* japanese, const char* chinese) { return tw ? std::string(chinese) : tr(japanese); };
    const auto with = [](std::string t, const std::string& arg) {
        if (const auto at = t.find("{0}"); at != std::string::npos) t.replace(at, 3, arg);
        return t;
    };
    const auto clock = [](std::int32_t frames) {
        const int s = std::max(frames, 0) / 60;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
        return std::string(buf);
    };
    // The units a goal names: each name once (with a count when several share
    // it), at most four.
    const auto names = [&](const std::vector<int>& slots) {
        std::vector<std::pair<std::string, int>> seen;
        for (const int slot : slots) {
            const TacticsUnits::Unit& u = units_.unit(slot);
            if (!u.used) continue;                   // not come in yet
            const std::string n = units_.name(u);
            if (n.empty()) continue;
            auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& p) { return p.first == n; });
            if (it == seen.end()) seen.emplace_back(n, 1);
            else ++it->second;
        }
        const std::string join = say("{0}、{1}", "{0}、{1}");
        std::string out;
        for (std::size_t i = 0; i < seen.size(); ++i) {
            std::string one = i == 4 ? "…" : seen[i].first;
            if (i < 4 && seen[i].second > 1) one += " ×" + std::to_string(seen[i].second);
            if (i == 0) {
                out = one;
            } else {
                std::string t = join;
                t.replace(t.find("{1}"), 3, one);
                out = with(t, out);
            }
            if (i == 4) break;
        }
        return out.empty() ? say("目標のユニット", "目標單位") : out;
    };
    using K = Goal::Kind;
    const auto text = [&](const Goal& x) -> std::string {
        switch (x.kind) {
            case K::Objective:  return say("ステージの目的を果たす（Spaceキーで確認）", "達成關卡目標（按Space鍵確認）");
            case K::AllEnemies: return say("敵を全滅させる", "殲滅所有敵人");
            case K::Defeat:     return with(say("{0}を倒す", "打倒{0}"), names(x.slots));
            case K::Side:
                if (x.keep) return with(say("{0}を味方のまま保つ", "讓{0}留在我方"), names(x.slots));
                if (x.collect) return with(say("{0}を回収する", "取得{0}"), names(x.slots));
                if (x.take) return with(say("{0}を確保する", "佔領{0}"), names(x.slots));
                return with(say("{0}を味方にする", "讓{0}加入我方"), names(x.slots));
            case K::Protect:
                if (x.take) return with(say("{0}を破壊させない", "不讓{0}被摧毀"), names(x.slots));
                if (x.foe) return with(say("{0}を倒さない", "不打倒{0}"), names(x.slots));
                return with(say("{0}を倒されない", "不讓{0}倒下"), names(x.slots));
            case K::NoneFall:   return say("仲間を一人も倒されない", "不讓任何同伴倒下");
            case K::Buildings:  return x.keep ? say("建物を全て守る", "守住所有建築物") : say("建物を全て確保する", "佔領所有建築物");
            case K::Bases:      return x.keep ? say("拠点を全て守る", "守住所有據點") : say("拠点を全て確保する", "佔領所有據點");
            case K::Gates:      return x.keep ? say("門を全て守る", "守住所有城門") : say("門を全て確保する", "佔領所有城門");
            case K::EnemyGates: return say("敵の門を全て壊す", "摧毀所有敵方城門");
            case K::Within:     return with(say("{0}以内に勝つ", "在{0}內獲勝"), clock(x.frames));
            case K::TimeLimit:  return say("制限時間内に勝つ", "在時限內獲勝");
            case K::HoldOut:    return with(say("{0}持ちこたえる", "堅守{0}"), clock(x.frames));
            case K::Hold:       return with(say("確保したまま{0}保つ", "保持佔領{0}"), clock(x.frames));
            case K::Occupy:     return say("目標地点を全て押さえる", "壓制所有目標地點");
            case K::Leak:       return say("敵に一体も突破されない", "不讓任何敵人突破");
            case K::Escort:     return with(say("{0}を目的地へ連れて行く", "將{0}帶到目的地"), names(x.slots));
            case K::Reach:      return say("目的地にたどり着く", "抵達目的地");
            case K::ReachAfter: return with(say("{0}経ってから目的地にたどり着く", "經過{0}後抵達目的地"), clock(x.frames));
        }
        return {};
    };
    const auto progress = [&](const Goal& x) -> std::string {
        if (x.state != Goal::State::Open && x.kind != K::Leak && x.kind != K::Side && x.kind != K::Defeat && x.kind != K::Escort &&
            x.kind != K::Occupy && x.kind != K::Buildings && x.kind != K::Bases && x.kind != K::Gates &&
            x.kind != K::EnemyGates)
            return {};
        switch (x.kind) {
            case K::AllEnemies: return with(say("残り{0}", "剩餘{0}"), std::to_string(x.have));
            case K::Leak:       return with(say("突破 {0}", "已突破 {0}"), std::to_string(x.have));
            case K::Within: case K::TimeLimit: case K::HoldOut: case K::Hold: case K::ReachAfter:
                return x.left >= 0 ? with(say("残り{0}", "剩餘{0}"), clock(x.left)) : std::string{};
            default:
                return x.need > 0 && !x.keep ? std::to_string(x.have) + " / " + std::to_string(x.need) : std::string{};
        }
    };

    std::vector<const Goal*> shown;
    for (const Goal& x : goals.list) if (!x.hidden) shown.push_back(&x);
    constexpr int kW = 600, kX = 0x200 - kW / 2, kRow = 24, kPrio = 4;
    const int h = 14 + 26 + int(shown.size()) * kRow + 8 + 26 + 10;
    const int y0 = 0x144 - 10 - h;
    ysub::fill(ctx_, kX, y0, kW, h, kPrio, 0xe0141a12u);
    for (const auto& [x, y, w, hh] : {std::array{kX, y0, kW, 1}, std::array{kX, y0 + h - 1, kW, 1},
                                      std::array{kX, y0, 1, h}, std::array{kX + kW - 1, y0, 1, h}})
        ysub::fill(ctx_, x, y, w, hh, kPrio - 1, 0xff8a9a7au);
    ctx_.draw.text({.x = 0x200, .y = y0 + 14 + 9, .priority = kPrio - 1, .argb = 0xffffe08au,
                    .text = say("パーフェクトへの条件", "PERFECT 的條件"), .fontPx = 16, .bold = true, .centred = true});
    int y = y0 + 14 + 26;
    for (const Goal* x : shown) {
        const int bx = kX + 16, by = y + 4;
        ysub::fill(ctx_, bx, by, 15, 15, kPrio - 1, 0xffc8c8c0u);
        ysub::fill(ctx_, bx + 1, by + 1, 13, 13, kPrio - 2, 0xff1c201cu);
        if (x->state == Goal::State::Done) {
            static constexpr int kTick[][2] = {{2, 7}, {3, 8}, {4, 9}, {5, 10}, {6, 9}, {7, 8}, {8, 7}, {9, 6}, {10, 5}, {11, 4}};
            for (const auto& p : kTick) ysub::fill(ctx_, bx + p[0], by + p[1] - 1, 2, 3, kPrio - 3, 0xff50e050u);
        } else if (x->state == Goal::State::Failed) {
            for (int i = 2; i <= 11; ++i) {
                ysub::fill(ctx_, bx + i, by + i, 2, 2, kPrio - 3, 0xffff4040u);
                ysub::fill(ctx_, bx + 13 - i, by + i, 2, 2, kPrio - 3, 0xffff4040u);
            }
        }
        const std::uint32_t argb = x->state == Goal::State::Done ? 0xffa8e0a8u : x->state == Goal::State::Failed ? 0xffff9090u : 0xffffffffu;
        ctx_.draw.text({.x = bx + 26, .y = y + 2, .priority = kPrio - 1, .argb = argb, .text = text(*x), .fontPx = 15});
        if (const std::string p = progress(*x); !p.empty())
            ctx_.draw.text({.x = kX + kW - 120, .y = y + 2, .priority = kPrio - 1, .argb = argb, .text = p, .fontPx = 15});
        y += kRow;
    }
    y += 8;
    ysub::fill(ctx_, kX + 12, y - 4, kW - 24, 1, kPrio - 1, 0xff5a6650u);
    static constexpr const char* kRanks[] = {"PERFECT", "EXCELLENT", "CONGRATULATIONS", "NEVER MIND", "BAD LUCK"};
    const int best = std::clamp(goals.best, 1, 5);
    ctx_.draw.text({.x = 0x200, .y = y + 11, .priority = kPrio - 1, .argb = best == 1 ? 0xffffe08au : 0xffd0d0d0u,
                    .text = with(say("今の最高評価：{0}", "目前可得最高評價：{0}"), kRanks[best - 1]), .fontPx = 15,
                    .centred = true});
}

// FUN_00453f10's blinds: 64 bands, 6 px each, in the top and bottom
// halves (y i * 6 and 0x180 + (0x3f - i) * 6), black at 0xff less
// (t + i * 16), clamped, priority 5; opening t from -0x400 up 6 a frame
// past 0xfe, closing from 0x4ff down past 0 (the alpha 0xff - (t - i * 16)
// over the rows taken the other way round).
void TacticsTask::drawBlinds() {
    if (blinds_ == 0) return;
    for (int i = 0; i < 0x40; ++i) {
        int a = blinds_ == 1 ? 0xff - std::clamp(blindsT_ + i * 16, 0, 0xff) : std::clamp(0xff - (blindsT_ - i * 16), 0, 0xff);
        const int row = blinds_ == 1 ? i : 0x3f - i;
        const std::uint32_t argb = std::uint32_t(a) << 24;
        ysub::fill(ctx_, 0, row * 6, 0x400, 6, 5, argb);
        ysub::fill(ctx_, 0, (0x3f - row) * 6 + 0x180, 0x400, 6, 5, argb);
    }
    if (blinds_ == 1) {
        blindsT_ += 6;                               // FUN_00415040(6)
        if (blindsT_ > 0xfe) blinds_ = 0;
    } else {
        blindsT_ -= 6;
        if (blindsT_ < 0) blinds_ = 0;
    }
}

// The port's wider view: how much more of the map shows at each side of the
// original's 0x400. Single player: what the window's shape has room for, up
// to 16:9. Network play: the same for every player whatever their window, so
// nobody sees more of the field than another (a narrower window shows the
// frame smaller, with bars above and below).
int TacticsTask::viewMargin() const {
    // Debug: CH_MARGIN=<n> sets it.
    static const char* const force = SDL_getenv("CH_MARGIN");
    if (force) return std::atoi(force);
    if (net_) return legacy::Engine::kMaxMargin;
    const SDL_Point out = ctx_.engine.outputSize();
    if (!options::port().wideBattle || out.y <= 0) return 0;
    return std::clamp((out.x * kScreenH / out.y - kScreenW) / 2, 0, int(legacy::Engine::kMaxMargin));
}

void TacticsTask::draw() {
    if (!wide_.on()) map_.setMargin(viewMargin());   // the split view sets its own
    ctx_.engine.setMargin(map_.margin());
    const auto& m = ctx_.mouse();
    int cx = 0, cy = 0;
    const bool onMap = map_.cellAt(m.x, m.y, cx, cy) && wide_.onMap();
    const TacticsUnits::Unit* hover = onMap ? units_.pointedUnit(m.x, m.y) : nullptr;
    map_.draw(kMapPrio);
    units_.draw(hover ? hover->slot : -1);
    script_.draw();                                  // FUN_00454f50 during an event: FUN_00497c90, FUN_00456650
    ctx_.draw.setLayer(1);
    if (net_) drawTalk();                            // FUN_00457c40
    ctx_.draw.setLayer(0);
    if (net_ && script_.waitingForOthers())
        ctx_.draw.text({.x = 0x200, .y = 0x60, .priority = 1, .argb = 0xffffffff, .text = "Waiting for the other players...", .fontPx = 16, .centred = true});
    drawSortie();
    drawBanner();
    ctx_.draw.setLayer(1);
    drawMenu();
    ctx_.draw.setLayer(0);
    drawBlinds();
    // (FUN_004539b0 also calls FUN_0043d970 each frame, which is empty.)
    // FUN_00453e10 -> FUN_0043b3c0 -> FUN_0043cd70: while the left button
    // drags on the map (+0x265d8), a frame from where it went down to the
    // pointer, white at half alpha, added, at 2. (FUN_0043cab0 /
    // FUN_0043cc50 keep the box's screen rectangle, rows clamped to
    // 0x1e..599, for the pick; the port's box is in map pixels.)
    if (dragging_) {
        const int x0 = dragX_ - map_.viewX(), y0 = dragY_ - map_.scrollY() + TacticsMap::kViewY;
        ysub::outline(ctx_, x0, y0, m.x, m.y, 2, 0x80ffffffu, kBlendAdd);
    }
    wide_.draw();
    // The HUD on its own layer, the pointer on a third (the wide view places them apart).
    ctx_.draw.setLayer(1);
    // FUN_00453e10: the top bar and the two bottom panels.
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kCommonBank), .frame = 0}, 200, 0, 0);
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kCommonBank), .frame = 1}, 200, 0x18c, 600);
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kCommonBank), .frame = 2}, 200, 0x31a, 600);
    // The port's wider view: the bars run on over the margins, with a plain
    // stretch of the top bar and the empty unit panel's left end (turned
    // round at the right). The MENU button moves out to the left edge, with
    // the roster's faces; a plain stretch covers where it was.
    if (const int margin = map_.margin(); margin > 0 && !wide_.on()) {
        const auto strip = [&](int frame, int sx, int x, int y, int w, bool flip, int prio = 200) {
            const int h = ctx_.sprites->frameSize(kCommonBank, std::uint16_t(frame)).second;
            ctx_.draw.sprite(SpriteCmd{.bank = kCommonBank, .frame = std::uint16_t(frame), .x = x, .y = y,
                                       .w = std::int16_t(w), .h = std::int16_t(h),
                                       .src = {std::int16_t(sx), 0, std::int16_t(sx + w), std::int16_t(h)},
                                       .flags = std::uint16_t(kSprBank | kSprSrcRect | (flip ? kSprFlipU : 0)), .priority = std::uint16_t(prio)});
        };
        strip(0, 0, -margin, 0, kMenuButtonW, false);
        strip(0, 0x2f0, kMenuButtonW - margin, 0, margin, false, 199);
        strip(0, 0x2f0, kScreenW, 0, margin, true);
        strip(4, 0, -margin, 600, margin, false);
        strip(4, 0, kScreenW, 600, margin, true);
    }
    drawClock(199);
    map_.drawMinimap(199);                           // FUN_00440f00(.., 199)
    // The unit the panel shows (+0x109d4e): the one under the pointer, else
    // the first picked.
    // The roster faces count as under the pointer too (the port's).
    int under = hover && units_.isSeen(*hover) ? hover->slot : -1;
    if (const int r = wide_.onHud() ? units_.rosterAt(m.x, m.y) : -1; r >= 0) under = r;
    const int shown = units_.updatePanel(under, sortie_.phase < kSortieDone);   // FUN_0049cc60
    units_.talk(sortie_.phase < kSortieDone);                // FUN_0046db20 -> FUN_0049ab60
    units_.drawHud(shown, sortie_.phase < kSortieDone);
    units_.updateHelp(barInput(), shown);            // FUN_0049c400
    units_.drawBar(sortie_.phase < kSortieDone);                   // FUN_0049beb0
    units_.drawHelp();                               // FUN_0049c850
    units_.updateCursor(onMap, cx, cy, hover && units_.isSeen(*hover) ? hover->slot : -1);   // FUN_00490fc0
    ctx_.draw.setLayer(2);
    if (!script_.eventRunning()) units_.drawCursor(m.x, m.y);                              // FUN_00491680
    ctx_.draw.setLayer(0);
    if (!SDL_getenv("CH_DEBUG")) return;
    std::string info = "map " + std::to_string(mapId_) + "  scroll " + std::to_string(map_.viewX()) + "," +
                       std::to_string(map_.scrollY());
    if (onMap) {
        ysub::fill(ctx_, cx * TacticsMap::kCellW - map_.viewX(), cy * TacticsMap::kCellH - map_.scrollY() + TacticsMap::kViewY,
                   TacticsMap::kCellW, TacticsMap::kCellH, 0xe00, 0x60ffffffu);
        info += "  cell " + std::to_string(cx) + "," + std::to_string(cy) + "  terrain " +
                std::to_string(map_.cell(cx, cy, 0)) + "  +2 " + std::to_string(map_.cell(cx, cy, 2));
        if (const TacticsUnits::Unit* u = hover) {
            info += "  unit " + std::to_string(u->slot) + " id " + std::to_string(u->data.s16(0)) + " kind " +
                    std::to_string(u->data.u16(0xc)) + " team " + std::to_string(u->team()) + " HP " +
                    std::to_string(u->hp()) + "/" + std::to_string(u->maxHp()) + " state " +
                    std::to_string(u->state);
        }
    }
    // Debug lines (CH_DEBUG) at the top of the view.
    ctx_.draw.text({.x = 0x40, .y = 0x22, .priority = 0x3f, .argb = 0xffffffff, .text = info, .fontPx = 13});
    ctx_.draw.text({.x = 0x40, .y = 0x34, .priority = 0x3f, .argb = 0xffc0c0c0,
                    .text = "Click / drag: select   Right click: walk / attack   Enter: win at once   Tab: skip dialogue", .fontPx = 12});
}

// FUN_00473860: the rank (1..5, FUN_00432100 per map; the stand-in's 2 for
// a win, 5 for a loss) to DAT_007f450c; the time taken against the stage's
// two marks (DAT_007f44ac / b0: 0 none, 1 within the first, 2 within the
// second, 3 over) to 7f450e; 2 when no enemy fighter is left (FUN_004307b0),
// else 1, to 7f4510; FUN_00473940: each of the stage's bases (DAT_007f44be,
// enemy slot numbers) 1 if ours at the end; FUN_00473ab0: the party's kills
// (+0xaa), exp earned (+0xac) and falls (+0xae), and what our kills were
// worth (+0x2ef14) to 7f4512. Made-up figures when no battle ran.
void TacticsTask::finish(int rank) {
    GameWork& w = ctx_.work;
    SDL_Log("[tactics] battle over, rank %d", rank);
    w.set16(stage::kRank, static_cast<std::int16_t>(rank));
    const std::int32_t mark1 = w.s32(stage::kTimeMark1), mark2 = w.s32(stage::kTimeMark2);
    const std::int32_t used = clock_.limit != 0x7fffffff ? clock_.limit - clock_.left : clock_.elapsed;
    w.set16(stage::kTimeGrade, static_cast<std::int16_t>(mark1 == 0 ? 0 : used <= mark1 ? 1 : used <= mark2 ? 2 : 3));
    w.set16(stage::kEnemiesGone, units_.enemiesGone() ? 2 : 1);
    for (std::uint32_t k = 0; k < 0x10; ++k)
        if (const int n = static_cast<std::int8_t>(w.u8(stage::kPrizes + k)); n != 0) w.set8(stage::kPrizes + k, units_.baseOurs(n) ? 1 : 0);
    w.set16(stage::kWorth, static_cast<std::int16_t>(units_.teamWorth(units_.viewer())));
    const bool fought = units_.partyCount() > 0;
    for (int i = 0; i < w.u8(stage::kPartyCount); ++i) {
        const std::uint32_t u = stage::kParty + std::uint32_t(i) * 0xb0;
        if (fought && i < units_.partyCount()) {
            const auto& d = units_.unit(i).data;
            w.set16(u + 0xaa, d.s16(0xaa));
            w.set16(u + 0xac, d.s16(0xac));
            w.set8(u + 0xae, d.u8(0xae));
            continue;
        }
        w.set16(u + 0xaa, i + 1);
        w.set16(u + 0xac, 120 * (i + 1));
        w.set8(u + 0xae, i == 0 ? 1 : 0);
    }
    route(rank);
}

// FUN_00451670's tail, by the stage (DAT_007f4488): the trial maps 0x4c,
// 0x4d, 0x4f go to the coming-soon screen, 0x4e and 0x50 to the title,
// 0x52 to the trial title when given up (DAT_005ff720 -1) else the coming
// screen. Any other stage with a next stage (DAT_007f448a) chains on
// (FUN_004536c0) while one of the party stands on the field and time is
// left: the party's records keep their start (+0x9d..0xa3 cleared, +0x9f
// 2), the next stage's row is loaded (FUN_004da8a0) and the battle runs
// again (its tallies added to the round's, FUN_004dab50); FUN_00453890
// (the last stage, or a chain that stops) adds them and closes the round
// (FUN_004dab50, FUN_004dae70). Then stage 0x2d at rank 4..5
// goes to the title, the rest to the results.
void TacticsTask::route(int rank) {
    GameWork& w = ctx_.work;
    const int stage = w.s16(stage::kMap);           // FUN_00451d10
    switch (stage - 0x4c) {
        case 0: case 1: case 3: ctx_.scenes.request(SceneId::Coming); return;
        case 2: case 4: ctx_.scenes.request(SceneId::Title); return;
        case 6: ctx_.scenes.request(ctx_.comingResult == -1 ? SceneId::TitleTrial : SceneId::Coming); return;
        default: break;
    }
    const int next = w.s16(stage::kNextMap);
    bool closeRound = next == -1;
    if (next != 0 && next != -1) {
        bool standing = false;
        for (int i = 0; i < units_.partyCount(); ++i)
            if (units_.unit(i).used && units_.unit(i).data.u8(0xf) == 1) standing = true;
        closeRound = true;
        if (standing && clock_.left != 0) {
            logic::addRoundStage(w);                 // FUN_004536c0 -> FUN_004dab50
            const int count = w.u8(stage::kPartyCount), net = w.u8(stage::kNetwork);
            for (int i = 0; i < count; ++i) {
                const std::uint32_t u = stage::kParty + std::uint32_t(i) * 0xb0;
                w.set8(u + 0x9d, 0);
                w.set8(u + 0x9e, 0);
                w.set8(u + 0x9f, 2);
                for (std::uint32_t k = 0xa0; k <= 0xa3; ++k) w.set8(u + k, 0);
            }
            PreparationTask::loadStage(w, ctx_.version, next, false);
            w.set8(stage::kChained, 1);
            w.set8(stage::kPartyCount, std::uint8_t(count));
            w.set8(stage::kNetwork, std::uint8_t(net));
            SDL_Log("[tactics] stage %d chains on to %d", stage, next);
            ctx_.scenes.request(SceneId::Tactics);
            return;
        }
    }
    if (closeRound) {                                // FUN_00453890
        logic::addRoundStage(w);
        logic::finishRound(w);
    }
    if (stage == 0x2d && rank > 3 && rank < 6) ctx_.scenes.request(SceneId::Title);
    else ctx_.scenes.request(SceneId::TacticsResult);
}

} // namespace ch
