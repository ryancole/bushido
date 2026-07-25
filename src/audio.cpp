#include "audio.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979f;

// Overlapping instances of the same effect (both players can swing at once,
// and a hit can land while a whoosh is still ringing out).
constexpr int kVoicesPerSfx = 4;

// Deterministic noise source so the effects sound identical every run.
float noise(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

// Chamberlin state-variable filter, bandpass output. q is the damping term
// (lower = sharper resonance).
struct Bandpass {
    float low = 0.0f;
    float band = 0.0f;
    float process(float in, float freqHz, float q, float rate) {
        float f = 2.0f * std::sin(kPi * std::min(freqHz / rate, 0.2f));
        low += f * band;
        float high = in - low - q * band;
        band += f * high;
        return band;
    }
};

void normalizePeak(std::vector<float>& pcm, float peakTarget) {
    float peak = 0.0f;
    for (float s : pcm) peak = std::max(peak, std::abs(s));
    if (peak > 1e-6f) {
        float gain = peakTarget / peak;
        for (float& s : pcm) s *= gain;
    }
}

// Swing: a noise whoosh whose bandpass center and level swell together.
// Triggered at windup start, the swell peaks ~0.16s in — right as the blade's
// active sweep begins (windup is 0.12s).
std::vector<float> synthSwing(float rate) {
    std::vector<float> pcm(static_cast<std::size_t>(rate * 0.32f));
    std::uint32_t rng = 0x51ce5eedu;
    Bandpass bp;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        float t = static_cast<float>(i) / static_cast<float>(pcm.size());
        float env = std::sin(kPi * t);
        env *= env; // peaks mid-sample, soft in and out
        float freq = 400.0f + 1900.0f * env;
        pcm[i] = bp.process(noise(rng), freq, 0.5f, rate) * env;
    }
    normalizePeak(pcm, 0.38f);
    return pcm;
}

// Hit: a pitch-dropping thud with a short noise crack on the front.
std::vector<float> synthHit(float rate) {
    std::vector<float> pcm(static_cast<std::size_t>(rate * 0.22f));
    std::uint32_t rng = 0x0ddba11u;
    float phase = 0.0f;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        float t = static_cast<float>(i) / rate; // seconds
        float freq = 65.0f + 120.0f * std::exp(-t * 28.0f);
        phase += 2.0f * kPi * freq / rate;
        float thud = std::sin(phase) * std::exp(-t * 16.0f);
        float crack = noise(rng) * std::exp(-t * 70.0f);
        pcm[i] = thud + 0.7f * crack;
    }
    normalizePeak(pcm, 0.85f);
    return pcm;
}

// Dismember: the hit's thud an octave down and slower, a harder crack, and a
// wet slicing noise band that rings out longer.
std::vector<float> synthDismember(float rate) {
    std::vector<float> pcm(static_cast<std::size_t>(rate * 0.42f));
    std::uint32_t rng = 0xdeadb0d5u;
    Bandpass bp;
    float phase = 0.0f;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        float t = static_cast<float>(i) / rate; // seconds
        float freq = 50.0f + 100.0f * std::exp(-t * 24.0f);
        phase += 2.0f * kPi * freq / rate;
        float thud = std::sin(phase) * std::exp(-t * 11.0f);
        float crack = noise(rng) * std::exp(-t * 60.0f);
        float slice = bp.process(noise(rng), 1400.0f - 900.0f * t, 0.8f, rate) *
                      std::exp(-t * 9.0f);
        pcm[i] = thud + 0.9f * crack + 0.8f * slice;
    }
    normalizePeak(pcm, 0.95f);
    return pcm;
}

// Thud: a severed limb landing — dull, low, no crack. Duller and shorter than
// a sword hit; play() scales its gain by impact speed so bounces trail off.
std::vector<float> synthThud(float rate) {
    std::vector<float> pcm(static_cast<std::size_t>(rate * 0.16f));
    std::uint32_t rng = 0xf10c0f0eu;
    float phase = 0.0f;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        float t = static_cast<float>(i) / rate; // seconds
        float freq = 55.0f + 45.0f * std::exp(-t * 35.0f);
        phase += 2.0f * kPi * freq / rate;
        float thump = std::sin(phase) * std::exp(-t * 24.0f);
        float scuff = noise(rng) * std::exp(-t * 110.0f);
        pcm[i] = thump + 0.25f * scuff;
    }
    normalizePeak(pcm, 0.60f);
    return pcm;
}

