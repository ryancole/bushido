#pragma once

#include <memory>

// Sound effect ids. The PCM for each is synthesized at startup — no asset
// files — in the same procedural spirit as the box-built samurai.
enum class Sfx { Swing = 0, Hit, Dismember, Thud, Block };
inline constexpr int kSfxCount = 5;

// Music track ids — like the SFX, every track is composed and synthesized at
// startup as one seamless loop. Menu underscores the whole front-end (main
// menu + select screens); each battleground has its own theme, ordered after
// Menu to match the level roster in level.cpp.
enum class Music { Menu = 0, Dojo, Hanami };
inline constexpr int kMusicCount = 3;

// A battleground's theme (level roster index -> track).
inline Music levelMusic(int levelIndex) {
    return static_cast<Music>(1 + levelIndex);
}

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

    // Crossfades to the given looping track; a no-op when it's already the
    // one playing, so callers just state "this should be the music now"
    // every frame and transitions fall out of app-state changes.
    void playMusic(Music track);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
