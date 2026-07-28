#include "netplay/session.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Bumped whenever the packet layout or the meaning of a field changes. Two
// builds that disagree here refuse each other at the handshake rather than
// desyncing ten seconds in, where the cause would be much harder to see.
constexpr std::uint32_t kProtocol = 6; // v6: inputs widened to u32 on the wire

enum class Msg : std::uint8_t {
    Hello = 0,   // client -> host: I would like a match, and here is my fighter
    Welcome = 1, // host -> client: here is the match, yours included
    Inputs = 2,  // both ways, every frame
};

constexpr int kMaxPacket = 256;

// Little-endian by hand rather than memcpy of a struct: the layout is the
// protocol, and it should not move because a compiler felt like padding.
struct Writer {
    unsigned char* p;
    int cap;
    int n = 0;
    void u8(std::uint8_t v) {
        if (n < cap) p[n++] = v;
    }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v & 0xff));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v & 0xffff));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
};

struct Reader {
    const unsigned char* p;
    int len;
    int n = 0;
    bool ok = true;
    std::uint8_t u8() {
        if (n >= len) {
            ok = false;
            return 0;
        }
        return p[n++];
    }
    std::uint16_t u16() {
        std::uint16_t lo = u8();
        return static_cast<std::uint16_t>(lo | (static_cast<std::uint16_t>(u8()) << 8));
    }
    std::uint32_t u32() {
        std::uint32_t lo = u16();
        return lo | (static_cast<std::uint32_t>(u16()) << 16);
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
};

} // namespace

void Session::start(Transport* transport, Role role, const MatchSetup& ownPick,
                    int inputDelay, float tickSeconds) {
    *this = Session{};
    m_transport = transport;
    m_role = role;
    m_setup = ownPick; // half filled in until the handshake merges the other half
    m_tickSeconds = tickSeconds > 0.0f ? tickSeconds : 1.0f / 120.0f;
    m_adaptDelay = inputDelay <= 0;
    m_ownsLevel = role == Role::Host; // until a match has been lost by someone
    m_inputDelay = m_adaptDelay ? kDefaultInputDelay
                   : inputDelay > kMaxInputDelay ? kMaxInputDelay
                                                 : inputDelay;

    // Both peers seed the first m_inputDelay frames with neutral input: nobody
    // has pressed anything yet, and it means each side has something to send
    // the moment it comes up, so the handshake and the first inputs overlap
    // instead of queueing.
    for (int f = 0; f < m_inputDelay; ++f) {
        m_local[f % kRing] = packInput(PlayerInput{});
    }
    m_localHead = m_inputDelay;
    setState(State::Handshake, role == Role::Host ? "waiting for an opponent"
                                                  : "connecting to the host");
}

void Session::stop() {
    if (m_totalFrames + m_frame > 0) {
        // Stalls are the number that matters: they are steps both machines sat
        // still waiting on a packet, and the honest measure of whether the
        // input delay is set high enough for the connection. Per *frame*
        // rather than per second, because that ratio is what does not depend
        // on how long the soak happened to run.
        const long long frames = static_cast<long long>(m_totalFrames + m_frame);
        const long long stalls = static_cast<long long>(m_totalStalls + m_stalls);
        std::fprintf(stderr,
                     "net: %lld frames simulated, %lld stalled steps (%.2f per frame), "
                     "delay %d%s, rtt %.0f ms +/- %.0f\n",
                     frames, stalls,
                     frames > 0 ? static_cast<double>(stalls) / static_cast<double>(frames)
                                : 0.0,
                     m_inputDelay, m_adaptDelay ? " (adapted)" : " (pinned)", m_rttMs,
                     m_rttVarMs);
        // Stalling more than about a quarter of the time means the link needs
        // more buffer than it was given, and the symptom — a match that runs
        // in slow motion — does not look like a network problem from inside
        // the game, so it is worth naming.
        if (frames > 0 && stalls * 4 > frames) {
            std::fprintf(stderr,
                         "net: that connection wanted more input delay than %d "
                         "frames; --delay trades input lag for fewer stalls\n",
                         m_inputDelay);
        }
    }
    *this = Session{};
}

