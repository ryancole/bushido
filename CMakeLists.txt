cmake_minimum_required(VERSION 3.24)
project(bushido LANGUAGES CXX)

# This project is built with the MSVC toolchain; other compilers are untested.
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR
        "bushido must be built with the MSVC build tools. "
        "Configure with a preset (cmake --preset msvc-debug) from an "
        "environment where vcvars64.bat has run.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4)
FetchContent_Declare(vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG v1.3.290)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1)
# Sound effects are synthesized at startup and music is decoded from the WAVs
# in assets/ — the core engine covers both, so the extra nodes stay off.
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.23)

# Dear ImGui ships no build system; its sources are compiled into a small
# static lib below (core + GLFW/Vulkan backends).
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.1)

# Header-only TOML parser, for the player's settings file.
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0)

# Jolt must use the dynamic CRT to match the rest of the build.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.6.0
    SOURCE_SUBDIR Build)

FetchContent_MakeAvailable(glfw vk-bootstrap glm JoltPhysics miniaudio imgui
                           tomlplusplus)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)

add_executable(bushido
    src/main.cpp
    src/renderer.cpp
    src/camera.cpp
    src/bot.cpp
    src/config.cpp
    src/characters/character.cpp
    src/characters/ronin.cpp
    src/characters/shinobi.cpp
    src/characters/oni.cpp
    src/characters/kensei.cpp
    src/weapons/weapon.cpp
    src/weapons/katana.cpp
    src/weapons/wakizashi.cpp
    src/weapons/odachi.cpp
    src/game.cpp
    src/input.cpp
    src/netplay/replay.cpp
    src/netplay/session.cpp
    src/netplay/simlink.cpp
    src/netplay/transport.cpp
    src/levels/level.cpp
    src/levels/dojo.cpp
    src/levels/hanami.cpp
    src/physics.cpp
    src/samurai.cpp
    src/audio.cpp)

# Headers are included relative to src/ ("game.hpp", "levels/level.hpp") so
# sources in subdirectories don't need ../ paths.
target_include_directories(bushido PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(bushido PRIVATE
    Vulkan::Vulkan
    glfw
    vk-bootstrap::vk-bootstrap
    glm::glm
    Jolt
    miniaudio
    imgui
    tomlplusplus::tomlplusplus
    ws2_32) # netplay's UDP transport (src/netplay/transport.cpp)

if(MSVC)
    target_compile_options(bushido PRIVATE /W4)
endif()

# Dev-only: shaders are loaded from the build tree and assets from the source
# tree, both by absolute path. Replace with a proper asset path scheme (and a
# copy-alongside-the-exe step) when the game ships.
target_compile_definitions(bushido PRIVATE
    SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
    ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)

# The music generator: renders assets/music/*.wav from the composition code in
# tools/musicgen.cpp. Deliberately *not* wired into the build — the WAVs are
# committed, so this is run by hand when a track changes:
#
#     build\musicgen.exe
#
# It's built with everything else (it costs one small translation unit) so it
# can't quietly rot, but nothing depends on its output at build time.
add_executable(musicgen tools/musicgen.cpp)
target_include_directories(musicgen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(musicgen PRIVATE
    MUSIC_OUT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/music")

if(MSVC)
    target_compile_options(musicgen PRIVATE /W4)
endif()

# Compile GLSL shaders to SPIR-V next to the executable.
set(SHADER_SOURCES
    shaders/cube.vert
    shaders/cube.frag)

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND Vulkan::glslc "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}" -o ${SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
        COMMENT "Compiling ${SHADER_NAME}")
    list(APPEND SPV_FILES ${SPV})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPV_FILES})
add_dependencies(bushido shaders)
