#pragma once

#include "levels/level.hpp"

// Internal wiring between the roster dispatch (level.cpp) and the per-level
// implementation files. Every battleground owns exactly one .cpp holding its
// LevelDef, its solid geometry, and its drawing — data and drawing together,
// since a level's scenery is bespoke procedural code that no table could
// describe on its own.
//
// Adding a battleground: write levels/<name>.cpp, declare its namespace below,
// add a row to level.cpp's table, and bump kLevelCount in level.hpp.

class Renderer;

namespace levels {

// One level's implementation as the dispatcher sees it. `def` and `draw` are
// required; the rest are optional — a null hook means "none", which is the
// common case for a plain stage (Dojo is flat, dry, and has nothing solid in
// it, so it only fills in the first two).
struct LevelEntry {
    const LevelDef* def;
    void (*draw)(Renderer& r, float time);
    const std::vector<LevelObstacle>& (*obstacles)() = nullptr;
    const std::vector<LevelObstacle>& (*ground)() = nullptr;
    const LevelWater* (*water)() = nullptr;
};

namespace dojo {
extern const LevelDef kDef;
void draw(Renderer& r, float time);
} // namespace dojo

namespace hanami {
extern const LevelDef kDef;
void draw(Renderer& r, float time);
const std::vector<LevelObstacle>& obstacles();
const std::vector<LevelObstacle>& ground();
const LevelWater* water();
} // namespace hanami

} // namespace levels
