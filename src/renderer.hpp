#pragma once

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

struct GLFWwindow;

// Push constant block shared by cube.vert / cube.frag (80 bytes; the
// guaranteed minimum push constant budget is 128).
struct ObjectPush {
    glm::mat4 mvp;
    glm::vec4 color;
};

class Renderer {
public:
    void init(GLFWwindow* window);
    void shutdown();

    // Acquires a swapchain image and starts recording. Returns false when the
    // frame must be skipped (swapchain rebuild, minimized window).
    bool beginFrame();
    void drawBox(const ObjectPush& object);
    void endFrame();

    void onResize() { m_resizeRequested = true; }

    float aspect() const {
        return m_swapchain.extent.height == 0
                   ? 1.0f
                   : static_cast<float>(m_swapchain.extent.width) /
                         static_cast<float>(m_swapchain.extent.height);
    }

private:
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

    Frame m_frames[kFramesInFlight];
    uint32_t m_frameIndex = 0;
    uint32_t m_imageIndex = 0;
    bool m_resizeRequested = false;
};
