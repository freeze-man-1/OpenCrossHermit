// main.cpp — replaces WinMain (FUN_0049dd70)
//
// Dropped from the original: registry InstallPath/VoicePath lookup and the
// CD check (check.bin / data.z); the VFS asset root is the install folder.
// The SDL engine stands in for the window and device code WinMain drives:
// FUN_00450290 keeps the instance handle, FUN_00450570 opens the DirectX
// device (MessageBoxA on failure), FUN_00450730 closes it, and
// FUN_00450d30 is the window procedure (Enter, activation, quit).
#include "engine/engine_core.hpp"
#include "game/game_system.hpp"
#include "game/import/data_scene.hpp"
#include "game/import/importer.hpp"
#include "game/lang/language_pack.hpp"
#include "game/option/options.hpp"
#include "game/state/exe_image.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    // Boots into the opening like the original; --title skips it.
    // --jp / --tw pick the release (default: the last one played).
    ch::SceneId boot = ch::SceneId::Opening;
    std::string language;
    ch::GameSystem::DebugSave save;   // --load FILE.SAV resumes a save, --save FILE writes one on exit
    // --data DIR: the imported game files (default: gamedata/ next to the
    // binary, else the user's profile). --import FILE...: import in the console.
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--data") == 0) legacy::vfs::setDataRoot(argv[i + 1]);
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--import") == 0) {
            std::vector<std::string> files;
            for (int k = i + 1; k < argc && std::strncmp(argv[k], "--", 2) != 0; ++k) {
                if (!ch::lang::isPackFile(argv[k])) { files.push_back(argv[k]); continue; }
                ch::lang::Pack pack;
                std::string error;
                if (!ch::lang::installPack(argv[k], pack, error)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", argv[k], error.c_str());
                    return 1;
                }
                SDL_Log("language pack added: %s (%s)", pack.name.c_str(), pack.id.c_str());
            }
            return files.empty() ? 0 : ch::import::importCli(files);
        }
    // The window from the port's settings (next to the saves); --window WxH,
    // --scale smooth|sharp|integer|stretch, --fullscreen and --windowed override them for this run.
    ch::options::loadPort();
    auto& display = ch::options::port();
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--data") == 0) ++i;
        else if (std::strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
            if (int w = 0, h = 0; std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                display.windowW = w;
                display.windowH = h;
            }
        }
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            if (!ch::options::parseScale(argv[++i], display.scale))
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "--scale: smooth, sharp, integer or stretch");
        }
        else if (std::strcmp(argv[i], "--fullscreen") == 0) display.fullscreen = true;
        else if (std::strcmp(argv[i], "--windowed") == 0) display.fullscreen = false;
        else if (std::strcmp(argv[i], "--title") == 0) boot = ch::SceneId::Title;
        else if (std::strcmp(argv[i], "--newgame") == 0) boot = ch::SceneId::WeekStart;
        else if (std::strcmp(argv[i], "--jp") == 0) language = "jp";
        else if (std::strcmp(argv[i], "--tw") == 0) language = "tw";
        else if (std::strcmp(argv[i], "--lang") == 0 && i + 1 < argc) language = argv[++i];   // a language pack's id
        else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {  // debug: boot any scene, e.g. --scene 8
            boot = static_cast<ch::SceneId>(std::strtoul(argv[++i], nullptr, 0));
            save.sceneGiven = true;
        }
        else if (std::strcmp(argv[i], "--load") == 0 && i + 1 < argc) save.loadPath = argv[++i];
        else if (std::strcmp(argv[i], "--save") == 0 && i + 1 < argc) save.savePath = argv[++i];

    try {
        // static: must outlive main() under Emscripten's simulated infinite loop
        static legacy::Engine engine({ .title = "Cross Hermit",
                                       .logicalW = ch::kScreenW, .logicalH = ch::kScreenH,
                                       .windowScale = 1, .windowW = display.windowW, .windowH = display.windowH,
                                       .tickHz = 60.0, .scaleMode = display.scale });
        // The release's files and tables, then the game. Without any release
        // imported, and on F9, the game files screen (game/import).
        // (static, as is all it uses: the web build's main() unwinds before the loop runs)
        static ch::SceneId bootScene = boot;
        static ch::GameSystem::DebugSave debugSave = save;
        static std::function<void(const std::string&)> launch = [](const std::string& language) {
            auto& languages = ch::options::languages();
            languages.all.clear();
            for (const auto& c : ch::lang::choices(ch::import::installed())) languages.all.push_back({c.id, c.name});
            languages.current = language;
            const ch::GameVersion v = ch::lang::versionOf(language);
            ch::lang::stopPainting();      // (it reads through the variant's files)
            legacy::vfs::setVariant(v == ch::GameVersion::Tw ? "tw" : "jp");
            ch::lang::activate(language);
            ch::exe::reload();
            SDL_ShowCursor(SDL_DISABLE);   // the title draws its own cursor sprite
            engine.setScene(std::make_unique<ch::GameSystem>(engine, bootScene, v, debugSave));
            bootScene = ch::SceneId::Title;   // after the game files screen: back to the title
            debugSave = {};
        };
        // The option screen's language switch: back on that screen, in the new release.
        ch::options::languages().switchTo = [](const std::string& id) {
            ch::lang::savePreferred(id);
            bootScene = ch::SceneId::Option;
            engine.setScene(std::make_unique<ch::SwitchScene>(launch, id));
        };
        engine.setHotkey(SDL_SCANCODE_F9, [] {
            const auto have = ch::import::installed();
            engine.setScene(std::make_unique<ch::DataScene>(launch, have.jp || have.tw));
        });
        // Any release, any screen.
        engine.setHotkey(SDL_SCANCODE_F11, [] {
            ch::options::port().fullscreen = !engine.fullscreen();
            engine.setFullscreen(ch::options::port().fullscreen);
            ch::options::savePort();
        });
#ifndef __EMSCRIPTEN__
        if (ch::options::port().fullscreen) engine.setFullscreen(true);   // (a browser refuses without a key or click)
#endif
        const auto have = ch::import::installed();
        const auto wanted = [&] {
            for (const auto& c : ch::lang::choices(have))
                if (c.id == language) return true;
            return false;
        };
        if (wanted()) launch(language);
        else if (have.jp || have.tw) launch(ch::lang::preferred(have));
        else engine.setScene(std::make_unique<ch::DataScene>(launch, false));
        engine.run();
    } catch (const std::exception& ex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "fatal: %s", ex.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cross Hermit", ex.what(), nullptr);
        return 1;
    }
    return 0;
}
