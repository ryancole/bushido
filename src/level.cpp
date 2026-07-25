#include "level.hpp"

#include "game.hpp" // arena bounds — scenery is sized off the playable area
#include "renderer.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {

void box(Renderer& r, glm::vec3 center, glm::vec3 size, glm::vec4 color) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::scale(model, size);
    r.drawBox(model, color);
}

// Cheap deterministic 0..1 hash for scattering scenery; keeps every frame
// identical without any stored state.
float hash01(int i) {
    std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

// The stream's placement is shared by the water, the stones, and the stone
// colliders, so it lives here rather than inside drawHanami.
constexpr float kStreamX = -3.5f;
constexpr float kStreamW = 1.9f;

float trunkHeight(int seed) { return 2.4f + hash01(seed) * 0.9f; }

// Bank stones, scattered along the stream once and reused every frame — the
// same boxes are drawn AND handed to Physics as static colliders, so a rock
// you see is a rock you (and tumbling debris) bump into. Stones that would
// crowd a fighter's spawn point are skipped.
const std::vector<LevelObstacle>& hanamiStones() {
    static const std::vector<LevelObstacle> stones = [] {
        std::vector<LevelObstacle> v;
        const float floorD = Game::kArenaHalfDepth * 2.0f + 4.0f;
        const glm::vec2 spawns[2] = {{-3.0f, 0.0f}, {3.0f, 0.0f}};
        for (int i = 0; i < 14; ++i) {
            float side = i % 2 == 0 ? -1.0f : 1.0f;
            float sz = 0.22f + hash01(i * 7 + 60) * 0.28f;
            float bz = (hash01(i * 7 + 61) - 0.5f) * (floorD - 1.0f);
            float bx =
                kStreamX + side * (kStreamW * 0.5f + 0.15f + hash01(i * 7 + 62) * 0.4f);
            bool nearSpawn = false;
            for (const glm::vec2& s : spawns) {
                nearSpawn |= glm::distance(glm::vec2{bx, bz}, s) < 1.2f;
            }
            if (nearSpawn) {
                continue;
            }
            v.push_back({{bx, sz * 0.35f, bz}, {sz * 0.7f, sz * 0.35f, sz * 0.5f}});
        }
        return v;
    }();
    return stones;
}

// The grove, dispersed through the level. In-field trees stand inside the
// playable area (clear of the stream band and both spawn points, biased away
// from the near foreground so canopies don't curtain the camera); backdrop
// trees stand past the playable depth for parallax. Only in-field trunks get
// colliders — canopy and branch stay cosmetic, so fighters jump through
// blossoms but never through wood.
struct HanamiTree {
    float x, z;
    int seed;
    bool backdrop;
};
constexpr HanamiTree kHanamiTrees[] = {
    {-9.5f, -3.2f, 3, false},
    {-6.8f, 2.0f, 20, false},
    {-0.8f, -3.5f, 37, false},
    {4.8f, 2.3f, 54, false},
    {9.0f, -1.2f, 71, false},
    {-13.0f, -6.8f, 88, true},
    {-1.8f, -7.0f, 105, true},
    {6.0f, -6.5f, 122, true},
    {12.5f, -7.2f, 139, true},
};

// Everything solid in Hanami: the bank stones plus the in-field tree trunks
// (half extents matching the drawn 0.32-wide trunk exactly).
const std::vector<LevelObstacle>& hanamiObstacles() {
    static const std::vector<LevelObstacle> obstacles = [] {
        std::vector<LevelObstacle> v = hanamiStones();
        for (const HanamiTree& t : kHanamiTrees) {
            if (t.backdrop) {
                continue;
            }
            float h = trunkHeight(t.seed);
            v.push_back({{t.x, h * 0.5f, t.z}, {0.16f, h * 0.5f, 0.16f}});
        }
        return v;
    }();
    return obstacles;
}

// The original arena: dark stone slab with pillars behind for depth reference.
void drawDojo(Renderer& r, float) {
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

// A cherry tree: trunk plus a clump of blossom boxes that sway together.
// Everything hangs off (x, z) at the ground; `seed` varies the build so the
// grove doesn't look copy-pasted. Backdrop trees lean for charm; in-field
// trees grow upright (`leaning` false) so their trunk exactly fills the
// axis-aligned collider box the level registers for them.
void drawCherryTree(Renderer& r, float x, float z, int seed, float time, bool leaning) {
    const float h = trunkHeight(seed);
    const float lean = leaning ? (hash01(seed + 1) - 0.5f) * 0.35f : 0.0f;
    const float sway = std::sin(time * 0.7f + seed * 1.7f) * 0.06f;

    glm::mat4 trunk = glm::translate(glm::mat4(1.0f), {x, 0.0f, z});
    trunk = glm::rotate(trunk, lean, {0.0f, 0.0f, 1.0f});
    r.drawBox(glm::scale(glm::translate(trunk, {0.0f, h * 0.5f, 0.0f}),
                         {0.32f, h, 0.32f}),
              {0.30f, 0.20f, 0.14f, 1.0f});
    // One low branch reaching sideways.
    float branchDir = hash01(seed + 2) > 0.5f ? 1.0f : -1.0f;
    r.drawBox(glm::scale(glm::translate(trunk, {branchDir * 0.55f, h * 0.72f, 0.0f}),
                         {1.0f, 0.16f, 0.16f}),
              {0.30f, 0.20f, 0.14f, 1.0f});

    // Blossom canopy: a fat core with offset clumps in two pinks. The whole
    // crown drifts on the sway so the grove feels alive.
    const glm::vec4 pinkLight{0.95f, 0.72f, 0.79f, 1.0f};
    const glm::vec4 pinkDeep{0.89f, 0.55f, 0.68f, 1.0f};
    glm::vec3 crown{x + lean * h + sway, h, z};
    box(r, crown + glm::vec3{0.0f, 0.55f, 0.0f}, {2.1f, 1.15f, 1.8f}, pinkLight);
    box(r, crown + glm::vec3{-0.85f, 0.25f, 0.3f}, {1.2f, 0.85f, 1.1f}, pinkDeep);
    box(r, crown + glm::vec3{0.8f, 0.35f, -0.35f}, {1.25f, 0.9f, 1.05f}, pinkDeep);
    box(r, crown + glm::vec3{branchDir * 1.05f, -0.35f, 0.0f}, {0.9f, 0.6f, 0.8f},
        pinkLight);
}

// Cherry blossom grove cut by a stream: grass floor, a water band flowing
// toward the camera with drifting ripple glints, stone banks, sakura trees
// behind the arena, and petals falling across the playfield. All ambient
// motion is a pure function of `time` — nothing here touches the sim.
void drawHanami(Renderer& r, float time) {
    const float floorW = Game::kArenaHalfWidth * 2.0f + 8.0f;
    const float floorD = Game::kArenaHalfDepth * 2.0f + 4.0f;

    // Grass slab (same footprint as the dojo floor).
    box(r, {0.0f, -0.5f, 0.0f}, {floorW, 1.0f, floorD}, {0.15f, 0.22f, 0.12f, 1.0f});

    // The stream: a shallow band crossing the whole arena in depth, slightly
    // off-center so duels at mid-stage aren't fought on top of it. The water
    // is cosmetic — fighters wade straight through.
    box(r, {kStreamX, 0.005f, 0.0f}, {kStreamW, 0.05f, floorD},
        {0.15f, 0.30f, 0.42f, 1.0f});

    // Ripple glints scrolling toward the camera (+z), recycled over the
    // floor's depth so the flow never ends.
    for (int i = 0; i < 10; ++i) {
        float lane = hash01(i * 3 + 40);
        float zPos = std::fmod(hash01(i * 3 + 41) * floorD + time * 1.3f, floorD) -
                     floorD * 0.5f;
        float gx = kStreamX + (lane - 0.5f) * (kStreamW - 0.5f);
        box(r, {gx, 0.045f, zPos}, {0.42f, 0.02f, 0.14f},
            {0.55f, 0.72f, 0.82f, 0.85f});
    }

    // Bank stones — drawn from the shared obstacle list so they sit exactly
    // where their colliders are.
    for (const LevelObstacle& stone : hanamiStones()) {
        box(r, stone.center, stone.halfExtent * 2.0f, {0.42f, 0.41f, 0.38f, 1.0f});
    }

    // The grove, dispersed through the playfield with a backdrop rank behind
    // it. In-field trunks are solid (see hanamiObstacles).
    for (const HanamiTree& t : kHanamiTrees) {
        drawCherryTree(r, t.x, t.z, t.seed, time, t.backdrop);
    }

    // Falling petals: each index owns a looping fall (height recycles) with a
    // sideways flutter and a slow tumble. Spread across the whole playfield,
    // including in front of the fighters.
    for (int i = 0; i < 40; ++i) {
        float fallH = 4.0f + hash01(i * 5 + 1) * 1.5f;
        float speed = 0.35f + hash01(i * 5 + 2) * 0.35f;
        float phase = hash01(i * 5 + 3) * 100.0f;
        float y = fallH - std::fmod(time * speed + phase, fallH);
        float px = (hash01(i * 5 + 4) - 0.5f) * floorW +
                   std::sin(time * 1.1f + phase) * 0.6f;
        float pz = (hash01(i * 5 + 5) - 0.5f) * floorD;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {px, y, pz});
        m = glm::rotate(m, time * (0.8f + hash01(i) * 1.5f) + phase,
                        {0.4f, 1.0f, 0.3f});
        m = glm::scale(m, {0.10f, 0.02f, 0.07f});
        r.drawBox(m, {0.96f, 0.76f, 0.82f, 0.9f});
    }
}

const LevelDef kLevels[kLevelCount] = {
    {
        "dojo",
        "Dojo",
        "Stone, shadow, and discipline",
        {0.22f, 0.21f, 0.26f, 0.75f},
    },
    {
        "hanami",
        "Hanami",
        "Petals drift over quiet water",
        {0.83f, 0.54f, 0.62f, 0.75f},
    },
};

} // namespace

const LevelDef& levelDef(int index) {
    return kLevels[std::clamp(index, 0, kLevelCount - 1)];
}

const std::vector<LevelObstacle>& levelObstacles(int index) {
    static const std::vector<LevelObstacle> kNone;
    switch (std::clamp(index, 0, kLevelCount - 1)) {
    case 1: return hanamiObstacles();
    default: return kNone;
    }
}

void drawLevel(Renderer& renderer, int index, float time) {
    switch (std::clamp(index, 0, kLevelCount - 1)) {
    case 0: drawDojo(renderer, time); break;
    case 1: drawHanami(renderer, time); break;
    }
}
