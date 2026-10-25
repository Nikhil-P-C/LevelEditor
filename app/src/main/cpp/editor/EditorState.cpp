#include "editor/EditorState.h"
#include "editor/Playtest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "engine/Engine.h"
#include "utils/utils.h"
#include "level/Bosses.h"

namespace fs = std::filesystem;

namespace editor {

// ================================================================================================
// constants / small helpers
// ================================================================================================
namespace {
constexpr float kW = 1600.0f, kH = 720.0f;
constexpr float kTop = 38.0f, kBottom = 26.0f, kLeft = 256.0f, kRight = 300.0f;
constexpr int   kGrids[3] = {16, 32, 64};

const SDL_Color cBg{24, 23, 36, 255};
const SDL_Color cCanvas{33, 31, 48, 255};
const SDL_Color cPanel{38, 36, 56, 255};
const SDL_Color cPanel2{52, 49, 76, 255};
const SDL_Color cLine{78, 74, 112, 255};
const SDL_Color cText{228, 228, 244, 255};
const SDL_Color cDim{150, 148, 178, 255};
const SDL_Color cAccent{255, 205, 90, 255};
const SDL_Color cHot{74, 70, 118, 255};
const SDL_Color cGood{110, 220, 140, 255};
const SDL_Color cBad{240, 100, 100, 255};

// palette tab order (the order people reach for things)
const Kind kTabs[] = {Kind::WALL, Kind::GROUND, Kind::PLATFORM, Kind::BLOCK, Kind::FRUIT, Kind::TRAP, Kind::ENEMY,
                      Kind::NPC, Kind::CHECKPOINT, Kind::SPAWN, Kind::LIGHT, Kind::BACKGROUND, Kind::FOREGROUND,
                      Kind::BOSS, Kind::ARENA, Kind::WORLD_RECT};

EditorState* s_instance = nullptr;

SDL_Color withAlpha(SDL_Color c, Uint8 a) { c.a = a; return c; }

bool inside(const SDL_FRect& r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

bool intersects(const SDL_FRect& a, const SDL_FRect& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

std::string fmt(float v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s = buf;
    if (decimals > 0) {                                  // 12.50 -> 12.5, 3.00 -> 3
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s.empty() ? "0" : s;
}

bool trapHasPath(const std::string& t) {
    return t == "ROCK_HEAD" || t == "SPIKE_HEAD" || t == "MOVING_PLATFORM_GREY" || t == "MOVING_PLATFORM_BROWN" ||
           t == "SAW" || t == "SPIKE_BALL";
}

void utf8Pop(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) i--;
    s.erase(i);
}
} // namespace

EditorState* EditorState::instance() { return s_instance; }

// ================================================================================================
// construction
// ================================================================================================
EditorState::EditorState(SDL_Renderer* renderer) : m_renderer(renderer) {
    this->Name = "EditorState";
    s_instance = this;
    m_gfx.init(renderer);
    if (levelExists(0)) loadLevel(0);
    else newLevel();
    m_scene.rebuild(m_model, m_level, m_parallax);
    m_sceneDirty = false;
}

EditorState::~EditorState() {
    if (s_instance == this) s_instance = nullptr;
    for (auto& kv : m_thumbs) if (kv.second) SDL_DestroyTexture(kv.second);
    m_gfx.shutdown();
}

// ================================================================================================
// level files + undo
// ================================================================================================
std::string EditorState::levelPath(int n) const { return "Levels/level_" + std::to_string(n) + ".txt"; }

bool EditorState::levelExists(int n) const {
    std::error_code ec;
    return fs::exists(levelPath(n), ec);
}

void EditorState::newLevel() {
    stopMusicPreview();
    m_model.clear();
    // same skeleton as the shipped levels: world bounds + a stone ceiling / moss floor, spawn on the left, goal on the right
    auto addObj = [&](Kind k, const std::string& preset, float x, float y, float w, float h) {
        Obj o = makeDefault(k, preset, x, y);
        if (info(k).sized) { o.w = w / info(k).unit; o.h = h / info(k).unit; }
        m_model.add(o);
    };
    addObj(Kind::WORLD_RECT, "", 0, -768, 3200, 1536);
    addObj(Kind::WALL, "STONE_BRICK_WALL", 0, -768, 3200, 768);
    addObj(Kind::WALL, "MOSS_WALL", 0, 0, 3200, 768);
    addObj(Kind::SPAWN, "", 109, 600, 0, 0);
    addObj(Kind::CHECKPOINT, "", 2900, 440, 256, 256);
    m_undo.clear();
    m_redo.clear();
    m_selection.clear();
    m_dirty = false;
    m_sceneDirty = true;
    frameAll();
    setStatus("New level (level " + std::to_string(m_level) + ")");
}

void EditorState::loadLevel(int n) {
    stopMusicPreview();
    std::ifstream in(levelPath(n), std::ios::binary);
    m_level = std::max(0, n);
    if (!in) {
        newLevel();
        setStatus("level_" + std::to_string(n) + ".txt does not exist yet - started a new level");
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    m_model.parse(ss.str());
    m_undo.clear();
    m_redo.clear();
    m_selection.clear();
    m_dirty = false;
    m_sceneDirty = true;
    frameAll();
    setStatus("Loaded " + levelPath(n) + "  (" + std::to_string(m_model.objs.size()) + " objects)");
}

bool EditorState::saveLevel(int n) {
    std::error_code ec;
    fs::create_directories("Levels", ec);
    const std::string path = levelPath(n);
    // keep one backup of whatever was on disk, once per session per file
    if (fs::exists(path, ec) && std::find(m_backedUp.begin(), m_backedUp.end(), path) == m_backedUp.end()) {
        fs::copy_file(path, path + ".bak", fs::copy_options::overwrite_existing, ec);
        m_backedUp.push_back(path);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        setStatus("FAILED to write " + path, 5.0f);
        return false;
    }
    out << m_model.serialize(n);
    out.close();
    m_level = n;
    m_dirty = false;
    setStatus("Saved " + path);
    return true;
}

void EditorState::autosaveIfDirty() {
    if (!m_dirty) return;
    std::error_code ec;
    fs::create_directories("Levels", ec);
    std::ofstream out("Levels/_autosave_level_" + std::to_string(m_level) + ".txt", std::ios::binary | std::ios::trunc);
    if (out) out << m_model.serialize(m_level);
}

void EditorState::requestSwitchLevel(int n) {
    if (n < 0) return;
    auto doSwitch = [this, n]() { loadLevel(n); };
    if (!m_dirty) { doSwitch(); return; }
    showModal("Unsaved changes", "Level " + std::to_string(m_level) + " has unsaved changes.", {"Save", "Discard", "Cancel"},
              [this, doSwitch](int choice) {
                  if (choice == 0) { if (saveLevel(m_level)) doSwitch(); }
                  else if (choice == 1) doSwitch();
              });
}

void EditorState::markChanged() {
    m_dirty = true;
    m_sceneDirty = true;
}

void EditorState::beginEdit() {
    m_undo.push_back(m_model);
    if (m_undo.size() > 120) m_undo.erase(m_undo.begin());
    m_redo.clear();
    markChanged();
}

static void pruneSelection(const LevelModel& m, std::vector<int>& sel) {
    sel.erase(std::remove_if(sel.begin(), sel.end(), [&](int id) { return m.find(id) == nullptr; }), sel.end());
}

void EditorState::undo() {
    if (m_undo.empty()) { setStatus("Nothing to undo"); return; }
    m_redo.push_back(m_model);
    m_model = m_undo.back();
    m_undo.pop_back();
    pruneSelection(m_model, m_selection);
    markChanged();
    setStatus("Undo");
}

void EditorState::redo() {
    if (m_redo.empty()) { setStatus("Nothing to redo"); return; }
    m_undo.push_back(m_model);
    m_model = m_redo.back();
    m_redo.pop_back();
    pruneSelection(m_model, m_selection);
    markChanged();
    setStatus("Redo");
}

void EditorState::setStatus(const std::string& s, float seconds) {
    m_status = s;
    m_statusTime = seconds;
}

// ================================================================================================
// view helpers
// ================================================================================================
SDL_FRect EditorState::canvasRect() const { return {kLeft, kTop, kW - kLeft - kRight, kH - kTop - kBottom}; }

void EditorState::screenToWorld(float sx, float sy, float& wx, float& wy) const {
    const SDL_FRect c = canvasRect();
    wx = std::round(m_camX) + (sx - c.x) / m_zoom;
    wy = std::round(m_camY) + (sy - c.y) / m_zoom;
}

void EditorState::worldToScreen(float wx, float wy, float& sx, float& sy) const {
    const SDL_FRect c = canvasRect();
    sx = c.x + (wx - std::round(m_camX)) * m_zoom;
    sy = c.y + (wy - std::round(m_camY)) * m_zoom;
}

float EditorState::snap(float v) const {
    if (!m_snap || m_altDown) return v;
    const float g = (float)kGrids[m_gridIndex];
    return std::round(v / g) * g;
}

float EditorState::snapSize(float v, Kind k) const {
    // platforms / grounds are made of whole 64px tiles; everything else follows the grid
    float step = (k == Kind::PLATFORM || k == Kind::GROUND) ? kTile : (float)kGrids[m_gridIndex];
    if (!m_snap || m_altDown) return std::max(8.0f, v);
    return std::max(step, std::round(v / step) * step);
}

void EditorState::zoomAt(float sx, float sy, float factor) {
    float wx, wy;
    screenToWorld(sx, sy, wx, wy);
    m_zoom = std::clamp(m_zoom * factor, 0.08f, 3.0f);
    const SDL_FRect c = canvasRect();
    m_camX = wx - (sx - c.x) / m_zoom;
    m_camY = wy - (sy - c.y) / m_zoom;
}

void EditorState::parallaxRef(float& px, float& py) const {
    if (!m_parallax) { px = std::round(m_camX); py = std::round(m_camY); return; }
    // the same 1600x720 frame the "Cam" button draws: centred in the view, so panning slides the game camera over the level
    const SDL_FRect c = canvasRect();
    px = std::round(m_camX + c.w * 0.5f / m_zoom - 800.0f);
    py = std::round(m_camY + c.h * 0.5f / m_zoom - 360.0f);
}

void EditorState::centerOn(float wx, float wy) {
    const SDL_FRect c = canvasRect();
    m_camX = wx - c.w / (2.0f * m_zoom);
    m_camY = wy - c.h / (2.0f * m_zoom);
}

void EditorState::frameAll() {
    bool any = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (const auto& o : m_model.objs) {
        if (o.kind == Kind::BACKGROUND || o.kind == Kind::FOREGROUND) continue;
        const SDL_FRect b = worldBounds(o);
        if (!any) { x0 = b.x; y0 = b.y; x1 = b.x + b.w; y1 = b.y + b.h; any = true; }
        x0 = std::min(x0, b.x); y0 = std::min(y0, b.y);
        x1 = std::max(x1, b.x + b.w); y1 = std::max(y1, b.y + b.h);
    }
    const SDL_FRect c = canvasRect();
    if (!any) { m_zoom = 0.5f; centerOn(0, 0); return; }
    const float bw = std::max(1.0f, x1 - x0), bh = std::max(1.0f, y1 - y0);
    m_zoom = std::clamp(std::min(c.w / bw, c.h / bh) * 0.92f, 0.08f, 1.0f);
    centerOn((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
}

// ================================================================================================
// objects
// ================================================================================================
SDL_FRect EditorState::objBounds(const Obj& o) const {
    SDL_FRect b = worldBounds(o);
    if (o.kind == Kind::TRAP || o.kind == Kind::ENEMY || o.kind == Kind::BOSS) {
        SDL_FRect s;
        if (m_scene.boundsFor(o.id, s)) b = s;
    }
    if (o.kind == Kind::BACKGROUND || o.kind == Kind::FOREGROUND) {
        // parallax: where the element is actually drawn for the current camera
        const float fx = m_parallax ? o.f[0] : 1.0f, fy = m_parallax ? o.f[1] : 1.0f;
        float px, py;
        parallaxRef(px, py);
        b.x += px * (1.0f - fx);
        b.y += py * (1.0f - fy);
    }
    return b;
}

bool EditorState::selectable(const Obj& o) const {
    // huge "backdrop" objects would swallow every click, so they are only editable from their own palette tab
    if (o.kind == Kind::BACKGROUND) return m_tab == Kind::BACKGROUND;
    if (o.kind == Kind::FOREGROUND) return m_tab == Kind::FOREGROUND;
    if (o.kind == Kind::WORLD_RECT) return m_tab == Kind::WORLD_RECT;
    if (o.kind == Kind::ARENA) return m_tab == Kind::ARENA;
    return true;
}

int EditorState::pickAt(float wx, float wy) const {
    int best = -1;
    float bestArea = 1e30f;
    for (const auto& o : m_model.objs) {
        if (!selectable(o)) continue;
        const SDL_FRect b = objBounds(o);
        if (!inside(b, wx, wy)) continue;
        const float area = b.w * b.h;
        if (area <= bestArea) { best = o.id; bestArea = area; }      // ties: later (drawn on top) wins
    }
    return best;
}

bool EditorState::isSelected(int id) const {
    return std::find(m_selection.begin(), m_selection.end(), id) != m_selection.end();
}

void EditorState::select(int id, bool additive) {
    if (!additive) { m_selection.assign(1, id); return; }
    auto it = std::find(m_selection.begin(), m_selection.end(), id);
    if (it == m_selection.end()) m_selection.push_back(id);
    else m_selection.erase(it);
}

void EditorState::clearSelection() { m_selection.clear(); }

Obj* EditorState::single() {
    if (m_selection.size() != 1) return nullptr;
    return m_model.find(m_selection[0]);
}

void EditorState::deleteSelection() {
    if (m_selection.empty()) return;
    beginEdit();
    for (int id : m_selection) m_model.remove(id);
    setStatus("Deleted " + std::to_string(m_selection.size()) + " object(s)");
    m_selection.clear();
}

void EditorState::copySelection() {
    m_clipboard.clear();
    for (int id : m_selection)
        if (const Obj* o = m_model.find(id)) m_clipboard.push_back(*o);
    if (!m_clipboard.empty()) setStatus("Copied " + std::to_string(m_clipboard.size()) + " object(s)");
}

void EditorState::pasteClipboard(float wx, float wy) {
    if (m_clipboard.empty()) return;
    float minX = 1e30f, minY = 1e30f;
    for (const auto& o : m_clipboard) { minX = std::min(minX, o.x); minY = std::min(minY, o.y); }
    const float dx = snap(wx) - minX, dy = snap(wy) - minY;
    beginEdit();
    m_selection.clear();
    for (Obj o : m_clipboard) {
        translateObj(o, dx, dy);
        if (info(o.kind).singleton) continue;                // never duplicate the spawn / goal / bounds
        m_selection.push_back(m_model.add(o));
    }
    setStatus("Pasted " + std::to_string(m_selection.size()) + " object(s)");
}

void EditorState::duplicateSelection() {
    copySelection();
    if (m_clipboard.empty()) return;
    float minX = 1e30f, minY = 1e30f;
    for (const auto& o : m_clipboard) { minX = std::min(minX, o.x); minY = std::min(minY, o.y); }
    pasteClipboard(minX + (float)kGrids[m_gridIndex], minY + (float)kGrids[m_gridIndex]);
}

void EditorState::reorderSelection(int dir) {
    Obj* o = single();
    if (!o) return;
    const int id = o->id;
    int idx = m_model.indexOf(id);
    int j = idx + dir;
    while (j >= 0 && j < (int)m_model.objs.size() && m_model.objs[j].kind != m_model.objs[idx].kind) j += dir;
    if (j < 0 || j >= (int)m_model.objs.size()) return;
    beginEdit();
    std::swap(m_model.objs[idx], m_model.objs[j]);
    setStatus(dir > 0 ? "Moved forward (drawn later)" : "Moved backward (drawn earlier)");
}

// moving an object also moves the path it follows (the path numbers are absolute world coordinates)
void EditorState::translateObj(Obj& o, float dx, float dy) const {
    o.x += dx;
    o.y += dy;
    if (o.kind == Kind::ENEMY) {
        o.f[0] += dx;
        o.f[1] += dx;
    } else if (o.kind == Kind::TRAP) {
        if (o.s[3] == "RECT") { o.f[0] += dx; o.f[1] += dy; }
        else if (o.s[3] == "LINE") {
            if (o.s[2] == "VERTICAL") { o.f[0] += dy; o.f[1] += dy; }
            else if (o.s[2] == "HORIZONTAL") { o.f[0] += dx; o.f[1] += dx; }
        }
    }
}

std::vector<EditorState::Handle> EditorState::handlesFor(const Obj& o) const {
    std::vector<Handle> hs;
    if (o.kind == Kind::LIGHT) {
        hs.push_back({HandleType::LIGHT_RADIUS, o.x + o.f[0], o.y});
    } else if (o.kind == Kind::ENEMY) {
        const SDL_FRect b = objBounds(o);
        const float cy = b.y + b.h * 0.5f;
        hs.push_back({HandleType::PATH_START, o.f[0], cy});
        hs.push_back({HandleType::PATH_END, o.f[1], cy});
    } else if (o.kind == Kind::TRAP && trapHasPath(o.s[0])) {
        const SDL_FRect b = objBounds(o);
        if (o.s[3] == "RECT") {
            hs.push_back({HandleType::PATH_CORNER, o.f[0], o.f[1]});
        } else if (o.s[3] == "LINE" && o.s[2] == "VERTICAL") {
            hs.push_back({HandleType::PATH_START, b.x + b.w * 0.5f, o.f[0]});
            hs.push_back({HandleType::PATH_END, b.x + b.w * 0.5f, o.f[1]});
        } else if (o.s[3] == "LINE" && o.s[2] == "HORIZONTAL") {
            hs.push_back({HandleType::PATH_START, o.f[0], b.y + b.h * 0.5f});
            hs.push_back({HandleType::PATH_END, o.f[1], b.y + b.h * 0.5f});
        }
    }
    return hs;
}

void EditorState::applyHandle(Obj& o, const Handle& h, float wx, float wy) const {
    switch (h.type) {
        case HandleType::LIGHT_RADIUS:
            o.f[0] = std::max(20.0f, std::round(std::fabs(wx - o.x)));
            break;
        case HandleType::PATH_CORNER:
            o.f[0] = snap(wx);
            o.f[1] = snap(wy);
            break;
        case HandleType::PATH_START:
        case HandleType::PATH_END: {
            const int slot = h.type == HandleType::PATH_START ? 0 : 1;
            if (o.kind == Kind::ENEMY || o.s[2] == "HORIZONTAL") o.f[slot] = snap(wx);
            else o.f[slot] = snap(wy);
            break;
        }
    }
}

// 8 resize handles around a screen-space rect: 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
int EditorState::resizeHandleAt(const SDL_FRect& r, float mx, float my) const {
    const float xs[3] = {r.x, r.x + r.w * 0.5f, r.x + r.w};
    const float ys[3] = {r.y, r.y + r.h * 0.5f, r.y + r.h};
    const int map[8][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
    for (int i = 0; i < 8; i++) {
        if (std::fabs(mx - xs[map[i][0]]) <= 7.0f && std::fabs(my - ys[map[i][1]]) <= 7.0f) return i;
    }
    return -1;
}

Obj EditorState::brushObject(Kind kind, const std::string& preset, float wx, float wy, float wx2, float wy2, bool dragged) const {
    const KindInfo& ki = info(kind);
    float x = snap(wx), y = snap(wy);
    Obj o;
    if (ki.sized) {
        o = makeDefault(kind, preset, x, y);
        if (dragged) {
            const float x0 = snap(std::min(wx, wx2)), y0 = snap(std::min(wy, wy2));
            const float w = snapSize(std::fabs(snap(wx2) - snap(wx)), kind);
            const float h = snapSize(std::fabs(snap(wy2) - snap(wy)), kind);
            setWorldRect(o, x0, y0, w, h);
        }
        if (kind == Kind::BLOCK) {
            const float bw = o.w * ki.unit, bh = o.h * ki.unit;
            o.s[0] = (bw <= kTile && bh <= kTile) ? "SINGLE" : (bw <= kTile ? "VERTICAL" : (bh <= kTile ? "HORIZONTAL" : "BLOCK"));
            // HORIZONTAL / VERTICAL blocks are auto-tiled by the game (end caps + repeated middle) at native tile
            // size, so the thickness is exactly one tile - the same as the shipped levels
            if (o.s[0] == "HORIZONTAL") o.h = kTile / ki.unit;
            else if (o.s[0] == "VERTICAL") o.w = kTile / ki.unit;
        }
    } else {
        if (kind == Kind::FRUIT) { x -= 64.0f; y -= 64.0f; }              // centre the fruit on the cursor
        else if (kind == Kind::ENEMY) { x -= 20.0f; y -= 85.0f; }         // feet on the cursor
        else if (kind == Kind::SPAWN) { x -= 20.0f; y -= 85.0f; }
        else if (kind == Kind::BOSS) { x -= 100.0f; y -= 150.0f; }        // it falls to the floor from here
        o = makeDefault(kind, preset, x, y);
        if (kind == Kind::TRAP && trapCanRotate(o.s[0])) o.s[5] = m_brushFacing;
    }
    return o;
}

// Boss levels 11..23 of the game, taken one by one. All of them: approach along x 0..2400, then the 1600 px wide arena (A);
// the floor is the bottom of the world bounds (R, floor = 656 in those levels) and the arena reaches 48 px below it; the boss
// spawns at its own spot inside the arena; the exit flag (C) sits behind the fight. NO arena has platforms except two:
//   level 12  Cave Spider : three one-way platforms inside the arena
//   level 23  The Preacher: phase 3 is fought in an UPPER arena 3840 px higher (BOSS_PREACHER_UPPER_RISE), the level has a wide
//                           platform there as its ground, the world bounds reach up to it and the exit flag is up there too
namespace {
enum class BossExtra { NONE, SPIDER_PLATFORMS, PREACHER_UPPER };
struct BossLayout {
    const char* type;
    float arenaTop;      // arena y in the shipped level (floor = 656)
    float arenaH;
    float bossDx;        // boss x - arena x
    float bossY;         // boss y in the shipped level (floor = 656)
    const char* plat;    // platform sprite of the biome (used by the extras)
    BossExtra extra;
};
const BossLayout kBossLayouts[] = {
    {"MOSS_LUMINOUS_WYRM",      0.0f,    704.0f,  935.0f, 490.0f, "WOODEN_PLATFORM", BossExtra::NONE},              // level 11
    {"MOSS_CAVE_SPIDER",        0.0f,    704.0f,  985.0f, 490.0f, "WOODEN_PLATFORM", BossExtra::SPIDER_PLATFORMS},  // 12
    {"STONE_GIANT_RAT",      -320.0f,   1024.0f, 1015.0f, 510.0f, "STONE_PLATFORM",  BossExtra::NONE},              // 13
    {"STONE_ANCIENT_SINNER", -896.0f,   1600.0f,  975.0f, 350.0f, "STONE_PLATFORM",  BossExtra::NONE},              // 14
    {"SHAFT_WARDEN",         -320.0f,   1024.0f, 1000.0f, 450.0f, "WOODEN_PLATFORM", BossExtra::NONE},              // 15
    {"SHAFT_BRONTOS",        -896.0f,   1600.0f,  950.0f, 360.0f, "WOODEN_PLATFORM", BossExtra::NONE},              // 16
    {"PLAINS_ANKYLOSAUR",    -320.0f,   1024.0f,  975.0f, 480.0f, "WOODEN_PLATFORM", BossExtra::NONE},              // 17
    {"PLAINS_SENTINEL_PARA", -576.0f,   1280.0f, 1015.0f, 425.0f, "WOODEN_PLATFORM", BossExtra::NONE},              // 18
    {"WOODS_THORN_MATRIARCH",-576.0f,   1280.0f, 1000.0f, 380.0f, "WOODEN_PLATFORM", BossExtra::NONE},              // 19
    {"WOODS_TREEHORN_ELDER", -896.0f,   1600.0f,  965.0f, 390.0f, "WOODEN_PLATFORM", BossExtra::NONE},              // 20
    {"MOUNTAIN_CAT_QUEEN",   -576.0f,   1280.0f, 1015.0f, 515.0f, "STONE_PLATFORM",  BossExtra::NONE},              // 21
    {"MOUNTAIN_BROTHER",     -896.0f,   1600.0f,  965.0f, 350.0f, "STONE_PLATFORM",  BossExtra::NONE},              // 22
    {"PEAK_THE_PREACHER",    -320.0f,   1024.0f, 1010.0f, 370.0f, "GOLD_PLATFORM",   BossExtra::PREACHER_UPPER},    // 23
};
constexpr float kShippedFloor = 640.0f;     // floor surface in those levels (R is 656 tall there: the game rounds it to whole tiles)
constexpr float kArenaW = 1600.0f;
constexpr float kPreacherRise = 3840.0f;    // = BOSS_PREACHER_UPPER_RISE
}

void EditorState::placeBoss(float wx, float wy) {
    const KindInfo& ki = info(Kind::BOSS);
    std::string type = m_brush.preset.empty() && ki.presets && !ki.presets->empty() ? (*ki.presets)[0] : m_brush.preset;
    const BossLayout* lay = nullptr;
    for (const BossLayout& l : kBossLayouts) if (type == l.type) lay = &l;
    static const BossLayout kFallback{"", 0.0f, 704.0f, 960.0f, 490.0f, "WOODEN_PLATFORM", BossExtra::NONE};
    if (!lay) lay = &kFallback;

    const Boss probe(0.0f, 0.0f, bossTypeFromName(type));        // real hitbox of this boss
    const float bw = probe.w, bh = probe.h;
    const bool preacher = lay->extra == BossExtra::PREACHER_UPPER;

    beginEdit();
    // copies, not pointers: adding objects below may reallocate the model's vector
    const bool hasBounds = m_model.first(Kind::WORLD_RECT) != nullptr;
    const Obj bounds = hasBounds ? *m_model.first(Kind::WORLD_RECT) : Obj{};
    const bool hasArena = m_model.first(Kind::ARENA) != nullptr;
    const Obj existingArena = hasArena ? *m_model.first(Kind::ARENA) : Obj{};
    const bool hasExit = m_model.first(Kind::CHECKPOINT) != nullptr;
    const Obj existingExit = hasExit ? *m_model.first(Kind::CHECKPOINT) : Obj{};

    // F = the floor SURFACE the boss fight happens on. The game computes it from the wall collision rect (R) exactly like
    // BossBuilder::wallBottomOf: y + ceil(h / 64) * 64 - 64 (so 640 for the shipped `R 0 0 4000 656`), NOT from R's raw bottom edge.
    // Without an R the click height is the floor.
    const float F = hasBounds ? bounds.y + std::ceil(bounds.h / kTile) * kTile - kTile : snap(wy);
    float bossX = snap(wx) - bw * 0.5f;
    std::string what;

    if (!hasArena) {
        // --- arena: the boss sits at its usual spot inside it; it reaches one tile below the floor surface (704 vs 640 in the game)
        const float ax = bossX - lay->bossDx;
        const float ay = F + (lay->arenaTop - kShippedFloor);
        Obj a = makeDefault(Kind::ARENA, "", ax, ay);
        a.w = kArenaW; a.h = lay->arenaH;
        m_model.add(a);
        what = "arena";

        // --- world bounds (R) must contain the arena (the Preacher's reach up to his upper arena). The collision floor has to stay at F:
        // floor = top + ceil(h/64)*64 - 64, so the new top is moved in whole tiles and h = (F - top) + 16 like the shipped levels (656 for 640).
        const float needTop = preacher ? ay - kPreacherRise : ay;
        float top = needTop;
        if (hasBounds) top = std::min(bounds.y, needTop);
        top = F - kTile * std::ceil((F - top) / kTile);                    // F - top is a whole number of tiles
        const float left  = hasBounds ? std::min(bounds.x, ax) : std::min(0.0f, ax);
        const float right = hasBounds ? std::max(bounds.x + bounds.w, ax + kArenaW) : ax + kArenaW;
        Obj r = hasBounds ? bounds : makeDefault(Kind::WORLD_RECT, "", left, top);
        r.x = left; r.y = top; r.w = right - left; r.h = (F - top) + 16.0f;
        m_model.add(r);                                                    // singleton: replaces the old one
        what += ", world bounds";

        auto plat = [&](float px, float py, float pw) {
            Obj p = makeDefault(Kind::PLATFORM, lay->plat, px, py);
            p.w = pw; p.h = 16.0f;                                         // stored units (x4 px), like the shipped levels
            p.s[0] = "ONE_WAY";
            m_model.add(p);
        };
        if (lay->extra == BossExtra::SPIDER_PLATFORMS) {                   // level 12: y 500 / 360 / 500 over a floor of 640
            plat(ax + 160.0f,  F - 140.0f, 64.0f);
            plat(ax + 640.0f,  F - 280.0f, 64.0f);
            plat(ax + 1160.0f, F - 140.0f, 64.0f);
            what += ", 3 platforms";
        } else if (preacher) {                                             // level 23: ground of the upper arena + the exit up there
            const float upperY = F - kPreacherRise;
            plat(ax - 32.0f, upperY, 408.0f);
            what += ", upper-arena platform";
            if (!(hasExit && existingExit.y < upperY)) {
                Obj c = makeDefault(Kind::CHECKPOINT, "", ax + 1240.0f, upperY - 264.0f);
                m_model.add(c);                                            // singleton: replaces the old exit
                what += ", exit flag upstairs";
            }
        }
        // --- the level's exit only opens once the boss is dead: put one behind the fight if the level has none
        if (!preacher && !hasExit) {
            Obj c = makeDefault(Kind::CHECKPOINT, "", ax + 1240.0f, F - 264.0f);
            m_model.add(c);
            what += ", exit flag";
        }
    } else {
        bossX = std::clamp(bossX, existingArena.x, existingArena.x + existingArena.w - bw);   // an arena already exists: just drop the boss into it
    }

    // the shipped spawn height, but never with the hitbox inside the floor (the Preacher is 330 px tall: 370 + 330 would sink 60 px into it)
    const float bossY = std::min(F + (lay->bossY - kShippedFloor), F - bh - 2.0f);
    Obj b = makeDefault(Kind::BOSS, type, bossX, bossY);
    const int id = m_model.add(b);
    m_selection.assign(1, id);
    setStatus(!what.empty() ? "Placed boss with " + what + " - edit any of them" : "Placed boss inside the existing arena");
    if (!what.empty()) centerOn(bossX + bw * 0.5f, F - 300.0f);
}

void EditorState::placeBrush(float wx, float wy, float wx2, float wy2, bool dragged) {
    if (!m_brush.active) return;
    if (m_brush.kind == Kind::BOSS) { placeBoss(wx, wy); return; }
    const KindInfo& ki = info(m_brush.kind);
    Obj o = brushObject(m_brush.kind, m_brush.preset, wx, wy, wx2, wy2, dragged);
    beginEdit();
    const int id = m_model.add(o);
    m_selection.assign(1, id);
    setStatus(std::string("Placed ") + ki.name);
    if (ki.singleton) m_brush.active = false;           // only one of these exists; back to the select tool
}

// ================================================================================================
// events
// ================================================================================================
bool EditorState::hit(const SDL_FRect& r) const { return inside(r, m_mx, m_my); }

static void toLogical(SDL_Renderer* r, float wx, float wy, float& x, float& y) {
    SDL_RenderCoordinatesFromWindow(r, wx, wy, &x, &y);
}

bool EditorState::handleEvents(SDL_Event& e) {
    if (m_playtesting) return false;

    auto updateMods = [&](SDL_Keymod mod) {
        m_shiftDown = (mod & SDL_KMOD_SHIFT) != 0;
        m_ctrlDown = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
        m_altDown = (mod & SDL_KMOD_ALT) != 0;
    };

    switch (e.type) {
    // ------------------------------------------------------------------------------ text typing
    case SDL_EVENT_TEXT_INPUT:
        if (m_edit.active) {
            std::string t = e.text.text;
            if (m_edit.numeric) {
                std::string f;
                for (char ch : t) if (std::isdigit((unsigned char)ch) || ch == '.' || ch == '-' || ch == '+') f += ch;
                t = f;
            }
            m_edit.buf += t;
            return true;
        }
        return false;

    // ------------------------------------------------------------------------------------ mouse
    case SDL_EVENT_MOUSE_MOTION: {
        toLogical(m_renderer, e.motion.x, e.motion.y, m_mx, m_my);
        if (m_drag.mode == DragMode::NONE) return true;
        float wx, wy;
        screenToWorld(m_mx, m_my, wx, wy);
        const float moved = std::hypot(m_mx - m_drag.startMX, m_my - m_drag.startMY);
        switch (m_drag.mode) {
        case DragMode::PAN:
            if (moved > 3.0f) m_drag.started = true;
            if (m_drag.started) {
                m_camX = m_drag.camX0 - (m_mx - m_drag.startMX) / m_zoom;
                m_camY = m_drag.camY0 - (m_my - m_drag.startMY) / m_zoom;
            }
            break;
        case DragMode::PALETTE:
            if (moved > 6.0f) m_drag.started = true;
            break;
        case DragMode::BOX:
        case DragMode::DRAW:
            if (moved > 5.0f) m_drag.started = true;
            if (m_drag.started) {
                float x0 = m_drag.startWX, y0 = m_drag.startWY, x1 = wx, y1 = wy;
                if (m_drag.mode == DragMode::DRAW) { x0 = snap(x0); y0 = snap(y0); x1 = snap(x1); y1 = snap(y1); }
                m_boxRect = {std::min(x0, x1), std::min(y0, y1), std::fabs(x1 - x0), std::fabs(y1 - y0)};
            }
            break;
        case DragMode::MOVE: {
            if (!m_drag.started && moved > 4.0f) { m_drag.started = true; beginEdit(); }
            if (!m_drag.started) break;
            const Obj* base = nullptr;
            for (const auto& o : m_drag.orig) if (o.id == m_drag.primary) base = &o;
            if (!base) break;
            float ddx = wx - m_drag.startWX, ddy = wy - m_drag.startWY;
            const bool free = base->kind == Kind::BACKGROUND || base->kind == Kind::FOREGROUND;
            if (!free) { ddx = snap(base->x + ddx) - base->x; ddy = snap(base->y + ddy) - base->y; }
            else { ddx = std::round(ddx); ddy = std::round(ddy); }
            for (const auto& orig : m_drag.orig) {
                if (Obj* cur = m_model.find(orig.id)) {
                    *cur = orig;
                    translateObj(*cur, ddx, ddy);
                }
            }
            m_sceneDirty = true;
            break;
        }
        case DragMode::RESIZE: {
            if (!m_drag.started && moved > 2.0f) { m_drag.started = true; beginEdit(); }
            Obj* o = m_model.find(m_drag.primary);
            if (!m_drag.started || !o) break;
            const SDL_FRect r0 = m_drag.origRect;
            float l = r0.x, t = r0.y, rr = r0.x + r0.w, b = r0.y + r0.h;
            const int h = m_drag.handle;
            if (h == 0 || h == 6 || h == 7) l = snap(wx);
            if (h == 2 || h == 3 || h == 4) rr = snap(wx);
            if (h == 0 || h == 1 || h == 2) t = snap(wy);
            if (h == 4 || h == 5 || h == 6) b = snap(wy);
            const float minSize = (o->kind == Kind::PLATFORM || o->kind == Kind::GROUND) ? kTile : 16.0f;
            if (rr - l < minSize) { if (h == 0 || h == 6 || h == 7) l = rr - minSize; else rr = l + minSize; }
            if (b - t < minSize) { if (h == 0 || h == 1 || h == 2) t = b - minSize; else b = t + minSize; }
            setWorldRect(*o, l, t, rr - l, b - t);
            m_sceneDirty = true;
            break;
        }
        case DragMode::HANDLE: {
            if (!m_drag.started && moved > 2.0f) { m_drag.started = true; beginEdit(); }
            Obj* o = m_model.find(m_drag.primary);
            if (!m_drag.started || !o) break;
            auto hs = handlesFor(*o);
            if (m_drag.handle >= 0 && m_drag.handle < (int)hs.size()) applyHandle(*o, hs[m_drag.handle], wx, wy);
            m_sceneDirty = true;
            break;
        }
        default: break;
        }
        return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        toLogical(m_renderer, e.button.x, e.button.y, m_mx, m_my);
        const bool left = e.button.button == SDL_BUTTON_LEFT;
        const bool mid = e.button.button == SDL_BUTTON_MIDDLE;
        const bool right = e.button.button == SDL_BUTTON_RIGHT;
        if (m_modal.active) { if (left) { m_uiClick = true; m_uiClickX = m_mx; m_uiClickY = m_my; } return true; }
        if (m_edit.active) commitEdit();
        const bool inCanvas = inside(canvasRect(), m_mx, m_my);
        if (left) m_mouseDown = true;

        if (!inCanvas) {
            if (left) { m_uiClick = true; m_uiClickX = m_mx; m_uiClickY = m_my; }
            if (right) m_uiRightClick = true;
            return true;
        }
        float wx, wy;
        screenToWorld(m_mx, m_my, wx, wy);
        m_drag = Drag{};
        m_drag.startMX = m_mx; m_drag.startMY = m_my; m_drag.startWX = wx; m_drag.startWY = wy;

        if (mid || right || (left && (m_spaceDown || m_handTool))) {
            m_drag.mode = DragMode::PAN;
            m_drag.camX0 = m_camX; m_drag.camY0 = m_camY;
            return true;
        }
        if (!left) return true;

        if (m_brush.active) {
            if (info(m_brush.kind).sized) m_drag.mode = DragMode::DRAW;
            else placeBrush(wx, wy, wx, wy, false);
            return true;
        }
        if (Obj* s = single()) {
            float sx, sy;
            const SDL_FRect b = objBounds(*s);
            worldToScreen(b.x, b.y, sx, sy);
            if (info(s->kind).sized && selectable(*s)) {
                const int h = resizeHandleAt({sx, sy, b.w * m_zoom, b.h * m_zoom}, m_mx, m_my);
                if (h >= 0) {
                    m_drag.mode = DragMode::RESIZE;
                    m_drag.handle = h;
                    m_drag.primary = s->id;
                    m_drag.origRect = b;
                    if (s->kind == Kind::BACKGROUND || s->kind == Kind::FOREGROUND) m_drag.origRect = worldBounds(*s);
                    return true;
                }
            }
            auto hs = handlesFor(*s);
            for (size_t i = 0; i < hs.size(); i++) {
                float hx, hy;
                worldToScreen(hs[i].wx, hs[i].wy, hx, hy);
                if (std::hypot(m_mx - hx, m_my - hy) <= 9.0f) {
                    m_drag.mode = DragMode::HANDLE;
                    m_drag.handle = (int)i;
                    m_drag.primary = s->id;
                    return true;
                }
            }
        }
        const int id = pickAt(wx, wy);
        if (id >= 0) {
            if (m_shiftDown) {
                select(id, true);
                if (!isSelected(id)) return true;
            } else if (!isSelected(id)) {
                select(id, false);
            }
            m_drag.mode = DragMode::MOVE;
            m_drag.primary = id;
            for (int sid : m_selection)
                if (const Obj* o = m_model.find(sid)) m_drag.orig.push_back(*o);
        } else {
            if (!m_shiftDown) clearSelection();
            m_drag.mode = DragMode::BOX;
            m_boxRect = {wx, wy, 0, 0};
        }
        return true;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        toLogical(m_renderer, e.button.x, e.button.y, m_mx, m_my);
        if (e.button.button == SDL_BUTTON_LEFT) m_mouseDown = false;
        float wx, wy;
        screenToWorld(m_mx, m_my, wx, wy);
        const Drag d = m_drag;
        m_drag = Drag{};
        switch (d.mode) {
        case DragMode::PAN:
            if (!d.started && e.button.button == SDL_BUTTON_RIGHT) {      // plain right click = drop the brush
                if (m_brush.active) { m_brush.active = false; setStatus("Brush cleared"); }
            }
            break;
        case DragMode::MOVE:
            if (!d.started && !m_shiftDown && m_selection.size() > 1) select(d.primary, false);
            break;
        case DragMode::BOX:
            if (d.started) {
                for (const auto& o : m_model.objs) {
                    if (!selectable(o) || !intersects(objBounds(o), m_boxRect)) continue;
                    if (!isSelected(o.id)) m_selection.push_back(o.id);
                }
            }
            break;
        case DragMode::DRAW:
            placeBrush(d.startWX, d.startWY, wx, wy, d.started);
            break;
        case DragMode::PALETTE:
            if (d.started && inside(canvasRect(), m_mx, m_my)) {
                m_brush = {true, m_paletteCandKind, m_paletteCandPreset};
                placeBrush(wx, wy, wx, wy, false);
                if (info(m_paletteCandKind).singleton) m_brush.active = false;
                else m_brush.active = false;                   // a drop places once; a click keeps the brush
            } else if (!d.started) {
                m_brush = {true, m_paletteCandKind, m_paletteCandPreset};
                setStatus(std::string("Brush: ") + info(m_paletteCandKind).name + " " + m_paletteCandPreset +
                          "  (click / drag in the level, right click or Esc to stop)");
            }
            break;
        default: break;
        }
        m_boxRect = {0, 0, 0, 0};
        return true;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        toLogical(m_renderer, mx, my, m_mx, m_my);
        if (m_modal.active) return true;
        const float dy = e.wheel.y;
        if (inside(canvasRect(), m_mx, m_my)) {
            if (e.wheel.x != 0.0f) m_camX += e.wheel.x * 120.0f / m_zoom;      // trackpad / tilt wheel = pan sideways
            if (m_ctrlDown) m_camX -= dy * 120.0f / m_zoom;
            else if (m_shiftDown) m_camY -= dy * 120.0f / m_zoom;
            else zoomAt(m_mx, m_my, std::pow(1.15f, dy));
        } else if (m_mx < kLeft) {
            m_paletteScroll = std::max(0.0f, m_paletteScroll - dy * 40.0f);
        } else if (m_mx > kW - kRight) {
            m_inspectorScroll = std::max(0.0f, m_inspectorScroll - dy * 40.0f);
        }
        return true;
    }

    // --------------------------------------------------------------------------------- keyboard
    case SDL_EVENT_KEY_UP:
        updateMods(e.key.mod);
        if (e.key.key == SDLK_SPACE) m_spaceDown = false;
        return false;

    case SDL_EVENT_KEY_DOWN: {
        updateMods(e.key.mod);
        const SDL_Keycode k = e.key.key;
        if (m_edit.active) {
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_TAB) commitEdit();
            else if (k == SDLK_ESCAPE) cancelEdit();
            else if (k == SDLK_BACKSPACE) utf8Pop(m_edit.buf);
            return true;
        }
        if (m_modal.active) {
            if (k == SDLK_ESCAPE && !m_modal.buttons.empty()) {
                auto cb = m_modal.onChoice;
                const int last = (int)m_modal.buttons.size() - 1;
                m_modal = Modal{};
                if (cb) cb(last);
            }
            return true;
        }
        if (k == SDLK_SPACE) { m_spaceDown = true; return true; }

        float wx, wy;
        screenToWorld(m_mx, m_my, wx, wy);
        const bool mouseInCanvas = inside(canvasRect(), m_mx, m_my);
        const float step = m_shiftDown ? 1.0f : (float)kGrids[m_gridIndex];

        if (m_ctrlDown) {
            switch (k) {
                case SDLK_S: if (m_shiftDown) { /* save as: ask for a number */
                                 beginTextEdit(3001, std::to_string(m_level), true, [this](const std::string& s) {
                                     const int n = std::atoi(s.c_str());
                                     if (n >= 0) saveLevel(n);
                                 });
                             } else saveLevel(m_level);
                             return true;
                case SDLK_Z: if (m_shiftDown) redo(); else undo(); return true;
                case SDLK_Y: redo(); return true;
                case SDLK_C: copySelection(); return true;
                case SDLK_X: copySelection(); deleteSelection(); return true;
                case SDLK_V: {
                    float px = wx, py = wy;
                    if (!mouseInCanvas) { const SDL_FRect c = canvasRect(); screenToWorld(c.x + c.w * 0.5f, c.y + c.h * 0.5f, px, py); }
                    pasteClipboard(px, py);
                    return true;
                }
                case SDLK_D: duplicateSelection(); return true;
                case SDLK_A:
                    m_selection.clear();
                    for (const auto& o : m_model.objs) if (selectable(o)) m_selection.push_back(o.id);
                    return true;
                case SDLK_N: showModal("New level", "Discard the current level and start an empty one?", {"New level", "Cancel"},
                                       [this](int c) { if (c == 0) newLevel(); });
                             return true;
                default: break;
            }
            return true;
        }

        switch (k) {
            case SDLK_ESCAPE:
                if (m_brush.active) { m_brush.active = false; setStatus("Brush cleared"); }
                else if (m_showHelp) m_showHelp = false;
                else clearSelection();
                return true;
            case SDLK_DELETE: case SDLK_BACKSPACE: deleteSelection(); return true;
            case SDLK_F1: m_showHelp = !m_showHelp; return true;
            case SDLK_F5: startPlaytest(m_shiftDown); return true;
            case SDLK_G: m_showGrid = !m_showGrid; return true;
            case SDLK_O: m_showOutlines = !m_showOutlines; return true;
            case SDLK_S: m_snap = !m_snap; setStatus(m_snap ? "Snap ON" : "Snap OFF"); return true;
            case SDLK_V: m_brush.active = false; return true;
            case SDLK_H: m_handTool = !m_handTool; setStatus(m_handTool ? "Pan tool ON: left drag pans (H to switch off)" : "Pan tool OFF"); return true;
            case SDLK_1: m_layers.background = !m_layers.background; return true;
            case SDLK_2: m_layers.foreground = !m_layers.foreground; return true;
            case SDLK_3: m_layers.walls = !m_layers.walls; return true;
            case SDLK_HOME: frameAll(); return true;
            case SDLK_LEFTBRACKET: m_gridIndex = std::max(0, m_gridIndex - 1); return true;
            case SDLK_RIGHTBRACKET: m_gridIndex = std::min(2, m_gridIndex + 1); return true;
            case SDLK_EQUALS: case SDLK_KP_PLUS: zoomAt(m_mx, m_my, 1.2f); return true;
            case SDLK_MINUS: case SDLK_KP_MINUS: zoomAt(m_mx, m_my, 1.0f / 1.2f); return true;
            case SDLK_R: rotateTraps(); return true;
            case SDLK_PAGEUP: reorderSelection(1); return true;
            case SDLK_PAGEDOWN: reorderSelection(-1); return true;
            case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN: {
                const float dx = k == SDLK_LEFT ? -step : (k == SDLK_RIGHT ? step : 0.0f);
                const float dy = k == SDLK_UP ? -step : (k == SDLK_DOWN ? step : 0.0f);
                if (m_selection.empty()) {
                    m_camX += dx * 4.0f / m_zoom;
                    m_camY += dy * 4.0f / m_zoom;
                } else {
                    beginEdit();
                    for (int id : m_selection) if (Obj* o = m_model.find(id)) translateObj(*o, dx, dy);
                }
                return true;
            }
            default: break;
        }
        return false;
    }
    default: break;
    }
    return false;
}

// ================================================================================================
// update
// ================================================================================================
void EditorState::update(float dt) {
    m_clock += dt;
    if (m_statusTime > 0) m_statusTime -= dt;
    if (m_playtesting) return;
    if (m_sceneDirty) {
        m_scene.rebuild(m_model, m_level, m_parallax);
        m_sceneDirty = false;
    }
    m_scene.update(dt);
}

// ================================================================================================
// small ui toolkit (immediate mode: widgets are drawn and clicked in render())
// ================================================================================================
bool EditorState::clicked(const SDL_FRect& r) const { return m_uiClick && inside(r, m_uiClickX, m_uiClickY); }

bool EditorState::button(const SDL_FRect& r, const std::string& label, bool active, SDL_Color tint) {
    const bool hot = hit(r) && !m_modal.active;
    SDL_Color bg = active ? SDL_Color{96, 86, 40, 255} : (hot ? cHot : cPanel2);
    if (tint.a) bg = hot ? SDL_Color{(Uint8)std::min(255, tint.r + 30), (Uint8)std::min(255, tint.g + 30), (Uint8)std::min(255, tint.b + 30), 255} : tint;
    m_gfx.fill(r, bg);
    m_gfx.frame(r, active ? cAccent : cLine);
    m_gfx.text(label, r.x + r.w * 0.5f, r.y + (r.h - 16.0f) * 0.5f, 16, active ? cAccent : cText, Align::CENTER);
    return !m_modal.active && clicked(r);
}

void EditorState::beginTextEdit(int id, const std::string& initial, bool numeric, std::function<void(const std::string&)> commit) {
    m_edit.active = true;
    m_edit.id = id;
    m_edit.buf = initial;
    m_edit.numeric = numeric;
    m_edit.commit = std::move(commit);
    if (SDL_Window* w = Engine::Get().getWindow()) SDL_StartTextInput(w);
}

void EditorState::commitEdit() {
    if (!m_edit.active) return;
    auto fn = m_edit.commit;
    const std::string buf = m_edit.buf;
    m_edit = Edit{};
    if (SDL_Window* w = Engine::Get().getWindow()) SDL_StopTextInput(w);
    if (fn) fn(buf);
}

void EditorState::cancelEdit() {
    m_edit = Edit{};
    if (SDL_Window* w = Engine::Get().getWindow()) SDL_StopTextInput(w);
}

bool EditorState::numberField(const SDL_FRect& r, int id, float value, std::function<void(float)> apply, int decimals) {
    const bool editing = m_edit.active && m_edit.id == id;
    m_gfx.fill(r, editing ? SDL_Color{20, 20, 32, 255} : (hit(r) ? cHot : cPanel2));
    m_gfx.frame(r, editing ? cAccent : cLine);
    std::string shown = editing ? m_edit.buf + (std::fmod(m_clock, 1.0f) < 0.5f ? "|" : " ") : fmt(value, decimals);
    m_gfx.text(shown, r.x + 6, r.y + (r.h - 16) * 0.5f, 16, editing ? cAccent : cText);
    if (!editing && !m_modal.active && clicked(r)) {
        beginTextEdit(id, fmt(value, decimals), true, [apply](const std::string& s) { apply((float)std::atof(s.c_str())); });
        return true;
    }
    return false;
}

bool EditorState::textField(const SDL_FRect& r, int id, const std::string& value, std::function<void(const std::string&)> apply) {
    const bool editing = m_edit.active && m_edit.id == id;
    m_gfx.fill(r, editing ? SDL_Color{20, 20, 32, 255} : (hit(r) ? cHot : cPanel2));
    m_gfx.frame(r, editing ? cAccent : cLine);
    std::string shown = editing ? m_edit.buf + (std::fmod(m_clock, 1.0f) < 0.5f ? "|" : " ") : value;
    // keep the END of long text visible while typing, the start otherwise
    while (shown.size() > 1 && m_gfx.textWidth(shown, 16) > r.w - 12) {
        if (editing) shown.erase(0, 1); else { shown.pop_back(); }
    }
    m_gfx.text(shown, r.x + 6, r.y + (r.h - 16) * 0.5f, 16, editing ? cAccent : cText);
    if (!editing && !m_modal.active && clicked(r)) {
        beginTextEdit(id, value, false, apply);
        return true;
    }
    return false;
}

void EditorState::showModal(const std::string& title, const std::string& line, std::vector<std::string> buttons, std::function<void(int)> cb) {
    m_modal.active = true;
    m_modal.title = title;
    m_modal.line = line;
    m_modal.buttons = std::move(buttons);
    m_modal.onChoice = std::move(cb);
}

void EditorState::drawModal() {
    if (!m_modal.active) return;
    m_gfx.fill({0, 0, kW, kH}, {0, 0, 0, 150});
    const SDL_FRect box{kW * 0.5f - 280, kH * 0.5f - 90, 560, 180};
    m_gfx.fill(box, cPanel);
    m_gfx.frame(box, cAccent);
    m_gfx.text(m_modal.title, box.x + box.w * 0.5f, box.y + 18, 24, cAccent, Align::CENTER);
    m_gfx.text(m_modal.line, box.x + box.w * 0.5f, box.y + 64, 16, cText, Align::CENTER);
    const int n = (int)m_modal.buttons.size();
    const float bw = 140, gap = 16;
    float x = box.x + (box.w - (bw * n + gap * (n - 1))) * 0.5f;
    for (int i = 0; i < n; i++) {
        const SDL_FRect r{x, box.y + 120, bw, 34};
        const bool hot = hit(r);
        m_gfx.fill(r, hot ? cHot : cPanel2);
        m_gfx.frame(r, cLine);
        m_gfx.text(m_modal.buttons[i], r.x + r.w * 0.5f, r.y + 9, 18, cText, Align::CENTER);
        if (clicked(r)) {
            auto cb = m_modal.onChoice;
            m_modal = Modal{};
            m_uiClick = false;
            if (cb) cb(i);
            return;
        }
        x += bw + gap;
    }
}

// ================================================================================================
// render
// ================================================================================================
void EditorState::render(SDL_Renderer* renderer) {
    if (m_playtesting) return;
    m_gfx.fill({0, 0, kW, kH}, cBg);
    drawCanvas();
    drawPalette();
    drawInspector();
    drawToolbar();
    drawStatusBar();
    if (m_showHelp) drawHelp();
    drawModal();
    m_uiClick = false;
    m_uiRightClick = false;
}

void EditorState::drawCanvas() {
    const SDL_FRect c = canvasRect();
    m_gfx.fill(c, cCanvas);

    // The level itself: the game's builders draw through Camera + SDL_SetRenderScale (= zoom).
    // We deliberately use NO SDL_SetRenderViewport / SDL_SetRenderClipRect around the scene: in SDL3 both are
    // expressed in scaled render coordinates, so combined with a render scale the visible area collapsed into a tiny
    // patch in the top-left of the canvas (and shrank further when zooming out).  Instead the canvas offset is folded
    // into the camera:  screen = (world - cam) * zoom,  cam = view - canvasOrigin / zoom.
    // Anything that spills over the canvas edge is painted over by the border/panels below.
    SDL_SetRenderViewport(m_renderer, nullptr);
    SDL_SetRenderClipRect(m_renderer, nullptr);
    const float camX = std::round(m_camX) - c.x / m_zoom;
    const float camY = std::round(m_camY) - c.y / m_zoom;
    Camera::getInstance().setPosition(camX, camY);
    {
        float px, py;
        parallaxRef(px, py);
        m_scene.setParallaxOffset(px - camX, py - camY);
    }
    SDL_SetRenderScale(m_renderer, m_zoom, m_zoom);
    m_scene.render(m_renderer, m_layers);
    SDL_SetRenderScale(m_renderer, 1.0f, 1.0f);

    // cover whatever the scene drew outside the canvas
    m_gfx.fill({0, 0, kW, c.y}, cBg);
    m_gfx.fill({0, c.y + c.h, kW, kH - (c.y + c.h)}, cBg);
    m_gfx.fill({0, c.y, c.x, c.h}, cBg);
    m_gfx.fill({c.x + c.w, c.y, kW - (c.x + c.w), c.h}, cBg);

    // editor overlays in window coordinates, clipped to the canvas (scale is 1 here, so the clip is exact)
    SDL_Rect clip{(int)c.x, (int)c.y, (int)c.w, (int)c.h};
    SDL_SetRenderClipRect(m_renderer, &clip);
    drawOverlays();
    SDL_SetRenderClipRect(m_renderer, nullptr);
    m_gfx.frame(c, cLine);
}

void EditorState::drawGrid(const SDL_FRect& c) {
    const float g = (float)kGrids[m_gridIndex];
    float stepW = g;
    while (stepW * m_zoom < 8.0f) stepW *= 2.0f;                // never draw a solid wall of lines
    float wx0, wy0, wx1, wy1;
    screenToWorld(c.x, c.y, wx0, wy0);
    screenToWorld(c.x + c.w, c.y + c.h, wx1, wy1);
    for (float x = std::floor(wx0 / stepW) * stepW; x <= wx1; x += stepW) {
        float sx, sy;
        worldToScreen(x, 0, sx, sy);
        const bool major = std::fmod(std::fabs(x), kTile) < 0.01f;
        m_gfx.line(sx, c.y, sx, c.y + c.h, x == 0 ? SDL_Color{255, 120, 120, 150} : (major ? SDL_Color{255, 255, 255, 34} : SDL_Color{255, 255, 255, 14}));
    }
    for (float y = std::floor(wy0 / stepW) * stepW; y <= wy1; y += stepW) {
        float sx, sy;
        worldToScreen(0, y, sx, sy);
        const bool major = std::fmod(std::fabs(y), kTile) < 0.01f;
        m_gfx.line(c.x, sy, c.x + c.w, sy, y == 0 ? SDL_Color{120, 255, 120, 150} : (major ? SDL_Color{255, 255, 255, 34} : SDL_Color{255, 255, 255, 14}));
    }
}

void EditorState::drawObjectOutline(const Obj& o, bool selected) {
    const KindInfo& ki = info(o.kind);
    const SDL_FRect b = objBounds(o);
    float sx, sy;
    worldToScreen(b.x, b.y, sx, sy);
    const SDL_FRect s{sx, sy, b.w * m_zoom, b.h * m_zoom};
    const SDL_Color col = ki.color;
    const bool dimLayer = !selectable(o) && (o.kind == Kind::BACKGROUND || o.kind == Kind::FOREGROUND || o.kind == Kind::WORLD_RECT || o.kind == Kind::ARENA);
    const Uint8 a = selected ? 255 : (dimLayer ? 50 : 120);

    if (o.kind == Kind::SPAWN) {
        if (SDL_Texture* tex = Engine::Get().getAssetManager().getTexture(TextureType::YELLOW_SKIN)) {
            SDL_FRect src{0, 0, 24, 24};
            SDL_FRect dst{sx - 40.0f * m_zoom, sy - 20.0f * m_zoom, 120.0f * m_zoom, 120.0f * m_zoom};
            SDL_RenderTexture(m_renderer, tex, &src, &dst);
        }
    }
    if (o.kind == Kind::LIGHT) {
        m_gfx.circle(sx + s.w * 0.5f, sy + s.h * 0.5f, o.f[0] * m_zoom, withAlpha({(Uint8)o.f[1], (Uint8)o.f[2], (Uint8)o.f[3], 255}, selected ? 230 : 90));
        m_gfx.dot(sx + s.w * 0.5f, sy + s.h * 0.5f, 5, {(Uint8)o.f[1], (Uint8)o.f[2], (Uint8)o.f[3], 255});
    } else if (o.kind == Kind::NPC) {
        m_gfx.fill(s, withAlpha(col, 40));
    } else if (o.kind == Kind::WORLD_RECT) {
        m_gfx.dashedFrame(s, withAlpha(col, a), 10.0f);
    } else if (o.kind == Kind::ARENA) {
        m_gfx.fill(s, withAlpha(col, selected ? 40 : 18));
        m_gfx.dashedFrame(s, withAlpha(col, selected ? 255 : std::max<int>(a, 120)), 14.0f);
    } else if (o.kind == Kind::BOSS) {
        m_gfx.fill(s, withAlpha(col, selected ? 70 : 40));
    }
    if (o.kind != Kind::WORLD_RECT && o.kind != Kind::LIGHT && o.kind != Kind::ARENA) m_gfx.frame(s, withAlpha(col, a));
    if (selected) m_gfx.frame({s.x - 1, s.y - 1, s.w + 2, s.h + 2}, {255, 255, 255, 230});

    // label
    if (selected || s.w > 70) {
        std::string label = std::string(1, ki.tag) + " " + presetOf(o);
        if (o.kind == Kind::NPC) label = "I " + o.s[0];
        if (o.kind == Kind::WORLD_RECT) label = "R world bounds";
        if (o.kind == Kind::ARENA) label = "A boss arena";
        if (o.kind == Kind::BOSS) label = "K " + presetOf(o);
        if (o.kind == Kind::CHECKPOINT) label = "C goal";
        if (o.kind == Kind::SPAWN) label = "S spawn";
        m_gfx.text(label, s.x + 3, s.y - 15, 14, withAlpha(col, selected ? 255 : 190));
    }

    // paths (enemy patrol line, trap line / rectangle)
    const bool showPath = selected || m_showOutlines;
    if (showPath) {
        const SDL_Color pc = withAlpha(col, selected ? 220 : 80);
        if (o.kind == Kind::ENEMY || (o.kind == Kind::TRAP && trapHasPath(o.s[0]))) {
            if (o.kind == Kind::TRAP && o.s[3] == "RECT") {
                float ex, ey;
                worldToScreen(o.f[0], o.f[1], ex, ey);
                m_gfx.dashedFrame({std::min(sx, ex), std::min(sy, ey), std::fabs(ex - sx), std::fabs(ey - sy)}, pc, 6.0f);
            } else {
                auto hs = handlesFor(o);
                if (hs.size() == 2) {
                    float x1, y1, x2, y2;
                    worldToScreen(hs[0].wx, hs[0].wy, x1, y1);
                    worldToScreen(hs[1].wx, hs[1].wy, x2, y2);
                    m_gfx.line(x1, y1, x2, y2, pc);
                }
            }
        }
    }
}

void EditorState::drawOverlays() {
    const SDL_FRect c = canvasRect();
    if (m_showGrid) drawGrid(c);

    for (const auto& o : m_model.objs) {
        const bool sel = isSelected(o.id);
        if (m_showOutlines || sel || o.kind == Kind::SPAWN) drawObjectOutline(o, sel);
    }

    // handles on a single selected object
    if (Obj* s = single()) {
        const SDL_FRect b = objBounds(*s);
        float sx, sy;
        worldToScreen(b.x, b.y, sx, sy);
        if (info(s->kind).sized && selectable(*s)) {
            const float w = b.w * m_zoom, h = b.h * m_zoom;
            const float xs[3] = {sx, sx + w * 0.5f, sx + w}, ys[3] = {sy, sy + h * 0.5f, sy + h};
            const int map[8][2] = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1}};
            for (auto& m : map) m_gfx.dot(xs[m[0]], ys[m[1]], 4, {255, 255, 255, 255});
        }
        for (const auto& h : handlesFor(*s)) {
            float hx, hy;
            worldToScreen(h.wx, h.wy, hx, hy);
            m_gfx.circle(hx, hy, 7, cAccent, 20);
            m_gfx.dot(hx, hy, 3, cAccent);
        }
    }
    if (m_showCamFrame) {
        // what the player's camera shows (1600x720 world px) at the middle of the view: slide the view to see parallax
        float wx, wy;
        screenToWorld(c.x + c.w * 0.5f, c.y + c.h * 0.5f, wx, wy);
        float sx, sy;
        worldToScreen(wx - 800.0f, wy - 360.0f, sx, sy);
        const SDL_FRect r{sx, sy, 1600.0f * m_zoom, 720.0f * m_zoom};
        m_gfx.dashedFrame(r, {255, 255, 255, 200}, 8.0f);
        m_gfx.text("game camera 1600x720", sx + 4, sy + 4, 14, {255, 255, 255, 220});
    }
    drawGhost();

    if (m_drag.mode == DragMode::BOX && m_drag.started) {
        float sx, sy;
        worldToScreen(m_boxRect.x, m_boxRect.y, sx, sy);
        const SDL_FRect r{sx, sy, m_boxRect.w * m_zoom, m_boxRect.h * m_zoom};
        m_gfx.fill(r, {120, 170, 255, 30});
        m_gfx.dashedFrame(r, {160, 200, 255, 255}, 5.0f);
    }
}

void EditorState::drawGhost() {
    if (m_modal.active || !inside(canvasRect(), m_mx, m_my)) return;
    float wx, wy;
    screenToWorld(m_mx, m_my, wx, wy);

    Kind kind = m_brush.kind;
    std::string preset = m_brush.preset;
    bool show = m_brush.active && m_drag.mode != DragMode::PAN;
    if (m_drag.mode == DragMode::PALETTE && m_drag.started) { kind = m_paletteCandKind; preset = m_paletteCandPreset; show = true; }
    if (!show) return;
    const KindInfo& ki = info(kind);
    const SDL_Color col = withAlpha(ki.color, 200);
    const bool drawing = m_drag.mode == DragMode::DRAW && m_drag.started;

    // the object exactly as it would be placed
    Obj o = drawing ? brushObject(kind, preset, m_drag.startWX, m_drag.startWY, wx, wy, true)
                    : brushObject(kind, preset, wx, wy, wx, wy, false);

    // real textures: a one-object scene built from the game's own loader + builders
    const bool hasArt = kind != Kind::NPC && kind != Kind::LIGHT && kind != Kind::WORLD_RECT && kind != Kind::SPAWN &&
                        kind != Kind::BOSS && kind != Kind::ARENA;
    if (m_ghostTex && hasArt) {
        LevelModel gm;
        gm.add(o);
        const std::string key = gm.serialize(0);
        const std::string pkey = key + "#f";          // always flat: the preview must sit exactly under the cursor
        if (pkey != m_ghostKey) { m_ghost.rebuild(gm, 0, false); m_ghostKey = pkey; }
        m_ghost.setParallaxOffset(0.0f, 0.0f);
        const SDL_FRect c = canvasRect();
        // same camera as drawCanvas() (it already holds the canvas offset); no viewport / clip, see drawCanvas()
        SDL_SetRenderClipRect(m_renderer, nullptr);
        SDL_SetRenderScale(m_renderer, m_zoom, m_zoom);
        SceneLayers only{kind == Kind::BACKGROUND, kind == Kind::FOREGROUND, true};
        m_ghost.render(m_renderer, only);
        SDL_SetRenderScale(m_renderer, 1.0f, 1.0f);
        SDL_Rect clip{(int)c.x, (int)c.y, (int)c.w, (int)c.h};
        SDL_SetRenderClipRect(m_renderer, &clip);
    } else if (kind == Kind::SPAWN && m_ghostTex) {
        if (SDL_Texture* tex = Engine::Get().getAssetManager().getTexture(TextureType::YELLOW_SKIN)) {
            float sx, sy;
            worldToScreen(o.x, o.y, sx, sy);
            SDL_FRect src{0, 0, 24, 24};
            SDL_FRect dst{sx - 40.0f * m_zoom, sy - 20.0f * m_zoom, 120.0f * m_zoom, 120.0f * m_zoom};
            SDL_RenderTexture(m_renderer, tex, &src, &dst);
        }
    }

    // outline + size label on top
    const SDL_FRect b = worldBounds(o);
    float sx, sy;
    worldToScreen(b.x, b.y, sx, sy);
    if (ki.sized) {
        const SDL_FRect r{sx, sy, b.w * m_zoom, b.h * m_zoom};
        if (!(m_ghostTex && hasArt)) m_gfx.fill(r, withAlpha(ki.color, 50));
        m_gfx.frame(r, col);
        if (drawing) m_gfx.text(fmt(b.w, 0) + " x " + fmt(b.h, 0), sx + 4, sy + 4, 16, cText);
    } else {
        float cx, cy;
        worldToScreen(o.x, o.y, cx, cy);
        m_gfx.line(cx - 12, cy, cx + 12, cy, col);
        m_gfx.line(cx, cy - 12, cx, cy + 12, col);
    }
    m_gfx.text(std::string(ki.name) + (preset.empty() ? "" : " " + preset), sx + 10, sy + 10, 14, cText);
}

// ================================================================================================
// panels
// ================================================================================================
void EditorState::drawToolbar() {
    m_gfx.fill({0, 0, kW, kTop}, cPanel);
    m_gfx.line(0, kTop, kW, kTop, cLine);
    float x = 6;
    const float y = 5, h = 28;
    auto btn = [&](const std::string& label, bool active = false, SDL_Color tint = {0, 0, 0, 0}) {
        const float w = m_gfx.textWidth(label, 16) + 12;
        const bool r = button({x, y, w, h}, label, active, tint);
        x += w + 4;
        return r;
    };
    if (btn("New")) showModal("New level", "Discard the current level and start an empty one?", {"New level", "Cancel"}, [this](int c) { if (c == 0) newLevel(); });
    if (btn("Save" + std::string(m_dirty ? "*" : ""))) saveLevel(m_level);
    if (btn("As")) beginTextEdit(3001, std::to_string(m_level), true, [this](const std::string& s) { const int n = std::atoi(s.c_str()); if (n >= 0) saveLevel(n); });
    x += 6;
    if (btn("<")) requestSwitchLevel(m_level - 1);
    {
        const std::string label = "Level " + std::to_string(m_level) + (levelExists(m_level) ? "" : " (new)");
        const SDL_FRect r{x, y, 150, h};
        const bool editing = m_edit.active && m_edit.id == 3000;
        m_gfx.fill(r, editing ? SDL_Color{20, 20, 32, 255} : cPanel2);
        m_gfx.frame(r, editing ? cAccent : cLine);
        m_gfx.text(editing ? "go to: " + m_edit.buf + "|" : label, r.x + r.w * 0.5f, r.y + 6, 16, cText, Align::CENTER);
        if (!editing && clicked(r) && !m_modal.active)
            beginTextEdit(3000, "", true, [this](const std::string& s) { if (!s.empty()) requestSwitchLevel(std::atoi(s.c_str())); });
        x += 154;
    }
    if (btn(">")) requestSwitchLevel(m_level + 1);
    x += 10;
    if (btn("Undo")) undo();
    if (btn("Redo")) redo();
    x += 10;
    if (btn(m_snap ? "Snap " + std::to_string(kGrids[m_gridIndex]) : "Snap off", m_snap)) {
        if (!m_snap) m_snap = true; else m_gridIndex = (m_gridIndex + 1) % 3;
    }
    if (btn("Grid", m_showGrid)) m_showGrid = !m_showGrid;
    if (btn("Lines", m_showOutlines)) m_showOutlines = !m_showOutlines;
    if (btn("BG", m_layers.background)) m_layers.background = !m_layers.background;
    if (btn("FG", m_layers.foreground)) m_layers.foreground = !m_layers.foreground;
    if (btn("Walls", m_layers.walls)) m_layers.walls = !m_layers.walls;
    if (btn("Ghost", m_ghostTex)) m_ghostTex = !m_ghostTex;
    if (btn("Pan (H)", m_handTool)) { m_handTool = !m_handTool; setStatus(m_handTool ? "Pan tool ON: left drag pans" : "Pan tool OFF"); }
    if (btn("Parallax", m_parallax)) {
        m_parallax = !m_parallax; m_sceneDirty = true;
        if (m_parallax) m_showCamFrame = true;       // show the 1600x720 frame the layers are laid out for
        setStatus(m_parallax ? "Parallax ON: layers shown as the game camera (dashed frame) sees them" : "Parallax OFF: flat view, every layer at its real position");
    }
    if (btn("Cam", m_showCamFrame)) m_showCamFrame = !m_showCamFrame;
    if (btn("Fit")) frameAll();
    x += 10;
    if (btn("> PLAY (F5)", false, {40, 110, 60, 255})) startPlaytest(false);
    if (btn("> Here", false, {40, 90, 110, 255})) startPlaytest(true);
    x = kW - 54;
    if (button({x, y, 48, h}, "Help", m_showHelp)) m_showHelp = !m_showHelp;
}

SDL_Texture* EditorState::thumbnail(Kind kind, const std::string& preset) {
    const std::string key = std::to_string((int)kind) + "|" + preset;
    auto it = m_thumbs.find(key);
    if (it != m_thumbs.end()) return it->second;

    const int N = 64;
    SDL_Texture* tex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, N, N);
    m_thumbs[key] = tex;
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    const KindInfo& ki = info(kind);
    Obj o = makeDefault(kind, preset, 0, 0);
    const bool hasArt = kind != Kind::NPC && kind != Kind::LIGHT && kind != Kind::WORLD_RECT && kind != Kind::SPAWN &&
                        kind != Kind::BOSS && kind != Kind::ARENA;

    // the palette's scissor must not apply to this offscreen target (and a clip + render scale would shrink it)
    const bool hadClip = SDL_RenderClipEnabled(m_renderer);
    SDL_Rect savedClip{0, 0, 0, 0};
    if (hadClip) SDL_GetRenderClipRect(m_renderer, &savedClip);
    SDL_SetRenderClipRect(m_renderer, nullptr);
    SDL_SetRenderTarget(m_renderer, tex);
    SDL_SetRenderDrawColor(m_renderer, 28, 27, 42, 255);
    SDL_RenderClear(m_renderer);
    if (hasArt) {
        LevelModel gm;
        const int id = gm.add(o);
        m_thumbScene.rebuild(gm, 0, false);
        SDL_FRect b = worldBounds(o);
        SDL_FRect sb;
        if (m_thumbScene.boundsFor(id, sb)) b = sb;
        const float z = std::min(1.5f, std::min((float)N / std::max(1.0f, b.w), (float)N / std::max(1.0f, b.h)));
        Camera::getInstance().setPosition(std::round(b.x + b.w * 0.5f - (float)N * 0.5f / z),
                                          std::round(b.y + b.h * 0.5f - (float)N * 0.5f / z));
        SDL_SetRenderScale(m_renderer, z, z);
        SceneLayers only{kind == Kind::BACKGROUND, kind == Kind::FOREGROUND, true};
        m_thumbScene.render(m_renderer, only);
        SDL_SetRenderScale(m_renderer, 1.0f, 1.0f);
    } else if (kind == Kind::SPAWN) {
        if (SDL_Texture* skin = Engine::Get().getAssetManager().getTexture(TextureType::YELLOW_SKIN)) {
            SDL_FRect src{0, 0, 24, 24}, dst{4, 4, 56, 56};
            SDL_RenderTexture(m_renderer, skin, &src, &dst);
        }
    } else {
        m_gfx.fill({6, 6, 52, 52}, withAlpha(ki.color, 90));
        m_gfx.frame({6, 6, 52, 52}, ki.color);
        m_gfx.text(std::string(1, ki.tag), 32, 18, 28, cText, Align::CENTER);
    }
    SDL_SetRenderTarget(m_renderer, nullptr);
    if (hadClip) SDL_SetRenderClipRect(m_renderer, &savedClip);
    return tex;
}

void EditorState::drawPalette() {
    const SDL_FRect panel{0, kTop, kLeft, kH - kTop - kBottom};
    m_gfx.fill(panel, cPanel);
    m_gfx.line(kLeft, kTop, kLeft, kH - kBottom, cLine);

    // category tabs (2 columns)
    const float bw = (kLeft - 18.0f) / 2.0f, bh = 24.0f;
    float y0 = kTop + 6;
    int i = 0;
    for (Kind k : kTabs) {
        const SDL_FRect r{6 + (i % 2) * (bw + 6), y0 + (i / 2) * (bh + 3), bw, bh};
        const KindInfo& ki = info(k);
        const bool active = (k == m_tab);
        const bool hot = hit(r);
        m_gfx.fill(r, active ? withAlpha(ki.color, 90) : (hot ? cHot : cPanel2));
        m_gfx.frame(r, active ? ki.color : cLine);
        m_gfx.dot(r.x + 9, r.y + r.h * 0.5f, 3, ki.color);
        m_gfx.text(ki.plural, r.x + 18, r.y + 4, 16, active ? cText : cDim);
        if (clicked(r) && !m_modal.active) { m_tab = k; m_brush.active = false; m_paletteScroll = 0; }
        i++;
    }
    const int tabRows = ((int)(sizeof(kTabs) / sizeof(kTabs[0])) + 1) / 2;
    const float listTop = y0 + (float)tabRows * (bh + 3) + 6;
    m_gfx.line(6, listTop - 3, kLeft - 6, listTop - 3, cLine);

    // items of the active tab
    const KindInfo& ki = info(m_tab);
    std::vector<std::string> items;
    if (ki.presets) items = *ki.presets; else items.push_back("");
    const SDL_FRect listArea{0, listTop, kLeft, kH - kBottom - listTop - 54};
    SDL_Rect clip{(int)listArea.x, (int)listArea.y, (int)listArea.w, (int)listArea.h};
    SDL_SetRenderClipRect(m_renderer, &clip);
    const float rowH = 44.0f;
    const float maxScroll = std::max(0.0f, rowH * (float)items.size() - listArea.h);
    m_paletteScroll = std::min(m_paletteScroll, maxScroll);
    for (size_t n = 0; n < items.size(); n++) {
        const SDL_FRect r{6, listTop + (float)n * rowH - m_paletteScroll, kLeft - 12, rowH - 2};
        if (r.y + r.h < listTop || r.y > listTop + listArea.h) continue;
        const bool selected = m_brush.active && m_brush.kind == m_tab && m_brush.preset == items[n];
        const bool hot = hit(r) && inside(listArea, m_mx, m_my) && !m_modal.active;
        m_gfx.fill(r, selected ? withAlpha(ki.color, 80) : (hot ? cHot : cPanel2));
        m_gfx.frame(r, selected ? ki.color : cLine);
        if (SDL_Texture* th = thumbnail(m_tab, items[n])) {
            SDL_FRect dst{r.x + 4, r.y + 3, 36, 36};
            SDL_RenderTexture(m_renderer, th, nullptr, &dst);
            m_gfx.frame(dst, withAlpha(ki.color, 160));
        }
        std::string name = items[n].empty() ? std::string(ki.name) : items[n];
        for (char& ch : name) if (ch == '_') ch = ' ';
        while (name.size() > 3 && m_gfx.textWidth(name, 15) > r.w - 52) name.pop_back();
        m_gfx.text(name, r.x + 48, r.y + 13, 15, cText);
        if (clicked(r) && inside(listArea, m_uiClickX, m_uiClickY) && !m_modal.active) {
            m_paletteCandKind = m_tab;
            m_paletteCandPreset = items[n];
            if (m_mouseDown) {                                 // still held: could become a drag-and-drop
                m_drag = Drag{};
                m_drag.mode = DragMode::PALETTE;
                m_drag.startMX = m_uiClickX; m_drag.startMY = m_uiClickY;
            } else {
                m_brush = {true, m_tab, items[n]};
            }
        }
    }
    SDL_SetRenderClipRect(m_renderer, nullptr);

    // drag ghost following the cursor
    if (m_drag.mode == DragMode::PALETTE && m_drag.started) {
        const std::string t = std::string(info(m_paletteCandKind).name) + " " + m_paletteCandPreset;
        const float w = m_gfx.textWidth(t, 16) + 14;
        m_gfx.fill({m_mx + 12, m_my + 8, w, 22}, {0, 0, 0, 190});
        m_gfx.frame({m_mx + 12, m_my + 8, w, 22}, info(m_paletteCandKind).color);
        m_gfx.text(t, m_mx + 19, m_my + 11, 16, cText);
    }

    // hint
    const float hy = kH - kBottom - 50;
    m_gfx.line(6, hy - 3, kLeft - 6, hy - 3, cLine);
    m_gfx.text("Click = brush, drag = drop", 10, hy, 14, cDim);
    m_gfx.text("Right click / Esc: stop brush", 10, hy + 16, 14, cDim);
    m_gfx.text("Wheel zoom | Space/MMB pan", 10, hy + 32, 14, cDim);
}

void EditorState::drawObjectFields(Obj& o, float x, float& y, float w) {
    const KindInfo& ki = info(o.kind);
    const int id = o.id;
    const float lw = 108.0f, fh = 24.0f, gap = 5.0f;
    const SDL_FRect none{0, 0, 0, 0};
    (void)none;

    auto row = [&](const std::string& label) {
        m_gfx.text(label, x, y + 4, 15, cDim);
        return SDL_FRect{x + lw, y, w - lw, fh};
    };
    auto setF = [this, id](float Obj::*member) {
        return [this, id, member](float v) { beginEdit(); if (Obj* p = m_model.find(id)) p->*member = v; };
    };

    {   // position
        SDL_FRect r = row("X");
        numberField(r, 10, o.x, setF(&Obj::x));
        y += fh + gap;
        r = row("Y");
        numberField(r, 11, o.y, setF(&Obj::y));
        y += fh + gap;
    }
    if (ki.sized) {
        const std::string unitNote = ki.unit != 1.0f ? " (x4 px)" : "";
        SDL_FRect r = row("Width" + unitNote);
        numberField(r, 12, o.w, setF(&Obj::w));
        y += fh + gap;
        r = row("Height" + unitNote);
        numberField(r, 13, o.h, setF(&Obj::h));
        y += fh + gap;
        if (ki.unit != 1.0f) {
            m_gfx.text("world size " + fmt(o.w * ki.unit, 0) + " x " + fmt(o.h * ki.unit, 0) + " px", x, y, 14, cDim);
            y += 18;
        }
    }
    y += 4;

    int fieldId = 100;
    for (const FieldDef& f : ki.fields) {
        if (o.kind == Kind::TRAP && f.slot == 5 && f.type == FieldType::ENUM && !trapCanRotate(o.s[0])) continue;
        SDL_FRect r = row(f.label);
        fieldId++;
        switch (f.type) {
        case FieldType::ENUM: {
            const auto& opts = *f.options;
            std::string& cur = o.s[f.slot];
            int idx = 0;
            for (size_t k = 0; k < opts.size(); k++) if (opts[k] == cur) idx = (int)k;
            const SDL_FRect left{r.x, r.y, 22, fh}, right{r.x + r.w - 22, r.y, 22, fh}, mid{r.x + 24, r.y, r.w - 48, fh};
            int dir = 0;
            if (button(left, "<")) dir = -1;
            if (button(right, ">")) dir = 1;
            m_gfx.fill(mid, cPanel2);
            m_gfx.frame(mid, cLine);
            std::string shown = cur.empty() ? "(none)" : cur;
            while (shown.size() > 3 && m_gfx.textWidth(shown, 15) > mid.w - 8) shown.pop_back();
            m_gfx.text(shown, mid.x + mid.w * 0.5f, mid.y + 4, 15, cText, Align::CENTER);
            if (clicked(mid) && !m_modal.active) dir = 1;
            if (dir != 0) {
                beginEdit();
                Obj* p = m_model.find(id);
                if (p) {
                    const int n = (int)opts.size();
                    p->s[f.slot] = opts[(idx + dir + n) % n];
                    if (p->kind == Kind::TRAP && f.slot == 0) {         // new trap type: take that trap's usual settings
                        Obj d = makeDefault(Kind::TRAP, p->s[0], p->x, p->y);
                        for (int q = 1; q < 5; q++) p->s[q] = d.s[q];
                        for (int q = 0; q < 5; q++) p->f[q] = d.f[q];
                        if (!trapCanRotate(p->s[0])) p->s[5] = "UP";      // only spikes / fire / trampoline can face a direction
                    }
                    if (p->kind == Kind::ENEMY && f.slot == 0) { /* keep path */ }
                }
            }
            break;
        }
        case FieldType::FLOAT: {
            const int slot = f.slot;
            const int dec = f.step > 0 && f.step < 1 ? 2 : 1;
            numberField(r, fieldId, o.f[slot], [this, id, slot](float v) { beginEdit(); if (Obj* p = m_model.find(id)) p->f[slot] = v; }, dec);
            break;
        }
        case FieldType::BOOL: {
            const bool on = o.f[f.slot] != 0.0f;
            if (button(r, on ? "ON" : "OFF", on)) {
                beginEdit();
                if (Obj* p = m_model.find(id)) p->f[f.slot] = on ? 0.0f : 1.0f;
            }
            break;
        }
        case FieldType::TEXT: {
            const int slot = f.slot;
            if (f.slot == 1 && o.kind == Kind::NPC) {              // long dialog gets its own full-width row
                y += fh + 2;
                r = {x, y - 2, w, fh};
            }
            textField(r, fieldId, o.s[slot], [this, id, slot](const std::string& v) { beginEdit(); if (Obj* p = m_model.find(id)) p->s[slot] = v; });
            break;
        }
        }
        y += fh + gap;
    }
}

void EditorState::drawLevelSettings(float x, float& y, float w) {
    const float lw = 128.0f, fh = 24.0f, gap = 5.0f;
    m_gfx.text("LEVEL SETTINGS", x, y, 16, cAccent);
    y += 24;
    SDL_FRect r{x, y, w, fh};
    if (button(r, m_model.ambient.has ? "Darkness / light line: ON" : "Darkness / light line: OFF", m_model.ambient.has)) {
        beginEdit();
        m_model.ambient.has = !m_model.ambient.has;
    }
    y += fh + gap;
    if (m_model.ambient.has) {
        auto row = [&](const std::string& label, float value, float Ambient::*member, int dec) {
            m_gfx.text(label, x, y + 4, 15, cDim);
            numberField({x + lw, y, w - lw, fh}, 2000 + (int)(member == &Ambient::darkness ? 0 : (member == &Ambient::radius ? 1 : 2)), value,
                        [this, member](float v) { beginEdit(); m_model.ambient.*member = v; }, dec);
            y += fh + gap;
        };
        row("Darkness 0..1", m_model.ambient.darkness, &Ambient::darkness, 2);
        row("Player light radius", m_model.ambient.radius, &Ambient::radius, 0);
        row("Player softness", m_model.ambient.softness, &Ambient::softness, 0);
    }
    y += 8;
    m_gfx.text("WEATHER / SUN", x, y, 16, cAccent);
    y += 22;
    drawWeatherFields(x, y, w);
    y += 8;
    m_gfx.text("MUSIC", x, y, 16, cAccent);
    y += 22;
    {
        const Music& cur = m_model.music;
        const int mode = !cur.has ? 0 : (cur.silent ? 2 : 1);
        const float third = (w - 12) / 3;
        if (button({x, y, third, fh}, "Default", mode == 0) && mode != 0) {
            beginEdit(); stopMusicPreview(); m_model.music = Music{};
        }
        if (button({x + third + 6, y, third, fh}, "File", mode == 1) && mode != 1) {
            beginEdit(); stopMusicPreview();
            m_model.music.has = true; m_model.music.silent = false;
            if (m_model.music.file.empty()) m_model.music.file = "level_" + std::to_string(m_level) + ".ogg";
        }
        if (button({x + (third + 6) * 2, y, third, fh}, "Silent", mode == 2) && mode != 2) {
            beginEdit(); stopMusicPreview(); m_model.music.has = true; m_model.music.silent = true;
        }
        y += fh + gap;
        if (mode == 1) {
            m_gfx.text("File", x, y + 4, 15, cDim);
            textField({x + lw, y, w - lw, fh}, 2100, cur.file, [this](const std::string& v) {
                beginEdit(); stopMusicPreview();
                std::string f = v;
                f.erase(std::remove(f.begin(), f.end(), ' '), f.end());
                m_model.music.file = f;
            });
            y += fh + gap;
            m_gfx.text("Volume 0..2", x, y + 4, 15, cDim);
            numberField({x + lw, y, w - lw, fh}, 2101, cur.gain,
                        [this](float v) { beginEdit(); m_model.music.gain = std::clamp(v, 0.0f, 2.0f); }, 2);
            y += fh + gap;
        }
        const std::string path = musicPathFor(m_model, m_level);
        if (path.empty()) {
            m_gfx.text("This level is silent.", x, y, 14, cDim);
            y += 20;
        } else {
            std::error_code ec;
            const bool found = fs::exists(path, ec);
            std::string shown = path;
            while (shown.size() > 4 && m_gfx.textWidth(shown, 14) > w) shown.erase(0, 1);
            m_gfx.text(shown, x, y, 14, cDim);
            y += 18;
            m_gfx.text(found ? "file found" : (mode == 0 ? "no file yet: the level plays silent" : "file not found: the level plays silent"),
                       x, y, 14, found ? cGood : cBad);
            y += 20;
            if (found) {
                if (m_musicPreview) {
                    if (button({x, y, w, fh}, "Stop music", true)) stopMusicPreview();
                } else if (button({x, y, w, fh}, "Play music")) {
                    Engine::Get().getAssetManager().playMusicFile(path, m_model.music.has ? m_model.music.gain : 1.0f);
                    m_musicPreview = true;
                }
                y += fh + gap;
            }
        }
        m_gfx.text("Put .ogg files in Sound/Music/", x, y, 13, cDim);
        y += 20;
    }
    y += 8;
    m_gfx.text("OBJECTS", x, y, 16, cAccent);
    y += 22;
    for (Kind k : kTabs) {
        const int n = m_model.count(k);
        if (!n) continue;
        m_gfx.dot(x + 5, y + 8, 3, info(k).color);
        m_gfx.text(std::string(info(k).name) + ": " + std::to_string(n), x + 16, y, 15, cText);
        y += 18;
    }
    if (m_model.count(Kind::SPAWN) == 0) { m_gfx.text("! no spawn point (S)", x, y, 15, cBad); y += 18; }
    if (m_model.count(Kind::CHECKPOINT) == 0) { m_gfx.text("! no checkpoint (C) - level has no goal", x, y, 15, cBad); y += 18; }
    if (m_model.count(Kind::WORLD_RECT) == 0) { m_gfx.text("! no world bounds (R)", x, y, 15, cBad); y += 18; }
    if (m_model.count(Kind::ARENA) && m_model.count(Kind::BOSS) == 0) { m_gfx.text("! boss arena (A) but no boss (K)", x, y, 15, cBad); y += 18; }
    if (m_model.count(Kind::BOSS) && m_model.count(Kind::ARENA) == 0) { m_gfx.text("boss without arena (A): one screen around it", x, y, 14, cDim); y += 18; }
}

// weather / ambience / sun as fields: the Z line of the level file is generated from these (see weatherToLine)
void EditorState::drawWeatherFields(float x, float& y, float w) {
    Weather& wx = m_model.weather;
    const float fh = 24.0f, gap = 5.0f, lw = 108.0f;
    const float btnW = 56.0f;

    if (button({x, y, w, fh}, wx.has ? "Weather / sun settings: ON" : "Weather / sun settings: OFF (biome decides)", wx.has)) {
        beginEdit();
        wx.has = !wx.has;
        if (wx.has && weatherToLine(wx) == "NONE") wx.autoBase = true;      // switching it on = the biome's normal ambience
    }
    y += fh + gap;
    if (!wx.has) return;

    if (button({x, y, w, fh}, wx.autoBase ? "Keep biome ambience: YES" : "Keep biome ambience: NO (only what is set)", wx.autoBase)) {
        beginEdit();
        wx.autoBase = !wx.autoBase;
    }
    y += fh + gap;

    // ready-made looks
    {
        const auto& names = weatherPresetNames();
        const int perRow = 4;
        const float q = (w - (perRow - 1) * 4.0f) / perRow;
        for (size_t i = 0; i < names.size(); i++) {
            const int col = (int)(i % perRow), rowN = (int)(i / perRow);
            const SDL_FRect r{x + col * (q + 4.0f), y + rowN * (fh + 3.0f), q, fh};
            if (button(r, names[i])) {
                beginEdit();
                weatherPreset(wx, names[i]);
            }
        }
        y += (float)((names.size() + perRow - 1) / perRow) * (fh + 3.0f) + 4.0f;
    }

    auto rowRect = [&](const std::string& label) {
        m_gfx.text(label, x, y + 4, 15, cDim);
        return SDL_FRect{x + lw, y, w - lw, fh};
    };

    // one number cell that edits p[i].v[k] (clamped to the parameter's range)
    auto cell = [&](const SDL_FRect& r, int fieldId, int i, int k, int dec) {
        const float lo = wxInfo(i).lo, hi = wxInfo(i).hi;
        numberField(r, fieldId, wx.p[i].v[k], [this, i, k, lo, hi](float v) {
            beginEdit();
            m_model.weather.p[i].v[k] = std::clamp(v, lo, hi);
        }, dec);
    };

    // ---- weather flags: Biome / ON strength / OFF
    for (int i = WX_RAIN; i <= WX_SUNFLARE; i++) {
        const WxParamInfo& pi = wxInfo(i);
        WxValue& v = wx.p[i];
        const SDL_FRect r = rowRect(pi.label);
        const char* txt = v.mode == 0 ? "Biome" : (v.mode == 1 ? "ON" : "OFF");
        if (button({r.x, r.y, btnW, fh}, txt, v.mode == 1)) {
            beginEdit();
            v.mode = (v.mode + 1) % 3;
            if (v.mode == 1) v.v[0] = pi.def;
        }
        if (v.mode == 1) cell({r.x + btnW + 4, r.y, r.w - btnW - 4, fh}, 3000 + i * 8, i, 0, 2);
        y += fh + gap;
    }
    // ---- plain numbers: Biome / set
    for (int i = WX_SNOWSIZE; i <= WX_SOUND; i++) {
        const WxParamInfo& pi = wxInfo(i);
        WxValue& v = wx.p[i];
        const SDL_FRect r = rowRect(pi.label);
        if (button({r.x, r.y, btnW, fh}, v.mode == 1 ? "SET" : "Biome", v.mode == 1)) {
            beginEdit();
            v.mode = v.mode == 1 ? 0 : 1;
            if (v.mode == 1) v.v[0] = pi.def;
        }
        if (v.mode == 1) cell({r.x + btnW + 4, r.y, r.w - btnW - 4, fh}, 3000 + i * 8, i, 0, 2);
        y += fh + gap;
    }
    // ---- colour (r g b)
    auto colourRow = [&](int i) {
        const WxParamInfo& pi = wxInfo(i);
        WxValue& v = wx.p[i];
        const SDL_FRect r = rowRect(pi.label);
        if (button({r.x, r.y, btnW, fh}, v.mode == 1 ? "SET" : "Biome", v.mode == 1)) {
            beginEdit();
            v.mode = v.mode == 1 ? 0 : 1;
            if (v.mode == 1) {
                if (i == WX_GLOW) { v.v[0] = 150; v.v[1] = 255; v.v[2] = 120; }
                else              { v.v[0] = 255; v.v[1] = 222; v.v[2] = 165; }
            }
        }
        y += fh + gap;
        if (v.mode == 1) {
            const float cw = (w - 8.0f) / 3.0f;
            for (int k = 0; k < 3; k++) cell({x + k * (cw + 4.0f), y, cw, fh}, 3000 + i * 8 + k, i, k, 0);
            y += fh + gap;
        }
    };
    colourRow(WX_GLOW);

    // ---- footsteps
    {
        const SDL_FRect r = rowRect("Footsteps");
        const auto& opts = wxStepSurfaces();
        int idx = -1;
        for (size_t k = 0; k < opts.size(); k++) if (opts[k] == wx.steps) idx = (int)k;
        const SDL_FRect left{r.x, r.y, 22, fh}, right{r.x + r.w - 22, r.y, 22, fh}, mid{r.x + 24, r.y, r.w - 48, fh};
        int dir = 0;
        if (button(left, "<")) dir = -1;
        if (button(right, ">")) dir = 1;
        m_gfx.fill(mid, cPanel2);
        m_gfx.frame(mid, cLine);
        m_gfx.text(idx < 0 ? "Automatic" : opts[idx], mid.x + mid.w * 0.5f, mid.y + 4, 15, cText, Align::CENTER);
        if (clicked(mid) && !m_modal.active) dir = 1;
        if (dir != 0) {
            beginEdit();
            const int n = (int)opts.size() + 1;                    // +1 = automatic
            int next = ((idx + 1) + dir + n) % n - 1;
            wx.steps = next < 0 ? std::string() : opts[next];
        }
        y += fh + gap;
    }

    // ---- sun
    y += 4;
    m_gfx.text("Sun", x, y, 15, cAccent);
    y += 20;
    {
        static const char* kModes[4] = {"Biome decides", "Preset", "Fixed position", "Moving across the level"};
        const SDL_FRect r = rowRect("Placement");
        const SDL_FRect left{r.x, r.y, 22, fh}, right{r.x + r.w - 22, r.y, 22, fh}, mid{r.x + 24, r.y, r.w - 48, fh};
        int dir = 0;
        if (button(left, "<")) dir = -1;
        if (button(right, ">")) dir = 1;
        m_gfx.fill(mid, cPanel2);
        m_gfx.frame(mid, cLine);
        std::string shown = kModes[std::clamp(wx.sunMode, 0, 3)];
        while (shown.size() > 3 && m_gfx.textWidth(shown, 15) > mid.w - 8) shown.pop_back();
        m_gfx.text(shown, mid.x + mid.w * 0.5f, mid.y + 4, 15, cText, Align::CENTER);
        if (clicked(mid) && !m_modal.active) dir = 1;
        if (dir != 0) { beginEdit(); wx.sunMode = (wx.sunMode + dir + 4) % 4; }
        y += fh + gap;
    }
    if (wx.sunMode == 1) {
        const SDL_FRect r = rowRect("Time of day");
        const auto& opts = wxSunPresets();
        int idx = 0;
        for (size_t k = 0; k < opts.size(); k++) if (opts[k] == wx.sunPreset) idx = (int)k;
        const SDL_FRect left{r.x, r.y, 22, fh}, right{r.x + r.w - 22, r.y, 22, fh}, mid{r.x + 24, r.y, r.w - 48, fh};
        int dir = 0;
        if (button(left, "<")) dir = -1;
        if (button(right, ">")) dir = 1;
        m_gfx.fill(mid, cPanel2);
        m_gfx.frame(mid, cLine);
        m_gfx.text(opts[idx], mid.x + mid.w * 0.5f, mid.y + 4, 15, cText, Align::CENTER);
        if (clicked(mid) && !m_modal.active) dir = 1;
        if (dir != 0) { beginEdit(); const int n = (int)opts.size(); wx.sunPreset = opts[(idx + dir + n) % n]; }
        y += fh + gap;
    } else if (wx.sunMode == 2) {
        m_gfx.text("screen fractions: 0,0 = top left  1,1 = bottom right", x, y, 13, cDim);
        y += 16;
        for (int k = 0; k < 2; k++) {
            const SDL_FRect r = rowRect(k == 0 ? "Sun X" : "Sun Y");
            numberField(r, 3200 + k, wx.sunPos[k], [this, k](float v) { beginEdit(); m_model.weather.sunPos[k] = std::clamp(v, -1.0f, 2.0f); }, 2);
            y += fh + gap;
        }
    } else if (wx.sunMode == 3) {
        m_gfx.text("screen fractions at the start and at the end", x, y, 13, cDim);
        y += 16;
        static const char* kPathLabels[4] = {"Start X", "Start Y", "End X", "End Y"};
        for (int k = 0; k < 4; k++) {
            const SDL_FRect r = rowRect(kPathLabels[k]);
            numberField(r, 3210 + k, wx.sunPath[k], [this, k](float v) { beginEdit(); m_model.weather.sunPath[k] = std::clamp(v, -1.0f, 2.0f); }, 2);
            y += fh + gap;
        }
    }
    // sun flare strength, size, drift, colour
    for (int i : {(int)WX_SUNSIZE, (int)WX_SUNDRIFT}) {        // (flare strength is in the weather list above)
        const WxParamInfo& pi = wxInfo(i);
        WxValue& v = wx.p[i];
        const SDL_FRect r = rowRect(pi.label);
        if (button({r.x, r.y, btnW, fh}, v.mode == 1 ? "SET" : "Biome", v.mode == 1)) {
            beginEdit();
            v.mode = v.mode == 1 ? 0 : 1;
            if (v.mode == 1) v.v[0] = pi.def;
        }
        if (v.mode == 1) cell({r.x + btnW + 4, r.y, r.w - btnW - 4, fh}, 3000 + i * 8, i, 0, 2);
        y += fh + gap;
    }
    colourRow(WX_SUNCOLOR);
    m_gfx.text("The flare fades with the level's darkness (keep D <= 0.3).", x, y, 13, cDim);
    y += 18;
}

void EditorState::drawInspector() {
    const float px = kW - kRight;
    m_gfx.fill({px, kTop, kRight, kH - kTop - kBottom}, cPanel);
    m_gfx.line(px, kTop, px, kH - kBottom, cLine);
    SDL_Rect clip{(int)px, (int)kTop, (int)kRight, (int)(kH - kTop - kBottom)};
    SDL_SetRenderClipRect(m_renderer, &clip);

    const float x = px + 10, w = kRight - 20;
    float y = kTop + 8 - m_inspectorScroll;
    if (m_selection.empty()) {
        drawLevelSettings(x, y, w);
    } else if (m_selection.size() == 1 && m_model.find(m_selection[0])) {
        Obj& o = *m_model.find(m_selection[0]);
        const KindInfo& ki = info(o.kind);
        m_gfx.fill({x, y, w, 26}, withAlpha(ki.color, 80));
        m_gfx.text(std::string(ki.name) + "  [" + ki.tag + "]", x + 8, y + 4, 18, cText);
        y += 32;
        drawObjectFields(o, x, y, w);
        y += 6;
        const float half = (w - 6) / 2;
        if (button({x, y, half, 26}, "Duplicate")) duplicateSelection();
        if (button({x + half + 6, y, half, 26}, "Delete", false, {110, 40, 40, 255})) deleteSelection();
        y += 32;
        if (button({x, y, half, 26}, "Forward (PgUp)")) reorderSelection(1);
        if (button({x + half + 6, y, half, 26}, "Back (PgDn)")) reorderSelection(-1);
        y += 32;
        if (o.kind != Kind::SPAWN && button({x, y, w, 26}, "Playtest from here")) {
            const SDL_FRect b = objBounds(o);
            LevelModel copy = m_model;
            Obj s = makeDefault(Kind::SPAWN, "", b.x + b.w * 0.5f - 20.0f, b.y + b.h * 0.5f - 85.0f);
            copy.add(s);
            autosaveIfDirty();
            m_playtesting = true;
            { stopMusicPreview(); if (!Playtest::start(m_renderer, copy.serialize(m_level), m_level)) m_playtesting = false; }
        }
        y += 32;
    } else {
        m_gfx.text(std::to_string(m_selection.size()) + " objects selected", x, y, 18, cAccent);
        y += 30;
        const float half = (w - 6) / 2;
        if (button({x, y, half, 26}, "Duplicate")) duplicateSelection();
        if (button({x + half + 6, y, half, 26}, "Delete", false, {110, 40, 40, 255})) deleteSelection();
        y += 34;
        m_gfx.text("Drag to move them together.", x, y, 14, cDim);
        y += 18;
        m_gfx.text("Arrows nudge, Ctrl+C / V copy.", x, y, 14, cDim);
    }
    const float contentH = y + m_inspectorScroll - kTop;
    m_inspectorScroll = std::clamp(m_inspectorScroll, 0.0f, std::max(0.0f, contentH - (kH - kTop - kBottom) + 20));
    SDL_SetRenderClipRect(m_renderer, nullptr);
}

void EditorState::drawStatusBar() {
    const float y = kH - kBottom;
    m_gfx.fill({0, y, kW, kBottom}, cPanel);
    m_gfx.line(0, y, kW, y, cLine);
    std::string left = m_statusTime > 0 ? m_status : "Ready";
    m_gfx.text(left, 8, y + 5, 16, m_statusTime > 0 ? cAccent : cDim);

    float wx, wy;
    screenToWorld(m_mx, m_my, wx, wy);
    std::string mid = "x " + fmt(wx, 0) + "  y " + fmt(wy, 0) + "   zoom " + fmt(m_zoom * 100.0f, 0) + "%   grid " + (m_snap ? std::to_string(kGrids[m_gridIndex]) : "off");
    if (const Obj* o = (m_selection.size() == 1 ? m_model.find(m_selection[0]) : nullptr)) {
        const SDL_FRect b = worldBounds(*o);
        mid += "   sel " + fmt(b.w, 0) + "x" + fmt(b.h, 0) + " px";
    }
    m_gfx.text(mid, kW * 0.5f, y + 5, 16, cText, Align::CENTER);
    m_gfx.text("level " + std::to_string(m_level) + (m_dirty ? " *unsaved*" : "  saved") + "   " + std::to_string(m_model.objs.size()) + " objects",
               kW - 8, y + 5, 16, m_dirty ? cBad : cGood, Align::RIGHT);
}

void EditorState::drawHelp() {
    const SDL_FRect c = canvasRect();
    const SDL_FRect box{c.x + 40, c.y + 30, c.w - 80, c.h - 60};
    m_gfx.fill(box, {16, 15, 26, 238});
    m_gfx.frame(box, cAccent);
    const char* lines[] = {
        "LEVEL EDITOR  -  shortcuts",
        "",
        "Palette (left): click an entry to get a brush, then click / drag in the level. Or DRAG an entry straight onto the level.",
        "Select tool: click an object, drag to move, drag the white squares to resize, drag the yellow circles to edit paths / light radius.",
        "Drag on empty space = box select.   Shift+click = add / remove from selection.",
        "",
        "Mouse wheel = zoom     Pan: Pan button / H (left drag), Space + drag, middle or right mouse drag, trackpad side-scroll, Ctrl+wheel / Shift+wheel",
        "[  ]  = grid size 16/32/64     S = snap on/off     hold Alt = ignore snap     G = grid     O = outlines     1 2 3 = hide BG / FG / walls",
        "Arrow keys = nudge selection (Shift = 1px) or pan     Home = fit the level     + / - = zoom",
        "",
        "Ctrl+S save     Ctrl+Shift+S save as     Ctrl+Z / Ctrl+Y undo / redo     Ctrl+C / X / V copy / cut / paste     Ctrl+D duplicate",
        "R = turn spikes / fire / trampolines clockwise (UP, RIGHT, DOWN, LEFT): the selected traps, or the trap brush before placing.",
        "Blocks: a thin one is a HORIZONTAL / VERTICAL strip - the game tiles end caps + middle at native size, so just drag it as long as you like.",
        "Ctrl+A select all     Del = delete     PgUp / PgDn = draw order (background / foreground layers)     Esc = stop brush / deselect",
        "",
        "F5 = playtest this level fullscreen with the real game engine.   Shift+F5 = start at the mouse cursor.",
        "In the playtest: Esc pauses, EXIT returns here; reaching the checkpoint also returns here.",
        "",
        "Background / foreground / world-bounds objects are only selectable while their palette tab is open.",
        "Parallax button: ON = backgrounds / foregrounds laid out as the game camera (dashed 1600x720 frame, centred in the view) sees them; pan to watch them scroll.  OFF = flat, every layer at its real position.",
        "Platforms and grounds store their size in source pixels (x4 in the world); the inspector shows the world size.",
        "Saving keeps a level_N.txt.bak of the file that was on disk. Closing the window autosaves to Levels/_autosave_level_N.txt.",
        "",
        "F1 or Esc = close this help",
    };
    float y = box.y + 14;
    int i = 0;
    for (const char* l : lines) {
        m_gfx.text(l, box.x + 20, y, i == 0 ? 22 : 16, i == 0 ? cAccent : cText);
        y += i == 0 ? 30 : 21;
        i++;
    }
}

// ================================================================================================
// playtest
// ================================================================================================
void EditorState::startPlaytest(bool atCursor) {
    if (m_playtesting) return;
    LevelModel copy = m_model;
    float wx = 0, wy = 0;
    const SDL_FRect c = canvasRect();
    if (atCursor && inside(c, m_mx, m_my)) {
        screenToWorld(m_mx, m_my, wx, wy);
        copy.add(makeDefault(Kind::SPAWN, "", wx - 20.0f, wy - 85.0f));
    } else if (!copy.first(Kind::SPAWN)) {
        screenToWorld(c.x + c.w * 0.5f, c.y + c.h * 0.5f, wx, wy);
        copy.add(makeDefault(Kind::SPAWN, "", wx, wy));
        setStatus("No spawn point: playing from the middle of the view");
    }
    autosaveIfDirty();                       // never lose work to a crash in the game code
    m_drag = Drag{};
    cancelEdit();
    m_playtesting = true;
    { stopMusicPreview(); if (!Playtest::start(m_renderer, copy.serialize(m_level), m_level)) m_playtesting = false; }
}

void EditorState::resumeFromPlaytest(bool completed) {
    m_playtesting = false;
    m_musicPreview = false;                  // Playtest::finish() already stopped the level music
    m_sceneDirty = true;
    m_drag = Drag{};
    m_mouseDown = false;
    m_spaceDown = false;
    m_uiClick = false;
    setStatus(completed ? "Playtest: level completed!" : "Playtest ended", 4.0f);
}

// ================================================================================================
// level music / trap facing
// ================================================================================================
// Same rules as LevelLoader: `M name.ogg` = Sound/Music/name.ogg, a name with a '/' is used as is,
// `M NONE` = silent, no M record = Sound/Music/level_<n>.ogg
std::string EditorState::musicPathFor(const LevelModel& m, int level) const {
    if (m.music.has) {
        if (m.music.silent || m.music.file.empty()) return {};
        std::string f = m.music.file;
        std::replace(f.begin(), f.end(), ' ', '_');
        return f.find('/') == std::string::npos ? "Sound/Music/" + f : f;
    }
    return "Sound/Music/level_" + std::to_string(level) + ".ogg";
}

void EditorState::stopMusicPreview() {
    if (!m_musicPreview) return;
    m_musicPreview = false;
    Engine::Get().getAssetManager().stopMusic();
}

void EditorState::rotateTraps() {
    if (m_brush.active && m_brush.kind == Kind::TRAP) {
        if (!trapCanRotate(m_brush.preset)) { setStatus("Only spikes, fire and trampolines can face a direction"); return; }
        m_brushFacing = nextFacing(m_brushFacing);
        setStatus("Trap brush faces " + m_brushFacing);
        return;
    }
    bool any = false, began = false;
    for (int id : m_selection) {
        Obj* o = m_model.find(id);
        if (!o || o->kind != Kind::TRAP || !trapCanRotate(o->s[0])) continue;
        if (!began) { beginEdit(); began = true; }
        o->s[5] = nextFacing(o->s[5].empty() ? "UP" : o->s[5]);
        any = true;
    }
    if (any) setStatus("Turned the trap clockwise");
    else setStatus("R turns selected spikes / fire / trampolines (or the trap brush)");
}

} // namespace editor
