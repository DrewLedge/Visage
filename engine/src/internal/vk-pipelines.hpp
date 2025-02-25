#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

#include "internal/vk-descriptorsets.hpp"
#include "internal/vk-swapchain.hpp"
#include "internal/vk-textures.hpp"
#include "libraries/vkhelper.hpp"
#include "structures/pipeline.hpp"

namespace pipelines {
class VkPipelines {
public:
    // delete copying and moving
    VkPipelines() = default;
    VkPipelines(const VkPipelines&) = delete;
    VkPipelines& operator=(const VkPipelines&) = delete;
    VkPipelines(VkPipelines&&) = delete;
    VkPipelines& operator=(VkPipelines&&) = delete;

    void init(bool rtEnabled, VkDevice device, const swapchain::VkSwapChain* swap, const textures::VkTextures* textures, const descriptorsets::VkDescriptorSets* descs) noexcept;
    void createPipelines(bool createShadow);

    // getters
    [[nodiscard]] pipeline::PipelineData getDeferredPipe() const noexcept { return m_deferredPipeline; }
    [[nodiscard]] pipeline::PipelineData getLightingPipe() const noexcept { return m_lightingPipeline; }
    [[nodiscard]] pipeline::PipelineData getSkyboxPipe() const noexcept { return m_skyboxPipeline; }
    [[nodiscard]] pipeline::PipelineData getShadowPipe() const noexcept { return m_shadowPipeline; }
    [[nodiscard]] pipeline::PipelineData getCompPipe() const noexcept { return m_compPipeline; }
    [[nodiscard]] pipeline::PipelineData getWBOITPipe() const noexcept { return m_wboitPipeline; }
    [[nodiscard]] pipeline::PipelineData getRTPipe() const noexcept { return m_rtPipeline; }

private:
    std::array<VkVertexInputAttributeDescription, 9> m_objectInputAttrDesc{};

    pipeline::PipelineData m_deferredPipeline{};
    pipeline::PipelineData m_lightingPipeline{};
    pipeline::PipelineData m_skyboxPipeline{};
    pipeline::PipelineData m_shadowPipeline{};
    pipeline::PipelineData m_compPipeline{};
    pipeline::PipelineData m_wboitPipeline{};
    pipeline::PipelineData m_rtPipeline{};

    const swapchain::VkSwapChain* m_swap = nullptr;
    const textures::VkTextures* m_textures = nullptr;
    const descriptorsets::VkDescriptorSets* m_descs = nullptr;

    bool m_rtEnabled = false;
    VkDevice m_device{};

private:
    [[nodiscard]] std::vector<char> readFile(const std::string& filename) const;
    [[nodiscard]] VkhShaderModule createShaderMod(const std::string& name) const;
    void getObjectVertInputAttrDescriptions();

    void createRayTracingPipeline();
    void createDeferredPipeline();
    void createLightingPipeline();
    void createShadowPipeline();
    void createSkyboxPipeline();
    void createWBOITPipeline();
    void createCompositionPipeline();
};
}  // namespace pipelines
