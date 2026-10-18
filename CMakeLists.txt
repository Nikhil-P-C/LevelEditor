# Level editor for the Dino game (desktop: Windows / Linux / macOS).
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#   build/level_editor            (or:  level_editor --assets <path>/app/src/main/assets)
#
# SDL3 / SDL3_image / SDL3_ttf / SDL3_mixer / enet are taken from app/src/main/cpp/vendor/ when that folder exists
# (same layout the Android build uses), otherwise from your system via find_package.
cmake_minimum_required(VERSION 3.22)
project(LevelEditor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CPP    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp)
set(ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/assets)

if(EXISTS ${CPP}/vendor/SDL3/CMakeLists.txt)
    message(STATUS "Using vendored SDL libraries in ${CPP}/vendor")
    set(SDL_ALSA OFF CACHE BOOL "" FORCE)
    set(SDL_ASAN OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CPP}/vendor/SDL3 EXCLUDE_FROM_ALL)

    set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_BMP  ON  CACHE BOOL "" FORCE)
    set(SDLIMAGE_GIF  ON  CACHE BOOL "" FORCE)
    set(SDLIMAGE_JPG  ON  CACHE BOOL "" FORCE)
    set(SDLIMAGE_PNG  ON  CACHE BOOL "" FORCE)
    set(SDLIMAGE_TIF  OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CPP}/vendor/SDL3_image EXCLUDE_FROM_ALL)

    set(SDL_DISABLE_VERSIONING ON)
    set(SDLTTF_VENDORED ON)
    add_subdirectory(${CPP}/vendor/SDL_ttf EXCLUDE_FROM_ALL)

    set(SDLMIXER_WAV ON CACHE BOOL "" FORCE)
    set(WAVPACK_BUILD_ASM OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_FLAC OFF CACHE BOOL "" FORCE)
    set(SDLMIXER_MP3 ON CACHE BOOL "" FORCE)
    set(SDLMIXER_OGG ON CACHE BOOL "" FORCE)
    set(SDLMIXER_OPUS OFF CACHE BOOL "" FORCE)
    set(BUILD_PROGRAMS OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_TESTING OFF)
    add_subdirectory(${CPP}/vendor/SDL_mixer EXCLUDE_FROM_ALL)

    file(GLOB ENET_SRC ${CPP}/vendor/enet-1.3.18/*.c)
    add_library(enet STATIC ${ENET_SRC})
    target_include_directories(enet PUBLIC ${CPP}/vendor/enet-1.3.18/include)
    set(ENET_TARGET enet)
else()
    message(STATUS "No vendor/ folder: using system SDL3 packages")
    find_package(SDL3 REQUIRED CONFIG)
    find_package(SDL3_image REQUIRED CONFIG)
    find_package(SDL3_ttf REQUIRED CONFIG)
    find_package(SDL3_mixer REQUIRED CONFIG)
    find_path(ENET_INCLUDE_DIR enet/enet.h)
    find_library(ENET_LIBRARY enet)
    if(ENET_INCLUDE_DIR AND ENET_LIBRARY)
        add_library(enet_ext INTERFACE)
        target_include_directories(enet_ext INTERFACE ${ENET_INCLUDE_DIR})
        target_link_libraries(enet_ext INTERFACE ${ENET_LIBRARY})
        set(ENET_TARGET enet_ext)
    else()
        message(FATAL_ERROR "enet not found (main.cpp includes <enet/enet.h>); install it or add app/src/main/cpp/vendor/enet-1.3.18")
    endif()
endif()

set(GAME_SOURCES
    States/InputOverlayState/ButtonOverlay.cpp
    States/InputOverlayState/SepJoysticknButton.cpp
    States/InputOverlayState/KeyboardOverlay.cpp
    States/InputOverlayState/GamepadOverlay.cpp
    States/DebugState/DebugState.cpp
    States/TransitionState/TransitionState.cpp
    States/HUDOverlayState/HUDOverlayState.cpp
    States/GameState/GameState.cpp
    States/GameState/ParticleSystem.cpp
    States/GameState/AmbientParticles.cpp
    States/MenuState/TitleScreenState.cpp
    States/MenuState/MenuState.cpp
    States/MenuState/OptionMenuState/OptionMenuState.cpp
    States/MenuState/OptionMenuState/ControlMenuState.cpp
    States/MenuState/OptionMenuState/AudioMenuState.cpp
    States/MenuState/OptionMenuState/AdvanceMenuState.cpp
    States/MenuState/EditMenuState/EditMenuState.cpp
    States/PauseState/PauseState.cpp
    States/RespawnState/RespawnState.cpp
    States/MenuState/MenuKit.cpp
    engine/Engine.cpp
    engine/AssetManager.cpp
    engine/PostProcessor.cpp
    engine/Lighting.cpp
    utils/Camera.cpp
    utils/gameMath.cpp
    utils/SpriteCollector.cpp
    utils/PlayerData.cpp
    level/GroundShapeBuilder.cpp
    level/BlockShapeBuilder.cpp
    level/WallTileBuilder.cpp
    level/Fruits.cpp
    level/Traps.cpp
    level/Enemies.cpp
    level/Interactables.cpp
    level/BackGroundBuilder.cpp
    level/ForeGroundBuilder.cpp
    level/LevelLoader/LevelLoader.cpp
)
set(EDITOR_SOURCES
    main.cpp
    editor/Gfx.cpp
    editor/LevelModel.cpp
    editor/EditorScene.cpp
    editor/EditorState.cpp
    editor/Playtest.cpp
)
list(TRANSFORM GAME_SOURCES   PREPEND ${CPP}/)
list(TRANSFORM EDITOR_SOURCES PREPEND ${CPP}/)

add_executable(level_editor ${GAME_SOURCES} ${EDITOR_SOURCES})
target_compile_definitions(level_editor PRIVATE
    LEVEL_EDITOR=1
    SDL_DISABLE_HIDAPI
    EDITOR_DEFAULT_ASSETS="${ASSETS}")
target_include_directories(level_editor PRIVATE
    ${CPP}
    ${CPP}/editor/compat        # fallbacks only (after ${CPP}): InputAutoDetect.h + case-insensitive include names
)
target_link_libraries(level_editor PRIVATE
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    SDL3::SDL3
    ${ENET_TARGET})
if(WIN32)
    target_link_libraries(level_editor PRIVATE ws2_32 winmm)
endif()
if(MSVC)
    target_compile_options(level_editor PRIVATE /utf-8)
endif()
# run from the build folder with the project's real assets
set_target_properties(level_editor PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${ASSETS})
