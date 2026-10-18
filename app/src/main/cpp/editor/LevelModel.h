//
// Level editor data model: an editable list of level objects that reads and writes the exact
// level_N.txt format described in assets/Levels/readme.txt.
//
// The model keeps the raw tokens of every record (sprite names, colliders, trap status ...) so a level
// loads and saves without losing anything. The editor never interprets the file itself for display: it saves
// the model to text and feeds that text to the game's own LevelLoader, so what you see is what the game parses.
//
#pragma once
#include <string>
#include <vector>
#include <SDL3/SDL.h>

namespace editor {

enum class Kind {
    SPAWN = 0,      // S
    WALL,           // W
    GROUND,         // G
    PLATFORM,       // P
    BLOCK,          // B
    FRUIT,          // F
    TRAP,           // T
    ENEMY,          // E
    NPC,            // I  (interactable: sign / npc)
    CHECKPOINT,     // C
    WORLD_RECT,     // R  (wall collision rect = world bounds)
    LIGHT,          // L
    BACKGROUND,     // X
    FOREGROUND,     // Y
    COUNT
};

enum class FieldType { ENUM, FLOAT, BOOL, TEXT };

struct FieldDef {
    const char* label;
    FieldType type;
    int slot;                                   // index into Obj::s (ENUM / TEXT) or Obj::f (FLOAT / BOOL)
    const std::vector<std::string>* options;    // ENUM only
    float step;                                 // FLOAT: arrow-key / wheel step
};

struct KindInfo {
    Kind kind;
    const char* name;           // "Wall"
    const char* plural;         // "Walls" (palette tab)
    char tag;                   // record letter in the level file
    bool sized;                 // has w / h
    bool singleton;             // only one allowed (S, C, R)
    float unit;                 // world px per stored unit of w/h (P and G are stored in source px: 4)
    float defW, defH;           // default stored size when clicked into the level
    SDL_Color color;            // editor accent colour
    int presetSlot;             // Obj::s slot holding the palette "type" (-1 = none)
    const std::vector<std::string>* presets;
    std::vector<FieldDef> fields;
};

const KindInfo& info(Kind k);

struct Obj {
    Kind kind = Kind::FRUIT;
    int id = 0;
    float x = 0, y = 0, w = 0, h = 0;      // stored (file) units
    std::string s[6];                      // traps: s[5] = facing (UP / RIGHT / DOWN / LEFT)
    float f[8] = {0, 0, 0, 0, 0, 0, 0, 0};
};

struct Ambient {
    bool has = false;
    float darkness = 0.20f, radius = 100.0f, softness = 250.0f;
};

// level music: the `M file [gain]` record. Not set = the game's default (Sound/Music/level_<n>.ogg if it exists)
struct Music {
    bool has = false;            // false: no M record is written
    bool silent = false;         // `M NONE`
    std::string file;            // as written in the file: bare name (cave.ogg = Sound/Music/cave.ogg) or a path
    float gain = 1.0f;           // 0..2, 1 = untouched
};

// traps that can face a direction (same list as the game's trapSupportsOrientation())
bool trapCanRotate(const std::string& trapType);
// next facing clockwise: UP -> RIGHT -> DOWN -> LEFT -> UP
std::string nextFacing(const std::string& facing);

class LevelModel {
public:
    std::vector<Obj> objs;
    Ambient ambient;
    Music music;
    std::vector<std::string> extraLines;   // records we do not know: kept and written back untouched
    int nextId = 1;

    void clear();
    Obj* find(int id);
    const Obj* find(int id) const;
    int  indexOf(int id) const;
    int  add(Obj o);                       // assigns an id; a singleton replaces the existing one. returns the id
    void remove(int id);
    int  count(Kind k) const;
    const Obj* first(Kind k) const;

    std::string serialize(int levelNumber) const;
    void parse(const std::string& text);   // replaces the whole model
};

// a fresh object of `kind` with the palette preset applied (fruit type, enemy type, ...) and sane defaults
Obj makeDefault(Kind kind, const std::string& preset, float worldX, float worldY);
// world-pixel rectangle of an object (what you click on). traps / enemies are refined by the scene.
SDL_FRect worldBounds(const Obj& o);
// resize in world px (converts to stored units)
void setWorldRect(Obj& o, float wx, float wy, float ww, float wh);
std::string presetOf(const Obj& o);

constexpr float kScale = 4.0f;       // the game's SCALE
constexpr float kTile  = 64.0f;      // TILE_SIZE * SCALE

} // namespace editor
