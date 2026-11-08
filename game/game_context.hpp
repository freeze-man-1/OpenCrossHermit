// ============================================================================
// game_context.hpp — what the original reached through its two big globals
//
//   DAT_007a4a00  GAME_WORK_SYSTEM (0x1110 bytes): scene switch, quit flag
//   DAT_007a49fc  WinCon (0xba20 bytes): D3D, sprite lists, input, sound
//
// Tasks get a GameContext& instead of touching globals.
// ============================================================================
#pragma once
#include "net/net_session.hpp"
#include "adv/ybc_vm.hpp"
#include "state/game_work.hpp"
#include "audio.hpp"
#include "draw_list.hpp"
#include "task_system.hpp"
#include "../engine/engine_core.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace ch {

inline constexpr int kScreenW = 1024;   // FUN_00450350(0,0,0x400,0x300,...)
inline constexpr int kScreenH = 768;

// Values of GAME_WORK_SYSTEM+0x30 handled by the dispatcher (FUN_0049e2b0).
// Named only where the code proves the meaning; the rest keep their numbers.
// Which executable's behaviour to reproduce. The two builds share the data
// files' formats but differ in layout tables (localized picture rects) and
// in a few code paths.
//   Jp  Cross Hermit.exe, Enterbrain 2002 (release build)
//   Tw  ch.exe, Traditional Chinese 2004 (debug build)
// Both report VERSION 1.2.
enum class GameVersion : std::uint8_t { Jp, Tw };

// Scene ids handled by FUN_0049e2b0 (requested through FUN_00439e30).
enum class SceneId : std::uint32_t {
    Opening        = 0x00,   // OpTask            src/HATA/OpTask.cpp (boot)
    Title          = 0x01,   // TitleTask         src/YOSHIDA/TitleTask.cpp
    TitleTrial     = 0x02,   // TitleTrialTask    src/YOSHIDA/TitleTrialTask.cpp (trial build title)
    Option         = 0x03,   // OptionTask        src/YOSHIDA/OptionTask.cpp
    AdvScript      = 0x06,   // YbcTask           src/YOSHIMARU/YBC/YbcTask.cpp
    WeekStart      = 0x07,   // WeekStartTask     src/yoshimaru/weekstart (new game and every week)
    WorkRoom       = 0x08,   // WorkRoomTask      src/yoshimaru/workroom
    HanKojin       = 0x09,   // HanTask + KojinTask  src/YOSHIMARU/HAN, src/yoshimaru/kojin
    Preparation    = 0x0A,   // PreparationTask   src/yoshimaru/preparation
    TacticsResult  = 0x0B,   // TacticsResultTask src/YOSHIMARU/TacticsResult
    AllResult      = 0x0C,   // AllResultTask     src/YOSHIMARU/AllResult
    SaveLoad       = 0x0D,   // SLProgTask        src/yoshimaru/slprog (title: Load Game)
    NetHost        = 0x0E,   // NetTask (FUN_004439d0)  src/YOSHIDA/NetTask.cpp (title: Multi)
    NetJoin        = 0x0F,   // NetTask (FUN_00443ac0)
    Tactics        = 0x10,   // TacticsTask       src/yoshida/TacticsTask.cpp (JP title: Tutorial, map 0x50)
    Coming         = 0x11,   // ComingTask        src/yoshida/comingtask.cpp
    Ending         = 0x12,   // EdTask            src/hata/edtask.cpp
    Scene14        = 0x14,   // FUN_0049e880 -> FUN_0045a2c0 (no task)
    AdvMenu        = 0x16,   // YbcTask on data\adv\dat\menu001.ybc
    Demo           = 0x17,   // DemoTask          src/hata/demotask.cpp
    AdvError       = 0x18,   // message box, then the error script
};

