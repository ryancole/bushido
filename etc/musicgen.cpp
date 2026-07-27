// musicgen — renders the game's music into the WAV files under assets/music/.
//
// The tracks used to be composed inside audio.cpp and synthesized on every
// startup, which spent close to 300 ms of the loading screen producing the
// same ~69 seconds of PCM every run. The composition still lives in code —
// it's the only sensible way to author this material — but it lives *here*
// now, run by hand when a track changes, and the game just loads the results:
//
//     build\musicgen.exe             (writes into assets/music/)
//     build\musicgen.exe <out-dir>
//
// The WAVs are committed, so an ordinary build never runs this. Everything is
// deterministic (seeded rng, no clock, no threads), so re-rendering unchanged
// tracks with the same toolchain rewrites the same bytes and git sees nothing.
//
// Adding a track: a render function below and a row in kRenderers, plus the
// matching Music enumerator and kMusicFiles name over in src/audio.hpp.

#include "audio.hpp"
#include "synth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

using synth::Bandpass;
using synth::kPi;
using synth::noise;
using synth::normalizePeak;

// Every file is rendered at this rate; miniaudio resamples on the way out if
// the player's device runs at another. (Composing straight at the device rate
// was the one thing a file on disk gives up, and it isn't audible.)
constexpr float kRate = 48000.0f;

// Peak the tracks are normalized to. Deliberately quiet — it's the headroom
// the mixer was tuned against, and Audio's music volume of 1 means "as
// rendered", so this number *is* the game's music level. Don't raise it here
// expecting a louder game; raise it and every level moves with it.
constexpr float kMusicPeak = 0.30f;

// ---- voices --------------------------------------------------------------
// Each track is a fixed count of beats rendered into one buffer that loops
// forever. Every note writes additively with wraparound, so a tail ringing
// past the loop's end lands at its start and the seam is inaudible. All
// voices are deterministic (seeded rng), same as the SFX.

void addWrapped(std::vector<float>& pcm, std::size_t start, std::size_t i, float s) {
    pcm[(start + i) % pcm.size()] += s;
}

// Terminal fade over a note's last eighth so a truncated tail can't click.
float tailFade(std::size_t i, std::size_t frames) {
    std::size_t fadeStart = frames - frames / 8;
    if (i < fadeStart) return 1.0f;
    return static_cast<float>(frames - i) / static_cast<float>(frames - fadeStart);
}

// Karplus-Strong pluck — the koto/shamisen voice. damp sets the string's
// sustain (~0.992 dry twang .. ~0.997 long ring); higher notes decay faster
// naturally, like a real string.
void addPluck(std::vector<float>& pcm, float rate, float timeSec, float freq,
              float amp, float damp, std::uint32_t& rng) {
    int period = std::max(2, static_cast<int>(rate / freq));
    std::vector<float> line(static_cast<std::size_t>(period));
    float mean = 0.0f;
    for (float& s : line) {
        s = noise(rng);
        mean += s;
    }
    mean /= static_cast<float>(period);
    for (float& s : line) s -= mean; // no DC thump on the attack
    std::size_t start = static_cast<std::size_t>(timeSec * rate);
    std::size_t frames = static_cast<std::size_t>(rate * 2.5f);
    int idx = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        float cur = line[static_cast<std::size_t>(idx)];
        int nxt = idx + 1 == period ? 0 : idx + 1;
        line[static_cast<std::size_t>(idx)] =
            damp * 0.5f * (cur + line[static_cast<std::size_t>(nxt)]);
        idx = nxt;
        addWrapped(pcm, start, i, amp * cur * tailFade(i, frames));
    }
}

// Taiko: the hit synth's cousin tuned as an instrument — pitch-dropping sine
// body and a soft skin slap, no crack.
void addTaiko(std::vector<float>& pcm, float rate, float timeSec, float baseFreq,
              float amp, std::uint32_t& rng) {
    std::size_t start = static_cast<std::size_t>(timeSec * rate);
    std::size_t frames = static_cast<std::size_t>(rate * 0.4f);
    float phase = 0.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        float t = static_cast<float>(i) / rate;
        float freq = baseFreq * (1.0f + 1.6f * std::exp(-t * 35.0f));
        phase += 2.0f * kPi * freq / rate;
        float body = std::sin(phase) * std::exp(-t * 10.0f);
        float slap = noise(rng) * std::exp(-t * 90.0f);
        addWrapped(pcm, start, i, amp * (body + 0.3f * slap) * tailFade(i, frames));
    }
}

