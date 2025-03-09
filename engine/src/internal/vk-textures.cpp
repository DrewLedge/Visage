#include "vk-textures.hpp"

#include "libraries/dvl.hpp"
#include "libraries/utils.hpp"
#include "libraries/vkhelper.hpp"
#include "stb_image.h"

namespace textures {
void VkTextures::init(VkhCommandPool commandPool, VkQueue gQueue, const swapchain::VkSwapChain* swap, scene::VkScene* scene) {
    m_commandPool = commandPool;
    m_gQueue = gQueue;

    m_swap = swap;
    m_scene = scene;

    m_maxFrames = m_swap->getMaxFrames();
    m_depthShadowFormat = vkh::findDepthFormat();
}

void VkTextures::createRenderTextures(bool rtEnabled, bool createShadow) {
    createCompTextures();

    size_t lightCount = (createShadow) ? m_scene->getLightCount() : 0;

    if (rtEnabled) {
        createRTTextures();
    } else {
        m_lighting.resize(m_maxFrames);
        m_wboit.resize(m_maxFrames);

        m_deferredDepth.resize(m_maxFrames);

        size_t colorCount = getDeferredColorCount();
        m_deferredColor.resize(colorCount);

        for (size_t i = 0; i < m_maxFrames; i++) {
            createLightingTextures(i);
            createWBOITTextures(i);

            if (lightCount) {
                createShadowTextures();
            }

            createDeferredTextures(i);
        }
    }
}

void VkTextures::loadMeshTextures() {
    std::cout << "- Loading model textures\n";

    size_t modelSize = m_scene->getModelCount();

    auto now = utils::now();

    // load the raw image data of each model
    size_t totalImages = 0;
    size_t totalTextures = 0;

    for (size_t i = 0; i < modelSize; i++) {
        size_t modelIndex = m_scene->getModelIndex(i);
        const tinygltf::Model* model = m_scene->getModel(modelIndex);

        totalImages += model->images.size();
        totalTextures += model->textures.size();

        loadModelTextures(model);
    }

    std::cout << "- Finished loading " << totalTextures << " textures, and " << totalImages << " images in: " << utils::durationString(utils::duration<milliseconds>(now)) << "\n";
    utils::sep();
}

void VkTextures::createNewShadowBatch() {
    for (uint32_t i = 0; i < m_maxFrames; i++) {
        vkh::Texture shadowMap{};
        shadowMap.arrayLayers = cfg::LIGHTS_PER_BATCH;

        vkh::createTexture(shadowMap, vkh::DEPTH, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cfg::SHADOW_WIDTH, cfg::SHADOW_HEIGHT);
        m_shadow.push_back(shadowMap);
    }
}

void VkTextures::resetShadowTextures() {
    m_shadow.clear();
}

void VkTextures::loadModelTextures(const tinygltf::Model* model) {
    std::vector<bool> imagesSRGB(model->images.size());

    for (const auto& material : model->materials) {
        if (material.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            int index = model->textures[material.pbrMetallicRoughness.baseColorTexture.index].source;
            imagesSRGB[index] = true;
        } else {
            utils::logWarning("Material: " + material.name + " doesnt have an albedo texture!");
        }

        if (material.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) {
            int index = model->textures[material.pbrMetallicRoughness.metallicRoughnessTexture.index].source;
            imagesSRGB[index] = false;
        } else {
            utils::logWarning("Material: " + material.name + " doesnt have a metallic roughness texture!");
        }

        if (material.normalTexture.index >= 0) {
            int index = model->textures[material.normalTexture.index].source;
            imagesSRGB[index] = false;
        } else {
            utils::logWarning("Material: " + material.name + " doesnt have a normal map!");
        }

        if (material.emissiveTexture.index >= 0) {
            int index = model->textures[material.emissiveTexture.index].source;
            imagesSRGB[index] = true;
        }

        if (material.occlusionTexture.index >= 0) {
            int index = model->textures[material.occlusionTexture.index].source;
            imagesSRGB[index] = false;
        }
    }

    for (size_t i = 0; i < model->images.size(); i++) {
        const tinygltf::Image& image = model->images[i];

        // only images with 4 channels are supported
        int channels = image.component;
        if (channels != 4) {
            throw std::runtime_error("Unsupported number of channels in image!");
        }

        // check if the image is fully opaque or not
        bool opaque = true;
        for (size_t j = 0; j < image.image.size(); j += 4) {
            if (image.image[j + 3] < 255) {
                opaque = false;
                break;
            }
        }

        MeshTexture meshTexture{};
        meshTexture.imageData = std::move(image.image);
        meshTexture.type = (imagesSRGB[i]) ? vkh::SRGB : vkh::UNORM;

        createMeshTexture(meshTexture, image.width, image.height, opaque);
    }
}

void VkTextures::createImageStagingBuffer(vkh::Texture& tex, const unsigned char* imgData) {
    size_t bpp = 4;
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(tex.width) * tex.height * bpp;

    vkh::createAndWriteHostBuffer(tex.stagingBuffer, imgData, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
}

void VkTextures::createImageStagingBufferHDR(vkh::Texture& tex, const float* imgData) {
    size_t bpp = 16;
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(tex.width) * tex.height * bpp;

    vkh::createAndWriteHostBuffer(tex.stagingBuffer, imgData, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
}

void VkTextures::createMeshTexture(const MeshTexture& meshTexture, uint32_t width, uint32_t height, bool opaque) {
    vkh::Texture tex{};
    tex.width = width;
    tex.height = height;
    tex.fullyOpaque = opaque;
    tex.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(tex.width, tex.height)))) + 1;

