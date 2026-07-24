# bushido

Side-view 3D sword PvP game (1v1). C++20, Vulkan 1.3 (dynamic rendering, sync2), GLFW, vk-bootstrap, GLM, Jolt Physics, miniaudio. Dependencies come in via CMake FetchContent; the Vulkan SDK is installed at `C:\VulkanSDK\1.4.350.0`.

## Build (Windows, MSVC + Ninja)

The toolchain is pinned to MSVC via CMakePresets.json (`msvc-debug` → `build/`, `msvc-release` → `build-release/`); CMakeLists errors out on any other Windows compiler. MSVC is not on PATH; chain through vcvars64:

```
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset msvc-debug && cmake --build --preset msvc-debug'
```

Run: `build\bushido.exe`. Esc quits. P1: WASD moves in the ground plane (W/S = depth), Space jumps, **left mouse button attacks**. P2: arrows move, Right Ctrl jumps, Right Shift attacks.

VS Code: `.vscode/` has a default build task (Ctrl+Shift+B), a `configure` task, and F5 debug configs (`cppvsdbg`). IntelliSense reads `build/compile_commands.json`.

## Layout

- `src/main.cpp` — game loop (fixed 120 Hz timestep), input reading, scene drawing
- `src/game.hpp/.cpp` — simulation: two `Player`s moving freely in the x/z ground plane. Gameplay stays authored (Game decides velocities: move speed, jump, gravity integration into `vy`, knockback); collision is delegated to `Physics` per fixed step, which returns resolved position + grounded. Sword combat: `AttackState` machine (windup 0.12s → active 0.14s → recovery 0.28s); during active the blade is swept as a segment along the model's swing arc (pure math on `Player::pos`, not Jolt queries) against per-limb AABBs (`samuraiLimbBounds` + per-limb pad) and a torso box. Any connect ⇒ hitstun + planar knockback + upward pop + interrupts the victim's swing; a limb connect also **dismembers**: the limb is flagged in `Player::severed[]`, launched as a Jolt debris body (`SeveredPiece` list, drawn by main), and stays lost for the match. The head's z pad is tight so beheading takes depth-aligning the blade lane (sword arm rides `kShoulderSide` off center); losing the sword arm swaps the katana to the off hand, losing both arms disables attacking. **Blood**: every connect bursts ballistic `BloodParticle` droplets from the wound (pure math, no Jolt; bigger burst on dismember); a droplet reaching the ground — or a debris impact near y = 0 from `debrisImpacts()` — stamps a `BloodMark` floor splat (capped ring buffer, oldest recycled; marks carry per-mark y jitter + random yaw, and main draws them under the blob shadows). Attack input is edge-triggered and latched in main so presses aren't lost between fixed steps (GLFW sticky input modes are on for the same reason).
- `src/physics.hpp/.cpp` — Jolt wrapper (pimpl; Jolt headers only in the .cpp). Owns the `PhysicsSystem`, invisible static arena colliders (ground slab + 4 walls at the arena bounds — these replaced the old position clamp), and one `CharacterVirtual` capsule per fighter (radius = `kHalfWidth`, height = 2·`kHalfHeight`, origin at the feet; `Player::pos` is capsule pos + half height in y). Fighter-vs-fighter solidity comes from `CharacterVsCharacterCollisionSimple`. `moveCharacter` does `SetLinearVelocity` + `ExtendedUpdate`; `step` runs the rigid-body world (arena statics + severed-limb debris boxes; a `DEBRIS` object layer collides with the world, other debris, and the fighters — the `CharacterVirtual` update shoves the much lighter limb bodies aside, so players kick severed pieces around by walking into them). A `ContactListener` (mutex-guarded — Jolt calls it from job threads) records debris contacts above a 2 m/s closing-speed threshold; `debrisImpacts()` exposes them (full contact point + closing speed) per step and Game turns them into thud sound cues and, for ground contacts, blood smears. Jolt is pinned at v5.6.0, dynamic CRT, demos/tests off (see CMakeLists).
- `src/audio.hpp/.cpp` — miniaudio wrapper (pimpl, same pattern as Physics; falls back to silent if no device). All SFX PCM is **synthesized at startup** (no asset files): swing whoosh (bandpassed noise, swell timed so it peaks as the blade goes active), hit (pitch-dropping thud + crack), dismember (deeper thud + slice band), thud (severed limb landing; dull, no crack). Event flow: `Game::update` pushes `SoundCue{Sfx, x, gain}` into a list (swing on attack start, hit/dismember on connect, thud from `Physics::debrisImpacts()` with gain scaled by impact speed); main drains it once per render frame via `soundCues()`/`clearSoundCues()`, panning by world x against the fighters' midpoint and adding per-play pitch jitter. Each effect has a small round-robin voice pool (own `ma_audio_buffer` cursors over shared PCM) so overlapping plays don't cut each other off. miniaudio is pinned at 0.11.23 with `MINIAUDIO_NO_EXTRA_NODES`.
- `src/camera.hpp/.cpp` — `FramingCamera`: follows the fighters' midpoint, zooms out so both always fit the frustum (with margin), accounting for each fighter's depth; exponential smoothing
- `src/renderer.hpp/.cpp` — all Vulkan state; API is `beginFrame` / `setViewProj` / `drawBox(model, color)` / `endFrame`
- `src/samurai.hpp/.cpp` — procedural samurai model: ~25 boxes (hakama legs, kimono torso, obi, sode, arms, head, kasa, sheathed katana) with stride/idle/jump animation driven by `SamuraiPose`. Severable parts are indexed 0..4 (arm +z, arm −z, leg +z, leg −z, head; must match game.hpp's `Limb`): `SamuraiPose::severed` draws stumps instead, `samuraiLimbBounds` exposes each limb's local AABB (gameplay hit regions + debris shapes), `drawSeveredLimb` draws a detached limb under a rigid-body transform
- `shaders/` — GLSL, compiled to SPIR-V at build time by glslc into `build/shaders/`

## Current conventions (early, expected to change)

- World space: x right, y **up**, z toward the camera, ground surface at y = 0, units ~meters. Camera looks down -Z from +Z; gameplay is side-focused but movement is full 3D.
- GLM is compiled with `GLM_FORCE_DEPTH_ZERO_TO_ONE` + `GLM_FORCE_RADIANS`; `FramingCamera::proj` flips Y for Vulkan clip space.
- Everything renders as a shaded unit cube generated in the vertex shader from `gl_VertexIndex`; models may rotate (normal matrix travels in push constants). `ObjectPush` is exactly 128 bytes — the guaranteed push-constant minimum — and must match `shaders/cube.vert`; do not grow it.
- Depth: single D32 depth image shared by frames in flight, transitioned from UNDEFINED each frame.
- Shaders are loaded via the `SHADER_DIR` compile definition (absolute path into the build tree) — dev-only scheme.