// Shime/rim stick: a short bright bandpassed tick for the offbeat "ka".
void addStick(std::vector<float>& pcm, float rate, float timeSec, float amp,
              std::uint32_t& rng) {
    std::size_t start = static_cast<std::size_t>(timeSec * rate);
    std::size_t frames = static_cast<std::size_t>(rate * 0.09f);
    Bandpass bp;
    for (std::size_t i = 0; i < frames; ++i) {
        float t = static_cast<float>(i) / rate;
        float s = bp.process(noise(rng), 2300.0f, 0.6f, rate) * std::exp(-t * 70.0f);
        addWrapped(pcm, start, i, amp * s * tailFade(i, frames));
    }
}

// Struck bell: detuned inharmonic partials each decaying at its own rate —
// a deep temple bell at low freq, a glint of light at high. decay is the
// fundamental's per-second exponential rate (smaller = longer ring).
void addBell(std::vector<float>& pcm, float rate, float timeSec, float freq,
             float amp, float decay) {
    constexpr float kRatios[4] = {1.0f, 2.01f, 2.74f, 4.52f};
    std::size_t start = static_cast<std::size_t>(timeSec * rate);
    std::size_t frames = static_cast<std::size_t>(rate * std::min(8.0f, 7.0f / decay));
    float phase[4] = {};
    for (std::size_t i = 0; i < frames; ++i) {
        float t = static_cast<float>(i) / rate;
        float s = 0.0f;
        for (int k = 0; k < 4; ++k) {
            phase[k] += 2.0f * kPi * freq * kRatios[k] / rate;
            s += std::sin(phase[k]) * std::exp(-t * decay * (1.0f + 0.8f * k)) /
                 (1.0f + 0.6f * k);
        }
        addWrapped(pcm, start, i, amp * s * tailFade(i, frames));
    }
}

// Breath flute (shakuhachi-ish): sine + weak second harmonic with vibrato
// that grows into the note, a bandpassed breath layer, half-sine swell.
void addFlute(std::vector<float>& pcm, float rate, float timeSec, float freq,
              float amp, float durSec, std::uint32_t& rng) {
    std::size_t start = static_cast<std::size_t>(timeSec * rate);
    std::size_t frames = static_cast<std::size_t>(durSec * rate);
    Bandpass bp;
    float phase = 0.0f;
    for (std::size_t i = 0; i < frames; ++i) {
        float t = static_cast<float>(i) / rate;
        float u = static_cast<float>(i) / static_cast<float>(frames);
        float env = std::pow(std::sin(kPi * u), 0.8f);
        float vib = 1.0f + 0.013f * std::sin(2.0f * kPi * 5.3f * t) *
                               std::min(1.0f, t * 1.5f);
        phase += 2.0f * kPi * freq * vib / rate;
        float tone = std::sin(phase) + 0.28f * std::sin(2.0f * phase);
        float breath = bp.process(noise(rng), freq * 2.2f, 1.0f, rate);
        addWrapped(pcm, start, i, amp * env * (tone + 0.3f * breath));
    }
}

// ---- tracks --------------------------------------------------------------

