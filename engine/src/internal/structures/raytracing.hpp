#pragma once

#include "../../libraries/vkhelper.hpp"

namespace rtstructures {
struct BLAS {
    VkhAccelerationStructure blas{};
    vkh::BufferObj compBuffer{};
};

struct TLAS {
    VkhAccelerationStructure as{};
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    VkAccelerationStructureGeometryKHR geometry{};

    vkh::BufferObj buffer{};
    vkh::BufferObj instanceBuffer{};
    vkh::BufferObj scratchBuffer{};
};

struct SBT {
    vkh::BufferObj buffer{};

    VkDeviceSize size{};
    VkDeviceSize entryS{};

    // sbt regions
    VkStridedDeviceAddressRegionKHR raygenR{};
    VkStridedDeviceAddressRegionKHR missR{};
    VkStridedDeviceAddressRegionKHR hitR{};
    VkStridedDeviceAddressRegionKHR callR{};
};
}  // namespace rtstructures
