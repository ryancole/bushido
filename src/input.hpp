#pragma once

#include "config.hpp" // Keybinds
#include "game.hpp"   // PlayerInput, AttackKind

struct GLFWwindow;

// Where a fighter's input comes from for the current match. The sim takes two
// PlayerInputs and has no idea which is which (see Game::update), so a match is
// described by a source per slot rather than by "player 2 is the bot" wired
// into the loop — which is what lets local versus and, later, a netplay peer
// reuse the same fixed-step dispatch.
enum class InputSource {
    Local0, // this machine's first control set
    Local1, // this machine's second control set (local versus)
    Bot,    // the AI in src/bot.hpp
    Remote, // a netplay peer; produces nothing until there is one
};

// Index of the local latch a source reads, or -1 for the sources that aren't a
// person at this keyboard.
inline int localSlot(InputSource s) {
    if (s == InputSource::Local0) return 0;
    if (s == InputSource::Local1) return 1;
    return -1;
}

// One locally-controlled fighter's input, latched across render frames.
//
// The held controls (move/jump/block/crouch) are levels and can be read fresh
// whenever. The two attack controls are *edges*: the attack control fires on
// release — a tap is the light swing, a hold past kHeavyHoldTime charges the
// heavy — and the jab control fires on press. Edges are detected once per
// render frame but consumed by fixed steps, and a frame can run zero of those,
// so a press has to sit here until a step takes it. This state used to be six
// loose locals in the main loop, which worked only for as long as exactly one
// fighter could ever be human.
class LocalInput {
public:
    static constexpr float kHeavyHoldTime = 0.28f; // s of hold that makes it a heavy

    // Re-seed from live control state at the start of a match. A control that
    // is already down is flagged suppressed, so the click that locked in the
    // battleground can't land as an opening swing when it is finally released.
    void beginMatch(GLFWwindow* window, const Keybinds& keys);

    // Refresh from the keyboard and mouse. Call once per render frame.
    void poll(GLFWwindow* window, const Keybinds& keys, float frameTime);

    // What the fixed steps should feed the sim this frame.
    const PlayerInput& current() const { return m_input; }

    // A fixed step has taken the latched attack; later steps in the same frame
    // must not swing again on the same press.
    void consumeAttack();

private:
    PlayerInput m_input;
    bool m_attackHeld = false;
    bool m_jabHeld = false;
    bool m_suppressed = false; // ignore the release of a click that predates the match
    float m_holdTime = 0.0f;   // s the attack control has been down
    bool m_pending = false;    // an attack is waiting for a fixed step to take it
    AttackKind m_pendingKind = AttackKind::Light;
};
