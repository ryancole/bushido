#include "levels/registry.hpp"

#include "game.hpp" // arena bounds — scenery is sized off the playable area
#include "levels/scenery.hpp"
#include "levels/sun.hpp"

#include <cmath>

// Sorihashi: a vermillion arched bridge at dusk, spanning a river the whole
// width of the widest arena yet. The duel lives on the deck — long in x,
// 4.2 m of it in z — so the level is the shape the bridge is: wide and
// narrow. Everything solid is authored once and shared between the drawing
// and the colliders, same guarantee as Hanami's stones and house.
//
// Two pieces of stagecraft are deliberate rather than accidental:
//
// The railing stands only on the *far* edge. A parapet on the camera side sat
// exactly across the sightline to the fighters' shins — the legs a low swing
// hunts — and a rail you can't see past is worse than no rail. The near edge
// gets a kerb beam instead (well under the 0.4 m step-up, so it stops nothing
// but rolling debris), the way all of Dojo's pillars stand behind the stage.
//
// The river is *two* water volumes, one strip each side of the deck. One box
// covering both would cover the deck's footprint too, and that footprint is
// what Game suppresses blood marks inside — a bridge that could never stain
// is half the point of fighting on one. It is also why falling in is a
// setback rather than a sentence: the bed is 0.6 m down (too deep to climb
// the deck's side), but a stone shelf runs along each bank at -0.28, so the
// way out is to wade to a bank — two steps, each under the 0.4 m step-up —
// and walk back around to the bridge.