// Menu — solo koto in the in scale on E (E F A B C) over a temple bell:
// the stillness before a duel. 8 bars of 4 at 72 BPM.
std::vector<float> renderMenu(float rate) {
    constexpr float kSpb = 60.0f / 72.0f;
    std::vector<float> pcm(static_cast<std::size_t>(rate * kSpb * 32.0f), 0.0f);
    std::uint32_t rng = 0x3e17a1e5u;
    auto pluck = [&](float beat, float freq, float amp, float damp = 0.9965f) {
        addPluck(pcm, rate, beat * kSpb, freq, amp, damp, rng);
    };
    // E2 82.41  B2 123.47  E3 164.81  F3 174.61  B3 246.94  C4 261.63
    // E4 329.63 F4 349.23  A4 440.00  B4 493.88  C5 523.25  E5 659.26
    addBell(pcm, rate, 0.0f * kSpb, 82.41f, 0.45f, 1.1f);
    addBell(pcm, rate, 16.0f * kSpb, 82.41f, 0.35f, 1.1f);
    // Low strings ground each half-phrase; B pulls the loop home to E.
    pluck(0.0f, 164.81f, 0.45f, 0.997f);
    pluck(8.0f, 164.81f, 0.40f, 0.997f);
    pluck(16.0f, 164.81f, 0.42f, 0.997f);
    pluck(20.0f, 220.00f, 0.34f, 0.997f);
    pluck(28.0f, 123.47f, 0.40f, 0.997f);
    // The melody: rise, hang, and settle back — unhurried.
    pluck(0.0f, 329.63f, 0.42f);
    pluck(1.5f, 349.23f, 0.30f);
    pluck(2.0f, 440.00f, 0.38f);
    pluck(4.0f, 493.88f, 0.40f);
    pluck(5.5f, 523.25f, 0.32f);
    pluck(6.0f, 493.88f, 0.30f);
    pluck(7.0f, 440.00f, 0.34f);
    pluck(8.0f, 659.26f, 0.36f);
    pluck(10.0f, 523.25f, 0.30f);
    pluck(11.0f, 493.88f, 0.28f);
    pluck(12.0f, 440.00f, 0.34f);
    pluck(13.5f, 349.23f, 0.26f);
    pluck(14.0f, 329.63f, 0.34f);
    pluck(16.0f, 329.63f, 0.40f);
    pluck(17.5f, 349.23f, 0.28f);
    pluck(18.0f, 440.00f, 0.36f);
    pluck(20.0f, 523.25f, 0.38f);
    pluck(21.5f, 493.88f, 0.30f);
    pluck(22.0f, 440.00f, 0.28f);
    pluck(23.0f, 349.23f, 0.26f);
    pluck(24.0f, 329.63f, 0.38f);
    pluck(26.0f, 246.94f, 0.30f);
    pluck(27.0f, 261.63f, 0.26f);
    pluck(28.0f, 246.94f, 0.32f);
    // A distant flute doubles the high turn, then breathes the phrase out.
    addFlute(pcm, rate, 8.0f * kSpb, 659.26f, 0.09f, 2.8f, rng);
    addFlute(pcm, rate, 24.0f * kSpb, 440.00f, 0.08f, 3.2f, rng);
    normalizePeak(pcm, kMusicPeak);
    return pcm;
}

