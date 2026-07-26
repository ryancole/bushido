#pragma once

#include "weapons/stance.hpp" // how this blade is carried, and so how it swings

#include <glm/glm.hpp>

// A selectable sword. Same contract as the character roster
// (characters/character.hpp): compiled-in constant data, so a match is fully
// described by two (character, weapon) index pairs — peers would exchange
// indices/ids, never stats. Weapons modify the wielder's swing rather than
// replacing it: the character stats stay the baseline and these scale/offset
// them.
//
// This header is the whole public surface. Each blade lives in its own file
// beside it (katana.cpp, wakizashi.cpp, odachi.cpp) and is wired up through
// registry.hpp; weapon.cpp is nothing but the index → definition dispatch.

struct WeaponStats {
    float swingScale;     // multiplies windup/active/recovery times; <1 = faster
    float damage;         // scales torso-hit and sever blood costs; 1 = baseline
    float reachBonus;     // m added to the character's reach (drawn AND swept)
    float knockbackScale; // multiplies knockback dealt
    float bladeWidth;     // visual-only scale on the drawn blade's thickness
};

struct WeaponDef {
    const char* id; // stable slug — the serialization/netplay key, never reordered
    const char* name;
    const char* epithet; // select-screen flavor line
    WeaponStats stats;
    // The stance this blade is carried in — where it rests, where the guard
    // holds it, and the arc of every swing (weapons/stance.hpp). One per
    // weapon for now, which is what makes picking a sword a choice of style
    // and not only of numbers.
    Stance stance;
    glm::vec4 tileColor; // select-screen tile face (weapons don't recolor the model)
    // Authored 0..5 select-screen ratings, display only (like CharacterDef's).
    int rSpeed;
    int rDamage;
    int rReach;
};

inline constexpr int kWeaponCount = 3;
const WeaponDef& weaponDef(int index);