[[nodiscard]] constexpr const char* sceneName(SceneId id) noexcept {
    switch (id) {
        case SceneId::Opening: return "Opening";           case SceneId::Title: return "Title";
        case SceneId::TitleTrial: return "TitleTrial";     case SceneId::Option: return "Option";
        case SceneId::AdvScript: return "AdvScript";       case SceneId::WeekStart: return "WeekStart";
        case SceneId::WorkRoom: return "WorkRoom";         case SceneId::HanKojin: return "Han/Kojin";
        case SceneId::Preparation: return "Preparation";   case SceneId::TacticsResult: return "TacticsResult";
        case SceneId::AllResult: return "AllResult";       case SceneId::SaveLoad: return "SaveLoad";
        case SceneId::NetHost: return "NetHost";           case SceneId::NetJoin: return "NetJoin";
        case SceneId::Tactics: return "Tactics";           case SceneId::Coming: return "Coming";
        case SceneId::Ending: return "Ending";             case SceneId::Scene14: return "Scene14";
        case SceneId::AdvMenu: return "AdvMenu";           case SceneId::Demo: return "Demo";
        case SceneId::AdvError: return "AdvError";
    }
    return "?";
}

// FUN_00439e30: one pending scene request at a time (the original asserts).
// The original's request record (gamectrl.cpp): FUN_00439d10 builds it
// with nothing pending (id -1) and a critical section, FUN_00439d90 ->
// FUN_00439dd0 tears it down; the port runs on one thread and needs no
// lock. Every scene's base class (GameTask) logs "GameTask(%08x): start /
// end" in FUN_00439ef0 / FUN_00439f80 and has an empty hook
// (FUN_0043a030); the port's scenes have no such base.
class SceneDirector {
public:
    void request(SceneId next) noexcept {
        if (pending_) SDL_Log("WARNING: scene request 0x%02x overrides pending 0x%02x",
                              unsigned(next), unsigned(*pending_));
        pending_ = next;
    }
    [[nodiscard]] std::optional<SceneId> take() noexcept { return std::exchange(pending_, std::nullopt); }
private:
    std::optional<SceneId> pending_;
};

// Globals not identified yet keep their address as their name.
struct LegacyGlobals {
    std::uint32_t titleVisits  = 0;   // DAT_007a49cc, ++ in ~TitleTask
    std::uint32_t trialTitleVisits = 0;   // DAT_007a49d4, ++ in ~TitleTask (trial)
};

// Script queued for the ADV scene (FUN_004ce210) before switching to scene 6.
struct AdvRequest {
    std::string   path;
    std::int32_t  entry = -1;
};

struct GameContext {
    legacy::Engine&               engine;
    GameVersion                   version = GameVersion::Jp;
    TaskManager                   tasks{};
    DrawList                      draw{};
    std::unique_ptr<ISpriteBanks> sprites;
    std::unique_ptr<IAudio>       audio;
    SceneDirector                 scenes{};
    LegacyGlobals                 globals{};
    bool                          quitRequested = false;   // GAME_WORK_SYSTEM+0x8e0
    int                           comingResult = -1;       // DAT_005ff720: how a trial map ended (-1 none)
    GameWork                      work{};                   // game-state globals (0x7a4e00..0x7f7c18)
    AdvRequest                    adv{};
    net::Session                  net{};                    // the relay (network play; game/net)

    [[nodiscard]] const legacy::MouseState& mouse() const noexcept { return engine.input().mouse(); }
    // FUN_004128f0(0x39, 0): Space held fast-forwards dialogue.
    [[nodiscard]] bool skipHeld() const noexcept { return engine.input().held(legacy::Action::Skip); }

    // FUN_004500b0: InstallPath + name. Install root == the VFS asset root.
    // FUN_00450110 / FUN_00450170 / FUN_004501d0 join a name to the same
    // install root (+0xcf8, a copy of +0xbf4) for sounds and the like, so
    // they resolve here too; FUN_00450230 joins it to "SaveData\\" (+0x1004),
    // which is the port's save directory (CH_SAVEDIR).
    [[nodiscard]] std::vector<std::uint8_t> readData(std::string_view legacyPath) const {
        return legacy::vfs::readAsset(legacyPath);
    }
};

} // namespace ch