// Block: steel catching steel — a hard noise strike on the front with a
// bright inharmonic clang (detuned high partials) ringing down behind it.
std::vector<float> synthBlock(float rate) {
    std::vector<float> pcm(static_cast<std::size_t>(rate * 0.28f));
    std::uint32_t rng = 0xb10c5eedu;
    constexpr float kPartials[4] = {1170.0f, 1780.0f, 2460.0f, 3310.0f};
    float phase[4] = {};
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        float t = static_cast<float>(i) / rate; // seconds
        float ring = 0.0f;
        for (int k = 0; k < 4; ++k) {
            phase[k] += 2.0f * kPi * kPartials[k] / rate;
            ring += std::sin(phase[k]) * std::exp(-t * (16.0f + 7.0f * k)) /
                    (1.0f + 0.7f * k);
        }
        float strike = noise(rng) * std::exp(-t * 90.0f);
        pcm[i] = ring + 0.8f * strike;
    }
    normalizePeak(pcm, 0.70f);
    return pcm;
}

// ---- music ---------------------------------------------------------------
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

// Menu — solo koto in the in scale on E (E F A B C) over a temple bell:
// the stillness before a duel. 8 bars of 4 at 72 BPM.
std::vector<float> synthMenuMusic(float rate) {
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
    normalizePeak(pcm, 0.30f);
    return pcm;
}

// Dojo — taiko discipline: a drum line under a tense E–F shamisen ostinato,
// sparse dark accents above. 8 bars of 4 at 100 BPM.
std::vector<float> synthDojoMusic(float rate) {
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
    normalizePeak(pcm, 0.30f);
    return pcm;
}

// Hanami — koto arpeggios in the yo scale on D (D E G A B), bell glints like
// light on the stream, a flute drifting over. 8 bars of 4 at 84 BPM.
std::vector<float> synthHanamiMusic(float rate) {
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
    normalizePeak(pcm, 0.30f);
    return pcm;
}

} // namespace

struct Audio::Impl {
    ma_engine engine;
    bool engineInited = false;
    bool ok = false;
    // The device's rate, captured once the engine is up: every synth step runs
    // after initDevice and composes straight at the output rate (no resampling).
    float rate = 48000.0f;

    std::vector<float> pcm[kSfxCount];
    // Each voice owns its own read cursor (ma_audio_buffer) over the shared
    // PCM, so the same effect can overlap itself.
    struct Voice {
        ma_audio_buffer buffer;
        ma_sound sound;
        bool inited = false;
    };
    Voice voices[kSfxCount][kVoicesPerSfx];
    int nextVoice[kSfxCount] = {};

    // One looping voice per music track; playMusic crossfades between them.
    std::vector<float> musicPcm[kMusicCount];
    Voice music[kMusicCount];
    int currentMusic = -1;

    // Mixer levels from the player's settings (1 = as synthesized).
    float musicVolume = 1.0f;
    float sfxVolume = 1.0f;
};

Audio::Audio() : m_impl(std::make_unique<Impl>()) {}

void Audio::initDevice() {
    if (ma_engine_init(nullptr, &m_impl->engine) != MA_SUCCESS) {
        std::fprintf(stderr, "audio: engine init failed; continuing without sound\n");
        return;
    }
    m_impl->engineInited = true;
    m_impl->rate = static_cast<float>(ma_engine_get_sample_rate(&m_impl->engine));
}

void Audio::initSfx() {
    if (!m_impl->engineInited) {
        return;
    }
    const float rate = m_impl->rate;
    m_impl->pcm[static_cast<int>(Sfx::Swing)] = synthSwing(rate);
    m_impl->pcm[static_cast<int>(Sfx::Hit)] = synthHit(rate);
    m_impl->pcm[static_cast<int>(Sfx::Dismember)] = synthDismember(rate);
    m_impl->pcm[static_cast<int>(Sfx::Thud)] = synthThud(rate);
    m_impl->pcm[static_cast<int>(Sfx::Block)] = synthBlock(rate);
}

void Audio::initMusic(Music track) {
    if (!m_impl->engineInited) {
        return;
    }
    const float rate = m_impl->rate;
    switch (track) {
    case Music::Menu:
        m_impl->musicPcm[static_cast<int>(track)] = synthMenuMusic(rate);
        break;
    case Music::Dojo:
        m_impl->musicPcm[static_cast<int>(track)] = synthDojoMusic(rate);
        break;
    case Music::Hanami:
        m_impl->musicPcm[static_cast<int>(track)] = synthHanamiMusic(rate);
        break;
    }
}

