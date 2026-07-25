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

// 3 frames at 120 Hz is 25 ms of buffer — about one good LAN round trip, and
// the default. It is a *setting*, not a constant, because it is the one knob
// that trades the two things a connection cares about against each other:
// every frame added is 8.3 ms of input lag bought in exchange for tolerating
// 8.3 ms more of network delay before the sim stalls. A link that needs more
// than it has does not degrade gracefully — it stalls on almost every frame.
inline constexpr int kDefaultInputDelay = 3;
inline constexpr int kMaxInputDelay = 32;
// Frames of input repeated in each packet. At 12, four consecutive datagrams
// have to vanish before a stall, and the packet is still under 40 bytes. The
// window actually sent is max(this, inputDelay): at startup a peer has seeded
// exactly inputDelay frames and sent nothing else, so a window narrower than
// the delay would leave the oldest frames — including frame 0 — never
// transmitted, and both sides would wait on them forever.
inline constexpr int kInputRedundancy = 12;
// Ceiling on frames in one packet, and so on the receive-side buffer.
inline constexpr int kMaxInputsPerPacket =
    kMaxInputDelay > kInputRedundancy ? kMaxInputDelay : kInputRedundancy;

// Sentinel for "no timestamp to echo yet", so the first packets of a session
// do not produce a nonsense round-trip sample.
inline constexpr std::uint32_t kNoStamp = 0xffffffffu;

class Session {
public:
    enum class Role { Host, Client };
    // Handshake: agreeing on the match. Running: stepping. Desynced: the peers
    // disagreed about a checksum, which is fatal and deliberately loud. Lost:
    // the peer went quiet for kTimeout.
    enum class State { Idle, Handshake, Running, Desynced, Lost };

    // Each side passes what it chose for itself: the host fills slots [0] and
    // the level, the client fills slots [1]. The handshake merges them — the
    // client's pick rides in on Hello, the host answers with the whole thing —
    // so neither player has their fighter chosen for them, and both end up
    // holding the identical MatchSetup that Game is built from.
    // `inputDelay` <= 0 measures the link and picks one; anything else pins it.
    // `tickSeconds` is the sim's fixed step, which is what turns a measured
    // round trip in milliseconds into a delay in frames.
    void start(Transport* transport, Role role, const MatchSetup& ownPick,
               int inputDelay, float tickSeconds);
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

    // What a peer wants to do once the match is over. Both are *agreed*, not
    // commanded — one player cannot restart the other's game, nor drag them
    // back to a select screen. Each peer's intent rides on every input packet,
    // so a request is a level rather than an edge: it repeats until the state
    // changes, and there is no ack to lose and nothing to retransmit. The same
    // property that lets the input stream drop packets safely.
    enum class Intent : std::uint8_t { None = 0, Rematch = 1, Reselect = 2 };

    void request(Intent intent) { m_intent = intent; }
    Intent requested() const { return m_intent; }
    Intent peerRequested() const { return m_peerIntent; }
    // What both sides asked for, or None while they disagree. A peer already
    // on the next match implies a Rematch: they only roll forward after seeing
    // our own request.
    Intent agreedIntent() const {
        if (m_peerAhead) {
            return Intent::Rematch;
        }
        return m_intent != Intent::None && m_intent == m_peerIntent ? m_intent
                                                                    : Intent::None;
    }

    // After a Reselect, each side picks again and submits its own half; the
    // level is the host's to give. Both halves travel on every packet, so the
    // merge needs no more handshaking than the intent did.
    void submitLoadout(int character, int weapon, int level);
    bool loadoutSubmitted() const { return m_loadoutReady; }
    bool loadoutsExchanged() const { return m_loadoutReady && m_peerLoadoutReady; }
    // Rolls onto the next match: frame counter and input rings clear, and the
    // match index moves so packets still in flight from the finished match are
    // ignored rather than replayed into the new one. The setup is unchanged.
    void beginRematch();

    std::int64_t frame() const { return m_frame; }
    std::int64_t stalls() const { return m_stalls; } // steps lost waiting, all match
    int inputDelay() const { return m_inputDelay; }
    // Smoothed round trip in ms, and its mean deviation. Zero until the first
    // echo comes back.
    float rttMs() const { return m_rttMs; }
    float rttJitterMs() const { return m_rttVarMs; }
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
    // Raises the delay to `frames` by repeating the newest scheduled input
    // into the gap. Only ever *raises* mid-match: lowering would mean unsending
    // frames the peer may already hold, so an improving link waits for the
    // next match to take the benefit while a degrading one is answered at once.
    void widenDelay(int frames);
    // What the measured link is asking for, in frames.
    int targetDelay() const;

    Transport* m_transport = nullptr;
    Role m_role = Role::Host;
    State m_state = State::Idle;
    MatchSetup m_setup;
    char m_status[96] = {};

    int m_inputDelay = kDefaultInputDelay;
    bool m_adaptDelay = true;      // false once --delay pins it
    float m_tickSeconds = 1.0f / 120.0f;
    // Round-trip estimate. Every input packet carries our clock and echoes the
    // last one we heard, so a sample needs no clock sync — just our own clock
    // minus what came back. The echo also folds in the peer's send
    // granularity, which is the delay that actually matters here.
    float m_clockMs = 0.0f;
    std::uint32_t m_peerStamp = kNoStamp; // their newest clock value, to echo back
    float m_rttMs = 0.0f;
    float m_rttVarMs = 0.0f;
    std::int64_t m_frame = 0;     // next frame to simulate
    std::int64_t m_localHead = 0; // next frame a local sample will be scheduled for
    std::int64_t m_stalls = 0;
    bool m_stalling = false; // currently short a remote input
    // Same two counts, but across every match of the session rather than the
    // current one — a rematch zeroes the per-match figures, which would
    // otherwise make a long soak report only its last few seconds.
    std::int64_t m_totalFrames = 0;
    std::int64_t m_totalStalls = 0;

    // Which match of this session we are on. Travels on every input packet so
    // a peer one match behind (or ahead) can tell the streams apart.
    std::uint32_t m_match = 0;
    Intent m_intent = Intent::None;
    Intent m_peerIntent = Intent::None;
    bool m_peerAhead = false; // peer has already started the next match
    // Each side's half of the next match. The peer's is kept from every packet
    // rather than only from a "ready" one, so if they roll forward before their
    // ready flag reaches us we still hold the pick they rolled with.
    bool m_loadoutReady = false;
    bool m_peerLoadoutReady = false;
    std::int32_t m_peerChar = 0;
    std::int32_t m_peerWeapon = 0;
    std::int32_t m_peerLevel = 0;

    std::uint16_t m_local[kRing] = {};
    std::uint16_t m_remote[kRing] = {};
    bool m_remoteHas[kRing] = {};
    std::uint32_t m_sums[kRing] = {};
    bool m_sumsHas[kRing] = {};

    float m_sinceHandshake = 0.0f;
    float m_sinceHeard = 0.0f;
};
