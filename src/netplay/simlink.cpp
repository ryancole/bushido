#include "netplay/simlink.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

SimulatedLink::SimulatedLink(Transport& inner, const Conditions& conditions,
                             std::uint32_t seed)
    : m_inner(inner), m_conditions(conditions), m_rng(seed ? seed : 1u) {
    std::fprintf(stderr,
                 "net: simulating %.0f ms one-way (%.0f ms round trip), "
                 "%.0f ms jitter, %.1f%% loss\n",
                 conditions.latencyMs, conditions.latencyMs * 2.0f,
                 conditions.jitterMs, conditions.lossPercent);
}

float SimulatedLink::frand() {
    m_rng = m_rng * 1664525u + 1013904223u;
    return static_cast<float>(m_rng >> 8) * (1.0f / 16777216.0f);
}

void SimulatedLink::advance(float dt) { m_now += dt; }

bool SimulatedLink::send(const void* data, int bytes) {
    // Outgoing goes straight out. Both peers hold what they *receive*, so the
    // delay is applied once per direction either way, and doing it on one side
    // only keeps the buffer here reasoning about one clock.
    return m_inner.send(data, bytes);
}

int SimulatedLink::receive(void* data, int capacity) {
    // Drain the real socket into the holding pen first, so a packet's delay is
    // measured from when it actually turned up.
    for (;;) {
        unsigned char buf[kMaxPacket];
        int got = m_inner.receive(buf, kMaxPacket);
        if (got <= 0) {
            break;
        }
        if (m_conditions.lossPercent > 0.0f &&
            frand() * 100.0f < m_conditions.lossPercent) {
            ++m_dropped;
            continue;
        }
        int slot = -1;
        for (int i = 0; i < kMaxPending; ++i) {
            if (!m_pending[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            // Pen full: the link is holding more than a lockstep session can
            // possibly have in flight, so this is a runaway rather than a
            // condition worth modelling. Dropping is the honest answer.
            ++m_dropped;
            continue;
        }
        Held& h = m_pending[slot];
        std::memcpy(h.bytes, buf, static_cast<std::size_t>(got));
        h.size = got;
        float jitter = m_conditions.jitterMs > 0.0f
                           ? (frand() * 2.0f - 1.0f) * m_conditions.jitterMs
                           : 0.0f;
        float delay = (m_conditions.latencyMs + jitter) * 0.001f;
        h.due = m_now + (delay > 0.0f ? delay : 0.0f);
        h.used = true;
    }

    // Release the one that came due first, so ordering is by delivery time
    // rather than arrival — which is what lets jitter actually reorder.
    int best = -1;
    for (int i = 0; i < kMaxPending; ++i) {
        if (m_pending[i].used && m_pending[i].due <= m_now &&
            (best < 0 || m_pending[i].due < m_pending[best].due)) {
            best = i;
        }
    }
    if (best < 0) {
        return 0;
    }
    Held& h = m_pending[best];
    int size = h.size < capacity ? h.size : capacity;
    std::memcpy(data, h.bytes, static_cast<std::size_t>(size));
    h.used = false;
    return size;
}

bool SimulatedLink::peerKnown() const { return m_inner.peerKnown(); }

bool parseConditions(const char* text, SimulatedLink::Conditions& out) {
    float values[3] = {0.0f, 0.0f, 0.0f};
    const char* p = text;
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        values[i] = std::strtof(p, &end);
        if (end == p || values[i] < 0.0f) {
            return false;
        }
        p = end;
        if (*p == '\0') {
            break;
        }
        if (*p != ',') {
            return false;
        }
        ++p;
    }
    if (*p != '\0') {
        return false;
    }
    out.latencyMs = values[0];
    out.jitterMs = values[1];
    out.lossPercent = values[2];
    return true;
}
