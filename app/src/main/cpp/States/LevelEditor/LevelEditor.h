//
// Created by LENOVO on 23-08-2026.
//
#pragma once
#include "State/State.h"
const float SCALE = 4;
const float TILE_SIZE =16;
class LevelEditor : public State{

public:
    void render(SDL_Renderer* renderer)override;

    void update(float dt)override;

    bool handleEvents(SDL_Event& event)override;

    LevelEditor(SDL_Renderer* renderer);

    ~LevelEditor()override;

private:
    SDL_Renderer* m_renderer = nullptr;

};


