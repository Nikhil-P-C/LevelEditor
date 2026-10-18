//
// The level editor screen. Pushed by Engine instead of the title screen when built with LEVEL_EDITOR.
//
//  - left panel : palette (every level component: walls, grounds, platforms, blocks, fruits, traps, enemies, NPCs,
//                 checkpoint, spawn, lights, background, foreground, world bounds). Click to pick a brush, or drag an
//                 entry onto the level to drop it.
//  - centre     : the level, drawn by the game's own builders. Place / select / move / resize / box-select / copy /
//                 paste / delete, snapping grid, zoom + pan.
//  - right panel: inspector for the selected object (every field of the level format) or the level settings.
//  - F5         : playtest in fullscreen with the real GameState (Shift+F5 starts at the mouse cursor).
//
#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "State/State.h"
#include "editor/Gfx.h"
#include "editor/LevelModel.h"
#include "editor/EditorScene.h"

namespace editor {

class EditorState : public State {
public:
    explicit EditorState(SDL_Renderer* renderer);
    ~EditorState() override;

    void render(SDL_Renderer* renderer) override;
    void update(float dt) override;
    bool handleEvents(SDL_Event& event) override;

    static EditorState* instance();
    void resumeFromPlaytest(bool completed);      // called by Playtest::finish()
    void autosaveIfDirty();                       // window closed with unsaved work

private:
    // ---- tools / interaction -------------------------------------------------------------------
    enum class DragMode { NONE, PAN, MOVE, RESIZE, BOX, DRAW, PALETTE, HANDLE };
    enum class HandleType { PATH_START, PATH_END, PATH_CORNER, LIGHT_RADIUS };
    struct Handle { HandleType type; float wx, wy; };
    struct Brush { bool active = false; Kind kind = Kind::WALL; std::string preset; };
    struct Drag {
        DragMode mode = DragMode::NONE;
        bool started = false;                     // moved far enough to count as a drag
        float startMX = 0, startMY = 0;           // mouse (logical px) at press
        float startWX = 0, startWY = 0;           // world position at press
        int primary = -1;                         // object id under the cursor (MOVE) / being resized
        int handle = -1;                          // resize handle 0..7, or index into handlesFor()
        SDL_FRect origRect{0, 0, 0, 0};           // world rect before a resize
        std::vector<Obj> orig;                    // objects before a MOVE
        float camX0 = 0, camY0 = 0;               // camera at press (PAN)
    };
    struct Edit {                                 // numeric / text field being typed into
        bool active = false;
        int id = 0;
        std::string buf;
        bool numeric = true;
        std::function<void(const std::string&)> commit;
    };
    struct Modal {
        bool active = false;
        std::string title, line;
        std::vector<std::string> buttons;
        std::function<void(int)> onChoice;
    };

    // ---- level io / history --------------------------------------------------------------------
    std::string levelPath(int n) const;
    bool levelExists(int n) const;
    void newLevel();
    void loadLevel(int n);
    bool saveLevel(int n);
    void requestSwitchLevel(int n);               // asks before dropping unsaved changes
    void beginEdit();                             // snapshot for undo (call BEFORE changing the model)
    void undo();
    void redo();
    void markChanged();
    void setStatus(const std::string& s, float seconds = 3.0f);

    // ---- view ----------------------------------------------------------------------------------
    SDL_FRect canvasRect() const;
    void screenToWorld(float sx, float sy, float& wx, float& wy) const;
    void worldToScreen(float wx, float wy, float& sx, float& sy) const;   // relative to the window (not the canvas)
    float snap(float v) const;
    float snapSize(float v, Kind k) const;
    void zoomAt(float sx, float sy, float factor);
    void frameAll();
    void centerOn(float wx, float wy);
    void parallaxRef(float& px, float& py) const;   // top-left of the game-camera frame used for parallax layers

    // ---- objects -------------------------------------------------------------------------------
    SDL_FRect objBounds(const Obj& o) const;      // world px, as drawn (traps/enemies from the scene, parallax for X/Y)
    bool selectable(const Obj& o) const;
    int  pickAt(float wx, float wy) const;
    bool isSelected(int id) const;
    void select(int id, bool additive);
    void clearSelection();
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void pasteClipboard(float wx, float wy);
    void reorderSelection(int dir);
    void translateObj(Obj& o, float dx, float dy) const;
    std::vector<Handle> handlesFor(const Obj& o) const;
    void applyHandle(Obj& o, const Handle& h, float wx, float wy) const;
    int  resizeHandleAt(const SDL_FRect& screenRect, float mx, float my) const;
    void placeBrush(float wx, float wy, float wx2, float wy2, bool dragged);
    Obj brushObject(Kind kind, const std::string& preset, float wx, float wy, float wx2, float wy2, bool dragged) const;
    Obj* single();

