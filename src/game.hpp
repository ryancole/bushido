#pragma once

#include <glm/glm.hpp>

struct PlayerInput {
    float move = 0.0f; // -1..1
    bool jump = false;
};

// World: x right, y up, fighting plane at z = 0, ground surface at y = 0.
// Units are meters-ish.
struct Player {
    glm::vec2 pos{0.0f, 0.0f}; // center of the body box
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

private:
    Player m_players[2];
};
