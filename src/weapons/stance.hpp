#pragma once

// A stance: how a blade is carried, and so where every swing starts and ends.
//
// Taken from Bushido Blade's three (wiki.supercombo.gg's BB2 system page):
// High holds the weapon over the head and gives the most offense, Normal holds
// it in front pointed at the foe and is balanced, Low points it down or behind
// and is the defensive one. Bushido Blade lets a fighter cycle all three
// mid-match, and now so does this: each weapon carries the stance it *starts*
// in — so choosing a blade still chooses a style — and the stanceUp /
// stanceDown inputs shift the carry from there, one step at a time
// (Game::setStance), clamped at High and Low rather than wrapped.
//
// Angles are about the shoulder, in the one convention game.cpp's blade sweep
// and samurai.cpp's sword arm both already use:
//
//     0      arm hanging straight down
//     +pi/2  level, out in front
//     +pi    straight overhead
//     >pi    up and behind (a raised, wound-up blade)
//     <0     down and behind (a trailing blade)
//
// The arcs live here rather than beside the rest of the per-attack tuning in
// game.cpp because the model animates the very angles the hit test sweeps: two
// copies of these numbers would drift, and the symptom is a cut landing
// somewhere the blade was never drawn.

enum class Stance { High = 0, Normal = 1, Low = 2 };
inline constexpr int kStanceCount = 3;

// One attack's Active-phase travel. The blade is a segment along the arm, so
// these are the whole shape of a swing.
struct StanceArc {
    float start;
    float end;
};

struct StanceDef {
    const char* name; // select-screen label
    const char* note; // one line on what the stance trades
    // Where the blade rests between swings: a recovery settles back to it, and
    // it is what a stance would look like if the fighter simply stood there.
    float readyAngle;
    float guardAngle; // where it is held with the guard up
    // Indexed by AttackKind (game.hpp): light, heavy, jab. Sized by hand
    // rather than by kAttackKindCount so this stays a leaf header — the model
    // includes it and has no business knowing the sim's enums.
    StanceArc arcs[3];
    // The offense/defense trade, small on purpose: the weapon itself is where
    // a loadout's character comes from, and a stance that also swung the
    // damage numbers around would be scoring the same trade twice. These only
    // tilt it. damage/knockback are the attacker's; riposte is the *defender's*
    // — how much of a counter window catching a blow in this stance opens.
    float damageScale;
    float knockbackScale;
    float riposteScale;
};

const StanceDef& stanceDef(Stance stance);
