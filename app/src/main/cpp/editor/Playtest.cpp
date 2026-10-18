#include "editor/Playtest.h"
#include "editor/EditorState.h"

#include <filesystem>
#include <string>
#include <vector>

#include "engine/Engine.h"
#include "utils/utils.h"
#include "States/GameState/GameState.h"
#include "States/HUDOverlayState/HUDOverlayState.h"
#include "States/DebugState/DebugState.h"
#include "States/InputOverlayState/ButtonOverlay.h"
#include "States/InputOverlayState/SepJoysticknButton.h"
#include "States/InputOverlayState/KeyboardOverlay.h"
#include "States/InputOverlayState/GamepadOverlay.h"

namespace fs = std::filesystem;

namespace Playtest {

static bool s_active = false;

static bool looksLikeAssets(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p / "Levels", ec) && fs::is_directory(p / "Fonts", ec);
}

void selectAssetDirectory(int argc, char** argv) {
    std::vector<fs::path> candidates;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--assets" && i + 1 < argc) { candidates.push_back(argv[++i]); }
        else if (!a.empty() && a[0] != '-') { candidates.push_back(a); }
    }
#ifdef EDITOR_DEFAULT_ASSETS
    candidates.push_back(EDITOR_DEFAULT_ASSETS);
#endif
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    for (const char* rel : {".", "assets", "../assets", "app/src/main/assets", "../app/src/main/assets", "../../app/src/main/assets"})
        candidates.push_back(cwd / rel);
    if (const char* base = SDL_GetBasePath()) {
        const fs::path b = base;
        for (const char* rel : {".", "assets", "../assets", "../app/src/main/assets", "../../app/src/main/assets"})
            candidates.push_back(b / rel);
    }
    for (const auto& c : candidates) {
        if (looksLikeAssets(c)) {
            fs::current_path(c, ec);
            if (!ec) {
                SDL_Log("level editor: assets directory = %s", fs::current_path(ec).string().c_str());
                return;
            }
        }
    }
    SDL_Log("level editor: could not find the assets directory (needs Levels/ and Fonts/). "
            "Run with:  level_editor --assets <path to app/src/main/assets>");
}

bool start(SDL_Renderer* renderer, const std::string& levelText, int levelNumber) {
    if (s_active) return false;
    s_active = true;

    LevelLoader::setOverrideContent(levelText, levelNumber);          // GameState parses this instead of Levels/level_N.txt
    GameData& gd = GameData::getInstance();
    gd.setPlaytest(true);
    gd.setPaused(false);
    if (gd.getControlType() == ControlType::BUTTONS || gd.getControlType() == ControlType::SEP_JUMP_W_JOYSTICK)
        gd.setControlType(ControlType::KEYBOARD);           // desktop: keyboard overlay (auto-detect still follows the device)

    PlayerDetail& pd = PlayerDetail::getInstance();
    pd.setScore(0);
    pd.setPlayerHP(5);
    pd.setMagic(pd.getMaxMagic());
    InputDispatcher::getInstance().inputLogClear();

    Engine::Get().setFullscreen(true);

    auto game = std::make_unique<GameState>(renderer, 0);
    GameState* raw = game.get();
    Engine::Get().pushState(std::move(game));

    // same overlay stack TransitionState builds after a level swap: HUD, [debug], controls
    Engine::Get().pushOverlayState(std::make_unique<HUDOverlayState>(renderer));
    if (gd.isDebugEnabled())
        Engine::Get().pushOverlayState(std::make_unique<DebugState>(renderer, raw));
    switch (gd.getControlType()) {
        case ControlType::BUTTONS:             Engine::Get().pushOverlayState(std::make_unique<ButtonOverlay>(renderer)); break;
        case ControlType::SEP_JUMP_W_JOYSTICK: Engine::Get().pushOverlayState(std::make_unique<SepJoysticknButton>(renderer)); break;
        case ControlType::KEYBOARD:            Engine::Get().pushOverlayState(std::make_unique<KeyboardOverlay>(renderer)); break;
        case ControlType::GAMEPAD:             Engine::Get().pushOverlayState(std::make_unique<GamepadOverlay>(renderer)); break;
    }
    return true;
}

void finish(bool completed) {
    if (!s_active) return;
    s_active = false;
    LevelLoader::clearOverrideContent();
    Engine::Get().getAssetManager().stopMusic();     // the level's music ends with the run; the editor itself is silent
    GameData& gd = GameData::getInstance();
    gd.setPlaytest(false);
    gd.setPaused(false);
    InputDispatcher::getInstance().setInputReleased(true);
    InputDispatcher::getInstance().inputLogClear();
    Engine::Get().setFullscreen(false);
    Camera::getInstance().resetShake();
    if (editor::EditorState* ed = editor::EditorState::instance())
        ed->resumeFromPlaytest(completed);
}

void requestQuit() {
    if (editor::EditorState* ed = editor::EditorState::instance())
        ed->autosaveIfDirty();
    Engine::Get().exitEngine();
}

bool active() { return s_active; }

} // namespace Playtest
