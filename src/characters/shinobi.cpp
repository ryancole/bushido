#include "characters/registry.hpp"

#include <glm/gtc/matrix_transform.hpp>

// Shinobi — speed bought with everything else. Fastest on the ground and the
// highest jump, and the whole swing (windup 0.09 / active 0.12 / recovery
// 0.21) undercuts the Ronin's, so it wins exchanges by striking first rather
// than by striking hard. The price is the shortest reach in the cast and a
// 0.75 weight: it has to get inside to work, and every hit it takes shoves it
// a third further than it would shove anyone else.
//
// The look says the same thing the stats do: no hat to catch the wind, a hood
// and mask instead of a face, and the kit strapped tight to the body — a
// chest strap and a second blade slung across the small of the back. Nothing
// on this fighter hangs loose.

namespace characters::shinobi {

namespace {

const glm::vec4 kStrap{0.10f, 0.10f, 0.12f, 1.0f};    // oiled leather
const glm::vec4 kScabbard{0.07f, 0.07f, 0.09f, 1.0f}; // dull lacquer
const glm::vec4 kWrap{0.80f, 0.78f, 0.70f, 1.0f};     // the bone-pale accent

// Everything hangs off `upper` (torso frame) and everything is detail: the
// strap and the slung blade live inside the torso-and-arms silhouette, so
// they spend nothing from the shadow band's budget. Rotated locals are fine
// here — unlike headgear, none of this ever leaves the body on a debris
// transform.
void adorn(const AdornContext& ctx, const AdornPart& part) {
    // Strap crossing the chest from the sword-side shoulder to the off hip,
    // riding just proud of the kimono's front face.
    const glm::mat4 strap =
        glm::rotate(glm::translate(ctx.upper, {0.21f, 1.22f, 0.0f}), 0.6f,
                    glm::vec3(1.0f, 0.0f, 0.0f));
    part(strap, {0.0f, 0.0f, 0.0f}, {0.025f, 0.44f, 0.09f}, kStrap, true);

    // The ninjato it carries: slung low across the back, grip up on the
    // sword side where the hand would find it. A straight blade in a plain
    // sheath — the visible spare answers the select screen's question of why
    // this fighter fears losing a sword less than anyone.
    const glm::mat4 sheath =
        glm::rotate(glm::translate(ctx.upper, {-0.24f, 1.14f, 0.0f}), -0.45f,
                    glm::vec3(1.0f, 0.0f, 0.0f));
    part(sheath, {0.0f, 0.0f, -0.02f}, {0.05f, 0.05f, 0.58f}, kScabbard, true);
    part(sheath, {0.0f, 0.0f, 0.34f}, {0.045f, 0.045f, 0.14f}, kWrap, true);
}

} // namespace

const CharacterDef kDef = {
    .id = "shinobi",
    .name = "Shinobi",
    .epithet = "Strikes before the breath",
    .stats =
        {
            .moveSpeed = 1.05f,
            .jumpVelocity = 11.5f,
            .windupTime = 0.09f,
            .activeTime = 0.12f,
            .recoveryTime = 0.21f,
            .reach = 1.40f,
            .knockback = 6.0f,
            .weight = 0.75f,
        },
    // Indigo over slate, bone-pale accents; hooded and masked in the hakama's
    // own slate, with the strap-and-sheath kit from the adorn hook above.
    .look =
        {
            .colors =
                {
                    .kimono = {0.15f, 0.28f, 0.72f, 1.0f},
                    .hakama = {0.15f, 0.16f, 0.20f, 1.0f},
                    .accent = {0.80f, 0.78f, 0.70f, 1.0f},
                },
            .headgear = Headgear::Hood,
            .adorn = adorn,
        },
    .rSpeed = 5,
    .rPower = 2,
    .rReach = 2,
    .rWeight = 2,
};

} // namespace characters::shinobi
