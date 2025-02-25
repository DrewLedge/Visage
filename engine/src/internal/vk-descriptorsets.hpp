#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

#include "structures/descriptorsets.hpp"
#include "vk-buffers.hpp"
#include "vk-scene.hpp"
#include "vk-textures.hpp"

namespace descriptorsets {
enum class PASSES {
    DEFERRED,
    SHADOW,
    LIGHTING,
    SKYBOX,
    WBOIT,
    COMP,
    RT
};

class VkDescriptorSets {
public:
    // delete copying and moving
    VkDescriptorSets() = default;
    VkDescriptorSets(const VkDescriptorSets&) = delete;
    VkDescriptorSets& operator=(const VkDescriptorSets&) = delete;
    VkDescriptorSets(VkDescriptorSets&&) = delete;
    VkDescriptorSets& operator=(VkDescriptorSets&&) = delete;

    void init(bool rtEnabled, uint32_t maxFrames, VkDevice device, const scene::VkScene* scene, const textures::VkTextures* textures, const buffers::VkBuffers* buffers, const VkAccelerationStructureKHR* tlasData);
    void update(bool updateLights, const VkAccelerationStructureKHR* tlasData);
    void updateLightDS();

    // getters
    [[nodiscard]] std::vector<VkDescriptorSetLayout> getLayouts(PASSES pass) const;
    [[nodiscard]] std::vector<VkDescriptorSet> getSets(PASSES pass) const;

    // shadow infos
    void clearShadowInfos(uint32_t newSize) {
        m_shadowInfos.clear();
        m_shadowInfos.reserve(newSize);
    }
    void addShadowInfo(VkDescriptorImageInfo shadowInfo) { m_shadowInfos.push_back(shadowInfo); }

private:
    enum SET {
        TLAS,
        RT,
        TEXINDICES,
        MATERIALTEXTURES,
        DEFERRED,
        SHADOWMAP,
        CAMDEPTH,
        CAMDATA,
        LIGHTS,
        COMPTEXTURES,
        KNOWN
    };

private:
    const std::unordered_map<PASSES, std::vector<SET>> m_passSets = {
        {PASSES::DEFERRED, {MATERIALTEXTURES, TEXINDICES, CAMDATA}},
        {PASSES::SHADOW, {LIGHTS}},
        {PASSES::LIGHTING, {DEFERRED, LIGHTS, SHADOWMAP, CAMDATA, CAMDEPTH}},
        {PASSES::SKYBOX, {KNOWN, CAMDATA}},
        {PASSES::WBOIT, {MATERIALTEXTURES, LIGHTS, SHADOWMAP, CAMDATA, CAMDEPTH, TEXINDICES}},
        {PASSES::COMP, {RT, COMPTEXTURES}},
        {PASSES::RT, {MATERIALTEXTURES, LIGHTS, KNOWN, CAMDATA, RT, TLAS, TEXINDICES}},
    };

    std::array<desc::DescriptorSet, 11> m_sets{};
    std::vector<VkDescriptorImageInfo> m_shadowInfos;
    uint32_t m_totalTextureCount = 0;

    const scene::VkScene* m_scene = nullptr;
    const textures::VkTextures* m_textures = nullptr;
    const buffers::VkBuffers* m_buffers = nullptr;

    bool m_rtEnabled = false;
    uint32_t m_maxFrames = 0;
    VkDevice m_device{};

private:
    void createDescriptorSet(desc::DescriptorSet& obj, bool variableDescriptorCount);
    void createDescriptorInfo(desc::DescriptorSet& obj, VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t binding, uint32_t descriptorCount);
    void initDSInfo();
    void createDescriptorSets();
};
}  // namespace descriptorsets
