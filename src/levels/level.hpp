#pragma once

#include <glm/glm.hpp>

#include <vector>

class Renderer;

// A selectable battleground. Same compiled-in-roster contract as characters
// (characters/character.hpp) and weapons (weapons/weapon.hpp): constant data
// addressed by index, so a match stays a handful of roster indices — peers
// exchange the level index/id, never scene data. Levels share the arena depth
// (Game::kArenaHalfDepth) but own their width; beyond what's drawn, a level
// contributes solid obstacle boxes (Hanami's bank stones, tree trunks, and
// house) that Game feeds to Physics as static colliders.
//
// This header is the whole public surface. Each battleground lives in its own
// file beside it (dojo.cpp, hanami.cpp) and is wired up through registry.hpp;
// level.cpp is nothing but the index → implementation dispatch.

// The weather in that sky: a deck of clouds drifting around the shared shell.
// Two colors rather than one because a cloud is read almost entirely off the
// contrast between its lit crowns and the flat underside beneath them — one
// color gives a paper cutout. A clear sky writes `count` 0 and costs nothing.
struct CloudDef {
    int count;         // clouds spread around the whole ring; 0 = clear sky
    glm::vec3 sunlit;  // the crowns, where the light lands
    glm::vec3 shaded;  // the flat base, which is most of what a low camera sees
    float scale;       // size multiplier on the shared cluster (1 = as authored)
    float drift;       // rad/s of wind — see sky.cpp for why it is an angle
};

// The sky a battleground stands under: three authored colors the shared shell
// (levels/sky.cpp) ramps between, plus its weather. Every level fills this in —
// it is a member of LevelDef rather than an optional hook, so a new
// battleground cannot open onto the empty clear color by forgetting to write
// one.
struct SkyDef {
    glm::vec3 zenith;  // straight overhead
    glm::vec3 horizon; // where the sky meets the ground plane
    glm::vec3 ground;  // the land beyond the arena floor, below the horizon
    CloudDef clouds;
};

struct LevelDef {
    const char* id; // stable slug — the serialization/netplay key, never reordered
    const char* name;
    const char* epithet;  // select-screen flavor line
    glm::vec4 tileColor;  // select-screen tile face
    // Playable half width: where Physics puts the side walls and the floor is
    // sized from. Game::kArenaHalfWidth is the baseline (Dojo); a level may
    // stretch it (Hanami widens for the house on its left flank).
    float arenaHalfWidth;
    SkyDef sky;
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

// Draws the level's sky in the shared shell, in this level's colors, with its
// clouds where `time` has carried them. Called by main's drawScene first of
// all, before even the scenery. The shell is built around `eye` rather than
// around the arena, which is what makes it read as sky: it never gets closer,
// and no camera pull-back can leave it.
void drawSky(Renderer& renderer, int index, const glm::vec3& eye, float time);

// Draws the level's scenery (floor, backdrop, ambient animation) for the
// current frame. Called by main's drawScene before fighters/blood so the
// scenery sits under everything gameplay draws.
void drawLevel(Renderer& renderer, int index, float time);
