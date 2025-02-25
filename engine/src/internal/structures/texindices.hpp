#pragma once

#include <vulkan/vulkan.h>

#include <array>

#include "../../config.hpp"

namespace texindices {
struct TexIndexObj {
    int albedoIndex = -1;
    int metallicRoughnessIndex = -1;
    int normalIndex = -1;
    int emissiveIndex = -1;
    int occlusionIndex = -1;

    VkDeviceAddress vertAddr = 0;
    VkDeviceAddress indAddr = 0;
};

struct TexIndices {
    std::array<TexIndexObj, cfg::MAX_OBJECTS> indices{};
};
}  // namespace texindices
