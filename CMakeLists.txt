cmake_minimum_required(VERSION 3.24)
project(bushido LANGUAGES CXX)

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

FetchContent_MakeAvailable(glfw vk-bootstrap glm)

add_executable(bushido
    src/main.cpp
    src/renderer.cpp)

target_link_libraries(bushido PRIVATE
    Vulkan::Vulkan
    glfw
    vk-bootstrap::vk-bootstrap
    glm::glm)

if(MSVC)
    target_compile_options(bushido PRIVATE /W4)
endif()

# Dev-only: shaders are loaded from the build tree by absolute path. Replace
# with a proper asset path scheme once there are real assets to ship.
target_compile_definitions(bushido PRIVATE
    SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")

# Compile GLSL shaders to SPIR-V next to the executable.
set(SHADER_SOURCES
    shaders/quad.vert
    shaders/quad.frag)

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
