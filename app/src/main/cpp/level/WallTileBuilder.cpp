#include "level/WallTileBuilder.h"
#include <cmath>

std::vector<PrecomputedWallTile> buildWallTileList(const std::vector<LevelGround>& walls, int tileSizeI, float scale) {
    std::vector<PrecomputedWallTile> out;
    const float T = (float)tileSizeI;               // source tile size
    const float step = T * scale;                   // on-screen tile size

    // is there a wall covering the point (cx,cy)?   (was GameState::hasWallAbove/Below/Left/Right)
    auto coveredAt = [&](float cx, float cy) {
        for (const auto& wall : walls) {
            if (cx >= wall.x && cx < wall.x + wall.w &&
                cy >= wall.y && cy < wall.y + wall.h)
                return true;
        }
        return false;
    };

    for (const auto& level : walls) {
        int tileSize = (int)(T * scale);
        const int platformWidth = (int)level.w;
        const int platformHeight = (int)level.h;
        int widthTiles = static_cast<int>(std::ceil((float)platformWidth / (float)tileSize));
        int heightTiles = static_cast<int>(std::ceil((float)platformHeight / (float)tileSize));

        SpriteValue CurrentSpriteValue;
        SpriteValue CurrentEdgeSpriteValue;
        if (level.spriteType == SpriteType::STONE_BRICK_WALL) {
            CurrentSpriteValue = SpriteCollection::stoneBrickWall;
            CurrentEdgeSpriteValue = SpriteCollection::stoneBrickWallEdges;
        } else if (level.spriteType == SpriteType::MOSS_WALL) {
            CurrentSpriteValue = SpriteCollection::mossWall;
            CurrentEdgeSpriteValue = SpriteCollection::mossWallEdges;
        } else {
            continue;   // no wall art for this sprite type (the game leaves these uninitialised)
        }

        for (int i = 0; i < heightTiles; i++) {
            for (int j = 0; j < widthTiles; j++) {
                SDL_FRect src;
                float x = level.x + (float)j * step;
                float y = level.y + (float)i * step;
                bool left = (j == 0);
                bool right = (j == widthTiles - 1);
                bool top = (i == 0);
                bool bottom = (i == heightTiles - 1);
                bool edge = false;

                if (coveredAt(x, y - step)) top = false;
                if (coveredAt(x, y + step)) bottom = false;
                if (coveredAt(x + step, y)) right = false;
                if (coveredAt(x - step, y)) left = false;

                if (top) {
                    if (left) {
                        src = {CurrentEdgeSpriteValue.x, CurrentEdgeSpriteValue.y, T, T};
                        edge = true;
                    } else if (right) {
                        src = {CurrentEdgeSpriteValue.x + T, CurrentEdgeSpriteValue.y, T, T};
                        edge = true;
                    } else {
                        src = {CurrentSpriteValue.x + T, CurrentSpriteValue.y, T, T};
                        edge = false;
                    }
                } else if (bottom) {
                    if (left) {
                        src = {CurrentEdgeSpriteValue.x, CurrentEdgeSpriteValue.y + T, T, T};
                        edge = true;
                    } else if (right) {
                        src = {CurrentEdgeSpriteValue.x + T, CurrentEdgeSpriteValue.y + T, T, T};
                        edge = true;
                    } else {
                        src = {CurrentSpriteValue.x + T, CurrentSpriteValue.y + 32.00f, T, T};
                        edge = false;
                    }
                } else {
                    edge = false;
                    if (left) src = {CurrentSpriteValue.x, CurrentEdgeSpriteValue.y + T, T, T};
                    else if (right) src = {CurrentSpriteValue.x + 32.00f, CurrentEdgeSpriteValue.y + T, T, T};
                    else src = {CurrentSpriteValue.x + T, CurrentSpriteValue.y + T, T, T};
                }

                // single-side tiles are drawn flipped, corner tiles drawn plain, pure-interior tiles never drawn
                if ((top || bottom || left || right) && !edge) {
                    out.push_back({src, x, y, /*flip=*/true});
                } else if ((top || bottom) && (left || right) && edge) {
                    out.push_back({src, x, y, /*flip=*/false});
                }
            }
        }
    }
    return out;
}