namespace levels::sorihashi {
namespace {

// The bridge plays wider than Hanami and much narrower than it looks: the
// def below and the floor math both read these so the drawn stage and the
// physics agree.
constexpr float kHalfWidth = 17.0f; // arena half width (side walls)
constexpr float kBankX = 10.5f;     // river spans between the banks
constexpr float kDeckHalfZ = 2.1f;  // the crossing is this narrow

// The river: bed deep enough that the deck reads as *above* the water, with
// the climb-out done in two sub-0.4 steps (bed -> shelf -> bank) so nobody is
// ever wedged in the current — a match that cannot end is a hang, and the
// clock papering over one is not the same as not digging one.
constexpr float kBedY = -0.60f;
constexpr float kSurfaceY = -0.30f;
constexpr float kShelfTopY = -0.28f; // stone lip poking just above the water
constexpr float kShelfW = 0.55f;     // how far the lip reaches off each bank

// The arch's crown: one step up (under the characters' 0.4 m step-up), flat
// on top, well clear of the spawns at x = +-3 (capsule radius 0.45).
constexpr float kHumpH = 0.26f;
constexpr float kHumpHalfX = 2.2f;

// Railing geometry, shared by the solid list and the drawing. Posts stand on
// whatever the deck is doing under them (flat or crown), beams run between.
constexpr float kPostHalf = 0.07f;
constexpr float kPostH = 0.84f;
constexpr float kRailZ = kDeckHalfZ - 0.08f; // post centers; outer face flush
constexpr int kPostCount = 11;               // per side of nothing — far edge only
constexpr float kPostSpacing = 2.06f;        // posts span x = -10.3 .. 10.3

constexpr glm::vec4 kVermillion{0.62f, 0.16f, 0.12f, 1.0f};
constexpr glm::vec4 kVermillionLit{0.70f, 0.21f, 0.14f, 1.0f};
constexpr glm::vec4 kBronze{0.20f, 0.16f, 0.11f, 1.0f}; // giboshi caps
constexpr glm::vec4 kDeckWood{0.34f, 0.25f, 0.17f, 1.0f};
constexpr glm::vec4 kCrownWood{0.38f, 0.28f, 0.19f, 1.0f};
constexpr glm::vec4 kStone{0.38f, 0.37f, 0.35f, 1.0f};

float postX(int i) {
    return -kPostSpacing * 0.5f * static_cast<float>(kPostCount - 1) +
           kPostSpacing * static_cast<float>(i);
}

float deckTopAt(float x) {
    return std::abs(x) < kHumpHalfX ? kHumpH : 0.0f;
}

// Everything solid on the bridge: deck, crown, the far railing (posts and top
// beams), the near kerb, the two near end posts, and the lantern bases on the
// far bank. One list drives the colliders and the drawing both.
const std::vector<LevelObstacle>& bridgeSolids() {
    static const std::vector<LevelObstacle> v = [] {
        std::vector<LevelObstacle> out;
        // Deck slab: top flush with the banks at y = 0, sides reaching below
        // the waterline so the bridge shows 0.3 m of freeboard and nothing
        // can slip beneath it. Overlaps the banks so there is no seam.
        out.push_back({{0.0f, -0.175f, 0.0f}, {kBankX + 0.6f, 0.175f, kDeckHalfZ}});
        // The crown — the sori in sorihashi, all 0.26 m of it. Any taller and
        // the step onto it stops being a step.
        out.push_back({{0.0f, kHumpH * 0.5f, 0.0f}, {kHumpHalfX, kHumpH * 0.5f, kDeckHalfZ}});
        // Far railing: posts riding the deck's own height, beams between.
        for (int i = 0; i < kPostCount; ++i) {
            const float x = postX(i);
            const float base = deckTopAt(x);
            out.push_back({{x, base + kPostH * 0.5f, -kRailZ},
                           {kPostHalf, kPostH * 0.5f, kPostHalf}});
        }
        // Top beams, one per deck height so the rail follows the arch: two
        // flat spans and the crown's. The 0.26 jog where they meet is the
        // arch told in the railing, which a level camera reads better than
        // the deck itself.
        const float flatBeamY = 0.66f;
        const float spanHalf = (10.3f - kHumpHalfX) * 0.5f;
        const float spanMid = kHumpHalfX + spanHalf;
        out.push_back({{-spanMid, flatBeamY, -kRailZ}, {spanHalf, 0.05f, 0.05f}});
        out.push_back({{spanMid, flatBeamY, -kRailZ}, {spanHalf, 0.05f, 0.05f}});
        out.push_back({{0.0f, kHumpH + flatBeamY, -kRailZ}, {kHumpHalfX, 0.05f, 0.05f}});
        // Near kerb: the deck edge beam, 0.12 m of it — under the step-up, so
        // it stops rolling debris and nobody's feet. Follows the crown.
        out.push_back({{-spanMid, 0.06f, kRailZ}, {spanHalf, 0.06f, 0.08f}});
        out.push_back({{spanMid, 0.06f, kRailZ}, {spanHalf, 0.06f, 0.08f}});
        out.push_back({{0.0f, kHumpH + 0.06f, kRailZ}, {kHumpHalfX, 0.06f, 0.08f}});
        // Near end posts: the kerb still gets its giboshi where the bridge
        // meets the banks, framing the stage without standing in front of it.
        for (float side : {-1.0f, 1.0f}) {
            out.push_back({{side * 10.3f, kPostH * 0.5f, kRailZ},
                           {kPostHalf, kPostH * 0.5f, kPostHalf}});
        }
        // Stone lantern bases on the far bank, one by each bridge end.
        for (float side : {-1.0f, 1.0f}) {
            out.push_back({{side * 12.2f, 0.14f, -3.4f}, {0.30f, 0.14f, 0.30f}});
        }
        return out;
    }();
    return v;
}

// The two backdrop pines on the far bank — cosmetic, like Hanami's leaning
// rank: past the fight, there for depth.
struct Pine {
    float x, z, h;
    int seed;
};
constexpr Pine kPines[] = {
    {-13.6f, -5.8f, 3.4f, 11},
    {14.2f, -6.3f, 3.0f, 47},
};

void drawPine(Renderer& r, const Pine& p) {
    box(r, {p.x, p.h * 0.5f, p.z}, {0.28f, p.h, 0.28f}, {0.16f, 0.12f, 0.09f, 1.0f});
    const glm::vec4 needle{0.10f, 0.17f, 0.13f, 1.0f};
    for (int t = 0; t < 3; ++t) {
        const float w = 2.4f - 0.7f * static_cast<float>(t) +
                        (hash01(p.seed + t) - 0.5f) * 0.3f;
        box(r, {p.x, p.h * (0.55f + 0.18f * static_cast<float>(t)), p.z},
            {w, 0.55f, w * 0.8f}, needle);
    }
}

// A stone lantern (toro) by each far bridge end, lit for the hour. The base
// is drawn from the solid list like the rest of the bridge; everything above
// it here is too thin to matter to a fight happening a bank away.
void drawLantern(Renderer& r, float x, float z) {
    box(r, {x, 0.56f, z}, {0.18f, 0.56f, 0.18f}, kStone);
    box(r, {x, 0.98f, z}, {0.32f, 0.28f, 0.32f}, kStone);
    // The flame's glow, unlit like everything that is light rather than
    // surface — a shaded window would be lit by the sun instead of the flame.
    box(r, {x, 0.98f, z + 0.17f}, {0.20f, 0.16f, 0.02f},
        {1.0f, 0.78f, 0.42f, 0.95f}, true);
    box(r, {x, 1.18f, z}, {0.52f, 0.12f, 0.52f}, {0.30f, 0.29f, 0.27f, 1.0f});
    box(r, {x, 1.28f, z}, {0.14f, 0.10f, 0.14f}, kStone);
}

} // namespace

const LevelDef kDef = {
    "sorihashi",
    "Sorihashi",
    "Fireflies cross the vermillion span",
    {0.55f, 0.20f, 0.16f, 0.75f},
    kHalfWidth,
    // Dusk: violet overhead falling to an amber band the bridge stands
    // against, the far land already night. Long sunset streaks, lit hard from
    // below the way only the last light manages, drifting slowly.
    {{0.10f, 0.10f, 0.24f},
     {0.88f, 0.52f, 0.30f},
     {0.10f, 0.09f, 0.10f},
     {16, {0.98f, 0.72f, 0.55f}, {0.35f, 0.28f, 0.40f}, 1.1f, 0.012f},
     // The last of the sun: warm, no haze left to hang shafts in, and
     // shadows going soft as the light goes level.
     {{1.0f, 0.72f, 0.45f}, 0.0f, 0.24f}},
};

const std::vector<LevelObstacle>& obstacles() {
    return bridgeSolids();
}

// Ground colliders: the flat slab is replaced by the two banks with the river
// carved between them, the bed at kBedY, and the stone shelf along each bank
// that makes the climb out two legal steps instead of one impossible one.
// Extents match the default slab's (1 m overhang past walls).
const std::vector<LevelObstacle>& ground() {
    static const std::vector<LevelObstacle> v = [] {
        const float hw = kHalfWidth + 1.0f;
        const float hd = Game::kArenaHalfDepth + 1.0f;
        std::vector<LevelObstacle> out;
        // Banks, top at y = 0.
        out.push_back({{-(hw + kBankX) * 0.5f, -0.5f, 0.0f},
                       {(hw - kBankX) * 0.5f, 0.5f, hd}});
        out.push_back({{(hw + kBankX) * 0.5f, -0.5f, 0.0f},
                       {(hw - kBankX) * 0.5f, 0.5f, hd}});
        // River bed.
        out.push_back({{0.0f, kBedY - 0.5f, 0.0f}, {kBankX, 0.5f, hd}});
        // Stone shelves hugging each bank's foot, top just above the water.
        for (float side : {-1.0f, 1.0f}) {
            out.push_back({{side * (kBankX - kShelfW * 0.5f), kShelfTopY - 0.25f, 0.0f},
                           {kShelfW * 0.5f, 0.25f, hd}});
        }
        return out;
    }();
    return v;
}

// The river, one strip each side of the dry deck — see the header comment for
// why it cannot be one box. Both flow toward the camera, the same +z the
// glints scroll: what drifts under the far fascia is simply gone downstream,
// and what is knocked off the near edge bobs past in front of the fight.
const std::vector<LevelWater>& water() {
    static const std::vector<LevelWater> v = [] {
        const float hd = Game::kArenaHalfDepth + 2.0f;
        std::vector<LevelWater> out;
        out.push_back({{-kBankX, kBedY, -hd}, {kBankX, kSurfaceY, -kDeckHalfZ},
                       {0.0f, 0.0f, 0.5f}});
        out.push_back({{-kBankX, kBedY, kDeckHalfZ}, {kBankX, kSurfaceY, hd},
                       {0.0f, 0.0f, 0.5f}});
        return out;
    }();
    return v;
}

// Dusk banks, the river with its drifting glints, the bridge from the shared
// solid list, lanterns, reeds, pines — and the fireflies, which are to this
// level what the petals are to Hanami. All ambient motion is a pure function
// of `time`; nothing here touches the sim.
void draw(Renderer& r, float time) {
    const float floorW = kHalfWidth * 2.0f + 8.0f;
    const float floorD = Game::kArenaHalfDepth * 2.0f + 4.0f;

    // Banks: dusk grass, already more shadow than green.
    box(r, {-(floorW * 0.5f + kBankX) * 0.5f, -0.5f, 0.0f},
        {floorW * 0.5f - kBankX, 1.0f, floorD}, {0.13f, 0.17f, 0.11f, 1.0f});
    box(r, {(floorW * 0.5f + kBankX) * 0.5f, -0.5f, 0.0f},
        {floorW * 0.5f - kBankX, 1.0f, floorD}, {0.13f, 0.17f, 0.11f, 1.0f});
    // Bed, shelves, and the water lying over them: the sheet is the sky's
    // own violet with the amber it is losing, translucent so what floats
    // shows half-submerged.
    box(r, {0.0f, kBedY - 0.325f, 0.0f}, {kBankX * 2.0f, 0.65f, floorD},
        {0.10f, 0.09f, 0.08f, 1.0f});
    for (float side : {-1.0f, 1.0f}) {
        box(r, {side * (kBankX - kShelfW * 0.5f), kShelfTopY - 0.25f, 0.0f},
            {kShelfW, 0.5f, floorD}, kStone);
    }
    box(r, {0.0f, kSurfaceY - 0.03f, 0.0f}, {kBankX * 2.0f, 0.06f, floorD},
        {0.16f, 0.15f, 0.28f, 0.90f});

    // Ripple glints, scrolling the same +z the current carries debris on
    // both strips — the far strip's run under the bridge and the near
    // strip's out past the camera, one river passing beneath the deck.
    for (int i = 0; i < 18; ++i) {
        const bool near = i % 2 == 0;
        const float lane = 2.5f + hash01(i * 3 + 70) * 1.8f;
        const float run = std::fmod(hash01(i * 3 + 71) * lane + time * 0.9f, lane);
        const float gz = near ? kDeckHalfZ + 0.4f + run
                              : -(kDeckHalfZ + 0.4f + lane) + run;
        const float gx = (hash01(i * 3 + 72) - 0.5f) * (kBankX * 2.0f - 1.2f);
        const bool warm = hash01(i * 3 + 73) > 0.5f;
        box(r, {gx, kSurfaceY + 0.01f, gz}, {0.50f, 0.02f, 0.15f},
            warm ? glm::vec4{0.95f, 0.65f, 0.40f, 0.75f}
                 : glm::vec4{0.72f, 0.68f, 0.92f, 0.70f});
    }

    // Shadows before the things that cast them, same order as Hanami — but
    // only the *posts* print. The deck's own shadow falls on moving water,
    // which has no floor to print on (a slab hovering over the river reads
    // as a hole in it), and the long beams cast mostly past the deck's far
    // edge into the same water. The posts' shadows land on the deck, which
    // is exactly the sort of shadow a level camera does read. Each lantern
    // casts once, as its whole column.
    const SunDef& sun = kDef.sky.sun;
    for (const LevelObstacle& b : bridgeSolids()) {
        if (b.halfExtent.x < 1.0f && b.center.y > 0.2f) {
            sunShadow(r, b.center, b.halfExtent, sun);
        }
    }
    for (float side : {-1.0f, 1.0f}) {
        sunShadow(r, {side * 12.2f, 0.7f, -3.4f}, {0.26f, 0.7f, 0.26f}, sun);
    }

    // The bridge, drawn from the same solid list Physics collides: deck and
    // crown in wood, rails and kerb in lacquer.
    {
        const std::vector<LevelObstacle>& solids = bridgeSolids();
        for (std::size_t i = 0; i < solids.size(); ++i) {
            const LevelObstacle& b = solids[i];
            glm::vec4 color = kVermillion;
            if (i == 0) {
                color = kDeckWood;
            } else if (i == 1) {
                color = kCrownWood;
            } else if (b.halfExtent.x > 1.0f && b.center.y > 0.3f) {
                color = kVermillionLit; // the top beams catch the low sun
            } else if (b.halfExtent.y < 0.2f && b.center.y > 0.1f) {
                color = kStone; // lantern bases
            }
            box(r, b.center, b.halfExtent * 2.0f, color);
        }
    }
    // Lacquered fascia along both deck edges, proud of the slab so the
    // freeboard reads as built rather than cut.
    for (float side : {-1.0f, 1.0f}) {
        box(r, {0.0f, -0.14f, side * (kDeckHalfZ + 0.02f)},
            {(kBankX + 0.6f) * 2.0f, 0.30f, 0.06f}, {0.55f, 0.15f, 0.11f, 1.0f});
        box(r, {0.0f, kHumpH * 0.5f, side * (kDeckHalfZ + 0.02f)},
            {kHumpHalfX * 2.0f, kHumpH, 0.06f}, {0.55f, 0.15f, 0.11f, 1.0f});
    }
    // Lower rail beam and the giboshi, cosmetic: the caps are what the eye
    // knows a bridge like this by, and they are far too small to fight.
    {
        const float spanHalf = (10.3f - kHumpHalfX) * 0.5f;
        const float spanMid = kHumpHalfX + spanHalf;
        box(r, {-spanMid, 0.34f, -kRailZ}, {spanHalf * 2.0f, 0.07f, 0.07f}, kVermillion);
        box(r, {spanMid, 0.34f, -kRailZ}, {spanHalf * 2.0f, 0.07f, 0.07f}, kVermillion);
        box(r, {0.0f, kHumpH + 0.34f, -kRailZ}, {kHumpHalfX * 2.0f, 0.07f, 0.07f},
            kVermillion);
        for (int i = 0; i < kPostCount; ++i) {
            const float x = postX(i);
            const float top = deckTopAt(x) + kPostH;
            box(r, {x, top + 0.05f, -kRailZ}, {0.11f, 0.10f, 0.11f}, kBronze);
            box(r, {x, top + 0.13f, -kRailZ}, {0.05f, 0.06f, 0.05f}, kBronze);
        }
        for (float side : {-1.0f, 1.0f}) {
            box(r, {side * 10.3f, kPostH + 0.05f, kRailZ}, {0.11f, 0.10f, 0.11f},
                kBronze);
            box(r, {side * 10.3f, kPostH + 0.13f, kRailZ}, {0.05f, 0.06f, 0.05f},
                kBronze);
        }
    }

    for (float side : {-1.0f, 1.0f}) {
        drawLantern(r, side * 12.2f, -3.4f);
    }
    for (const Pine& p : kPines) {
        drawPine(r, p);
    }

    // Reeds along the waterline, leaning on a slow breeze. Kept mostly to the
    // far half so nothing sways between the camera and a fighter wading.
    for (int i = 0; i < 12; ++i) {
        const float side = i % 2 == 0 ? -1.0f : 1.0f;
        const float rx = side * (kBankX + 0.15f + hash01(i * 7 + 20) * 0.5f);
        const float rz = -0.5f - hash01(i * 7 + 21) * 5.5f;
        const float h = 0.5f + hash01(i * 7 + 22) * 0.4f;
        const float sway = std::sin(time * 0.9f + static_cast<float>(i) * 1.9f) * 0.06f;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), {rx, 0.0f, rz});
        m = glm::rotate(m, 0.08f + sway, {0.0f, 0.0f, 1.0f});
        r.drawBox(glm::scale(glm::translate(m, {0.0f, h * 0.5f, 0.0f}),
                             {0.05f, h, 0.05f}),
                  {0.18f, 0.30f, 0.16f, 1.0f});
    }

    // Fireflies: each index owns a slow wander and its own blink, warm and
    // unlit — they are light, not surface, and at dusk they carry the level
    // the way the petals carry Hanami. Alpha rides the blink so one winks
    // out while another comes on.
    for (int i = 0; i < 16; ++i) {
        const float cx = (hash01(i * 9 + 1) - 0.5f) * 26.0f;
        const float cz = (hash01(i * 9 + 2) - 0.5f) * 9.0f;
        const float phase = hash01(i * 9 + 3) * 100.0f;
        const float fx = cx + std::sin(time * (0.31f + hash01(i * 9 + 4) * 0.2f) + phase) * 1.4f;
        const float fz = cz + std::cos(time * (0.23f + hash01(i * 9 + 5) * 0.2f) + phase) * 1.0f;
        const float fy = 0.45f + hash01(i * 9 + 6) * 0.9f +
                         std::sin(time * 0.6f + phase * 1.7f) * 0.25f;
        const float blink = std::sin(time * (1.1f + hash01(i * 9 + 7) * 0.8f) + phase);
        const float a = 0.15f + 0.85f * std::max(0.0f, blink) * std::max(0.0f, blink);
        box(r, {fx, fy, fz}, {0.05f, 0.05f, 0.05f}, {1.0f, 0.82f, 0.40f, a}, true);
    }
}

} // namespace levels::sorihashi
