//
// Level editor <-> game glue. Included by the game states that need to hand control back to the editor
// (PauseState / RespawnState "exit", GameState level complete) when GameData::isPlaytest() is set.
//
#pragma once
#include <string>
#include <SDL3/SDL.h>

namespace Playtest {

// picks the directory that holds Levels/, Fonts/, ... and makes it the working directory
// (--assets <dir>, a path argument, EDITOR_DEFAULT_ASSETS, or a few common relative locations)
void selectAssetDirectory(int argc, char** argv);

// pushes the real GameState + HUD + input overlay for `levelText`, fullscreen.
// levelNumber = the level being edited (names the default music file Sound/Music/level_<n>.ogg)
bool start(SDL_Renderer* renderer, const std::string& levelText, int levelNumber = 0);

// called by the game when the run is over (Pause -> EXIT, Respawn -> EXIT, or checkpoint reached)
void finish(bool completed);

// window close: autosave unsaved editor work, then quit
void requestQuit();

bool active();

} // namespace Playtest
