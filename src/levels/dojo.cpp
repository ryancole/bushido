#include "levels/registry.hpp"

#include "game.hpp" // arena bounds — scenery is sized off the playable area
#include "levels/scenery.hpp"

// Dojo: the original arena, and the tuning baseline for everything else. Flat
// ground, nothing solid, no water — so it fills in only `kDef` and `draw` and
// lets the dispatcher's defaults cover the rest.

namespace levels::dojo {

const LevelDef kDef = {
    "dojo",
    "Dojo",
    "Stone, shadow, and discipline",
    {0.22f, 0.21f, 0.26f, 0.75f},
    Game::kArenaHalfWidth,
};

// Dark stone slab with pillars behind for depth reference.
void draw(Renderer& r, float) {
    box(r, {0.0f, -0.5f, 0.0f},
        {Game::kArenaHalfWidth * 2.0f + 8.0f, 1.0f, Game::kArenaHalfDepth * 2.0f + 4.0f},
        {0.16f, 0.15f, 0.13f, 1.0f});

    const float pillarX[] = {-14.0f, -7.0f, 0.0f, 7.0f, 14.0f};
    const float pillarH[] = {4.5f, 3.2f, 5.5f, 3.8f, 4.8f};
    for (int i = 0; i < 5; ++i) {
        box(r, {pillarX[i], pillarH[i] * 0.5f, -Game::kArenaHalfDepth - 1.5f},
            {1.2f, pillarH[i], 1.2f}, {0.10f, 0.10f, 0.14f, 1.0f});
    }
}

} // namespace levels::dojo
