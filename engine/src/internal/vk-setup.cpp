#include "vk-setup.hpp"

#include <string>

#include "config.hpp"
#include "libraries/utils.hpp"

namespace setup {
core::VkCore VkSetup::init(GLFWwindow* window) {
    createInstance();
    createSurface(window);
    pickPhysicalDevice();
    getPhysicalDeviceProperties();
    createDevice();
    initQueues();

    VkSingleton::v().init(m_vulkanCore.instance, m_vulkanCore.device, m_vulkanCore.surface, m_vulkanCore.physicalDevice);

    return m_vulkanCore;
}

bool VkSetup::isSupported(const char* extensionName) const {
    // get extension count
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(m_vulkanCore.physicalDevice, nullptr, &extensionCount, nullptr);

    // get available extensions
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(m_vulkanCore.physicalDevice, nullptr, &extensionCount, extensions.data());

    // check if extension is in list of available extensions
    for (const VkExtensionProperties& e : extensions) {
        if (std::strcmp(extensionName, e.extensionName) == 0) {
            return true;
        }
    }

    return false;
}

bool VkSetup::isRTSupported() {
    // check if extensions are supported
    bool extensionsSupported = isSupported(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) && isSupported(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) && isSupported(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

    if (!extensionsSupported) {
        return false;
    }

    // check if the device supports ray tracing pipeline features
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{};
    rtFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    // check if the device supports acceleration structure features
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeatures.pNext = &rtFeatures;

    VkPhysicalDeviceFeatures2 deviceFeatures2{};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.pNext = &asFeatures;

    vkGetPhysicalDeviceFeatures2(m_vulkanCore.physicalDevice, &deviceFeatures2);

    return (rtFeatures.rayTracingPipeline == VK_TRUE && asFeatures.accelerationStructure == VK_TRUE);
}

int VkSetup::scoreDevice(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;

    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);

    int score = 0;

    // prioritize discrete over integrated gpus
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    } else if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 100;
    }

    return score;
}

