#include "levels/level.hpp"

#include "levels/registry.hpp"

#include <algorithm>

// Nothing but the index → implementation dispatch. Each battleground's data
// and drawing live together in its own file (dojo.cpp, hanami.cpp); this table
// is the only place that knows the roster order.

namespace {

using levels::LevelEntry;

const LevelEntry kEntries[kLevelCount] = {
    {&levels::dojo::kDef, levels::dojo::draw},
    {&levels::hanami::kDef, levels::hanami::draw, levels::hanami::obstacles,
     levels::hanami::ground, levels::hanami::water, levels::hanami::shafts},
    {&levels::sorihashi::kDef, levels::sorihashi::draw, levels::sorihashi::obstacles,
     levels::sorihashi::ground, levels::sorihashi::water},
    {&levels::tanbo::kDef, levels::tanbo::draw, levels::tanbo::obstacles,
     levels::tanbo::ground, levels::tanbo::water, levels::tanbo::shafts},
};

const LevelEntry& entry(int index) {
    return kEntries[std::clamp(index, 0, kLevelCount - 1)];
}

} // namespace

const LevelDef& levelDef(int index) {
    return *entry(index).def;
}

const std::vector<LevelObstacle>& levelObstacles(int index) {
    static const std::vector<LevelObstacle> kNone;
    const LevelEntry& e = entry(index);
    return e.obstacles ? e.obstacles() : kNone;
}

const std::vector<LevelObstacle>& levelGround(int index) {
    static const std::vector<LevelObstacle> kFlat;
    const LevelEntry& e = entry(index);
    return e.ground ? e.ground() : kFlat;
}

const std::vector<LevelWater>& levelWater(int index) {
    static const std::vector<LevelWater> kDry;
    const LevelEntry& e = entry(index);
    return e.water ? e.water() : kDry;
}

void drawLevel(Renderer& renderer, int index, float time) {
    entry(index).draw(renderer, time);
}

void drawLightShafts(Renderer& renderer, int index, float time) {
    const LevelEntry& e = entry(index);
    if (e.shafts) {
        e.shafts(renderer, time);
    }
}
