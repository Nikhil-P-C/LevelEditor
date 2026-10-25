#include "LevelModel.h"
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <vector>

namespace editor {

// ------------------------------------------------------------------------------------------------
// option lists (kept in sync with level/LevelLoader/LevelTypeConverter.h)
// ------------------------------------------------------------------------------------------------
static const std::vector<std::string> kWallSprites   = {"STONE_BRICK_WALL", "MOSS_WALL", "WOODEN_SHAFT_WALL"};
static const std::vector<std::string> kGroundSprites = {"GREEN_GRASS_GROUND", "ORANGE_GRASS_GROUND", "PINK_GRASS_GROUND", "BRICK_GROUND"};
static const std::vector<std::string> kPlatSprites   = {"GOLD_PLATFORM", "WOODEN_PLATFORM", "STONE_PLATFORM"};
static const std::vector<std::string> kColliders     = {"SOLID", "ONE_WAY", "TRIGGER"};
static const std::vector<std::string> kBlockTypes    = {"CLAY", "STONE", "WAX", "GOLD"};
static const std::vector<std::string> kBlockVariants = {"SINGLE", "VERTICAL", "HORIZONTAL", "BLOCK"};
static const std::vector<std::string> kFruits        = {"BANANA", "APPLE", "ORANGE", "STRAWBERRY"};
static const std::vector<std::string> kTrapTypes     = {"FALLING_PLATFORM", "FAN", "FIRE", "MOVING_PLATFORM_BROWN", "MOVING_PLATFORM_GREY",
                                                        "ROCK_HEAD", "SAW", "SPIKE_HEAD", "SPIKE_BALL", "SPIKES", "TRAMPOLINE"};
static const std::vector<std::string> kTrapStatus    = {"IDLE", "HIT", "ON", "OFF", "TRIGGERED"};
static const std::vector<std::string> kTrapFacing    = {"UP", "RIGHT", "DOWN", "LEFT"};
static const std::vector<std::string> kPathAxis      = {"AUTO", "VERTICAL", "HORIZONTAL", "CIRCLE"};
static const std::vector<std::string> kPathShape     = {"RECT", "LINE", "ARC", "CIRCLE"};
static const std::vector<std::string> kEnemyTypes    = {
        "SPIDER_CHILD", "MOSS_GLOW_WORM", "MOSS_BAT", "SINNERS_CHILD", "CAVE_PREDATOR", "CAVE_RAT", "SHAFTS_RAPTOR", "GIANT_BATS",
        "SHAFTS_WORKER", "ANKYLOSAURUS_CHILD", "HUNTER_RAPTOR", "HUNTER_EAGLE", "PREDATOR_THEROPOD", "THEROPOD_CHILD", "PREDATOR_CAT",
        "MOUNTAIN_CAT", "CROCODYLOMORPH", "TARBOSAURUS_CHILD", "TYRANNOSAURUS_REX_CHILD", "ALBERTOSAURUS", "DASPELTOSAURUS"};
static const std::vector<std::string> kFlags         = {"NO_FLAG", "FLAG_OUT", "FLAG_IDLE"};
static const std::vector<std::string> kBackgrounds   = {
        "GREENERY_SKY", "GREENERY_MOUNTAINS", "GREENERY_TREES", "MEADOWS_SKY", "MEADOWS_CLOUD", "MEADOWS_FAR_PLAIN", "MEADOWS_FLOWER_FIELD",
        "CAVE_SKY", "CAVE_FAR_OBJECT1", "CAVE_FAR_LIGHT1", "CAVE_FAR_OBJECT2", "CAVE_FAR_LIGHT2", "CAVE_FAR_OBJECT3", "CAVE_FAR_OBJECT4",
        "CAVE_FAR_LIGHT3", "CAVE_FAR_OBJECT5"};
static const std::vector<std::string> kForegrounds   = {"GREENERY_NEAR_OBJECT", "GREENERY_NEAR_GRASS", "MEADOWS_NEAR_FLOWERS", "CAVE_NEAR_OBJECT"};

// same names as kTokens in level/Bosses.cpp (the loader rejects anything else)
static const std::vector<std::string> kBossTypes     = {
        "MOSS_LUMINOUS_WYRM", "MOSS_CAVE_SPIDER", "STONE_GIANT_RAT", "STONE_ANCIENT_SINNER", "SHAFT_WARDEN", "SHAFT_BRONTOS",
        "PLAINS_ANKYLOSAUR", "PLAINS_SENTINEL_PARA", "WOODS_THORN_MATRIARCH", "WOODS_TREEHORN_ELDER", "MOUNTAIN_CAT_QUEEN",
        "MOUNTAIN_BROTHER", "PEAK_THE_PREACHER"};

static const std::vector<KindInfo>& table() {
    static const std::vector<KindInfo> t = {
        // kind, name, plural, tag, sized, singleton, unit, defW, defH, colour, presetSlot, presets, fields
        {Kind::SPAWN, "Spawn", "Spawn", 'S', false, true, 1, 0, 0, {80, 220, 120, 255}, -1, nullptr, {}},
        {Kind::WALL, "Wall", "Walls", 'W', true, false, 1, 192, 192, {170, 150, 120, 255}, 0, &kWallSprites,
         {{"Sprite", FieldType::ENUM, 0, &kWallSprites, 0}, {"Collider", FieldType::ENUM, 1, &kColliders, 0}}},
        {Kind::GROUND, "Ground", "Grounds", 'G', true, false, 4, 64, 32, {110, 190, 90, 255}, 0, &kGroundSprites,
         {{"Sprite", FieldType::ENUM, 0, &kGroundSprites, 0}, {"Collider", FieldType::ENUM, 1, &kColliders, 0}}},
        {Kind::PLATFORM, "Platform", "Platforms", 'P', true, false, 4, 128, 16, {240, 200, 70, 255}, 1, &kPlatSprites,
         {{"Collider", FieldType::ENUM, 0, &kColliders, 0}, {"Sprite", FieldType::ENUM, 1, &kPlatSprites, 0}}},
        {Kind::BLOCK, "Block", "Blocks", 'B', true, false, 1, 64, 64, {210, 130, 90, 255}, 1, &kBlockTypes,
         {{"Variant", FieldType::ENUM, 0, &kBlockVariants, 0}, {"Type", FieldType::ENUM, 1, &kBlockTypes, 0}}},
        {Kind::FRUIT, "Fruit", "Fruits", 'F', false, false, 1, 0, 0, {250, 120, 120, 255}, 0, &kFruits,
         {{"Type", FieldType::ENUM, 0, &kFruits, 0}}},
        {Kind::TRAP, "Trap", "Traps", 'T', false, false, 1, 0, 0, {240, 70, 70, 255}, 0, &kTrapTypes,
         {{"Type", FieldType::ENUM, 0, &kTrapTypes, 0},
          {"Status", FieldType::ENUM, 1, &kTrapStatus, 0},
          {"Axis", FieldType::ENUM, 2, &kPathAxis, 0},
          {"Shape", FieldType::ENUM, 3, &kPathShape, 0},
          {"Collider", FieldType::ENUM, 4, &kColliders, 0},
          {"Path start", FieldType::FLOAT, 0, nullptr, 10},
          {"Path end", FieldType::FLOAT, 1, nullptr, 10},
          {"Speed", FieldType::FLOAT, 2, nullptr, 10},
          {"Radius", FieldType::FLOAT, 3, nullptr, 10},
          {"Show chain", FieldType::BOOL, 4, nullptr, 0},
          {"Facing", FieldType::ENUM, 5, &kTrapFacing, 0}}},
        {Kind::ENEMY, "Enemy", "Enemies", 'E', false, false, 1, 0, 0, {230, 60, 160, 255}, 0, &kEnemyTypes,
         {{"Type", FieldType::ENUM, 0, &kEnemyTypes, 0},
          {"Path start X", FieldType::FLOAT, 0, nullptr, 10},
          {"Path end X", FieldType::FLOAT, 1, nullptr, 10},
          {"Speed", FieldType::FLOAT, 2, nullptr, 10},
          {"Aerial", FieldType::BOOL, 3, nullptr, 0}}},
        {Kind::NPC, "NPC / Sign", "NPC", 'I', true, false, 1, 64, 208, {90, 220, 255, 255}, -1, nullptr,
         {{"Speaker", FieldType::TEXT, 0, nullptr, 0}, {"Dialog (| = new page)", FieldType::TEXT, 1, nullptr, 0}}},
        {Kind::CHECKPOINT, "Checkpoint", "Goal", 'C', true, true, 1, 256, 256, {255, 255, 255, 255}, -1, nullptr,
         {{"Flag", FieldType::ENUM, 0, &kFlags, 0}}},
        {Kind::WORLD_RECT, "World bounds", "Bounds", 'R', true, true, 1, 3200, 1536, {255, 90, 255, 255}, -1, nullptr, {}},
        {Kind::LIGHT, "Light", "Lights", 'L', false, false, 1, 0, 0, {255, 230, 120, 255}, -1, nullptr,
         {{"Radius", FieldType::FLOAT, 0, nullptr, 10},
          {"Red", FieldType::FLOAT, 1, nullptr, 5},
          {"Green", FieldType::FLOAT, 2, nullptr, 5},
          {"Blue", FieldType::FLOAT, 3, nullptr, 5},
          {"Intensity 0..1", FieldType::FLOAT, 4, nullptr, 0.05f},
          {"Flicker 0..1", FieldType::FLOAT, 5, nullptr, 0.05f},
          {"Visible core", FieldType::BOOL, 6, nullptr, 0}}},
        {Kind::BACKGROUND, "Background", "Backgr.", 'X', true, false, 1, 1600, 720, {120, 160, 255, 255}, 0, &kBackgrounds,
         {{"Type", FieldType::ENUM, 0, &kBackgrounds, 0},
          {"Parallax X", FieldType::FLOAT, 0, nullptr, 0.01f},
          {"Parallax Y", FieldType::FLOAT, 1, nullptr, 0.01f},
          {"Animated", FieldType::BOOL, 2, nullptr, 0}}},
        {Kind::FOREGROUND, "Foreground", "Foregr.", 'Y', true, false, 1, 2400, 1692, {150, 255, 200, 255}, 0, &kForegrounds,
         {{"Type", FieldType::ENUM, 0, &kForegrounds, 0},
          {"Parallax X", FieldType::FLOAT, 0, nullptr, 0.01f},
          {"Parallax Y", FieldType::FLOAT, 1, nullptr, 0.01f},
          {"Animated", FieldType::BOOL, 2, nullptr, 0}}},
        // boss: s[0] type, s[1..3] music for phase 1 / 2 / 3 (empty = the game's default Sound/Music/Boss/<biome>_<tier>_phase<n>.ogg),
        // f[0] music gain (0 = the boss's default). x / y = top-left of its hitbox, it falls onto the floor.
        {Kind::BOSS, "Boss", "Bosses", 'K', false, false, 1, 0, 0, {255, 120, 40, 255}, 0, &kBossTypes,
         {{"Type", FieldType::ENUM, 0, &kBossTypes, 0},
          {"Phase 1 music", FieldType::TEXT, 1, nullptr, 0},
          {"Phase 2 music", FieldType::TEXT, 2, nullptr, 0},
          {"Phase 3 music", FieldType::TEXT, 3, nullptr, 0},
          {"Music gain (0=default)", FieldType::FLOAT, 0, nullptr, 0.05f}}},
        {Kind::ARENA, "Boss arena", "Arena", 'A', true, true, 1, 1600, 704, {255, 150, 30, 255}, -1, nullptr, {}},
    };
    return t;
}

const KindInfo& info(Kind k) { return table()[(size_t)k]; }

// ------------------------------------------------------------------------------------------------
// model
// ------------------------------------------------------------------------------------------------
bool trapCanRotate(const std::string& t) {
    return t == "SPIKES" || t == "FIRE" || t == "TRAMPOLINE";
}

std::string nextFacing(const std::string& f) {
    for (size_t i = 0; i < kTrapFacing.size(); i++)
        if (kTrapFacing[i] == f) return kTrapFacing[(i + 1) % kTrapFacing.size()];
    return "RIGHT";
}

void LevelModel::clear() {
    objs.clear();
    ambient = Ambient{};
    music = Music{};
    weather = Weather{};
    extraLines.clear();
    nextId = 1;
}

Obj* LevelModel::find(int id) {
    for (auto& o : objs) if (o.id == id) return &o;
    return nullptr;
}
const Obj* LevelModel::find(int id) const {
    for (const auto& o : objs) if (o.id == id) return &o;
    return nullptr;
}
int LevelModel::indexOf(int id) const {
    for (size_t i = 0; i < objs.size(); i++) if (objs[i].id == id) return (int)i;
    return -1;
}
int LevelModel::add(Obj o) {
    if (info(o.kind).singleton) {
        objs.erase(std::remove_if(objs.begin(), objs.end(), [&](const Obj& e) { return e.kind == o.kind; }), objs.end());
    }
    o.id = nextId++;
    objs.push_back(o);
    return o.id;
}
void LevelModel::remove(int id) {
    objs.erase(std::remove_if(objs.begin(), objs.end(), [&](const Obj& e) { return e.id == id; }), objs.end());
}
int LevelModel::count(Kind k) const {
    int n = 0;
    for (const auto& o : objs) if (o.kind == k) n++;
    return n;
}
const Obj* LevelModel::first(Kind k) const {
    for (const auto& o : objs) if (o.kind == k) return &o;
    return nullptr;
}

// ------------------------------------------------------------------------------------------------
// text format
// ------------------------------------------------------------------------------------------------
static std::string num(float v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

std::string LevelModel::serialize(int levelNumber) const {
    std::ostringstream out;
    out << "#Level: " << levelNumber << "\n";

    auto section = [&](Kind k, const char* title) {
        bool any = false;
        for (const auto& o : objs) {
            if (o.kind != k) continue;
            if (!any) { out << "\n# " << title << "\n"; any = true; }
            const char tag = info(k).tag;
            out << tag << ' ';
            switch (k) {
                case Kind::SPAWN:
                    out << num(o.x) << ' ' << num(o.y);
                    break;
                case Kind::WALL: case Kind::GROUND:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << num(o.w) << ' ' << num(o.h) << ' ' << o.s[0] << ' ' << o.s[1];
                    break;
                case Kind::PLATFORM:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << num(o.w) << ' ' << num(o.h) << ' ' << o.s[0] << ' ' << o.s[1];
                    break;
                case Kind::BLOCK:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << num(o.w) << ' ' << num(o.h) << ' ' << o.s[0] << ' ' << o.s[1];
                    break;
                case Kind::FRUIT:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << o.s[0];
                    break;
                case Kind::TRAP:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << o.s[0] << ' ' << o.s[1] << ' ' << num(o.f[0]) << ' ' << num(o.f[1])
                        << ' ' << num(o.f[2]) << ' ' << o.s[2] << ' ' << o.s[3] << ' ' << o.s[4];
                    if (o.s[0] == "SAW" || o.s[0] == "SPIKE_BALL")
                        out << ' ' << num(o.f[3]) << ' ' << (o.f[4] != 0.0f ? "true" : "false");
                    // facing is the last token; UP is the default so it is only written when it matters
                    if (trapCanRotate(o.s[0]) && !o.s[5].empty() && o.s[5] != "UP")
                        out << ' ' << o.s[5];
                    break;
                case Kind::ENEMY:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << o.s[0] << ' ' << num(o.f[0]) << ' ' << num(o.f[1]) << ' ' << num(o.f[2]);
                    if (o.f[3] != 0.0f) out << " AERIAL";
                    break;
                case Kind::NPC: {
                    std::string text = o.s[1];
                    std::replace(text.begin(), text.end(), '\n', ' ');
                    std::string speaker = o.s[0].empty() ? "NPC" : o.s[0];
                    std::replace(speaker.begin(), speaker.end(), ' ', '_');
                    out << num(o.x) << ' ' << num(o.y) << ' ' << num(o.w) << ' ' << num(o.h) << ' ' << speaker << ' ' << text;
                    break;
                }
                case Kind::CHECKPOINT:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << num(o.w) << ' ' << num(o.h) << ' ' << o.s[0];
                    break;
                case Kind::WORLD_RECT: case Kind::ARENA:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << num(o.w) << ' ' << num(o.h);
                    break;
                case Kind::BOSS: {
                    out << num(o.x) << ' ' << num(o.y) << ' ' << o.s[0];
                    // K x y TYPE [p1 [p2 [p3] [gain]]]  ('-' = default track; the loader reads one word per track)
                    auto track = [](std::string t) {
                        if (t.empty()) return std::string("-");
                        std::replace(t.begin(), t.end(), ' ', '_');
                        return t;
                    };
                    const bool gain = o.f[0] > 0.001f;
                    const bool p3 = !o.s[3].empty();
                    if (!o.s[1].empty() || !o.s[2].empty() || p3 || gain) {
                        out << ' ' << track(o.s[1]) << ' ' << track(o.s[2]);
                        if (p3) out << ' ' << track(o.s[3]);
                        if (gain) out << ' ' << num(std::clamp(o.f[0], 0.0f, 2.0f));
                    }
                    break;
                }
                case Kind::LIGHT:
                    out << (int)std::lround(o.x) << ' ' << (int)std::lround(o.y) << ' ' << (int)std::lround(o.f[0]) << ' '
                        << (int)std::lround(o.f[1]) << ' ' << (int)std::lround(o.f[2]) << ' ' << (int)std::lround(o.f[3]) << ' '
                        << num(o.f[4]) << ' ' << num(o.f[5]) << ' ' << (o.f[6] != 0.0f ? 1 : 0);
                    break;
                case Kind::BACKGROUND: case Kind::FOREGROUND:
                    out << num(o.x) << ' ' << num(o.y) << ' ' << num(o.w) << ' ' << num(o.h) << ' ' << num(o.f[0]) << ' ' << num(o.f[1])
                        << ' ' << (o.f[2] != 0.0f ? "true" : "false") << ' ' << o.s[0];
                    break;
                default: break;
            }
            out << "\n";
        }
    };

    if (music.has) {
        out << "\n# music\nM ";
        if (music.silent || music.file.empty()) out << "NONE";
        else {
            std::string f = music.file;
            std::replace(f.begin(), f.end(), ' ', '_');      // the loader reads one word
            out << f;
            if (std::fabs(music.gain - 1.0f) > 0.001f) out << ' ' << num(music.gain);
        }
        out << "\n";
    }
    section(Kind::SPAWN, "spawn");
    section(Kind::WALL, "walls");
    section(Kind::WORLD_RECT, "world bounds (wall collision rect)");
    section(Kind::PLATFORM, "platforms");
    section(Kind::GROUND, "grounds");
    section(Kind::BLOCK, "blocks");
    section(Kind::FRUIT, "fruits");
    section(Kind::TRAP, "traps");
    section(Kind::ENEMY, "enemies");
    section(Kind::BOSS, "bosses");
    section(Kind::ARENA, "boss arena (camera + player are locked to this rectangle during the fight)");
    section(Kind::NPC, "interactables");
    section(Kind::CHECKPOINT, "checkpoint");
    section(Kind::BACKGROUND, "background (drawn in this order)");
    section(Kind::FOREGROUND, "foreground");
    if (ambient.has) {
        out << "\n# lighting\nD " << num(ambient.darkness) << ' ' << num(ambient.radius) << ' ' << num(ambient.softness) << "\n";
    }
    section(Kind::LIGHT, "lights");
    if (weather.has)
        out << "\n# weather / ambience / sun\nZ " << weatherToLine(weather) << "\n";
    if (!extraLines.empty()) {
        out << "\n# unrecognised lines kept from the original file\n";
        for (const auto& l : extraLines) out << l << "\n";
    }
    return out.str();
}

static std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void LevelModel::parse(const std::string& text) {
    clear();
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        std::istringstream ss(t);
        char tag = 0;
        ss >> tag;
        Obj o;
        bool ok = false;
        switch (tag) {
            case 'S':
                o.kind = Kind::SPAWN;
                ok = (bool)(ss >> o.x >> o.y);
                break;
            case 'W': case 'G':
                o.kind = tag == 'W' ? Kind::WALL : Kind::GROUND;
                ok = (bool)(ss >> o.x >> o.y >> o.w >> o.h >> o.s[0] >> o.s[1]);
                break;
            case 'P':
                o.kind = Kind::PLATFORM;
                ok = (bool)(ss >> o.x >> o.y >> o.w >> o.h >> o.s[0] >> o.s[1]);
                break;
            case 'B':
                o.kind = Kind::BLOCK;
                ok = (bool)(ss >> o.x >> o.y >> o.w >> o.h >> o.s[0] >> o.s[1]);
                break;
            case 'F':
                o.kind = Kind::FRUIT;
                ok = (bool)(ss >> o.x >> o.y >> o.s[0]);
                break;
            case 'T': {
                o.kind = Kind::TRAP;
                ok = (bool)(ss >> o.x >> o.y >> o.s[0] >> o.s[1] >> o.f[0] >> o.f[1] >> o.f[2] >> o.s[2] >> o.s[3] >> o.s[4]);
                if (ok) {
                    // tail tokens: [radius showChain] (saw / spike ball) and/or a facing word (UP RIGHT DOWN LEFT)
                    o.s[5] = "UP";
                    int numbers = 0;
                    std::string tok;
                    while (ss >> tok) {
                        if (tok == "UP" || tok == "RIGHT" || tok == "DOWN" || tok == "LEFT") { o.s[5] = tok; continue; }
                        if (numbers == 0) { o.f[3] = (float)std::atof(tok.c_str()); numbers++; }
                        else if (numbers == 1) { o.f[4] = (tok == "true" || tok == "1") ? 1.0f : 0.0f; numbers++; }
                    }
                }
                break;
            }
            case 'E': {
                o.kind = Kind::ENEMY;
                ok = (bool)(ss >> o.x >> o.y >> o.s[0] >> o.f[0] >> o.f[1] >> o.f[2]);
                std::string mode;
                if (ok && (ss >> mode) && mode == "AERIAL") o.f[3] = 1.0f;
                break;
            }
            case 'I': {
                o.kind = Kind::NPC;
                ok = (bool)(ss >> o.x >> o.y >> o.w >> o.h >> o.s[0]);
                if (ok) {
                    std::string rest;
                    std::getline(ss, rest);
                    o.s[1] = trim(rest);
                    std::replace(o.s[0].begin(), o.s[0].end(), '_', ' ');
                }
                break;
            }
            case 'C':
                o.kind = Kind::CHECKPOINT;
                ok = (bool)(ss >> o.x >> o.y >> o.w >> o.h >> o.s[0]);
                break;
            case 'R':
                o.kind = Kind::WORLD_RECT;
                ok = (bool)(ss >> o.x >> o.y >> o.w >> o.h);
                break;
            case 'K': {
                o.kind = Kind::BOSS;
                ok = (bool)(ss >> o.x >> o.y >> o.s[0]);
                if (ok) {
                    auto isNum = [](const std::string& s) {
                        if (s.empty()) return false;
                        char* end = nullptr;
                        std::strtof(s.c_str(), &end);
                        return end && *end == '\0';
                    };
                    auto keep = [](const std::string& s) { return s == "-" ? std::string() : s; };
                    std::string t1, t2, t3, g;
                    if (ss >> t1) {
                        o.s[1] = keep(t1);
                        if (ss >> t2) {
                            o.s[2] = keep(t2);
                            if (ss >> t3) {
                                if (isNum(t3)) o.f[0] = (float)std::atof(t3.c_str());          // K ... p1 p2 gain
                                else {                                                          // K ... p1 p2 p3 [gain]
                                    o.s[3] = keep(t3);
                                    if (ss >> g) o.f[0] = (float)std::atof(g.c_str());
                                }
                            }
                        }
                    }
                }
                break;
            }
            case 'A':
                o.kind = Kind::ARENA;
                ok = (bool)(ss >> o.x >> o.y >> o.w >> o.h);
                break;
            case 'Z': {
                std::string rest;
                std::getline(ss, rest);
                if (!weather.has) { weather = Weather{}; weather.has = true; weather.autoBase = false; }   // a Z line replaces the biome defaults unless AUTO is in it
                weatherFromTokens(weather, rest);
                continue;
            }
            case 'D': {
                float d = 0;
                if (ss >> d) {
                    ambient.has = true;
                    ambient.darkness = d;
                    float r = 0, sft = 0;
                    if (ss >> r >> sft) { ambient.radius = r; ambient.softness = sft; }
                    continue;
                }
                break;
            }
            case 'M': {
                std::string file;
                if (ss >> file) {
                    music.has = true;
                    if (file == "NONE") { music.silent = true; music.file.clear(); music.gain = 1.0f; }
                    else {
                        music.silent = false;
                        music.file = file;
                        float g = 1.0f;
                        music.gain = (ss >> g) ? std::clamp(g, 0.0f, 2.0f) : 1.0f;
                    }
                    continue;
                }
                break;
            }
            case 'L': {
                o.kind = Kind::LIGHT;
                float r = 0, g = 0, b = 0, inten = 0;
                ok = (bool)(ss >> o.x >> o.y >> o.f[0] >> r >> g >> b >> inten);
                if (ok) {
                    o.f[1] = r; o.f[2] = g; o.f[3] = b; o.f[4] = inten; o.f[6] = 1.0f;
                    float flicker = 0; int visible = 1;
                    if (ss >> flicker) { o.f[5] = flicker; if (ss >> visible) o.f[6] = visible != 0 ? 1.0f : 0.0f; }
                }
                break;
            }
            case 'X': case 'Y': {
                o.kind = tag == 'X' ? Kind::BACKGROUND : Kind::FOREGROUND;
                bool anim = false;
                ok = (bool)(ss >> o.x >> o.y >> o.w >> o.h >> o.f[0] >> o.f[1] >> std::boolalpha >> anim >> o.s[0]);
                o.f[2] = anim ? 1.0f : 0.0f;
                break;
            }
            default: break;
        }
        if (ok) {
            // singletons: the game keeps the LAST line of that kind, so do the same
            add(o);
        } else {
            extraLines.push_back(t);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// helpers
// ------------------------------------------------------------------------------------------------
std::string presetOf(const Obj& o) {
    const KindInfo& k = info(o.kind);
    return k.presetSlot >= 0 ? o.s[k.presetSlot] : std::string();
}

// =================================================================================================
// weather / sun  <->  Z line
// =================================================================================================
static const WxParamInfo kWx[WX_COUNT] = {
    {"RAIN",      "Rain",            1, true,  1.0f,  0.2f, 3.0f},
    {"SNOW",      "Snow",            1, true,  1.0f,  0.2f, 3.0f},
    {"THUNDER",   "Thunder",         1, true,  1.0f,  0.2f, 5.0f},
    {"WIND",      "Wind (-=left)",   1, true,  1.0f, -3.0f, 3.0f},
    {"FIREFLIES", "Fireflies",       1, true,  1.0f,  0.0f, 3.0f},
    {"LEAVES",    "Leaves",          1, true,  1.0f,  0.0f, 3.0f},
    {"PETALS",    "Petals",          1, true,  1.0f,  0.0f, 3.0f},
    {"SUNFLARE",  "Sun flare",       1, true,  1.0f,  0.0f, 2.0f},
    {"SNOWSIZE",  "Snowflake size",  1, false, 1.0f,  0.5f, 2.5f},
    {"GUST",      "Gusts",           1, false, 1.0f,  0.0f, 3.0f},
    {"ANGLE",     "Slant (deg)",     1, false, 0.0f, -75.0f, 75.0f},
    {"FOG",       "Fog / haze",      1, false, 0.25f, 0.0f, 1.0f},
    {"DENSITY",   "Particle density",1, false, 1.0f,  0.25f, 1.5f},
    {"SOUND",     "Ambience volume", 1, false, 1.0f,  0.0f, 2.0f},
    {"SUNSIZE",   "Sun size",        1, false, 1.0f,  0.4f, 3.0f},
    {"SUNDRIFT",  "Sun follows cam", 1, false, 0.04f, -0.5f, 0.5f},
    {"GLOW",      "Firefly colour",  3, false, 255.0f, 0.0f, 255.0f},
    {"SUNCOLOR",  "Sun colour",      3, false, 255.0f, 0.0f, 255.0f},
};
const WxParamInfo& wxInfo(int p) { return kWx[p]; }

static const std::vector<std::string> kSunPresets = {"NOON", "MORNING", "AFTERNOON", "DAWN", "SUNSET", "DUSK", "TRAVEL"};
static const std::vector<std::string> kStepSurfaces = {"GRASS", "LEAVES", "SNOW", "PUDDLE", "CONCRETE"};
const std::vector<std::string>& wxSunPresets() { return kSunPresets; }
const std::vector<std::string>& wxStepSurfaces() { return kStepSurfaces; }

static std::string wnum(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s.empty() || s == "-0" ? "0" : s;
}

std::string weatherToLine(const Weather& w) {
    std::vector<std::string> t;
    if (w.autoBase) t.push_back("AUTO");
    for (int i = 0; i < WX_COUNT; i++) {
        const WxParamInfo& pi = kWx[i];
        const WxValue& v = w.p[i];
        if (v.mode == 0) continue;
        if (pi.flag) {
            if (v.mode == 2) { if (w.autoBase) t.push_back(std::string("-") + pi.key); continue; }
            if (std::fabs(v.v[0] - 1.0f) < 0.001f) t.push_back(pi.key);
            else t.push_back(std::string(pi.key) + "=" + wnum(std::clamp(v.v[0], pi.lo, pi.hi)));
        } else if (pi.count == 3) {
            t.push_back(std::string(pi.key) + "=" + std::to_string((int)std::lround(std::clamp(v.v[0], 0.0f, 255.0f))) + "," +
                        std::to_string((int)std::lround(std::clamp(v.v[1], 0.0f, 255.0f))) + "," +
                        std::to_string((int)std::lround(std::clamp(v.v[2], 0.0f, 255.0f))));
        } else {
            t.push_back(std::string(pi.key) + "=" + wnum(std::clamp(v.v[0], pi.lo, pi.hi)));
        }
    }
    if (w.sunMode == 1) t.push_back("SUNPRESET=" + w.sunPreset);
    else if (w.sunMode == 2) t.push_back("SUNPOS=" + wnum(w.sunPos[0]) + "," + wnum(w.sunPos[1]));
    else if (w.sunMode == 3)
        t.push_back("SUNPATH=" + wnum(w.sunPath[0]) + "," + wnum(w.sunPath[1]) + "," + wnum(w.sunPath[2]) + "," + wnum(w.sunPath[3]));
    if (!w.steps.empty()) t.push_back("STEPS=" + w.steps);
    if (!w.unknown.empty()) t.push_back(w.unknown);
    if (t.empty()) return "NONE";
    std::string out;
    for (const std::string& s : t) { if (!out.empty()) out += ' '; out += s; }
    return out;
}

static std::string upperOf(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

static std::vector<float> floatList(const std::string& s) {
    std::vector<float> r;
    std::string cur;
    for (char c : s + ",") {
        if (c == ',') { if (!cur.empty()) r.push_back((float)std::atof(cur.c_str())); cur.clear(); }
        else cur += c;
    }
    return r;
}

void weatherFromTokens(Weather& w, const std::string& tokens) {
    w.has = true;
    std::istringstream ts(tokens);
    std::string raw;
    auto find = [](const std::string& key) {
        std::string k = key;
        if (k == "FIREFLY" || k == "FLIES" || k == "FLY") k = "FIREFLIES";
        else if (k == "PETAL") k = "PETALS";
        else if (k == "LEAF") k = "LEAVES";
        else if (k == "LIGHTNING") k = "THUNDER";
        else if (k == "SUN" || k == "LENSFLARE" || k == "FLARE") k = "SUNFLARE";
        else if (k == "GUSTS") k = "GUST";
        else if (k == "SLANT") k = "ANGLE";
        else if (k == "HAZE") k = "FOG";
        else if (k == "VOLUME") k = "SOUND";
        else if (k == "SUNCOLOUR") k = "SUNCOLOR";
        else if (k == "FIREFLYCOLOR" || k == "FIREFLYCOLOUR") k = "GLOW";
        for (int i = 0; i < WX_COUNT; i++) if (k == kWx[i].key) return i;
        return -1;
    };
    auto keepUnknown = [&w](const std::string& t) { w.unknown += (w.unknown.empty() ? "" : " ") + t; };
    while (ts >> raw) {
        std::string up = upperOf(raw);
        if (up == "AUTO") { w.autoBase = true; continue; }
        if (up == "NONE") continue;
        if (up == "STORM") {
            for (int i : {WX_RAIN, WX_THUNDER, WX_WIND}) { w.p[i].mode = 1; w.p[i].v[0] = 1.0f; }
            continue;
        }
        bool neg = false;
        if (up.size() > 1 && up[0] == '-') { neg = true; up.erase(0, 1); }
        const size_t eq = up.find('=');
        const std::string key = up.substr(0, eq);
        const std::string val = eq == std::string::npos ? std::string() : up.substr(eq + 1);
        const std::vector<float> nums = floatList(val);

        if (key == "SUNPOS" || key == "SUNPOSITION" || key == "SUNPATH") {
            if (nums.size() == 2)      { w.sunMode = 2; w.sunPos[0] = nums[0]; w.sunPos[1] = nums[1]; }
            else if (nums.size() == 4) { w.sunMode = 3; for (int i = 0; i < 4; i++) w.sunPath[i] = nums[i]; }
            else keepUnknown(raw);
            continue;
        }
        if (key == "SUNPRESET" || key == "SUNAT" || key == "TIMEOFDAY") {
            std::string name = val;
            if (name == "MIDDAY") name = "NOON"; else if (name == "SUNRISE") name = "DAWN";
            if (std::find(kSunPresets.begin(), kSunPresets.end(), name) != kSunPresets.end()) { w.sunMode = 1; w.sunPreset = name; }
            else keepUnknown(raw);
            continue;
        }
        if (key == "STEPS" || key == "STEP" || key == "GROUND") {
            std::string name = val;
            if (name == "STONE" || name == "ROCK") name = "CONCRETE"; else if (name == "LEAF") name = "LEAVES";
            if (std::find(kStepSurfaces.begin(), kStepSurfaces.end(), name) != kStepSurfaces.end()) w.steps = name;
            else keepUnknown(raw);
            continue;
        }
        const int idx = find(key);
        if (idx < 0) { keepUnknown(raw); continue; }
        const WxParamInfo& pi = kWx[idx];
        WxValue& v = w.p[idx];
        if (pi.flag) {
            if (neg) { v.mode = 2; continue; }
            if (eq == std::string::npos) { v.mode = 1; v.v[0] = 1.0f; continue; }
            if (nums.empty() || nums[0] == 0.0f) { v.mode = 2; continue; }
            v.mode = 1; v.v[0] = nums[0];
        } else if (pi.count == 3) {
            if (nums.size() < 3) { keepUnknown(raw); continue; }
            v.mode = 1; for (int i = 0; i < 3; i++) v.v[i] = nums[i];
        } else {
            if (nums.empty()) { keepUnknown(raw); continue; }
            v.mode = 1; v.v[0] = nums[0];
        }
    }
}

static const std::vector<std::string> kWxPresetNames = {"Clear", "Rain", "Storm", "Snow", "Blizzard", "Sunset", "Noon"};
const std::vector<std::string>& weatherPresetNames() { return kWxPresetNames; }

void weatherPreset(Weather& w, const std::string& name) {
    w = Weather{};
    w.has = true;
    w.autoBase = true;
    auto on = [&w](int p, float v) { w.p[p].mode = 1; w.p[p].v[0] = v; };
    if (name == "Rain")          { on(WX_RAIN, 1.0f); }
    else if (name == "Storm")    { on(WX_RAIN, 1.2f); on(WX_THUNDER, 1.0f); on(WX_WIND, 1.5f); }
    else if (name == "Snow")     { on(WX_SNOW, 1.0f); on(WX_WIND, 1.0f); }
    else if (name == "Blizzard") { on(WX_SNOW, 1.4f); on(WX_WIND, 3.0f); on(WX_GUST, 2.0f); on(WX_FOG, 0.4f); }
    else if (name == "Sunset")   { w.sunMode = 1; w.sunPreset = "SUNSET"; on(WX_SUNSIZE, 1.5f); }
    else if (name == "Noon")     { w.sunMode = 2; w.sunPos[0] = 0.5f; w.sunPos[1] = 0.1f; on(WX_SUNDRIFT, 0.0f); }
}

Obj makeDefault(Kind kind, const std::string& preset, float wx, float wy) {
    const KindInfo& k = info(kind);
    Obj o;
    o.kind = kind;
    o.x = wx; o.y = wy;
    o.w = k.defW; o.h = k.defH;
    if (k.presetSlot >= 0) o.s[k.presetSlot] = preset.empty() && k.presets && !k.presets->empty() ? (*k.presets)[0] : preset;
    switch (kind) {
        case Kind::WALL:      o.s[1] = "SOLID"; break;
        case Kind::GROUND:    o.s[1] = "SOLID"; break;
        case Kind::PLATFORM:  o.s[0] = "ONE_WAY"; break;
        case Kind::BLOCK:     o.s[0] = "BLOCK"; break;
        case Kind::CHECKPOINT:o.s[0] = "NO_FLAG"; break;
        case Kind::NPC:       o.s[0] = "NPC"; o.s[1] = "Hello, traveler.|Mind the cave ahead."; break;
        case Kind::BOSS:      o.f[0] = 0; break;
        case Kind::LIGHT:
            o.f[0] = 420; o.f[1] = 255; o.f[2] = 170; o.f[3] = 90; o.f[4] = 1.0f; o.f[5] = 0.2f; o.f[6] = 1.0f;
            break;
        case Kind::BACKGROUND: o.f[0] = 0.03f; o.f[1] = 0.01f; break;
        case Kind::FOREGROUND: o.f[0] = 1.10f; o.f[1] = 1.10f; break;
        case Kind::ENEMY:
            o.f[0] = wx - 150; o.f[1] = wx + 150; o.f[2] = 110;
            break;
        case Kind::TRAP: {
            // sensible defaults per trap type, taken from the shipped levels
            const std::string& t = o.s[0];
            o.s[1] = "IDLE"; o.s[2] = "AUTO"; o.s[3] = "LINE"; o.s[4] = "SOLID"; o.s[5] = "UP";
            o.f[0] = 0; o.f[1] = 0; o.f[2] = 250; o.f[3] = 0; o.f[4] = 0;
            if (t == "FAN")              { o.s[1] = "ON";  o.s[4] = "TRIGGER"; }
            else if (t == "TRAMPOLINE")  { o.s[4] = "TRIGGER"; }
            else if (t == "FIRE")        { o.s[1] = "OFF"; }
            else if (t == "FALLING_PLATFORM") { o.s[1] = "ON"; o.s[4] = "ONE_WAY"; }
            else if (t == "ROCK_HEAD")   { o.s[2] = "AUTO"; o.s[3] = "RECT"; o.f[0] = wx + 600; o.f[1] = wy; }
            else if (t == "SPIKE_HEAD")  { o.s[1] = "HIT"; o.s[2] = "VERTICAL"; o.f[0] = wy; o.f[1] = wy + 600; }
            else if (t == "SAW")         { o.s[1] = "ON"; o.s[2] = "HORIZONTAL"; o.f[0] = wx; o.f[1] = wx + 600; o.f[3] = 0; o.f[4] = 1; }
            else if (t == "SPIKE_BALL")  { o.s[2] = "CIRCLE"; o.s[3] = "CIRCLE"; o.f[0] = 0; o.f[1] = 0; o.f[2] = 400; o.f[3] = 250; o.f[4] = 1; }
            else if (t == "MOVING_PLATFORM_BROWN" || t == "MOVING_PLATFORM_GREY")
                                         { o.s[1] = "ON"; o.s[2] = "HORIZONTAL"; o.s[4] = "ONE_WAY"; o.f[0] = wx; o.f[1] = wx + 400; }
            break;
        }
        default: break;
    }
    return o;
}

SDL_FRect worldBounds(const Obj& o) {
    const KindInfo& k = info(o.kind);
    switch (o.kind) {
        case Kind::SPAWN:   return {o.x, o.y, 40.0f, 85.0f};          // the player's hitbox
        case Kind::FRUIT:   return {o.x + 32.0f, o.y + 32.0f, 64.0f, 64.0f};
        case Kind::LIGHT:   return {o.x - 18.0f, o.y - 18.0f, 36.0f, 36.0f};
        case Kind::TRAP:    return {o.x, o.y, 64.0f, 64.0f};          // refined by the scene (real sprite size)
        case Kind::ENEMY:   return {o.x, o.y, 40.0f, 85.0f};          // refined by the scene
        case Kind::BOSS:    return {o.x, o.y, 200.0f, 200.0f};        // refined by the scene (hitbox of the real boss type)
        default:            break;
    }
    return {o.x, o.y, o.w * k.unit, o.h * k.unit};
}

void setWorldRect(Obj& o, float wx, float wy, float ww, float wh) {
    const KindInfo& k = info(o.kind);
    o.x = wx; o.y = wy;
    if (k.sized) {
        o.w = ww / k.unit;
        o.h = wh / k.unit;
    }
}

} // namespace editor
