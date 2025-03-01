#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "internal/vk-buffers.hpp"
#include "internal/vk-descriptorsets.hpp"
#include "internal/vk-pipelines.hpp"
#include "internal/vk-raytracing.hpp"
#include "internal/vk-scene.hpp"
#include "internal/vk-setup.hpp"
#include "internal/vk-swapchain.hpp"
#include "internal/vk-textures.hpp"
#include "libraries/vkhelper.hpp"
#include "structures/commandbuffers.hpp"
#include "structures/pushconstants.hpp"

namespace renderer {
class VkRenderer {
public:
    // delete copying and moving
    VkRenderer() = default;
    VkRenderer(const VkRenderer&) = delete;
    VkRenderer& operator=(const VkRenderer&) = delete;
    VkRenderer(VkRenderer&&) = delete;
    VkRenderer& operator=(VkRenderer&&) = delete;

    void init(bool rtEnabled, bool showDebugInfo, VkDevice device, const setup::VkSetup* setup, const swapchain::VkSwapChain* swap, const textures::VkTextures* textures, const scene::VkScene* scene, const buffers::VkBuffers* buffers, const descriptorsets::VkDescriptorSets* descs, const pipelines::VkPipelines* pipelines, const raytracing::VkRaytracing* raytracing) noexcept;
    void createCommandBuffers();
    void createFrameBuffers(bool shadow);
    [[nodiscard]] VkResult drawFrame(uint32_t currentFrame, float fps);

    // lights
    void freeLights();
    void addShadowFrameBuffer(const vkh::Texture& tex);
    void addShadowCommandBuffers();

    // getters
    [[nodiscard]] const VkFence* getFence(uint32_t frame) const noexcept { return m_fences[frame].p(); }
    [[nodiscard]] VkhCommandPool getCommandPool() const noexcept { return m_commandPool; }
    [[nodiscard]] VkSemaphore getImageAvailableSemaphore(uint32_t frame) const noexcept { return m_imageAvailableSemaphores[frame].v(); }

private:
    // vulkan
    const setup::VkSetup* m_setup = nullptr;
    const swapchain::VkSwapChain* m_swap = nullptr;
    const textures::VkTextures* m_textures = nullptr;
    const scene::VkScene* m_scene = nullptr;
    const buffers::VkBuffers* m_buffers = nullptr;
    const descriptorsets::VkDescriptorSets* m_descs = nullptr;
    const pipelines::VkPipelines* m_pipe = nullptr;
    const raytracing::VkRaytracing* m_raytracing = nullptr;

    // framebuffers
    std::vector<VkhFramebuffer> m_lightingFB{};
    std::vector<VkhFramebuffer> m_shadowFB{};
    std::vector<VkhFramebuffer> m_wboitFB{};
    std::vector<VkhFramebuffer> m_deferredFB{};
    std::vector<VkhFramebuffer> m_swapFB{};

    // command buffers
    VkhCommandPool m_commandPool{};
    commandbuffers::CommandBufferSet m_deferredCB{};
    commandbuffers::CommandBufferSet m_lightingCB{};
    commandbuffers::CommandBufferSet m_shadowCB{};
    commandbuffers::CommandBufferSet m_wboitCB{};
    commandbuffers::CommandBufferSet m_compCB{};
    commandbuffers::CommandBufferSet m_rtCB{};
    std::vector<VkCommandBuffer> m_frameShadowCommandBuffers;

    // synchronization primitives
    std::vector<VkhFence> m_fences;
    std::vector<VkhSemaphore> m_imageAvailableSemaphores{};
    std::vector<VkhSemaphore> m_renderFinishedSemaphores{};
    std::vector<VkhSemaphore> m_deferredSemaphores{};
    std::vector<VkhSemaphore> m_shadowSemaphores{};
    std::vector<VkhSemaphore> m_wboitSemaphores{};
    std::vector<VkhSemaphore> m_compSemaphores{};
    std::vector<VkhSemaphore> m_rtSemaphores{};

    // push constants
    pushconstants::FramePushConst m_framePushConst{};
    pushconstants::LightPushConst m_lightPushConst{};
    pushconstants::RTPushConst m_rtPushConst{};

    // other
    bool m_rtEnabled = false;
    bool m_showDebugInfo = false;

    VkDevice m_device{};
    uint32_t m_currentFrame = 0;
    float m_fps = 0.0f;

private:
    void setupFences();
    void createSemaphores();

    void allocateCommandBuffers(commandbuffers::CommandBufferSet& cmdBuffers, size_t primaryCount, size_t secondaryCount);

    void renderImguiFrame(VkhCommandBuffer& commandBuffer);

    // command buffer recording
    void recordObjectCommandBuffers(VkhCommandBuffer& secondary, const pipeline::PipelineData& pipe, const VkCommandBufferBeginInfo& beginInfo, const VkDescriptorSet* descriptorsets, size_t descriptorCount);
    void recordDeferredCommandBuffers();
    void recordShadowCommandBuffers();
    void recordLightingCommandBuffers();
    void recordWBOITCommandBuffers();
    void recordCompCommandBuffers();
    void recordRTCommandBuffers();

    void updatePushConstants() noexcept;
    void recordAllCommandBuffers();
};
}  // namespace renderer
