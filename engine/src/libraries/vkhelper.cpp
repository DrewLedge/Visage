#include "vkhelper.hpp"

#include <algorithm>
#include <stdexcept>

namespace vkh {
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const VkSurfaceFormatKHR& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const VkPresentModeKHR& present : availablePresentModes) {
        if (present == VK_PRESENT_MODE_MAILBOX_KHR) {
            return present;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

QueueFamilyIndices findQueueFamilyIndices(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice) {
    QueueFamilyIndices indices;

    // get the number of queue families and their properties
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    // iterate through all the queue families
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        const auto& family = queueFamilies[i];

        // check if the queue family supports graphics, compute and transfer operations
        if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) indices.graphicsFamily = i;
        if (family.queueFlags & VK_QUEUE_COMPUTE_BIT) indices.computeFamily = i;
        if (family.queueFlags & VK_QUEUE_TRANSFER_BIT) indices.transferFamily = i;

        // check if the queue family supports presentation operations
        VkBool32 presSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presSupport);
        if (presSupport) indices.presentFamily = i;

        if (indices.allComplete()) {
            break;
        }
    }

    return indices;
}

SCsupportDetails querySCsupport() {
    SCsupportDetails details;

    VkSurfaceKHR surface = VkSingleton::v().gsurface();
    VkPhysicalDevice physicalDevice = VkSingleton::v().gphysicalDevice();

    // get the surface capabilities of the physical device
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);

    // get the number of supported surface formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);

    // if the format count isnt 0, get the surface format details
    // the surface format specifies the color space and pixel format
    if (formatCount) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, details.formats.data());
    }

    // get the number of supported present modes
    // present modes determine how the swapping of images to the display is handled
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

    // if any present modes are supported, get the present mode details
    if (presentModeCount) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height) {
    VkExtent2D extent = {width, height};

    // clamp the width and height to the min and max image extent
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return extent;
}

VkhCommandPool createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags) {
    VkhCommandPool commandPool;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = createFlags;

    VkResult result = vkCreateCommandPool(VkSingleton::v().gdevice(), &poolInfo, nullptr, commandPool.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
    return commandPool;
}

VkhCommandBuffer allocateCommandBuffers(const VkhCommandPool& commandPool, VkCommandBufferLevel level) {
    VkhCommandBuffer commandBuffer(commandPool.v());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool.v();
    allocInfo.level = level;
    allocInfo.commandBufferCount = 1;

    VkResult result = vkAllocateCommandBuffers(VkSingleton::v().gdevice(), &allocInfo, commandBuffer.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffer!!");
    }
    return commandBuffer;
}

VkhCommandBuffer beginSingleTimeCommands(const VkhCommandPool& commandPool) {
    VkhCommandBuffer commandBuffer = allocateCommandBuffers(commandPool);
    commandBuffer.setDestroy(false);  // command buffer wont be autodestroyed

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;  // one time command buffer

    vkBeginCommandBuffer(commandBuffer.v(), &beginInfo);
    return commandBuffer;
}

void endSingleTimeCommands(VkhCommandBuffer& commandBuffer, const VkhCommandPool& commandPool, VkQueue queue) {
    vkEndCommandBuffer(commandBuffer.v());
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandBuffer.p();

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);  // submit the command buffer to the queue
    vkQueueWaitIdle(queue);                                // wait for the queue to be idle
    vkFreeCommandBuffers(VkSingleton::v().gdevice(), commandPool.v(), 1, commandBuffer.p());
}

void createFB(const VkhRenderPass& renderPass, VkhFramebuffer& frameBuf, const VkImageView* attachments, size_t attachmentCount, uint32_t width, uint32_t height) {
    frameBuf.reset();

    VkFramebufferCreateInfo frameBufferInfo{};
    frameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferInfo.renderPass = renderPass.v();
    frameBufferInfo.attachmentCount = static_cast<uint32_t>(attachmentCount);
    frameBufferInfo.pAttachments = attachments;
    frameBufferInfo.width = width;
    frameBufferInfo.height = height;
    frameBufferInfo.layers = 1;

    if (vkCreateFramebuffer(VkSingleton::v().gdevice(), &frameBufferInfo, nullptr, frameBuf.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
    }
}

VkhSemaphore createSemaphore() {
    VkhSemaphore result{};

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(VkSingleton::v().gdevice(), &semaphoreInfo, nullptr, result.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphore!");
    }

    return result;
}

VkSubmitInfo createSubmitInfo(const VkCommandBuffer* commandBuffers, size_t commandBufferCount) {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;
    submitInfo.pCommandBuffers = commandBuffers;
    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBufferCount);
    submitInfo.pSignalSemaphores = nullptr;
    submitInfo.signalSemaphoreCount = 0;
    return submitInfo;
}