void Session::setState(State s, const char* why) {
    m_state = s;
    std::snprintf(m_status, sizeof m_status, "%s", why);
}

void Session::pump(float dt) {
    if (m_state == State::Idle || m_state == State::Desynced || m_state == State::Lost) {
        return;
    }
    m_clockMs += dt * 1000.0f;
    receiveAll();

    // A worse link is answered at once; a better one waits for the next match,
    // since lowering the delay would mean unsending scheduled frames.
    if (m_adaptDelay && m_state == State::Running) {
        widenDelay(targetDelay());
    }

    // A host still in the handshake is *advertising*, not waiting on anyone in
    // particular, so it waits as long as the player leaves it up — timing that
    // out would mean a lobby that quietly dies while you walk to the other
    // machine. Everyone else has someone specific they expect to hear from: a
    // client is talking to an address that may be wrong, and a running match
    // has a peer that was there a moment ago.
    m_sinceHeard += dt;
    const bool expectingSomeone =
        m_state == State::Running || m_role == Role::Client;
    if (expectingSomeone && m_sinceHeard > kTimeout) {
        setState(State::Lost, m_state == State::Running
                                  ? "the opponent stopped responding - Esc to leave"
                                  : "could not reach the host - Esc to leave");
        return;
    }

    m_sinceHandshake += dt;
    if (m_state == State::Handshake && m_sinceHandshake >= kHandshakeGap) {
        m_sinceHandshake = 0.0f;
        sendHandshake();
    }
    // Inputs go out every frame regardless of whether the sim advanced: during
    // a stall, our inputs are exactly what the peer is waiting for.
    if (m_state == State::Running) {
        sendInputs();
    }
}

void Session::sendHandshake() {
    if (m_role != Role::Client) {
        return; // the host answers Hello; it does not solicit
    }
    unsigned char buf[kMaxPacket];
    Writer w{buf, kMaxPacket};
    w.u8(static_cast<std::uint8_t>(Msg::Hello));
    w.u32(kProtocol);
    w.i32(m_setup.chars[1]); // the fighter this client chose for itself
    w.i32(m_setup.weapons[1]);
    m_transport->send(buf, w.n);
}

void Session::sendInputs() {
    unsigned char buf[kMaxPacket];
    Writer w{buf, kMaxPacket};
    w.u8(static_cast<std::uint8_t>(Msg::Inputs));
    w.u32(m_match);
    w.u8(static_cast<std::uint8_t>(static_cast<std::uint8_t>(m_intent) |
                                   (m_loadoutReady ? 0x04u : 0x00u)));
    // Our half of the next match, on every packet. Twelve bytes to avoid a
    // negotiation: the peer always holds our current pick, so a reselect needs
    // no more handshaking than the intent flag does.
    const int mine = localSlot();
    w.i32(m_setup.chars[mine]);
    w.i32(m_setup.weapons[mine]);
    w.i32(m_setup.level);
    // Our clock, and the newest of theirs we have seen. They echo ours back
    // the same way, so each side can measure a round trip against its own
    // clock alone — no synchronisation, no agreement on what time it is.
    w.u32(static_cast<std::uint32_t>(m_clockMs));
    w.u32(m_peerStamp);
    // The newest kInputRedundancy scheduled frames, oldest first. Repeating
    // them is the entire loss-recovery scheme.
    std::int64_t last = m_localHead - 1;
    // Never narrower than the input delay — see kInputRedundancy. At startup
    // that is the difference between sending frame 0 and deadlocking on it.
    const int window = m_inputDelay > kInputRedundancy ? m_inputDelay : kInputRedundancy;
    std::int64_t first = last - (window - 1);
    if (first < 0) {
        first = 0;
    }
    w.u32(static_cast<std::uint32_t>(first));
    w.u8(static_cast<std::uint8_t>(last - first + 1));
    for (std::int64_t f = first; f <= last; ++f) {
        w.u32(m_local[f % kRing]);
    }
    // The last frame we simulated and what the sim looked like after it. The
    // peer compares against its own and shouts if they differ.
    std::int64_t confirmed = m_frame - 1;
    if (confirmed >= 0 && m_sumsHas[confirmed % kRing]) {
        w.u32(static_cast<std::uint32_t>(confirmed));
        w.u32(m_sums[confirmed % kRing]);
    } else {
        w.u32(0xffffffffu); // nothing simulated yet
        w.u32(0);
    }
    m_transport->send(buf, w.n);
}

