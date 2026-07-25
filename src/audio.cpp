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

} // namespace

struct Audio::Impl {
    ma_engine engine;
    bool engineInited = false;
    bool ok = false;

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
};

Audio::Audio() : m_impl(std::make_unique<Impl>()) {
    if (ma_engine_init(nullptr, &m_impl->engine) != MA_SUCCESS) {
        std::fprintf(stderr, "audio: engine init failed; continuing without sound\n");
        return;
    }
    m_impl->engineInited = true;

    const float rate = static_cast<float>(ma_engine_get_sample_rate(&m_impl->engine));
    m_impl->pcm[static_cast<int>(Sfx::Swing)] = synthSwing(rate);
    m_impl->pcm[static_cast<int>(Sfx::Hit)] = synthHit(rate);
    m_impl->pcm[static_cast<int>(Sfx::Dismember)] = synthDismember(rate);
    m_impl->pcm[static_cast<int>(Sfx::Thud)] = synthThud(rate);
    m_impl->pcm[static_cast<int>(Sfx::Block)] = synthBlock(rate);

    for (int s = 0; s < kSfxCount; ++s) {
        for (int v = 0; v < kVoicesPerSfx; ++v) {
            Impl::Voice& voice = m_impl->voices[s][v];
            ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
                ma_format_f32, 1, m_impl->pcm[s].size(), m_impl->pcm[s].data(),
                nullptr);
            cfg.sampleRate = ma_engine_get_sample_rate(&m_impl->engine);
            if (ma_audio_buffer_init(&cfg, &voice.buffer) != MA_SUCCESS) {
                std::fprintf(stderr, "audio: buffer init failed; continuing without sound\n");
                return;
            }
            if (ma_sound_init_from_data_source(&m_impl->engine, &voice.buffer,
                                               MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                               &voice.sound) != MA_SUCCESS) {
                ma_audio_buffer_uninit(&voice.buffer);
                std::fprintf(stderr, "audio: sound init failed; continuing without sound\n");
                return;
            }
            voice.inited = true;
        }
    }
    m_impl->ok = true;
}

Audio::~Audio() {
    for (int s = 0; s < kSfxCount; ++s) {
        for (int v = 0; v < kVoicesPerSfx; ++v) {
            Impl::Voice& voice = m_impl->voices[s][v];
            if (voice.inited) {
                ma_sound_uninit(&voice.sound);
                ma_audio_buffer_uninit(&voice.buffer);
            }
        }
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
    ma_sound_set_volume(&voice.sound, std::clamp(gain, 0.0f, 1.0f));
    ma_sound_start(&voice.sound);
}
