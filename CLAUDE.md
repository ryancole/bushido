# bushido

Side-scrolling sword PvP game. C++20, Vulkan 1.3 (dynamic rendering, sync2), GLFW, vk-bootstrap, GLM. Dependencies come in via CMake FetchContent; the Vulkan SDK is installed at `C:\VulkanSDK\1.4.350.0`.

## Build (Windows, MSVC + Ninja)

MSVC is not on PATH; chain through vcvars64:

```
cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build'
```

Run: `build\bushido.exe` (Esc quits, A/D move, Space/W jump).

VS Code: `.vscode/` has a default build task (Ctrl+Shift+B), a `configure` task, and F5 debug configs (`cppvsdbg`). IntelliSense reads `build/compile_commands.json`.

## Layout

- `src/main.cpp` — game loop (fixed 120 Hz timestep), input, world/player state
- `src/renderer.hpp/.cpp` — all Vulkan state; API is `beginFrame` / `drawQuad` / `endFrame`
- `shaders/` — GLSL, compiled to SPIR-V at build time by glslc into `build/shaders/`

## Current conventions (early, expected to change)

- World coordinates are Vulkan NDC directly (x right, y **down**); a camera/world-space transform should replace this once the arena is bigger than one screen.
- Quads are generated in the vertex shader from `gl_VertexIndex`; per-quad data goes in push constants (`QuadPush` — must match layout in `shaders/quad.vert`).
- Shaders are loaded via the `SHADER_DIR` compile definition (absolute path into the build tree) — dev-only scheme.
