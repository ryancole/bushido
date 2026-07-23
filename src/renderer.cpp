#include "renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void vkCheck(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed: VkResult " + std::to_string(result));
    }
}

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open " + path);
    }
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

void transitionImage(VkCommandBuffer cmd, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

} // namespace

void Renderer::init(GLFWwindow* window) {
    m_window = window;

    auto systemInfo = vkb::SystemInfo::get_system_info();
    bool validation = false;
#ifndef NDEBUG
    validation = systemInfo.has_value() && systemInfo->validation_layers_available;
#endif

    auto instanceResult = vkb::InstanceBuilder{}
                              .set_app_name("bushido")
                              .require_api_version(1, 3, 0)
                              .request_validation_layers(validation)
                              .use_default_debug_messenger()
                              .build();
    if (!instanceResult) {
        throw std::runtime_error("failed to create Vulkan instance: " + instanceResult.error().message());
    }
    m_instance = instanceResult.value();

    vkCheck(glfwCreateWindowSurface(m_instance.instance, window, nullptr, &m_surface),
            "glfwCreateWindowSurface");

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    auto physicalResult = vkb::PhysicalDeviceSelector{m_instance}
                              .set_surface(m_surface)
                              .set_minimum_version(1, 3)
                              .set_required_features_13(features13)
                              .select();
    if (!physicalResult) {
        throw std::runtime_error("no suitable GPU: " + physicalResult.error().message());
    }
    m_physicalDevice = physicalResult.value();
    std::printf("GPU: %s\n", m_physicalDevice.properties.deviceName);

    auto deviceResult = vkb::DeviceBuilder{m_physicalDevice}.build();
    if (!deviceResult) {
        throw std::runtime_error("failed to create device: " + deviceResult.error().message());
    }
    m_device = deviceResult.value();

    m_graphicsQueue = m_device.get_queue(vkb::QueueType::graphics).value();
    m_graphicsQueueFamily = m_device.get_queue_index(vkb::QueueType::graphics).value();

    createSwapchain();
    createPipeline();

    for (Frame& frame : m_frames) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
        vkCheck(vkCreateCommandPool(m_device.device, &poolInfo, nullptr, &frame.pool),
                "vkCreateCommandPool");

        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = frame.pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkCheck(vkAllocateCommandBuffers(m_device.device, &allocInfo, &frame.cmd),
                "vkAllocateCommandBuffers");

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCheck(vkCreateSemaphore(m_device.device, &semInfo, nullptr, &frame.imageAvailable),
                "vkCreateSemaphore");

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCheck(vkCreateFence(m_device.device, &fenceInfo, nullptr, &frame.inFlight),
                "vkCreateFence");
    }
}

void Renderer::createSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);

    auto swapchainResult = vkb::SwapchainBuilder{m_device}
                               .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM,
                                                    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                               .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                               .set_desired_extent(static_cast<uint32_t>(width),
                                                   static_cast<uint32_t>(height))
                               .set_old_swapchain(m_swapchain)
                               .build();
    if (!swapchainResult) {
        throw std::runtime_error("failed to create swapchain: " + swapchainResult.error().message());
    }
    vkb::destroy_swapchain(m_swapchain);
    m_swapchain = swapchainResult.value();
    m_swapchainImages = m_swapchain.get_images().value();
    m_swapchainImageViews = m_swapchain.get_image_views().value();

    m_renderFinished.resize(m_swapchainImages.size());
    for (VkSemaphore& sem : m_renderFinished) {
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCheck(vkCreateSemaphore(m_device.device, &semInfo, nullptr, &sem), "vkCreateSemaphore");
    }
}

void Renderer::destroySwapchain() {
    for (VkSemaphore sem : m_renderFinished) {
        vkDestroySemaphore(m_device.device, sem, nullptr);
    }
    m_renderFinished.clear();
    m_swapchain.destroy_image_views(m_swapchainImageViews);
    m_swapchainImageViews.clear();
    vkb::destroy_swapchain(m_swapchain);
    m_swapchain = {};
}

void Renderer::recreateSwapchain() {
    // Wait out minimization: a zero-sized framebuffer cannot back a swapchain.
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(m_window, &width, &height);
    }

    vkDeviceWaitIdle(m_device.device);

    for (VkSemaphore sem : m_renderFinished) {
        vkDestroySemaphore(m_device.device, sem, nullptr);
    }
    m_renderFinished.clear();
    m_swapchain.destroy_image_views(m_swapchainImageViews);
    m_swapchainImageViews.clear();

    createSwapchain();
    m_resizeRequested = false;
}

