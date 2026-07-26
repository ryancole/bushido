#include "audio.hpp"

#include "paths.hpp"
#include "synth.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using synth::Bandpass;
using synth::kPi;
using synth::noise;
using synth::normalizePeak;

// Overlapping instances of the same effect (both players can swing at once,
// and a hit can land while a whoosh is still ringing out).
constexpr int kVoicesPerSfx = 4;

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
    // The device's rate, captured once the engine is up: the effects are
    // synthesized after initDevice and so land straight at the output rate.
    // (The music can't — it's a file, rendered at a fixed rate — so miniaudio
    // resamples those on the way out if the device disagrees.)
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

    // One looping voice per music track, each decoded from its WAV by
    // loadMusic; playMusic crossfades between them. No ma_audio_buffer here —
    // the sound owns the decoded data, so a track is just the sound.
    struct MusicVoice {
        ma_sound sound;
        bool inited = false;
        // Whether loading has been attempted. A track that failed is never
        // retried, so a missing file can't re-open (and re-log) every frame
        // playMusic asks for it.
        bool tried = false;
    };
    MusicVoice music[kMusicCount];
    int currentMusic = -1;

    // Mixer levels from the player's settings (1 = as authored).
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

// A track's WAV, decoded in full (no MA_SOUND_FLAG_STREAM) so playback never
// touches the disk again — a track is ~20 s of PCM, which is cheap to hold and
// cheaper than a decoder thread waking up mid-match. Called for the front-end
// theme during the loading screen and for a battleground's as its match is
// built; missing or unreadable files cost that one track and nothing else.
void Audio::loadMusic(Music track) {
    if (!m_impl->engineInited) {
        return;
    }
    int t = static_cast<int>(track);
    Impl::MusicVoice& voice = m_impl->music[t];
    if (voice.tried) {
        return;
    }
    voice.tried = true;
    std::string path = assetPath((std::string("music/") + kMusicFiles[t]).c_str());
    if (ma_sound_init_from_file(&m_impl->engine, path.c_str(),
                                MA_SOUND_FLAG_NO_SPATIALIZATION |
                                    MA_SOUND_FLAG_DECODE,
                                nullptr, nullptr, &voice.sound) != MA_SUCCESS) {
        std::fprintf(stderr, "audio: could not load %s; that track stays silent\n",
                     path.c_str());
        return;
    }
    voice.inited = true;
    ma_sound_set_looping(&voice.sound, MA_TRUE);
    // The player's level may have been set long before this track existed
    // (setMusicVolume stores it either way), so seat it now.
    ma_sound_set_volume(&voice.sound, m_impl->musicVolume);
}

// The last startup step: one playback cursor per voice over the PCM the synth
// step produced. The effects stay silent until this succeeds for all of them.
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
    for (int t = 0; t < kMusicCount; ++t) {
        if (m_impl->music[t].inited) {
            ma_sound_uninit(&m_impl->music[t].sound);
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
    ma_sound_set_volume(&voice.sound,
                        std::clamp(gain, 0.0f, 1.0f) * m_impl->sfxVolume);
    ma_sound_start(&voice.sound);
}

void Audio::setMusicVolume(float volume) {
    m_impl->musicVolume = std::clamp(volume, 0.0f, 1.0f);
    // The bus volume and the crossfade fader are independent multipliers in
    // miniaudio, so setting this mid-transition scales the fade rather than
    // cancelling it — no need to know whether one is running. Tracks not
    // loaded yet pick the level up in loadMusic.
    for (int t = 0; t < kMusicCount; ++t) {
        if (m_impl->music[t].inited) {
            ma_sound_set_volume(&m_impl->music[t].sound, m_impl->musicVolume);
        }
    }
}

void Audio::setSfxVolume(float volume) {
    // Nothing to push: play() scales each effect as it starts.
    m_impl->sfxVolume = std::clamp(volume, 0.0f, 1.0f);
}

void Audio::playMusic(Music track) {
    int t = static_cast<int>(track);
    if (t == m_impl->currentMusic) {
        return;
    }
    // Normally a no-op — main loads a track as its screen or match is built —
    // but doing it here means asking for music is enough to get it.
    loadMusic(track);
    if (!m_impl->music[t].inited) {
        return;
    }
    if (m_impl->currentMusic >= 0) {
        ma_sound_stop_with_fade_in_milliseconds(
            &m_impl->music[m_impl->currentMusic].sound, 700);
    }
    Impl::MusicVoice& voice = m_impl->music[t];
    ma_sound_stop(&voice.sound); // reset any lingering fade-out state
    ma_sound_seek_to_pcm_frame(&voice.sound, 0);
    ma_sound_set_fade_in_milliseconds(&voice.sound, 0.0f, 1.0f, 900);
    ma_sound_start(&voice.sound);
    m_impl->currentMusic = t;
}
