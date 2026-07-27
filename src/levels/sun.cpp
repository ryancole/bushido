#include "levels/sun.hpp"

#include "levels/scenery.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace levels {
namespace {

constexpr float kPi = 3.14159265358979f;

// Where a point at height y lands on the floor once the sun has pushed it
// down. The one piece of geometry both halves of this file share: a shadow is
// the cast of a box's corners, a shaft is the cast of the gap it shines
// through, and they slant by exactly the same amount because it is one sun.
glm::vec2 castDown(const glm::vec3& p) {
    const float t = p.y / kSunDir.y;
    return {p.x - kSunDir.x * t, p.z - kSunDir.z * t};
}

// A few millimetres of lift, deterministic in the footprint's own position, so
// that two shadows landing on top of each other (a canopy over its own trunk,
// a stone under a branch) sit on separate planes instead of tearing. The whole
// band lives under the blood marks — see the header.
float shadowY(const glm::vec2& at) {
    const int key = static_cast<int>(std::floor(at.x * 31.0f)) * 149 +
                    static_cast<int>(std::floor(at.y * 31.0f));
    return 0.0015f + hash01(key) * 0.0035f;
}

// What one box prints on the floor: all eight corners of the unit cube under
// `model` cast down the sun, and the axis-aligned rectangle enclosing them.
// Corners already below the floor cast straight down onto their own footing,
// which is what keeps a caster standing in a hollow from throwing its shadow
// off sideways from underground.
//
// One function for both entry points, because an oriented box and an
// axis-aligned one differ only in how the corners are reached: the rectangle
// this returns for a box with no rotation is exactly the top-face/bottom-face
// spread sunShadow used to compute by hand.
struct Footprint {
    glm::vec2 mid, half;
    float top; // highest the caster stands — what softens and fades its shadow
};

Footprint castBox(const glm::mat4& model) {
    glm::vec2 lo{1e9f, 1e9f};
    glm::vec2 hi{-1e9f, -1e9f};
    float top = -1e9f;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner{(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f,
                               (i & 4) ? 0.5f : -0.5f};
        const glm::vec3 p = glm::vec3(model * glm::vec4(corner, 1.0f));
        top = std::max(top, p.y);
        const glm::vec2 at = castDown({p.x, std::max(p.y, 0.0f), p.z});
        lo = glm::min(lo, at);
        hi = glm::max(hi, at);
    }
    return {(lo + hi) * 0.5f, (hi - lo) * 0.5f, top};
}

void drawFootprint(Renderer& r, const Footprint& f, const ShadowSlab& slab) {
    box(r, {f.mid.x, slab.y, f.mid.y},
        {(f.half.x + slab.soften) * 2.0f, slab.thickness,
         (f.half.y + slab.soften) * 2.0f},
        // Clamped because a caster is allowed to ask for more than the level's
        // own shadow strength (a fighter does) and blending past 1 does not
        // mean anything darker — only wrong.
        {0.03f, 0.04f, 0.07f, std::min(slab.alpha, 1.0f)});
}

// ---- Shafts ---------------------------------------------------------------

// A beam has to end somewhere, and both of its ends are lies: the top is where
// we decided the leaves parted, the bottom is where we ran out of floor. So
// both fade — a plateau with soft ends, which is what turns two cut faces of a
// box into a column of lit air.
float shaftFade(float t) {
    return glm::smoothstep(0.0f, 0.40f, t) * (1.0f - glm::smoothstep(0.70f, 1.0f, t));
}

// The beam is cut into segments along its length so the fade above has anything
// to grade, and into lanes across it so it has a soft edge. Lanes rather than
// one box inside another, which is the obvious way to build a glow and does not
// work here: the pipeline writes depth, so an outer shell's near face would win
// the depth test and hide the bright core inside it. Side by side, nothing
// overlaps and nothing has to be sorted.
constexpr int kSegments = 6;
constexpr float kLaneAlpha[] = {0.40f, 1.0f, 0.40f};
constexpr float kLaneOffset[] = {-1.0f, 0.0f, 1.0f};
// Alpha of the core lane at full strength. Every box is drawn with culling off,
// so its front and back faces both blend and the value on screen is roughly
// twice this — which is accounted for here rather than fixed in the renderer,
// since the water sheet and the petals have always been drawn the same way.
constexpr float kCoreAlpha = 0.10f;

} // namespace

