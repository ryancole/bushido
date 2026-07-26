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
    src/paths.cpp
    src/netplay/steamlobby.cpp
    src/netplay/steamtransport.cpp
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

# Steam networking. Every other dependency here arrives by FetchContent; this
# one cannot, because the Steamworks SDK is licence-gated — you accept an
# agreement and download it while signed in. So it is opt-in: point
# STEAM_SDK_DIR at the extracted `sdk` directory (the one holding public/ and
# redistributable_bin/) and the Steam transport compiles in. Without it the
# game still builds and still plays over direct IP; SteamTransport stubs out
# and reports itself unavailable.
set(STEAM_SDK_DIR "" CACHE PATH "Steamworks SDK 'sdk' directory (optional)")
if(STEAM_SDK_DIR)
    if(NOT EXISTS "${STEAM_SDK_DIR}/public/steam/steam_api.h")
        message(FATAL_ERROR
            "STEAM_SDK_DIR is set to '${STEAM_SDK_DIR}' but "
            "public/steam/steam_api.h is not there. It should be the 'sdk' "
            "directory from inside the Steamworks SDK zip.")
    endif()
    # SYSTEM: the SDK headers trip /W4 (deprecated strncpy in matchmakingtypes.h)
    # and they are not ours to fix.
    target_include_directories(bushido SYSTEM PRIVATE "${STEAM_SDK_DIR}/public")
    target_link_libraries(bushido PRIVATE
        "${STEAM_SDK_DIR}/redistributable_bin/win64/steam_api64.lib")
    target_compile_definitions(bushido PRIVATE BUSHIDO_STEAM)
    # SteamAPI_Init reads the app id from a file in the working directory. 480
    # is Valve's public test app ("Spacewar"), which is what you develop
    # against until the game has an id of its own.
    set(STEAM_APP_ID "480" CACHE STRING "Steam app id used at runtime")
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:bushido>/steam_appid.txt"
         CONTENT "${STEAM_APP_ID}")
    # steam_api64.dll has to sit beside the exe or it will not start.
    add_custom_command(TARGET bushido POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${STEAM_SDK_DIR}/redistributable_bin/win64/steam_api64.dll"
                "$<TARGET_FILE_DIR:bushido>"
        COMMENT "Copying steam_api64.dll beside bushido.exe")
    message(STATUS "Steam networking: ENABLED (${STEAM_SDK_DIR}), app id ${STEAM_APP_ID}")
else()
    message(STATUS "Steam networking: disabled (set STEAM_SDK_DIR to enable)")
endif()

# `cmake --build --preset msvc-release --target package` assembles a folder
# that runs on a machine that has never seen this source tree. The build
# directory itself is not that: it is hundreds of megabytes of intermediates,
# and the exe inside it only finds its shaders because the absolute path they
# had at compile time still happens to exist here (see src/paths.hpp).
set(DIST_DIR "${CMAKE_CURRENT_BINARY_DIR}/dist")
set(PACKAGE_STEPS
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:bushido>" "${DIST_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/shaders" "${DIST_DIR}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${DIST_DIR}/assets")
if(STEAM_SDK_DIR)
    list(APPEND PACKAGE_STEPS
        COMMAND ${CMAKE_COMMAND} -E copy
                "${STEAM_SDK_DIR}/redistributable_bin/win64/steam_api64.dll"
                "${DIST_DIR}/"
        # Needed beside the exe for SteamAPI_Init until the game has a real
        # app id and is launched by Steam itself.
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_BINARY_DIR}/steam_appid.txt" "${DIST_DIR}/")
endif()
add_custom_target(package ${PACKAGE_STEPS}
    DEPENDS bushido
    COMMENT "Assembling ${DIST_DIR}")

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
