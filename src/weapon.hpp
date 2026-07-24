#pragma once

#include <glm/glm.hpp>

// A selectable sword. Same contract as the character roster (character.hpp):
// compiled-in constant data, so a match is fully described by two
// (character, weapon) index pairs — peers would exchange indices/ids, never
// stats. Weapons modify the wielder's swing rather than replacing it: the
// character stats stay the baseline and these scale/offset them.

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
    glm::vec4 tileColor; // select-screen tile face (weapons don't recolor the model)
    // Authored 0..5 select-screen ratings, display only (like CharacterDef's).
    int rSpeed;
    int rDamage;
    int rReach;
};

inline constexpr int kWeaponCount = 3;
const WeaponDef& weaponDef(int index);
