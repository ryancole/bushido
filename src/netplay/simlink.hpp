#pragma once

#include "netplay/transport.hpp"

#include <cstdint>

// A Transport that wraps another one and makes it worse on purpose.
//
// Every netplay test so far has run over loopback: no latency, no loss, no
// reordering. That is the one network condition the game will never actually
// ship on, and a lockstep protocol's whole character — how much input delay it
// needs, how often it stalls, whether the redundancy window is wide enough —
// only shows up under a real link. This holds arriving datagrams back, drops
// some, and jitters the rest, so those questions can be answered before
// anyone's connection has to answer them.
//
// Delay is applied on *receive* rather than send, which models the same thing
// when both peers are simulating: each holds what arrives, so the round trip
// is twice the one-way figure. Jitter can reorder packets, which is realistic
// and which the protocol is supposed to survive — frames are indexed, not
// sequenced, so a late packet is either still useful or already redundant.
//
// This is deliberately outside the simulation: the two peers drop different
// packets, exactly as two real machines would, and nothing here touches the
// deterministic path.
class SimulatedLink : public Transport {
public:
    struct Conditions {
        float latencyMs = 0.0f; // one-way; round trip is twice this
        float jitterMs = 0.0f;  // uniform +/- on top of the latency
        float lossPercent = 0.0f;
    };

    SimulatedLink(Transport& inner, const Conditions& conditions, std::uint32_t seed);

    // Advances the link's clock. Call once per frame, before pumping the
    // session, or nothing is ever released.
    void advance(float dt);

    bool send(const void* data, int bytes) override;
    int receive(void* data, int capacity) override;
    bool peerKnown() const override;

    // Datagrams thrown away, for the run's summary.
    std::int64_t dropped() const { return m_dropped; }

private:
    static constexpr int kMaxPending = 128;
    static constexpr int kMaxPacket = 256;

    float frand(); // 0..1

    struct Held {
        unsigned char bytes[kMaxPacket];
        int size = 0;
        float due = 0.0f; // link time at which it may be delivered
        bool used = false;
    };

    Transport& m_inner;
    Conditions m_conditions;
    std::uint32_t m_rng;
    float m_now = 0.0f;
    std::int64_t m_dropped = 0;
    Held m_pending[kMaxPending];
};

// Parses "latency", "latency,jitter" or "latency,jitter,loss" (ms, ms,
// percent) — what --netsim takes. False on anything unparsable or negative.
bool parseConditions(const char* text, SimulatedLink::Conditions& out);
