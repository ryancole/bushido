#pragma once

#include "input.hpp" // PlayerInput, packInput/unpackInput

#include <cstdint>
#include <filesystem>
#include <fstream>

// Deterministic replay: the harness every other piece of netplay rests on.
//
// Lockstep and rollback both assume two machines fed the same inputs reach the
// same state. That assumption is either true or the whole thing is worthless,
// and the only way to know which is to check. A recording is a match's setup
// plus, per fixed step, both fighters' inputs and the checksum the sim had
// after them; replaying feeds those inputs back and compares every step. Run
// the log back on the machine that made it and you have caught anything
// frame-order or uninitialised; run it on a second machine and you have caught
// the rest — thread counts, instruction sets, compiler differences.
//
// A log is only meaningful against the build that wrote it: any change to the
// sim moves the checksums legitimately. Bump kReplayVersion when the *file*
// layout changes; a stale log against a changed sim just reports a desync at
// frame 0, which is the right amount of alarming.

// What a match is, in full: the same five integers Game's ctor takes. Two
// netplay peers would agree on exactly this before the first step.
struct MatchSetup {
    std::int32_t chars[2] = {0, 1};
    std::int32_t weapons[2] = {0, 0};
    std::int32_t level = 0;
};

inline constexpr std::uint32_t kReplayVersion = 1;

// Where recordings live, relative to the working directory.
inline constexpr const char* kReplayDir = "replays";

// Resolves a recording's name to the file it means. A bare name goes under
// kReplayDir; a name that already carries a directory — or is absolute — is
// honoured exactly as given, because somebody who typed a path meant it.
//
// Recorder and Replayer both go through this, and that is the whole point:
// whatever --record writes, --replay has to find, so the rule exists once. It
// is also why the documented `--record run.bsdr` / `--replay run.bsdr` pair
// still reads the same — both ends move together.
std::filesystem::path replayPath(const char* name);

// Writes a recording. Failing to open one is never fatal — a match should not
// die because a debug log could not be created — so a failed open logs and
// leaves the recorder inert, with active() false and step() a no-op.
class Recorder {
public:
    Recorder() = default;
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool open(const char* path, const MatchSetup& setup);
    void step(std::uint16_t inputA, std::uint16_t inputB, std::uint32_t checksum);
    void close(); // logs the frame count; also called by the destructor
    bool active() const { return m_out.is_open(); }

private:
    std::ofstream m_out;
    std::int64_t m_frames = 0;
};

// Reads one back. The caller builds a Game from setup(), then pulls one step
// at a time and compares its own checksum against the recorded one.
class Replayer {
public:
    Replayer() = default;
    ~Replayer();
    Replayer(const Replayer&) = delete;
    Replayer& operator=(const Replayer&) = delete;

    bool open(const char* path);
    void close();
    bool active() const { return m_in.is_open(); }
    const MatchSetup& setup() const { return m_setup; }

    // Next step's recorded inputs and the checksum the recording had *after*
    // applying them. False once the log runs out.
    bool next(PlayerInput inputs[2], std::uint32_t& checksum);

    std::int64_t frame() const { return m_frame; } // steps pulled so far

private:
    std::ifstream m_in;
    MatchSetup m_setup;
    std::int64_t m_frame = 0;
};

// Prints the frame and both fighters' state field by field to stderr. Called
// on the first checksum mismatch: a bare "desync at frame 4213" leaves you
// bisecting, whereas one look at which field moved usually names the cause.
void dumpDesync(const Game& game, std::int64_t frame, std::uint32_t expected,
                std::uint32_t actual);
