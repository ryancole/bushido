#include "paths.hpp"

#include <cstdio>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

// The directory holding the running exe. GetModuleFileName rather than
// argv[0], which is whatever the launcher felt like passing and is not a path
// at all when the game is started from a shell's PATH.
const std::filesystem::path& exeDir() {
    static const std::filesystem::path dir = [] {
        wchar_t buf[MAX_PATH] = {};
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::filesystem::path exe(buf, buf + n);
        return exe.has_parent_path() ? exe.parent_path() : std::filesystem::path(".");
    }();
    return dir;
}

// Beside the exe if it is there, else where it was at build time. Resolved
// once and announced, because the failure this exists to prevent — a friend
// getting no sound, or no window, and nobody able to say why — is invisible
// otherwise.
std::filesystem::path resolve(const char* name, const char* builtIn) {
    std::error_code ec;
    std::filesystem::path beside = exeDir() / name;
    const bool packaged = std::filesystem::exists(beside, ec);
    std::filesystem::path chosen = packaged ? beside : std::filesystem::path(builtIn);
    std::fprintf(stderr, "paths: %s -> %s (%s)\n", name, chosen.string().c_str(),
                 packaged ? "packaged" : "build tree");
    return chosen;
}

} // namespace

std::string shaderPath(const char* file) {
    static const std::filesystem::path root = resolve("shaders", SHADER_DIR);
    return (root / file).string();
}

std::string assetPath(const char* relative) {
    static const std::filesystem::path root = resolve("assets", ASSET_DIR);
    return (root / relative).string();
}
