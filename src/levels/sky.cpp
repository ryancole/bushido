#include "levels/level.hpp"

#include "levels/scenery.hpp"

// The sky shell: one piece of geometry shared by every battleground, drawn in
// whatever three colors that level's SkyDef authored. Levels differ in weather,
// not in how a sky is put together, so the shape lives here and level files
// only say what color the sky is.
//
// Everything the renderer draws is a box, so the gradient is banded rather than
// smooth: a stack of four-walled rings from the horizon up to a cap, each ring
// one step along horizon → zenith. Below the horizon the same rings carry the
// ground color, which is the distant land the arena floor sits on — visible
// past the edge of that floor and under it nowhere else.

namespace {

using levels::box;

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

} // namespace

void drawSky(Renderer& renderer, int index, const glm::vec3& eye) {
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
}