void Session::receiveAll() {
    unsigned char buf[kMaxPacket];
    for (;;) {
        int got = m_transport->receive(buf, kMaxPacket);
        if (got <= 0) {
            break;
        }
        Reader r{buf, got};
        Msg kind = static_cast<Msg>(r.u8());

        if (kind == Msg::Hello) {
            std::uint32_t version = r.u32();
            std::int32_t theirChar = r.i32();
            std::int32_t theirWeapon = r.i32();
            if (!r.ok) {
                continue;
            }
            if (version != kProtocol) {
                setState(State::Lost, "the opponent is running a different build - Esc to leave");
                return;
            }
            m_sinceHeard = 0.0f;
            // Their half of the match. Taken on trust here and range-checked
            // by the caller before it reaches a roster lookup — the session
            // does not know how long the roster is.
            m_setup.chars[1] = theirChar;
            m_setup.weapons[1] = theirWeapon;
            // Answer every Hello, not just the first: the client repeats until
            // it hears back, so a lost Welcome has to be answerable again.
            unsigned char out[kMaxPacket];
            Writer w{out, kMaxPacket};
            w.u8(static_cast<std::uint8_t>(Msg::Welcome));
            w.u32(kProtocol);
            for (int i = 0; i < 2; ++i) {
                w.i32(m_setup.chars[i]);
                w.i32(m_setup.weapons[i]);
            }
            w.i32(m_setup.level);
            m_transport->send(out, w.n);
            if (m_state == State::Handshake) {
                setState(State::Running, "opponent connected");
            }
            continue;
        }

        if (kind == Msg::Welcome) {
            std::uint32_t version = r.u32();
            MatchSetup setup;
            for (int i = 0; i < 2; ++i) {
                setup.chars[i] = r.i32();
                setup.weapons[i] = r.i32();
            }
            setup.level = r.i32();
            if (!r.ok) {
                continue;
            }
            if (version != kProtocol) {
                setState(State::Lost, "the host is running a different build - Esc to leave");
                return;
            }
            m_sinceHeard = 0.0f;
            if (m_state == State::Handshake) {
                m_setup = setup;
                setState(State::Running, "connected to the host");
            }
            continue;
        }

        if (kind != Msg::Inputs) {
            continue;
        }

        std::uint32_t theirMatch = r.u32();
        std::uint8_t flags = r.u8();
        std::int32_t theirChar = r.i32();
        std::int32_t theirWeapon = r.i32();
        std::int32_t theirLevel = r.i32();
        std::uint32_t theirStamp = r.u32();
        std::uint32_t echoed = r.u32();
        std::int64_t first = static_cast<std::int64_t>(r.u32());
        int count = r.u8();
        std::uint32_t bits[kMaxInputsPerPacket] = {};
        if (count > kMaxInputsPerPacket) {
            continue; // not something this build would have sent
        }
        for (int i = 0; i < count; ++i) {
            bits[i] = r.u32();
        }
        std::int64_t confirmFrame = static_cast<std::int64_t>(r.u32());
        std::uint32_t confirmSum = r.u32();
        if (!r.ok) {
            continue; // truncated; the next packet repeats all of it anyway
        }
        m_sinceHeard = 0.0f; // alive, whichever match they are on
        m_peerStamp = theirStamp;
        // Kept even from a packet of a match we have left: it is their pick for
        // the *next* one, and it is the thing we need if they roll first.
        m_peerChar = theirChar;
        m_peerWeapon = theirWeapon;
        m_peerLevel = theirLevel;

        // Round trip: our own clock, minus the value of it they sent back.
        // Smoothed the RTP way — a running mean plus a running mean deviation
        // — because a single late packet should not move the delay, but a
        // consistently worse link should.
        if (echoed != kNoStamp) {
            float sample = m_clockMs - static_cast<float>(echoed);
            if (sample >= 0.0f && sample < 10000.0f) {
                if (m_rttMs <= 0.0f) {
                    m_rttMs = sample;
                    m_rttVarMs = sample * 0.5f;
                } else {
                    float diff = sample - m_rttMs;
                    m_rttMs += 0.10f * diff;
                    m_rttVarMs += 0.10f * (std::fabs(diff) - m_rttVarMs);
                }
            }
        }

        if (theirMatch == m_match + 1) {
            // They have already restarted, which they only do once they have
            // seen our own request — so the rematch is agreed even if their
            // last flag never arrived. Their inputs belong to the next match;
            // they will still be repeating them by the time we get there.
            m_peerAhead = true;
            continue;
        }
        if (theirMatch != m_match) {
            continue; // a match we have already left; nothing here applies
        }
        const std::uint8_t rawIntent = flags & 0x03u;
        m_peerIntent = rawIntent <= static_cast<std::uint8_t>(Intent::Reselect)
                           ? static_cast<Intent>(rawIntent)
                           : Intent::None;
        m_peerLoadoutReady = (flags & 0x04u) != 0;

        // Deliberately no "promote to Running on Inputs" shortcut here: the
        // host cannot start without the client's fighter, which only arrives
        // on Hello. It cannot miss one either — a client only sends inputs
        // after a Welcome, and a Welcome only follows a Hello the host got.

        for (int i = 0; i < count; ++i) {
            std::int64_t f = first + i;
            // Ignore anything already played or absurdly far ahead: a stale or
            // malformed packet must not be able to scribble on the ring.
            if (f < m_frame || f >= m_frame + kRing / 2) {
                continue;
            }
            m_remote[f % kRing] = bits[i];
            m_remoteHas[f % kRing] = true;
        }

        // Desync check. Only meaningful for a frame we have also simulated and
        // that is still inside the ring.
        if (confirmFrame >= 0 && confirmFrame < m_frame &&
            confirmFrame > m_frame - kRing && m_sumsHas[confirmFrame % kRing] &&
            m_sums[confirmFrame % kRing] != confirmSum) {
            char why[96];
            std::snprintf(why, sizeof why, "desynced at frame %lld - Esc to leave",
                          static_cast<long long>(confirmFrame));
            std::fprintf(stderr,
                         "\nnet: DESYNC at frame %lld — ours %08x, theirs %08x\n"
                         "The two machines no longer agree. Record the match with "
                         "--record and replay it on both to find where.\n",
                         static_cast<long long>(confirmFrame),
                         m_sums[confirmFrame % kRing], confirmSum);
            setState(State::Desynced, why);
            return;
        }
    }
}

