#include "levels/registry.hpp"

#include "game.hpp" // arena bounds — scenery is sized off the playable area
#include "levels/scenery.hpp"
#include "levels/sun.hpp"

#include <cmath>

// Hanami: a cherry blossom grove cut by a carved stream, with the shinobi no
// ie holding the widened left flank. Every solid thing here is authored once
// and shared between the drawing and the colliders, so a rock you see is a
// rock you bump into.

namespace levels::hanami {
namespace {

// The stream's placement is shared by the water, the stones, the carved
// ground channel, and the water volume, so it lives here rather than inside
// draw(). It sits at x = -6: left of center for composition, but clear of
// player 1's spawn at x = -3 — the channel is genuinely recessed, and nobody
// should start the match standing in it.
constexpr float kStreamX = -6.0f;
constexpr float kStreamW = 1.9f;
// The channel: bed 0.35 m down — under the characters' 0.4 m step-up, so
// fighters wade through and climb out — with the surface just below bank
// level so the water reads as sunken into the ground.
constexpr float kStreamBedY = -0.35f;
constexpr float kStreamSurfaceY = -0.02f;

// Hanami plays wider than the baseline arena — the extra room on the left
// belongs to the shinobi house. The def below and the floor/wall math both
// read this so the drawn stage and the physics walls agree.
constexpr float kHalfWidth = 16.0f;

float trunkHeight(int seed) { return 2.4f + hash01(seed) * 0.9f; }

// Bank stones, scattered along the stream once and reused every frame — the
// same boxes are drawn AND handed to Physics as static colliders, so a rock
// you see is a rock you (and tumbling debris) bump into. Stones that would
// crowd a fighter's spawn point are skipped.
const std::vector<LevelObstacle>& stones() {
    static const std::vector<LevelObstacle> v = [] {
        std::vector<LevelObstacle> out;
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
            out.push_back({{bx, sz * 0.35f, bz}, {sz * 0.7f, sz * 0.35f, sz * 0.5f}});
        }
        return out;
    }();
    return v;
}

// The grove, dispersed through the level. In-field trees stand inside the
// playable area (clear of the stream band and both spawn points, biased away
// from the near foreground so canopies don't curtain the camera); backdrop
// trees stand past the playable depth for parallax. Only in-field trunks get
// colliders — canopy and branch stay cosmetic, so fighters jump through
// blossoms but never through wood.
struct Tree {
    float x, z;
    int seed;
    bool backdrop;
};
constexpr Tree kTrees[] = {
    {-9.5f, -3.2f, 3, false},
    {-8.6f, 1.8f, 20, false},
    {-0.8f, -3.5f, 37, false},
    {4.8f, 2.3f, 54, false},
    {9.0f, -1.2f, 71, false},
    {-13.0f, -6.8f, 88, true},
    {-1.8f, -7.0f, 105, true},
    {6.0f, -6.5f, 122, true},
    {14.5f, -7.2f, 139, true},
};

// Shinobi no ie — the ninja house holding Hanami's widened left flank. One
// authored box list drives both the drawing and (for `solid` entries) the
// colliders, same guarantee as the stones and trunks. The raised floor is
// 0.36 m high — under the characters' 0.4 m step-up, so fighters climb onto
// the engawa and duel inside; the roof is solid too (debris lands on it).
// Shoji panels are flush decoration on the solid back wall, so they need no
// colliders of their own.
struct HouseBox {
    glm::vec3 center;
    glm::vec3 halfExtent;
    glm::vec4 color;
    bool solid;
};
constexpr float kHouseX = -13.6f; // footprint spans x -16 (arena wall) .. -11.2
constexpr float kHouseZ = -0.8f;
constexpr glm::vec4 kWoodDark{0.26f, 0.19f, 0.13f, 1.0f};
constexpr glm::vec4 kWoodFloor{0.52f, 0.38f, 0.25f, 1.0f};
constexpr glm::vec4 kRoofDark{0.17f, 0.13f, 0.11f, 1.0f};
constexpr glm::vec4 kShoji{0.88f, 0.84f, 0.72f, 1.0f};
constexpr HouseBox kHouse[] = {
    // Raised floor platform (engawa) and its entry step stone.
    {{kHouseX, 0.18f, kHouseZ}, {2.4f, 0.18f, 2.2f}, kWoodFloor, true},
    {{-12.4f, 0.09f, 1.75f}, {0.4f, 0.09f, 0.3f}, {0.42f, 0.41f, 0.38f, 1.0f}, true},
    // Back wall (away from camera) and left wall (flush with the arena wall);
    // the right side and the front stay open so fights spill through.
    {{kHouseX, 1.36f, kHouseZ - 2.05f}, {2.4f, 1.0f, 0.15f}, kWoodDark, true},
    {{-15.8f, 1.36f, kHouseZ}, {0.2f, 1.0f, 2.2f}, kWoodDark, true},
    // Shoji screens on the back wall's inner face (decorative, flush).
    {{kHouseX - 1.0f, 1.36f, kHouseZ - 1.86f}, {0.62f, 0.72f, 0.05f}, kShoji, false},
    {{kHouseX + 1.0f, 1.36f, kHouseZ - 1.86f}, {0.62f, 0.72f, 0.05f}, kShoji, false},
    // Posts framing the open front: corner and mid (a door-frame pair).
    {{-11.45f, 1.36f, kHouseZ + 2.05f}, {0.1f, 1.0f, 0.1f}, kWoodDark, true},
    {{kHouseX, 1.36f, kHouseZ + 2.05f}, {0.1f, 1.0f, 0.1f}, kWoodDark, true},
    // Two-tier roof with a wide overhang.
    {{kHouseX, 2.5f, kHouseZ}, {2.9f, 0.13f, 2.7f}, kRoofDark, true},
    {{kHouseX, 2.76f, kHouseZ}, {1.9f, 0.13f, 1.8f}, {0.23f, 0.18f, 0.15f, 1.0f}, true},
};

// The stream's water volume: spans the channel for the floor's whole depth,
// surface at kStreamSurfaceY, flowing toward the camera (+z) — the same
// direction the ripple glints scroll.
constexpr LevelWater kWater = {
    {kStreamX - kStreamW * 0.5f, kStreamBedY, -(Game::kArenaHalfDepth + 2.0f)},
    {kStreamX + kStreamW * 0.5f, kStreamSurfaceY, Game::kArenaHalfDepth + 2.0f},
    {0.0f, 0.0f, 0.55f},
};

// A blossom clump, as an offset from the crown's origin. The canopy became a
// table the moment the sun started casting it: the shadow on the grass has to
// be the shadow of the crown that is actually drawn, and two copies of this
// list would drift apart the way any two copies do — a shadow under a clump
// that had since moved is the whole failure, and it is silent.
struct Blob {
    glm::vec3 offset;
    glm::vec3 size;
    bool deep;     // the deeper of the two pinks
    bool onBranch; // hangs off the low branch, so its side follows branchDir
};
constexpr Blob kCrown[] = {
    {{0.0f, 0.55f, 0.0f}, {2.1f, 1.15f, 1.8f}, false, false},
    {{-0.85f, 0.25f, 0.3f}, {1.2f, 0.85f, 1.1f}, true, false},
    {{0.8f, 0.35f, -0.35f}, {1.25f, 0.9f, 1.05f}, true, false},
    {{1.05f, -0.35f, 0.0f}, {0.9f, 0.6f, 0.8f}, false, true},
};

// One tree resolved for this frame: everything both the drawing and the
// shadowing need to agree on, worked out once from (x, z, seed, time).
struct Canopy {
    float height;    // trunk height
    float lean;      // radians about +z; in-field trees stand upright
    float branchDir; // which side the low branch reaches
    glm::vec3 crown; // origin the blossom clumps hang off, swaying included
};

Canopy canopyOf(float x, float z, int seed, float time, bool leaning) {
    const float h = trunkHeight(seed);
    const float lean = leaning ? (hash01(seed + 1) - 0.5f) * 0.35f : 0.0f;
    const float sway = std::sin(time * 0.7f + seed * 1.7f) * 0.06f;
    return {h, lean, hash01(seed + 2) > 0.5f ? 1.0f : -1.0f,
            {x + lean * h + sway, h, z}};
}

// A cherry tree: trunk plus a clump of blossom boxes that sway together.
// Everything hangs off (x, z) at the ground; `seed` varies the build so the
// grove doesn't look copy-pasted. Backdrop trees lean for charm; in-field
// trees grow upright (`leaning` false) so their trunk exactly fills the
// axis-aligned collider box the level registers for them.
void drawCherryTree(Renderer& r, float x, float z, int seed, float time, bool leaning) {
    const Canopy c = canopyOf(x, z, seed, time, leaning);

    glm::mat4 trunk = glm::translate(glm::mat4(1.0f), {x, 0.0f, z});
    trunk = glm::rotate(trunk, c.lean, {0.0f, 0.0f, 1.0f});
    r.drawBox(glm::scale(glm::translate(trunk, {0.0f, c.height * 0.5f, 0.0f}),
                         {0.32f, c.height, 0.32f}),
              {0.30f, 0.20f, 0.14f, 1.0f});
    // One low branch reaching sideways.
    r.drawBox(glm::scale(glm::translate(trunk, {c.branchDir * 0.55f, c.height * 0.72f, 0.0f}),
                         {1.0f, 0.16f, 0.16f}),
              {0.30f, 0.20f, 0.14f, 1.0f});

    // Blossom canopy: a fat core with offset clumps in two pinks. The whole
    // crown drifts on the sway so the grove feels alive.
    const glm::vec4 pinkLight{0.95f, 0.72f, 0.79f, 1.0f};
    const glm::vec4 pinkDeep{0.89f, 0.55f, 0.68f, 1.0f};
    for (const Blob& b : kCrown) {
        const float sx = b.onBranch ? b.offset.x * c.branchDir : b.offset.x;
        box(r, c.crown + glm::vec3{sx, b.offset.y, b.offset.z}, b.size,
            b.deep ? pinkDeep : pinkLight);
    }
}

// The same tree, printed on the grass. Trunk and every blossom clump cast
// separately rather than the crown casting as one slab: a canopy's shadow is
// dappled, and four overlapping footprints is the cheapest dappling there is.
void shadowCherryTree(Renderer& r, float x, float z, int seed, float time,
                      bool leaning, const SunDef& sun) {
    const Canopy c = canopyOf(x, z, seed, time, leaning);
    sunShadow(r, {x, c.height * 0.5f, z}, {0.16f, c.height * 0.5f, 0.16f}, sun);
    for (const Blob& b : kCrown) {
        const float sx = b.onBranch ? b.offset.x * c.branchDir : b.offset.x;
        sunShadow(r, c.crown + glm::vec3{sx, b.offset.y, b.offset.z}, b.size * 0.5f, sun);
    }
}

} // namespace