void sunShadow(Renderer& r, glm::vec3 center, glm::vec3 halfExtent, const SunDef& sun) {
    const float top = center.y + halfExtent.y;
    if (sun.shadow <= 0.0f || top <= 0.0f) {
        return; // no sun to cast it, or nothing standing above the floor
    }

    // The footprint spans everything between the cast of the box's top face and
    // the cast of its bottom — that spread along the sun is what makes a tall
    // thing's shadow long and a stone's nearly its own size.
    const Footprint f =
        castBox(glm::scale(glm::translate(glm::mat4(1.0f), center), halfExtent * 2.0f));

    // The higher the caster, the softer and the fainter: a canopy four metres up
    // prints a wide grey suggestion, a stone prints a crisp dark patch.
    drawFootprint(r, f,
                  {shadowY(f.mid), 0.0018f, top * 0.09f,
                   sun.shadow / (1.0f + top * 0.22f)});
}

void sunShadowBox(Renderer& r, const glm::mat4& model, const SunDef& sun,
                  const ShadowSlab& slab) {
    if (sun.shadow <= 0.0f) {
        return;
    }
    const Footprint f = castBox(model);
    if (f.top <= 0.0f) {
        return; // nothing standing above the floor to cast anything
    }
    drawFootprint(r, f, {slab.y, slab.thickness, slab.soften, sun.shadow * slab.alpha});
}

void sunShaft(Renderer& r, glm::vec3 gap, float width, float strength,
              const SunDef& sun, float time, int seed) {
    const float amount = sun.shafts * strength;
    if (amount <= 0.0f || gap.y <= 0.0f) {
        return;
    }

    // From the gap straight down the sun's own line to the floor.
    const float length = gap.y / kSunDir.y;
    const glm::vec2 footXZ = castDown(gap);
    const glm::vec3 foot{footXZ.x, 0.0f, footXZ.y};

    // Lanes spread across the beam as the camera sees it, not along some world
    // axis: the camera looks down -Z, so the direction that puts the lanes side
    // by side on screen is the one square to both the beam and the view.
    const glm::vec3 lateral =
        glm::normalize(glm::cross(kSunDir, glm::vec3{0.0f, 0.0f, 1.0f}));

    // Stand a unit cube's +y up along the beam. Its long axis is the sun, so
    // every segment is drawn in the same rotation and it is worked out once.
    const glm::vec3 spin = glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, kSunDir);
    const float spinLen = glm::length(spin);
    const glm::mat4 tilt =
        spinLen < 1e-5f
            ? glm::mat4(1.0f)
            : glm::rotate(glm::mat4(1.0f),
                          std::acos(std::clamp(kSunDir.y, -1.0f, 1.0f)), spin / spinLen);

    // Leaves moving in front of the sun, on the cherry canopies' own sway rate.
    const float shimmer =
        0.82f + 0.18f * std::sin(time * 0.7f + static_cast<float>(seed) * 1.9f);
    const float segment = length / static_cast<float>(kSegments);

    for (int i = 0; i < kSegments; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSegments);
        const float fade = shaftFade(t);
        if (fade <= 0.0f) {
            continue;
        }
        // Narrow where it squeezes through the leaves, spreading as it falls —
        // sunlight is parallel, but scattered light is not, and a column of
        // constant width reads as a plank of glass.
        const float w = width * (0.72f + 0.55f * (1.0f - t));
        const glm::vec3 center = foot + kSunDir * (t * length);

        for (int lane = 0; lane < 3; ++lane) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f),
                                         center + lateral * (kLaneOffset[lane] * w));
            m = glm::scale(m * tilt, {w, segment * 1.02f, w});
            // Unlit, like everything else that is light rather than a surface:
            // a shaft shaded by the sun it *is* would be darker on the side
            // facing away from itself.
            r.drawBox(m, {sun.color, amount * fade * shimmer * kCoreAlpha * kLaneAlpha[lane]},
                      true);
        }
    }
}

} // namespace levels