    // ---- ui ------------------------------------------------------------------------------------
    bool hit(const SDL_FRect& r) const;
    bool button(const SDL_FRect& r, const std::string& label, bool active = false, SDL_Color tint = {0, 0, 0, 0});
    bool clicked(const SDL_FRect& r) const;
    void beginTextEdit(int id, const std::string& initial, bool numeric, std::function<void(const std::string&)> commit);
    void commitEdit();
    void cancelEdit();
    bool numberField(const SDL_FRect& r, int id, float value, std::function<void(float)> apply, int decimals = 2);
    bool textField(const SDL_FRect& r, int id, const std::string& value, std::function<void(const std::string&)> apply);
    void showModal(const std::string& title, const std::string& line, std::vector<std::string> buttons, std::function<void(int)> cb);

    void drawCanvas();
    void drawOverlays();
    void drawGrid(const SDL_FRect& canvas);
    void drawObjectOutline(const Obj& o, bool selected);
    void drawToolbar();
    void drawPalette();
    void drawInspector();
    void drawStatusBar();
    void drawHelp();
    void drawModal();
    void drawGhost();
    void drawObjectFields(Obj& o, float x, float& y, float w);
    void drawLevelSettings(float x, float& y, float w);

    // ---- playtest ------------------------------------------------------------------------------
    void startPlaytest(bool atCursor);

    // ---- level music / trap facing --------------------------------------------------------------
    std::string musicPathFor(const LevelModel& m, int level) const;   // file the game would stream ("" = silent)
    void stopMusicPreview();
    void rotateTraps();                           // R: turn the brush / the selected traps clockwise

private:
    SDL_Renderer* m_renderer = nullptr;
    Gfx m_gfx;
    EditorScene m_scene;
    EditorScene m_ghost;                          // one-object scene: the placement preview uses the real textures
    std::string m_ghostKey;
    bool m_ghostTex = true;
    bool m_parallax = false;                      // true = backgrounds/foregrounds are shown as the game camera sees them
    bool m_handTool = false;                      // Pan tool: left drag pans (mouse and touch, no Space / middle button needed)
    bool m_showCamFrame = false;                  // 1600x720 frame = what the player's camera sees
    EditorScene m_thumbScene;
    std::unordered_map<std::string, SDL_Texture*> m_thumbs;
    SDL_Texture* thumbnail(Kind kind, const std::string& preset);
    SceneLayers m_layers;
    LevelModel m_model;
    int m_level = 0;
    bool m_dirty = false;
    bool m_sceneDirty = true;
    std::vector<std::string> m_backedUp;          // level files already backed up this session

    // undo
    std::vector<LevelModel> m_undo, m_redo;

    // view
    float m_camX = -200, m_camY = -300, m_zoom = 0.5f;
    int m_gridIndex = 2;                          // 16 / 32 / 64
    bool m_snap = true;
    bool m_showGrid = true;
    bool m_showOutlines = true;
    bool m_showHelp = false;

    // selection / tools
    std::vector<int> m_selection;
    Brush m_brush;
    Kind m_tab = Kind::WALL;
    float m_paletteScroll = 0;
    std::vector<Obj> m_clipboard;
    Drag m_drag;
    SDL_FRect m_boxRect{0, 0, 0, 0};              // world rect while box selecting / drawing
    float m_inspectorScroll = 0;

    // input state
    float m_mx = 0, m_my = 0;
    bool m_mouseDown = false;
    bool m_uiClick = false;                       // left press seen this frame outside the canvas
    bool m_uiRightClick = false;
    float m_uiClickX = 0, m_uiClickY = 0;
    bool m_spaceDown = false;
    bool m_altDown = false;
    bool m_shiftDown = false;
    bool m_ctrlDown = false;
    std::string m_pendingPaletteKind;             // palette entry pressed (drag candidate)
    bool m_paletteCandidate = false;
    Kind m_paletteCandKind = Kind::WALL;
    std::string m_paletteCandPreset;

    Edit m_edit;
    Modal m_modal;

    std::string m_status;
    float m_statusTime = 0;
    bool m_playtesting = false;
    bool m_musicPreview = false;                  // the level music is playing from the settings panel
    std::string m_brushFacing = "UP";             // facing a newly placed spikes / fire / trampoline gets (R)
    float m_clock = 0;

    friend struct EditorAccess;
};

} // namespace editor