const LevelDef kDef = {
    "hanami",
    "Hanami",
    "Petals drift over quiet water",
    {0.83f, 0.54f, 0.62f, 0.75f},
    kHalfWidth,
    // Afternoon: deep blue overhead falling to a warm haze the blossoms sit
    // against, over distant wooded ground that carries the grass outward.
    // Fair-weather cumulus crossing on the same breeze that takes the petals:
    // white crowns over a base warmed by the haze they sit in.
    {{0.24f, 0.44f, 0.72f},
     {0.80f, 0.74f, 0.70f},
     {0.16f, 0.22f, 0.15f},
     {20, {0.99f, 0.98f, 0.96f}, {0.68f, 0.69f, 0.76f}, 1.15f, 0.018f},
     // A warm afternoon sun with enough haze under the blossoms to show its
     // shafts. The shadows are kept well short of black: this is open ground
     // under a bright sky, where half the light on a shaded patch arrives from
     // the sky rather than the sun, and a shadow drawn as an absence of sun
     // alone reads as a hole cut in the grass.
     {{1.0f, 0.95f, 0.80f}, 1.0f, 0.30f}},
};

// Where the light gets through the canopy: the *top* of each shaft, in the
// blossoms, with the beam falling from there down the sun's own line — see
// levels/sun.hpp for why a level authors the gap rather than the beam. The
// heights sit at crown level so each beam's upper end is buried in the
// blossoms it is supposed to be squeezing past, and the gaps are placed
// between the in-field trees so the grove is what breaks the light up.
struct Gap {
    float x, y, z;
    float width;
    float strength;
};
constexpr Gap kGaps[] = {
    {-9.2f, 4.4f, -1.2f, 0.55f, 1.0f},  // between the two trees on the left
    {-6.4f, 4.0f, 2.6f, 0.42f, 0.75f},  // catches the stream on its way down
    {-1.0f, 4.6f, -1.8f, 0.60f, 1.0f},  // over center stage
    {2.6f, 4.1f, 1.2f, 0.45f, 0.80f},
    {6.6f, 4.5f, 3.8f, 0.50f, 0.90f},
    {9.4f, 4.3f, 0.2f, 0.55f, 0.95f},   // the right-hand tree
};
// Beams are kept a couple of metres clear of the two spawn points (x = ±3,
// z = 0) — not for the duel's sake, where walking through a shaft is the whole
// idea, but for the versus intro, whose close-up sits a short way in front of
// a fighter's face and would otherwise open the match inside a column of haze.

