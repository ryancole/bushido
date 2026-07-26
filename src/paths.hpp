#pragma once

#include <string>

// Where the shaders and the assets actually are, at runtime.
//
// SHADER_DIR and ASSET_DIR are absolute paths baked in when the game is
// compiled. That is convenient on the machine that built it and useless
// anywhere else: a copy handed to someone else looks for its shaders in a
// directory that exists only on the build machine, and dies before the window
// opens. So the exe's own directory wins when it has what we want — a packaged
// build carries `shaders/` and `assets/` beside the exe and is self-contained
// — and the compile-time paths are the fallback, so a dev build still needs
// nothing copied after every compile.
std::string shaderPath(const char* file);
std::string assetPath(const char* relative);