void VkSetup::createInstance() {
    VkApplicationInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    instanceInfo.pApplicationName = "Visage";
    instanceInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    instanceInfo.pEngineName = "Visage";
    instanceInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    instanceInfo.apiVersion = VK_API_VERSION_1_3;

    // get glfw extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

    VkInstanceCreateInfo newInfo{};
    newInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    newInfo.pApplicationInfo = &instanceInfo;
    newInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    newInfo.ppEnabledExtensionNames = extensions.data();
    newInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    newInfo.ppEnabledLayerNames = validationLayers.data();

    if (vkCreateInstance(&newInfo, nullptr, &m_vulkanCore.instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create instance!");
    }
}

void VkSetup::createSurface(GLFWwindow* window) {
    if (glfwCreateWindowSurface(m_vulkanCore.instance, window, nullptr, &m_vulkanCore.surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create window surface!");
    }
}

void VkSetup::pickPhysicalDevice() {
    // get number of devices that support vulkan
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vulkanCore.instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    // get list of devices that support vulkan
    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(m_vulkanCore.instance, &deviceCount, physicalDevices.data());

    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    int highestScore = -1;

    // go through all devices that support vulkan
    for (const VkPhysicalDevice& p : physicalDevices) {
        // get the queue family indices
        vkh::QueueFamilyIndices indices = vkh::findQueueFamilyIndices(m_vulkanCore.surface, p);

        // ensure the device supports graphics, presentation, compute and transfer;
        if (indices.allComplete()) {
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(p, &deviceProperties);

            // score the device
            int score = scoreDevice(p);
            if (score > highestScore) {
                bestDevice = p;
                highestScore = score;
            }
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU for graphics, compute, transfer and presentation!");
    }

    // get all of the queue family indices for the best selected device
    m_queueFamilyIndices = vkh::findQueueFamilyIndices(m_vulkanCore.surface, bestDevice);

    // use the best device
    m_vulkanCore.physicalDevice = bestDevice;

    // check if ray tracing is supported
    m_rtSupported = isRTSupported();

    utils::sep();
    std::cout << "Raytacing is " << (m_rtSupported ? "supported" : "not supported") << " on this device!\n";
}

void VkSetup::getPhysicalDeviceProperties() {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(m_vulkanCore.physicalDevice, &deviceProperties);

    m_rtProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    VkPhysicalDeviceMultiviewPropertiesKHR multiViewProperties{};
    multiViewProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES_KHR;
    multiViewProperties.pNext = &m_rtProperties;

    VkPhysicalDeviceProperties2 deviceProperties2{};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &multiViewProperties;
    vkGetPhysicalDeviceProperties2(m_vulkanCore.physicalDevice, &deviceProperties2);

    m_maxMultiViewCount = multiViewProperties.maxMultiviewViewCount;
    if (cfg::LIGHTS_PER_BATCH > m_maxMultiViewCount) {
        throw std::runtime_error("Device doesn't support multiview count of: " + std::to_string(cfg::LIGHTS_PER_BATCH));
    }
}

void VkSetup::createDevice() {
    VkPhysicalDeviceMultiviewFeaturesKHR multiView{};
    multiView.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR;
    multiView.multiview = true;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    bufferDeviceAddressFeatures.pNext = &multiView;

    // raytracing specific
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeatures.accelerationStructure = VK_TRUE;
    asFeatures.pNext = &bufferDeviceAddressFeatures;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{};
    rtFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rtFeatures.rayTracingPipeline = VK_TRUE;
    rtFeatures.pNext = &asFeatures;

    // descriptorset indexing
    VkPhysicalDeviceDescriptorIndexingFeatures descIndexing{};
    descIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descIndexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    descIndexing.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    descIndexing.runtimeDescriptorArray = VK_TRUE;
    descIndexing.descriptorBindingVariableDescriptorCount = VK_TRUE;
    descIndexing.descriptorBindingPartiallyBound = VK_TRUE;

    if (m_rtSupported) {
        descIndexing.pNext = &rtFeatures;
    } else {
        descIndexing.pNext = &bufferDeviceAddressFeatures;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_queueFamilyIndices.graphicsFamily.value();
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.imageCubeArray = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_TRUE;
    deviceFeatures.shaderInt64 = VK_TRUE;
    deviceFeatures.multiDrawIndirect = VK_TRUE;

    VkDeviceCreateInfo newInfo{};
    newInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    newInfo.pNext = &descIndexing;  // add the indexing features to the pNext chain
    newInfo.pQueueCreateInfos = &queueInfo;
    newInfo.queueCreateInfoCount = 1;
    newInfo.pEnabledFeatures = &deviceFeatures;  // device features to enable

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME,
        VK_KHR_MULTIVIEW_EXTENSION_NAME,
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    };

    if (m_rtSupported) {
        deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }

    vkhfp::loadFuncPointers(m_vulkanCore.instance);

    for (auto& e : deviceExtensions) {
        if (!isSupported(e)) {
            utils::logWarning("Extension: " + std::string(e) + " is NOT supported!");
            deviceExtensions.erase(std::remove(deviceExtensions.begin(), deviceExtensions.end(), e), deviceExtensions.end());
        }
    }

    newInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    newInfo.ppEnabledExtensionNames = deviceExtensions.data();
    newInfo.enabledLayerCount = 0;
    newInfo.ppEnabledLayerNames = nullptr;
    VkResult result = vkCreateDevice(m_vulkanCore.physicalDevice, &newInfo, nullptr, &m_vulkanCore.device);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create device!");
    }
}

void VkSetup::initQueues() {
    vkGetDeviceQueue(m_vulkanCore.device, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_vulkanCore.device, m_queueFamilyIndices.presentFamily.value(), 0, &m_presentQueue);
    vkGetDeviceQueue(m_vulkanCore.device, m_queueFamilyIndices.computeFamily.value(), 0, &m_computeQueue);
    vkGetDeviceQueue(m_vulkanCore.device, m_queueFamilyIndices.transferFamily.value(), 0, &m_transferQueue);
}
}  // namespace setup
