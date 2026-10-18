//
// EditorScene: turns the editor's LevelModel into the game's real runtime objects and draws them with the
// game's own builders (background, wall + ground autotiling, platforms, blocks, traps, fruits, enemies, foreground).
//
// Flow: model -> level text -> LevelLoader (the game's parser) -> builders.init() like GameState::setLevel().
// Nothing is simulated: traps/enemies stay at their placed positions so they can be edited.
//
#pragma once
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>
#include "editor/LevelModel.h"
#include "level/Traps.h"
#include "level/Fruits.h"
#include "level/Enemies.h"
#include "level/BlockShapeBuilder.h"
#include "level/BackGroundBuilder.h"
#include "level/ForeGroundBuilder.h"
#include "level/WallTileBuilder.h"
#include "level/GroundShapeBuilder.h"
#include "level/CheckPoint.h"

namespace editor {

struct SceneLayers {
    bool background = true;
    bool foreground = true;
    bool walls = true;
};

class EditorScene {
public:
    // parse + build everything from the model (cheap enough to call after every edit)
    // parallax=false flattens backgrounds / foregrounds to factor 1 (they then behave like ordinary world objects)
    void rebuild(const LevelModel& model, int levelNumber, bool parallax = true);

    // Where the "game camera" sits relative to the editor view, in world px (dx = gameCamX - viewCamX).
    // Parallax layers are drawn as the player would see them from that camera, instead of being pinned to the
    // top-left corner of the editor view (which made them shrink into the corner and vanish when zooming out).
    void setParallaxOffset(float dx, float dy) { m_parDX = dx; m_parDY = dy; }

    void update(float dt);
    // draws through Camera::getInstance() exactly like GameState::render() does (no player)
    void render(SDL_Renderer* renderer, const SceneLayers& layers);

    // real on-screen size of traps / enemies (they depend on their sprite), keyed by model object id
    bool boundsFor(int objId, SDL_FRect& out) const;

private:
    std::vector<Platform> m_platforms;
    std::vector<LevelGround> m_levelWalls;
    std::vector<LevelGround> m_grounds;
    std::vector<Block> m_blocks;
    std::vector<Trap> m_traps;
    std::vector<Enemy> m_enemies;
    std::vector<Fruit> m_fruits;
    std::vector<BackGroundElement> m_background;
    std::vector<ForeGroundElement> m_foreground;
    std::vector<PrecomputedWallTile> m_wallTiles;
    GroundShape m_wallShape;
    CheckPoint m_checkPoint;

    FruitBuilder m_fruitBuilder;
    TrapBuilder m_trapBuilder;
    EnemiesBuilder m_enemyBuilder;
    BlockBuilder m_blockBuilder;
    BackGroundBuilder m_backgroundBuilder;
    ForeGroundBuilder m_foregroundBuilder;

    std::unordered_map<int, SDL_FRect> m_bounds;
    float m_parDX = 0.0f, m_parDY = 0.0f;
};

} // namespace editor
