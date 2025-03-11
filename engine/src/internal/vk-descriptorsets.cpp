#include "vk-descriptorsets.hpp"

#include "libraries/vkhelper.hpp"

namespace descriptorsets {
void VkDescriptorSets::init(bool rtEnabled, uint32_t maxFrames, VkDevice device, const scene::VkScene* scene, const textures::VkTextures* textures, const buffers::VkBuffers* buffers, const VkAccelerationStructureKHR* tlasData) {
    m_rtEnabled = rtEnabled;
    m_maxFrames = maxFrames;
    m_device = device;

    m_scene = scene;
    m_textures = textures;
    m_buffers = buffers;

    m_totalTextureCount = static_cast<uint32_t>(m_textures->getMeshTexCount());
    createDescriptorSets();
    update(true, tlasData);
}

void VkDescriptorSets::update(bool updateLights, const VkAccelerationStructureKHR* tlasData) {
    VkDescriptorBufferInfo texIndexInfo{};
    texIndexInfo.buffer = m_buffers->getTexIndicesBuffer().buf.v();
    texIndexInfo.offset = 0;
    texIndexInfo.range = sizeof(texindices::TexIndices);

    std::vector<VkDescriptorImageInfo> imageInfos;
    imageInfos.reserve(m_totalTextureCount);

    for (size_t i = 0; i < m_totalTextureCount; i++) {
        vkh::Texture tex = m_textures->getMeshTex(i);
        imageInfos.push_back(vkh::createDSImageInfo(tex.imageView, tex.sampler));
    }

    std::vector<VkDescriptorBufferInfo> lightBufferInfos{};
    if (updateLights) {
        lightBufferInfos.reserve(m_maxFrames);
        for (uint32_t i = 0; i < m_maxFrames; i++) {
            VkDescriptorBufferInfo linfo{};
            linfo.buffer = m_buffers->getLightBuffer(i).buf.v();
            linfo.offset = 0;
            linfo.range = sizeof(light::RawLights);
            lightBufferInfos.push_back(linfo);
        }
    }

    std::vector<VkDescriptorBufferInfo> camBufferInfos{};
    camBufferInfos.reserve(m_maxFrames);

    for (uint32_t i = 0; i < m_maxFrames; i++) {
        VkDescriptorBufferInfo cinfo{};
        cinfo.buffer = m_buffers->getCamBuffer(i).buf.v();
        cinfo.offset = 0;
        cinfo.range = sizeof(cam::CamMatrices);
        camBufferInfos.push_back(cinfo);
    }

    vkh::Texture skybox = m_textures->getSkyboxCubemap();
    VkDescriptorImageInfo skyboxInfo = vkh::createDSImageInfo(skybox.imageView, skybox.sampler);

    // rasterization
    std::vector<VkDescriptorImageInfo> compositionPassImageInfo{};
    std::vector<VkDescriptorImageInfo> deferredImageInfo{};
    std::vector<VkDescriptorImageInfo> depthInfo{};

    // raytracing
    std::vector<VkDescriptorImageInfo> rtPresentTextures{};
    VkWriteDescriptorSetAccelerationStructureKHR tlasInfo{};

    if (m_rtEnabled) {
        rtPresentTextures.reserve(m_maxFrames);

        for (size_t i = 0; i < m_maxFrames; i++) {
            const vkh::Texture& rt = m_textures->getRTTex(i);
            rtPresentTextures.push_back(vkh::createDSImageInfo(rt.imageView, rt.sampler, VK_IMAGE_LAYOUT_GENERAL));
        }

        tlasInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        tlasInfo.pAccelerationStructures = tlasData;
        tlasInfo.accelerationStructureCount = m_maxFrames;
    } else {
        compositionPassImageInfo.reserve(m_maxFrames * 2);
        deferredImageInfo.reserve(m_textures->getDeferredColorCount());
        depthInfo.reserve(m_maxFrames);

        if (updateLights) {
            size_t batchCount = m_scene->getShadowBatchCount();
            m_shadowInfos.reserve(m_maxFrames * batchCount);

            for (size_t i = 0; i < batchCount; i++) {
                for (size_t j = 0; j < m_maxFrames; j++) {
                    const vkh::Texture& tex = m_textures->getShadowTex(i, j);
                    m_shadowInfos.push_back(vkh::createDSImageInfo(tex.imageView, tex.sampler));
                }
            }
        }

        for (size_t i = 0; i < m_maxFrames; i++) {
            for (size_t j = 0; j < 4; j++) {
                size_t k = (i * 4) + j;

                const vkh::Texture& tex = m_textures->getDeferredColorTex(k);
                deferredImageInfo.push_back(vkh::createDSImageInfo(tex.imageView, tex.sampler));
            }

            const vkh::Texture& deferredDepthT = m_textures->getDeferredDepthTex(i);
            const vkh::Texture& lightingT = m_textures->getLightingTex(i);
            const vkh::Texture& wboitT = m_textures->getWboitTex(i);

            depthInfo.push_back(vkh::createDSImageInfo(deferredDepthT.imageView, deferredDepthT.sampler));
            compositionPassImageInfo.push_back(vkh::createDSImageInfo(lightingT.imageView, lightingT.sampler));
            compositionPassImageInfo.push_back(vkh::createDSImageInfo(wboitT.imageView, wboitT.sampler));
        }
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites{};
    if (m_rtEnabled) {
        descriptorWrites.push_back(vkh::createDSWrite(m_sets[RT].set, 0, m_sets[RT].bindings[0].descriptorType, rtPresentTextures.data(), rtPresentTextures.size()));
        descriptorWrites.push_back(vkh::createDSWrite(m_sets[TLAS].set, 0, m_sets[TLAS].bindings[0].descriptorType, &tlasInfo, m_maxFrames));
    } else {
        descriptorWrites.push_back(vkh::createDSWrite(m_sets[DEFERRED].set, 0, m_sets[DEFERRED].bindings[0].descriptorType, deferredImageInfo.data(), deferredImageInfo.size()));

        // if any lights are loaded
        if (updateLights && m_shadowInfos.size() > 0) {
            descriptorWrites.push_back(vkh::createDSWrite(m_sets[SHADOWMAP].set, 0, m_sets[SHADOWMAP].bindings[0].descriptorType, m_shadowInfos.data(), m_shadowInfos.size()));
        }

        descriptorWrites.push_back(vkh::createDSWrite(m_sets[CAMDEPTH].set, 0, m_sets[CAMDEPTH].bindings[0].descriptorType, depthInfo.data(), depthInfo.size()));
        descriptorWrites.push_back(vkh::createDSWrite(m_sets[COMPTEXTURES].set, 0, m_sets[COMPTEXTURES].bindings[0].descriptorType, compositionPassImageInfo.data(), compositionPassImageInfo.size()));
    }

    descriptorWrites.push_back(vkh::createDSWrite(m_sets[TEXINDICES].set, 0, m_sets[TEXINDICES].bindings[0].descriptorType, texIndexInfo));
    descriptorWrites.push_back(vkh::createDSWrite(m_sets[MATERIALTEXTURES].set, 0, m_sets[MATERIALTEXTURES].bindings[0].descriptorType, imageInfos.data(), imageInfos.size()));
    descriptorWrites.push_back(vkh::createDSWrite(m_sets[CAMDATA].set, 0, m_sets[CAMDATA].bindings[0].descriptorType, camBufferInfos.data(), camBufferInfos.size()));
    if (updateLights) descriptorWrites.push_back(vkh::createDSWrite(m_sets[LIGHTS].set, 0, m_sets[LIGHTS].bindings[0].descriptorType, lightBufferInfos.data(), lightBufferInfos.size()));
    descriptorWrites.push_back(vkh::createDSWrite(m_sets[KNOWN].set, 0, m_sets[KNOWN].bindings[0].descriptorType, skyboxInfo));

    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
}

void VkDescriptorSets::updateLightDS() {
    VkWriteDescriptorSet dw = vkh::createDSWrite(m_sets[SHADOWMAP].set, 0, m_sets[SHADOWMAP].bindings[0].descriptorType, m_shadowInfos.data(), m_shadowInfos.size());
    vkUpdateDescriptorSets(m_device, 1, &dw, 0, nullptr);
}

std::vector<VkDescriptorSetLayout> VkDescriptorSets::getLayouts(PASSES pass) const {
    const std::vector<SET> setTypes = m_passSets.at(pass);

    std::vector<VkDescriptorSetLayout> outLayouts;
    outLayouts.reserve(setTypes.size());

    for (size_t i = 0; i < setTypes.size(); i++) {
        SET type = setTypes[i];
        const desc::DescriptorSet& d = m_sets[type];
        outLayouts.push_back(d.layout.v());
    }

    return outLayouts;
}

std::vector<VkDescriptorSet> VkDescriptorSets::getSets(PASSES pass) const {
    const std::vector<SET> setTypes = m_passSets.at(pass);

    std::vector<VkDescriptorSet> outSets;
    outSets.reserve(setTypes.size());

    for (size_t i = 0; i < setTypes.size(); i++) {
        SET type = setTypes[i];
        const desc::DescriptorSet& d = m_sets[type];
        outSets.push_back(d.set.v());
    }

    return outSets;
}

void VkDescriptorSets::createDescriptorSet(desc::DescriptorSet& obj, bool variableDescriptorCount) {
    obj.set.reset();

    vkh::createDSLayout(obj.layout, obj.bindings.data(), obj.bindings.size(), variableDescriptorCount, false);
    vkh::createDSPool(obj.pool, obj.poolSizes.data(), obj.poolSizes.size());

    uint32_t size = 0;
    if (variableDescriptorCount) {
        size = obj.bindings.back().descriptorCount;
    }

    obj.set = vkh::allocDS(obj.layout, obj.pool, size);
}

void VkDescriptorSets::createDescriptorInfo(desc::DescriptorSet& obj, VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t binding, uint32_t descriptorCount) {
    obj.bindings.push_back(vkh::createDSLayoutBinding(binding, descriptorCount, type, stageFlags));
    obj.poolSizes.push_back(vkh::createDSPoolSize(descriptorCount, type));
}

void VkDescriptorSets::initDSInfo() {
    VkShaderStageFlags textursSS{};
    VkShaderStageFlags lightDataSS{};
    VkShaderStageFlags skyboxSS{};
    VkShaderStageFlags camSS{};

    if (m_rtEnabled) {
        textursSS = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        lightDataSS = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        skyboxSS = VK_SHADER_STAGE_MISS_BIT_KHR;
        camSS = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    } else {
        textursSS = VK_SHADER_STAGE_FRAGMENT_BIT;
        lightDataSS = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        skyboxSS = VK_SHADER_STAGE_FRAGMENT_BIT;
        camSS = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    uint32_t deferredColorCount = static_cast<uint32_t>(m_textures->getDeferredColorCount());

    createDescriptorInfo(m_sets[RT], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_FRAGMENT_BIT, 0, m_maxFrames);
    createDescriptorInfo(m_sets[TLAS], VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0, m_maxFrames);

    createDescriptorInfo(m_sets[TEXINDICES], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, textursSS, 0, cfg::MAX_OBJECTS);
    createDescriptorInfo(m_sets[MATERIALTEXTURES], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textursSS, 0, m_totalTextureCount);
    createDescriptorInfo(m_sets[CAMDATA], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, camSS, 0, m_maxFrames);
    createDescriptorInfo(m_sets[LIGHTS], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, lightDataSS, 0, m_maxFrames);
    createDescriptorInfo(m_sets[DEFERRED], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, deferredColorCount);
    createDescriptorInfo(m_sets[SHADOWMAP], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, cfg::MAX_LIGHTS * m_maxFrames);
    createDescriptorInfo(m_sets[CAMDEPTH], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, m_maxFrames);
    createDescriptorInfo(m_sets[COMPTEXTURES], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, m_maxFrames * 2);

    createDescriptorInfo(m_sets[KNOWN], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, skyboxSS, 0, 1);
}

void VkDescriptorSets::createDescriptorSets() {
    initDSInfo();

    if (m_rtEnabled) {
        createDescriptorSet(m_sets[RT], true);
        createDescriptorSet(m_sets[TLAS], true);
    } else {
        createDescriptorSet(m_sets[DEFERRED], true);
        createDescriptorSet(m_sets[SHADOWMAP], true);
        createDescriptorSet(m_sets[CAMDEPTH], true);
        createDescriptorSet(m_sets[COMPTEXTURES], true);
    }

    createDescriptorSet(m_sets[TEXINDICES], true);
    createDescriptorSet(m_sets[MATERIALTEXTURES], true);
    createDescriptorSet(m_sets[CAMDATA], true);
    createDescriptorSet(m_sets[LIGHTS], true);
    createDescriptorSet(m_sets[KNOWN], false);
}
}  // namespace descriptorsets