VkSubmitInfo createSubmitInfo(const VkCommandBuffer* commandBuffers, size_t commandBufferCount, const VkPipelineStageFlags* waitStages, const VkhSemaphore& wait, const VkhSemaphore& signal) {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = wait.p();
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.pCommandBuffers = commandBuffers;
    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBufferCount);
    submitInfo.pSignalSemaphores = signal.p();
    submitInfo.signalSemaphoreCount = 1;
    return submitInfo;
}

VkSubmitInfo createSubmitInfo(const VkCommandBuffer* commandBuffers, size_t commandBufferCount, const VkPipelineStageFlags* waitStages, const VkSemaphore* wait, const VkSemaphore* signal, size_t waitSemaphoreCount, size_t signalSemaphoreCount) {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphoreCount);
    submitInfo.pWaitSemaphores = wait;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.pCommandBuffers = commandBuffers;
    submitInfo.commandBufferCount = static_cast<uint32_t>(commandBufferCount);
    submitInfo.pSignalSemaphores = signal;
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphoreCount);
    return submitInfo;
}

uint32_t findMemoryType(uint32_t memTypeBits, VkMemoryPropertyFlags memPropertyFlags) {
    // get memory properties
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(VkSingleton::v().gphysicalDevice(), &memProperties);

    // iterate over memory types
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        // check if the current memory bit for i is in the memory bits
        // if memoryBitmask is 0, the type isnt supported
        uint32_t currentMemoryBit = (1 << i);
        uint32_t memoryBitmask = memTypeBits & currentMemoryBit;
        bool memoryBitAvailable = memoryBitmask != 0;

        // check if the memory property flags are supported by the physical device
        VkMemoryPropertyFlags supportedMemPropertyFlags = memProperties.memoryTypes[i].propertyFlags & memPropertyFlags;
        bool memFlagsAvailable = supportedMemPropertyFlags == memPropertyFlags;

        if (memoryBitAvailable && memFlagsAvailable) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

VkDeviceAddress bufferDeviceAddress(const VkhBuffer& buffer) {
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = buffer.v();

    return vkGetBufferDeviceAddress(VkSingleton::v().gdevice(), &addrInfo);
}

VkDeviceAddress asDeviceAddress(const VkhAccelerationStructure& accelerationStructure) {
    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = accelerationStructure.v();

    return vkhfp::vkGetAccelerationStructureDeviceAddressKHR(VkSingleton::v().gdevice(), &addrInfo);
}

void allocateMemory(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags memPropertyFlags, VkDeviceMemory* memory, VkMemoryAllocateFlags memAllocFlags) {
    // memory allocation flags
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    if (memAllocFlags) {
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = memAllocFlags;
    }

    // memory allocation info
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, memPropertyFlags);
    allocInfo.pNext = (memAllocFlags) ? allocInfo.pNext = &allocFlagsInfo : nullptr;

    VkDevice device = VkSingleton::v().gdevice();
    VkPhysicalDevice physicalDevice = VkSingleton::v().gphysicalDevice();

    // get the memory budget
    VkPhysicalDeviceMemoryBudgetPropertiesEXT memBudget{};
    memBudget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

    VkPhysicalDeviceMemoryProperties2 memProperties2{};
    memProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    memProperties2.pNext = &memBudget;

    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memProperties2);

    // get the heap index
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    uint32_t heapIndex = memProperties.memoryTypes[allocInfo.memoryTypeIndex].heapIndex;

    // if heap usage is over the budget, throw runtime error
    if (memBudget.heapUsage[heapIndex] > memBudget.heapBudget[heapIndex]) {
        throw std::runtime_error("device ran out of memory!");
    }

    VkResult memoryAllocResult = vkAllocateMemory(device, &allocInfo, nullptr, memory);
    if (memoryAllocResult != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate memory!");
    }
}

