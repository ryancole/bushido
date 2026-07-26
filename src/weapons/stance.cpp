#include "weapons/stance.hpp"

#include <algorithm>

// The three stances, and the whole of what separates one blade's swing from
// another's. See stance.hpp for the angle convention (0 = hanging down,
// +pi = overhead, negative = trailing behind).
//
// Normal is deliberately the tuning every attack had before stances existed,
// so the katana still plays exactly as it always did and the other two blades
// read as departures from it rather than everything moving at once.

namespace {

const StanceDef kStances[kStanceCount] = {
    // High (jodan) — the blade lives above the head, and every cut falls out
    // of that. The arcs are the longest in the game: a light swing travels
    // nearly 2.7 rad from wound-up-behind to the foe's shins, which is both
    // why it covers so much and why there is no taking it back once started.
    // The guard is up there with the blade, which looks like exactly what it
    // is — steel held where it is not going to catch much — so a block here
    // buys the shortest counter window of the three.
    {
        "High",
        "Blade overhead. The heaviest cuts, the poorest guard.",
        3.05f, // ready: near-vertical, a touch forward
        2.95f, // guard: still overhead
        {
            {3.35f, 0.65f}, // light: a full descending cleave
            {3.55f, 0.50f}, // heavy: wound further back, ending lower
            {2.45f, 1.60f}, // jab: a short downward snap out of the raise
        },
        1.12f,
        1.12f,
        0.60f,
    },
    // Normal (chudan) — point at the opponent, nothing given away. Balanced,
    // and the baseline the other two are measured against; these are the
    // angles every weapon used before it had a stance of its own.
    {
        "Normal",
        "Point at the foe. Even in attack and defense.",
        1.45f, // ready: level, out in front
        1.30f, // guard: just under level (unchanged)
        {
            {2.60f, 0.55f}, // light: shoulder height, down across
            {2.95f, 0.45f}, // heavy: from past overhead
            {1.20f, 1.55f}, // jab: a straight thrust, barely an arc
        },
        1.00f,
        1.00f,
        1.00f,
    },
    // Low (gedan) — the blade trails down and behind, so every cut is a rising
    // one that finishes above the head. It gives up the overhead threat
    // entirely and gets it back on defense: a blow caught from here opens half
    // again the counter window, which is the stance's whole argument for
    // fighting from inside the other blades' reach.
    //
    // The ready angle is what it is because of the floor. The blade is held
    // at rest, not only mid-swing, so this pose is on screen most of the
    // match: the tip sits at shoulder height (1.36) minus reach*cos(angle),
    // and a blade merely hanging down would spear through the ground on a
    // long-armed fighter. The longest reach that can carry this stance today
    // is a Kensei's 1.95 with the wakizashi's -0.25, and -0.78 rad leaves that
    // tip 0.15 m clear of the floor while still reading as unmistakably low.
    // Worth recomputing if a longer blade ever holds Low.
    {
        "Low",
        "Blade trailing low. Rising cuts, and the best guard.",
        -0.78f, // ready: down and behind, tip just off the ground
        0.75f,  // guard: angled down in front
        {
            {-0.55f, 1.95f}, // light: a rising cut, finishing high
            {-0.85f, 2.25f}, // heavy: from further back, carried further over
            {0.45f, 1.30f},  // jab: a rising thrust to the body
        },
        0.92f,
        0.92f,
        1.50f,
    },
};

} // namespace

const StanceDef& stanceDef(Stance stance) {
    return kStances[std::clamp(static_cast<int>(stance), 0, kStanceCount - 1)];
}
