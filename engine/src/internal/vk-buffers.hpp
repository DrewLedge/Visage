#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "internal/vk-scene.hpp"
#include "libraries/vkhelper.hpp"

namespace buffers {
class VkBuffers {
public:
    // delete copying and moving
    VkBuffers() = default;
    VkBuffers(const VkBuffers&) = delete;
    VkBuffers& operator=(const VkBuffers&) = delete;
    VkBuffers(VkBuffers&&) = delete;
    VkBuffers& operator=(VkBuffers&&) = delete;

    void init(VkhCommandPool commandPool, VkQueue gQueue, bool rtEnabled, uint32_t maxFrames, const scene::VkScene* scene);
    void createBuffers(uint32_t currentFrame);

    void update(uint32_t currentFrame);
    void createTexIndicesBuffer();
    void updateSceneIndirectCommandsBuffer();

    // getters
    [[nodiscard]] vkh::BufferObj getTexIndicesBuffer() const noexcept { return m_texIndicesBuffer; }
    [[nodiscard]] VkBuffer getSceneIndirectCommandsBuffer() const noexcept { return m_sceneIndirectBuffer.buf.v(); }

    [[nodiscard]] vkh::BufferObj getCamBuffer(uint32_t index) const noexcept { return m_camBuffers[index]; }
    [[nodiscard]] vkh::BufferObj getLightBuffer(uint32_t index) const noexcept { return m_lightBuffers[index]; }
    [[nodiscard]] vkh::BufferObj getObjectInstanceBuffer(uint32_t index) const noexcept { return m_objInstanceBuffers[index]; }

private:
    vkh::BufferObj m_texIndicesBuffer{};
    vkh::BufferObj m_sceneIndirectBuffer{};

    std::vector<vkh::BufferObj> m_camBuffers;
    std::vector<vkh::BufferObj> m_lightBuffers;
    std::vector<vkh::BufferObj> m_objInstanceBuffers;

    const scene::VkScene* m_scene = nullptr;

    VkhCommandPool m_commandPool{};
    VkQueue m_gQueue{};
    bool m_rtEnabled = false;
    uint32_t m_maxFrames = 0;
};
}  // namespace buffers
