// ============================================================================
// data_scene.hpp — the game files screen: import, and pick the release
// ============================================================================
// Shown at start when no release is imported, and on F9 at any time. Files
// are dropped onto the window (desktop) or chosen with the page's buttons
// (web: they arrive through ch_web_add_file).
#pragma once
#include "importer.hpp"
#include "../lang/language_pack.hpp"
#include "../game_context.hpp"
#include "../../engine/engine_core.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ch {

class TextRenderer;

class DataScene final : public legacy::IScene {
public:
    using Launch = std::function<void(const std::string& language)>;   // a lang::Choice's id
    DataScene(Launch launch, bool canReturn);
    ~DataScene() override;
    void onEnter(legacy::Engine& e) override;
    void fixedUpdate(legacy::Engine& e, double dt) override;
    void render(legacy::Engine& e, SDL_Renderer* r, double alpha) override;

private:
    struct Button { SDL_Rect r; std::string label; bool enabled; std::function<void()> click; int px = 18; };
    void startSync(std::string doneMessage);       // web: keep the changes in the browser
    void layout();
    void text(SDL_Renderer* r, const std::string& s, int x, int y, int px, SDL_Color c, bool bold = false);

    Launch launch_;
    bool canReturn_;
    import::Importer imp_;
    import::Installed have_;
    std::unique_ptr<TextRenderer> text_;
    std::vector<Button> buttons_;
    std::string message_;
    std::string syncedMessage_;
    std::vector<lang::Pack> packs_;
    bool syncing_ = false;
    int hover_ = -1;
};

// Starts a release from a running game. The old game has to be gone first (a
// new one opens the audio device, which the old one closes when it ends), so
// this scene takes its place for a frame and launches from there.
class SwitchScene final : public legacy::IScene {
public:
    SwitchScene(DataScene::Launch launch, std::string language) : launch_(std::move(launch)), language_(std::move(language)) {}
    void fixedUpdate(legacy::Engine&, double) override {
        if (done_) return;
        done_ = true;
        launch_(language_);
    }
    void render(legacy::Engine&, SDL_Renderer*, double) override {}
private:
    DataScene::Launch launch_;
    std::string       language_;
    bool              done_ = false;
};

} // namespace ch
