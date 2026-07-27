#include "levels/registry.hpp"

#include "game.hpp" // arena bounds — scenery is sized off the playable area
#include "levels/scenery.hpp"
#include "levels/sun.hpp"

#include <cmath>

// Tanbo: flooded rice paddies on a summer morning — the roaming level. Wide
// *and* deep (the first level to stretch arenaHalfDepth), an open grid of
// shallow pools cut by earthen bunds, so the duel is free to range anywhere
// and the ground underfoot keeps changing meaning: dry footing on the paths,
// ankle-deep splashing between the rice rows.
//
// The paddies are the reason the water-volume list and the per-level depth
// both exist in the shape they do, and the level leans on two standing rules:
//
// Every *dry* surface is at y = 0 — bund tops, margins, all of it. Blood
// droplets are ballistics against the bare ground plane and marks stamp at
// y ~ 0.01, so dry ground anywhere else would collect floating blood. The
// paddies sit 0.12 m down (a step so small nobody notices taking it), water
// over the lower 8 cm, and each basin is its own water volume: marks are
// suppressed exactly where there is water to wash them, and the bunds
// between the pools still take a stain.
//
// The rice itself is cosmetic, like Hanami's petals — a fighter wades
// through the rows, never around them. Only the scarecrow's pole and the
// drying-rack posts are solid, and they stand off the natural fight lanes.