// Dojo — taiko discipline: a drum line under a tense E–F shamisen ostinato,
// sparse dark accents above. 8 bars of 4 at 100 BPM.
std::vector<float> renderDojo(float rate) {
    constexpr float kSpb = 60.0f / 100.0f;
    std::vector<float> pcm(static_cast<std::size_t>(rate * kSpb * 32.0f), 0.0f);
    std::uint32_t rng = 0xd0704a1du;
    auto taiko = [&](float beat, float amp) {
        addTaiko(pcm, rate, beat * kSpb, 75.0f, amp, rng);
    };
    auto stick = [&](float beat, float amp) {
        addStick(pcm, rate, beat * kSpb, amp, rng);
    };
    auto pluck = [&](float beat, float freq, float amp, float damp = 0.992f) {
        addPluck(pcm, rate, beat * kSpb, freq, amp, damp, rng);
    };
    addBell(pcm, rate, 0.0f, 110.0f, 0.35f, 0.9f);
    // Don doko don — the pattern breathes over two bars, with a pickup fill
    // in the last bar that rolls the loop back to the downbeat.
    constexpr struct { float beat, amp; } kDrums[] = {
        {0.0f, 0.55f},  {1.5f, 0.30f},  {2.0f, 0.45f},  {4.0f, 0.55f},
        {5.5f, 0.30f},  {6.0f, 0.45f},  {7.5f, 0.28f},  {8.0f, 0.55f},
        {9.5f, 0.30f},  {10.0f, 0.45f}, {12.0f, 0.55f}, {13.5f, 0.28f},
        {14.0f, 0.45f}, {15.0f, 0.30f}, {15.5f, 0.30f}, {16.0f, 0.55f},
        {17.5f, 0.30f}, {18.0f, 0.45f}, {20.0f, 0.55f}, {21.5f, 0.30f},
        {22.0f, 0.45f}, {23.5f, 0.28f}, {24.0f, 0.55f}, {25.5f, 0.30f},
        {26.0f, 0.45f}, {28.0f, 0.55f}, {29.0f, 0.30f}, {29.5f, 0.30f},
        {30.0f, 0.45f}, {31.0f, 0.30f}, {31.5f, 0.25f},
    };
    for (const auto& d : kDrums) taiko(d.beat, d.amp);
    for (float b = 3.0f; b < 32.0f; b += 4.0f) stick(b, 0.18f);
    stick(15.5f, 0.14f);
    stick(31.5f, 0.14f);
    // E3 164.81 F3 174.61 — the half-step growl that says "danger".
    for (float b = 0.0f; b < 32.0f; b += 4.0f) {
        pluck(b, 164.81f, 0.38f);
        pluck(b + 2.0f, 164.81f, 0.28f);
        pluck(b + 3.5f, 174.61f, 0.32f);
    }
    // A cold answer up high, twice a loop; a lone flute cry near the end.
    pluck(8.0f, 493.88f, 0.30f, 0.995f);
    pluck(10.0f, 523.25f, 0.28f, 0.995f);
    pluck(11.0f, 493.88f, 0.24f, 0.995f);
    pluck(12.0f, 440.00f, 0.30f, 0.995f);
    pluck(24.0f, 329.63f, 0.28f, 0.995f);
    pluck(26.0f, 349.23f, 0.26f, 0.995f);
    pluck(27.0f, 329.63f, 0.26f, 0.995f);
    addFlute(pcm, rate, 24.0f * kSpb, 493.88f, 0.07f, 2.4f, rng);
    normalizePeak(pcm, kMusicPeak);
    return pcm;
}

// Hanami — koto arpeggios in the yo scale on D (D E G A B), bell glints like
// light on the stream, a flute drifting over. 8 bars of 4 at 84 BPM.
std::vector<float> renderHanami(float rate) {
    constexpr float kSpb = 60.0f / 84.0f;
    std::vector<float> pcm(static_cast<std::size_t>(rate * kSpb * 32.0f), 0.0f);
    std::uint32_t rng = 0x4a9a2f10u;
    // D4 E4 G4 A4 B4 D5 E5 G5, indexed by the figures below (-1 = rest).
    constexpr float kYo[8] = {293.66f, 329.63f, 392.00f, 440.00f,
                              493.88f, 587.33f, 659.26f, 784.00f};
    constexpr int kFigure[8][8] = {
        {0, 2, 3, 5, 4, 3, 2, 1},  {0, 2, 3, 4, 3, 2, 1, 2},
        {1, 3, 4, 6, 5, 4, 3, 2},  {0, 2, 3, 4, 5, 4, 2, 1},
        {0, 2, 3, 5, 4, 3, 2, 1},  {2, 4, 5, 7, 6, 5, 4, 3},
        {1, 3, 4, 5, 4, 3, 2, 1},  {0, 1, 2, 1, 0, 1, 2, -1},
    };
    constexpr float kArpAmp[8] = {0.26f, 0.16f, 0.20f, 0.16f,
                                  0.24f, 0.16f, 0.20f, 0.15f};
    for (int bar = 0; bar < 8; ++bar) {
        for (int e = 0; e < 8; ++e) {
            int deg = kFigure[bar][e];
            if (deg < 0) continue;
            float beat = static_cast<float>(bar) * 4.0f + static_cast<float>(e) * 0.5f;
            addPluck(pcm, rate, beat * kSpb, kYo[deg], kArpAmp[e], 0.996f, rng);
        }
    }
    // Low koto roots: D3 146.83, G3 196.00, A3 220.00.
    constexpr struct { float beat, freq, amp; } kRoots[] = {
        {0.0f, 146.83f, 0.40f},  {4.0f, 220.00f, 0.32f},
        {8.0f, 196.00f, 0.32f},  {12.0f, 220.00f, 0.32f},
        {16.0f, 146.83f, 0.40f}, {20.0f, 196.00f, 0.34f},
        {24.0f, 220.00f, 0.32f}, {28.0f, 146.83f, 0.36f},
    };
    for (const auto& r : kRoots) addPluck(pcm, rate, r.beat * kSpb, r.freq, r.amp, 0.997f, rng);
    // Glints just before each phrase turn — sun on the water.
    addBell(pcm, rate, 3.75f * kSpb, 1568.00f, 0.07f, 3.5f);
    addBell(pcm, rate, 11.75f * kSpb, 1760.00f, 0.07f, 3.5f);
    addBell(pcm, rate, 19.75f * kSpb, 1975.53f, 0.07f, 3.5f);
    addBell(pcm, rate, 27.75f * kSpb, 1760.00f, 0.07f, 3.5f);
    // A soft heartbeat drum and the flute drifting over the petals.
    addTaiko(pcm, rate, 0.0f, 70.0f, 0.18f, rng);
    addTaiko(pcm, rate, 16.0f * kSpb, 70.0f, 0.18f, rng);
    addFlute(pcm, rate, 8.0f * kSpb, 493.88f, 0.085f, 2.6f, rng);
    addFlute(pcm, rate, 16.0f * kSpb, 587.33f, 0.080f, 2.2f, rng);
    addFlute(pcm, rate, 24.0f * kSpb, 440.00f, 0.085f, 3.4f, rng);
    normalizePeak(pcm, kMusicPeak);
    return pcm;
}

