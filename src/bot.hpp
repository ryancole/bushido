#pragma once

#include "game.hpp"

#include <cstdint>

// Drives one fighter from the sim's public state, producing the same
// PlayerInput a human would. Deliberately simple: close to strike range,
// line up in depth, swing on a randomized cooldown, and occasionally
// strafe, back off, or jump so it doesn't play like a homing missile.
// Lives outside Game so the sim stays a pure function of its two inputs.
class Bot {
public:
    explicit Bot(int self = 1, std::uint32_t seed = 0xb07ca75u);
    PlayerInput think(const Game& game, float dt);

private:
    enum class Mode { Approach, Strafe, Retreat };
    float frand(); // 0..1

    int m_self;
    std::uint32_t m_rng;
    Mode m_mode = Mode::Approach;
    float m_modeTimer = 0.0f;     // s left before re-picking a mode
    float m_strafeSign = 1.0f;    // z direction while strafing
    float m_attackDelay = 0.0f;   // s before it's willing to swing again
    bool m_jumpQueued = false;    // one-shot hop, decided with the mode
    bool m_foeWasWinding = false; // edge-detect the foe's windup for dodging
};
