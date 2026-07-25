#include "netplay/replay.hpp"

#include <cstdio>
#include <cstring>

namespace {

// "BSDR" — bushido replay. Little-endian throughout, which is every platform
// the game builds for; a big-endian port would byte-swap here and nowhere else.
constexpr char kMagic[4] = {'B', 'S', 'D', 'R'};

void writeU32(std::ofstream& out, std::uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof v);
}

void readU32(std::ifstream& in, std::uint32_t& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof v);
}

} // namespace

Recorder::~Recorder() { close(); }

bool Recorder::open(const char* path, const MatchSetup& setup) {
    close();
    m_out.open(path, std::ios::binary | std::ios::trunc);
    if (!m_out) {
        std::fprintf(stderr, "replay: could not open '%s' for writing\n", path);
        m_out.close();
        return false;
    }
    m_out.write(kMagic, sizeof kMagic);
    writeU32(m_out, kReplayVersion);
    for (int i = 0; i < 2; ++i) {
        writeU32(m_out, static_cast<std::uint32_t>(setup.chars[i]));
        writeU32(m_out, static_cast<std::uint32_t>(setup.weapons[i]));
    }
    writeU32(m_out, static_cast<std::uint32_t>(setup.level));
    if (!m_out) {
        std::fprintf(stderr, "replay: failed writing the header of '%s'\n", path);
        m_out.close();
        return false;
    }
    m_frames = 0;
    std::fprintf(stderr, "replay: recording to '%s'\n", path);
    return true;
}

void Recorder::step(std::uint16_t inputA, std::uint16_t inputB,
                    std::uint32_t checksum) {
    if (!m_out.is_open()) {
        return;
    }
    // Written per step rather than buffered to the end: a recording of the
    // crash you are chasing is worth more than a tidy one you never get.
    m_out.write(reinterpret_cast<const char*>(&inputA), sizeof inputA);
    m_out.write(reinterpret_cast<const char*>(&inputB), sizeof inputB);
    writeU32(m_out, checksum);
    m_out.flush();
    if (!m_out) {
        std::fprintf(stderr, "replay: write failed at step %lld, recording stopped\n",
                     static_cast<long long>(m_frames));
        m_out.close();
        return;
    }
    ++m_frames;
}

void Recorder::close() {
    if (!m_out.is_open()) {
        return;
    }
    m_out.close();
    std::fprintf(stderr, "replay: recorded %lld steps\n",
                 static_cast<long long>(m_frames));
}

Replayer::~Replayer() { close(); }

bool Replayer::open(const char* path) {
    close();
    m_in.open(path, std::ios::binary);
    if (!m_in) {
        std::fprintf(stderr, "replay: could not open '%s' for reading\n", path);
        m_in.close();
        return false;
    }

    char magic[4] = {};
    std::uint32_t version = 0;
    m_in.read(magic, sizeof magic);
    readU32(m_in, version);
    if (!m_in || std::memcmp(magic, kMagic, sizeof magic) != 0) {
        std::fprintf(stderr, "replay: '%s' is not a replay file\n", path);
        m_in.close();
        return false;
    }
    if (version != kReplayVersion) {
        std::fprintf(stderr, "replay: '%s' is version %u, this build reads %u\n", path,
                     version, kReplayVersion);
        m_in.close();
        return false;
    }

    for (int i = 0; i < 2; ++i) {
        std::uint32_t c = 0;
        std::uint32_t w = 0;
        readU32(m_in, c);
        readU32(m_in, w);
        m_setup.chars[i] = static_cast<std::int32_t>(c);
        m_setup.weapons[i] = static_cast<std::int32_t>(w);
    }
    std::uint32_t level = 0;
    readU32(m_in, level);
    m_setup.level = static_cast<std::int32_t>(level);
    if (!m_in) {
        std::fprintf(stderr, "replay: '%s' ends inside its header\n", path);
        m_in.close();
        return false;
    }
    m_frame = 0;
    return true;
}

bool Replayer::next(PlayerInput inputs[2], std::uint32_t& checksum) {
    if (!m_in.is_open()) {
        return false;
    }
    std::uint16_t bits[2] = {};
    m_in.read(reinterpret_cast<char*>(bits), sizeof bits);
    readU32(m_in, checksum);
    if (!m_in) {
        return false; // end of log (a truncated tail reads the same, and should)
    }
    inputs[0] = unpackInput(bits[0]);
    inputs[1] = unpackInput(bits[1]);
    ++m_frame;
    return true;
}

void Replayer::close() { m_in.close(); }

void dumpDesync(const Game& game, std::int64_t frame, std::uint32_t expected,
                std::uint32_t actual) {
    std::fprintf(stderr, "\n=== DESYNC at step %lld ===\nrecorded %08x, got %08x\n",
                 static_cast<long long>(frame), expected, actual);
    for (int i = 0; i < 2; ++i) {
        const Player& p = game.player(i);
        std::fprintf(stderr,
                     "player %d\n"
                     "  pos      %+.9g %+.9g %+.9g\n"
                     "  vy       %+.9g   grounded %d   facing %+.9g\n"
                     "  anim     phase %+.9g  move %+.9g\n"
                     "  attack   state %d kind %d timer %+.9g t %+.9g landed %d\n"
                     "  windup   scale %+.9g  buffer %+.9g kind %d\n"
                     "  hitstun  %+.9g   block %d   riposte %+.9g\n"
                     "  crouch   %d amount %+.9g\n"
                     "  kbVel    %+.9g %+.9g\n"
                     "  severed  %d%d%d%d%d   blood %+.9g\n"
                     "  topple   tilt %+.9g vel %+.9g side %+.9g\n",
                     i, p.pos.x, p.pos.y, p.pos.z, p.vy, p.grounded ? 1 : 0, p.facing,
                     p.animPhase, p.moveAmount, static_cast<int>(p.attackState),
                     static_cast<int>(p.attackKind), p.attackTimer, p.attackT,
                     p.attackLanded ? 1 : 0, p.attackWindupScale, p.attackBuffer,
                     static_cast<int>(p.bufferedKind), p.hitstun, p.blocking ? 1 : 0,
                     p.riposteTime, p.crouching ? 1 : 0, p.crouchAmount, p.kbVel.x,
                     p.kbVel.y, p.severed[0] ? 1 : 0, p.severed[1] ? 1 : 0,
                     p.severed[2] ? 1 : 0, p.severed[3] ? 1 : 0, p.severed[4] ? 1 : 0,
                     p.blood, p.fallTilt, p.fallVel, p.fallSide);
    }
    std::fprintf(stderr,
                 "pieces %d   winner %d\n"
                 "Compare against the same step in a run that agrees: the first\n"
                 "field that differs is where the divergence entered.\n\n",
                 static_cast<int>(game.severedPieces().size()), game.winner());
}