bool Session::nextStep(const PlayerInput& local, PlayerInput out[2]) {
    if (m_state != State::Running) {
        return false;
    }
    if (!m_remoteHas[m_frame % kRing]) {
        ++m_stalls;
        m_stallRate += 0.02f * (1.0f - m_stallRate);
        if (!m_stalling) {
            m_stalling = true;
            setState(State::Running, "waiting for the opponent");
        }
        return false;
    }

    m_stallRate += 0.02f * (0.0f - m_stallRate);
    const int mine = localSlot();
    out[mine] = unpackInput(m_local[m_frame % kRing]);
    out[1 - mine] = unpackInput(m_remote[m_frame % kRing]);

    // This frame's sample is scheduled m_inputDelay frames out — m_localHead is
    // always m_frame + m_inputDelay, which is what buys the network its time.
    m_local[m_localHead % kRing] = packInput(local);
    ++m_localHead;

    // Release the slot we just consumed so the same index cannot read as
    // "already have it" when the ring wraps around to it kRing frames later.
    m_remoteHas[m_frame % kRing] = false;
    ++m_frame;
    if (m_stalling) {
        m_stalling = false;
        setState(State::Running, "connected");
    }
    return true;
}

void Session::submitLoadout(int character, int weapon, int level, bool ownsLevel) {
    const int mine = localSlot();
    m_setup.chars[mine] = character;
    m_setup.weapons[mine] = weapon;
    m_ownsLevel = ownsLevel;
    if (ownsLevel) {
        m_setup.level = level;
    }
    m_loadoutReady = true;
}

