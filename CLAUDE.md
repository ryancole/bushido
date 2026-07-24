# bushido

Side-view 3D sword PvP game (1v1). C++20, Vulkan 1.3 (dynamic rendering, sync2), GLFW, vk-bootstrap, GLM. Dependencies come in via CMake FetchContent; the Vulkan SDK is installed at `C:\VulkanSDK\1.4.350.0`.

## Build (Windows, MSVC + Ninja)

The toolchain is pinned to MSVC via CMakePresets.json (`msvc-debug` → `build/`, `msvc-release` → `build-release/`); CMakeLists errors out on any other Windows compiler. MSVC is not on PATH; chain through vcvars64:

```
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset msvc-debug && cmake --build --preset msvc-debug'
```

Run: `build\bushido.exe`. Esc quits. P1: WASD moves in the ground plane (W/S = depth), Space jumps, **left mouse button attacks**. P2: arrows move, Right Ctrl jumps, Right Shift attacks.

VS Code: `.vscode/` has a default build task (Ctrl+Shift+B), a `configure` task, and F5 debug configs (`cppvsdbg`). IntelliSense reads `build/compile_commands.json`.

## Layout

- `src/main.cpp` — game loop (fixed 120 Hz timestep), input reading, scene drawing
- `src/game.hpp/.cpp` — simulation: two `Player`s moving freely in the x/z ground plane, gravity/jump, radial body push-apart, arena clamp (±kArenaHalfWidth in x, ±kArenaHalfDepth in z). Sword combat: `AttackState` machine (windup 0.12s → active 0.14s → recovery 0.28s), facing-directed AABB hitbox during active, hit ⇒ hitstun + planar knockback + upward pop + interrupts the victim's swing. Attack input is edge-triggered and latched in main so presses aren't lost between fixed steps (GLFW sticky input modes are on for the same reason).
- `src/camera.hpp/.cpp` — `FramingCamera`: follows the fighters' midpoint, zooms out so both always fit the frustum (with margin), accounting for each fighter's depth; exponential smoothing
- `src/renderer.hpp/.cpp` — all Vulkan state; API is `beginFrame` / `setViewProj` / `drawBox(model, color)` / `endFrame`
- `src/samurai.hpp/.cpp` — procedural samurai model: ~25 boxes (hakama legs, kimono torso, obi, sode, arms, head, kasa, sheathed katana) with stride/idle/jump animation driven by `SamuraiPose`
- `shaders/` — GLSL, compiled to SPIR-V at build time by glslc into `build/shaders/`

## Current conventions (early, expected to change)

- World space: x right, y **up**, z toward the camera, ground surface at y = 0, units ~meters. Camera looks down -Z from +Z; gameplay is side-focused but movement is full 3D.
- GLM is compiled with `GLM_FORCE_DEPTH_ZERO_TO_ONE` + `GLM_FORCE_RADIANS`; `FramingCamera::proj` flips Y for Vulkan clip space.
- Everything renders as a shaded unit cube generated in the vertex shader from `gl_VertexIndex`; models may rotate (normal matrix travels in push constants). `ObjectPush` is exactly 128 bytes — the guaranteed push-constant minimum — and must match `shaders/cube.vert`; do not grow it.
- Depth: single D32 depth image shared by frames in flight, transitioned from UNDEFINED each frame.
- Shaders are loaded via the `SHADER_DIR` compile definition (absolute path into the build tree) — dev-only scheme.