    createImageStagingBuffer(tex, meshTexture.imageData.data());

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    vkh::createTexture(tex, meshTexture.type, usage, tex.width, tex.height);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {tex.width, tex.height, 1};

    VkhCommandBuffer tempBuffer = vkh::beginSingleTimeCommands(m_commandPool);

    // copy image staging buffer into image
    vkh::transitionImageLayout(tempBuffer, tex, meshTexture.type, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(tempBuffer.v(), tex.stagingBuffer.buf.v(), tex.image.v(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    int mipWidth = tex.width;
    int mipHeight = tex.height;

    // create mipmaps for the image if enabled
    for (uint32_t j = 0; j < tex.mipLevels; j++) {
        vkh::transitionImageLayout(tempBuffer, tex, meshTexture.type, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, j);

        // if the cutrrent mip level isnt the last, blit the image to generate the next mip level
        // bliting is the process of transfering the image data from one image to another usually with a form of scaling or filtering
        if (j < tex.mipLevels - 1) {
            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = j;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};

            // divide the width and height by 2 if over 1
            blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};

            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = j + 1;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;
            vkCmdBlitImage(tempBuffer.v(), tex.image.v(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tex.image.v(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        }

        vkh::transitionImageLayout(tempBuffer, tex, meshTexture.type, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, j);

        // for the next mip level, divide the width and height by 2, unless theyre already 1
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    vkh::endSingleTimeCommands(tempBuffer, m_commandPool, m_gQueue);

    m_meshTextures.push_back(tex);
}

void VkTextures::getImageData(const std::string& path, vkh::Texture& t, unsigned char*& imgData) {
    int texWidth, texHeight, texChannels;
    imgData = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, 4);
    t.width = texWidth;
    t.height = texHeight;
    if (imgData == nullptr) {
        std::string error = stbi_failure_reason();
        throw std::runtime_error("failed to load LDR image: " + path + "! Reason: " + error);
    }
}

void VkTextures::getImageDataHDR(const std::string& path, vkh::Texture& t, float*& imgData) {
    int texWidth, texHeight, texChannels;
    imgData = stbi_loadf(path.c_str(), &texWidth, &texHeight, &texChannels, 4);
    t.width = texWidth;
    t.height = texHeight;
    if (imgData == nullptr) {
        std::string error = stbi_failure_reason();
        throw std::runtime_error("failed to load HDR image: " + path + "! Reason: " + error);
    }
}

void VkTextures::createTextureFromFile(vkh::Texture& tex, const std::string& path) {
    // load image data
    unsigned char* imageData = nullptr;
    getImageData(path, tex, imageData);

    // create staging buffer
    createImageStagingBuffer(tex, imageData);

    // create image
    vkh::createTexture(tex, vkh::UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, tex.width, tex.height);

    // copy staging buffer into image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {tex.width, tex.height, 1};

    VkhCommandBuffer tempBuffer = vkh::beginSingleTimeCommands(m_commandPool);
    vkh::transitionImageLayout(tempBuffer, tex, vkh::UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(tempBuffer.v(), tex.stagingBuffer.buf.v(), tex.image.v(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkh::transitionImageLayout(tempBuffer, tex, vkh::UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkh::endSingleTimeCommands(tempBuffer, m_commandPool, m_gQueue);

    // free image data
    stbi_image_free(imageData);
}

void VkTextures::createCubemapTextureFromFile(vkh::Texture& tex, const std::string& path) {
    tex.arrayLayers = 6;

    float* imageData = nullptr;
    getImageDataHDR(path, tex, imageData);

    createImageStagingBufferHDR(tex, imageData);

    // calculate the size of one face of the cubemap
    uint32_t faceWidth = tex.width / 4;
    uint32_t faceHeight = tex.height / 3;
    size_t bpp = 16;

    // ensure the atlas dimensions are valid for a horizontal cross layout
    if (faceHeight != faceWidth) {
        throw std::runtime_error("Cubemap atlas dimensions are invalid!!");
    }

    vkh::createTexture(tex, vkh::CUBEMAP, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, faceWidth, faceHeight);

    VkhCommandBuffer tempBuffer = vkh::beginSingleTimeCommands(m_commandPool);
    vkh::transitionImageLayout(tempBuffer, tex, vkh::CUBEMAP, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    std::array<VkBufferImageCopy, 6> regions;
    std::array<std::pair<uint32_t, uint32_t>, 6> faceOffsets = {{{2, 1}, {0, 1}, {1, 0}, {1, 2}, {1, 1}, {3, 1}}};

    for (uint32_t i = 0; i < regions.size(); i++) {
        VkBufferImageCopy& region = regions[i];

        uint32_t offsetX = faceOffsets[i].first * faceWidth;
        uint32_t offsetY = faceOffsets[i].second * faceHeight;

        region.bufferOffset = offsetY * tex.width * bpp + offsetX * bpp;
        region.bufferRowLength = tex.width;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = i;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {faceWidth, faceHeight, 1};

        vkCmdCopyBufferToImage(tempBuffer.v(), tex.stagingBuffer.buf.v(), tex.image.v(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    vkh::transitionImageLayout(tempBuffer, tex, vkh::CUBEMAP, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkh::endSingleTimeCommands(tempBuffer, m_commandPool, m_gQueue);

    // free image data
    stbi_image_free(imageData);
}

void VkTextures::createCompTextures() {
    size_t imageCount = m_swap->getImageCount();

    m_comp.clear();
    m_comp.resize(imageCount);

    for (size_t i = 0; i < imageCount; i++) {
        m_comp[i] = vkh::Texture(m_compSampleCount);
        vkh::createSwapTexture(m_comp[i], m_swap->getFormat(), VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_swap->getWidth(), m_swap->getHeight());
    }
}

void VkTextures::createRTTextures() {
    m_rt.resize(m_maxFrames);
    for (size_t i = 0; i < m_maxFrames; i++) {
        vkh::createTexture(m_rt[i], vkh::SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_swap->getWidth(), m_swap->getHeight());
    }
}

void VkTextures::createLightingTextures(size_t i) {
    vkh::createTexture(m_lighting[i], vkh::SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_swap->getWidth(), m_swap->getHeight());
}

void VkTextures::createWBOITTextures(size_t i) {
    vkh::createTexture(m_wboit[i], vkh::SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_swap->getWidth(), m_swap->getHeight());
}

void VkTextures::createShadowTextures() {
    size_t batches = m_scene->getShadowBatchCount();

    for (size_t j = 0; j < batches; j++) {
        vkh::Texture shadowMap{};
        shadowMap.arrayLayers = cfg::LIGHTS_PER_BATCH;

        vkh::createTexture(shadowMap, vkh::DEPTH, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cfg::SHADOW_WIDTH, cfg::SHADOW_HEIGHT);
        m_shadow.push_back(shadowMap);
    }
}

void VkTextures::createDeferredTextures(size_t i) {
    vkh::createTexture(m_deferredDepth[i], vkh::DEPTH, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_swap->getWidth(), m_swap->getHeight());

    for (size_t j = 0; j < 4; j++) {
        size_t texIndex = (i * 4) + j;

        vkh::TextureType type = (j == 0 || j == 3) ? vkh::SRGB : vkh::UNORM;

        m_deferredColorFormats[j] = vkh::getTextureFormat(type);
        vkh::createTexture(m_deferredColor[texIndex], type, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, m_swap->getWidth(), m_swap->getHeight());
    }
}
}  // namespace textures