// The last startup step: one playback cursor per voice over the PCM the synth
// steps produced. Everything stays silent until this succeeds for all of them.
void Audio::initVoices() {
    if (!m_impl->engineInited) {
        return;
    }

    auto initVoice = [&](Impl::Voice& voice, const std::vector<float>& pcm) {
        ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
            ma_format_f32, 1, pcm.size(), pcm.data(), nullptr);
        cfg.sampleRate = ma_engine_get_sample_rate(&m_impl->engine);
        if (ma_audio_buffer_init(&cfg, &voice.buffer) != MA_SUCCESS) {
            std::fprintf(stderr, "audio: buffer init failed; continuing without sound\n");
            return false;
        }
        if (ma_sound_init_from_data_source(&m_impl->engine, &voice.buffer,
                                           MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                           &voice.sound) != MA_SUCCESS) {
            ma_audio_buffer_uninit(&voice.buffer);
            std::fprintf(stderr, "audio: sound init failed; continuing without sound\n");
            return false;
        }
        voice.inited = true;
        return true;
    };

    for (int s = 0; s < kSfxCount; ++s) {
        for (int v = 0; v < kVoicesPerSfx; ++v) {
            if (!initVoice(m_impl->voices[s][v], m_impl->pcm[s])) {
                return;
            }
        }
    }
    for (int t = 0; t < kMusicCount; ++t) {
        if (!initVoice(m_impl->music[t], m_impl->musicPcm[t])) {
            return;
        }
        ma_sound_set_looping(&m_impl->music[t].sound, MA_TRUE);
        // The player's level may have been set before these voices existed
        // (setMusicVolume stores it either way), so seat it now.
        ma_sound_set_volume(&m_impl->music[t].sound, m_impl->musicVolume);
    }
    m_impl->ok = true;
}

Audio::~Audio() {
    auto uninitVoice = [](Impl::Voice& voice) {
        if (voice.inited) {
            ma_sound_uninit(&voice.sound);
            ma_audio_buffer_uninit(&voice.buffer);
        }
    };
    for (int s = 0; s < kSfxCount; ++s) {
        for (int v = 0; v < kVoicesPerSfx; ++v) {
            uninitVoice(m_impl->voices[s][v]);
        }
    }
    for (int t = 0; t < kMusicCount; ++t) {
        uninitVoice(m_impl->music[t]);
    }
    if (m_impl->engineInited) {
        ma_engine_uninit(&m_impl->engine);
    }
}

void Audio::play(Sfx sfx, float pan, float pitch, float gain) {
    if (!m_impl->ok) {
        return;
    }
    int s = static_cast<int>(sfx);
    Impl::Voice& voice = m_impl->voices[s][m_impl->nextVoice[s]];
    m_impl->nextVoice[s] = (m_impl->nextVoice[s] + 1) % kVoicesPerSfx;

    ma_sound_stop(&voice.sound);
    ma_sound_seek_to_pcm_frame(&voice.sound, 0);
    ma_sound_set_pan(&voice.sound, std::clamp(pan, -1.0f, 1.0f));
    ma_sound_set_pitch(&voice.sound, pitch);
    ma_sound_set_volume(&voice.sound,
                        std::clamp(gain, 0.0f, 1.0f) * m_impl->sfxVolume);
    ma_sound_start(&voice.sound);
}

void Audio::setMusicVolume(float volume) {
    m_impl->musicVolume = std::clamp(volume, 0.0f, 1.0f);
    if (!m_impl->ok) {
        return;
    }
    // The bus volume and the crossfade fader are independent multipliers in
    // miniaudio, so setting this mid-transition scales the fade rather than
    // cancelling it — no need to know whether one is running.
    for (int t = 0; t < kMusicCount; ++t) {
        ma_sound_set_volume(&m_impl->music[t].sound, m_impl->musicVolume);
    }
}

void Audio::setSfxVolume(float volume) {
    // Nothing to push: play() scales each effect as it starts.
    m_impl->sfxVolume = std::clamp(volume, 0.0f, 1.0f);
}

void Audio::playMusic(Music track) {
    if (!m_impl->ok) {
        return;
    }
    int t = static_cast<int>(track);
    if (t == m_impl->currentMusic) {
        return;
    }
    if (m_impl->currentMusic >= 0) {
        ma_sound_stop_with_fade_in_milliseconds(
            &m_impl->music[m_impl->currentMusic].sound, 700);
    }
    Impl::Voice& voice = m_impl->music[t];
    ma_sound_stop(&voice.sound); // reset any lingering fade-out state
    ma_sound_seek_to_pcm_frame(&voice.sound, 0);
    ma_sound_set_fade_in_milliseconds(&voice.sound, 0.0f, 1.0f, 900);
    ma_sound_start(&voice.sound);
    m_impl->currentMusic = t;
}