// Everything solid here: the bank stones, the in-field tree trunks (half
// extents matching the drawn 0.32-wide trunk exactly), and the house's
// structural boxes.
const std::vector<LevelObstacle>& obstacles() {
    static const std::vector<LevelObstacle> v = [] {
        std::vector<LevelObstacle> out = stones();
        for (const Tree& t : kTrees) {
            if (t.backdrop) {
                continue;
            }
            float h = trunkHeight(t.seed);
            out.push_back({{t.x, h * 0.5f, t.z}, {0.16f, h * 0.5f, 0.16f}});
        }
        for (const HouseBox& b : kHouse) {
            if (b.solid) {
                out.push_back({b.center, b.halfExtent});
            }
        }
        return out;
    }();
    return v;
}

// Ground colliders: the flat slab is replaced by two banks with the stream
// channel carved between them, bed at kStreamBedY. Extents match the default
// slab's (1 m overhang past walls) so behavior at the walls is unchanged.
const std::vector<LevelObstacle>& ground() {
    static const std::vector<LevelObstacle> v = [] {
        const float hw = kHalfWidth + 1.0f;
        const float hd = Game::kArenaHalfDepth + 1.0f;
        const float xL = kStreamX - kStreamW * 0.5f;
        const float xR = kStreamX + kStreamW * 0.5f;
        std::vector<LevelObstacle> out;
        out.push_back({{(-hw + xL) * 0.5f, -0.5f, 0.0f}, {(xL + hw) * 0.5f, 0.5f, hd}});
        out.push_back({{(xR + hw) * 0.5f, -0.5f, 0.0f}, {(hw - xR) * 0.5f, 0.5f, hd}});
        out.push_back(
            {{kStreamX, kStreamBedY - 0.5f, 0.0f}, {kStreamW * 0.5f, 0.5f, hd}});
        return out;
    }();
    return v;
}

