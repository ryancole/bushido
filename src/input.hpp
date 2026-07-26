#pragma once

#include "config.hpp" // Keybinds
#include "game.hpp"   // PlayerInput, AttackKind

#include <cstdint>

struct GLFWwindow;

// A PlayerInput in twelve bits. This is the replay record *and* the netplay
// wire format, defined once so a recorded match and a transmitted one can
// never drift apart. move.x/move.y only ever take -1, 0 or +1 (readHeld
// builds each from two opposed buttons), so two bits apiece is lossless — and
// saying so here is what keeps a non-canonical float out of the input path,
// where it would be a desync waiting to happen.
//
// Layout: [1:0] move.x + 1, [3:2] move.y + 1, [4] jump, [5] block,
//         [6] crouch, [7] attack, [9:8] attackKind, [10] drop, [11] sprint.
// Bits are only ever appended: the packet is a fixed two bytes either way, so
// widening the meaning of a spare bit costs nothing on the wire, and an older
// recording replays with the new bit clear, which is exactly right — nobody
// dropped anything in a match played before the control existed.
std::uint16_t packInput(const PlayerInput& in);
PlayerInput unpackInput(std::uint16_t bits);

// Round a movement axis to one of the three states the wire format carries.
//
// This is a *contract on every input source*, not a detail of the packer. An
// input that does not survive packInput unchanged is one the sim plays and a
// recording — or a netplay peer — never sees: the source moved at 0.4 and
// everyone else replayed it at 1. So a source that naturally thinks in
// continuous values (the bot steering into a depth lane) quantizes here, at
// the point it decides, rather than being silently rounded downstream.
float quantizeAxis(float v);

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
// whenever. The rest are *edges*: the attack control fires on release — a tap
// is the light swing, a hold past kHeavyHoldTime charges the heavy — while
// the jab and drop controls fire on press. Edges are detected once per render
// frame but consumed by fixed steps, and a frame can run zero of those, so a
// press has to sit here until a step takes it. This state used to be six
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

    // A fixed step has taken the latched edges; later steps in the same frame
    // must not swing — or throw the blade down — again on the same press.
    void consumeEdges();

private:
    PlayerInput m_input;
    bool m_attackHeld = false;
    bool m_jabHeld = false;
    bool m_dropHeld = false;
    bool m_suppressed = false; // ignore the release of a click that predates the match
    float m_holdTime = 0.0f;   // s the attack control has been down
    bool m_pending = false;    // an attack is waiting for a fixed step to take it
    AttackKind m_pendingKind = AttackKind::Light;
    bool m_dropPending = false; // a drop/take-up is waiting for a fixed step
};
