#pragma once

// Shared DSP primitives for the game's procedurally generated audio. Two
// callers: audio.cpp, which still synthesizes every sound effect at startup,
// and tools/musicgen.cpp, which renders the music tracks offline into the
// WAVs under assets/music/. Header-only so neither needs a library.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace synth {

constexpr float kPi = 3.14159265358979f;

// Deterministic noise source so everything sounds identical every run — and,
// for the music, so a regenerated WAV is byte-identical to the last one.
inline float noise(std::uint32_t& state) {
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

inline void normalizePeak(std::vector<float>& pcm, float peakTarget) {
    float peak = 0.0f;
    for (float s : pcm) peak = std::max(peak, std::abs(s));
    if (peak > 1e-6f) {
        float gain = peakTarget / peak;
        for (float& s : pcm) s *= gain;
    }
}

} // namespace synth
