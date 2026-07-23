#pragma once

#include <glm/glm.hpp>

struct PlayerInput {
    glm::vec2 move{0.0f, 0.0f}; // x: left/right, y: depth (+ toward camera), each -1..1
    bool jump = false;
};

// World: x right, y up, z toward the camera; ground surface at y = 0.
// Units are meters-ish. Players move freely in the x/z ground plane.
struct Player {
    glm::vec3 pos{0.0f, 0.0f, 0.0f}; // center of the body box
    float vy = 0.0f;
    bool grounded = true;
    float facing = 1.0f; // +1 toward +x; always faces the opponent

    static constexpr float kHalfWidth = 0.45f;
    static constexpr float kHalfHeight = 0.9f;
};

class Game {
public:
    Game();
    void update(const PlayerInput inputs[2], float dt);
    const Player& player(int i) const { return m_players[i]; }

    static constexpr float kArenaHalfWidth = 12.0f;
    static constexpr float kArenaHalfDepth = 5.0f;

private:
    Player m_players[2];
};
