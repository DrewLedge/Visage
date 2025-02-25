#pragma once

#include "../../libraries/vkhelper.hpp"

namespace desc {
struct DescriptorSet {
    VkhDescriptorPool pool{};
    VkhDescriptorSetLayout layout{};
    VkhDescriptorSet set;

    std::vector<VkDescriptorSetLayoutBinding> bindings{};
    std::vector<VkDescriptorPoolSize> poolSizes{};

    DescriptorSet() : pool(), layout(), set(pool.v()) {
    }
};
}  // namespace desc
