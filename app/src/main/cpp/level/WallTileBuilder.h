//
// Wall autotiling ('W' lines), shared by GameState and the level editor.
// This is GameState::buildWallTiles() moved out unchanged so both draw walls exactly the same way.
//
#pragma once
#include <vector>
#include <SDL3/SDL.h>
#include "level/GroundShapeBuilder.h"

struct PrecomputedWallTile{
    SDL_FRect src;
    float x;
    float y;
    bool flip;
};

// tileSize = source tile px (16), scale = draw scale (4): every tile is drawn tileSize*scale px
std::vector<PrecomputedWallTile> buildWallTileList(const std::vector<LevelGround>& walls, int tileSize, float scale);
