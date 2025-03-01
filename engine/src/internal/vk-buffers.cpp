#include "vk-buffers.hpp"

namespace buffers {
void VkBuffers::init(VkhCommandPool commandPool, VkQueue gQueue, bool rtEnabled, uint32_t maxFrames, const scene::VkScene *scene) {
    m_scene = scene;

    m_commandPool = commandPool;
    m_gQueue = gQueue;
    m_rtEnabled = rtEnabled;
    m_maxFrames = maxFrames;
}

void VkBuffers::createBuffers(uint32_t currentFrame) {
    m_lightBuffers.resize(m_maxFrames);
    m_objInstanceBuffers.resize(m_maxFrames);
    m_camBuffers.resize(m_maxFrames);

    for (size_t i = 0; i < m_maxFrames; i++) {
        vkh::createHostVisibleBuffer(m_lightBuffers[i], sizeof(light::RawLights), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        vkh::createHostVisibleBuffer(m_objInstanceBuffers[i], sizeof(instancing::ObjectInstanceData), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        vkh::createHostVisibleBuffer(m_camBuffers[i], sizeof(cam::CamMatrices), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    // indirect commands buffer
    vkh::createDeviceLocalBuffer(m_sceneIndirectBuffer, m_scene->getUniqueObjectCount() * sizeof(VkDrawIndexedIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    updateSceneIndirectCommandsBuffer();

    // create texindices buffer
    vkh::createDeviceLocalBuffer(m_texIndicesBuffer, sizeof(texindices::TexIndices), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    createTexIndicesBuffer();

    update(currentFrame);
}

void VkBuffers::update(uint32_t currentFrame) {
    const cam::CamMatrices *camMatrices = m_scene->getCamMatrices();
    const light::LightDataObject *lightData = m_scene->getRawLightData();
    const instancing::ObjectInstance *objectInstances = m_scene->getObjectInstances();

    size_t lightCount = m_scene->getLightCount();
    size_t objectCount = m_scene->getObjectCount();

    if (lightCount > 0) {
        vkh::writeBuffer(m_lightBuffers[currentFrame].mem, lightData, sizeof(light::LightDataObject) * lightCount);
    }

    vkh::writeBuffer(m_camBuffers[currentFrame].mem, camMatrices, sizeof(cam::CamMatrices));
    vkh::writeBuffer(m_objInstanceBuffers[currentFrame].mem, objectInstances, sizeof(instancing::ObjectInstance) * objectCount);
}

void VkBuffers::createTexIndicesBuffer() {
    const texindices::TexIndexObj *texIndices = m_scene->getTexIndices();
    size_t size = sizeof(texindices::TexIndexObj) * m_scene->getObjectCount();

    vkh::BufferObj stagingBuffer{};
    vkh::createAndWriteHostBuffer(stagingBuffer, texIndices, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    vkh::copyBuffer(stagingBuffer.buf, m_texIndicesBuffer.buf, m_commandPool, m_gQueue, size);
}

void VkBuffers::updateSceneIndirectCommandsBuffer() {
    const VkDrawIndexedIndirectCommand *indirectCommands = m_scene->getSceneIndirectCommands();
    VkDeviceSize indirectBufferSize = m_scene->getUniqueObjectCount() * sizeof(VkDrawIndexedIndirectCommand);

    // create and copy staging buffer to the indirect commands buffer
    vkh::BufferObj stagingBuffer{};
    vkh::createAndWriteHostBuffer(stagingBuffer, indirectCommands, indirectBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    vkh::copyBuffer(stagingBuffer.buf, m_sceneIndirectBuffer.buf, m_commandPool, m_gQueue, indirectBufferSize);
}
}  // namespace buffers
