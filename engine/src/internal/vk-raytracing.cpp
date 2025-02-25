#include "vk-raytracing.hpp"

namespace raytracing {
void VkRaytracing::init(uint32_t maxFrames, const VkhCommandPool& commandPool, VkQueue gQueue, VkDevice device, const scene::VkScene* scene) noexcept {
    m_scene = scene;

    m_maxFrames = maxFrames;
    m_commandPool = commandPool;
    m_gQueue = gQueue;
    m_device = device;
}

void VkRaytracing::createAccelStructures() {
    size_t uniqueObjectCount = m_scene->getUniqueObjectCount();

    m_blas.resize(uniqueObjectCount);
    const size_t* uniqueObjects = m_scene->getUniqueObjects();

    for (size_t i = 0; i < uniqueObjectCount; i++) {
        size_t index = uniqueObjects[i];

        size_t bufferInd = m_scene->getBufferIndex(index);
        vkh::BufData bufferData = m_scene->getBufferData(bufferInd);
        createBLAS(bufferData, bufferInd);
    }

    for (size_t i = 0; i < m_scene->getObjectCount(); i++) {
        createMeshInstace(i);
    }

    m_tlas.resize(m_maxFrames);
    for (size_t i = 0; i < m_maxFrames; i++) {
        createTLAS(m_tlas[i]);
    }
}

void VkRaytracing::updateTLAS(uint32_t currentFrame, bool changed) {
    if (changed) m_meshInstances.clear();

    for (size_t i = 0; i < m_scene->getObjectCount(); i++) {
        if (changed) {
            createMeshInstace(i);
        } else {
            VkAccelerationStructureInstanceKHR& meshInstance = m_meshInstances[i];

            const dml::mat4 m = m_scene->getObjectInstances()[i].model;
            meshInstance.transform = mat4ToVk(m);
        }
    }

    if (changed) {
        for (size_t i = 0; i < m_maxFrames; i++) {
            recreateTLAS(i, true);
        }
    } else {
        recreateTLAS(currentFrame, false);
    }
}

void VkRaytracing::createSBT(const VkhPipeline& rtPipeline, const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rtProperties) {
    const uint32_t shaderGroupCount = 5;

    // the size of a single shader group handle
    // shader group handles tell the gpu where to find specific shaders
    uint32_t handleSize = rtProperties.shaderGroupHandleSize;

    // the alignment requirement for the shader group handles in the sbt
    // it ensures each group is properly aligned for maximum efficiency
    uint32_t handleAlignment = rtProperties.shaderGroupHandleAlignment;

    // the alignment of the shader record data inside the SBT
    // the shader record data holds the shader group handle along with other per-shader data
    uint32_t baseAlignment = rtProperties.shaderGroupBaseAlignment;

    m_sbt.entryS = baseAlignment;
    m_sbt.size = m_sbt.entryS * shaderGroupCount;  // the total size of the sbt

    // create the sbt buffer
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryAllocateFlags memAllocF = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    vkh::createBuffer(m_sbt.buffer, m_sbt.size, usage, memFlags, memAllocF);

    // get the shader handles
    std::vector<uint8_t> shaderHandles(handleSize * shaderGroupCount);
    vkhfp::vkGetRayTracingShaderGroupHandlesKHR(m_device, rtPipeline.v(), 0, shaderGroupCount, shaderHandles.size(), shaderHandles.data());

    void* data;
    vkMapMemory(m_device, m_sbt.buffer.mem.v(), 0, m_sbt.size, 0, &data);
    uint8_t* d = reinterpret_cast<uint8_t*>(data);

    uint32_t dataOffset = 0;
    uint32_t handleOffset = 0;

    // copy the data of each shader group handle into the sbt buffer
    for (uint32_t i = 0; i < shaderGroupCount; i++) {
        std::memcpy(d + dataOffset, shaderHandles.data() + handleOffset, handleSize);
        dataOffset += static_cast<uint32_t>(m_sbt.entryS);
        handleOffset += handleSize;
    }

    vkUnmapMemory(m_device, m_sbt.buffer.mem.v());

    VkDeviceAddress sbtAddr = vkh::bufferDeviceAddress(m_sbt.buffer.buf);

    // ray gen region
    m_sbt.raygenR.deviceAddress = sbtAddr;
    m_sbt.raygenR.stride = m_sbt.entryS;
    m_sbt.raygenR.size = m_sbt.entryS;

    // ray miss region
    m_sbt.missR.deviceAddress = sbtAddr + m_sbt.entryS;
    m_sbt.missR.stride = m_sbt.entryS;
    m_sbt.missR.size = m_sbt.entryS * 2;

    // ray hit region
    m_sbt.hitR.deviceAddress = sbtAddr + (3 * m_sbt.entryS);
    m_sbt.hitR.stride = m_sbt.entryS;
    m_sbt.hitR.size = m_sbt.entryS * 2;

    // callable region (not used)
    m_sbt.callR.deviceAddress = 0;
    m_sbt.callR.stride = 0;
    m_sbt.callR.size = 0;
}

const VkAccelerationStructureKHR* VkRaytracing::tlasData(bool rtEnabled) {
    if (!rtEnabled) return nullptr;

    m_rawTLASData.clear();
    m_rawTLASData.reserve(m_maxFrames);

    for (size_t i = 0; i < m_maxFrames; i++) {
        m_rawTLASData.push_back(m_tlas[i].as.v());
    }

    return m_rawTLASData.data();
}

void VkRaytracing::createBLAS(vkh::BufData bufferData, size_t index) {
    VkhAccelerationStructure tempBLAS{};
    uint32_t primitiveCount = bufferData.indexCount / 3;

    // get the device addresses (location of the data on the device) of the vertex and index buffers
    // this allows the data within the gpu to be accessed very efficiently
    VkhBuffer vertBuffer = m_scene->getVertBuffer().buf;
    VkhBuffer indexBuffer = m_scene->getIndexBuffer().buf;

    VkDeviceAddress vertexAddress = vkh::bufferDeviceAddress(vertBuffer) + (bufferData.vertexOffset * sizeof(dvl::Vertex));
    VkDeviceAddress indexAddress = vkh::bufferDeviceAddress(indexBuffer) + (bufferData.indexOffset * sizeof(uint32_t));

    // acceleration structure geometry - specifies the device addresses and data inside of the vertex and index buffers
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = 0;  // no geometry flags set
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.vertexData.deviceAddress = vertexAddress;
    geometry.geometry.triangles.vertexStride = sizeof(dvl::Vertex);
    geometry.geometry.triangles.maxVertex = bufferData.vertexCount;
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geometry.geometry.triangles.indexData.deviceAddress = indexAddress;

    VkBuildAccelerationStructureFlagsKHR accelerationFlags = 0;
    accelerationFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;   // allows the blas to be compacted
    accelerationFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;  // optimizes the blas for faster path tracing

    // BLAS build info - specifies the acceleration structure type, the flags, and the geometry
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.flags = accelerationFlags;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    // size requirements for the BLAS - the total size of the acceleration structure, taking into account the amount of primitives, etc
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkhfp::vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    // create a buffer for the BLAS - the buffer used in the creation of the blas
    vkh::BufferObj blasBuffer{};
    VkBufferUsageFlags blasUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    vkh::createDeviceLocalBuffer(blasBuffer, sizeInfo.accelerationStructureSize, blasUsage, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    // create the BLAS
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    createInfo.buffer = blasBuffer.buf.v();
    createInfo.size = sizeInfo.accelerationStructureSize;
    vkhfp::vkCreateAccelerationStructureKHR(m_device, &createInfo, nullptr, tempBLAS.p());

    // scratch buffer - used to create space for intermediate data thats used when building the BLAS
    vkh::BufferObj blasScratchBuffer{};
    VkBufferUsageFlags scratchUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    vkh::createDeviceLocalBuffer(blasScratchBuffer, sizeInfo.buildScratchSize, scratchUsage, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    // build range info - specifies the primitive count and offsets for the blas
    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = primitiveCount;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;
    buildRangeInfo.firstVertex = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;

    // set the dst of the build info to be the blas and add the scratch buffer address
    buildInfo.dstAccelerationStructure = tempBLAS.v();
    buildInfo.scratchData.deviceAddress = vkh::bufferDeviceAddress(blasScratchBuffer.buf);

    // build and populate the BLAS
    VkhCommandBuffer commandBufferB = vkh::beginSingleTimeCommands(m_commandPool);
    vkhfp::vkCmdBuildAccelerationStructuresKHR(commandBufferB.v(), 1, &buildInfo, &pBuildRangeInfo);
    vkh::endSingleTimeCommands(commandBufferB, m_commandPool, m_gQueue);

    // create a query pool used to store the size of the compacted BLAS
    VkhQueryPool queryPool;
    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
    queryPoolInfo.queryCount = 1;
    vkCreateQueryPool(m_device, &queryPoolInfo, nullptr, queryPool.p());

    // query the size of the BLAS by writing its properties to the query pool
    // the data becomes avaible after submitting the command buffer
    VkhCommandBuffer commandBufferQ = vkh::beginSingleTimeCommands(m_commandPool);
    vkCmdResetQueryPool(commandBufferQ.v(), queryPool.v(), 0, 1);
    vkhfp::vkCmdWriteAccelerationStructuresPropertiesKHR(commandBufferQ.v(), 1, tempBLAS.p(), VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, queryPool.v(), 0);
    vkh::endSingleTimeCommands(commandBufferQ, m_commandPool, m_gQueue);

    // get the compacted size from the query pool
    VkDeviceSize compactedSize = 0;
    vkGetQueryPoolResults(m_device, queryPool.v(), 0, 1, sizeof(VkDeviceSize), &compactedSize, sizeof(VkDeviceSize), VK_QUERY_RESULT_WAIT_BIT);

    // create a buffer for the compacted BLAS
    VkBufferUsageFlags compUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    vkh::createDeviceLocalBuffer(m_blas[index].compBuffer, compactedSize, compUsage, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    // create the compacted BLAS
    VkAccelerationStructureCreateInfoKHR compactedCreateInfo{};
    compactedCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    compactedCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    compactedCreateInfo.buffer = m_blas[index].compBuffer.buf.v();
    compactedCreateInfo.size = compactedSize;
    vkhfp::vkCreateAccelerationStructureKHR(m_device, &compactedCreateInfo, nullptr, m_blas[index].blas.p());

    // the info for the copying of the original blas to the compacted blas
    VkCopyAccelerationStructureInfoKHR copyInfo{};
    copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    copyInfo.src = tempBLAS.v();
    copyInfo.dst = m_blas[index].blas.v();

    // copy the original BLAS into the compacted one to perform the compaction
    VkhCommandBuffer commandBufferC = vkh::beginSingleTimeCommands(m_commandPool);
    vkhfp::vkCmdCopyAccelerationStructureKHR(commandBufferC.v(), &copyInfo);
    vkh::endSingleTimeCommands(commandBufferC, m_commandPool, m_gQueue);
}

void VkRaytracing::createTLASInstanceBuffer(rtstructures::TLAS& t) {
    VkDeviceSize iSize = m_meshInstances.size() * sizeof(VkAccelerationStructureInstanceKHR);
    VkBufferUsageFlags iUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateFlags iMemFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    vkh::createAndWriteHostBuffer(t.instanceBuffer, m_meshInstances.data(), iSize, iUsage, iMemFlags);
}

void VkRaytracing::createTLAS(rtstructures::TLAS& t) {
    t.as.reset();

    uint32_t primitiveCount = static_cast<uint32_t>(m_meshInstances.size());
    uint32_t primitiveCountMax = cfg::MAX_OBJECTS;

    // create a buffer to hold all of the instances
    createTLASInstanceBuffer(t);

    // acceleration structure geometry
    VkDeviceAddress instanceAddress = vkh::bufferDeviceAddress(t.instanceBuffer.buf);
    t.geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    t.geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    t.geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    t.geometry.geometry.instances.data.deviceAddress = instanceAddress;
    t.geometry.geometry.instances.arrayOfPointers = VK_FALSE;

    VkBuildAccelerationStructureFlagsKHR accelerationFlags = 0;
    accelerationFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;  // optimizes the tlas for faster path tracing
    accelerationFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;       // allows the tlas to be updated, without having to fully recreate it

    // TLAS build info - specifies the acceleration structure type, the flags, and the geometry
    t.buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    t.buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    t.buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    t.buildInfo.flags = accelerationFlags;
    t.buildInfo.geometryCount = 1;
    t.buildInfo.pGeometries = &t.geometry;

    // size requirements for the TLAS - the total size of the acceleration structure, taking into account the amount of primitives, etc
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkhfp::vkGetAccelerationStructureBuildSizesKHR(m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &t.buildInfo, &primitiveCountMax, &sizeInfo);

    VkDeviceSize asSize = sizeInfo.accelerationStructureSize;

    // create a buffer for the TLAS - the buffer used in the creation of the tlas
    VkBufferUsageFlags tlasUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    vkh::createDeviceLocalBuffer(t.buffer, asSize, tlasUsage, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    // create the TLAS
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    createInfo.buffer = t.buffer.buf.v();
    createInfo.size = asSize;
    vkhfp::vkCreateAccelerationStructureKHR(m_device, &createInfo, nullptr, t.as.p());

    // scratch buffer - used to create space for intermediate data thats used when building the TLAS
    VkBufferUsageFlags scratchUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    vkh::createDeviceLocalBuffer(t.scratchBuffer, sizeInfo.buildScratchSize, scratchUsage, VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT);

    // build range info - specifies the primitive count and offsets for the tlas
    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = primitiveCount;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;
    buildRangeInfo.firstVertex = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;

    // set the dst of the build info to be the tlas and add the scratch buffer address
    t.buildInfo.dstAccelerationStructure = t.as.v();
    t.buildInfo.scratchData.deviceAddress = vkh::bufferDeviceAddress(t.scratchBuffer.buf);

    // build and populate the TLAS
    VkhCommandBuffer commandBufferB = vkh::beginSingleTimeCommands(m_commandPool);
    vkhfp::vkCmdBuildAccelerationStructuresKHR(commandBufferB.v(), 1, &t.buildInfo, &pBuildRangeInfo);
    vkh::endSingleTimeCommands(commandBufferB, m_commandPool, m_gQueue);
}

VkTransformMatrixKHR VkRaytracing::mat4ToVk(const dml::mat4& m) {
    dml::mat4 t = m.transpose();

    VkTransformMatrixKHR result{};
    std::memcpy(&result.matrix, &t.flat, 12 * sizeof(float));
    return result;
}

void VkRaytracing::createMeshInstace(size_t index) {
    VkAccelerationStructureInstanceKHR meshInstance{};
    size_t bufferInd = m_scene->getBufferIndex(index);

    // copy the models model matrix into the instance data
    const dml::mat4 m = m_scene->getObjectInstances()[index].model;
    meshInstance.transform = mat4ToVk(m);

    VkDeviceAddress blasAddress = vkh::asDeviceAddress(m_blas[bufferInd].blas);

    // populate the instance data
    meshInstance.accelerationStructureReference = blasAddress;
    meshInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    size_t objectInd = m_scene->getUniqueObjectIndex(index);
    meshInstance.instanceCustomIndex = static_cast<uint32_t>(objectInd);

    meshInstance.instanceShaderBindingTableRecordOffset = 0;
    meshInstance.mask = 0xFF;
    m_meshInstances.push_back(meshInstance);
}

void VkRaytracing::recreateTLAS(size_t index, bool rebuild) {
    rtstructures::TLAS& t = m_tlas[index];

    // if rebuilding, recreate the instance buffer to reflect the new size
    if (rebuild) createTLASInstanceBuffer(t);

    // copy the new data into the instance buffer
    vkh::writeBuffer(t.instanceBuffer.mem, m_meshInstances.data(), m_meshInstances.size() * sizeof(VkAccelerationStructureInstanceKHR));

    // update the instance buffer device address
    t.geometry.geometry.instances.data.deviceAddress = vkh::bufferDeviceAddress(t.instanceBuffer.buf);

    // update the build info
    t.buildInfo.pGeometries = &t.geometry;
    t.buildInfo.mode = rebuild ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    t.buildInfo.srcAccelerationStructure = rebuild ? VK_NULL_HANDLE : t.as.v();
    t.buildInfo.dstAccelerationStructure = t.as.v();

    // update the buildRangeInfo
    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = static_cast<uint32_t>(m_meshInstances.size());
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;
    buildRangeInfo.firstVertex = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo = &buildRangeInfo;

    // rebuild and populate the TLAS
    VkhCommandBuffer commandBufferB = vkh::beginSingleTimeCommands(m_commandPool);
    vkhfp::vkCmdBuildAccelerationStructuresKHR(commandBufferB.v(), 1, &t.buildInfo, &pBuildRangeInfo);
    vkh::endSingleTimeCommands(commandBufferB, m_commandPool, m_gQueue);
}
}  // namespace raytracing
