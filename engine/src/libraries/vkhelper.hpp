// A bunch of Vulkan helper functions for the project

#pragma once
#include <vulkan/vulkan.h>

#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

// -------------------- FUNCTION POINTERS -------------------- //
namespace vkhfp {
#define FUNCTIONS                                    \
    F(vkCreateAccelerationStructureKHR)              \
    F(vkDestroyAccelerationStructureKHR)             \
    F(vkGetAccelerationStructureBuildSizesKHR)       \
    F(vkCmdBuildAccelerationStructuresKHR)           \
    F(vkCmdWriteAccelerationStructuresPropertiesKHR) \
    F(vkCmdCopyAccelerationStructureKHR)             \
    F(vkGetAccelerationStructureDeviceAddressKHR)    \
    F(vkCmdPushDescriptorSetKHR)                     \
    F(vkCreateRayTracingPipelinesKHR)                \
    F(vkGetRayTracingShaderGroupHandlesKHR)          \
    F(vkCmdTraceRaysKHR)

#define F(name) inline PFN_##name name = nullptr;
FUNCTIONS
#undef F

template <typename T>
void loadFunc(VkInstance instance, T& ptr, const char* name) {
    ptr = reinterpret_cast<T>(vkGetInstanceProcAddr(instance, name));
    if (!ptr) std::cerr << name << " isnt supported!\n";
}

inline void loadFuncPointers(VkInstance instance) {
#define F(name) loadFunc(instance, name, #name);
    FUNCTIONS
#undef F
}
}  // namespace vkhfp

// -------------------- SINGLETON -------------------- //

class VkSingleton {
private:
    VkInstance instance{};
    VkDevice device{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physicalDevice{};

    VkSingleton() = default;

    // delete copy and move
    VkSingleton(const VkSingleton&) = delete;
    VkSingleton& operator=(const VkSingleton&) = delete;
    VkSingleton(VkSingleton&&) = delete;
    VkSingleton& operator=(VkSingleton&&) = delete;

    void cleanup() const {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    }

public:
    static VkSingleton& v() {
        static VkSingleton i;
        return i;
    }

    void init(VkInstance instance_, VkDevice device_, VkSurfaceKHR surface_, VkPhysicalDevice physicalDevice_) {
        instance = instance_;
        device = device_;
        surface = surface_;
        physicalDevice = physicalDevice_;
    }

    // getters
    VkInstance ginstance() const { return instance; }
    VkDevice gdevice() const { return device; }
    VkSurfaceKHR gsurface() const { return surface; }
    VkPhysicalDevice gphysicalDevice() const { return physicalDevice; }

    ~VkSingleton() {
        cleanup();
    }
};

// -------------------- RAII WRAPPER -------------------- //

template <typename Object, typename... Destroy>
struct VulkanDestroy;

template <typename Object, typename... Destroy>
class VulkanRAII {
public:
    // constructors
    explicit VulkanRAII(Destroy... args) : objectP(std::make_shared<VulkanObject>(VK_NULL_HANDLE, true, args...)) {}
    explicit VulkanRAII(Object obj, Destroy... args) : objectP(std::make_shared<VulkanObject>(obj, true, args...)) {}

    // destructor
    ~VulkanRAII() = default;

    // copy constructor and assignment operator
    VulkanRAII(const VulkanRAII& other) : objectP(other.objectP) {}
    VulkanRAII& operator=(const VulkanRAII& other) {
        if (this != &other) {
            objectP = other.objectP;
        }
        return *this;
    }

    // move constructor and assignment operator
    VulkanRAII(VulkanRAII&& other) noexcept = default;
    VulkanRAII& operator=(VulkanRAII&& other) noexcept = default;

    // equality operators
    bool operator==(const VulkanRAII& other) const noexcept { return objectP->object == other.objectP->object; }
    bool operator!=(const VulkanRAII& other) const noexcept { return !(*this == other); }

    const Object& v() const noexcept { return objectP->object; }
    const Object* p() const noexcept { return &objectP->object; }
    Object& v() noexcept { return objectP->object; }
    Object* p() noexcept { return &objectP->object; }

    bool valid() const noexcept { return objectP->object != VK_NULL_HANDLE; }
    size_t useCount() const noexcept { return objectP.use_count(); }
    void setDestroy(bool destruction) noexcept { objectP->autoDestroy = destruction; }

