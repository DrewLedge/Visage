#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "libraries/vkhelper.hpp"
#include "structures/core.hpp"

namespace setup {
class VkSetup {
public:
    // delete copying and moving
    VkSetup() = default;
    VkSetup(const VkSetup&) = delete;
    VkSetup& operator=(const VkSetup&) = delete;
    VkSetup(VkSetup&&) = delete;
    VkSetup& operator=(VkSetup&&) = delete;

    [[nodiscard]] core::VkCore init(GLFWwindow* window);

    // getters
    [[nodiscard]] VkPhysicalDeviceRayTracingPipelinePropertiesKHR getRtProperties() const noexcept { return m_rtProperties; }
    [[nodiscard]] uint32_t getMaxMultiViewCount() const noexcept { return m_maxMultiViewCount; }

    [[nodiscard]] uint32_t getGraphicsFamily() const { return m_queueFamilyIndices.graphicsFamily.value(); }
    [[nodiscard]] bool isRaytracingSupported() const noexcept { return m_rtSupported; }

    [[nodiscard]] VkQueue gQueue() const noexcept { return m_graphicsQueue; }
    [[nodiscard]] VkQueue pQueue() const noexcept { return m_presentQueue; }
    [[nodiscard]] VkQueue cQueue() const noexcept { return m_computeQueue; }
    [[nodiscard]] VkQueue tQueue() const noexcept { return m_transferQueue; }

private:
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtProperties{};
    uint32_t m_maxMultiViewCount = 0;

    core::VkCore m_vulkanCore{};

    // queues
    VkQueue m_graphicsQueue{};
    VkQueue m_presentQueue{};
    VkQueue m_computeQueue{};
    VkQueue m_transferQueue{};

    vkh::QueueFamilyIndices m_queueFamilyIndices;

    bool m_rtSupported = false;

private:
    // helper
    bool isSupported(const char* extensionName) const;
    bool isRTSupported();
    int scoreDevice(VkPhysicalDevice physicalDevice);

    void createInstance();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void getPhysicalDeviceProperties();
    void createDevice();
    void initQueues();
};
}  // namespace setup
