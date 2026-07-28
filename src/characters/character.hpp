#pragma once

#include "samurai.hpp" // SamuraiLook: a character owns its whole appearance

// A selectable fighter. The whole roster is compiled-in constant data, and a
// match is fully described by two character indices — important for
// multiplayer later: peers only ever need to exchange indices (or the stable
// `id` slugs), never stats, and the sim stays deterministic across machines.
//
// This header is the whole public surface. Each fighter lives in its own file
// beside it (ronin.cpp, shinobi.cpp, …) and is wired up through registry.hpp;
// character.cpp is nothing but the index → definition dispatch.

struct CharacterStats {
    // Ground speed, in m/s, and deliberately slower than a walk: the roster
    // sits between 0.65 and 1.05, a pace you read as individual steps rather
    // than as travel. A hit here can end the match outright, so the approach
    // is the whole negotiation — closing the last two meters has to cost
    // enough seconds that the foe gets to answer it. These used to be 4.6–7.6,
    // a dead sprint that put the entire arena inside a windup.
    //
    // The Ronin's 0.85 is the anchor and is exactly half of Game's
    // kStrideCycle, i.e. **one footfall per second**. That is also where the
    // model and the floor agree: the drawn stride swings the leg ±0.55 rad
    // about a hip 0.85 m up, which carries the foot ~0.88 m — so a step
    // covers the ground it looks like it covers. Anything reading as speed
    // hangs off this number, since Game paces the stride by distance covered:
    // a slower fighter takes fewer steps, never shorter ones.
    float moveSpeed;

    float jumpVelocity; // m/s takeoff speed
    float windupTime;   // s from press until the blade goes live
    float activeTime;   // s the blade can connect
    float recoveryTime; // s locked out after the swing
    float reach;        // m from shoulder to blade tip (gameplay sweep AND drawn blade)
    float knockback;    // m/s planar shove dealt on connect
    float weight;       // divides knockback/pop received; 1 = baseline
};

struct CharacterDef {
    const char* id; // stable slug — the serialization/netplay key, never reordered
    const char* name;
    const char* epithet; // select-screen flavor line
    CharacterStats stats;
    // Everything about how this fighter is drawn — palette, headgear, and the
    // bespoke-geometry hook — authored here in the character's own file and
    // consumed by the one shared body builder. Purely visual: the sim never
    // reads it, so nothing in it can desync a peer (see SamuraiLook in
    // samurai.hpp for the rules a hook has to keep).
    SamuraiLook look;
    // Authored 0..5 select-screen ratings. Display only — gameplay reads
    // stats, the UI reads these, so a stat tweak doesn't skew the bars.
    int rSpeed;
    int rPower;
    int rReach;
    int rWeight;
};

inline constexpr int kCharacterCount = 4;
const CharacterDef& characterDef(int index);
