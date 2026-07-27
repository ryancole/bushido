#include "levels/level.hpp"

#include "levels/scenery.hpp"

#include <cmath>

// The sky shell: one piece of geometry shared by every battleground, drawn in
// whatever colors and weather that level's SkyDef authored. Levels differ in
// weather, not in how a sky is put together, so the shape lives here and level
// files only say what the sky looks like.
//
// Everything the renderer draws is a box, so the gradient is banded rather than
// smooth: a stack of four-walled rings from the horizon up to a cap, each ring
// one step along horizon → zenith. Below the horizon the same rings carry the
// ground color, which is the distant land the arena floor sits on — visible
// past the edge of that floor and under it nowhere else.

namespace {

using levels::box;
using levels::hash01;

constexpr float kPi = 3.14159265358979f;

// Two things fix the size. The shell must enclose the camera from anywhere it
// can get to, and it must fit inside FramingCamera's 200 m far plane from its
// own center — a corner of these numbers sits ~180 m out, which leaves room
// for the eye to sit above the horizon plane. Building it around the eye is
// what makes both hold at once: FramingCamera pulls back further the narrower
// the window, so a shell pinned to the arena would have to guess how far.
constexpr float kHalf = 110.0f;   // horizontal reach from the eye
constexpr float kTop = 90.0f;     // zenith cap height above the ground plane
constexpr float kBottom = -40.0f; // floor of the shell, well under the arena
constexpr float kPlate = 2.0f;    // wall thickness; only the inner face is seen
// Bands enough that the steps stop reading as steps. The camera sits near the
// ground and its vertical fov is narrow, so only the lowest few bands are ever
// on screen at once — the count has to buy resolution *there*, and 8 left
// visible seams behind the Dojo's pillars.
constexpr int kBands = 16;

// One four-walled ring of the shell, spanning [y0, y1] in a single color. The
// walls overlap at the corners (each spans the full width) so the ring is
// closed from the inside at every angle; the overlapping faces are
// perpendicular, so nothing z-fights.
void ring(Renderer& r, const glm::vec3& eye, float y0, float y1, glm::vec4 color) {
    const float h = y1 - y0;
    const float mid = (y0 + y1) * 0.5f;
    const float span = kHalf * 2.0f + kPlate;
    box(r, {eye.x, mid, eye.z - kHalf}, {span, h, kPlate}, color, true);
    box(r, {eye.x, mid, eye.z + kHalf}, {span, h, kPlate}, color, true);
    box(r, {eye.x - kHalf, mid, eye.z}, {kPlate, h, span}, color, true);
    box(r, {eye.x + kHalf, mid, eye.z}, {kPlate, h, span}, color, true);
}

// Band boundaries rise quadratically while the color steps evenly, so the
// bands are thin and the color moves fast right above the horizon — which is
// where a sky's gradient actually lives — and the upper half is near enough
// flat zenith.
float bandY(int i) {
    const float t = static_cast<float>(i) / static_cast<float>(kBands);
    return kTop * t * t;
}

// ---- Clouds ---------------------------------------------------------------
//
// The deck rides its own ring inside the shell, and the wind is quoted as an
// *angle* per second rather than meters: what a distant cloud does on screen is
// sweep across, and a cloud translated in world meters would instead slide out
// of the sky and leave a hole where it had been. Turning the ring is what a
// skybox has always done, and it costs nothing to keep honest — the ring is
// closed, so no cloud ever pops in or out at an edge and there is no wrap to
// hide. Like the shell, it is centered on the eye, so the deck never gets
// closer and the camera can never walk out from under it.
//
// Radius is bounded by the shell it hangs inside: a cloud is a few meters deep,
// so the far ring plus that has to clear the wall's inner face at kHalf. Height
// is authored as an *elevation angle* from the eye instead of a y, which is
// both what the camera actually cares about (the frame's top edge is a fixed
// 25° above the horizontal view axis, since FramingCamera's fov never changes)
// and what a sky at infinity does when the eye rises: hold its angle.
//
// The band is deliberately the *lower* half of the sky. A camera standing on
// the ground sees a cloud overhead belly-first, and a belly made of boxes is a
// flat slab with nothing on it — the crowns that say "cloud" are all on top,
// and only a cloud seen near the horizon shows them. Which is also where clouds
// mostly are: the deck is a layer, so it reaches the horizon and stops well
// short of the zenith.
constexpr float kCloudNear = 74.0f;
constexpr float kCloudFar = 96.0f;
constexpr float kCloudRef = 85.0f;      // the radius `drift` is quoted at
constexpr float kCloudLowElev = 0.10f;  // ~6°, just clear of the treetops
constexpr float kCloudHighElev = 0.34f; // ~19°, short of the frame's top edge

// One cloud, as a cluster of boxes in its own frame: +x across the sky, +z
// toward the eye. A base with crowns piled on it — `lit` is where the piece
// sits between the deck's two authored colors, and it is the whole of the shape
// reading as a cloud rather than as a stack of boxes. The base is deliberately
// two staggered slabs rather than one long one: an unbroken rectangle of
// underside is exactly what makes a box cloud look like a plank.
struct Puff {
    glm::vec3 offset;
    glm::vec3 size;
    float lit;
};

constexpr Puff kCluster[] = {
    {{-3.0f, 0.0f, 0.5f}, {8.0f, 2.1f, 4.5f}, 0.0f},  // base, windward half
    {{3.0f, 0.45f, -0.8f}, {7.0f, 1.9f, 4.0f}, 0.1f}, // base, lee half
    {{-2.5f, 1.7f, 0.3f}, {5.5f, 3.0f, 4.0f}, 1.0f},  // main crown
    {{2.1f, 1.8f, -0.4f}, {4.5f, 2.7f, 3.5f}, 0.8f},
    {{-0.5f, 3.2f, 0.0f}, {3.2f, 1.6f, 2.8f}, 1.0f},  // cap on the crown
    {{5.7f, 0.6f, 0.25f}, {3.0f, 1.6f, 2.5f}, 0.5f},  // tails, thinning out
    {{-6.5f, 0.4f, -0.3f}, {2.8f, 1.4f, 2.3f}, 0.45f},
};

// Everything about cloud `i` falls out of hash01 and the clock — no stored
// state, exactly like the petals and the ripples, so two machines drawing the
// same second of the same match draw the same sky.
void drawCloud(Renderer& r, const glm::vec3& eye, const SkyDef& sky, int i, int count,
               float time) {
    const CloudDef& def = sky.clouds;
    const int seed = i * 8;
    const float radius = glm::mix(kCloudNear, kCloudFar, hash01(seed + 1));
    // Angular speed rises as the ring closes in, the way it does for anything
    // passing at a fixed speed — the near clouds visibly overtake the far ones,
    // which is the only depth a sky of flat colors gets to have.
    const float omega = def.drift * kCloudRef / radius;
    // Spread evenly around the ring and then jittered within a slot, so the
    // deck is neither a picket fence nor clumped into one quarter of the sky.
    const float angle = 2.0f * kPi *
                            (static_cast<float>(i) + hash01(seed + 2)) /
                            static_cast<float>(count) +
                        omega * time;
    const float elev = glm::mix(kCloudLowElev, kCloudHighElev, hash01(seed + 3));

    const glm::vec3 center{eye.x + radius * std::sin(angle),
                           eye.y + radius * std::tan(elev),
                           eye.z + radius * std::cos(angle)};

    // The lower it sits the more of the horizon's haze is in front of it. This
    // is what stops a deck looking pasted onto the gradient rather than hanging
    // in it, and it costs one mix toward a color the level already authored.
    const float haze =
        0.25f * (1.0f - (elev - kCloudLowElev) / (kCloudHighElev - kCloudLowElev));

    const float scale = def.scale * glm::mix(0.8f, 1.25f, hash01(seed + 4));
    const float stretch = glm::mix(0.9f, 1.25f, hash01(seed + 5)); // width alone
    const float flip = hash01(seed + 6) < 0.5f ? -1.0f : 1.0f;

    // Yaw the cluster to present its width to the eye. The pieces are boxes and
    // so cannot billboard properly, but a cloud built long in x and shallow in z
    // would otherwise turn edge-on as it came round the side of the sky — which
    // is exactly where the versus intro's close-ups look.
    glm::mat4 frame = glm::translate(glm::mat4(1.0f), center);
    frame = glm::rotate(frame, angle + kPi, {0.0f, 1.0f, 0.0f});

    for (const Puff& p : kCluster) {
        glm::vec3 color = glm::mix(def.shaded, def.sunlit, p.lit);
        color = glm::mix(color, sky.horizon, haze);
        glm::mat4 m = glm::translate(
            frame, glm::vec3{p.offset.x * flip * stretch, p.offset.y, p.offset.z} *
                       scale);
        m = glm::scale(m, glm::vec3{p.size.x * stretch, p.size.y, p.size.z} * scale);
        // Unlit for the same reason the shell is: a cloud is part of the sky,
        // and shading it would make its color depend on which side of the ring
        // it happened to be on. The two authored colors do that job on purpose.
        r.drawBox(m, {color, 1.0f}, true);
    }
}

} // namespace

