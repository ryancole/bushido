#pragma once

#include "audio.hpp" // Sfx ids for sound cues

#include <glm/glm.hpp>

#include <memory>
#include <vector>

class Physics;

// A one-shot sound the sim wants played. The sim only raises these; main
// drains them each frame and hands them to Audio (panned by world x).
struct SoundCue {
    Sfx sfx;
    float x; // world x of the sound's source
};

struct PlayerInput {
    glm::vec2 move{0.0f, 0.0f}; // x: left/right, y: depth (+ toward camera), each -1..1
    bool jump = false;
    bool attack = false; // edge-triggered: true only on the tick(s) after a fresh press
};

// Sword swing phases. Values double as the pose index handed to the model.
enum class AttackState { None = 0, Windup = 1, Active = 2, Recovery = 3 };

// Severable body parts. Values index Player::severed and match the limb
// indices the samurai model exposes (samuraiLimbBounds / drawSeveredLimb).
// "Front" is the model's +z side — the sword arm's side.
enum class Limb { ArmFront = 0, ArmBack, LegFront, LegBack, Head };
inline constexpr int kLimbCount = 5;

// A limb that has been cut off and now tumbles as a physics debris body.
struct SeveredPiece {
    int victim;   // player index the limb came from (for colors)
    Limb limb;
    int debrisId; // handle into Physics' debris bodies
};

// World: x right, y up, z toward the camera; ground surface at y = 0.
// Units are meters-ish. Players move freely in the x/z ground plane.
struct Player {
    glm::vec3 pos{0.0f, 0.0f, 0.0f}; // center of the body box
    float vy = 0.0f;
    bool grounded = true;
    float facing = 1.0f;     // +1 toward +x; faces the opponent (locked mid-attack)
    float animPhase = 0.0f;  // walk-cycle phase in radians, advances with ground speed
    float moveAmount = 0.0f; // 0..1 fraction of max ground speed this tick

    AttackState attackState = AttackState::None;
    float attackTimer = 0.0f;  // seconds left in the current attack phase
    float attackT = 0.0f;      // 0..1 progress through the current attack phase
    bool attackLanded = false; // this swing already connected
    float hitstun = 0.0f;      // seconds of control lockout after being hit
    glm::vec2 kbVel{0.0f};     // knockback velocity in the ground plane
    bool severed[kLimbCount] = {}; // dismembered parts stay lost for the match

    static constexpr float kHalfWidth = 0.45f;
    static constexpr float kHalfHeight = 0.9f;
};

class Game {
public:
    Game();
    ~Game();
    void update(const PlayerInput inputs[2], float dt);
    const Player& player(int i) const { return m_players[i]; }

    const std::vector<SeveredPiece>& severedPieces() const { return m_pieces; }
    // World transform of a severed piece's debris body, for rendering.
    glm::mat4 severedPieceTransform(const SeveredPiece& piece) const;

    // Sounds raised by update() since the last clear; main drains these.
    const std::vector<SoundCue>& soundCues() const { return m_soundCues; }
    void clearSoundCues() { m_soundCues.clear(); }

    static constexpr float kArenaHalfWidth = 12.0f;
    static constexpr float kArenaHalfDepth = 5.0f;

private:
    void severLimb(int victim, Limb limb, const glm::vec2& impulseDir);

    Player m_players[2];
    std::vector<SeveredPiece> m_pieces;
    std::vector<SoundCue> m_soundCues;
    std::unique_ptr<Physics> m_physics; // collision & movement solver (Jolt)
};
