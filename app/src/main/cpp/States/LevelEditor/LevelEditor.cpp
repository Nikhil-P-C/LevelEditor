//
// Created by LENOVO on 23-08-2026.
//

#include <SDL3/SDL.h>

#include "engine/engine.h"
#include "utils/utils.h"
#include "LevelEditor.h"
void LevelEditor::render(SDL_Renderer *renderer) {
    SDL_FRect dst{0,0,1600,720};
    SDL_SetRenderDrawColor(renderer,255,0,0,255);
    SDL_RenderFillRect(renderer,&dst);
}

void LevelEditor::update(float dt) {
}

bool LevelEditor::handleEvents(SDL_Event &event) {
}

LevelEditor::LevelEditor(SDL_Renderer *renderer) {
    this->Name = "LevelEditor";
    m_renderer = renderer;
}

LevelEditor::~LevelEditor() {
    LOGI("Level Editor destructor:%p",this);
}
