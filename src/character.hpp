#pragma once

#include "samurai.hpp" // SamuraiColors: a character owns its palette

// A selectable fighter. The whole roster is compiled-in constant data, and a
// match is fully described by two character indices — important for
// multiplayer later: peers only ever need to exchange indices (or the stable
// `id` slugs), never stats, and the sim stays deterministic across machines.

struct CharacterStats {
    float moveSpeed;    // m/s ground speed
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
    SamuraiColors colors;
    // Authored 0..5 select-screen ratings. Display only — gameplay reads
    // stats, the UI reads these, so a stat tweak doesn't skew the bars.
    int rSpeed;
    int rPower;
    int rReach;
    int rWeight;
};

inline constexpr int kCharacterCount = 4;
const CharacterDef& characterDef(int index);