// Sorihashi — dusk on the river: koto in the in scale on A over a slow
// walking taiko, a deep bell for the failing light, glints low on the water.
// The B-flat pulling home to A at each turn is the in scale's own melancholy,
// which is what a bridge at nightfall sounds like. 8 bars of 4 at 66 BPM.
std::vector<float> renderSorihashi(float rate) {
    constexpr float kSpb = 60.0f / 66.0f;
    std::vector<float> pcm(static_cast<std::size_t>(rate * kSpb * 32.0f), 0.0f);
    std::uint32_t rng = 0x50a1ba51u;
    auto pluck = [&](float beat, float freq, float amp, float damp = 0.9965f) {
        addPluck(pcm, rate, beat * kSpb, freq, amp, damp, rng);
    };
    // A2 110.00  Bb2 116.54  D3 146.83  E3 164.81  A3 220.00  Bb3 233.08
    // D4 293.66  E4 329.63  F4 349.23  A4 440.00  D5 587.33
    addBell(pcm, rate, 0.0f, 110.0f, 0.42f, 0.9f);
    addBell(pcm, rate, 16.0f * kSpb, 110.0f, 0.32f, 0.9f);
    // The walk across: one soft footfall to a bar, never hurried.
    for (float b = 0.0f; b < 32.0f; b += 4.0f) {
        addTaiko(pcm, rate, b * kSpb, 65.0f, 0.20f, rng);
    }
    // Low koto roots; Bb at the last turn leans the loop back onto A.
    pluck(0.0f, 110.0f, 0.42f, 0.997f);
    pluck(8.0f, 146.83f, 0.34f, 0.997f);
    pluck(16.0f, 110.0f, 0.40f, 0.997f);
    pluck(24.0f, 164.81f, 0.34f, 0.997f);
    pluck(28.0f, 116.54f, 0.30f, 0.997f);
    // The melody: out over the water, a long look down, and back.
    pluck(0.0f, 440.00f, 0.38f);
    pluck(2.0f, 349.23f, 0.30f);
    pluck(3.0f, 329.63f, 0.32f);
    pluck(6.0f, 293.66f, 0.30f);
    pluck(8.0f, 329.63f, 0.34f);
    pluck(10.0f, 349.23f, 0.28f);
    pluck(11.0f, 329.63f, 0.26f);
    pluck(12.0f, 293.66f, 0.30f);
    pluck(14.0f, 233.08f, 0.26f);
    pluck(16.0f, 220.00f, 0.34f);
    pluck(18.0f, 293.66f, 0.30f);
    pluck(20.0f, 329.63f, 0.34f);
    pluck(22.0f, 349.23f, 0.28f);
    pluck(23.0f, 329.63f, 0.24f);
    pluck(24.0f, 440.00f, 0.36f);
    pluck(26.0f, 329.63f, 0.28f);
    pluck(28.0f, 293.66f, 0.26f);
    pluck(29.5f, 233.08f, 0.24f);
    pluck(30.5f, 220.00f, 0.26f);
    // Glints before each phrase turn — the last light off the river.
    addBell(pcm, rate, 7.75f * kSpb, 1174.66f, 0.055f, 3.4f);
    addBell(pcm, rate, 15.75f * kSpb, 880.00f, 0.055f, 3.4f);
    addBell(pcm, rate, 23.75f * kSpb, 1318.51f, 0.050f, 3.4f);
    // A flute out of the dark on the far bank, twice, the second lower.
    addFlute(pcm, rate, 8.0f * kSpb, 440.00f, 0.080f, 3.0f, rng);
    addFlute(pcm, rate, 24.0f * kSpb, 349.23f, 0.075f, 3.6f, rng);
    normalizePeak(pcm, kMusicPeak);
    return pcm;
}

