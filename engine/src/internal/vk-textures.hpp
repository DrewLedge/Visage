#pragma once

#include <vulkan/vulkan.h>

#include "libraries/dvl.hpp"
#include "libraries/vkhelper.hpp"
#include "vk-scene.hpp"
#include "vk-swapchain.hpp"

namespace textures {
class VkTextures {
public:
    // delete copying and moving
    VkTextures() = default;
    VkTextures(const VkTextures&) = delete;
    VkTextures& operator=(const VkTextures&) = delete;
    VkTextures(VkTextures&&) = delete;
    VkTextures& operator=(VkTextures&&) = delete;

    void init(uint32_t maxFrames, VkhCommandPool commandPool, VkQueue gQueue, const swapchain::VkSwapChain* swap, scene::VkScene* scene);
    void createRenderTextures(bool rtEnabled, bool createShadow);
    void loadMeshTextures();

    void loadSkybox(const std::string& fileName) { createCubemapTextureFromFile(m_skyboxCubemap, cfg::SKYBOX_DIR + fileName); }

    void createNewShadowBatch();
    void resetShadowTextures();

    // mesh textures
    [[nodiscard]] vkh::Texture getMeshTex(size_t index) const noexcept { return m_meshTextures[index]; }
    [[nodiscard]] size_t getMeshTexCount() const noexcept { return m_meshTextures.size(); }

    // render textures
    [[nodiscard]] vkh::Texture getCompTex(size_t index) const noexcept { return m_comp[index]; }
    [[nodiscard]] vkh::Texture getRTTex(size_t index) const noexcept { return m_rt[index]; }
    [[nodiscard]] vkh::Texture getLightingTex(size_t index) const noexcept { return m_lighting[index]; }
    [[nodiscard]] vkh::Texture getWboitTex(size_t index) const noexcept { return m_wboit[index]; }
    [[nodiscard]] vkh::Texture getDeferredColorTex(size_t index) const noexcept { return m_deferredColor[index]; }
    [[nodiscard]] vkh::Texture getDeferredDepthTex(size_t index) const noexcept { return m_deferredDepth[index]; }
    [[nodiscard]] vkh::Texture getShadowTex(size_t batchIndex, size_t currentFrame) const noexcept { return m_shadow[currentFrame + (batchIndex * m_maxFrames)]; }

    [[nodiscard]] const vkh::Texture* getCompTextures() const noexcept { return m_comp.data(); }
    [[nodiscard]] size_t getCompTexCount() const noexcept { return m_comp.size(); }
    [[nodiscard]] constexpr VkSampleCountFlagBits getCompSampleCount() const noexcept { return m_compSampleCount; }

    [[nodiscard]] VkFormat getDeferredColorFormat(size_t index) const noexcept { return m_deferredColorFormats[index]; }
    [[nodiscard]] size_t getDeferredColorCount() const noexcept { return m_maxFrames * 4; }

    // other
    [[nodiscard]] vkh::Texture getSkyboxCubemap() const noexcept { return m_skyboxCubemap; }

    [[nodiscard]] VkFormat getDepthFormat() const noexcept { return m_depthShadowFormat; }

private:
    struct MeshTexture {
        std::vector<unsigned char> imageData;
        vkh::TextureType type;
    };

private:
    static constexpr VkSampleCountFlagBits m_compSampleCount = VK_SAMPLE_COUNT_8_BIT;

    std::vector<vkh::Texture> m_rt{};

    std::vector<vkh::Texture> m_comp{};
    std::vector<vkh::Texture> m_lighting{};
    std::vector<vkh::Texture> m_wboit{};
    std::vector<vkh::Texture> m_shadow{};
    std::vector<vkh::Texture> m_deferredColor{};
    std::vector<vkh::Texture> m_deferredDepth{};

    vkh::Texture m_skyboxCubemap{};
    std::string m_skyboxPath{};

    std::array<VkFormat, 4> m_deferredColorFormats{};
    VkFormat m_depthShadowFormat{};

    std::vector<vkh::Texture> m_meshTextures;

    const swapchain::VkSwapChain* m_swap = nullptr;
    scene::VkScene* m_scene = nullptr;

    VkhCommandPool m_commandPool{};
    VkQueue m_gQueue{};
    uint32_t m_maxFrames = 0;

private:
    void loadModelTextures(const tinygltf::Model* model);

    void createImageStagingBuffer(vkh::Texture& tex, const unsigned char* imgData);
    void createImageStagingBufferHDR(vkh::Texture& tex, const float* imgData);

    void createMeshTexture(const MeshTexture& meshTexture, uint32_t width, uint32_t height, bool opaque);

    void getImageData(const std::string& path, vkh::Texture& t, unsigned char*& imgData);
    void getImageDataHDR(const std::string& path, vkh::Texture& t, float*& imgData);

    void createTextureFromFile(vkh::Texture& tex, const std::string& path);
    void createCubemapTextureFromFile(vkh::Texture& tex, const std::string& path);

    void createCompTextures();
    void createRTTextures();
    void createLightingTextures(size_t i);
    void createWBOITTextures(size_t i);
    void createShadowTextures();
    void createDeferredTextures(size_t i);
};
}  // namespace textures
