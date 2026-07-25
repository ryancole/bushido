#include "netplay/session.hpp"

#include <cstdio>
#include <cstring>

namespace {

// Bumped whenever the packet layout or the meaning of a field changes. Two
// builds that disagree here refuse each other at the handshake rather than
// desyncing ten seconds in, where the cause would be much harder to see.
constexpr std::uint32_t kProtocol = 1;

enum class Msg : std::uint8_t {
    Hello = 0,   // client -> host: I would like a match
    Welcome = 1, // host -> client: here is the match
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

void Session::start(Transport* transport, Role role, const MatchSetup& setup) {
    *this = Session{};
    m_transport = transport;
    m_role = role;
    m_setup = setup;

    // Both peers seed the first kInputDelay frames with neutral input: nobody
    // has pressed anything yet, and it means each side has something to send
    // the moment it comes up, so the handshake and the first inputs overlap
    // instead of queueing.
    for (int f = 0; f < kInputDelay; ++f) {
        m_local[f % kRing] = packInput(PlayerInput{});
    }
    m_localHead = kInputDelay;
    setState(State::Handshake, role == Role::Host ? "waiting for an opponent"
                                                  : "connecting to the host");
}

void Session::stop() {
    if (m_frame > 0) {
        // Stalls are the number that matters: they are frames both machines
        // sat still waiting on a packet, and the honest measure of whether
        // kInputDelay is set high enough for the connection.
        std::fprintf(stderr, "net: %lld frames simulated, %lld stalled steps\n",
                     static_cast<long long>(m_frame), static_cast<long long>(m_stalls));
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
    receiveAll();

    m_sinceHeard += dt;
    if (m_sinceHeard > kTimeout) {
        setState(State::Lost, "the opponent stopped responding");
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
    m_transport->send(buf, w.n);
}

void Session::sendInputs() {
    unsigned char buf[kMaxPacket];
    Writer w{buf, kMaxPacket};
    w.u8(static_cast<std::uint8_t>(Msg::Inputs));
    // The newest kInputRedundancy scheduled frames, oldest first. Repeating
    // them is the entire loss-recovery scheme.
    std::int64_t last = m_localHead - 1;
    std::int64_t first = last - (kInputRedundancy - 1);
    if (first < 0) {
        first = 0;
    }
    w.u32(static_cast<std::uint32_t>(first));
    w.u8(static_cast<std::uint8_t>(last - first + 1));
    for (std::int64_t f = first; f <= last; ++f) {
        w.u16(m_local[f % kRing]);
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
            if (!r.ok) {
                continue;
            }
            if (version != kProtocol) {
                setState(State::Lost, "the opponent is running a different build");
                return;
            }
            m_sinceHeard = 0.0f;
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
                setState(State::Lost, "the host is running a different build");
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

        std::int64_t first = static_cast<std::int64_t>(r.u32());
        int count = r.u8();
        std::uint16_t bits[kInputRedundancy] = {};
        if (count > kInputRedundancy) {
            continue; // not something this build would have sent
        }
        for (int i = 0; i < count; ++i) {
            bits[i] = r.u16();
        }
        std::int64_t confirmFrame = static_cast<std::int64_t>(r.u32());
        std::uint32_t confirmSum = r.u32();
        if (!r.ok) {
            continue; // truncated; the next packet repeats all of it anyway
        }
        m_sinceHeard = 0.0f;
        // A host can be handed inputs before it ever sees a Hello if the
        // Welcome was lost — treat that as connected too.
        if (m_state == State::Handshake && m_role == Role::Host) {
            setState(State::Running, "opponent connected");
        }

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
            std::snprintf(why, sizeof why, "desynced at frame %lld",
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
        if (!m_stalling) {
            m_stalling = true;
            setState(State::Running, "waiting for the opponent");
        }
        return false;
    }

    const int mine = localSlot();
    out[mine] = unpackInput(m_local[m_frame % kRing]);
    out[1 - mine] = unpackInput(m_remote[m_frame % kRing]);

    // This frame's sample is scheduled kInputDelay frames out — m_localHead is
    // always m_frame + kInputDelay, which is what buys the network its time.
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

void Session::stepped(std::uint32_t checksum) {
    std::int64_t f = m_frame - 1;
    if (f < 0) {
        return;
    }
    m_sums[f % kRing] = checksum;
    m_sumsHas[f % kRing] = true;
}