void drawSky(Renderer& renderer, int index, const glm::vec3& eye, float time) {
    const SkyDef& sky = levelDef(index).sky;
    const glm::vec4 zenith{sky.zenith, 1.0f};
    const glm::vec4 ground{sky.ground, 1.0f};

    // The land: one ring below the horizon plus the floor cap under it, so a
    // camera that ever looks down past the arena floor still finds ground.
    ring(renderer, eye, kBottom, 0.0f, ground);
    box(renderer, {eye.x, kBottom, eye.z},
        {kHalf * 2.0f + kPlate, kPlate, kHalf * 2.0f + kPlate}, ground, true);

    // Sky proper, horizon up to the zenith cap.
    for (int i = 0; i < kBands; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(kBands);
        ring(renderer, eye, bandY(i), bandY(i + 1),
             {glm::mix(sky.horizon, sky.zenith, t), 1.0f});
    }
    box(renderer, {eye.x, kTop, eye.z},
        {kHalf * 2.0f + kPlate, kPlate, kHalf * 2.0f + kPlate}, zenith, true);

    // Weather last: the deck hangs inside the shell, so the depth test sorts it
    // against those walls for free, and a level with a clear sky draws nothing.
    for (int i = 0; i < sky.clouds.count; ++i) {
        drawCloud(renderer, eye, sky, i, sky.clouds.count, time);
    }
}
