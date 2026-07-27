#pragma once

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

struct GLFWwindow;

// Push constant block shared by cube.vert / cube.frag. Exactly 128 bytes —
// the guaranteed minimum push constant budget; do not grow it. The unlit flag
// rides in normal0.w, which the columns leave spare, for that reason: the
// block is full, and a bool of its own would push it over.
struct ObjectPush {
    glm::mat4 mvp;
    glm::vec4 normal0; // columns of the normal matrix (inverse-transpose of
    glm::vec4 normal1; // the model's upper 3x3), for correct lighting under
    glm::vec4 normal2; // rotation and non-uniform scale; normal0.w = unlit
    glm::vec4 color;
};

class Renderer {
public:
    void init(GLFWwindow* window);
    void shutdown();

    // Acquires a swapchain image and starts recording. Returns false when the
    // frame must be skipped (swapchain rebuild, minimized window).
    bool beginFrame();
    void setViewProj(const glm::mat4& viewProj) { m_viewProj = viewProj; }
    // Draws a shaded unit cube under `model` (any translate/rotate/scale).
    // `unlit` skips the diffuse term and emits `color` exactly as given, which
    // is what the sky shell wants — see levels/sky.cpp.
    void drawBox(const glm::mat4& model, const glm::vec4& color, bool unlit = false);
    void endFrame();

    void onResize() { m_resizeRequested = true; }

    float aspect() const {
        return m_swapchain.extent.height == 0
                   ? 1.0f
                   : static_cast<float>(m_swapchain.extent.width) /
                         static_cast<float>(m_swapchain.extent.height);
    }

private:
    void initImGui();
    void createSwapchain();
    void destroySwapchain();
    void recreateSwapchain();
    void createDepthResources();
    void destroyDepthResources();
    void createPipeline();

    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    GLFWwindow* m_window = nullptr;

    vkb::Instance m_instance;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    vkb::PhysicalDevice m_physicalDevice;
    vkb::Device m_device;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;

    vkb::Swapchain m_swapchain;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    // One per swapchain image: signaled by the submit that renders to that
    // image, waited on by present.
    std::vector<VkSemaphore> m_renderFinished;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;

    glm::mat4 m_viewProj{1.0f};
    Frame m_frames[kFramesInFlight];
    uint32_t m_frameIndex = 0;
    uint32_t m_imageIndex = 0;
    bool m_resizeRequested = false;
};
