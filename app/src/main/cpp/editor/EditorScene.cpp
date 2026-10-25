#include "editor/EditorScene.h"
#include "States/GameState/GameState.h"     // TILE_SIZE, SCALE, Camera, builders
#include "level/LevelLoader/LevelLoader.h"
#include <cmath>
#include <utility>

namespace editor {

void EditorScene::rebuild(const LevelModel& model, int levelNumber, bool parallax) {
    LevelLoader loader;
    loader.loadLevelFromString(model.serialize(levelNumber));     // the game's own parser

    m_checkPoint = loader.getCheckPoint();
    m_blocks = loader.getBlocks();
    m_levelWalls = loader.getLevelWalls();
    m_platforms = loader.getPlatforms();
    m_grounds = loader.getLevelGrounds();
    m_fruits = loader.getFruits();
    m_traps = loader.getTraps();
    m_enemies = loader.getEnemies();
    m_background = loader.getBackGroundElements();
    m_foreground = loader.getForeGroundElements();

    if (!parallax) {
        for (auto& b : m_background) { b.parallaxFloatX = 1.0f; b.parallaxFloatY = 1.0f; }
        for (auto& f : m_foreground) { f.parallaxFloatX = 1.0f; f.parallaxFloatY = 1.0f; }
    }

    m_blockBuilder.init(m_blocks, TILE_SIZE, SCALE);
    m_fruitBuilder.init(m_fruits);
    m_trapBuilder.init(m_traps);
    m_enemyBuilder.init(m_enemies);
    GroundShapeBuilder groundBuilder;
    m_wallShape = groundBuilder.build(m_grounds, TILE_SIZE, (int)SCALE);     // ground autotiling (G)
    m_backgroundBuilder.init(m_background);
    m_foregroundBuilder.init(m_foreground);
    m_wallTiles = buildWallTileList(m_levelWalls, TILE_SIZE, SCALE);         // wall autotiling (W)

    // model object -> real sprite rectangle for traps and enemies (same order as the file)
    m_bounds.clear();
    size_t trapIdx = 0, enemyIdx = 0, bossIdx = 0;
    const std::vector<Boss>& bosses = loader.getBosses();       // same order as the K lines of the file
    const auto& traps = m_trapBuilder.getTraps();
    const auto& enemies = m_enemyBuilder.getEnemies();
    for (const auto& o : model.objs) {
        if (o.kind == Kind::TRAP && trapIdx < traps.size()) {
            const Trap& t = traps[trapIdx++];
            if (const auto* inf = getTrapFrameInfo(t.type, t.status)) {
                float bw = inf->frameW * SCALE, bh = inf->frameH * SCALE;
                // a trap turned to face left / right stands on its side: the footprint swaps width and height
                if (trapSupportsOrientation(t.type) &&
                    (t.orientation == TrapOrientation::LEFT || t.orientation == TrapOrientation::RIGHT))
                    std::swap(bw, bh);
                m_bounds[o.id] = {t.x, t.y, bw, bh};
            }
        } else if (o.kind == Kind::BOSS && bossIdx < bosses.size()) {
            const Boss& b = bosses[bossIdx++];                  // real hitbox of this boss type
            m_bounds[o.id] = {b.x, b.y, b.w, b.h};
        } else if (o.kind == Kind::ENEMY && enemyIdx < enemies.size()) {
            const Enemy& e = enemies[enemyIdx++];
            SDL_FRect hit{e.x, e.y, e.w, e.h};
            SDL_FRect spr = m_enemyBuilder.getSpriteRect(e);
            // union of hitbox and sprite so there is something comfortable to click on
            const float x0 = std::fmin(hit.x, spr.x), y0 = std::fmin(hit.y, spr.y);
            const float x1 = std::fmax(hit.x + hit.w, spr.x + spr.w), y1 = std::fmax(hit.y + hit.h, spr.y + spr.h);
            m_bounds[o.id] = {x0, y0, x1 - x0, y1 - y0};
        }
    }
}

bool EditorScene::boundsFor(int objId, SDL_FRect& out) const {
    auto it = m_bounds.find(objId);
    if (it == m_bounds.end()) return false;
    out = it->second;
    return true;
}

void EditorScene::update(float dt) {
    m_fruitBuilder.update(dt);      // only advances the fruit sprite animation
}

void EditorScene::render(SDL_Renderer* renderer, const SceneLayers& layers) {
    // everything below mirrors GameState::render() minus the player / particles / lighting
    int camX = (int)std::round(Camera::getInstance().getCamera().x);
    int camY = (int)std::round(Camera::getInstance().getCamera().y);
    SDL_Texture* m_tileset = Engine::Get().getAssetManager().getTexture(TextureType::TILESET_SPRITE);
    const std::vector<Platform>& m_platforms = this->m_platforms;
    const CheckPoint& m_checkPoint = this->m_checkPoint;

    // Builders draw an element at  rect - cam * factor.  To show it as seen from the game camera (view camera +
    // m_par*) we shift the rect by  m_par * (1 - factor)  -- elements with factor 1 (walls, flat mode) are untouched.
    if (layers.background) {
        std::vector<BackGroundElement> adj = m_background;
        for (auto& b : adj) {
            b.rect.x += m_parDX * (1.0f - b.parallaxFloatX);
            b.rect.y += m_parDY * (1.0f - b.parallaxFloatY);
        }
        m_backgroundBuilder.init(adj);
        m_backgroundBuilder.render(renderer);
    }

    if (layers.walls) {
        for (const auto& tile : m_wallTiles) {
            SDL_FRect dst = {
                    tile.x - (float)camX,
                    tile.y - (float)camY,
                    TILE_SIZE * SCALE, TILE_SIZE * SCALE};
            if (tile.flip) {
                SDL_RenderTextureRotated(renderer, m_tileset, &tile.src, &dst, 0.0f, nullptr, SDL_FLIP_HORIZONTAL_AND_VERTICAL);
            } else {
                SDL_RenderTexture(renderer, m_tileset, &tile.src, &dst);
            }
        }

        for (auto const& tile : m_wallShape.tiles) {
            SDL_FRect src = tile.src;
            SDL_FRect dst{tile.x - (float)camX, tile.y - (float)camY, tile.w, tile.h};
            SDL_RenderTexture(renderer, m_tileset, &src, &dst);
        }

    for(const auto& platform : m_platforms){
        int tileSize =TILE_SIZE;

        const int platformWidth = (int)platform.w;
        const int platformHeight = (int)platform.h;

        int widthTiles = platformWidth / tileSize;
        int heightTiles = platformHeight / tileSize;

        for (int y = 0; y < heightTiles; y++) {
            for (int x = 0; x < widthTiles; x++) {

                SDL_FRect src;

                bool left = (x == 0);
                bool right = (x == widthTiles - 1);
                bool top = (y == 0);
                bool bottom = (y == heightTiles - 1);

                if(heightTiles ==1){

                    if(platform.platformType == SpriteType::GOLD_PLATFORM){

                        if (left) src = {SpriteCollection::goldPlatform.x, 0.00f, TILE_SIZE, TILE_SIZE};

                        else if (right)
                            src = {SpriteCollection::goldPlatform.x + 32.00f, 0.00f, TILE_SIZE,
                                   TILE_SIZE};

                        else
                            src = {SpriteCollection::goldPlatform.x + TILE_SIZE, 0.00f, TILE_SIZE,
                                   TILE_SIZE};
                    }

                    else if(platform.platformType == SpriteType::WOODEN_PLATFORM){
                        if (left) src = {SpriteCollection::woodenPlatform.x,
                                         SpriteCollection::woodenPlatform.y, TILE_SIZE, TILE_SIZE};

                        else if (right)
                            src = {SpriteCollection::woodenPlatform.x + 32.00f,
                                   SpriteCollection::woodenPlatform.y, TILE_SIZE,TILE_SIZE};

                        else
                            src = {SpriteCollection::woodenPlatform.x + TILE_SIZE,
                                   SpriteCollection::woodenPlatform.y, TILE_SIZE,
                                   TILE_SIZE};

                    }

                    else if(platform.platformType == SpriteType::STONE_PLATFORM) {
                        if (left) src = {SpriteCollection::stonePlatform.x,
                                         SpriteCollection::stonePlatform.y, TILE_SIZE, TILE_SIZE};

                        else if (right)
                            src = {SpriteCollection::stonePlatform.x + 32.00f,
                                   SpriteCollection::stonePlatform.y, TILE_SIZE,TILE_SIZE};

                        else
                            src = {SpriteCollection::stonePlatform.x + TILE_SIZE,
                                   SpriteCollection::stonePlatform.y, TILE_SIZE,
                                   TILE_SIZE};
                    }

                }

                SDL_FRect dst = {(platform.x+x* (TILE_SIZE * SCALE)) - camX,
                                 ( platform.y+y * (TILE_SIZE * SCALE)) - camY,
                                 TILE_SIZE * SCALE, TILE_SIZE * SCALE};

                SDL_RenderTexture(renderer, m_tileset, &src, &dst);
            }

        }

    }

    SDL_FRect checkPointDst{m_checkPoint.x-camX,m_checkPoint.y-camY,m_checkPoint.w,m_checkPoint.h};
    if(m_checkPoint.aniType == CheckPointAni::NO_FLAG)
    {
        SDL_FRect checkPointSrc{0.00f,0.00f,64.00f,64.00f};
        SDL_RenderTexture(renderer,Engine::Get().getAssetManager().getTexture(TextureType::CHECKPOINT_FLAG_NO),
                          &checkPointSrc,&checkPointDst);
    }
    if(m_checkPoint.aniType == CheckPointAni::FLAG_OUT)
    {
        SDL_FRect checkPointSrc{0.00f+64.00f*m_checkPoint.currentFrame,0.00f,64.00f,64.00f};
        SDL_RenderTexture(renderer,Engine::Get().getAssetManager().getTexture(TextureType::CHECKPOINT_FLAG_OUT),
                          &checkPointSrc,&checkPointDst);
    }
    if(m_checkPoint.aniType == CheckPointAni::FLAG_IDLE)
    {
        SDL_FRect checkPointSrc{0.00f+64.00f*m_checkPoint.currentFrame,0.00f,64.00f,64.00f};
        SDL_RenderTexture(renderer,Engine::Get().getAssetManager().getTexture(TextureType::CHECKPOINT_FLAG_IDLE),
                          &checkPointSrc,&checkPointDst);
    }

    }

    m_trapBuilder.render(renderer);
    m_fruitBuilder.render(renderer);
    m_enemyBuilder.render(renderer);
    m_blockBuilder.render(renderer);
    if (layers.foreground) {
        std::vector<ForeGroundElement> adj = m_foreground;
        for (auto& f : adj) {
            f.rect.x += m_parDX * (1.0f - f.parallaxFloatX);
            f.rect.y += m_parDY * (1.0f - f.parallaxFloatY);
        }
        m_foregroundBuilder.init(adj);
        m_foregroundBuilder.render(renderer);
    }
}

} // namespace editor
