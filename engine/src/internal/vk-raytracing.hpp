#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "libraries/vkhelper.hpp"
#include "structures/raytracing.hpp"
#include "vk-scene.hpp"

namespace raytracing {
class VkRaytracing {
public:
    // delete copying and moving
    VkRaytracing() = default;
    VkRaytracing(const VkRaytracing&) = delete;
    VkRaytracing& operator=(const VkRaytracing&) = delete;
    VkRaytracing(VkRaytracing&&) = delete;
    VkRaytracing& operator=(VkRaytracing&&) = delete;

    void init(uint32_t maxFrames, const VkhCommandPool& commandPool, VkQueue gQueue, VkDevice device, const scene::VkScene* scene) noexcept;
    void createAccelStructures();
    void updateTLAS(uint32_t currentFrame, bool changed);
    void createSBT(const VkhPipeline& rtPipeline, const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rtProperties);

    [[nodiscard]] const VkAccelerationStructureKHR* tlasData(bool rtEnabled);

    [[nodiscard]] const VkStridedDeviceAddressRegionKHR* getRaygenRegion() const noexcept { return &m_sbt.raygenR; }
    [[nodiscard]] const VkStridedDeviceAddressRegionKHR* getMissRegion() const noexcept { return &m_sbt.missR; }
    [[nodiscard]] const VkStridedDeviceAddressRegionKHR* getHitRegion() const noexcept { return &m_sbt.hitR; }
    [[nodiscard]] const VkStridedDeviceAddressRegionKHR* getCallableRegion() const noexcept { return &m_sbt.callR; }

private:
    std::vector<rtstructures::BLAS> m_blas;
    std::vector<rtstructures::TLAS> m_tlas;
    std::vector<VkAccelerationStructureKHR> m_rawTLASData;
    rtstructures::SBT m_sbt{};
    std::vector<VkAccelerationStructureInstanceKHR> m_meshInstances;

    const scene::VkScene* m_scene = nullptr;

    uint32_t m_maxFrames = 0;
    VkDevice m_device{};
    VkhCommandPool m_commandPool{};
    VkQueue m_gQueue{};

private:
    void createBLAS(vkh::BufData bufferData, size_t index);

    void createTLASInstanceBuffer(rtstructures::TLAS& t);
    void createTLAS(rtstructures::TLAS& t);
    [[nodiscard]] VkTransformMatrixKHR mat4ToVk(const dml::mat4& m);
    void createMeshInstace(size_t index);
    void recreateTLAS(size_t index, bool rebuild);
};
}  // namespace raytracing
