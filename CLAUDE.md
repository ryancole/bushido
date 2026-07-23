# bushido

Side-view 3D sword PvP game (1v1). C++20, Vulkan 1.3 (dynamic rendering, sync2), GLFW, vk-bootstrap, GLM. Dependencies come in via CMake FetchContent; the Vulkan SDK is installed at `C:\VulkanSDK\1.4.350.0`.

## Build (Windows, MSVC + Ninja)

The toolchain is pinned to MSVC via CMakePresets.json (`msvc-debug` → `build/`, `msvc-release` → `build-release/`); CMakeLists errors out on any other Windows compiler. MSVC is not on PATH; chain through vcvars64:

```
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset msvc-debug && cmake --build --preset msvc-debug'
```

Run: `build\bushido.exe`. Esc quits. P1: A/D move, W/Space jump. P2: arrows.

VS Code: `.vscode/` has a default build task (Ctrl+Shift+B), a `configure` task, and F5 debug configs (`cppvsdbg`). IntelliSense reads `build/compile_commands.json`.

## Layout

- `src/main.cpp` — game loop (fixed 120 Hz timestep), input reading, scene drawing
- `src/game.hpp/.cpp` — simulation: two `Player`s, movement/gravity/jump, body push-apart, arena clamp
- `src/camera.hpp/.cpp` — `FramingCamera`: follows the fighters' midpoint, zooms out so both always fit the frustum (with margin), exponential smoothing
- `src/renderer.hpp/.cpp` — all Vulkan state; API is `beginFrame` / `drawBox` / `endFrame`
- `shaders/` — GLSL, compiled to SPIR-V at build time by glslc into `build/shaders/`

## Current conventions (early, expected to change)

- World space: x right, y **up**, fighting plane at z = 0, ground surface at y = 0, units ~meters. Camera looks down -Z from +Z.
- GLM is compiled with `GLM_FORCE_DEPTH_ZERO_TO_ONE` + `GLM_FORCE_RADIANS`; `FramingCamera::proj` flips Y for Vulkan clip space.
- Everything renders as a shaded unit cube generated in the vertex shader from `gl_VertexIndex` (translate+scale models only — normals assume no rotation). Per-object data goes in push constants (`ObjectPush { mat4 mvp; vec4 color; }` — must match `shaders/cube.vert`).
- Depth: single D32 depth image shared by frames in flight, transitioned from UNDEFINED each frame.
- Shaders are loaded via the `SHADER_DIR` compile definition (absolute path into the build tree) — dev-only scheme.