int Session::targetDelay() const {
    if (m_rttMs <= 0.0f) {
        return kDefaultInputDelay; // nothing measured yet
    }
    // Cover the round trip plus a couple of deviations, round up, and add one
    // frame of slack. No more than that: the measurement already *includes*
    // the send granularity, because the echo travels back through the peer's
    // render loop — on loopback that alone reads as ~10 ms of round trip. An
    // extra margin on top would be double-counting, and every frame of it is
    // 8.3 ms of input lag charged to the player. These constants reproduce the
    // delays that measured clean in the table in CLAUDE.md.
    const float budgetMs = m_rttMs + 2.0f * m_rttVarMs;
    const float frameMs = m_tickSeconds * 1000.0f;
    int frames = static_cast<int>(budgetMs / frameMs) + 1 + 1;
    if (frames < kDefaultInputDelay) frames = kDefaultInputDelay;
    if (frames > kMaxInputDelay) frames = kMaxInputDelay;
    return frames;
}

void Session::widenDelay(int frames) {
    if (frames <= m_inputDelay || m_localHead <= 0) {
        return;
    }
    if (frames > kMaxInputDelay) {
        frames = kMaxInputDelay;
    }
    // Fill the new gap by repeating the newest scheduled input, which is what
    // the player was doing anyway — the alternative, leaving holes, is the one
    // thing the ring must never have.
    const std::uint32_t held = m_local[(m_localHead - 1) % kRing];
    while (m_localHead < m_frame + frames) {
        m_local[m_localHead % kRing] = held;
        ++m_localHead;
    }
    std::fprintf(stderr, "net: input delay %d -> %d frames (rtt %.0f ms +/- %.0f)\n",
                 m_inputDelay, frames, m_rttMs, m_rttVarMs);
    m_inputDelay = frames;
}

void Session::beginRematch() {
    // Everything about *this* match resets; everything about the connection —
    // transport, role, agreed setup, state — is kept. The match index moving
    // is what makes packets still arriving from the finished match harmless.
    ++m_match;
    m_totalFrames += m_frame;
    m_totalStalls += m_stalls;
    if (m_adaptDelay) {
        // The one point the delay may also come *down*: nothing is scheduled
        // across a match boundary, so there is nothing to unsend.
        m_inputDelay = targetDelay();
    }
    m_frame = 0;
    m_stalls = 0;
    m_stalling = false;
    // Fold in whatever the peer last told us they were bringing. After a plain
    // rematch this is unchanged; after a reselect it is their new fighter.
    const int theirs = 1 - localSlot();
    m_setup.chars[theirs] = m_peerChar;
    m_setup.weapons[theirs] = m_peerWeapon;
    if (!m_ownsLevel) {
        m_setup.level = m_peerLevel; // theirs was the pick that counted
    }
    m_intent = Intent::None;
    m_peerIntent = Intent::None;
    m_peerAhead = false;
    m_loadoutReady = false;
    m_peerLoadoutReady = false;
    for (int i = 0; i < kRing; ++i) {
        m_remoteHas[i] = false;
        m_sumsHas[i] = false;
    }
    for (int f = 0; f < m_inputDelay; ++f) {
        m_local[f % kRing] = packInput(PlayerInput{});
    }
    m_localHead = m_inputDelay;
    setState(State::Running, "connected");
    std::fprintf(stderr, "net: rematch - starting match %u\n", m_match);
}

void Session::stepped(std::uint32_t checksum) {
    std::int64_t f = m_frame - 1;
    if (f < 0) {
        return;
    }
    m_sums[f % kRing] = checksum;
    m_sumsHas[f % kRing] = true;
}
