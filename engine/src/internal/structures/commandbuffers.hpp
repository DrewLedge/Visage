#pragma once

#include "../../libraries/vkhelper.hpp"

namespace commandbuffers {
struct CommandBufferCollection {
    std::vector<VkhCommandPool> pools;
    std::vector<VkhCommandBuffer> buffers;
    std::vector<VkCommandBuffer> rawBuffers;

    void reserveClear(size_t size) {
        buffers.clear();
        pools.clear();
        buffers.reserve(size);
        pools.reserve(size);
    }

    size_t size() const {
        return buffers.size();
    }

    VkCommandBuffer* data() {
        if (buffers.size() == 1) {
            return buffers[0].p();
        } else {
            rawBuffers.clear();
            rawBuffers.reserve(buffers.size());
            for (const auto& b : buffers) {
                rawBuffers.push_back(b.v());
            }
            return rawBuffers.data();
        }
    }

    VkhCommandBuffer& operator[](size_t index) {
        return buffers[index];
    }
};

struct CommandBufferSet {
    CommandBufferCollection primary{};
    CommandBufferCollection secondary{};
};
}  // namespace commandbuffers
