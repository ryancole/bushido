#pragma once

#include <glm/glm.hpp>

#include <vector>

class Renderer;

// A selectable battleground. Same compiled-in-roster contract as characters
// (character.hpp) and weapons (weapon.hpp): constant data addressed by index,
// so a match setup stays a handful of roster indices — netplay peers would
// exchange the level index/id, never scene data. Every level shares the same
// arena bounds (Game::kArenaHalfWidth/Depth); a level differs in what's drawn
// plus a set of solid obstacle boxes (Hanami's bank stones) that Game feeds
// to Physics as static colliders.

struct LevelDef {
    const char* id; // stable slug — the serialization/netplay key, never reordered
    const char* name;
    const char* epithet;  // select-screen flavor line
    glm::vec4 tileColor;  // select-screen tile face
};

// A solid scenery box: axis-aligned, static for the whole match. The same
// list drives the drawing and the physics colliders, so what you see is
// exactly what you collide with. Deterministically generated once — the
// level index alone still fully describes the geometry.
struct LevelObstacle {
    glm::vec3 center;     // world space, y up
    glm::vec3 halfExtent;
};

inline constexpr int kLevelCount = 2;
const LevelDef& levelDef(int index);
const std::vector<LevelObstacle>& levelObstacles(int index);

// Draws the level's scenery (floor, backdrop, ambient animation) for the
// current frame. Called by main's drawScene before fighters/blood so the
// scenery sits under everything gameplay draws.
void drawLevel(Renderer& renderer, int index, float time);
