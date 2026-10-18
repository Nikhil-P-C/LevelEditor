#include "editor/Gfx.h"
#include <cmath>
#include <vector>

namespace editor {

static const char* kFontPath = "Fonts/Pixel Game.otf";

void Gfx::init(SDL_Renderer* renderer) { m_r = renderer; }

void Gfx::clearCache() {
    for (auto& kv : m_cache) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    m_cache.clear();
}

void Gfx::shutdown() {
    clearCache();
    for (auto& kv : m_fonts) if (kv.second) TTF_CloseFont(kv.second);
    m_fonts.clear();
}

TTF_Font* Gfx::font(int size) {
    auto it = m_fonts.find(size);
    if (it != m_fonts.end()) return it->second;
    TTF_Font* f = TTF_OpenFont(kFontPath, (float)size);
    if (!f) SDL_Log("editor: could not open %s: %s", kFontPath, SDL_GetError());
    m_fonts[size] = f;
    return f;
}

static void setColor(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

void Gfx::fill(const SDL_FRect& r, SDL_Color c) { setColor(m_r, c); SDL_RenderFillRect(m_r, &r); }
void Gfx::frame(const SDL_FRect& r, SDL_Color c) { setColor(m_r, c); SDL_RenderRect(m_r, &r); }
void Gfx::line(float x1, float y1, float x2, float y2, SDL_Color c) { setColor(m_r, c); SDL_RenderLine(m_r, x1, y1, x2, y2); }
void Gfx::dot(float cx, float cy, float half, SDL_Color c) { fill({cx - half, cy - half, half * 2, half * 2}, c); }

void Gfx::circle(float cx, float cy, float radius, SDL_Color c, int segments) {
    if (radius < 1.0f) return;
    setColor(m_r, c);
    std::vector<SDL_FPoint> pts;
    pts.reserve((size_t)segments + 1);
    for (int i = 0; i <= segments; i++) {
        const float a = (float)i / (float)segments * 6.2831853f;
        pts.push_back({cx + std::cos(a) * radius, cy + std::sin(a) * radius});
    }
    SDL_RenderLines(m_r, pts.data(), (int)pts.size());
}

void Gfx::dashedFrame(const SDL_FRect& r, SDL_Color c, float dash) {
    setColor(m_r, c);
    for (float x = r.x; x < r.x + r.w; x += dash * 2) {
        const float x2 = std::fmin(x + dash, r.x + r.w);
        SDL_RenderLine(m_r, x, r.y, x2, r.y);
        SDL_RenderLine(m_r, x, r.y + r.h, x2, r.y + r.h);
    }
    for (float y = r.y; y < r.y + r.h; y += dash * 2) {
        const float y2 = std::fmin(y + dash, r.y + r.h);
        SDL_RenderLine(m_r, r.x, y, r.x, y2);
        SDL_RenderLine(m_r, r.x + r.w, y, r.x + r.w, y2);
    }
}

Gfx::Entry& Gfx::entry(const std::string& s, int size, SDL_Color c) {
    char key[64];
    std::snprintf(key, sizeof(key), "%d|%02x%02x%02x%02x|", size, c.r, c.g, c.b, c.a);
    std::string k = std::string(key) + s;
    auto it = m_cache.find(k);
    if (it != m_cache.end()) return it->second;
    if (m_cache.size() > 700) clearCache();           // cheap eviction: labels are re-rendered on demand

    Entry e;
    if (TTF_Font* f = font(size)) {
        SDL_Surface* surf = TTF_RenderText_Blended(f, s.c_str(), 0, c);
        if (surf) {
            e.tex = SDL_CreateTextureFromSurface(m_r, surf);
            e.w = (float)surf->w;
            e.h = (float)surf->h;
            SDL_DestroySurface(surf);
        }
    }
    return m_cache.emplace(k, e).first->second;
}

float Gfx::textWidth(const std::string& s, int size) {
    if (s.empty()) return 0;
    return entry(s, size, {255, 255, 255, 255}).w;
}

void Gfx::text(const std::string& s, float x, float y, int size, SDL_Color c, Align a) {
    if (s.empty()) return;
    Entry& e = entry(s, size, c);
    if (!e.tex) return;
    float dx = x;
    if (a == Align::CENTER) dx = x - e.w * 0.5f;
    else if (a == Align::RIGHT) dx = x - e.w;
    SDL_FRect dst{std::floor(dx), std::floor(y), e.w, e.h};
    SDL_RenderTexture(m_r, e.tex, nullptr, &dst);
}

} // namespace editor
