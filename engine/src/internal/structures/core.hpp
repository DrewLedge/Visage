#pragma once

#include <vulkan/vulkan.h>

namespace core {
struct VkCore {
    VkInstance instance{};
    VkDevice device{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physicalDevice{};
};
}  // namespace core
