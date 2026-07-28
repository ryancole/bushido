#include "characters/registry.hpp"

#include <glm/gtc/matrix_transform.hpp>

// Kensei — reach as the whole identity. At 1.95 m it out-ranges everyone by a
// clear margin, which matters twice: the sweep starts connecting sooner, and
// the drawn blade is that long too (SamuraiPose::reach), so the threat is
// visible. Everything else is deliberately unremarkable — baseline weight,
// modest knockback, a swing a touch slower than the Ronin's — so the fighter
// lives or dies on holding the spacing its reach earns.
//
// The look is ceremony rather than menace: a tall lacquered eboshi and the
// stiff wings of a kataginu, the dress of a duelist who expects the match to
// be decided at the distance of their choosing. The wings widen the fighter
// the way the blade lengthens them — the silhouette promises reach.

namespace characters::kensei {

namespace {

const glm::vec4 kLinen{0.82f, 0.78f, 0.70f, 1.0f};  // the bleached kimono cloth
const glm::vec4 kViolet{0.42f, 0.16f, 0.45f, 1.0f}; // the accent, as wing trim

// Kataginu wings: one stiff panel rising off each shoulder, tipped in
// violet. Marked detail even though the tips genuinely clear the torso —
// the shadow band's stack is budgeted (see kShadowTop in samurai.cpp), and
// a wing-tip missing from a shadow is a far smaller lie than two more
// casters crowding the band over the blood marks. Torso only, per the
// AdornFn rules.
void adorn(const AdornContext& ctx, const AdornPart& part) {
    for (float s : {-1.0f, 1.0f}) {
        const glm::mat4 wing =
            glm::rotate(glm::translate(ctx.upper, {0.0f, 1.42f, s * 0.26f}), -s * 0.5f,
                        glm::vec3(1.0f, 0.0f, 0.0f));
        part(wing, {0.0f, 0.02f, s * 0.10f}, {0.20f, 0.045f, 0.30f}, kLinen, true);
        part(wing, {0.0f, 0.025f, s * 0.24f}, {0.20f, 0.05f, 0.05f}, kViolet, true);
    }
}

} // namespace

const CharacterDef kDef = {
    .id = "kensei",
    .name = "Kensei",
    .epithet = "Death at a distance",
    .stats =
        {
            .moveSpeed = 0.75f,
            .jumpVelocity = 9.5f,
            .windupTime = 0.16f,
            .activeTime = 0.14f,
            .recoveryTime = 0.33f,
            .reach = 1.95f,
            .knockback = 7.0f,
            .weight = 1.0f,
        },
    // Bleached white over cold blue-grey, deep violet accents; capped and
    // winged by the eboshi and the kataginu hook above.
    .look =
        {
            .colors =
                {
                    .kimono = {0.82f, 0.78f, 0.70f, 1.0f},
                    .hakama = {0.22f, 0.24f, 0.30f, 1.0f},
                    .accent = {0.42f, 0.16f, 0.45f, 1.0f},
                },
            .headgear = Headgear::Eboshi,
            .adorn = adorn,
        },
    .rSpeed = 2,
    .rPower = 2,
    .rReach = 5,
    .rWeight = 3,
};

} // namespace characters::kensei
