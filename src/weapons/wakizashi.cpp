#include "weapons/registry.hpp"

// Wakizashi — lands often, costs little. The 0.70 swing scale cuts every
// phase of the attack (windup, active, recovery alike), so it both threatens
// sooner and leaves less to punish. Against that: 0.25 m off the reach, so it
// has to fight from inside the other blades' range, and 0.60 damage with 0.75
// knockback, so a torso hit barely bleeds and the shove won't buy space.
// Dismemberment is what makes it worth carrying — sever costs scale with
// damage, but a taken limb is gone for the match regardless, and beheading
// empties the pool with any blade at all.

namespace weapons::wakizashi {

const WeaponDef kDef = {
    .id = "wakizashi",
    .name = "Wakizashi",
    .epithet = "A whisper of steel",
    .stats =
        {
            .swingScale = 0.70f,
            .damage = 0.60f,
            .reachBonus = -0.25f,
            .knockbackScale = 0.75f,
            .bladeWidth = 0.80f,
        },
    .tileColor = {0.62f, 0.63f, 0.60f, 0.75f}, // pale steel
    .rSpeed = 5,
    .rDamage = 2,
    .rReach = 1,
};

} // namespace weapons::wakizashi
