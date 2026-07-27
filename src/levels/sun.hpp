#pragma once

#include "levels/level.hpp"

#include <glm/glm.hpp>

// The two things the sun does that a diffuse term in a fragment shader cannot:
// print a shadow on the ground where something stands in its way, and hang in
// the air as a shaft where it gets through. Shared by every battleground the
// way scenery.hpp's box/hash01 are — the geometry of a shadow is the same
// wherever it falls, so only the *placement* belongs in a level's own file.
//
// Both read levels::kSunDir (level.hpp), which is the same sun shaders/cube.frag
// shades against. That is the whole reason these read as light rather than as
// dark rectangles: the shadow leaves the face the shader already lit.

class Renderer;

namespace levels {

// The ground shadow of one solid box: its footprint pushed along the sun to
// where the light actually puts it, drawn as a flat translucent slab lying on
// the floor. An approximation on purpose — the true silhouette of a box is a
// hexagon, and the axis-aligned rectangle that encloses the swept footprint is
// both the right shape for a game made of boxes and one draw instead of three.
//
// Higher off the ground means fainter and wider, which is the only penumbra
// this gets and is most of what stops a canopy's shadow reading as a rug. The
// slab is lifted a couple of millimetres by a hash of its own position, so two
// overlapping footprints never land coplanar and z-fight; everything stays
// under the blood marks (y = 0.006 and up) and the blob shadows (y = 0.03), so
// gameplay's own floor marks still draw on top of scenery's.
void sunShadow(Renderer& r, glm::vec3 center, glm::vec3 halfExtent, const SunDef& sun);

// One shaft of sunlight, entering the air at `gap` — the hole in the canopy the
// light came through — and slanting down the sun's own direction to the floor.
// Authoring the gap rather than the beam's foot is what makes a level file
// readable: a shaft is placed where the leaves part, and where it lands is
// arithmetic.
//
// `seed` only varies the shimmer, which breathes on the same 0.7 rad/s the
// cherry canopies sway at — the beam flickers because the leaves it is coming
// through are moving, so the two want the same clock.
void sunShaft(Renderer& r, glm::vec3 gap, float width, float strength,
              const SunDef& sun, float time, int seed);

} // namespace levels
