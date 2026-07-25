#pragma once

#include <memory>

// Sound effect ids. The PCM for each is synthesized at startup — no asset
// files — in the same procedural spirit as the box-built samurai. They're
// short enough (under half a second each) that generating them costs nothing.
enum class Sfx { Swing = 0, Hit, Dismember, Thud, Block };
inline constexpr int kSfxCount = 5;

// Music track ids. Each track is one seamless loop, and unlike the effects
// they are *files*: ~69 seconds of PCM was too much to re-synthesize on every
// launch, so the composition moved into tools/musicgen, which renders the
// WAVs below once. Menu underscores the whole front-end (main menu + select
// screens); each battleground has its own theme, ordered after Menu to match
// the level roster in level.cpp.
enum class Music { Menu = 0, Dojo, Hanami };
inline constexpr int kMusicCount = 3;

// Each track's file under assets/music/, indexed by the enum above — the one
// place the generator and the loader agree on a name.
inline constexpr const char* kMusicFiles[kMusicCount] = {
    "menu.wav", "dojo.wav", "hanami.wav"};

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

    // Staged startup, so the loading screen can report honest progress: the
    // constructor costs nothing and main's load-step table drives these in
    // order — initDevice, initSfx, then initVoices, which builds the playback
    // cursors over the finished effect PCM. Until initVoices lands, play is a
    // silent no-op (as it is for the whole run if the device didn't open).
    void initDevice();
    void initSfx();
    void initVoices();

    // Decodes a track's WAV, if it isn't already loaded. Idempotent and safe
    // to call at any point after initDevice — main loads the front-end theme
    // during the loading screen and a battleground's theme as its match is
    // built, which is why nothing here is tied to the startup sequence. A
    // track that fails to load simply stays silent; the game plays on.
    // Once loaded a track stays loaded, so a rematch costs nothing.
    void loadMusic(Music track);

    // pan: -1 full left .. +1 full right. pitch: 1 = as synthesized; small
    // per-play jitter keeps repeated effects from sounding stamped-out.
    // gain: 0..1 scale on the effect's baked level (e.g. impact strength).
    void play(Sfx sfx, float pan, float pitch, float gain = 1.0f);

    // Crossfades to the given looping track; a no-op when it's already the
    // one playing, so callers just state "this should be the music now"
    // every frame and transitions fall out of app-state changes. Loads the
    // track first if it isn't yet, so this can never ask for silence.
    void playMusic(Music track);

    // Mixer levels, 0..1, scaling everything this class plays. Both start at 1
    // — the levels the effects and tracks were rendered against — and the
    // options screen drives them from the saved settings. Music applies to
    // every loaded track at once and is seated on later ones as they load, so
    // the level can be set before any track exists (it rides *under* the
    // crossfade, which is a separate fader, so dragging the slider
    // mid-transition doesn't fight it); SFX applies to each effect as it's
    // played.
    void setMusicVolume(float volume);
    void setSfxVolume(float volume);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