    void reset() {
        if (!valid()) return;

        if (useCount() != 1) {
            throw std::runtime_error("Cannot reset object in use!");
        }

        std::tuple<Destroy...> originalDestroyArgs = objectP->destroyArgs;
        objectP->destroy();
        std::apply([this](auto&&... args) { objectP = std::make_shared<VulkanObject>(VK_NULL_HANDLE, true, args...); }, originalDestroyArgs);
    }

private:
    struct VulkanObject {
        Object object;
        bool autoDestroy;
        std::tuple<Destroy...> destroyArgs;

        VulkanObject(Object obj, bool destroy, Destroy... args) : object(obj), autoDestroy(destroy), destroyArgs(args...) {}

        void destroy() {
            std::apply([this](auto&&... args) {
                VkDevice device = VkSingleton::v().gdevice();

                // no destroy args
                if constexpr (sizeof...(Destroy) == 0) {
                    if constexpr (std::is_same_v<Object, VkBuffer>) {
                        vkDestroyBuffer(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkDeviceMemory>) {
                        vkFreeMemory(device, object, nullptr);
                    }

                    else if constexpr (std::is_same_v<Object, VkImage>) {
                        vkDestroyImage(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkImageView>) {
                        vkDestroyImageView(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkSampler>) {
                        vkDestroySampler(device, object, nullptr);
                    }

                    else if constexpr (std::is_same_v<Object, VkCommandPool>) {
                        vkDestroyCommandPool(device, object, nullptr);
                    }

                    else if constexpr (std::is_same_v<Object, VkPipeline>) {
                        vkDestroyPipeline(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkPipelineLayout>) {
                        vkDestroyPipelineLayout(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkShaderModule>) {
                        vkDestroyShaderModule(device, object, nullptr);
                    }

                    else if constexpr (std::is_same_v<Object, VkDescriptorPool>) {
                        vkDestroyDescriptorPool(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkDescriptorSetLayout>) {
                        vkDestroyDescriptorSetLayout(device, object, nullptr);
                    }

                    else if constexpr (std::is_same_v<Object, VkRenderPass>) {
                        vkDestroyRenderPass(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkFramebuffer>) {
                        vkDestroyFramebuffer(device, object, nullptr);
                    }

                    else if constexpr (std::is_same_v<Object, VkSemaphore>) {
                        vkDestroySemaphore(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkFence>) {
                        vkDestroyFence(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkQueryPool>) {
                        vkDestroyQueryPool(device, object, nullptr);
                    }

                    else if constexpr (std::is_same_v<Object, VkSwapchainKHR>) {
                        vkDestroySwapchainKHR(device, object, nullptr);
                    } else if constexpr (std::is_same_v<Object, VkAccelerationStructureKHR>) {
                        vkhfp::vkDestroyAccelerationStructureKHR(device, object, nullptr);
                    }
                }

                // one destroy arg
                else if constexpr (sizeof...(Destroy) == 1) {
                    if constexpr (std::is_same_v<Object, VkCommandBuffer>) {
                        vkFreeCommandBuffers(device, std::get<0>(destroyArgs), 1, &object);
                    } else if constexpr (std::is_same_v<Object, VkDescriptorSet>) {
                        vkFreeDescriptorSets(device, std::get<0>(destroyArgs), 1, &object);
                    }
                }
            },
                       destroyArgs);
            object = VK_NULL_HANDLE;
        }

        // destructor
        ~VulkanObject() {
            if (autoDestroy && object != VK_NULL_HANDLE) {
                destroy();
            }
        }

        // disallow moving and copying
        VulkanObject(const VulkanObject&) = delete;
        VulkanObject& operator=(const VulkanObject&) = delete;
        VulkanObject(VulkanObject&&) = delete;
        VulkanObject& operator=(VulkanObject&&) = delete;
    };

    std::shared_ptr<VulkanObject> objectP;
};

#define RAII_NO_DESTROY_ARGS(Name, Type) using Name = VulkanRAII<Type>;
#define RAII_ONE_DESTROY_ARG(Name, MainType, DestroyType) using Name = VulkanRAII<MainType, DestroyType>;

// no destroy args
RAII_NO_DESTROY_ARGS(VkhBuffer, VkBuffer)
RAII_NO_DESTROY_ARGS(VkhDeviceMemory, VkDeviceMemory)

RAII_NO_DESTROY_ARGS(VkhImage, VkImage)
RAII_NO_DESTROY_ARGS(VkhImageView, VkImageView)
RAII_NO_DESTROY_ARGS(VkhSampler, VkSampler)

RAII_NO_DESTROY_ARGS(VkhCommandPool, VkCommandPool)

RAII_NO_DESTROY_ARGS(VkhDescriptorPool, VkDescriptorPool)
RAII_NO_DESTROY_ARGS(VkhDescriptorSetLayout, VkDescriptorSetLayout)

RAII_NO_DESTROY_ARGS(VkhPipeline, VkPipeline)
RAII_NO_DESTROY_ARGS(VkhPipelineLayout, VkPipelineLayout)
RAII_NO_DESTROY_ARGS(VkhShaderModule, VkShaderModule)

RAII_NO_DESTROY_ARGS(VkhRenderPass, VkRenderPass)
RAII_NO_DESTROY_ARGS(VkhFramebuffer, VkFramebuffer)

RAII_NO_DESTROY_ARGS(VkhSemaphore, VkSemaphore)
RAII_NO_DESTROY_ARGS(VkhFence, VkFence)
RAII_NO_DESTROY_ARGS(VkhQueryPool, VkQueryPool)

RAII_NO_DESTROY_ARGS(VkhSwapchainKHR, VkSwapchainKHR)

RAII_NO_DESTROY_ARGS(VkhAccelerationStructure, VkAccelerationStructureKHR)

// one destroy arg
RAII_ONE_DESTROY_ARG(VkhCommandBuffer, VkCommandBuffer, VkCommandPool)
RAII_ONE_DESTROY_ARG(VkhDescriptorSet, VkDescriptorSet, VkDescriptorPool)

#undef RAII_ONE_DESTROY_ARG
#undef RAII_NO_DESTROY_ARGS

namespace vkh {
typedef enum {
    BASE,
    SRGB,
    SFLOAT,
    UNORM,
    DEPTH,
    CUBEMAP,
    ALPHA
} TextureType;

struct BufferObj {
    VkhBuffer buf{};
    VkhDeviceMemory mem{};

    void reset() {
        buf.reset();
        mem.reset();
    }
};

struct Texture {
    // vulkan objects
    VkhSampler sampler{};
    VkhImage image{};
    VkhDeviceMemory memory{};
    VkhImageView imageView{};
    BufferObj stagingBuffer{};

    // image data
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    uint32_t width = 1024;
    uint32_t height = 1024;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    bool fullyOpaque = false;

    // consuructors
    Texture() = default;
    explicit Texture(VkSampleCountFlagBits s) : sampleCount{s} {}
};

struct BufData {
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;
    std::optional<uint32_t> transferFamily;

    bool graphicsComplete() const noexcept {
        return graphicsFamily.has_value();
    }

    bool presentComplete() const noexcept {
        return presentFamily.has_value();
    }

    bool computeComplete() const noexcept {
        return computeFamily.has_value();
    }

    bool transferComplete() const noexcept {
        return transferFamily.has_value();
    }

    bool allComplete() const noexcept {
        return graphicsComplete() && presentComplete() && computeComplete() && transferComplete();
    }
};

struct SCsupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct LayoutTransition {
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;
};

// -------------------- SWAP CHAIN -------------------- //
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

QueueFamilyIndices findQueueFamilyIndices(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice);

SCsupportDetails querySCsupport();

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);

// -------------------- COMMAND BUFFERS -------------------- //
VkhCommandPool createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags);

VkhCommandBuffer allocateCommandBuffers(const VkhCommandPool& commandPool, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

VkhCommandBuffer beginSingleTimeCommands(const VkhCommandPool& commandPool);

void endSingleTimeCommands(VkhCommandBuffer& commandBuffer, const VkhCommandPool& commandPool, VkQueue queue);

void createFB(const VkhRenderPass& renderPass, VkhFramebuffer& frameBuf, const VkImageView* attachments, size_t attachmentCount, uint32_t width, uint32_t height);

VkhSemaphore createSemaphore();

VkSubmitInfo createSubmitInfo(const VkCommandBuffer* commandBuffers, size_t commandBufferCount);
VkSubmitInfo createSubmitInfo(const VkCommandBuffer* commandBuffers, size_t commandBufferCount, const VkPipelineStageFlags* waitStages, const VkhSemaphore& wait, const VkhSemaphore& signal);
VkSubmitInfo createSubmitInfo(const VkCommandBuffer* commandBuffers, size_t commandBufferCount, const VkPipelineStageFlags* waitStages, const VkSemaphore* wait, const VkSemaphore* signal, size_t waitSemaphoreCount, size_t signalSemaphoreCount);

// -------------------- MEMORY -------------------- //
uint32_t findMemoryType(uint32_t memTypeBits, VkMemoryPropertyFlags memPropertyFlags);

VkDeviceAddress bufferDeviceAddress(const VkhBuffer& buffer);

VkDeviceAddress asDeviceAddress(const VkhAccelerationStructure& accelerationStructure);

void allocateMemory(VkMemoryRequirements memRequirements, VkMemoryPropertyFlags memPropertyFlags, VkDeviceMemory* memory, VkMemoryAllocateFlags memAllocFlags = 0);

void copyBuffer(VkhBuffer& src, VkhBuffer& dst, const VkhCommandPool& commandPool, VkQueue queue, VkDeviceSize size);

void createBuffer(BufferObj& buffer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, VkMemoryAllocateFlags memAllocFlags = 0);

void createHostVisibleBuffer(BufferObj& buffer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryAllocateFlags memAllocFlags = 0);

void createDeviceLocalBuffer(BufferObj& buffer, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryAllocateFlags memAllocFlags = 0);

// -------------------- IMAGES -------------------- //
VkFormat findDepthFormat();

LayoutTransition getLayoutTransition(VkImageLayout oldLayout, VkImageLayout newLayout);

void transitionImageLayout(const VkhCommandBuffer& commandBuffer, const VkhImage& image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount, uint32_t levelCount, uint32_t baseMip);
void transitionImageLayout(const VkhCommandBuffer& commandBuffer, const Texture& tex, TextureType textureType, VkImageLayout oldLayout, VkImageLayout newLayout);
void transitionImageLayout(const VkhCommandBuffer& commandBuffer, const Texture& tex, TextureType textureType, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t baseMip);

void createImage(VkhImage& image, VkhDeviceMemory& imageMemory, uint32_t width, uint32_t height, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers, bool cubeMap, VkImageUsageFlags usage, VkSampleCountFlagBits sample);
void createImage(VkhImage& image, VkhDeviceMemory& imageMemory, uint32_t width, uint32_t height, TextureType textureType, uint32_t mipLevels, uint32_t arrayLayers, bool cubeMap, VkImageUsageFlags usage, VkSampleCountFlagBits sample);

void createSampler(VkhSampler& sampler, uint32_t mipLevels, TextureType type);

void createImageView(Texture& tex, TextureType type);
void createImageView(Texture& tex, VkFormat format);

void createTexture(Texture& tex, TextureType textureType, VkImageUsageFlags usage, uint32_t width, uint32_t height);

void createSwapTexture(Texture& tex, VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height);

VkFormat getTextureFormat(TextureType textureType);

// -------------------- DESCRIPTOR SETS -------------------- //
void createDSLayout(VkhDescriptorSetLayout& layout, const VkDescriptorSetLayoutBinding* bindings, size_t bindingCount, bool variableDescriptorCount, bool pushDescriptors);

void createDSPool(VkhDescriptorPool& pool, const VkDescriptorPoolSize* poolSizes, size_t poolSizeCount);

VkhDescriptorSet allocDS(VkhDescriptorSetLayout& layout, const VkhDescriptorPool& pool, uint32_t variableCount = 0);

VkDescriptorImageInfo createDSImageInfo(const VkhImageView& imageView, const VkhSampler& sampler, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

VkDescriptorSetLayoutBinding createDSLayoutBinding(uint32_t binding, size_t count, VkDescriptorType type, VkShaderStageFlags stageFlags);

VkDescriptorPoolSize createDSPoolSize(size_t count, VkDescriptorType type);

// -------------------- PIPELINES -------------------- //
VkhShaderModule createShaderModule(const std::vector<char>& code);

VkPipelineShaderStageCreateInfo createShaderStage(VkShaderStageFlagBits stage, const VkhShaderModule& shaderModule);

VkVertexInputBindingDescription vertInputBindDesc(uint32_t binding, uint32_t stride, VkVertexInputRate inputRate);

VkVertexInputAttributeDescription vertInputAttrDesc(VkFormat format, uint32_t binding, uint32_t location, size_t offset);

VkPipelineVertexInputStateCreateInfo vertInputInfo(const VkVertexInputBindingDescription* bindingDescriptions, size_t bindingDescriptionCount, const VkVertexInputAttributeDescription* attrDescriptions, size_t attrDescriptionCount);

// -------------------- TEMPLATES -------------------- //

template <typename ObjectT>
void writeBuffer(const VkhDeviceMemory& bufferMem, const ObjectT* object, VkDeviceSize size) {
    if (object == nullptr) throw std::invalid_argument("Object is null!");
    if (size == 0) throw std::invalid_argument("Buffer size is 0!");

    VkDevice device = VkSingleton::v().gdevice();

    void* data = nullptr;
    if (vkMapMemory(device, bufferMem.v(), 0, size, 0, &data) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map memory for buffer!");
    }

    if (data == nullptr) {
        throw std::runtime_error("Mapped memory is null!");
    }

    std::memcpy(data, object, static_cast<size_t>(size));
    vkUnmapMemory(device, bufferMem.v());
}

template <typename ObjectT>
void createAndWriteLocalBuffer(BufferObj& buffer, const ObjectT* data, VkDeviceSize size, const VkhCommandPool& commandPool, VkQueue queue, VkBufferUsageFlags usage, VkMemoryAllocateFlags memAllocFlags = 0) {
    createDeviceLocalBuffer(buffer, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memAllocFlags);

    BufferObj stagingBuffer;
    createHostVisibleBuffer(stagingBuffer, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memAllocFlags);

    // write the data to the staging buffer
    writeBuffer(stagingBuffer.mem, data, size);

    // copy the staging buffer to the device local buffer
    copyBuffer(stagingBuffer.buf, buffer.buf, commandPool, queue, size);
}

template <typename ObjectT>
void createAndWriteHostBuffer(BufferObj& buffer, const ObjectT* data, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryAllocateFlags memAllocFlags = 0) {
    createHostVisibleBuffer(buffer, size, usage, memAllocFlags);
    writeBuffer(buffer.mem, data, size);
}

template <typename InfoType>
VkWriteDescriptorSet createDSWrite(const VkhDescriptorSet& set, uint32_t binding, VkDescriptorType type, const InfoType* infos, size_t count) {
    // static assert if an invalid type is passed in
    static_assert(std::is_same_v<InfoType, VkDescriptorImageInfo> || std::is_same_v<InfoType, VkDescriptorBufferInfo> || std::is_same_v<InfoType, VkWriteDescriptorSetAccelerationStructureKHR>, "Invalid info type");

    VkWriteDescriptorSet d{};
    d.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    d.dstSet = set.v();
    d.dstBinding = binding;
    d.descriptorType = type;
    d.descriptorCount = static_cast<uint32_t>(count);

    if constexpr (std::is_same_v<InfoType, VkDescriptorImageInfo>) {  // if the info type is an image
        d.pImageInfo = infos;
    } else if constexpr (std::is_same_v<InfoType, VkDescriptorBufferInfo>) {  // if the info type is a buffer
        d.pBufferInfo = infos;
    } else if constexpr (std::is_same_v<InfoType, VkWriteDescriptorSetAccelerationStructureKHR>) {  // if the info type is an acceleration structure
        d.pNext = infos;
    }

    return d;
}

template <typename InfoType>
VkWriteDescriptorSet createDSWrite(const VkhDescriptorSet& set, uint32_t binding, VkDescriptorType type, InfoType info) {
    // static assert if an invalid type is passed in
    static_assert(std::is_same_v<InfoType, VkDescriptorImageInfo> || std::is_same_v<InfoType, VkDescriptorBufferInfo> || std::is_same_v<InfoType, VkWriteDescriptorSetAccelerationStructureKHR>, "Invalid info type");

    VkWriteDescriptorSet d{};
    d.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    d.dstSet = set.v();
    d.dstBinding = binding;
    d.descriptorType = type;
    d.descriptorCount = 1;

    if constexpr (std::is_same_v<InfoType, VkDescriptorImageInfo>) {  // if the info type is an image
        d.pImageInfo = &info;
    } else if constexpr (std::is_same_v<InfoType, VkDescriptorBufferInfo>) {  // if the info type is a buffer
        d.pBufferInfo = &info;
    } else if constexpr (std::is_same_v<InfoType, VkWriteDescriptorSetAccelerationStructureKHR>) {  // if the info type is an acceleration structure
        d.pNext = &info;
    }

    return d;
}
}  // namespace vkh
