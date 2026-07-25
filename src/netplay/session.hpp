#pragma once

#include "input.hpp"          // PlayerInput, packInput/unpackInput
#include "netplay/replay.hpp" // MatchSetup
#include "netplay/transport.hpp"

#include <cstdint>

// Delay-based lockstep between two peers.
//
// Both machines run the identical simulation — that is the assumption the
// replay harness exists to check — so the wire carries *inputs*, never a
// world. Two bytes per frame per peer instead of two fighters, their debris,
// and a floor covered in blood.
//
// The shape of it: an input sampled now is not played now, it is scheduled
// kInputDelay frames ahead, which is how long the network gets to deliver it.
// A frame is simulated only once **both** peers' inputs for it are in hand, so
// the two machines advance in exact agreement or they do not advance at all. A
// missing remote input is a stall, not a guess — nothing is ever mispredicted,
// which is what makes this the honest first step and rollback the optimisation
// on top of it.
//
// Nothing here is retransmitted. Every packet repeats the last
// kInputRedundancy frames of input, so a dropped datagram is covered by the
// next one to land, and a reliability layer would only add the head-of-line
// blocking this protocol exists to avoid.

// 3 frames at 120 Hz is 25 ms of buffer — about one good LAN round trip. Raise
// it for a worse connection: every frame added is 8.3 ms of input lag bought in
// exchange for tolerating 8.3 ms more of network jitter before a stall.
inline constexpr int kInputDelay = 3;
// Frames of input repeated in each packet. At 12, four consecutive datagrams
// have to vanish before a stall, and the packet is still under 40 bytes.
inline constexpr int kInputRedundancy = 12;

class Session {
public:
    enum class Role { Host, Client };
    // Handshake: agreeing on the match. Running: stepping. Desynced: the peers
    // disagreed about a checksum, which is fatal and deliberately loud. Lost:
    // the peer went quiet for kTimeout.
    enum class State { Idle, Handshake, Running, Desynced, Lost };

    // The host already knows the match (it picked it) and sends it across; the
    // client passes anything and takes what arrives.
    void start(Transport* transport, Role role, const MatchSetup& setup);
    void stop();

    // Drains the socket and keeps the handshake going. Once per render frame.
    void pump(float dt);

    State state() const { return m_state; }
    bool active() const { return m_state != State::Idle; }
    // Meaningful once the handshake lands: on the client this is the host's,
    // which is the whole point of sending it.
    const MatchSetup& setup() const { return m_setup; }
    // The fighter this machine drives. Host is player 1, client player 2.
    int localSlot() const { return m_role == Role::Host ? 0 : 1; }

    // Both peers' inputs for the next frame, if they are in hand. False is a
    // stall: the remote input has not arrived, and neither machine may move.
    bool nextStep(const PlayerInput& local, PlayerInput out[2]);
    // The checksum the sim reached after that step. Sent to the peer, who
    // compares it against their own for the same frame.
    void stepped(std::uint32_t checksum);

    std::int64_t frame() const { return m_frame; }
    std::int64_t stalls() const { return m_stalls; } // steps lost waiting, all match
    // One line for the UI — "waiting for an opponent", "desynced at frame N".
    const char* status() const { return m_status; }
    // Is there something the player should be told? True while handshaking,
    // stalled on a missing input, desynced, or dropped — i.e. exactly when the
    // sim is not advancing and they deserve to know why.
    bool waiting() const { return m_state != State::Running || m_stalling; }

private:
    static constexpr int kRing = 256; // frames of history; drift never nears it
    static constexpr float kTimeout = 8.0f;    // s of silence before Lost
    static constexpr float kHandshakeGap = 0.25f; // s between handshake retries

    void receiveAll();
    void sendInputs();
    void sendHandshake();
    void setState(State s, const char* why);

    Transport* m_transport = nullptr;
    Role m_role = Role::Host;
    State m_state = State::Idle;
    MatchSetup m_setup;
    char m_status[96] = {};

    std::int64_t m_frame = 0;     // next frame to simulate
    std::int64_t m_localHead = 0; // next frame a local sample will be scheduled for
    std::int64_t m_stalls = 0;
    bool m_stalling = false; // currently short a remote input

    std::uint16_t m_local[kRing] = {};
    std::uint16_t m_remote[kRing] = {};
    bool m_remoteHas[kRing] = {};
    std::uint32_t m_sums[kRing] = {};
    bool m_sumsHas[kRing] = {};

    float m_sinceHandshake = 0.0f;
    float m_sinceHeard = 0.0f;
};