const std::vector<LevelWater>& water() {
    static const std::vector<LevelWater> v{kWater};
    return v;
}

// Grass floor, a water band flowing toward the camera with drifting ripple
// glints, stone banks, sakura through the playfield, the house on the left
// flank, and petals falling across everything. All ambient motion is a pure
// function of `time` — nothing here touches the sim.
void draw(Renderer& r, float time) {
    const float floorW = kHalfWidth * 2.0f + 8.0f;
    const float floorD = Game::kArenaHalfDepth * 2.0f + 4.0f;

    // Grass banks flanking the carved stream channel (the physics ground is
    // carved the same way — see ground()). The channel floor is mud, and the
    // water surface is a translucent sheet just below bank level, so the
    // stream is genuinely sunken: fighters wade shin-deep and severed pieces
    // bob half-submerged as the current carries them off.
    const float xL = kStreamX - kStreamW * 0.5f;
    const float xR = kStreamX + kStreamW * 0.5f;
    box(r, {(-floorW * 0.5f + xL) * 0.5f, -0.5f, 0.0f},
        {xL + floorW * 0.5f, 1.0f, floorD}, {0.15f, 0.22f, 0.12f, 1.0f});
    box(r, {(xR + floorW * 0.5f) * 0.5f, -0.5f, 0.0f},
        {floorW * 0.5f - xR, 1.0f, floorD}, {0.15f, 0.22f, 0.12f, 1.0f});
    box(r, {kStreamX, kStreamBedY - 0.325f, 0.0f}, {kStreamW, 0.65f, floorD},
        {0.11f, 0.10f, 0.08f, 1.0f});
    box(r, {kStreamX, kStreamSurfaceY - 0.03f, 0.0f}, {kStreamW, 0.06f, floorD},
        {0.15f, 0.30f, 0.42f, 0.88f});

    // Ripple glints scrolling toward the camera (+z) — the same direction and
    // rough speed as the water volume's current — recycled over the floor's
    // depth so the flow never ends.
    for (int i = 0; i < 10; ++i) {
        float lane = hash01(i * 3 + 40);
        float zPos = std::fmod(hash01(i * 3 + 41) * floorD + time * 1.3f, floorD) -
                     floorD * 0.5f;
        float gx = kStreamX + (lane - 0.5f) * (kStreamW - 0.5f);
        box(r, {gx, kStreamSurfaceY + 0.005f, zPos}, {0.42f, 0.02f, 0.14f},
            {0.55f, 0.72f, 0.82f, 0.85f});
    }

    // Everything the sun has to go around, printed on the grass before the
    // things themselves are drawn over it. Cast from the same authored boxes
    // that are about to be drawn, which is the same guarantee the obstacle
    // list gives the colliders: a shadow on the ground belongs to a thing that
    // is actually standing there.
    const SunDef& sun = kDef.sky.sun;
    for (const LevelObstacle& stone : stones()) {
        sunShadow(r, stone.center, stone.halfExtent, sun);
    }
    for (const Tree& t : kTrees) {
        if (t.backdrop) {
            continue; // stands past the playable depth; its shadow falls off the floor
        }
        shadowCherryTree(r, t.x, t.z, t.seed, time, false, sun);
    }
    // The house casts from its roof alone. The roof overhangs every wall, post
    // and platform beneath it, so their shadows are already inside its own —
    // casting them too would only stack four alphas into a black rectangle.
    for (const HouseBox& b : kHouse) {
        if (b.center.y > 2.0f) {
            sunShadow(r, b.center, b.halfExtent, sun);
        }
    }

    // Bank stones — drawn from the shared obstacle list so they sit exactly
    // where their colliders are.
    for (const LevelObstacle& stone : stones()) {
        box(r, stone.center, stone.halfExtent * 2.0f, {0.42f, 0.41f, 0.38f, 1.0f});
    }

    // The grove, dispersed through the playfield with a backdrop rank behind
    // it. In-field trunks are solid (see obstacles()).
    for (const Tree& t : kTrees) {
        drawCherryTree(r, t.x, t.z, t.seed, time, t.backdrop);
    }

    // Shinobi no ie on the left flank, drawn from the same authored boxes
    // whose solid entries are the colliders.
    for (const HouseBox& b : kHouse) {
        box(r, b.center, b.halfExtent * 2.0f, b.color);
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

// The light itself, hanging in the air between the blossoms. Drawn after the
// fighters rather than with the rest of the scenery — see drawLightShafts in
// level.hpp; a translucent beam that wrote depth over a fighter would erase
// them, and drawn last the depth test alone puts every beam on the right side
// of everything it crosses.
void shafts(Renderer& r, float time) {
    int seed = 0;
    for (const Gap& g : kGaps) {
        sunShaft(r, {g.x, g.y, g.z}, g.width, g.strength, kDef.sky.sun, time, seed++);
    }
}

} // namespace levels::hanami