void copyBuffer(VkhBuffer& src, VkhBuffer& dst, const VkhCommandPool& commandPool, VkQueue queue, VkDeviceSize size) {
    VkhCommandBuffer commandBuffer = beginSingleTimeCommands(commandPool);
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer.v(), src.v(), dst.v(), 1, &copyRegion);
    endSingleTimeCommands(commandBuffer, commandPool, queue);
}

void createBuffer(BufferObj& buffer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, VkMemoryAllocateFlags memAllocFlags) {
    buffer.reset();

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = VkSingleton::v().gdevice();

    // create the buffer
    if (vkCreateBuffer(device, &bufferCreateInfo, nullptr, buffer.buf.p()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    // get the memory requirements for the buffer
    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(device, buffer.buf.v(), &memoryRequirements);

    // allocate memory for the buffer
    allocateMemory(memoryRequirements, memFlags, buffer.mem.p(), memAllocFlags);

    if (vkBindBufferMemory(device, buffer.buf.v(), buffer.mem.v(), 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed to bind memory to buffer!");
    }
}

void createHostVisibleBuffer(BufferObj& buffer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryAllocateFlags memAllocFlags) {
    VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    createBuffer(buffer, size, usage, memFlags, memAllocFlags);
}

void createDeviceLocalBuffer(BufferObj& buffer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryAllocateFlags memAllocFlags) {
    VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    createBuffer(buffer, size, usage, memFlags, memAllocFlags);
}

VkFormat findDepthFormat() {
    // the formats that are supported
    std::vector<VkFormat> allowed = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};

    for (VkFormat format : allowed) {
        // get the format properties
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(VkSingleton::v().gphysicalDevice(), format, &props);

        // if the format has the depth stencil attachment bit
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }

    throw std::runtime_error("failed to find suitable depth format!");
}

LayoutTransition getLayoutTransition(VkImageLayout oldLayout, VkImageLayout newLayout) {
    LayoutTransition out{};

    switch (oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            out.srcAccessMask = 0;
            out.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            out.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            out.srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            out.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            out.srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            out.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            out.srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            out.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            out.srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;

        default:
            throw std::invalid_argument("Unsupported old layout!");
    }

    switch (newLayout) {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            out.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            out.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            out.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            out.dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            out.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            out.dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            out.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            out.dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            break;

        default:
            throw std::invalid_argument("Unsupported new layout!");
    }

    return out;
}

void transitionImageLayout(const VkhCommandBuffer& commandBuffer, const VkhImage& image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount, uint32_t levelCount, uint32_t baseMip) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.v();

    // if the format is a depth format
    if (format == VK_FORMAT_D32_SFLOAT) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    } else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.baseMipLevel = baseMip;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

    // get layout transition
    LayoutTransition transition = getLayoutTransition(oldLayout, newLayout);
    barrier.srcAccessMask = transition.srcAccessMask;
    barrier.dstAccessMask = transition.dstAccessMask;

    // insert the barrier into the command buffer
    vkCmdPipelineBarrier(commandBuffer.v(), transition.srcStage, transition.dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void transitionImageLayout(const VkhCommandBuffer& commandBuffer, const Texture& tex, TextureType textureType, VkImageLayout oldLayout, VkImageLayout newLayout) {
    transitionImageLayout(commandBuffer, tex.image, getTextureFormat(textureType), oldLayout, newLayout, tex.arrayLayers, tex.mipLevels, 0);
}

void transitionImageLayout(const VkhCommandBuffer& commandBuffer, const Texture& tex, TextureType textureType, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t baseMip) {
    transitionImageLayout(commandBuffer, tex.image, getTextureFormat(textureType), oldLayout, newLayout, tex.arrayLayers, mipLevels, baseMip);
}

void createImage(VkhImage& image, VkhDeviceMemory& imageMemory, uint32_t width, uint32_t height, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers, bool cubeMap, VkImageUsageFlags usage, VkSampleCountFlagBits sample) {
    image.reset();
    imageMemory.reset();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.mipLevels = mipLevels;
    imageInfo.format = format;

    // allows the gpu to format the image data in memory in the most efficient way
    // this means that the cpu cant easily read or write to the image though
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = sample;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (cubeMap) imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VkDevice device = VkSingleton::v().gdevice();

    if (vkCreateImage(device, &imageInfo, nullptr, image.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create color image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image.v(), &memRequirements);

    // allocate memory for the image
    allocateMemory(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, imageMemory.p());

    vkBindImageMemory(device, image.v(), imageMemory.v(), 0);
}

void createImage(VkhImage& image, VkhDeviceMemory& imageMemory, uint32_t width, uint32_t height, TextureType textureType, uint32_t mipLevels, uint32_t arrayLayers, bool cubeMap, VkImageUsageFlags usage, VkSampleCountFlagBits sample) {
    VkFormat format = getTextureFormat(textureType);
    createImage(image, imageMemory, width, height, format, mipLevels, arrayLayers, cubeMap, usage, sample);
}

void createSampler(VkhSampler& sampler, uint32_t mipLevels, TextureType type) {
    sampler.reset();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    switch (type) {
        case DEPTH:
            // when the texture coords go out of bounds, clamp the uv coords to the edge of the texture
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

            // instead of directly returning a sampled image, the sampler will compare a refrence value to the sampled value
            // this is useful for shadowmapping
            samplerInfo.compareEnable = VK_TRUE;
            samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case CUBEMAP:
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            break;
        default:
            // when the texture coords go out of bounds, repeat the texture
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);

    if (vkCreateSampler(VkSingleton::v().gdevice(), &samplerInfo, nullptr, sampler.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }
}

void createImageView(Texture& tex, TextureType type) {
    if (tex.imageView.valid()) tex.imageView.reset();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image.v();
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = tex.arrayLayers;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.subresourceRange.aspectMask = (type == DEPTH) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.format = getTextureFormat(type);

    if (type == CUBEMAP) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    } else if (tex.arrayLayers > 1) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }

    viewInfo.subresourceRange.baseMipLevel = 0;
    uint32_t level = (tex.mipLevels <= 0) ? 1 : tex.mipLevels;
    viewInfo.subresourceRange.levelCount = level - viewInfo.subresourceRange.baseMipLevel;
    if (vkCreateImageView(VkSingleton::v().gdevice(), &viewInfo, nullptr, tex.imageView.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image view!");
    }
}

void createImageView(Texture& tex, VkFormat format) {
    if (tex.imageView.valid()) tex.imageView.reset();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image.v();
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = tex.arrayLayers;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;

    if (tex.arrayLayers > 1) {
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }

    uint32_t level = (tex.mipLevels <= 0) ? 1 : tex.mipLevels;
    viewInfo.subresourceRange.levelCount = level - viewInfo.subresourceRange.baseMipLevel;
    if (vkCreateImageView(VkSingleton::v().gdevice(), &viewInfo, nullptr, tex.imageView.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image view! (swap)");
    }
}

void createTexture(Texture& tex, TextureType textureType, VkImageUsageFlags usage, uint32_t width, uint32_t height) {
    bool cubemap = (textureType == CUBEMAP);

    createImage(tex.image, tex.memory, width, height, textureType, tex.mipLevels, tex.arrayLayers, cubemap, usage, tex.sampleCount);
    createImageView(tex, textureType);
    createSampler(tex.sampler, tex.mipLevels, textureType);
}

void createSwapTexture(Texture& tex, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height) {
    createImage(tex.image, tex.memory, width, height, format, tex.mipLevels, tex.arrayLayers, false, usage, tex.sampleCount);
    createImageView(tex, format);
    createSampler(tex.sampler, tex.mipLevels, BASE);
}

VkFormat getTextureFormat(TextureType textureType) {
    switch (textureType) {
        case SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case DEPTH:
            return findDepthFormat();
        case SFLOAT16:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case SFLOAT32:
        case CUBEMAP:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case ALPHA:
            return VK_FORMAT_R32_SFLOAT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

void createDSLayout(VkhDescriptorSetLayout& layout, const VkDescriptorSetLayoutBinding* bindings, size_t bindingCount, bool variableDescriptorCount, bool pushDescriptors) {
    layout.reset();

    uint32_t count = static_cast<uint32_t>(bindingCount);

    std::vector<VkDescriptorBindingFlags> bindingFlags(count);
    if (variableDescriptorCount) {
        // set the last element to be variable descriptor count
        bindingFlags.back() |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();
    bindingFlagsInfo.bindingCount = count;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pBindings = bindings;
    layoutInfo.bindingCount = count;
    layoutInfo.pNext = &bindingFlagsInfo;
    if (pushDescriptors) layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;

    if (vkCreateDescriptorSetLayout(VkSingleton::v().gdevice(), &layoutInfo, nullptr, layout.p()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

void createDSPool(VkhDescriptorPool& pool, const VkDescriptorPoolSize* poolSizes, size_t poolSizeCount) {
    pool.reset();

    uint32_t count = static_cast<uint32_t>(poolSizeCount);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.poolSizeCount = count;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(VkSingleton::v().gdevice(), &poolInfo, nullptr, pool.p()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

VkhDescriptorSet allocDS(VkhDescriptorSetLayout& layout, const VkhDescriptorPool& pool, uint32_t variableCount) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.descriptorPool = pool.v();
    allocInfo.pSetLayouts = layout.p();

    VkDescriptorSetVariableDescriptorCountAllocateInfo varCountInfo{};
    if (variableCount) {
        varCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        varCountInfo.descriptorSetCount = 1;
        varCountInfo.pDescriptorCounts = &variableCount;
        allocInfo.pNext = &varCountInfo;
    }

    VkhDescriptorSet set(pool.v());
    VkResult result = vkAllocateDescriptorSets(VkSingleton::v().gdevice(), &allocInfo, set.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set!");
    }

    return set;
}

VkDescriptorImageInfo createDSImageInfo(const VkhImageView& imageView, const VkhSampler& sampler, VkImageLayout layout) {
    VkDescriptorImageInfo info{};
    info.imageLayout = layout;
    info.imageView = imageView.v();
    info.sampler = sampler.v();

    return info;
}

VkDescriptorSetLayoutBinding createDSLayoutBinding(uint32_t binding, size_t count, VkDescriptorType type, VkShaderStageFlags stageFlags) {
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorCount = static_cast<uint32_t>(count);
    layoutBinding.descriptorType = type;
    layoutBinding.stageFlags = stageFlags;

    return layoutBinding;
}

VkDescriptorPoolSize createDSPoolSize(size_t count, VkDescriptorType type) {
    VkDescriptorPoolSize poolSize{};
    poolSize.descriptorCount = static_cast<uint32_t>(count);
    poolSize.type = type;

    return poolSize;
}

VkhShaderModule createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());  // convert the char array to uint32_t array

    VkhShaderModule shaderModule;
    if (vkCreateShaderModule(VkSingleton::v().gdevice(), &createInfo, nullptr, shaderModule.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

VkPipelineShaderStageCreateInfo createShaderStage(VkShaderStageFlagBits stage, const VkhShaderModule& shaderModule) {
    VkPipelineShaderStageCreateInfo shader{};
    shader.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader.stage = stage;
    shader.module = shaderModule.v();
    shader.pName = "main";

    return shader;
}

VkVertexInputBindingDescription vertInputBindDesc(uint32_t binding, uint32_t stride, VkVertexInputRate inputRate) {
    VkVertexInputBindingDescription inputBindingDesc{};
    inputBindingDesc.binding = binding;
    inputBindingDesc.stride = stride;
    inputBindingDesc.inputRate = inputRate;

    return inputBindingDesc;
}

VkVertexInputAttributeDescription vertInputAttrDesc(VkFormat format, uint32_t binding, uint32_t location, size_t offset) {
    VkVertexInputAttributeDescription inputAttrDesc{};
    inputAttrDesc.format = format;
    inputAttrDesc.binding = binding;
    inputAttrDesc.location = location;
    inputAttrDesc.offset = static_cast<uint32_t>(offset);

    return inputAttrDesc;
}

VkPipelineVertexInputStateCreateInfo vertInputInfo(const VkVertexInputBindingDescription* bindingDescriptions, size_t bindingDescriptionCount, const VkVertexInputAttributeDescription* attrDescriptions, size_t attrDescriptionCount) {
    VkPipelineVertexInputStateCreateInfo inputInfo{};
    inputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    inputInfo.pVertexBindingDescriptions = bindingDescriptions;
    inputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptionCount);
    inputInfo.pVertexAttributeDescriptions = attrDescriptions;
    inputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescriptionCount);

    return inputInfo;
}
}  // namespace vkh
