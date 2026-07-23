#pragma once

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>

#include <cstdint>
#include <vector>

struct GLFWwindow;

// Push constant block shared by quad.vert / quad.frag. Layout must match the
// shader: vec2 offset, vec2 scale, vec4 color (32 bytes).
struct QuadPush {
    float offset[2];
    float scale[2];
    float color[4];
};

class Renderer {
public:
    void init(GLFWwindow* window);
    void shutdown();

    // Acquires a swapchain image and starts recording. Returns false when the
    // frame must be skipped (swapchain rebuild, minimized window).
    bool beginFrame();
    void drawQuad(const QuadPush& quad);
    void endFrame();

    void onResize() { m_resizeRequested = true; }

private:
    void createSwapchain();
    void destroySwapchain();
    void recreateSwapchain();
    void createPipeline();

    static constexpr uint32_t kFramesInFlight = 2;

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

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    Frame m_frames[kFramesInFlight];
    uint32_t m_frameIndex = 0;
    uint32_t m_imageIndex = 0;
    bool m_resizeRequested = false;
};
