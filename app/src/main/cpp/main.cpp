
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include "engine/Engine.h"
extern "C" int SDL_main(int argc, char* argv[]) {

    Engine::Get().run();

    return 0;
}
