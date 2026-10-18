#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <enet/enet.h>
#include "utils/utils.h"
#include "engine/Engine.h"
#ifdef LEVEL_EDITOR
#include "editor/Playtest.h"
#endif

//TODO : Change all unsigned int to uint32_t for type size safety
extern "C" int SDL_main(int argc, char* argv[]) {
#ifdef LEVEL_EDITOR
    // must happen before Engine::Get(): assets (Levels/, Fonts/, textures...) are opened relative to the cwd
    Playtest::selectAssetDirectory(argc, argv);
#endif
    Engine::Get().run();
    return 0;
}
