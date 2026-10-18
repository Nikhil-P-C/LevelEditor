//
// Tiny 2D drawing helper for the editor UI: filled/outlined rects, lines, circles and cached text
// (SDL_ttf, using the game's own pixel font).
//
#pragma once
#include <string>
#include <unordered_map>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

namespace editor {

enum class Align { LEFT, CENTER, RIGHT };

class Gfx {
public:
    void init(SDL_Renderer* renderer);
    void shutdown();

    SDL_Renderer* renderer() const { return m_r; }

    void fill(const SDL_FRect& r, SDL_Color c);
    void frame(const SDL_FRect& r, SDL_Color c);
    void line(float x1, float y1, float x2, float y2, SDL_Color c);
    void circle(float cx, float cy, float radius, SDL_Color c, int segments = 48);
    void dot(float cx, float cy, float half, SDL_Color c);          // small filled square centred on cx,cy
    void dashedFrame(const SDL_FRect& r, SDL_Color c, float dash = 6.0f);

    // text with its top-left (or centre / right edge) at x,y. size = font pixel size.
    void text(const std::string& s, float x, float y, int size, SDL_Color c, Align a = Align::LEFT);
    float textWidth(const std::string& s, int size);

private:
    struct Entry { SDL_Texture* tex = nullptr; float w = 0, h = 0; };
    TTF_Font* font(int size);
    Entry& entry(const std::string& s, int size, SDL_Color c);
    void clearCache();

    SDL_Renderer* m_r = nullptr;
    std::unordered_map<int, TTF_Font*> m_fonts;
    std::unordered_map<std::string, Entry> m_cache;
};

} // namespace editor