// In Music enum order, so kMusicFiles[i] names what kRenderers[i] produces.
using Renderer = std::vector<float> (*)(float);
constexpr Renderer kRenderers[kMusicCount] = {renderMenu, renderDojo, renderHanami,
                                              renderSorihashi};

// ---- wav -----------------------------------------------------------------

void pushLE(std::vector<unsigned char>& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xffu));
    }
}

void pushTag(std::vector<unsigned char>& out, const char* tag) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(tag[i]));
}

// Canonical 44-byte-header mono 16-bit PCM. Nothing here needs a library, and
// 16-bit is plenty: the tracks peak at kMusicPeak, well above the noise floor,
// and it halves what lands in git versus float samples.
bool writeWav(const std::filesystem::path& path, const std::vector<float>& pcm) {
    auto dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    std::vector<unsigned char> bytes;
    bytes.reserve(44 + dataBytes);
    pushTag(bytes, "RIFF");
    pushLE(bytes, 36 + dataBytes, 4);
    pushTag(bytes, "WAVE");
    pushTag(bytes, "fmt ");
    pushLE(bytes, 16, 4);                                       // chunk size
    pushLE(bytes, 1, 2);                                        // PCM
    pushLE(bytes, 1, 2);                                        // mono
    pushLE(bytes, static_cast<std::uint32_t>(kRate), 4);
    pushLE(bytes, static_cast<std::uint32_t>(kRate) * 2, 4);    // byte rate
    pushLE(bytes, 2, 2);                                        // block align
    pushLE(bytes, 16, 2);                                       // bits
    pushTag(bytes, "data");
    pushLE(bytes, dataBytes, 4);
    for (float s : pcm) {
        auto q = static_cast<std::int16_t>(
            std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
        pushLE(bytes, static_cast<std::uint16_t>(q), 2);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "musicgen: cannot open %s for writing\n",
                     path.string().c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        std::fprintf(stderr, "musicgen: write failed for %s\n", path.string().c_str());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    // MUSIC_OUT_DIR is assets/music in the source tree, so the no-argument
    // form regenerates the committed files in place.
    std::filesystem::path outDir = argc > 1 ? argv[1] : MUSIC_OUT_DIR;
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        std::fprintf(stderr, "musicgen: cannot create %s: %s\n",
                     outDir.string().c_str(), ec.message().c_str());
        return 1;
    }

    for (int t = 0; t < kMusicCount; ++t) {
        std::vector<float> pcm = kRenderers[t](kRate);
        std::filesystem::path path = outDir / kMusicFiles[t];
        if (!writeWav(path, pcm)) {
            return 1;
        }
        std::printf("%-12s %6.2f s  %s\n", kMusicFiles[t],
                    static_cast<double>(pcm.size()) / kRate, path.string().c_str());
    }
    return 0;
}