namespace levels::tanbo {
namespace {

constexpr float kHalfWidth = 16.0f;
constexpr float kHalfDepth = 8.0f; // the deep half of "wide and deep"

// The flooded field: everything inside this rectangle is paddy (bed sunk to
// kBedY, water to kSurfaceY) except the bunds; everything outside it up to
// the walls is the dry grass margin at y = 0.
constexpr float kFieldX = 13.0f;
constexpr float kFieldZ = 6.7f;
constexpr float kBedY = -0.12f;
constexpr float kSurfaceY = -0.04f;

// The bund grid: a main causeway along z = 0 (the spawns at x = +-3 start
// the duel on dry footing) and three cross paths, splitting the field into
// eight pools. Bund tops at y = 0 like every dry surface; 0.12 m is under
// any notice — the level has no ledges, only wet and dry.
constexpr float kBundHalfW = 0.5f;  // the causeway
constexpr float kCrossHalfW = 0.35f;
constexpr float kCrossX[3] = {-8.7f, 0.0f, 8.7f};

constexpr glm::vec4 kGrass{0.22f, 0.32f, 0.15f, 1.0f};
constexpr glm::vec4 kEarth{0.32f, 0.26f, 0.16f, 1.0f};
constexpr glm::vec4 kMud{0.13f, 0.11f, 0.08f, 1.0f};
constexpr glm::vec4 kWaterSheet{0.45f, 0.55f, 0.62f, 0.72f};
constexpr glm::vec4 kStraw{0.72f, 0.62f, 0.38f, 1.0f};
constexpr glm::vec4 kWood{0.30f, 0.22f, 0.14f, 1.0f};

// The eight pool basins, worked out once from the grid: two rows (near and
// far of the causeway) by four columns (between the cross paths and the
// field edge). Shared by the water volumes, the water sheets, and the rice
// planting, so all three agree on where a pool is.
struct Pool {
    float x0, x1, z0, z1;
};

const std::vector<Pool>& pools() {
    static const std::vector<Pool> v = [] {
        std::vector<Pool> out;
        const float xEdges[] = {-kFieldX,
                                kCrossX[0] - kCrossHalfW, kCrossX[0] + kCrossHalfW,
                                kCrossX[1] - kCrossHalfW, kCrossX[1] + kCrossHalfW,
                                kCrossX[2] - kCrossHalfW, kCrossX[2] + kCrossHalfW,
                                kFieldX};
        for (int row = 0; row < 2; ++row) {
            const float z0 = row == 0 ? kBundHalfW : -kFieldZ;
            const float z1 = row == 0 ? kFieldZ : -kBundHalfW;
            for (int col = 0; col < 4; ++col) {
                out.push_back({xEdges[col * 2], xEdges[col * 2 + 1], z0, z1});
            }
        }
        return out;
    }();
    return v;
}

// The scarecrow (kakashi), leaning over the north-west pool, and the two
// rice-drying racks (hasa) on the back margin. Solid parts only — the pole
// and the posts; everything else about them is straw.
constexpr float kKakashiX = -6.2f;
constexpr float kKakashiZ = 3.6f;
constexpr float kHasa[2][2] = {{-8.0f, -7.4f}, {5.0f, -7.4f}}; // x, z per rack

// One white egret stands in the south-east pool; a second waits far off on
// the margin. Cosmetic, off the fight lanes, and the near one dips for a
// frog now and then — the only wildlife the sim will never know about.
constexpr float kEgretX = 4.6f;
constexpr float kEgretZ = 4.3f;

void drawEgret(Renderer& r, float x, float z, float base, float time, int seed) {
    // `base` is what the bird stands on — the paddy bed for the one fishing,
    // the margin for the one loafing. The dip: a slow cycle, most of it
    // spent upright.
    const float t = std::sin(time * 0.35f + static_cast<float>(seed) * 2.6f);
    const float dip = std::max(0.0f, t * t * t) * 0.22f;
    const glm::vec4 white{0.96f, 0.96f, 0.93f, 1.0f};
    const glm::vec4 leg{0.25f, 0.22f, 0.18f, 1.0f};
    box(r, {x, base + 0.42f - dip * 0.3f, z}, {0.30f, 0.18f, 0.16f}, white);
    box(r, {x + 0.16f, base + 0.58f - dip, z}, {0.06f, 0.26f, 0.06f}, white);
    box(r, {x + 0.20f, base + 0.70f - dip * 1.6f, z}, {0.11f, 0.08f, 0.08f}, white);
    box(r, {x + 0.29f, base + 0.69f - dip * 1.7f, z}, {0.10f, 0.03f, 0.03f},
        {0.85f, 0.65f, 0.25f, 1.0f});
    box(r, {x - 0.04f, base + 0.17f, z}, {0.03f, 0.34f, 0.03f}, leg);
    box(r, {x + 0.06f, base + 0.17f, z}, {0.03f, 0.34f, 0.03f}, leg);
}

} // namespace

const LevelDef kDef = {
    "tanbo",
    "Tanbo",
    "Mist lifts off the morning fields",
    {0.35f, 0.48f, 0.22f, 0.75f},
    kHalfWidth,
    kHalfDepth,
    // Early summer morning: fresh blue falling to a milky gold where the sun
    // is still low, green country running out below. Small fair clouds on a
    // light breeze.
    {{0.35f, 0.55f, 0.78f},
     {0.88f, 0.86f, 0.78f},
     {0.20f, 0.26f, 0.16f},
     {12, {1.0f, 0.99f, 0.95f}, {0.78f, 0.80f, 0.84f}, 1.0f, 0.015f},
     // A clear morning sun: crisp shadows, and no haze for shafts — the
     // `shafts` hook carries the ground mist instead (see registry.hpp).
     {{1.0f, 0.97f, 0.88f}, 0.0f, 0.35f}},
};

// Solid scenery: the scarecrow's pole and the drying racks' posts. Everything
// else in the field is water, mud, and rice — nothing a blade or a body
// should stop against.
const std::vector<LevelObstacle>& obstacles() {
    static const std::vector<LevelObstacle> v = [] {
        std::vector<LevelObstacle> out;
        // Kakashi pole, planted in the paddy bed.
        out.push_back({{kKakashiX, kBedY + 0.80f, kKakashiZ}, {0.06f, 0.80f, 0.06f}});
        // Hasa posts, a pair per rack on the dry back margin.
        for (const auto& rack : kHasa) {
            for (float side : {-1.0f, 1.0f}) {
                out.push_back({{rack[0] + side * 1.2f, 0.55f, rack[1]},
                               {0.07f, 0.55f, 0.07f}});
            }
        }
        return out;
    }();
    return v;
}

// Ground colliders: one slab sunk to the paddy bed under everything, with
// the dry pieces — margins and bunds — built back up to y = 0 on top of it.
// Extents match the default slab's (1 m overhang past walls).
const std::vector<LevelObstacle>& ground() {
    static const std::vector<LevelObstacle> v = [] {
        const float hw = kHalfWidth + 1.0f;
        const float hd = kHalfDepth + 1.0f;
        std::vector<LevelObstacle> out;
        // The bed, everywhere.
        out.push_back({{0.0f, kBedY - 0.5f, 0.0f}, {hw, 0.5f, hd}});
        // Dry margins ringing the field: two full-depth sides, two strips.
        const float lift = -kBedY * 0.5f; // thin box from bed up to y = 0
        out.push_back({{-(hw + kFieldX) * 0.5f, kBedY + lift, 0.0f},
                       {(hw - kFieldX) * 0.5f, lift, hd}});
        out.push_back({{(hw + kFieldX) * 0.5f, kBedY + lift, 0.0f},
                       {(hw - kFieldX) * 0.5f, lift, hd}});
        out.push_back({{0.0f, kBedY + lift, -(hd + kFieldZ) * 0.5f},
                       {kFieldX, lift, (hd - kFieldZ) * 0.5f}});
        out.push_back({{0.0f, kBedY + lift, (hd + kFieldZ) * 0.5f},
                       {kFieldX, lift, (hd - kFieldZ) * 0.5f}});
        // The causeway and the three cross paths.
        out.push_back({{0.0f, kBedY + lift, 0.0f}, {kFieldX, lift, kBundHalfW}});
        for (float x : kCrossX) {
            out.push_back({{x, kBedY + lift, 0.0f}, {kCrossHalfW, lift, kFieldZ}});
        }
        return out;
    }();
    return v;
}

// Eight still pools, one volume per basin — the reason levelWater is a list.
// No current: paddy water goes nowhere, so severed pieces bob where they
// fell and the blood above them washes away.
const std::vector<LevelWater>& water() {
    static const std::vector<LevelWater> v = [] {
        std::vector<LevelWater> out;
        for (const Pool& p : pools()) {
            out.push_back({{p.x0, kBedY - 0.1f, p.z0}, {p.x1, kSurfaceY, p.z1},
                           {0.0f, 0.0f, 0.0f}});
        }
        return out;
    }();
    return v;
}

// Mud, water, grass, rice, and the few built things a working field owns.
// All ambient motion is a pure function of `time`; nothing here touches the
// sim.
void draw(Renderer& r, float time) {
    const float floorW = kHalfWidth * 2.0f + 8.0f;
    const float floorD = kHalfDepth * 2.0f + 4.0f;

    // The bed under everything, then the dry ring and paths built up over it
    // in grass — same shapes the ground colliders take.
    box(r, {0.0f, kBedY - 0.5f, 0.0f}, {floorW, 1.0f, floorD}, kMud);
    box(r, {-(floorW * 0.5f + kFieldX) * 0.5f, -0.06f, 0.0f},
        {floorW * 0.5f - kFieldX, 0.12f, floorD}, kGrass);
    box(r, {(floorW * 0.5f + kFieldX) * 0.5f, -0.06f, 0.0f},
        {floorW * 0.5f - kFieldX, 0.12f, floorD}, kGrass);
    box(r, {0.0f, -0.06f, -(floorD * 0.5f + kFieldZ) * 0.5f},
        {kFieldX * 2.0f, 0.12f, floorD * 0.5f - kFieldZ}, kGrass);
    box(r, {0.0f, -0.06f, (floorD * 0.5f + kFieldZ) * 0.5f},
        {kFieldX * 2.0f, 0.12f, floorD * 0.5f - kFieldZ}, kGrass);
    // Bunds: an earthen body with a worn grass top.
    auto bund = [&](glm::vec3 center, glm::vec3 size) {
        box(r, {center.x, -0.07f, center.z}, {size.x, 0.10f, size.z}, kEarth);
        box(r, {center.x, -0.01f, center.z}, {size.x, 0.02f, size.z}, kGrass);
    };
    bund({0.0f, 0.0f, 0.0f}, {kFieldX * 2.0f, 0.0f, kBundHalfW * 2.0f});
    for (float x : kCrossX) {
        bund({x, 0.0f, 0.0f}, {kCrossHalfW * 2.0f, 0.0f, kFieldZ * 2.0f});
    }

    // Standing water, one sheet per pool. Translucent and depth-writing, so
    // everything under the surface — rice stems, wading shins, settled
    // debris — is clipped at the waterline for free.
    for (const Pool& p : pools()) {
        box(r, {(p.x0 + p.x1) * 0.5f, kSurfaceY - 0.02f, (p.z0 + p.z1) * 0.5f},
            {p.x1 - p.x0, 0.04f, p.z1 - p.z0}, kWaterSheet);
    }

    // Sun glitter: still water sparkles in place rather than scrolling.
    // Each glint has its own spot and blinks on the ripple of a breeze.
    for (int i = 0; i < 14; ++i) {
        const float gx = (hash01(i * 5 + 40) - 0.5f) * (kFieldX * 2.0f - 1.0f);
        const float gz = (hash01(i * 5 + 41) - 0.5f) * (kFieldZ * 2.0f - 1.0f);
        const float tw = std::sin(time * (0.8f + hash01(i * 5 + 42) * 0.8f) +
                                  hash01(i * 5 + 43) * 100.0f);
        const float a = std::max(0.0f, tw) * 0.55f;
        box(r, {gx, kSurfaceY + 0.005f, gz}, {0.28f, 0.015f, 0.10f},
            {1.0f, 0.98f, 0.90f, a}, true);
    }

    // Shadows for the built things, printed before they are drawn.
    const SunDef& sun = kDef.sky.sun;
    for (const LevelObstacle& b : obstacles()) {
        sunShadow(r, b.center, b.halfExtent, sun);
    }

    // The rice: seedling clumps in working rows, planted through the pools
    // and swaying together on the breeze. Pure decoration — a fighter wades
    // through the crop, and the rows read the ranging duel back to you as
    // bent and parted lines.
    const glm::vec4 riceA{0.45f, 0.62f, 0.25f, 1.0f};
    const glm::vec4 riceB{0.36f, 0.55f, 0.21f, 1.0f};
    int n = 0;
    for (const Pool& p : pools()) {
        for (float z = p.z0 + 0.55f; z < p.z1 - 0.35f; z += 0.95f) {
            for (float x = p.x0 + 0.55f; x < p.x1 - 0.35f; x += 0.90f) {
                ++n;
                if (hash01(n * 7 + 3) < 0.15f) {
                    continue; // a skipped planting here and there
                }
                const float h = 0.42f + hash01(n * 7 + 4) * 0.12f;
                const float lean =
                    std::sin(time * 0.7f + x * 0.35f + z * 0.5f) * 0.05f + 0.03f;
                glm::mat4 m = glm::translate(glm::mat4(1.0f), {x, kBedY, z});
                m = glm::rotate(m, hash01(n * 7 + 5) * 6.2831853f,
                                {0.0f, 1.0f, 0.0f});
                m = glm::rotate(m, lean, {0.0f, 0.0f, 1.0f});
                r.drawBox(glm::scale(glm::translate(m, {0.0f, h * 0.5f, 0.0f}),
                                     {0.16f, h, 0.10f}),
                          hash01(n * 7 + 6) > 0.5f ? riceA : riceB);
            }
        }
    }

    // The kakashi: solid pole, straw body, spread arms, a kasa of its own —
    // one more figure standing in the field, which is the whole joke of a
    // scarecrow in a duelling game.
    {
        const float px = kKakashiX;
        const float pz = kKakashiZ;
        box(r, {px, kBedY + 0.80f, pz}, {0.12f, 1.60f, 0.12f}, kWood);
        box(r, {px, 0.78f, pz}, {1.10f, 0.10f, 0.10f}, kWood);
        box(r, {px, 0.62f, pz}, {0.42f, 0.55f, 0.26f}, kStraw);
        box(r, {px, 1.06f, pz}, {0.20f, 0.20f, 0.20f}, {0.82f, 0.72f, 0.55f, 1.0f});
        box(r, {px, 1.20f, pz}, {0.55f, 0.09f, 0.55f}, {0.55f, 0.45f, 0.25f, 1.0f});
    }

    // Hasa racks on the back margin: posts from the solid list, a beam, and
    // last season's straw hanging in a curtain.
    for (const auto& rack : kHasa) {
        for (float side : {-1.0f, 1.0f}) {
            box(r, {rack[0] + side * 1.2f, 0.55f, rack[1]}, {0.14f, 1.10f, 0.14f},
                kWood);
        }
        box(r, {rack[0], 1.06f, rack[1]}, {2.70f, 0.09f, 0.09f}, kWood);
        box(r, {rack[0], 0.72f, rack[1]}, {2.40f, 0.60f, 0.20f}, kStraw);
    }

    drawEgret(r, kEgretX, kEgretZ, kBedY, time, 1);
    drawEgret(r, -11.5f, -7.2f, 0.0f, time, 2);

    // Past the back wall, on the drawn margin: the farmhouse the field
    // belongs to, and its tree. Backdrop only — nothing out there collides.
    box(r, {-9.0f, 0.7f, -9.6f}, {3.6f, 1.4f, 2.6f}, {0.28f, 0.22f, 0.16f, 1.0f});
    box(r, {-9.0f, 1.75f, -9.6f}, {4.4f, 0.8f, 3.2f}, {0.36f, 0.30f, 0.20f, 1.0f});
    box(r, {-9.0f, 2.25f, -9.6f}, {4.6f, 0.22f, 1.2f}, {0.30f, 0.25f, 0.17f, 1.0f});
    box(r, {8.5f, 1.5f, -9.7f}, {0.4f, 3.0f, 0.4f}, {0.22f, 0.16f, 0.11f, 1.0f});
    box(r, {8.5f, 3.1f, -9.7f}, {3.0f, 1.1f, 2.6f}, {0.15f, 0.25f, 0.13f, 1.0f});
    box(r, {8.9f, 3.9f, -9.9f}, {2.0f, 0.9f, 1.8f}, {0.18f, 0.29f, 0.15f, 1.0f});
    box(r, {8.2f, 4.5f, -9.6f}, {1.1f, 0.7f, 1.0f}, {0.21f, 0.33f, 0.17f, 1.0f});
}

// The morning mist, low over the pools — this level's translucent air, in
// the after-the-fighters slot the light shafts use elsewhere (see the note
// in registry.hpp). Each band breathes and drifts a little in place, and
// fades out entirely as its drift cycle turns over so nothing ever pops.
void shafts(Renderer& r, float time) {
    for (int i = 0; i < 6; ++i) {
        const float phase = hash01(i * 11 + 60) * 100.0f;
        const float cycle = std::fmod(time * 0.05f + hash01(i * 11 + 61), 1.0f);
        const float fade = std::sin(cycle * 3.14159265f);
        const float mx = (hash01(i * 11 + 62) - 0.5f) * 22.0f +
                         std::sin(time * 0.11f + phase) * 3.0f;
        const float mz = (hash01(i * 11 + 63) - 0.5f) * 12.0f;
        const float w = 8.0f + hash01(i * 11 + 64) * 5.0f;
        const float breathe = 0.06f + 0.03f * std::sin(time * 0.23f + phase);
        box(r, {mx, 0.32f + 0.08f * std::sin(time * 0.17f + phase), mz},
            {w, 0.45f, 2.4f}, {0.93f, 0.96f, 1.0f, breathe * fade}, true);
    }
}

} // namespace levels::tanbo
