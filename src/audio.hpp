#pragma once

#include <memory>

// Sound effect ids. The PCM for each is synthesized at startup — no asset
// files — in the same procedural spirit as the box-built samurai.
enum class Sfx { Swing = 0, Hit, Dismember, Thud };
inline constexpr int kSfxCount = 4;

// miniaudio wrapper (pimpl; miniaudio headers only in the .cpp). If the audio
// device can't be opened the game keeps running silent.
class Audio {
public:
    Audio();
    ~Audio();

    // pan: -1 full left .. +1 full right. pitch: 1 = as synthesized; small
    // per-play jitter keeps repeated effects from sounding stamped-out.
    // gain: 0..1 scale on the effect's baked level (e.g. impact strength).
    void play(Sfx sfx, float pan, float pitch, float gain = 1.0f);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
