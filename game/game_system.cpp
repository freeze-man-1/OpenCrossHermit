// ============================================================================
// game_system.cpp
// ============================================================================
#include <cstdlib>
#include <vector>
#include <stdexcept>
#include <iterator>
#include <fstream>
#include "game_system.hpp"
#include "sprite_bank.hpp"
#include "adv/adv_scene.hpp"
#include "han/han_task.hpp"
#include "kojin/kojin_task.hpp"
#include "preparation/preparation_task.hpp"
#include "saveload/saveload_task.hpp"
#include "allresult/allresult_task.hpp"
#include "tacticsresult/tacticsresult_task.hpp"
#include "coming_task.hpp"
#include "ed/ed_task.hpp"
#include "net/net_task.hpp"
#include "option/option_task.hpp"
#include "tactics/tactics_task.hpp"
#include "option/options.hpp"
#include "op/op_task.hpp"
#include "title_task.hpp"
#include "demo/demo_task.hpp"
#include "state/game_logic.hpp"
#include "workroom/workroom_task.hpp"

namespace ch {

GameSystem::GameSystem(legacy::Engine& engine, SceneId bootScene, GameVersion version, DebugSave save)
    : ctx_{.engine = engine,
           .version = version,
           .sprites = std::make_unique<BankSprites>(engine.renderer()),
           .audio = makeAudio()},
      bootScene_(bootScene), save_(std::move(save)) {}

GameSystem::~GameSystem() {
    if (!save_.savePath.empty()) {
        const std::vector<std::uint8_t> bytes = logic::saveGame(ctx_.work);
        std::ofstream f(save_.savePath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        SDL_Log("[save] wrote %s (%zu bytes)", save_.savePath.c_str(), bytes.size());
    }
    // The scene tasks release their banks and sounds as they go: end them
    // while the sprite banks and the audio still exist (GameContext declares
    // the task manager before them, so it would otherwise outlive them).
    ctx_.tasks.killAll();
}

std::unique_ptr<IAudio> GameSystem::makeAudio() {
#ifdef CH_HAVE_MIXER
    if (auto mixer = makeMixerAudio()) return mixer;
#endif
    SDL_Log("[audio] running silent (NullAudio)");
    return std::make_unique<NullAudio>();
}

// FUN_0049e1b0: FUN_004e1a10 (the boot reset, with the options' reset),
// FUN_004e1b60 (the new game's work), the options applied, loaded and
// applied again, the system sounds (FUN_0040c780), the opening.
// Still to port from it: FUN_00415480(1, 10).
void GameSystem::onEnter(legacy::Engine&) {
    logic::bootReset(ctx_.work);       // FUN_004e1a10
    options::reset(ctx_.work);         // FUN_004e1a10 -> FUN_004e1ad0 -> FUN_004e2c60
    logic::newGame(ctx_.work, ctx_.version);   // FUN_004e1b60
    options::load(ctx_.work);          // FUN_004e2f50
    options::apply(ctx_.work, *ctx_.audio, ctx_.version);   // FUN_004e2d00
    ctx_.audio->loadSystemSe();        // FUN_0040c780
    if (!save_.loadPath.empty()) {
        std::ifstream f(save_.loadPath, std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
        if (!logic::loadGame(ctx_.work, bytes))
            throw std::runtime_error("not a valid Cross Hermit save: " + save_.loadPath);
        SDL_Log("[save] loaded %s: month %d week %d", save_.loadPath.c_str(), ctx_.work.month(), ctx_.work.week());
        ctx_.scenes.request(save_.sceneGiven ? bootScene_ : SceneId::HanKojin);
        return;
    }
    // Booting past the title (--newgame, --scene N) skips the menu's New Game
    // reset, which would leave the calendar at month 0 / day 0.
    switch (bootScene_) {
        case SceneId::Opening: case SceneId::Title: case SceneId::TitleTrial: case SceneId::Option: break;
        case SceneId::WeekStart: logic::newGame(ctx_.work, ctx_.version); break;
        default:   // a mid-week scene: also run WeekStart's advance (week 1, area BGM)
            logic::newGame(ctx_.work, ctx_.version);
            logic::advanceWeek(ctx_.work, ctx_.version);
    }
    ctx_.scenes.request(bootScene_);   // original: FUN_00439e30(0) -> Opening
}

// FUN_0049e2b0: one scene task is created per request, at priority 0.
void GameSystem::dispatchScene(SceneId id) {
    // Debug: CH_EXIT_SCENE=<n> quits (saving with --save) when scene n is
    // requested, so test runs stop at a known point.
    if (const char* e = SDL_getenv("CH_EXIT_SCENE"); e != nullptr && std::strtol(e, nullptr, 0) == long(id)) {
        SDL_Log("[debug] scene 0x%02x requested: exiting", unsigned(id));
        ctx_.quitRequested = true;
        return;
    }
    switch (id) {
        case SceneId::Opening:       // FUN_00506150: new (FUN_00506240) and link, start(0)
            ctx_.tasks.link(std::make_unique<OpTask>(ctx_), 0, 0);
            break;
        case SceneId::WeekStart:       // FUN_0049f570: new (FUN_0049f410) and link, start(2)
            ctx_.tasks.link(std::make_unique<WeekStartTask>(ctx_), 0, 2);
            break;
        case SceneId::AdvScript:
            ctx_.tasks.link(std::make_unique<AdvTask>(ctx_), 0, 0);
            break;
        case SceneId::WorkRoom:       // FUN_004a16c0: new (FUN_004a1350) and link, start(2)
            ctx_.tasks.link(std::make_unique<WorkRoomTask>(ctx_), 0, 2);
            break;
        case SceneId::HanKojin:      // FUN_004aba70 + FUN_004b89a0: new and link both, start(2), side by side
            ctx_.tasks.link(std::make_unique<HanTask>(ctx_), 0, 2);
            ctx_.tasks.link(std::make_unique<KojinTask>(ctx_), 0, 2);
            break;
        case SceneId::AllResult:       // FUN_004c1d30: new (FUN_004c1910) and link, start(2)
            ctx_.tasks.link(std::make_unique<AllResultTask>(ctx_), 0, 2);
            break;
        case SceneId::Tactics:
            // CH_NET: the battle waits for the room's start (netDebug).
            if (SDL_getenv("CH_NET") && ctx_.net.state() != net::Session::State::InGame) {
                netBattleHeld_ = true;
                SDL_Log("[net] the battle waits for the room to start");
                break;
            }
            ctx_.tasks.link(std::make_unique<TacticsTask>(ctx_), 0, 0);
            break;
        case SceneId::Option:       // FUN_0044d280: new (FUN_0044d370) and link, start(0)
            ctx_.tasks.link(std::make_unique<OptionTask>(ctx_), 0, 0);
            break;
        case SceneId::TacticsResult:       // FUN_004bd6f0: new (FUN_004bd540) and link, start(2)
            ctx_.tasks.link(std::make_unique<TacticsResultTask>(ctx_), 0, 2);
            break;
        case SceneId::SaveLoad:       // FUN_004d9f30: new (FUN_004d9b20) and link, start(2)
            ctx_.tasks.link(std::make_unique<SaveLoadTask>(ctx_), 0, 2);
            break;
        case SceneId::Preparation:       // FUN_004b9120: new (FUN_004b8f20) and link, start(2)
            ctx_.tasks.link(std::make_unique<PreparationTask>(ctx_), 0, 2);
            break;
        case SceneId::Coming:       // FUN_0042fcd0: new (FUN_0042fdc0) and link, start(0)
            ctx_.tasks.link(std::make_unique<ComingTask>(ctx_), 0, 0);
            break;
        case SceneId::Ending:       // FUN_004fb1e0: new (FUN_004fb2d0) and link, start(0)
            ctx_.tasks.link(std::make_unique<EdTask>(ctx_), 0, 0);
            break;
        case SceneId::NetHost:       // FUN_004439d0: new (FUN_00443c10) and link, start(0)
            ctx_.tasks.link(std::make_unique<NetTask>(ctx_), 1, 0);
            break;
        case SceneId::NetJoin:       // FUN_00443ac0: the same task, start(1): back to the room
            // CH_NET with --scene 15: the room screen once netDebug's room is joined.
            if (SDL_getenv("CH_NET") && ctx_.net.state() != net::Session::State::InRoom && ctx_.net.state() != net::Session::State::InGame) {
                netRoomHeld_ = true;
                break;
            }
            ctx_.tasks.link(std::make_unique<NetTask>(ctx_), 1, 1);
            break;
        case SceneId::Title:       // FUN_0045e8d0: new (FUN_0045e9c0) and link, start(0)
            ctx_.tasks.link(std::make_unique<TitleTask>(ctx_), 0, 0);
            break;
        case SceneId::AdvMenu:     // FUN_004ce210("data\\adv\\dat\\menu001.ybc", 0x16) + FUN_004d1820: the chapter select,
                                   // run again after each script it starts (nothing in the exe requests it; the port's Options do)
            ctx_.adv = AdvRequest{"data\\adv\\dat\\menu001.ybc", static_cast<std::int32_t>(SceneId::AdvMenu)};
            ctx_.tasks.link(std::make_unique<AdvTask>(ctx_), 0, 0);
            break;
        case SceneId::Demo:        // FUN_004e3890: new (FUN_004e3980) and link, start(0)
            ctx_.tasks.link(std::make_unique<DemoTask>(ctx_), 0, 0);
            break;
        case SceneId::TitleTrial:  // FUN_00460040: new (FUN_00460130) and link, start(0)
            ctx_.tasks.link(std::make_unique<TitleTask>(ctx_, true), 0, 0);
            break;
        default:
            SDL_Log("scene 0x%02x (%s) not ported yet", unsigned(id), sceneName(id));
            ctx_.tasks.link(std::make_unique<StubSceneTask>(ctx_, id), 0, 0);
            break;
    }
}

// Debug network play until the network game task (NetTask) is ported:
//   CH_NET=ws://host:8787/  CH_NET_ROOM=new | <invite code>  [CH_NET_NAME=...]
// "new" creates a room (its code goes to the log) and starts the game once
// everyone in it is ready; a code joins that room and readies at once. Run
// each player with the same save, CH_MAP and --scene 10: the battle the
// preparation asks for waits for the room's start. With --scene 15 instead
// the room screen opens once the room is joined (the ready-made units and
// the host's map, CH_MOUSE to pick another) and starts the battle itself.
void GameSystem::netDebug() {
    static const char* url = SDL_getenv("CH_NET");
    if (!url) return;
    net::Session& s = ctx_.net;
    static const std::string room = SDL_getenv("CH_NET_ROOM") ? SDL_getenv("CH_NET_ROOM") : "new";
    static bool asked = false, readied = false;
    if (s.state() == net::Session::State::Offline) {
        const char* name = SDL_getenv("CH_NET_NAME");
        s.connect(url, name ? name : "Player");
        SDL_Log("[net] connecting to %s", url);
        return;
    }
    if (s.state() == net::Session::State::Failed) {
        static bool told = false;
        if (!told) SDL_Log("[net] %s", s.lastError().c_str());
        told = true;
        return;
    }
    if (s.state() == net::Session::State::Lobby && !asked) {
        asked = true;
        if (room == "new") s.createRoom("debug room", 4, true);
        else s.joinRoom(room);
    }
    if (s.state() == net::Session::State::InRoom) {
        static std::string shown;
        if (shown != s.room().code) {
            shown = s.room().code;
            SDL_Log("[net] in room %s as player %d (%s)", shown.c_str(), s.room().you, s.isHost() ? "host" : "guest");
        }
        if (!s.isHost() && !readied) {
            readied = true;
            s.setReady(true);
        }
        if (netRoomHeld_) {
            netRoomHeld_ = false;
            dispatchScene(SceneId::NetJoin);
        }
        // The host starts once everyone is ready (and, from the room screen,
        // has sent units and the map).
        static std::size_t lastCount = 0;
        bool all = s.room().members.size() >= 2;
        for (const auto& m : s.room().members)
            all = all && (m.slot == s.room().host || m.ready) && (netBattleHeld_ || !m.data.empty());
        all = all && (netBattleHeld_ || !s.room().options.empty());
        if (s.isHost() && all && lastCount != s.room().members.size()) {
            lastCount = s.room().members.size();
            s.startGame();
        }
    }
    if (s.startedPending && netBattleHeld_) {   // (else the room screen takes it)
        s.startedPending = false;
        SDL_Log("[net] the game starts: player %d of %d, input delay %d turns", s.room().you, s.slots, s.lockstep.inputDelay());
        netBattleHeld_ = false;
        dispatchScene(SceneId::Tactics);
    }
    for (const auto& a : s.alerts()) SDL_Log("[net] turn %u: %s (suspects %02x)", a.turn, a.text.c_str(), a.suspects);
    s.alerts().clear();
}

// One call == one original frame (FUN_0049e2b0 followed by FUN_0049e540).
void GameSystem::fixedUpdate(legacy::Engine& engine, double) {
    ctx_.net.poll(SDL_GetTicks());
    netDebug();
    if (auto next = ctx_.scenes.take()) dispatchScene(*next);

    ctx_.draw.clear();
    engine.setPieces({});   // a scene with a split view sets it again
    ctx_.tasks.runFrame();
    ctx_.audio->tick();
    presented_ = ctx_.draw;
    // Debug: CH_DUMP=<tick> writes that frame's draw list to stderr.
    static const long long dumpTick = [] { const char* v = SDL_getenv("CH_DUMP"); return v ? std::atoll(v) : -1LL; }();
    if (dumpTick >= 0 && engine.tickCount() == static_cast<std::uint64_t>(dumpTick)) presented_.dump(stderr, 0);

    if (ctx_.quitRequested) engine.requestQuit();   // WinMain: +0x8e0 -> DestroyWindow
}

void GameSystem::render(legacy::Engine& engine, SDL_Renderer* r, double) {
    // Debug: CH_PERF logs the draw list's size and the time to render it.
    static const bool perf = SDL_getenv("CH_PERF") != nullptr;
    const std::uint64_t t0 = perf ? SDL_GetPerformanceCounter() : 0;
    if (!engine.split()) {
        presented_.render(r, *ctx_.sprites);
    } else {
        // A split view: each layer into its own frame (Engine puts the pieces together).
        SDL_Texture* frame = SDL_GetRenderTarget(r);
        presented_.render(r, *ctx_.sprites, 0);
        for (int layer = 1; layer < legacy::Engine::kLayers; ++layer) {
            SDL_SetRenderTarget(r, engine.layerTarget(layer));
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
            SDL_RenderClear(r);
            presented_.render(r, *ctx_.sprites, layer);
        }
        SDL_SetRenderTarget(r, frame);
    }
    if (perf) {
        static std::uint64_t total = 0;
        static int frames = 0;
        total += SDL_GetPerformanceCounter() - t0;
        if (++frames == 300) {
            SDL_Log("[perf] render: %.2f ms per frame, %zu draw entries", double(total) * 1000.0 / double(SDL_GetPerformanceFrequency()) / frames,
                    presented_.size());
            total = 0;
            frames = 0;
        }
    }
}

// ------------------------------------------------------- StubSceneTask ----
Co<> StubSceneTask::start(std::uint32_t) {
    for (;;) {
        ctx_.draw.fill({.x = 0, .y = 0, .w = kScreenW, .h = kScreenH, .priority = 100,
                        .argb = 0xFF000000u | (unsigned(id_) * 0x0A1B2Cu & 0x7F7F7Fu)});
        ctx_.draw.text({.x = 32, .y = 32, .priority = 1, .argb = 0xFFFFFFFF,
                        .text = std::string("Scene ") + sceneName(id_) + " is not ported yet - click to return",
                        .fontPx = 20});
        if (ctx_.mouse().released & legacy::kMouseLeft) {
            ctx_.scenes.request(SceneId::Title);
            co_return;
        }
        co_await yieldFrame();
    }
}

} // namespace ch