void Renderer::createPipeline() {
    auto vertCode = readFile(std::string(SHADER_DIR) + "/quad.vert.spv");
    auto fragCode = readFile(std::string(SHADER_DIR) + "/quad.frag.spv");
    VkShaderModule vertModule = createShaderModule(m_device.device, vertCode);
    VkShaderModule fragModule = createShaderModule(m_device.device, fragCode);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(QuadPush);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCheck(vkCreatePipelineLayout(m_device.device, &layoutInfo, nullptr, &m_pipelineLayout),
            "vkCreatePipelineLayout");

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkFormat colorFormat = m_swapchain.image_format;
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;

    vkCheck(vkCreateGraphicsPipelines(m_device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                      &m_pipeline),
            "vkCreateGraphicsPipelines");

    vkDestroyShaderModule(m_device.device, vertModule, nullptr);
    vkDestroyShaderModule(m_device.device, fragModule, nullptr);
}

bool Renderer::beginFrame() {
    Frame& frame = m_frames[m_frameIndex];

    vkCheck(vkWaitForFences(m_device.device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX),
            "vkWaitForFences");

    if (m_resizeRequested) {
        recreateSwapchain();
    }

    VkResult acquire = vkAcquireNextImageKHR(m_device.device, m_swapchain.swapchain, UINT64_MAX,
                                             frame.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquire, "vkAcquireNextImageKHR");
    }

    vkCheck(vkResetFences(m_device.device, 1, &frame.inFlight), "vkResetFences");
    vkCheck(vkResetCommandBuffer(frame.cmd, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(frame.cmd, &beginInfo), "vkBeginCommandBuffer");

    transitionImage(frame.cmd, m_swapchainImages[m_imageIndex],
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = m_swapchainImageViews[m_imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.043f, 0.043f, 0.078f, 1.0f}};

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, m_swapchain.extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    vkCmdBeginRendering(frame.cmd, &renderingInfo);

    VkViewport viewport{};
    viewport.width = static_cast<float>(m_swapchain.extent.width);
    viewport.height = static_cast<float>(m_swapchain.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, m_swapchain.extent};
    vkCmdSetScissor(frame.cmd, 0, 1, &scissor);

    vkCmdBindPipeline(frame.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    return true;
}

void Renderer::drawQuad(const QuadPush& quad) {
    Frame& frame = m_frames[m_frameIndex];
    vkCmdPushConstants(frame.cmd, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(QuadPush), &quad);
    vkCmdDraw(frame.cmd, 6, 1, 0, 0);
}

void Renderer::endFrame() {
    Frame& frame = m_frames[m_frameIndex];

    vkCmdEndRendering(frame.cmd);

    transitionImage(frame.cmd, m_swapchainImages[m_imageIndex],
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

    vkCheck(vkEndCommandBuffer(frame.cmd), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = m_renderFinished[m_imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = frame.cmd;

    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;
    vkCheck(vkQueueSubmit2(m_graphicsQueue, 1, &submitInfo, frame.inFlight), "vkQueueSubmit2");

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinished[m_imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain.swapchain;
    presentInfo.pImageIndices = &m_imageIndex;
    VkResult present = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
        m_resizeRequested = true;
    } else {
        vkCheck(present, "vkQueuePresentKHR");
    }

    m_frameIndex = (m_frameIndex + 1) % kFramesInFlight;
}

void Renderer::shutdown() {
    vkDeviceWaitIdle(m_device.device);

    for (Frame& frame : m_frames) {
        vkDestroyFence(m_device.device, frame.inFlight, nullptr);
        vkDestroySemaphore(m_device.device, frame.imageAvailable, nullptr);
        vkDestroyCommandPool(m_device.device, frame.pool, nullptr);
    }

    vkDestroyPipeline(m_device.device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device.device, m_pipelineLayout, nullptr);

    destroySwapchain();

    vkb::destroy_device(m_device);
    vkDestroySurfaceKHR(m_instance.instance, m_surface, nullptr);
    vkb::destroy_instance(m_instance);
}
