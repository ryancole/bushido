#pragma once

#include <glm/glm.hpp>

#include <vector>

class Renderer;

// A selectable battleground. Same compiled-in-roster contract as characters
// (character.hpp) and weapons (weapon.hpp): constant data addressed by index,
// so a match setup stays a handful of roster indices — netplay peers would
// exchange the level index/id, never scene data. Levels share the arena depth
// (Game::kArenaHalfDepth) but own their width; beyond what's drawn, a level
// contributes solid obstacle boxes (Hanami's bank stones, tree trunks, and
// house) that Game feeds to Physics as static colliders.

struct LevelDef {
    const char* id; // stable slug — the serialization/netplay key, never reordered
    const char* name;
    const char* epithet;  // select-screen flavor line
    glm::vec4 tileColor;  // select-screen tile face
    // Playable half width: where Physics puts the side walls and the floor is
    // sized from. Game::kArenaHalfWidth is the baseline (Dojo); a level may
    // stretch it (Hanami widens for the house on its left flank).
    float arenaHalfWidth;
};

// A solid scenery box: axis-aligned, static for the whole match. The same
// list drives the drawing and the physics colliders, so what you see is
// exactly what you collide with. Deterministically generated once — the
// level index alone still fully describes the geometry.
struct LevelObstacle {
    glm::vec3 center;     // world space, y up
    glm::vec3 halfExtent;
};

// A level's water volume (at most one, axis-aligned). Physics floats debris
// that drifts inside — buoyancy against the surface plus the current's push,
// so severed pieces bob off downstream — and Game suppresses blood floor
// marks inside it (moving water washes blood away).
struct LevelWater {
    glm::vec3 min, max; // AABB; max.y is the surface height
    glm::vec3 current;  // m/s drift applied to floating debris
};

inline constexpr int kLevelCount = 2;
const LevelDef& levelDef(int index);
const std::vector<LevelObstacle>& levelObstacles(int index);
// Ground collider boxes replacing the default flat slab (empty = flat ground
// at y = 0 everywhere). Hanami carves its stream channel out of these.
const std::vector<LevelObstacle>& levelGround(int index);
const LevelWater* levelWater(int index); // nullptr = dry level

// Draws the level's scenery (floor, backdrop, ambient animation) for the
// current frame. Called by main's drawScene before fighters/blood so the
// scenery sits under everything gameplay draws.
void drawLevel(Renderer& renderer, int index, float time);
