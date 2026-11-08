// ============================================================================
// game_system.hpp — WinMain's per-frame body as an IScene
//
//   FUN_0049dd70  WinMain            -> main.cpp + GameSystem ctor
//   FUN_0049e1b0  boot init          -> GameSystem::onEnter
//   FUN_0049e2b0  scene dispatcher   -> GameSystem::dispatchScene
//   FUN_0049e540  task exec + render -> GameSystem::fixedUpdate / render
// ============================================================================
#pragma once
#include "game_context.hpp"

#include <memory>
#include <string>

namespace ch {

class GameSystem final : public legacy::IScene {
public:
    // Debug options: loadPath resumes a .SAV file (scene 9 unless bootScene is
    // given), savePath writes the game state there when the game exits.
    struct DebugSave { std::string loadPath, savePath; bool sceneGiven = false; };
    GameSystem(legacy::Engine& engine, SceneId bootScene, GameVersion version, DebugSave save);
    ~GameSystem() override;

    void onEnter(legacy::Engine&) override;
    void fixedUpdate(legacy::Engine& engine, double dt) override;
    void render(legacy::Engine&, SDL_Renderer* r, double alpha) override;

private:
    static std::unique_ptr<IAudio> makeAudio();
    void dispatchScene(SceneId id);
    void netDebug();                         // CH_NET: a room without the network menus
    bool netBattleHeld_ = false;             // the battle waiting for the room to start
    bool netRoomHeld_ = false;               // the room screen waiting for netDebug's room

    GameContext ctx_;
    SceneId     bootScene_;
    DebugSave   save_;
    DrawList    presented_;   // last completed frame, redrawn if no tick ran
};

// Shown for scenes that are not ported yet; left click returns to the title.
class StubSceneTask final : public Task {
public:
    StubSceneTask(GameContext& ctx, SceneId id) : ctx_(ctx), id_(id) {}
    Co<> start(std::uint32_t) override;
private:
    GameContext& ctx_;
    SceneId      id_;
};

} // namespace ch
